// Blynk Settings
#define BLYNK_TEMPLATE_ID "TMPL6fI-yrb2c"
#define BLYNK_TEMPLATE_NAME "Water Quality Monitor"
#define BLYNK_AUTH_TOKEN "A3hcFFknJ7uIOuVSAhWrqUlBiCSudGwb"

#include <Arduino.h>
#include <Wire.h>
#include <DHT.h>
#include <Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <BlynkSimpleEsp32.h>

// Pin Definitions
#define DHTPIN 15  
#define DHTTYPE DHT11 
#define TDS_PIN 34    
#define SERVO_PIN 13
#define LED_RED_PIN 4  

// MQTT Settings
const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic = "aquarium/sensors";
const char* mqtt_client_id = "ESP32_Aquarium_Tubes_Final_01"; 

// WiFi Credentials
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

// Forward Declarations
void executeFeeding();
void reconnectMQTT();
void readSensors();
void sendData();
void handleHeartbeatLED();

// --- LOGIKA LED HEARTBEAT ---
void handleHeartbeatLED() {
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 500) {
        lastBlink = millis();
        digitalWrite(LED_RED_PIN, !digitalRead(LED_RED_PIN));
    }
}

void reconnectMQTT() {
    if (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        if (client.connect(mqtt_client_id)) {
            Serial.println("connected");
        } else {
            Serial.print("failed, rc=");
            Serial.println(client.state());
        }
    }
}

void executeFeeding() {
    Serial.println("\n--- FEEDING COMMAND STARTED ---");
    // Proteksi: Jangan beri makan jika air kotor (TDS > 140)
    if (tdsValue > 140) {
        Serial.printf("Feeding cancelled. High TDS: %.1f ppm\n", tdsValue);
        Blynk.virtualWrite(V5, "Air Kotor! Pakan Batal.");
        return;
    }
    
    feedingProcess = true;
    Blynk.virtualWrite(V5, "Memberi pakan...");
    
    myServo.write(90);  
    delay(1000);        
    myServo.write(0);   
    
    feedingProcess = false;
    Blynk.virtualWrite(V5, "Selesai");
    Serial.println("Feeding Successful.");
    Serial.println("------------------------------\n");
}

void readSensors() {
    h = dht.readHumidity();
    t = dht.readTemperature();
    
    // Baca TDS
    int sensorValue = analogRead(TDS_PIN);
    float voltage = sensorValue * (3.3 / 4095.0);
    tdsValue = (133.42 * pow(voltage, 3) - 255.86 * pow(voltage, 2) + 857.39 * voltage) * 0.5;

    // Log ke Serial untuk lokal debugging
    if (isnan(h) || isnan(t)) {
        Serial.println("Failed to read from DHT sensor!");
    } else {
        Serial.printf("Sensor -> Temp: %.1fC | Hum: %.1f%% | TDS: %.1f ppm\n", t, h, tdsValue);
    }
}

void sendData() {
    // 1. Kirim ke MQTT (Hanya JSON murni agar Node-RED tidak error)
    if (client.connected() && !isnan(t) && !isnan(h)) {
        // Format String JSON tanpa spasi tambahan
        String payload = "{\"temp\":" + String(t, 1) + ",\"hum\":" + String(h, 1) + ",\"tds\":" + String(tdsValue, 1) + "}";
        client.publish(mqtt_topic, payload.c_str());
    }

    // 2. Kirim ke Blynk
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
    
    // Indikator Power On (5 detik)
    digitalWrite(LED_RED_PIN, HIGH);
    Serial.println("System Booting...");
    delay(5000); 
    
    dht.begin();
    myServo.attach(SERVO_PIN);
    myServo.write(0);
    
    // Inisialisasi Blynk dan MQTT
    Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);
    client.setServer(mqtt_server, 1883);
    
    Serial.println("Setup Complete.");
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