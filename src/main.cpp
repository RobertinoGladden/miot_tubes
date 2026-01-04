#define BLYNK_TEMPLATE_ID "TMPL6fI-yrb2c"
#define BLYNK_TEMPLATE_NAME "Water Quality Monitor"
#define BLYNK_AUTH_TOKEN "A3hcFFknJ7uIOuVSAhWrqUlBiCSudGwb"

#include <Arduino.h>
#include <Wire.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <BlynkSimpleEsp32.h>

#define DHTPIN 15  
#define DHTTYPE DHT11 
#define TDS_PIN 34    
#define SERVO_PIN 13
#define LED_RED_PIN 4  

const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic_sensor = "aquarium/sensors";
const char* mqtt_topic_feed = "aquarium/feed";
const char* mqtt_client_id = "ESP32_Aquarium_MIOT_Final"; 

const char* ssid = "onit";
const char* password = "robertino";

WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE);
Servo myServo;

float h = 0, t = 0, tdsValue = 0;
bool feedingProcess = false;
unsigned long previousMillis = 0;
const long interval = 3000; 

void executeFeeding();
void reconnectMQTT();
void readSensors();
void sendData();
void handleHeartbeatLED();
void callback(char* topic, byte* payload, unsigned int length);

void handleHeartbeatLED() {
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 500) {
        lastBlink = millis();
        digitalWrite(LED_RED_PIN, !digitalRead(LED_RED_PIN));
    }
}

void callback(char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (int i = 0; i < length; i++) message += (char)payload[i];
    
    Serial.printf("MQTT Command [%s]: %s\n", topic, message.c_str());

    if (String(topic) == mqtt_topic_feed && message == "1") {
        if (!feedingProcess) executeFeeding();
    }
}

void reconnectMQTT() {
    if (!client.connected()) {
        Serial.print("Mencoba koneksi MQTT...");
        if (client.connect(mqtt_client_id)) {
            Serial.println("Terhubung!");
            client.subscribe(mqtt_topic_feed);
        } else {
            Serial.printf("Gagal, rc=%d\n", client.state());
        }
    }
}

void executeFeeding() {
    Serial.println("\n--- MEMULAI URUTAN PAKAN ---");
    if (tdsValue > 80) {
        Serial.printf("Pakan Dibatalkan: TDS Tinggi (%.1f ppm)\n", tdsValue);
        Blynk.virtualWrite(V5, "Air Kotor! Pakan Batal.");
        return;
    }
    
    feedingProcess = true;
    Blynk.virtualWrite(V5, "Memberi pakan...");
    
    Serial.println("Servo: Bergerak ke 90 derajat...");
    myServo.write(90);  
    delay(1500); 
    
    Serial.println("Servo: Kembali ke 0 derajat...");
    myServo.write(0);   
    delay(500);
    
    feedingProcess = false;
    Blynk.virtualWrite(V5, "Selesai");
    Serial.println("Urutan Pakan Selesai.");
    Serial.println("---------------------------\n");
}

void readSensors() {
    h = dht.readHumidity();
    t = dht.readTemperature();
    
    int sensorValue = analogRead(TDS_PIN);
    float voltage = sensorValue * (3.3 / 4095.0);
    tdsValue = (133.42 * pow(voltage, 3) - 255.86 * pow(voltage, 2) + 857.39 * voltage) * 0.5;

    if (isnan(h) || isnan(t)) {
        Serial.println("Gagal membaca sensor DHT!");
    } else {
        Serial.printf("Sensor -> Suhu: %.1fC | Hum: %.1f%% | TDS: %.1f ppm\n", t, h, tdsValue);
    }
}

void sendData() {
    if (client.connected() && !isnan(t)) {
        String payload = "{\"temp\":" + String(t, 1) + ",\"hum\":" + String(h, 1) + ",\"tds\":" + String(tdsValue, 1) + "}";
        client.publish(mqtt_topic_sensor, payload.c_str());
    }

    Blynk.virtualWrite(V0, t);
    Blynk.virtualWrite(V1, h);
    Blynk.virtualWrite(V2, tdsValue);
}

BLYNK_WRITE(V4) {
    if (param.asInt() == 1 && !feedingProcess) {
        executeFeeding();
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_RED_PIN, OUTPUT);
    digitalWrite(LED_RED_PIN, HIGH);
    
    Serial.println("System Booting...");
    
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);
    myServo.setPeriodHertz(50);    
    
    myServo.attach(SERVO_PIN, 500, 2400); 
    myServo.write(0); 
    
    delay(5000); 
    
    dht.begin();
    
    Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
    
    Serial.println("Setup Selesai.");
}

void loop() {
    Blynk.run();
    client.loop();
    reconnectMQTT();
    handleHeartbeatLED();

    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        readSensors();
        sendData();
    }
}