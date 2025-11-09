#include <Arduino.h>

// Pin mapping berdasarkan platform
#ifdef ESP8266
  // ESP8266 generic module pin mapping
  #define RELAY_HEATER_PIN       14   // GPIO4
  #define WATER_EMPTY_LED_PIN    13   // GPIO5 / RED WIRE
  #define BUILTIN_LED_PIN        5   // GPIO2
  #define SENSOR_WATER_LEVEL_PIN 12  // GPIO12 / COKELAT WIRE
  #define RELAY_PUMP_PIN         16  // GPIO13
#else
  // ESP32 pin mapping
  #define RELAY_HEATER_PIN       16
  #define WATER_EMPTY_LED_PIN    17
  #define SENSOR_WATER_LEVEL_PIN 18
  #define RELAY_PUMP_PIN         19
#endif

#define ON_STATE  HIGH
#define OFF_STATE LOW

// --- Wiring Relay ---
// REKOMENDASI: Gunakan terminal NO (Normally Open) untuk HEATER dan POMPA
// 
// Relay Module biasanya memiliki 3 terminal:
// - COM (Common): Koneksi ke sumber listrik (L/N)
// - NO (Normally Open): Kontak terbuka saat relay OFF, tertutup saat relay ON
// - NC (Normally Closed): Kontak tertutup saat relay OFF, terbuka saat relay ON
//
// Wiring yang DISARANKAN:
// - HEATER: COM -> Sumber listrik, NO -> Heater (Fail-safe: mati saat relay OFF)
// - POMPA:  COM -> Sumber listrik, NO -> Pompa (Fail-safe: mati saat relay OFF)
//
// Mengapa NO?
// 1. Fail-safe: Jika relay mati/error, peralatan tidak akan menyala
// 2. Standar untuk peralatan listrik
// 3. Lebih aman untuk aplikasi otomatis

// --- Wiring Sensor Air (Water Level Sensor) ---
// REKOMENDASI: Gunakan Float Switch atau Reed Switch
//
// Tipe Sensor yang Didukung:
// 1. FLOAT SWITCH (Mekanis)
//    - Switch tertutup saat air penuh (float naik)
//    - Switch terbuka saat air kosong (float turun)
//
// 2. REED SWITCH (Magnetik)
//    - Switch tertutup saat magnet dekat (air penuh)
//    - Switch terbuka saat magnet jauh (air kosong)
//
// Wiring Sensor:
// - Satu terminal sensor -> GND (Ground)
// - Terminal lainnya -> SENSOR_WATER_LEVEL_PIN (GPIO12 untuk ESP8266)
// - Pin menggunakan INPUT_PULLUP (internal pull-up resistor)
//
// Cara Kerja (SENSOR_ACTIVE_LOW = true):
// - Air PENUH:  Switch tertutup ke GND -> Pin membaca LOW  -> waterOk = true
// - Air KOSONG: Switch terbuka        -> Pin membaca HIGH -> waterOk = false
//
// Jika logika terbalik saat dites, ubah SENSOR_ACTIVE_LOW ke false.

// --- Konfigurasi ---
const bool SENSOR_ACTIVE_LOW = true;
// true  = WATER_OK jika pembacaan LOW (switch ke GND saat penuh)
// false = WATER_OK jika pembacaan HIGH (switch ke VCC saat penuh)
//
// Jika logika terbalik saat dites, ubah ke false.

// interval (non-blocking)
const unsigned long SCAN_INTERVAL_MS   = 100;     // baca sensor tiap 100ms
const unsigned long LED_BLINK_INTERVAL_MS = 500;  // interval blink LED 500ms
const unsigned long GALLON_EMPTY_DETECT_MS = 300000; // deteksi galon kosong setelah 5 menit

// --- State ---
unsigned long lastScanMs   = 0;
bool pumpOn                = false;
unsigned long pumpStartMs  = 0;
unsigned long lastLedBlinkMs = 0;
bool ledWaterEmptyState              = false;
bool gallonEmpty                    = false;
bool lastWaterOkState                = false;
bool lastHeaterState                 = false;
unsigned long gallonEmptyDetectStartMs = 0;  // timer terpisah untuk deteksi galon kosong
bool gallonEmptyDetectActive          = false;  // flag apakah timer deteksi aktif
bool waterEmptyLedShouldBeActive      = false;  // flag apakah LED seharusnya aktif

void setPump(bool on) {
  if (on && !pumpOn) {
    pumpOn = true;
    pumpStartMs = millis();
    digitalWrite(RELAY_PUMP_PIN, ON_STATE);
    Serial.println("[PUMP] ON");
  } else if (!on && pumpOn) {
    pumpOn = false;
    digitalWrite(RELAY_PUMP_PIN, OFF_STATE);
    Serial.println("[PUMP] OFF");
  }
}

void setHeater(bool on) {
  if (on != lastHeaterState) {
    digitalWrite(RELAY_HEATER_PIN, on ? ON_STATE : OFF_STATE);
    lastHeaterState = on;
    Serial.print("[HEATER] ");
    Serial.println(on ? "ON" : "OFF");
  }
}

void setWaterEmptyLed(bool on) {
  digitalWrite(WATER_EMPTY_LED_PIN, on ? ON_STATE : OFF_STATE);
  digitalWrite(BUILTIN_LED_PIN, on ? ON_STATE : OFF_STATE);
}

void updateWaterEmptyLed(bool shouldBeActive, unsigned long now) {
  if (shouldBeActive) {
    // LED aktif: berkedip
    if (now - lastLedBlinkMs >= LED_BLINK_INTERVAL_MS) {
      lastLedBlinkMs = now;
      ledWaterEmptyState = !ledWaterEmptyState;
      setWaterEmptyLed(ledWaterEmptyState);
    }
  } else {
    // LED tidak aktif: mati
    if (ledWaterEmptyState) {
      ledWaterEmptyState = false;
      setWaterEmptyLed(false);
    }
  }
}

// baca status level air -> true = cukup / OK, false = kosong/rendah
bool isWaterLevelOk() {
  int v = digitalRead(SENSOR_WATER_LEVEL_PIN);
  if (SENSOR_ACTIVE_LOW) {
    return (v == LOW);
  } else {
    return (v == HIGH);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(SENSOR_WATER_LEVEL_PIN, INPUT_PULLUP);   // float switch ke GND
  pinMode(RELAY_HEATER_PIN, OUTPUT);
  pinMode(WATER_EMPTY_LED_PIN, OUTPUT);
  pinMode(BUILTIN_LED_PIN, OUTPUT);
  pinMode(RELAY_PUMP_PIN, OUTPUT);

  digitalWrite(RELAY_HEATER_PIN, OFF_STATE);
  digitalWrite(WATER_EMPTY_LED_PIN, OFF_STATE);
  digitalWrite(BUILTIN_LED_PIN, OFF_STATE);
  digitalWrite(RELAY_PUMP_PIN, OFF_STATE);

  lastHeaterState = false;
  lastWaterOkState = isWaterLevelOk();

  Serial.println("Starting...");
  #ifdef ESP8266
    Serial.println("[PLATFORM] ESP8266");
  #else
    Serial.println("[PLATFORM] ESP32");
  #endif
  Serial.print("[PIN] RELAY_HEATER_PIN: ");
  Serial.println(RELAY_HEATER_PIN);
  Serial.print("[PIN] WATER_EMPTY_LED_PIN: ");
  Serial.println(WATER_EMPTY_LED_PIN);
  Serial.print("[PIN] BUILTIN_LED_PIN: ");
  Serial.println(BUILTIN_LED_PIN);
  Serial.print("[PIN] SENSOR_WATER_LEVEL_PIN: ");
  Serial.println(SENSOR_WATER_LEVEL_PIN);
  Serial.print("[PIN] RELAY_PUMP_PIN: ");
  Serial.println(RELAY_PUMP_PIN);
  Serial.print("SENSOR_ACTIVE_LOW: ");
  Serial.println(SENSOR_ACTIVE_LOW ? "true" : "false");
  Serial.print("GALLON_EMPTY_DETECT_MS: ");
  Serial.println(GALLON_EMPTY_DETECT_MS);
}

void loop() {
  unsigned long now = millis();

  // non-blocking periodic scan
  if (now - lastScanMs >= SCAN_INTERVAL_MS) {
    lastScanMs = now;

    bool waterOk = isWaterLevelOk();

    // log perubahan status level air
    if (waterOk != lastWaterOkState) {
      lastWaterOkState = waterOk;
      Serial.print("[WATER] Level: ");
      Serial.println(waterOk ? "OK" : "LOW");
    }

    // manajemen timer deteksi galon kosong
    // PENTING: Timer selalu divalidasi oleh sensor water
    // - Jika sensor mendeteksi air PENUH (LOW), timer di-reset dan pompa stop
    // - Timer hanya aktif jika pompa ON dan air masih KURANG
    if (pumpOn && !waterOk) {
      // kondisi: pompa aktif dan air masih kurang
      if (!gallonEmptyDetectActive) {
        // mulai timer deteksi galon kosong
        gallonEmptyDetectActive = true;
        gallonEmptyDetectStartMs = now;
        Serial.println("[TIMER] Deteksi galon kosong dimulai");
      }
    } else if (waterOk) {
      // VALIDASI SENSOR: air sudah cukup (sensor LOW = penuh)
      // - Timer di-reset karena sensor sudah mendeteksi air penuh
      // - Pompa akan di-stop oleh logika kontrol di bawah
      if (gallonEmptyDetectActive) {
        gallonEmptyDetectActive = false;
        Serial.println("[TIMER] Deteksi galon kosong di-reset - air sudah cukup (sensor validasi)");
      }
      // reset deteksi galon kosong jika sebelumnya terdeteksi
      if (gallonEmpty) {
        gallonEmpty = false;
        Serial.println("[INFO] Galon kosong reset - air sudah cukup");
      }
    } else {
      // pompa mati tapi air masih kurang: hentikan timer
      if (gallonEmptyDetectActive) {
        gallonEmptyDetectActive = false;
      }
    }

    // deteksi galon kosong: timer mencapai batas waktu (hanya jika sensor belum mendeteksi penuh)
    // Catatan: Jika sensor sudah mendeteksi penuh, timer sudah di-reset di atas
    if (gallonEmptyDetectActive && (now - gallonEmptyDetectStartMs > GALLON_EMPTY_DETECT_MS)) {
      gallonEmpty = true;
      gallonEmptyDetectActive = false;  // stop timer
      Serial.println("[ALERT] Galon kosong terdeteksi!");
      setPump(false);            // matikan pompa karena galon kosong
      setHeater(false);          // matikan heater untuk aman
    }

    // --- LOGIKA KONTROL POMPA ---
    // POMPA BEKERJA JIKA:
    // 1. Air di tank KURANG/KOSONG (waterOk = false)
    // 2. Galon MASIH ADA (gallonEmpty = false)
    //
    // POMPA BERHENTI JIKA:
    // 1. Air di tank SUDAH CUKUP (waterOk = true) - SENSOR MENDETEKSI AIR PENUH (LOW)
    //    -> Timer deteksi galon kosong juga di-reset
    // 2. Galon KOSONG terdeteksi (gallonEmpty = true) - pompa > 5 menit tapi air masih kurang
    //
    // PRIORITAS: Sensor water SELALU memvalidasi dan menghentikan pompa jika air sudah penuh

    if (gallonEmpty) {
      // galon kosong terdeteksi: LED berkedip, pompa MATI
      setPump(false);           // pastikan pompa mati
      setHeater(false);         // matikan heater untuk aman
      waterEmptyLedShouldBeActive = true;
    } else if (waterOk) {
      // air cukup di tank: pompa MATI, heater BOLEH AKTIF
      setPump(false);           // pompa berhenti
      waterEmptyLedShouldBeActive = false; // LED mati
      setHeater(true);          // heater boleh aktif (asumsi seperti modul asli)
    } else {
      // air kurang / kosong di tank, tapi galon masih ada: pompa AKTIF
      setPump(true);            // isi tank - POMPA BEKERJA
      setHeater(false);         // jangan panaskan tanpa air
      waterEmptyLedShouldBeActive = false; // LED mati karena galon masih ada
    }
  }

  // update LED setiap loop untuk kedipan yang lancar
  updateWaterEmptyLed(waterEmptyLedShouldBeActive, now);
}
