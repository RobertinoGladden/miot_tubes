/* * PROGRAM MONITORING AKUARIUM SMART IOT
 * Fitur: Cek Suhu (DHT22), Kualitas Air (TDS), dan Feeder dengan MG995 + Relay
 * Logika: Relay memutus/menyambung daya eksternal agar ESP32 tidak terbakar
 */

// --- IDENTITAS BLYNK (Wajib di bagian paling atas) ---
#define BLYNK_TEMPLATE_ID "TMPL6fI-yrb2c"
#define BLYNK_TEMPLATE_NAME "Water Quality Monitor"
#define BBLYNK_AUTH_TOKEN "A3hcFFknJ7uIOuVSAhWrqUlBiCSudGwb"

// --- IMPORT LIBRARY ---
#include <Arduino.h>      // Library dasar Arduino
#include <Wire.h>         // Library komunikasi I2C
#include <DHT.h>          // Library untuk sensor suhu/kelembapan
#include <ESP32Servo.h>   // Library khusus servo untuk ESP32 (mendukung PWM)
#include <WiFi.h>         // Library koneksi WiFi
#include <PubSubClient.h> // Library untuk protokol MQTT
#include <BlynkSimpleEsp32.h> // Library utama Blynk IoT

// --- DEFINISI PIN ---
#define DHTPIN 15         // Pin data sensor DHT22
#define DHTTYPE DHT22     // Memberitahu program kita pakai DHT22 (bukan DHT11)
#define TDS_PIN 34        // Pin AnalogRead untuk sensor TDS
#define RELAY_PIN 13      // Pin kontrol Relay (Saklar Daya)
#define SERVO_PIN 14      // Pin sinyal PWM untuk MG995
#define LED_RED_PIN 4     // Pin LED indikator sistem

// --- KONFIGURASI JARINGAN ---
const char* mqtt_server = "broker.hivemq.com"; // Alamat server broker MQTT
const char* mqtt_topic_sensor = "aquarium/sensors"; // Jalur kirim data sensor
const char* mqtt_topic_feed = "aquarium/feed";     // Jalur terima perintah pakan
const char* mqtt_client_id = "ESP32_Aquarium_MIOT_Final"; 

const char* ssid = "onit";           // Nama WiFi Anda
const char* password = "robertino";   // Password WiFi Anda

// --- INISIALISASI OBJEK ---
WiFiClient espClient;           // Membuat klien WiFi
PubSubClient client(espClient); // Membuat klien MQTT berbasis WiFi
DHT dht(DHTPIN, DHTTYPE);       // Inisialisasi sensor suhu
Servo myServo;                  // Membuat objek kontrol motor servo

// --- VARIABEL PENYIMPAN DATA ---
float h = 0, t = 0, tdsValue = 0; // Wadah angka pecahan (float) untuk sensor
bool feedingProcess = false;       // Status: true jika pakan sedang jalan
unsigned long previousMillis = 0;  // Pengingat waktu terakhir kirim data
const long interval = 3000;        // Jeda waktu antar pembacaan (3 detik)

// --- FUNGSI HEARTBEAT (KEDIP LED) ---
void handleHeartbeatLED() {
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 500) { // Jika sudah lewat 500ms
        lastBlink = millis();         // Perbarui waktu terakhir
        digitalWrite(LED_RED_PIN, !digitalRead(LED_RED_PIN)); // Balik status LED (On/Off)
    }
}

// --- FUNGSI MQTT CALLBACK (TERIMA PERINTAH) ---
void callback(char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (int i = 0; i < length; i++) message += (char)payload[i]; // Rakit pesan masuk
    
    // Jika pesan "1" masuk ke topik feed, jalankan pakan
    if (String(topic) == mqtt_topic_feed && message == "1") {
        if (!feedingProcess) executeFeeding();
    }
}

// --- FUNGSI REKONEKSI MQTT ---
void reconnectMQTT() {
    if (!client.connected()) { // Jika koneksi putus
        if (client.connect(mqtt_client_id)) { // Coba hubungkan ulang
            client.subscribe(mqtt_topic_feed); // Daftar ulang ke topik perintah
        }
    }
}

// --- FUNGSI UTAMA: PEMBERIAN PAKAN ---
void executeFeeding() {
    Serial.println("\n--- CEK TDS SEBELUM PAKAN ---");

    // Syarat: Air harus bersih (20 - 100 ppm)
    if (tdsValue >= 20.0 && tdsValue <= 100.0) {
        Serial.printf("Kondisi OK (%.1f ppm). Relay Aktif!\n", tdsValue);
        Blynk.virtualWrite(V5, "Memberi Pakan..."); // Update status di Blynk
        
        feedingProcess = true; // Kunci proses (lock)

        // 1. HIDUPKAN RELAY (Menyalakan aliran listrik dari charger ke MG995)
        digitalWrite(RELAY_PIN, LOW); // LOW biasanya mengaktifkan modul relay
        delay(500); // Tunggu listrik stabil

        // 2. AKTIFKAN SINYAL SERVO
        myServo.attach(SERVO_PIN, 500, 2400); // Kirim pulsa PWM ke servo
        delay(100); 

        // 3. GERAKAN SERVO (BUKA - TUTUP)
        myServo.write(90);  // Putar ke 90 derajat (Membuka)
        delay(1500);        // Jeda pakan turun
        myServo.write(0);   // Putar ke 0 derajat (Menutup)
        delay(1000);        // Tunggu servo selesai berputar

        // 4. MATIKAN DAYA (KEAMANAN)
        myServo.detach();             // Hentikan sinyal data
        digitalWrite(RELAY_PIN, HIGH); // Putus arus listrik (Relay OFF)
        
        feedingProcess = false;        // Buka kunci proses (unlock)
        Blynk.virtualWrite(V5, "Selesai");
        Serial.println("Pakan Selesai. Daya MG995 Diputus.");
    } 
    else {
        // Jika TDS di luar rentang, pakan ditolak
        Serial.printf("Batal! TDS %.1f ppm Tidak Sesuai.\n", tdsValue);
        Blynk.virtualWrite(V5, "TDS Jelek! Pakan Batal.");
    }
}

// --- FUNGSI BACA SENSOR ---
void readSensors() {
    h = dht.readHumidity();      // Ambil data kelembapan udara
    t = dht.readTemperature();   // Ambil data suhu air/udara
    
    int sensorValue = analogRead(TDS_PIN); // Baca nilai analog (0-4095)
    float voltage = sensorValue * (3.3 / 4095.0); // Ubah ke voltase (ESP32 = 3.3V)
    
    // Rumus konversi voltase ke kepekatan zat air (PPM)
    tdsValue = (133.42 * pow(voltage, 3) - 255.86 * pow(voltage, 2) + 857.39 * voltage) * 0.5;

    if (isnan(h) || isnan(t)) {
        Serial.println("Error: Sensor DHT22 tidak terbaca!");
    }
}

// --- FUNGSI KIRIM DATA KE CLOUD ---
void sendData() {
    // Kirim ke MQTT dalam format teks JSON
    if (client.connected() && !isnan(t)) {
        String payload = "{\"temp\":" + String(t, 1) + ",\"tds\":" + String(tdsValue, 1) + "}";
        client.publish(mqtt_topic_sensor, payload.c_str());
    }
    // Kirim ke Blynk Dashboard
    Blynk.virtualWrite(V0, t);        // Gauge Suhu
    Blynk.virtualWrite(V1, h);        // Gauge Kelembapan
    Blynk.virtualWrite(V2, tdsValue); // Gauge TDS
}

// --- TERIMA INPUT TOMBOL DARI BLYNK ---
BLYNK_WRITE(V4) { // Pin V4 di aplikasi adalah tombol pakan
    if (param.asInt() == 1 && !feedingProcess) { // Jika ditekan dan sedang tidak sibuk
        executeFeeding(); // Jalankan fungsi pakan
    }
}

// --- SETUP (JALAN 1 KALI SAAT NYALA) ---
void setup() {
    Serial.begin(115200); // Mulai monitor serial untuk debug
    
    pinMode(RELAY_PIN, OUTPUT);     // Set pin relay sebagai output
    digitalWrite(RELAY_PIN, HIGH);  // Pastikan relay mati di awal (HIGH = OFF)
    pinMode(LED_RED_PIN, OUTPUT);   // Set pin LED sebagai output
    
    ESP32PWM::allocateTimer(0);     // Alokasi memori PWM untuk servo
    myServo.setPeriodHertz(50);     // Frekuensi servo standar 50Hz
    
    dht.begin(); // Mulai sensor suhu
    
    // Memulai koneksi ke server Blynk dan WiFi
    Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);
    
    // Set alamat server MQTT
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback); // Tentukan fungsi untuk terima perintah
    
    Serial.println("Sistem Siap! Menunggu Sensor...");
}

// --- LOOP (JALAN TERUS MENERUS) ---
void loop() {
    Blynk.run();       // Jalankan layanan Blynk
    client.loop();     // Jalankan layanan MQTT
    reconnectMQTT();   // Pastikan MQTT selalu terhubung
    handleHeartbeatLED(); // Berkedip sebagai tanda sistem hidup

    unsigned long currentMillis = millis(); // Ambil waktu sekarang
    if (currentMillis - previousMillis >= interval) { // Jika sudah 3 detik
        previousMillis = currentMillis; // Reset pengingat waktu
        readSensors(); // Ambil data sensor terbaru
        sendData();    // Kirim data ke aplikasi
    }
}