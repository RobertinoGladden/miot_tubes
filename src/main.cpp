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

#define DHTPIN 15  
#define DHTTYPE DHT11 
#define TDS_PIN 34    
#define SERVO_PIN 13
#define LED_RED_PIN 4     // Satu-satunya LED Merah untuk status

const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic = "aquarium/sensors";
const char* mqtt_client_id = "ESP32_FishFeeder_Final_001"; // Pastikan ID Unik

WiFiClient espClient;
PubSubClient client(espClient);

const char* ssid = "onit";
const char* password = "robertino";

DHT dht(DHTPIN, DHTTYPE);
Servo myServo;

float h = 0, t = 0, tdsValue = 0;
bool feedingProcess = false;
unsigned long previousMillis = 0;
const long interval = 2000; 

// Forward Declarations
void executeFeeding();
void reconnectMQTT();
void readSensors();
void sendData();
void handleHeartbeatLED();

// --- LOGIKA LED HEARTBEAT ---
void handleHeartbeatLED() {
  static unsigned long lastBlink = 0;
  // Berkedip setiap 500ms sebagai tanda sistem berjalan sempurna
  if (millis() - lastBlink > 500) {
    lastBlink = millis();
    digitalWrite(LED_RED_PIN, !digitalRead(LED_RED_PIN));
  }
}

void reconnectMQTT() {
  static unsigned long lastAttempt = 0;
  if (!client.connected()) {
    unsigned long now = millis();
    if (now - lastAttempt > 5000) {
      lastAttempt = now;
      if (client.connect(mqtt_client_id)) {
        Serial.println("MQTT Connected");
      }
    }
  }
}

void executeFeeding() {
  if (tdsValue > 1000) {
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
}

void readSensors() {
  h = dht.readHumidity();
  t = dht.readTemperature();
  int sensorValue = analogRead(TDS_PIN);
  float voltage = sensorValue * (3.3 / 4095.0);
  tdsValue = (133.42 * pow(voltage, 3) - 255.86 * pow(voltage, 2) + 857.39 * voltage) * 0.5;
}

void sendData() {
  if (client.connected()) {
    String payload = "{\"temp\":" + String(t) + ",\"hum\":" + String(h) + ",\"tds\":" + String(tdsValue) + "}";
    client.publish(mqtt_topic, payload.c_str());
  }
  Blynk.virtualWrite(V0, t);
  Blynk.virtualWrite(V1, h);
  Blynk.virtualWrite(V2, tdsValue);
}

BLYNK_WRITE(V4) {
  if (param.asInt() == 1 && !feedingProcess) executeFeeding();
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_RED_PIN, OUTPUT);
  
  // LOGIKA AWAL: Nyala diam selama 5 detik
  digitalWrite(LED_RED_PIN, HIGH);
  Serial.println("System Booting...");
  delay(5000); 
  
  dht.begin();
  myServo.attach(SERVO_PIN);
  myServo.write(0);
  
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);
  client.setServer(mqtt_server, 1883);
}

void loop() {
  Blynk.run();
  reconnectMQTT();
  client.loop();
  
  // LED mulai berkedip setelah proses setup selesai
  handleHeartbeatLED();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    readSensors();
    sendData();
  }
}