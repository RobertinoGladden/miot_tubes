#define BLYNK_TEMPLATE_ID "TMPL6fI-yrb2c"
#define BLYNK_TEMPLATE_NAME "Water Quality Monitor"
#define BLYNK_AUTH_TOKEN "A3hcFFknJ7uIOuVSAhWrqUlBiCSudGwb"

#include <WiFi.h>
#include <PubSubClient.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <ESP32Servo.h>

// --- KONFIGURASI UTAMA ---
const long JEDA_PAKAN = 60000;       // 1 MENIT (60.000 ms)
const float BATAS_TDS_AMAN = 80.0;   // Batas TDS agar auto-feed berjalan

// --- PIN MAPPING ---
#define DHTPIN      15
#define DHTTYPE     DHT22
#define TDS_PIN     34
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
  
  // 1. Attach Servo
  servo.attach(SERVO_PIN, 500, 2400);
  delay(100); 
  
  // 2. Gerakan Servo
  servo.write(90);  delay(1500); // Buka
  servo.write(0);   delay(1000); // Tutup
  
  // 3. Detach Servo (Agar tidak berdengung/jitter saat diam)
  servo.detach();
  
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
    
    // --- UPDATE SERIAL MONITOR ---
    // Menampilkan status Temp & Hum dengan jelas sesuai permintaan
    long timeLeft = JEDA_PAKAN - (millis() - lastAutoFeed);
    if (timeLeft < 0) timeLeft = 0;

    Serial.println("------------------------------------------------");
    Serial.printf("Temp: %.1f°C | Hum: %.1f%% | TDS: %.0f ppm\n", t, h, tds);
    Serial.printf("Next Check in: %ld ms\n", timeLeft);

    // 3. LOGIKA PAKAN (TDS <= 80 & Timer)
    
    if (tds <= BATAS_TDS_AMAN) {
      // --- AIR BERSIH (<= 80) ---
      if (millis() - lastAutoFeed > JEDA_PAKAN || lastAutoFeed == 0) {
        Serial.println("[ACTION] Status: Air Bersih -> EXECUTE AUTO FEED.");
        executeFeeding();
        lastAutoFeed = millis(); 
      }
    } else {
      // --- AIR KOTOR (> 80) ---
      if (millis() - lastAutoFeed > JEDA_PAKAN) {
         Serial.println("[WARNING] Jadwal Pakan Tiba, TAPI DIBATALKAN (TDS > 80).");
         // lastAutoFeed = millis(); // Uncomment jika ingin men-skip jadwal ini
      }
    }
    Serial.println("------------------------------------------------");
  } else {
    Serial.println("Failed to read from DHT sensor!");
  }
}

void reconnectMQTT() {
  if (!mqtt.connected()) {
    if (mqtt.connect(mqtt_id)) {
      // Koneksi sukses
    }
  }
}

// --- SETUP & LOOP ---

void setup() {
  Serial.begin(115200);
  
  // Setup Servo & Sensor
  
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

// Tombol Manual Pakan (V4) - Tetap bisa override walau TDS tinggi
BLYNK_WRITE(V4) {
  if (param.asInt() == 1) {
    Serial.println("[MANUAL] Trigger via Blynk V4...");
    executeFeeding();
    lastAutoFeed = millis(); 
  }
}