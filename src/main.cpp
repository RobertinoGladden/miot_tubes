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
#define DHTTYPE DHT22 
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

// Prototype Fungsi
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
    
    if (String(topic) == mqtt_topic_feed && message == "1") {
        if (!feedingProcess) executeFeeding();
    }
}

void reconnectMQTT() {
    if (!client.connected()) {
        if (client.connect(mqtt_client_id)) {
            client.subscribe(mqtt_topic_feed);
        }
    }
}

void executeFeeding() {
    Serial.println("\n--- PENGECEKAN KONDISI AIR ---");

    // LOGIKA BARU: Bersih jika berada di rentang 20 - 100 ppm
    if (tdsValue >= 20.0 && tdsValue <= 100.0) {
        Serial.printf("Air Bersih (%.1f ppm). Memulai MG995...\n", tdsValue);
        Blynk.virtualWrite(V5, "Air Bersih. Memberi Pakan...");
        
        feedingProcess = true;
        
        // Konfigurasi khusus MG995: Attach dengan range PWM standar
        myServo.attach(SERVO_PIN, 500, 2400); 
        delay(200); 

        // Gerakan Membuka
        myServo.write(90);  
        delay(1500); 
        
        // Gerakan Menutup
        myServo.write(0);   
        delay(800); // MG995 butuh waktu sedikit lebih lama untuk kembali karena berat

        myServo.detach(); // Lepas sinyal agar MG995 tidak bergetar/panas
        feedingProcess = false;
        
        Blynk.virtualWrite(V5, "Selesai");
        Serial.println("Proses Pakan Selesai.");
    } 
    else {
        Serial.printf("Pakan Dibatalkan: TDS %.1f ppm (Bukan 20-50 ppm)\n", tdsValue);
        Blynk.virtualWrite(V5, "Air Kotor/Tidak Sesuai! Batal.");
    }
    Serial.println("---------------------------\n");
}

void readSensors() {
    h = dht.readHumidity();
    t = dht.readTemperature();
    
    int sensorValue = analogRead(TDS_PIN);
    float voltage = sensorValue * (3.3 / 4095.0);
    tdsValue = (133.42 * pow(voltage, 3) - 255.86 * pow(voltage, 2) + 857.39 * voltage) * 0.5;

    if (isnan(h) || isnan(t)) {
        Serial.println("Gagal membaca sensor DHT22!");
    } else {
        Serial.printf("Update -> Suhu: %.1fC | TDS: %.1f ppm\n", t, tdsValue);
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
    
    // Setup PWM untuk MG995
    ESP32PWM::allocateTimer(0);
    myServo.setPeriodHertz(50); 
    
    dht.begin();
    Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
    
    Serial.println("System Ready.");
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