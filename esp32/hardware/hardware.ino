/*
 *  EcoThrow - ESP32 Firmware
 *  Kelompok 9 · PRD K51 · STEI-K ITB 2025
 *  ─────────────────────────────────────────────────────────
 *  CHANGELOG v2.2 (Dual LED Indicator)
 *
 *  PERBAIKAN v2.2 dari v2.1:
 *
 *  [LED-1] LED BIRU (GPIO 2) — Indikator Throw & Siap
 *    · Saat awaitingThrow aktif (menunggu user buang sampah):
 *      berkedip lambat (500ms on/off) → sinyal "hardware siap, buang sekarang"
 *    · Saat throw berhasil terdeteksi:
 *      berkedip cepat 3× (100ms) → konfirmasi visual sampah masuk
 *    · Saat idle / tidak ada sesi: MATI
 *
 *  [LED-2] LED PUTIH (GPIO 4) — Indikator Status Bin
 *    · Bin normal  (< 75%)  : MATI
 *    · Bin hampir penuh (75–89%) : berkedip lambat (1000ms) → peringatan
 *    · Bin penuh   (>= 90%) : NYALA STEADY → perlu dikosongkan
 *    · Sensor error         : berkedip sangat cepat (200ms)
 *
 *  [LED-3] SISTEM NON-BLOCKING
 *    Semua kedip LED dikendalikan oleh ledUpdate() dengan millis(),
 *    tidak ada delay() → tidak memblokir loop sensor/HTTP.
 *
 *  ─────────────────────────────────────────────────────────
 *  PERBAIKAN v2.1 (referensi):
 *    [FIX-LCD-1] Hapus blocking delay() dari kirimEventThrow()
 *    [FIX-LCD-2] LCD interaktif 4 mode bergantian
 *    [FIX-LCD-3] Fetch top 2 individu & fakultas untuk LCD
 *
 *  ─────────────────────────────────────────────────────────
 *  Pin Mapping:
 *    HC-SR04 #1 (Level)    TRIG→5  ECHO→18
 *    HC-SR04 #2 (Presence) TRIG→19 ECHO→23
 *    LCD 1602 I2C           SDA→21  SCL→22
 *    LED Biru  (Throw/Siap) GPIO 2
 *    LED Putih (Status Bin) GPIO 4
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ─── KONFIGURASI ────────────────────────────────────────────────
const char* WIFI_SSID     = "KOST GWENZA LOBBY";
const char* WIFI_PASSWORD = "1402Zordix";
const char* SERVER_BASE   = "http://192.168.0.109:8000";
const char* BIN_ID        = "44208e55-d0e2-487d-9a81-f0f2f28b2468";

// Presence sensor sebagai gate (false = informasi saja)
const bool PRESENCE_GATE = false;

// Threshold bin (persen)
const float THRESHOLD_WARN = 75.0;
const float THRESHOLD_FULL = 90.0;

// Threshold deteksi throw: delta kapasitas minimum (%)
const float THROW_MIN_DELTA = 2.0;

// Jumlah sampel kalibrasi baseline
const int CALIBRATION_SAMPLES = 5;

// Timeout sesi: mulai dari PERTAMA KALI ada pergerakan
const unsigned long AWAIT_TIMEOUT_MS   = 120000;  // 2 menit
const unsigned long TIME_GATE_MS       = 8000;    // jeda minimum antar throw

// Interval operasi
const unsigned long INTERVAL_BACA_SENSOR   = 2000;
const unsigned long INTERVAL_UPDATE_SERVER = 30000;
const unsigned long INTERVAL_POLL_QR       = 5000;
const unsigned long INTERVAL_WIFI_RETRY    = 10000;
const unsigned long INTERVAL_LEADERBOARD   = 60000;

// ── [v2.1] LCD slide interval: berapa lama setiap mode tampil
const unsigned long LCD_SLIDE_INTERVAL_MS  = 5000;   // 5 detik per slide
// ── Durasi override tampilan throw result (non-blocking)
const unsigned long LCD_OVERRIDE_DURATION  = 5000;   // 5 detik

// ─── PIN DEFINITIONS ────────────────────────────────────────────
const int TRIG1 = 5;
const int ECHO1 = 18;
const int TRIG2 = 19;
const int ECHO2 = 23;
const int LED_BIRU  = 2;   // Indikator throw / siap buang
const int LED_PUTIH = 4;   // Indikator status bin

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ─── STATE GLOBAL ───────────────────────────────────────────────

// Baseline adaptif
float jarakBaseline     = 0.0;
bool  baselineValid     = false;

// Bacaan sensor saat ini
float jarakSaatIni      = 0.0;
float kapasitasSaatIni  = 0.0;
bool  sensorValid       = false;
int   sensorErrorCount  = 0;

// State sesi throw
bool   awaitingThrow    = false;
String pendingQrCode    = "";
float  kapasitasBaseline = 0.0;
unsigned long awaitingStartTime = 0;

// Presence sensor
bool presenceDetected   = false;

// Timing
unsigned long lastBacaSensor    = 0;
unsigned long lastUpdateServer  = 0;
unsigned long lastPollQr        = 0;
unsigned long lastWifiRetry     = 0;
unsigned long lastThrowTime     = 0;
unsigned long lastLeaderboard   = 0;

// ── [v2.1] LCD state management
int           lcdMode            = 0;   // 0=bin, 1=top2 individu, 2=top2 fakultas, 3=tips
unsigned long lastLcdSlide       = 0;
const int     LCD_MODE_COUNT     = 4;

// Override mode: tampilkan pesan sementara (throw result, error, dll)
bool          lcdOverrideActive  = false;
unsigned long lcdOverrideStart   = 0;
String        lcdOverrideLine0   = "";
String        lcdOverrideLine1   = "";

// ── [v2.1] Data leaderboard untuk LCD
struct LcdLeaderEntry {
  String name;
  int    xp;
  String faculty;
};
LcdLeaderEntry topIndividu[2];   // Top 2 individu
LcdLeaderEntry topFakultas[2];   // Top 2 fakultas

// ── Tips/info yang ditampilkan di mode 3 (bergantian tiap slide)
const char* LCD_TIPS[] = {
  "PRD gacor!",
  "PRD gacor!",
  "PRD gacor!",
  "PRD gacor!",
  "PRD gacor!",
};
const int LCD_TIPS_COUNT = 5;
int       lcdTipIndex    = 0;

// ─── UTILITAS SENSOR ────────────────────────────────────────────

float bacaJarak(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 35000);
  if (dur == 0) return -1.0;
  return dur * 0.034f / 2.0f;
}

float hitungKapasitasAdaptif(float jarakCm) {
  if (jarakCm < 0) return -1.0;
  if (!baselineValid || jarakBaseline <= 0) return -1.0;
  if (jarakCm >= jarakBaseline) return 0.0;
  float kap = (jarakBaseline - jarakCm) / jarakBaseline * 100.0f;
  return constrain(kap, 0.0f, 100.0f);
}

bool kalibrasBaseline(int samples = CALIBRATION_SAMPLES) {
  float sum = 0.0;
  int   ok  = 0;
  Serial.print("[KALIBRASI] Mengambil baseline");
  for (int i = 0; i < samples; i++) {
    delay(300);
    float j = bacaJarak(TRIG1, ECHO1);
    if (j > 0 && j < 600) {
      sum += j;
      ok++;
      Serial.print(".");
    }
  }
  Serial.println();
  if (ok < 2) {
    Serial.println("[KALIBRASI] GAGAL — terlalu sedikit bacaan valid");
    return false;
  }
  jarakBaseline = sum / ok;
  baselineValid  = true;
  Serial.printf("[KALIBRASI] OK. Baseline = %.1f cm (dari %d sampel)\n", jarakBaseline, ok);
  return true;
}

// ─── [v2.2] SISTEM LED NON-BLOCKING ────────────────────────────
//
// LED Biru  → kondisi throw (siap/berhasil)
// LED Putih → kondisi bin (warn/full/error)
//
// Pakai #define int agar kompatibel dengan auto-prototyping Arduino IDE.

#define LED_MODE_OFF        0   // mati
#define LED_MODE_STEADY     1   // nyala terus
#define LED_MODE_BLINK_SLOW 2   // kedip lambat 1000ms
#define LED_MODE_BLINK_MED  3   // kedip sedang 500ms
#define LED_MODE_BLINK_FAST 4   // kedip cepat  100ms
#define LED_MODE_PULSE_N    5   // kedip N kali lalu mati

// State setiap LED disimpan sebagai variabel terpisah (hindari struct+enum)
int           biruMode      = LED_MODE_OFF;
bool          biruPin       = false;
unsigned long biruToggle    = 0;
unsigned long biruInterval  = 0;
int           biruPulseMax  = 0;
int           biruPulseN    = 0;

int           putihMode     = LED_MODE_OFF;
bool          putihPin      = false;
unsigned long putihToggle   = 0;
unsigned long putihInterval = 0;

// ── Set mode LED biru
void setBiruMode(int mode, int pulseN = 0) {
  biruMode     = mode;
  biruPulseMax = pulseN * 2;   // setiap kedip = ON + OFF = 2 toggle
  biruPulseN   = 0;
  biruToggle   = millis();
  switch (mode) {
    case LED_MODE_OFF:
      digitalWrite(LED_BIRU, LOW);  biruPin = false; break;
    case LED_MODE_STEADY:
      digitalWrite(LED_BIRU, HIGH); biruPin = true;  break;
    case LED_MODE_BLINK_SLOW: biruInterval = 1000; break;
    case LED_MODE_BLINK_MED:  biruInterval = 500;  break;
    case LED_MODE_BLINK_FAST: biruInterval = 100;  break;
    case LED_MODE_PULSE_N:    biruInterval = 100;  break;
  }
}

// ── Set mode LED putih
void setPutihMode(int mode) {
  putihMode   = mode;
  putihToggle = millis();
  switch (mode) {
    case LED_MODE_OFF:
      digitalWrite(LED_PUTIH, LOW);  putihPin = false; break;
    case LED_MODE_STEADY:
      digitalWrite(LED_PUTIH, HIGH); putihPin = true;  break;
    case LED_MODE_BLINK_SLOW: putihInterval = 1000; break;
    case LED_MODE_BLINK_MED:  putihInterval = 500;  break;
    case LED_MODE_BLINK_FAST: putihInterval = 200;  break;
    default: break;
  }
}

// Dipanggil setiap loop — handle kedip non-blocking
void ledUpdate() {
  unsigned long now = millis();

  // ── LED Biru
  if (biruMode == LED_MODE_BLINK_SLOW ||
      biruMode == LED_MODE_BLINK_MED  ||
      biruMode == LED_MODE_BLINK_FAST ||
      biruMode == LED_MODE_PULSE_N) {
    if (now - biruToggle >= biruInterval) {
      biruToggle = now;
      biruPin    = !biruPin;
      digitalWrite(LED_BIRU, biruPin ? HIGH : LOW);
      if (biruMode == LED_MODE_PULSE_N) {
        biruPulseN++;
        if (biruPulseN >= biruPulseMax) {
          biruMode = LED_MODE_OFF;
          digitalWrite(LED_BIRU, LOW);
          biruPin = false;
        }
      }
    }
  }

  // ── LED Putih
  if (putihMode == LED_MODE_BLINK_SLOW ||
      putihMode == LED_MODE_BLINK_MED  ||
      putihMode == LED_MODE_BLINK_FAST) {
    if (now - putihToggle >= putihInterval) {
      putihToggle = now;
      putihPin    = !putihPin;
      digitalWrite(LED_PUTIH, putihPin ? HIGH : LOW);
    }
  }
}

// Update LED putih berdasarkan kapasitas bin — dipanggil setiap baca sensor
void updateLEDPutih(float kap, bool sensorErr) {
  if (sensorErr) {
    setPutihMode(LED_MODE_BLINK_FAST);
  } else if (kap >= THRESHOLD_FULL) {
    setPutihMode(LED_MODE_STEADY);
  } else if (kap >= THRESHOLD_WARN) {
    setPutihMode(LED_MODE_BLINK_SLOW);
  } else {
    setPutihMode(LED_MODE_OFF);
  }
}

// Update LED biru berdasarkan state throw — dipanggil saat state berubah
void updateLEDBiru() {
  if (awaitingThrow) {
    setBiruMode(LED_MODE_BLINK_MED);
  } else {
    setBiruMode(LED_MODE_OFF);
  }
}

// Dipanggil tepat setelah throw berhasil — biru kedip 3× cepat
void ledThrowSuccess() {
  setBiruMode(LED_MODE_PULSE_N, 3);
}

// ─── [v2.1] LCD HELPERS ─────────────────────────────────────────

// Tulis ke LCD dengan padding agar teks sebelumnya terhapus bersih
void lcdPrint(int row, const String& text) {
  lcd.setCursor(0, row);
  String padded = text;
  while (padded.length() < 16) padded += " ";
  lcd.print(padded.substring(0, 16));
}

// Set override: tampilkan pesan sementara selama LCD_OVERRIDE_DURATION ms
void lcdSetOverride(const String& line0, const String& line1) {
  lcdOverrideLine0   = line0;
  lcdOverrideLine1   = line1;
  lcdOverrideActive  = true;
  lcdOverrideStart   = millis();
  lcdPrint(0, line0);
  lcdPrint(1, line1);
  Serial.printf("[LCD] Override: \"%s\" | \"%s\"\n", line0.c_str(), line1.c_str());
}

// Hitung bar visual isi bin (8 karakter max di LCD)
String binBar(float pct, int width = 8) {
  int filled = (int)(pct / 100.0f * width);
  filled = constrain(filled, 0, width);
  String bar = "[";
  for (int i = 0; i < width; i++) bar += (i < filled) ? "#" : "-";
  bar += "]";
  return bar;
}

// Truncate string agar muat di LCD
String trunc(const String& s, int maxLen) {
  if ((int)s.length() <= maxLen) return s;
  return s.substring(0, maxLen - 1) + ".";
}

// ─── [v2.1] LCD RENDER PER MODE (non-blocking) ──────────────────

void lcdRenderMode0() {
  // Mode 0: Level bin + status
  String statusStr = kapasitasSaatIni >= THRESHOLD_FULL ? "FULL"
                   : kapasitasSaatIni >= THRESHOLD_WARN ? "WARN"
                   : "OK";
  if (!sensorValid) statusStr = "ERR";

  // Baris 1: "Bin: XX% WARN"
  String line0 = "Bin: " + String((int)kapasitasSaatIni) + "% " + statusStr;
  // Baris 2: bar visual
  String line1 = binBar(kapasitasSaatIni, 10);

  lcdPrint(0, line0);
  lcdPrint(1, line1);
}

void lcdRenderMode1() {
  // Mode 1: Top 2 leaderboard individu
  lcdPrint(0, "Top Individu:");
  // Tampilkan nama dan XP, bergantian antara #1 dan #2 setiap 2.5 detik
  // (pakai lastLcdSlide offset untuk sub-cycling)
  static unsigned long lastSubSlide = 0;
  static int subIdx = 0;
  if (millis() - lastSubSlide >= 2500) {
    lastSubSlide = millis();
    subIdx = (subIdx + 1) % 2;
  }
  int idx = subIdx;
  if (topIndividu[idx].name.length() > 0) {
    String entry = "#" + String(idx + 1) + " " + trunc(topIndividu[idx].name, 9)
                   + " " + String(topIndividu[idx].xp) + "XP";
    lcdPrint(1, trunc(entry, 16));
  } else {
    lcdPrint(1, "Belum ada data");
  }
}

void lcdRenderMode2() {
  // Mode 2: Top 2 fakultas
  lcdPrint(0, "Top Fakultas:");
  static unsigned long lastSubSlide2 = 0;
  static int subIdx2 = 0;
  if (millis() - lastSubSlide2 >= 2500) {
    lastSubSlide2 = millis();
    subIdx2 = (subIdx2 + 1) % 2;
  }
  int idx = subIdx2;
  if (topFakultas[idx].faculty.length() > 0) {
    String entry = "#" + String(idx + 1) + " " + trunc(topFakultas[idx].faculty, 8)
                   + " " + String(topFakultas[idx].xp) + "XP";
    lcdPrint(1, trunc(entry, 16));
  } else {
    lcdPrint(1, "Belum ada data");
  }
}

void lcdRenderMode3() {
  // Mode 3: Tips EcoThrow
  lcdPrint(0, "Tips EcoThrow:");
  lcdPrint(1, String(LCD_TIPS[lcdTipIndex]));
}

// Master render — dipanggil setiap loop
void lcdUpdate() {
  unsigned long now = millis();

  // ── Override aktif? Tampilkan override, cek kadaluarsa
  if (lcdOverrideActive) {
    if (now - lcdOverrideStart >= LCD_OVERRIDE_DURATION) {
      lcdOverrideActive = false;
      lcd.clear();
    } else {
      // Tidak render mode normal selama override aktif
      return;
    }
  }

  // ── Awaiting throw? Tampilkan prompt
  if (awaitingThrow) {
    lcdPrint(0, "Siap! Buang");
    lcdPrint(1, "sampah sekarang!");
    return;
  }

  // ── Slide timing: pindah mode setiap LCD_SLIDE_INTERVAL_MS
  if (now - lastLcdSlide >= LCD_SLIDE_INTERVAL_MS) {
    lastLcdSlide = now;
    int prevMode = lcdMode;
    lcdMode = (lcdMode + 1) % LCD_MODE_COUNT;

    // Rotasi tip index di mode 3
    if (lcdMode == 3) {
      lcdTipIndex = (lcdTipIndex + 1) % LCD_TIPS_COUNT;
    }

    Serial.printf("[LCD] Slide: mode %d → %d\n", prevMode, lcdMode);
    lcd.clear();
  }

  // ── Render mode aktif
  switch (lcdMode) {
    case 0: lcdRenderMode0(); break;
    case 1: lcdRenderMode1(); break;
    case 2: lcdRenderMode2(); break;
    case 3: lcdRenderMode3(); break;
    default: lcdRenderMode0();
  }
}

// ─── WIFI ────────────────────────────────────────────────────────

void reconnectWiFiIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastWifiRetry < INTERVAL_WIFI_RETRY) return;
  lastWifiRetry = now;

  Serial.println("[WiFi] Reconnecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 6000) delay(200);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] OK. IP: %s\n", WiFi.localIP().toString().c_str());
  }
}

// ─── HTTP HELPERS ────────────────────────────────────────────────

void updateKapasitasBin(float pct) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(String(SERVER_BASE) + "/api/bin/update");
  http.addHeader("Content-Type", "application/json");
  JsonDocument doc;
  doc["bin_id"]       = BIN_ID;
  doc["capacity_pct"] = pct;
  String body; serializeJson(doc, body);
  int code = http.POST(body);
  Serial.printf("[HTTP] bin/update → %d\n", code);
  http.end();
}

// [v2.1] kirimEventThrow NON-BLOCKING — tidak ada delay() di sini
// Hasil ditampilkan via lcdSetOverride() yang non-blocking
void kirimEventThrow(String qr, float before, float after) {
  if (WiFi.status() != WL_CONNECTED) {
    lcdSetOverride("No WiFi!", "Throw gagal.");
    return;
  }
  HTTPClient http;
  http.begin(String(SERVER_BASE) + "/api/throw");
  http.addHeader("Content-Type", "application/json");
  JsonDocument doc;
  doc["user_qr_code"]     = qr;
  doc["bin_id"]           = BIN_ID;
  doc["bin_level_before"] = before;
  doc["bin_level_after"]  = after;
  String body; serializeJson(doc, body);
  int code = http.POST(body);

  if (code == 200 || code == 201) {
    String resp = http.getString();
    JsonDocument r;
    if (deserializeJson(r, resp) == DeserializationError::Ok) {
      int    xp     = r["xp_earned"]   | 0;
      int    streak = r["streak_days"] | 0;
      String lvl    = r["new_level"]   | "?";
      Serial.printf("[THROW] +%d XP | Level: %s | Streak: %dd\n", xp, lvl.c_str(), streak);
      // [v2.1] Tampilkan hasil non-blocking
      String l0 = "+" + String(xp) + "XP! " + trunc(lvl, 8);
      String l1 = "Streak: " + String(streak) + "d";
      lcdSetOverride(l0, l1);
    }
  } else {
    Serial.printf("[HTTP] throw gagal: %d\n", code);
    lcdSetOverride("Throw Error!", "Code: " + String(code));
  }
  http.end();
}

void pollPendingThrow() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(String(SERVER_BASE) + "/api/pending_throw?bin_id=" + String(BIN_ID));
  http.setTimeout(3000);
  int code = http.GET();
  if (code == 200) {
    String resp = http.getString();
    JsonDocument r;
    if (deserializeJson(r, resp) == DeserializationError::Ok) {
      const char* qr = r["qr_code"];
      if (qr && strlen(qr) > 0) {
        pendingQrCode     = String(qr);
        awaitingThrow     = true;
        kapasitasBaseline = kapasitasSaatIni;
        awaitingStartTime = 0;
        Serial.printf("[POLL] Pending QR: %s | Baseline: %.1f%%\n",
                      pendingQrCode.c_str(), kapasitasBaseline);
        updateLEDBiru();   // biru: mulai kedip sedang (siap buang)
        lcd.clear();
      } else {
        awaitingThrow = false; pendingQrCode = "";
      }
    }
  } else if (code == 404) {
    awaitingThrow = false; pendingQrCode = "";
  }
  http.end();
}

void cancelPendingThrow() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(String(SERVER_BASE) + "/api/pending_throw?bin_id=" + String(BIN_ID));
  http.setTimeout(3000);
  int code = http.sendRequest("DELETE");
  Serial.printf("[HTTP] cancel pending_throw → %d\n", code);
  http.end();
}

void resetSesiThrow(bool cancelServer) {
  if (cancelServer) cancelPendingThrow();
  awaitingThrow     = false;
  pendingQrCode     = "";
  presenceDetected  = false;
  awaitingStartTime = 0;
  kapasitasBaseline = 0.0;
  lcdOverrideActive = false;
  lcd.clear();
  // LED biru mati (kecuali sedang PULSE_N konfirmasi throw — biarkan selesai)
  if (biruMode != LED_MODE_PULSE_N) {
    setBiruMode(LED_MODE_OFF);
  }
  Serial.println("[STATE] Sesi reset ke idle.");
}

// [v2.1] Fetch top 2 individu DAN top 2 fakultas
void updateLeaderboardData() {
  if (WiFi.status() != WL_CONNECTED) return;

  // Top 2 individu
  {
    HTTPClient http;
    http.begin(String(SERVER_BASE) + "/api/leaderboard?limit=2");
    http.setTimeout(3000);
    int code = http.GET();
    if (code == 200) {
      JsonDocument r;
      String resp = http.getString();
      if (deserializeJson(r, resp) == DeserializationError::Ok && r.is<JsonArray>()) {
        JsonArray arr = r.as<JsonArray>();
        for (int i = 0; i < 2 && i < (int)arr.size(); i++) {
          const char* n = arr[i]["name"];
          int xp        = arr[i]["total_xp"] | 0;
          topIndividu[i].name = n ? String(n) : "---";
          topIndividu[i].xp   = xp;
        }
        Serial.printf("[LDR] Top: %s (%dXP), %s (%dXP)\n",
                      topIndividu[0].name.c_str(), topIndividu[0].xp,
                      topIndividu[1].name.c_str(), topIndividu[1].xp);
      }
    }
    http.end();
  }

  // Top 2 fakultas
  {
    HTTPClient http;
    http.begin(String(SERVER_BASE) + "/api/leaderboard/faculty");
    http.setTimeout(3000);
    int code = http.GET();
    if (code == 200) {
      JsonDocument r;
      String resp = http.getString();
      if (deserializeJson(r, resp) == DeserializationError::Ok && r.is<JsonArray>()) {
        JsonArray arr = r.as<JsonArray>();
        for (int i = 0; i < 2 && i < (int)arr.size(); i++) {
          const char* f  = arr[i]["faculty"];
          int xp         = arr[i]["total_xp"] | 0;
          topFakultas[i].faculty = f ? String(f) : "---";
          topFakultas[i].xp      = xp;
        }
        Serial.printf("[FAK] Top: %s (%dXP), %s (%dXP)\n",
                      topFakultas[0].faculty.c_str(), topFakultas[0].xp,
                      topFakultas[1].faculty.c_str(), topFakultas[1].xp);
      }
    }
    http.end();
  }
}

// ─── SETUP ───────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== EcoThrow v2.2 ===");

  pinMode(TRIG1, OUTPUT); pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT); pinMode(ECHO2, INPUT);
  pinMode(LED_BIRU,  OUTPUT);
  pinMode(LED_PUTIH, OUTPUT);
  // Kedua LED mati saat boot
  digitalWrite(LED_BIRU,  LOW);
  digitalWrite(LED_PUTIH, LOW);

  lcd.init(); lcd.backlight();
  lcdPrint(0, "EcoThrow v2.2");
  lcdPrint(1, "Connecting...");

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) { delay(500); Serial.print("."); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] OK. IP: %s\n", WiFi.localIP().toString().c_str());
    lcd.clear();
    lcdPrint(0, "WiFi OK!");
    lcdPrint(1, WiFi.localIP().toString());
    delay(1500);
  } else {
    Serial.println("[WiFi] GAGAL.");
    lcd.clear();
    lcdPrint(0, "WiFi GAGAL");
    delay(1500);
  }

  // Kalibrasi baseline adaptif
  lcd.clear();
  lcdPrint(0, "Kalibrasi...");
  lcdPrint(1, "Jangan isi bin");
  bool ok = kalibrasBaseline(CALIBRATION_SAMPLES);
  if (ok) {
    kapasitasSaatIni = 0.0;
    sensorValid = true;
    lcd.clear();
    lcdPrint(0, "Baseline OK!");
    lcdPrint(1, String(jarakBaseline, 1) + " cm");
    delay(1500);
  } else {
    jarakBaseline = 200.0;
    baselineValid  = true;
    sensorValid    = false;
    Serial.println("[SETUP] Sensor gagal — pakai baseline default 200 cm");
    lcd.clear();
    lcdPrint(0, "Sensor Error!");
    lcdPrint(1, "Default 200cm");
    delay(1500);
  }

  // Inisialisasi data leaderboard
  topIndividu[0] = {"---", 0, ""};
  topIndividu[1] = {"---", 0, ""};
  topFakultas[0] = {"", 0, "---"};
  topFakultas[1] = {"", 0, "---"};
  updateLeaderboardData();

  // Init LCD state
  lcdMode       = 0;
  lastLcdSlide  = millis();
  lcd.clear();

  Serial.printf("[SETUP] Siap. Baseline=%.1fcm | PRESENCE_GATE=%s\n",
                jarakBaseline, PRESENCE_GATE ? "ON" : "OFF");
}

// ─── LOOP UTAMA ──────────────────────────────────────────────────

void loop() {
  unsigned long now = millis();

  // 0. WiFi watchdog
  reconnectWiFiIfNeeded();

  // ── 1. BACA SENSOR LEVEL ─────────────────────────────────────
  if (now - lastBacaSensor >= INTERVAL_BACA_SENSOR) {
    lastBacaSensor = now;

    float jarak = bacaJarak(TRIG1, ECHO1);

    if (jarak > 0 && jarak < 600) {
      jarakSaatIni     = jarak;
      kapasitasSaatIni = hitungKapasitasAdaptif(jarak);
      if (kapasitasSaatIni < 0) kapasitasSaatIni = 0;
      sensorValid      = true;
      sensorErrorCount = 0;
      updateLEDPutih(kapasitasSaatIni, false);   // putih: status bin
      // LED biru dikendalikan state throw — tidak diubah di sini
      Serial.printf("[SENSOR-1] %.1f cm → %.1f%% (baseline=%.1f cm)\n",
                    jarak, kapasitasSaatIni, jarakBaseline);
    } else {
      sensorValid = false;
      sensorErrorCount++;
      Serial.printf("[SENSOR-1] ERROR #%d: jarak=%.1f\n", sensorErrorCount, jarak);
      if (sensorErrorCount == 5) {
        lcdSetOverride("! Sensor Error !", "Hubungi petugas");
        updateLEDPutih(0, true);   // putih: kedip cepat tanda error
      }
    }

    // Baca sensor depan (presence) — informasi saja
    float jarakDepan = bacaJarak(TRIG2, ECHO2);
    presenceDetected = (jarakDepan > 0 && jarakDepan < 100.0);
    if (presenceDetected) Serial.printf("[SENSOR-2] Orang terdeteksi %.1f cm\n", jarakDepan);
  }

  // ── 2. POLL PENDING THROW ────────────────────────────────────
  if (!awaitingThrow &&
      now - lastThrowTime >= TIME_GATE_MS &&
      now - lastPollQr   >= INTERVAL_POLL_QR) {
    lastPollQr = now;
    pollPendingThrow();
  }

  // ── 3. DETEKSI THROW ─────────────────────────────────────────
  if (awaitingThrow && pendingQrCode.length() > 0) {

    bool bolehProses = true;
    if (PRESENCE_GATE && !presenceDetected) {
      bolehProses = false;
    }

    // Timeout: hanya hitung mundur setelah ada pergerakan awal
    if (awaitingStartTime > 0 && now - awaitingStartTime >= AWAIT_TIMEOUT_MS) {
      Serial.println("[TIMEOUT] Sesi habis waktu. Cancel.");
      lcdSetOverride("Waktu Habis!", "Coba lagi.");
      resetSesiThrow(true);
      return;
    }

    if (bolehProses && sensorValid && now - lastThrowTime >= TIME_GATE_MS) {
      float delta = kapasitasSaatIni - kapasitasBaseline;

      // Mulai timer hanya saat ada gerakan positif pertama kali
      if (delta > 0.5 && awaitingStartTime == 0) {
        awaitingStartTime = now;
        Serial.printf("[AWAIT] Gerakan terdeteksi (delta=%.1f%%). Timer dimulai.\n", delta);
      }

      Serial.printf("[AWAIT] kap=%.1f%% baseline=%.1f%% delta=%.1f%%\n",
                    kapasitasSaatIni, kapasitasBaseline, delta);

      if (delta >= THROW_MIN_DELTA) {
        Serial.printf("[THROW] Sampah masuk! delta=%.1f%% QR=%s\n",
                      delta, pendingQrCode.c_str());

        String qrKirim = pendingQrCode;
        float  sebelum = kapasitasBaseline;
        float  sesudah = kapasitasSaatIni;

        // Reset state SEBELUM kirim HTTP (hindari double throw)
        lastThrowTime    = millis();
        resetSesiThrow(false);

        // Update baseline cepat setelah throw
        jarakBaseline     = jarakSaatIni;
        kapasitasBaseline = sesudah;

        // LED biru: 3 kedip cepat konfirmasi throw berhasil
        ledThrowSuccess();

        // [v2.1] kirimEventThrow non-blocking, hasil tampil via override
        kirimEventThrow(qrKirim, sebelum, sesudah);

      } else if (delta > 0) {
        // Ada gerakan tapi belum cukup — LCD menampilkan "awaiting" (handled di lcdUpdate)
        Serial.printf("[AWAIT] Delta kecil: %.1f%% (butuh %.1f%%)\n", delta, THROW_MIN_DELTA);
      }
    }
  }

  // ── 4. DETEKSI BIN DIKOSONGKAN ───────────────────────────────
  if (!awaitingThrow && sensorValid) {
    if (kapasitasSaatIni < 5.0 && jarakBaseline > 0 &&
        jarakSaatIni > jarakBaseline * 0.95) {
      Serial.println("[BIN] Bin dikosongkan. Re-kalibrasi baseline.");
      kalibrasBaseline(3);
      kapasitasSaatIni = 0.0;
      updateKapasitasBin(0.0);
      lastUpdateServer = now;
    }
  }

  // ── 5. KIRIM UPDATE PERIODIK KE SERVER ───────────────────────
  if (now - lastUpdateServer >= INTERVAL_UPDATE_SERVER) {
    lastUpdateServer = now;
    if (sensorValid) {
      updateKapasitasBin(kapasitasSaatIni);
    } else {
      Serial.println("[HTTP] Skip bin/update — sensor error.");
    }
  }

  // ── 6. UPDATE LEADERBOARD ────────────────────────────────────
  if (now - lastLeaderboard >= INTERVAL_LEADERBOARD) {
    lastLeaderboard = now;
    updateLeaderboardData();
  }

  // ── 7. UPDATE LCD (non-blocking, dihandle oleh lcdUpdate) ────
  lcdUpdate();

  // ── 8. UPDATE LED (non-blocking, dihandle oleh ledUpdate) ────
  ledUpdate();

  delay(50);
}
