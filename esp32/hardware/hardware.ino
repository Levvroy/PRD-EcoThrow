/*
 *  EcoThrow
 *  Kelompok 9 · PRD K51 · STEI-K ITB 2025
 *  ─────────────────────────────────────────────────────────
 *  Pin Mapping:
 *  HC-SR04 #1 (Sensor Atas Bin - Level)
 *    TRIG → GPIO 5
 *    ECHO → Voltage Divider (1K+10K) → GPIO 18
 *
 *  HC-SR04 #2 (Sensor Depan Bin - Presence)
 *    TRIG → GPIO 19
 *    ECHO → Voltage Divider (1K+10K) → GPIO 23
 *
 *  LCD 1602 I2C
 *    SDA  → GPIO 21
 *    SCL  → GPIO 22
 *    VCC  → Rail 5V
 *    GND  → Rail GND
 *
 *  LED Biru  (Bin Normal)   → GPIO 2  → Resistor 1K → Anoda LED
 *  LED Merah (Bin Hampir Penuh) → GPIO 4  → Resistor 1K → Anoda LED
 *
 *  Library yang diperlukan (Arduino IDE Library Manager):
 *    - LiquidCrystal I2C  by Frank de Brabander
 *    - ArduinoJson         by Benoit Blanchon
 *    - WiFi.h & HTTPClient.h sudah built-in ESP32 package
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

//  KONFIGURASI - Ubah bagian ini sesuai environment kamu
// WiFi
const char* WIFI_SSID     = "ITB IoT";
const char* WIFI_PASSWORD = "";

// Server FastAPI - ganti IP dengan IP laptop kamu (cek: ipconfig)
const char* SERVER_BASE       = "http://192.168.0.1:8000";
const char* URL_UPDATE_BIN    = "http://192.168.0.1:8000/api/bin/update";
const char* URL_THROW         = "http://192.168.0.1:8000/api/throw";
const char* URL_PENDING_THROW = "http://192.168.0.1:8000/api/pending_throw";
const char* URL_LEADERBOARD   = "http://192.168.0.1:8000/api/leaderboard?limit=1";

// Identitas bin ini - ganti dengan bin_id dari database
const char* BIN_ID = "44208e55-d0e2-487d-9a81-f0f2f28b2468"; // GKU 2

// Dimensi fisik bin (ukur fisik sebelum deploy)
const float BIN_HEIGHT_CM = 25.0;  // Tinggi dalam bin dalam cm

// Threshold level bin (persen)
const float THRESHOLD_WARN = 75.0;  // LED Merah menyala
const float THRESHOLD_FULL = 90.0;  // Notifikasi "Hampir Penuh"

// Threshold anti-cheat: perubahan level minimum agar throw dihitung
const float THROW_MIN_DELTA = 2.0;  // persen (sesuai backend validasi)

// Time-gate anti-cheat: jeda minimum setelah throw (ms)
const unsigned long TIME_GATE_MS = 10000; 

// Interval operasi (ms)
const unsigned long INTERVAL_BACA_SENSOR   = 3000;   // baca sensor setiap 3 detik
const unsigned long INTERVAL_UPDATE_SERVER = 30000;  // kirim bin/update setiap 30 detik
const unsigned long INTERVAL_UPDATE_LCD    = 10000;  // refresh LCD setiap 10 detik
const unsigned long INTERVAL_POLL_QR       = 5000;   // poll pending throw setiap 5 detik
const unsigned long INTERVAL_WIFI_RETRY    = 10000;  // retry WiFi jika putus

//  PIN DEFINITIONS
// Sensor HC-SR04 #1 (Atas Bin - ukur level)
const int TRIG1 = 5;
const int ECHO1 = 18;

// Sensor HC-SR04 #2 (Depan Bin - deteksi kehadiran orang)
const int TRIG2 = 19;
const int ECHO2 = 23;

// LED Indikator
const int LED_BIRU  = 2;  // Bin normal (level < THRESHOLD_WARN)
const int LED_MERAH = 4;  // Bin hampir penuh (level >= THRESHOLD_WARN)

//  INISIALISASI LCD
//  Jika karakter tidak muncul, coba ganti 0x27 menjadi 0x3F
//  (jalankan I2C Scanner untuk cek address yang benar)
LiquidCrystal_I2C lcd(0x27, 16, 2);

//  STATE VARIABLES
float kapasitasSebelumnya  = 0.0;
float kapasitasSaatIni     = 0.0;
bool  presenceDetected     = false;   // apakah ada orang di depan bin
bool  awaitingThrow        = false;   // sudah dapat pending QR, tunggu sampah masuk
String pendingQrCode       = "";      // QR code user yang sedang menunggu (dari server)

unsigned long lastBacaSensor   = 0;
unsigned long lastUpdateServer = 0;
unsigned long lastUpdateLcd    = 0;
unsigned long lastPollQr       = 0;
unsigned long lastWifiRetry    = 0;
unsigned long lastThrowTime    = 0;   // untuk time-gate anti-cheat

String topLeaderboardName  = "---";  // nama top-1 leaderboard (update berkala)
unsigned long lastUpdateLeaderboard = 0;
const unsigned long INTERVAL_UPDATE_LEADERBOARD = 60000; // update tiap 1 menit

//  FUNGSI UTILITAS
/**
 * Baca jarak sensor HC-SR04 dalam cm.
 * Timeout 30ms untuk hindari hang/error jika tidak ada echo.
 */
float bacaJarak(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long durasi = pulseIn(echo, HIGH, 30000); // timeout 30ms
  if (durasi == 0) return -1.0; // tidak ada echo / objek terlalu jauh
  return durasi * 0.034 / 2.0;
}

/**
 * Hitung persentase level bin dari jarak sensor atas.
 * Jarak = 0 cm  → bin penuh (100%)
 * Jarak = BIN_HEIGHT_CM → bin kosong (0%)
 */
float hitungKapasitas(float jarakCm) {
  if (jarakCm < 0) return kapasitasSebelumnya; // gunakan nilai sebelumnya jika error
  float kapasitas = ((BIN_HEIGHT_CM - jarakCm) / BIN_HEIGHT_CM) * 100.0;
  kapasitas = constrain(kapasitas, 0.0, 100.0);
  return kapasitas;
}

/**
 * Update LED indikator sesuai level bin.
 * Biru  = normal  (level < THRESHOLD_WARN)
 * Merah = hampir penuh (level >= THRESHOLD_WARN)
 */
void updateLED(float kapasitas) {
  if (kapasitas >= THRESHOLD_WARN) {
    digitalWrite(LED_MERAH, HIGH);
    digitalWrite(LED_BIRU,  LOW);
  } else {
    digitalWrite(LED_BIRU,  HIGH);
    digitalWrite(LED_MERAH, LOW);
  }
}

/**
 * Reconnect WiFi jika terputus.
 * Non-blocking: hanya mencoba jika interval sudah lewat.
 */
void reconnectWiFiIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) return;

  unsigned long now = millis();
  if (now - lastWifiRetry < INTERVAL_WIFI_RETRY) return;
  lastWifiRetry = now;

  Serial.println("[WiFi] Koneksi terputus. Mencoba reconnect...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Reconnecting...");

  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Tunggu maksimal 5 detik
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 5000) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Terhubung kembali. IP: ");
    Serial.println(WiFi.localIP());
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi OK!");
    delay(1500);
  } else {
    Serial.println("[WiFi] Gagal reconnect.");
  }
}

//  FUNGSI HTTP - UPDATE BIN CAPACITY

/**
 * Kirim update level bin ke server (POST /api/bin/update).
 * Dipanggil setiap INTERVAL_UPDATE_SERVER.
 */
void updateKapasitasBin(float capacity_pct) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(URL_UPDATE_BIN);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["bin_id"]       = BIN_ID;
  doc["capacity_pct"] = capacity_pct;

  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  if (code > 0) {
    Serial.printf("[HTTP] bin/update → %d\n", code);
  } else {
    Serial.printf("[HTTP] bin/update gagal: %s\n", http.errorToString(code).c_str());
  }
  http.end();
}

//  FUNGSI HTTP - KIRIM EVENT THROW
/**
 * Kirim event throw ke server (POST /api/throw).
 * Dipanggil hanya setelah presence + perubahan level terdeteksi
 * DAN sudah ada pendingQrCode dari polling.
 */
void kirimEventThrow(String qr_code, float level_before, float level_after) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(URL_THROW);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["user_qr_code"]    = qr_code;
  doc["bin_id"]          = BIN_ID;
  doc["bin_level_before"] = level_before;
  doc["bin_level_after"]  = level_after;

  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  if (code == 200 || code == 201) {
    // Parse respons untuk tampilkan di LCD
    String respStr = http.getString();
    JsonDocument resp;
    if (deserializeJson(resp, respStr) == DeserializationError::Ok) {
      int xpEarned  = resp["xp_earned"]  | 0;
      String level  = resp["new_level"]  | "?";
      int streak    = resp["streak_days"] | 0;

      Serial.printf("[THROW] Berhasil! +%d XP | Level: %s | Streak: %d hari\n",
                    xpEarned, level.c_str(), streak);

      // Tampilkan hasil di LCD selama 5 detik
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("+");
      lcd.print(xpEarned);
      lcd.print(" XP!");
      lcd.setCursor(0, 1);
      lcd.print(level);
      lcd.print(" S:");
      lcd.print(streak);
      lcd.print("d");
      delay(5000);
    } else {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Throw Sukses!");
      delay(3000);
    }
  } else {
    Serial.printf("[HTTP] throw gagal: %d\n", code);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Throw Error");
    lcd.setCursor(0, 1);
    lcd.print("Code: ");
    lcd.print(code);
    delay(3000);
  }
  http.end();
}

//  FUNGSI HTTP - POLL PENDING THROW (QR Code User)
/**
 * Poll endpoint pending_throw untuk mendapatkan QR code user
 * yang sudah trigger dari website dan sedang menunggu di depan bin.
 *
 * Endpoint yang diharapkan: GET /api/pending_throw?bin_id=BIN_ID
 * Respons jika ada:   { "qr_code": "ECO-13525136-XXXXXX", "nim": "..." }
 * Respons jika kosong: { "qr_code": null } atau 404
 */
void pollPendingThrow() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(SERVER_BASE) + "/api/pending_throw?bin_id=" + String(BIN_ID);
  http.begin(url);
  http.setTimeout(3000); // timeout singkat agar loop tidak terhambat

  int code = http.GET();
  if (code == 200) {
    String respStr = http.getString();
    JsonDocument resp;
    if (deserializeJson(resp, respStr) == DeserializationError::Ok) {
      const char* qr = resp["qr_code"];
      if (qr && strlen(qr) > 0) {
        pendingQrCode  = String(qr);
        awaitingThrow  = true;
        Serial.printf("[POLL] Ada pending throw: %s\n", pendingQrCode.c_str());

        // Tampilkan instruksi di LCD
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Siap! Buang");
        lcd.setCursor(0, 1);
        lcd.print("sampah sekarang");
      } else {
        // Tidak ada user menunggu
        awaitingThrow = false;
        pendingQrCode = "";
      }
    }
  } else if (code == 404) {
    awaitingThrow = false;
    pendingQrCode = "";
  }
  http.end();
}

//  FUNGSI HTTP - UPDATE TOP LEADERBOARD (untuk LCD F4)
/**
 * Ambil nama top-1 leaderboard dari server untuk ditampilkan di LCD.
 */
void updateTopLeaderboard() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(URL_LEADERBOARD);
  http.setTimeout(3000);

  int code = http.GET();
  if (code == 200) {
    String respStr = http.getString();
    JsonDocument resp;
    if (deserializeJson(resp, respStr) == DeserializationError::Ok) {
      // Respons: array of users, ambil index 0
      if (resp.is<JsonArray>() && resp.as<JsonArray>().size() > 0) {
        const char* nama = resp[0]["name"];
        if (nama) {
          // Potong nama maksimal 9 karakter agar muat di LCD 16 char
          topLeaderboardName = String(nama).substring(0, 9);
          Serial.printf("[LCD] Top-1: %s\n", topLeaderboardName.c_str());
        }
      }
    }
  }
  http.end();
}

//  FUNGSI LCD - UPDATE TAMPILAN UTAMA
/**
 * Update tampilan LCD dengan info level bin + top leaderboard.
 *
 * Baris 0: "Isi: XX% [status]"
 * Baris 1: "#1: NamaTerpotong"
 *
 * Status: "OK" / "WARN" / "FULL"
 */
void updateLCDUtama(float kapasitas) {
  // Tentukan status
  String status;
  if      (kapasitas >= THRESHOLD_FULL) status = "FULL";
  else if (kapasitas >= THRESHOLD_WARN) status = "WARN";
  else                                   status = "OK";

  // Baris 0: Isi bin
  lcd.setCursor(0, 0);
  lcd.print("Isi:");
  lcd.print((int)kapasitas);
  lcd.print("% ");
  lcd.print(status);
  lcd.print("   "); // bersihkan sisa karakter lama

  // Baris 1: Top leaderboard (F4 Social Visibility)
  lcd.setCursor(0, 1);
  lcd.print("#1:");
  lcd.print(topLeaderboardName);
  lcd.print("       "); // bersihkan sisa
}

//  SETUP
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== EcoThrow - Starting ===");

  // Setup pin sensor #1
  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);

  // Setup pin sensor #2
  pinMode(TRIG2, OUTPUT);
  pinMode(ECHO2, INPUT);

  // Setup LED
  pinMode(LED_BIRU,  OUTPUT);
  pinMode(LED_MERAH, OUTPUT);
  digitalWrite(LED_BIRU,  LOW);
  digitalWrite(LED_MERAH, LOW);

  // Setup LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("EcoThrow v1.0");
  lcd.setCursor(0, 1);
  lcd.print("Connecting...");

  // Koneksi WiFi (blocking saat boot)
  Serial.printf("[WiFi] Menghubungkan ke: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Terhubung! IP: ");
    Serial.println(WiFi.localIP());
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi OK!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
  } else {
    Serial.println("[WiFi] GAGAL terhubung. Sistem akan retry otomatis.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi GAGAL");
    lcd.setCursor(0, 1);
    lcd.print("Retrying...");
    delay(2000);
  }

  // Baca kondisi awal bin
  float jarakAwal = bacaJarak(TRIG1, ECHO1);
  kapasitasSebelumnya = hitungKapasitas(jarakAwal);
  kapasitasSaatIni    = kapasitasSebelumnya;

  // Ambil leaderboard pertama kali
  updateTopLeaderboard();

  // Tampilkan layar utama
  lcd.clear();
  updateLCDUtama(kapasitasSaatIni);
  updateLED(kapasitasSaatIni);

  Serial.println("[SETUP] Inisialisasi selesai. Loop dimulai.");
}

//  LOOP UTAMA
void loop() {
  unsigned long now = millis();

  // 0. Reconnect WiFi jika putus
  reconnectWiFiIfNeeded();

  // 1. Baca Sensor Atas (Level Bin)
  if (now - lastBacaSensor >= INTERVAL_BACA_SENSOR) {
    lastBacaSensor = now;

    float jarakLevel = bacaJarak(TRIG1, ECHO1);
    kapasitasSaatIni = hitungKapasitas(jarakLevel);

    Serial.printf("[SENSOR-1] Jarak: %.1f cm | Kapasitas: %.1f%%\n",
                  jarakLevel, kapasitasSaatIni);

    // Update LED setiap baca sensor
    updateLED(kapasitasSaatIni);

    // 2. Deteksi Presence (Sensor Depan)
    float jarakDepan = bacaJarak(TRIG2, ECHO2);
    // Orang terdeteksi jika jaraknya < 100 cm di depan bin
    presenceDetected = (jarakDepan > 0 && jarakDepan < 100.0);

    if (presenceDetected) {
      Serial.printf("[SENSOR-2] Orang terdeteksi. Jarak: %.1f cm\n", jarakDepan);
    }
  }

  // 3. Poll Pending Throw dari Server
  // Hanya poll jika ada orang di depan bin DAN time-gate sudah lewat
  if (presenceDetected &&
      !awaitingThrow &&
      (now - lastThrowTime >= TIME_GATE_MS) &&
      (now - lastPollQr >= INTERVAL_POLL_QR)) {
    lastPollQr = now;
    pollPendingThrow();
  }

  // 4. Deteksi Sampah Masuk & Kirim Throw Event
  if (awaitingThrow &&
      presenceDetected &&
      pendingQrCode.length() > 0 &&
      (now - lastThrowTime >= TIME_GATE_MS)) {

    float delta = kapasitasSaatIni - kapasitasSebelumnya;

    if (delta >= THROW_MIN_DELTA) {
      // Sampah masuk! Validasi terpenuhi.
      Serial.printf("[THROW] Sampah terdeteksi! Delta: %.1f%% | QR: %s\n",
                    delta, pendingQrCode.c_str());

      // Tampilkan notifikasi di LCD
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Sampah Masuk!");
      lcd.setCursor(0, 1);
      lcd.print("Mengirim data...");

      // Kirim ke backend
      kirimEventThrow(pendingQrCode, kapasitasSebelumnya, kapasitasSaatIni);

      // Update state
      kapasitasSebelumnya = kapasitasSaatIni;
      lastThrowTime       = millis(); // reset time-gate
      awaitingThrow       = false;
      pendingQrCode       = "";
      presenceDetected    = false;

      // Kembali ke tampilan utama
      lcd.clear();
      updateLCDUtama(kapasitasSaatIni);
    }
  }

  // 5. Jika bin dikosongkan (kapasitas turun)
  if (kapasitasSaatIni < kapasitasSebelumnya - 5.0) {
    // Bin dikosongkan petugas, reset nilai sebelumnya
    Serial.printf("[BIN] Bin dikosongkan. Kapasitas: %.1f%% → %.1f%%\n",
                  kapasitasSebelumnya, kapasitasSaatIni);
    kapasitasSebelumnya = kapasitasSaatIni;
  }

  // 6. Update Kapasitas Bin ke Server (periodik)
  if (now - lastUpdateServer >= INTERVAL_UPDATE_SERVER) {
    lastUpdateServer = now;
    updateKapasitasBin(kapasitasSaatIni);
  }

  // 7. Update Leaderboard (periodik, untuk LCD F4)
  if (now - lastUpdateLeaderboard >= INTERVAL_UPDATE_LEADERBOARD) {
    lastUpdateLeaderboard = now;
    updateTopLeaderboard();
  }

  // 8. Update Tampilan LCD (periodik)
  if (now - lastUpdateLcd >= INTERVAL_UPDATE_LCD) {
    lastUpdateLcd = now;
    // Hanya update tampilan utama jika tidak sedang awaitingThrow
    if (!awaitingThrow) {
      updateLCDUtama(kapasitasSaatIni);
    }
  }

  // Loop delay kecil untuk hindari busy-wait berlebihan
  delay(50);
}
