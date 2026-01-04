/* * KODE MONITORING KUALITAS AIR & FEEDER OTOMATIS
 * Menggunakan: ESP32, DHT22, TDS Meter, dan Servo MG995
 */

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

// --- KONFIGURASI PIN ---
#define DHTPIN 15           // Pin data sensor Suhu DHT22
#define DHTTYPE DHT22       // Menentukan jenis sensor DHT yang dipakai
#define TDS_PIN 34          // Pin analog untuk sensor TDS (ADC)
#define SERVO_PIN 13        // Pin sinyal (PWM) untuk servo MG995
#define LED_RED_PIN 4       // Pin LED sebagai indikator sistem hidup

// --- KONFIGURASI MQTT & WIFI ---
const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic_sensor = "aquarium/sensors";
const char* mqtt_topic_feed = "aquarium/feed";
const char* mqtt_client_id = "ESP32_Aquarium_MIOT_Final"; 

const char* ssid = "onit";            // Nama WiFi Anda
const char* password = "robertino";    // Password WiFi Anda

// --- INISIALISASI OBJEK ---
WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE);
Servo myServo;

// --- VARIABEL GLOBAL ---
float h = 0, t = 0, tdsValue = 0;
bool feedingProcess = false;       // Penanda jika servo sedang bergerak
unsigned long previousMillis = 0;  // Untuk pengatur waktu non-blocking
const long interval = 3000;        // Jeda pembacaan sensor (3 detik)

// --- PROTOTIPE FUNGSI ---
void executeFeeding();
void reconnectMQTT();
void readSensors();
void sendData();
void handleHeartbeatLED();
void callback(char* topic, byte* payload, unsigned int length);

// Fungsi membuat LED berkedip (Heartbeat) agar kita tahu ESP32 tidak hang
void handleHeartbeatLED() {
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 500) {
        lastBlink = millis();
        digitalWrite(LED_RED_PIN, !digitalRead(LED_RED_PIN));
    }
}

// Fungsi yang berjalan otomatis jika ada pesan masuk dari MQTT (Broker)
void callback(char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (int i = 0; i < length; i++) message += (char)payload[i];
    
    // Jika ada perintah "1" masuk ke topik aquarium/feed, jalankan pakan
    if (String(topic) == mqtt_topic_feed && message == "1") {
        if (!feedingProcess) executeFeeding();
    }
}

// Fungsi untuk menghubungkan ulang ke Broker MQTT jika terputus
void reconnectMQTT() {
    if (!client.connected()) {
        if (client.connect(mqtt_client_id)) {
            client.subscribe(mqtt_topic_feed); // Langganan topik perintah pakan
        }
    }
}

// LOGIKA UTAMA: Pemberian Pakan
void executeFeeding() {
    Serial.println("\n--- PENGECEKAN KONDISI AIR ---");

    // SYARAT AMBANG BATAS: Hanya memberi pakan jika TDS 20 - 100 ppm
    if (tdsValue >= 20.0 && tdsValue <= 100.0) {
        Serial.printf("Air Bersih (%.1f ppm). Memulai MG995...\n", tdsValue);
        Blynk.virtualWrite(V5, "Air Bersih. Memberi Pakan...");
        
        feedingProcess = true; // Kunci proses agar tidak tumpang tindih
        
        // Hubungkan sinyal ke Servo MG995
        myServo.attach(SERVO_PIN, 500, 2400); 
        delay(200); 

        // Gerakan Membuka (90 derajat)
        myServo.write(90);  
        delay(1500); 
        
        // Gerakan Menutup kembali ke 0
        myServo.write(0);   
        delay(800); 

        // Putus Sinyal (Detach) agar servo tidak menarik arus besar saat diam
        myServo.detach(); 
        feedingProcess = false;
        
        Blynk.virtualWrite(V5, "Selesai");
        Serial.println("Proses Pakan Selesai.");
    } 
    else {
        // Jika air kotor, pakan dibatalkan demi kesehatan ikan
        Serial.printf("Pakan Dibatalkan: TDS %.1f ppm (Bukan 20-100 ppm)\n", tdsValue);
        Blynk.virtualWrite(V5, "Air Kotor/Tidak Sesuai! Batal.");
    }
    Serial.println("---------------------------\n");
}

// Fungsi Membaca Sensor (DHT22 & TDS)
void readSensors() {
    h = dht.readHumidity();      // Baca Kelembapan
    t = dht.readTemperature();   // Baca Suhu Celsius
    
    // Membaca nilai analog dari TDS Meter (0-4095)
    int sensorValue = analogRead(TDS_PIN);
    // Konversi ADC ke Voltase (ESP32 = 3.3V)
    float voltage = sensorValue * (3.3 / 4095.0);
    // Rumus polinomial untuk mendapatkan nilai PPM dari voltase
    tdsValue = (133.42 * pow(voltage, 3) - 255.86 * pow(voltage, 2) + 857.39 * voltage) * 0.5;

    if (isnan(h) || isnan(t)) {
        Serial.println("Gagal membaca sensor DHT22!");
    } else {
        Serial.printf("Update -> Suhu: %.1fC | TDS: %.1f ppm\n", t, tdsValue);
    }
}

// Fungsi mengirim data ke Blynk (Virtual Pin) dan MQTT (JSON)
void sendData() {
    if (client.connected() && !isnan(t)) {
        // Format data ke JSON untuk MQTT
        String payload = "{\"temp\":" + String(t, 1) + ",\"hum\":" + String(h, 1) + ",\"tds\":" + String(tdsValue, 1) + "}";
        client.publish(mqtt_topic_sensor, payload.c_str());
    }
    
    // Kirim data ke Aplikasi Blynk di HP
    Blynk.virtualWrite(V0, t);        // V0 untuk Suhu
    Blynk.virtualWrite(V1, h);        // V1 untuk Kelembapan
    Blynk.virtualWrite(V2, tdsValue); // V2 untuk TDS
}

// Fungsi yang jalan saat tombol di Aplikasi Blynk (Virtual Pin 4) ditekan
BLYNK_WRITE(V4) {
    if (param.asInt() == 1 && !feedingProcess) {
        executeFeeding();
    }
}

void setup() {
    Serial.begin(115200);      // Komunikasi Serial ke PC
    pinMode(LED_RED_PIN, OUTPUT);
    
    // Pengaturan PWM untuk Servo pada ESP32
    ESP32PWM::allocateTimer(0);
    myServo.setPeriodHertz(50); 
    
    dht.begin();               // Aktifkan sensor suhu
    
    // Hubungkan ke Server Blynk
    Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);
    
    // Hubungkan ke Server MQTT
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
    
    Serial.println("System Ready.");
}

void loop() {
    Blynk.run();               // Menjaga koneksi Blynk
    client.loop();             // Menjaga koneksi MQTT
    reconnectMQTT();           // Cek koneksi internet/MQTT
    handleHeartbeatLED();      // Kedipkan LED indikator

    // Timer pembacaan sensor (setiap 3 detik)
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        readSensors();         // Ambil data sensor
        sendData();            // Kirim data ke awan/cloud
    }
}