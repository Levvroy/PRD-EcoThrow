/*
 *  EcoThrow
 *  Kelompok 9 · PRD K51 · STEI-K ITB 2025
 *  ─────────────────────────────────────────────────────────
 *  CHANGELOG v1.3 (Security & Presence Gate):
 *
 *  [FIX-8] presenceDetected sebagai syarat WAJIB sebelum throw
 *          diproses. Sebelumnya throw bisa trigger tanpa orang
 *          di depan bin (noise sensor / remote trigger).
 *          Sensor depan HARUS mendeteksi orang pada saat delta
 *          level terpenuhi. Jika tidak ada orang, throw dibatalkan
 *          dan sesi pending di-cancel via DELETE /api/pending_throw.
 *
 *  [FIX-9] presenceDetected dibaca FRESH (real-time) setiap kali
 *          throw detection dijalankan, bukan pakai nilai cached
 *          dari 3 detik lalu. Ini memastikan kehadiran orang
 *          dikonfirmasi tepat saat sampah terdeteksi masuk.
 *
 *  [FIX-10] Alert LCD multi-tahap:
 *           - "Scan NIM dulu!" saat poll dapat pending tapi
 *             tidak ada orang di depan bin (anomali remote trigger)
 *           - "Berdiri di depan!" saat awaiting tapi orang pergi
 *           - "Sensor Error!" saat sensor level gagal berulang (>5x)
 *           - "Bin Penuh!" saat kapasitas >= THRESHOLD_FULL
 *
 *  [FIX-11] Timeout sesi: jika awaitingThrow > 120 detik tanpa
 *           throw terdeteksi, sesi otomatis dibatalkan & pending
 *           throw di server dihapus via DELETE. LCD menampilkan
 *           "Waktu Habis!" dan sistem kembali ke idle.
 *           Sebelumnya sesi bisa hang selamanya jika user pergi.
 *
 *  [FIX-12] cancelPendingThrow(): fungsi baru untuk menghapus
 *           pending throw di server saat sesi dibatalkan lokal.
 *           Ini membebaskan slot eksklusif agar user lain bisa pakai.
 *
 *  ─────────────────────────────────────────────────────────
 *  CHANGELOG v1.2 (Sensor Validity):
 *
 *  [FIX-1] hitungKapasitas: sensor error (-1.0) tidak lagi mengembalikan
 *          kapasitasSebelumnya. Sekarang mengembalikan -1.0 agar caller
 *          bisa membedakan bacaan valid vs error.
 *
 *  [FIX-2] Loop - baca sensor: kapasitasSaatIni hanya di-update jika
 *          bacaan sensor valid (>= 0). Bacaan error di-skip & dicatat.
 *          Counter sensorErrorCount ditambahkan untuk monitoring.
 *
 *  [FIX-3] Loop - baseline: kapasitasSebelumnya hanya di-update saat
 *          sensor valid DAN idle (bukan awaiting throw). Ini menjaga
 *          baseline akurat meski sensor sesekali error.
 *
 *  [FIX-4] Loop - deteksi bin dikosongkan: hanya diproses jika sensor
 *          valid. Mencegah false-positive saat sensor timeout.
 *
 *  [FIX-5] Loop - bin/update: updateKapasitasBin hanya dikirim ke
 *          server jika sensor valid pada saat update periodik.
 *          Reset 0% dari frontend tidak akan tertimpa nilai stale.
 *
 *  [FIX-6] setup(): inisialisasi awal dengan retry sensor 3x agar
 *          kapasitasSebelumnya/SaatIni tidak stuck di 0.0 saat boot
 *          jika sensor butuh waktu warm-up.
 *
 *  [FIX-7] Tambah variabel sensorValid (bool) dan sensorErrorCount
 *          sebagai flag global untuk monitoring & logging.
 *
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
const char* WIFI_SSID     = "KOPINAKO JATINANGOR";
const char* WIFI_PASSWORD = "eskrimnako";

// Server FastAPI - ganti IP dengan IP laptop kamu (cek: ipconfig)
const char* SERVER_BASE       = "http://172.16.67.240:8000";
const char* URL_UPDATE_BIN    = "http://172.16.67.240:8000/api/bin/update";
const char* URL_THROW         = "http://172.16.67.240:8000/api/throw";
const char* URL_PENDING_THROW = "http://172.16.67.240:8000/api/pending_throw";
const char* URL_LEADERBOARD   = "http://172.16.67.240:8000/api/leaderboard?limit=1";

// Identitas bin ini - ganti dengan bin_id dari database
const char* BIN_ID = "44208e55-d0e2-487d-9a81-f0f2f28b2468"; // GKU 2

// Dimensi fisik bin -- WAJIB diukur & disesuaikan sebelum deploy!
// Cara ukur: letakkan sensor di atas bin KOSONG, catat jarak ke dasar bin.
// Dari log serial terlihat jarak idle ~68-70 cm -> BIN_HEIGHT_CM = 70.0
// Ganti nilai ini dengan hasil pengukuran fisik bin kamu.
const float BIN_HEIGHT_CM = 70.0;  // cm (sensor ke dasar bin kosong)

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

// [FIX-7] Status sensor level (SENSOR-1)
// sensorValid = false berarti bacaan terakhir timeout/error.
// Semua operasi kritis (bin/update, baseline update) di-skip saat false.
bool sensorValid      = false;  // true jika bacaan sensor terakhir valid
int  sensorErrorCount = 0;      // jumlah error berturut-turut (untuk Serial monitoring)

// [FIX-11] Timeout tracking sesi awaiting throw
// Sesi otomatis dibatalkan jika > AWAIT_TIMEOUT_MS tanpa throw
unsigned long awaitingStartTime  = 0;
const unsigned long AWAIT_TIMEOUT_MS = 120000; // 120 detik = sama dengan expire server

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
 * Jarak = 0 cm          → bin penuh (100%)
 * Jarak = BIN_HEIGHT_CM → bin kosong (0%)
 *
 * [FIX-1] Return -1.0 jika jarakCm tidak valid (< 0 atau terlalu jauh).
 * Sebelumnya mengembalikan kapasitasSebelumnya yang menyebabkan
 * nilai stale terus dipakai dan dikirim ke server saat sensor error.
 * Caller WAJIB cek return value >= 0 sebelum menggunakan.
 */
float hitungKapasitas(float jarakCm) {
  // Jarak negatif = sensor timeout / tidak ada echo
  if (jarakCm < 0) return -1.0;
  // Jarak melebihi tinggi bin = sensor membaca di luar range fisik
  // (misal bin sudah dikosongkan total & ada jarak ke lantai)
  // Kembalikan 0.0 karena bin pasti kosong/sangat kosong
  if (jarakCm >= BIN_HEIGHT_CM) return 0.0;
  float kapasitas = ((BIN_HEIGHT_CM - jarakCm) / BIN_HEIGHT_CM) * 100.0;
  return constrain(kapasitas, 0.0, 100.0);
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
        awaitingStartTime = millis(); // [FIX-11] mulai timer timeout sesi
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

// [FIX-12] FUNGSI HTTP - CANCEL PENDING THROW
/**
 * Hapus pending throw di server saat sesi dibatalkan lokal.
 * Dipanggil ketika:
 *   - Timeout 120 detik tanpa throw terdeteksi
 *   - Orang pergi dari depan bin setelah terlalu lama
 * Membebaskan slot eksklusif agar user lain bisa mendaftar.
 */
void cancelPendingThrow() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(SERVER_BASE) + "/api/pending_throw?bin_id=" + String(BIN_ID);
  http.begin(url);
  http.setTimeout(3000);

  int code = http.sendRequest("DELETE");
  if (code > 0) {
    Serial.printf("[HTTP] pending_throw cancelled → %d\n", code);
  } else {
    Serial.printf("[HTTP] cancel pending_throw gagal: %s\n", http.errorToString(code).c_str());
  }
  http.end();
}

/**
 * Reset semua state sesi throw ke idle.
 * Dipanggil setelah throw berhasil, timeout, atau pembatalan.
 */
void resetSesiThrow(bool cancelServer) {
  if (cancelServer) {
    cancelPendingThrow();
  }
  awaitingThrow      = false;
  pendingQrCode      = "";
  presenceDetected   = false;
  awaitingStartTime  = 0;
  Serial.println("[STATE] Sesi throw di-reset ke idle.");
}
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
  lcd.print("EcoThrow v1.1");
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

  // Baca kondisi awal bin — retry 3x agar tidak stuck di 0.0 jika
  // sensor butuh warm-up setelah power-on. [FIX-6]
  kapasitasSebelumnya = 0.0;
  kapasitasSaatIni    = 0.0;
  sensorValid         = false;
  for (int i = 0; i < 3; i++) {
    delay(300);
    float jarakAwal   = bacaJarak(TRIG1, ECHO1);
    float bacaanAwal  = hitungKapasitas(jarakAwal);
    if (bacaanAwal >= 0) {
      kapasitasSebelumnya = bacaanAwal;
      kapasitasSaatIni    = bacaanAwal;
      sensorValid         = true;
      Serial.printf("[SETUP] Sensor OK. Kapasitas awal: %.1f%%\n", bacaanAwal);
      break;
    }
    Serial.printf("[SETUP] Sensor retry %d/3 gagal (jarak: %.1f cm)\n", i + 1, jarakAwal);
  }
  if (!sensorValid) {
    Serial.println("[SETUP] WARNING: Sensor tidak terbaca saat boot. Kapasitas dimulai dari 0%.");
  }

  // Ambil leaderboard pertama kali
  updateTopLeaderboard();

  // Tampilkan layar utama
  lcd.clear();
  updateLCDUtama(kapasitasSaatIni);
  updateLED(kapasitasSaatIni);

  Serial.println("[SETUP] Inisialisasi selesai. Loop dimulai.");
}

//  LOOP UTAMA
//  v1.3 — Presence Gate + Timeout + Alert LCD:
//    - Poll pending_throw tetap berjalan, tapi throw HANYA diproses
//      jika sensor depan (SENSOR-2) mengkonfirmasi ada orang di depan bin.
//    - Sesi awaiting auto-cancel setelah AWAIT_TIMEOUT_MS (120 detik).
//    - Alert LCD untuk: sensor error, bin penuh, tidak ada orang, timeout.
void loop() {
  unsigned long now = millis();

  // 0. Reconnect WiFi jika putus
  reconnectWiFiIfNeeded();

  // 1. Baca Sensor Level (SENSOR-1) setiap INTERVAL_BACA_SENSOR
  if (now - lastBacaSensor >= INTERVAL_BACA_SENSOR) {
    lastBacaSensor = now;

    float jarakLevel  = bacaJarak(TRIG1, ECHO1);
    float bacaanBaru  = hitungKapasitas(jarakLevel);

    // [FIX-2] Hanya update kapasitasSaatIni jika sensor valid.
    if (bacaanBaru >= 0) {
      kapasitasSaatIni  = bacaanBaru;
      sensorValid       = true;
      sensorErrorCount  = 0;
    } else {
      sensorValid = false;
      sensorErrorCount++;
      Serial.printf("[SENSOR-1] ERROR #%d: Timeout (jarak: %.1f cm) — pakai nilai terakhir: %.1f%%\n",
                    sensorErrorCount, jarakLevel, kapasitasSaatIni);

      // [FIX-10] Alert LCD jika sensor error terlalu banyak berturut-turut
      if (sensorErrorCount == 5) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("! Sensor Error !");
        lcd.setCursor(0, 1);
        lcd.print("Hubungi petugas");
        Serial.println("[ALERT] Sensor level error 5x berturut-turut!");
      }
    }

    if (sensorValid) {
      Serial.printf("[SENSOR-1] Jarak: %.1f cm | Kapasitas: %.1f%%\n",
                    jarakLevel, kapasitasSaatIni);
      updateLED(kapasitasSaatIni);

      // [FIX-10] Alert LCD bin hampir penuh / penuh
      if (!awaitingThrow) {
        if (kapasitasSaatIni >= THRESHOLD_FULL) {
          // Hanya tampilkan sekali tiap refresh LCD (bukan setiap 3 detik)
        }
      }
    }

    // [FIX-9] Baca sensor depan FRESH setiap siklus sensor
    // Tidak pakai nilai cached — kehadiran orang harus konfirmasi real-time
    float jarakDepan = bacaJarak(TRIG2, ECHO2);
    presenceDetected = (jarakDepan > 0 && jarakDepan < 100.0);
    if (presenceDetected) {
      Serial.printf("[SENSOR-2] Orang terdeteksi. Jarak: %.1f cm\n", jarakDepan);
    }

    // [FIX-3] Update baseline hanya jika sensor valid DAN idle
    if (sensorValid && !awaitingThrow) {
      kapasitasSebelumnya = kapasitasSaatIni;
    }
  }

  // 2. Poll /api/pending_throw setiap INTERVAL_POLL_QR
  if (!awaitingThrow &&
      (now - lastThrowTime >= TIME_GATE_MS) &&
      (now - lastPollQr >= INTERVAL_POLL_QR)) {
    lastPollQr = now;
    pollPendingThrow();

    // [FIX-8] Setelah poll berhasil dapat QR, verifikasi orang ada di depan
    // Jika pending throw ada tapi tidak ada orang → anomali remote trigger
    // Tampilkan alert dan JANGAN langsung set awaitingThrow=true tanpa orang
    if (awaitingThrow && !presenceDetected) {
      Serial.println("[ALERT] Pending throw diterima tapi TIDAK ADA orang di depan bin!");
      Serial.println("[ALERT] Menunggu orang berdiri di depan bin...");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Ada pendaftaran!");
      lcd.setCursor(0, 1);
      lcd.print("Berdiri di depan");
      // awaitingThrow tetap true — biarkan orang datang ke bin
      // Timeout akan membatalkan sesi jika tidak ada yang datang
      awaitingStartTime = millis(); // mulai timer timeout
    } else if (awaitingThrow && presenceDetected) {
      awaitingStartTime = millis(); // orang sudah di depan, mulai timer
    }
  }

  // 3. Deteksi sampah masuk & kirim throw event
  if (awaitingThrow &&
      pendingQrCode.length() > 0 &&
      (now - lastThrowTime >= TIME_GATE_MS)) {

    // [FIX-11] Cek timeout sesi — batalkan jika sudah > AWAIT_TIMEOUT_MS
    if (awaitingStartTime > 0 && (now - awaitingStartTime) >= AWAIT_TIMEOUT_MS) {
      Serial.println("[TIMEOUT] Sesi throw habis waktu (120 detik). Membatalkan...");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Waktu Habis!");
      lcd.setCursor(0, 1);
      lcd.print("Coba lagi.");
      delay(3000);
      resetSesiThrow(true); // cancel server + reset state lokal
      lcd.clear();
      updateLCDUtama(kapasitasSaatIni);
      // Loop selesai untuk iterasi ini
    }
    // [FIX-8 + FIX-9] Baca presence FRESH tepat sebelum proses delta
    // Ini adalah gate keamanan utama — throw TIDAK diproses tanpa orang
    else {
      float jarakDepanFresh = bacaJarak(TRIG2, ECHO2);
      bool  orangAdaFresh   = (jarakDepanFresh > 0 && jarakDepanFresh < 100.0);

      if (!orangAdaFresh) {
        // Orang tidak ada di depan bin — bisa jadi remote trigger / orang pergi
        // [FIX-10] Alert LCD
        Serial.println("[ALERT] Tidak ada orang di depan bin saat cek throw. Menunggu...");
        if (!awaitingThrow) return; // sudah di-reset di blok timeout
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Berdiri di depan");
        lcd.setCursor(0, 1);
        lcd.print("bin dulu!");
        // Jangan batalkan sesi — beri waktu user untuk berdiri di depan bin
        // Timeout akan handle jika user tidak datang sama sekali

      } else if (!sensorValid) {
        // Orang ada tapi sensor level error
        Serial.println("[AWAIT] Orang ada, tapi sensor level error — tunggu bacaan valid.");

      } else {
        // Orang ada DAN sensor valid — proses delta
        float delta = kapasitasSaatIni - kapasitasSebelumnya;
        Serial.printf("[AWAIT] Orang ada | kapasitas: %.1f%% | baseline: %.1f%% | delta: %.1f%%\n",
                      kapasitasSaatIni, kapasitasSebelumnya, delta);

        if (delta >= THROW_MIN_DELTA) {
          Serial.printf("[THROW] Sampah terdeteksi! Delta: %.1f%% | QR: %s\n",
                        delta, pendingQrCode.c_str());

          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Sampah Masuk!");
          lcd.setCursor(0, 1);
          lcd.print("Mengirim data...");

          String qrUntukKirim = pendingQrCode; // simpan sebelum di-reset
          float  baselineKirim = kapasitasSebelumnya;
          float  kapasitasKirim = kapasitasSaatIni;

          // Reset state dulu sebelum kirim (hindari double-throw jika HTTP lambat)
          kapasitasSebelumnya = kapasitasSaatIni;
          lastThrowTime       = millis();
          resetSesiThrow(false); // jangan cancel server — throw akan menghapusnya sendiri

          kirimEventThrow(qrUntukKirim, baselineKirim, kapasitasKirim);

          lcd.clear();
          updateLCDUtama(kapasitasSaatIni);

        } else {
          // Delta belum cukup — tampilkan status menunggu di LCD
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Siap! Buang");
          lcd.setCursor(0, 1);
          lcd.print("sampah sekarang");
        }
      }
    }
  }

  // 4. Bin dikosongkan petugas (kapasitas turun signifikan saat idle)
  // [FIX-4] Hanya proses jika sensor valid — mencegah false-positive
  // ketika sensor timeout lalu tiba-tiba terbaca lagi dengan nilai rendah.
  if (!awaitingThrow && sensorValid && kapasitasSaatIni < kapasitasSebelumnya - 5.0) {
    Serial.printf("[BIN] Bin dikosongkan. %.1f%% -> %.1f%%\n",
                  kapasitasSebelumnya, kapasitasSaatIni);
    kapasitasSebelumnya = kapasitasSaatIni;
    // Segera kirim update ke server agar frontend langsung sinkron
    updateKapasitasBin(kapasitasSaatIni);
    lastUpdateServer = now; // reset timer agar tidak double-kirim 30 detik lagi
  }

  // 5. Update kapasitas bin ke server (periodik)
  // [FIX-5] Hanya kirim jika sensor valid. Ini mencegah nilai stale
  // menimpa reset 0% yang dilakukan dari frontend/admin.
  if (now - lastUpdateServer >= INTERVAL_UPDATE_SERVER) {
    lastUpdateServer = now;
    if (sensorValid) {
      updateKapasitasBin(kapasitasSaatIni);
    } else {
      Serial.printf("[HTTP] Skip bin/update — sensor error (%d berturut-turut). Nilai server dipertahankan.\n",
                    sensorErrorCount);
    }
  }

  // 6. Update leaderboard (periodik, untuk LCD)
  if (now - lastUpdateLeaderboard >= INTERVAL_UPDATE_LEADERBOARD) {
    lastUpdateLeaderboard = now;
    updateTopLeaderboard();
  }

  // 7. Update tampilan LCD (periodik)
  if (now - lastUpdateLcd >= INTERVAL_UPDATE_LCD) {
    lastUpdateLcd = now;
    if (!awaitingThrow) {
      // [FIX-10] Tampilkan alert bin penuh jika kapasitas >= THRESHOLD_FULL
      if (sensorValid && kapasitasSaatIni >= THRESHOLD_FULL) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("! BIN PENUH !");
        lcd.setCursor(0, 1);
        lcd.print("Hubungi petugas");
        Serial.printf("[ALERT] Bin penuh: %.1f%%\n", kapasitasSaatIni);
        // Tampilkan 2 detik lalu kembali ke layar utama
        delay(2000);
      }
      updateLCDUtama(kapasitasSaatIni);
    }
  }

  delay(50);
}
