#define BLYNK_TEMPLATE_ID "TMPL6fI-yrb2c"
#define BLYNK_TEMPLATE_NAME "Water Quality Monitor"
#define BLYNK_AUTH_TOKEN "A3hcFFknJ7uIOuVSAhWrqUlBiCSudGwb"

#include <WiFi.h>
#include <PubSubClient.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <ESP32Servo.h>

// --- KONFIGURASI UTAMA ---
const float BATAS_TDS_KOTOR = 80.0; // Jika TDS > 80, dianggap kotor
const long JEDA_PAKAN = 60000;       // 1 MENIT (60.000 ms) jeda antar pakan otomatis

// --- PIN MAPPING ---
#define DHTPIN      15
#define DHTTYPE     DHT22
#define TDS_PIN     34
#define RELAY_PIN   13
#define SERVO_PIN   14

// --- NETWORK ---
const char* ssid = "onit";
const char* pass = "robertino";
const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_id = "ESP32_Aquarium_Hybrid";

// --- OBJECTS ---
WiFiClient espClient;
PubSubClient mqtt(espClient);
DHT dht(DHTPIN, DHTTYPE);
Servo servo;

// --- VARIABLES ---
unsigned long lastTime = 0;
unsigned long lastAutoFeed = 0; 
bool isFeeding = false;

// --- FUNGSI UTAMA ---

void executeFeeding() {
  if (isFeeding) return; // Cegah tumpang tindih
  isFeeding = true;

  Serial.println(">> MEMBERI PAKAN...");
  
  // 1. Power ON Servo (Lewat Relay)
  digitalWrite(RELAY_PIN, HIGH);
  servo.attach(SERVO_PIN, 500, 2400);
  delay(200);
  
  // 2. Gerakan Servo
  servo.write(90);  delay(1500); // Buka
  servo.write(0);   delay(1000); // Tutup
  
  // 3. Power OFF Servo (Hemat daya & anti getar)
  servo.detach();
  digitalWrite(RELAY_PIN, LOW);
  
  isFeeding = false;
  Serial.println(">> SELESAI.");
}

void handleSensors() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  
  // Baca & Hitung TDS
  float rawV = analogRead(TDS_PIN) * (3.3 / 4095.0);
  float tds = (133.42 * pow(rawV, 3) - 255.86 * pow(rawV, 2) + 857.39 * rawV) * 0.5;

  if (!isnan(t)) {
    // 1. Kirim ke MQTT
    if (mqtt.connected()) {
      char payload[100];
      snprintf(payload, sizeof(payload), "{\"temp\":%.1f,\"hum\":%.1f,\"tds\":%.1f}", t, h, tds);
      mqtt.publish("aquarium/sensors", payload);
    }
    
    // 2. Kirim ke Blynk (V0, V1, V2)
    Blynk.virtualWrite(V0, t);
    Blynk.virtualWrite(V1, h);
    Blynk.virtualWrite(V2, tds);
    
    Serial.printf("T:%.1f | TDS:%.0f | Next Feed in: %ld ms\n", t, tds, (JEDA_PAKAN - (millis() - lastAutoFeed)));

    // 3. LOGIKA OTOMATIS (Air Kotor -> Pakan)
    // Syarat: TDS Tinggi DAN (Sudah lewat 1 menit ATAU belum pernah pakan sama sekali)
    if (tds > BATAS_TDS_KOTOR) {
      if (millis() - lastAutoFeed > JEDA_PAKAN || lastAutoFeed == 0) {
        Serial.println("KONDISI: Air Kotor Terdeteksi & Cooldown Selesai -> Trigger Pakan.");
        executeFeeding();
        lastAutoFeed = millis(); // Reset timer jeda
      } else {
        Serial.println("INFO: TDS Tinggi, tapi masih menunggu jeda 1 menit...");
      }
    }
  }
}

void reconnectMQTT() {
  if (!mqtt.connected()) {
    if (mqtt.connect(mqtt_id)) {
      // Koneksi sukses, tidak perlu subscribe untuk mode ini
    }
  }
}

// --- SETUP & LOOP ---

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Pastikan Relay Off saat boot
  
  dht.begin();
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  mqtt.setServer(mqtt_server, 1883);
}

void loop() {
  Blynk.run();
  
  if (!mqtt.connected()) reconnectMQTT();
  mqtt.loop();

  // Interval baca sensor 3 detik
  if (millis() - lastTime >= 3000) {
    lastTime = millis();
    handleSensors();
  }
}

// --- BLYNK CONTROL ---

// Tombol Manual Pakan (V4)
BLYNK_WRITE(V4) {
  if (param.asInt() == 1) {
    Serial.println("Trigger Manual via Blynk V4");
    executeFeeding();
    // Opsional: Reset timer otomatis juga agar tidak double feed
    lastAutoFeed = millis(); 
  }
}