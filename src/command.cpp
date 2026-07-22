#include "command.h"
#include "rs485.h"
#include "encoder.h"
#include "buzzer.h"
#include "button.h"
#include <WiFi.h>
#include "esp_bt.h"

State    state              = Operation;
bool     measurementRequested = false;

static uint32_t lastSentMs              = 0;
static uint32_t measurementStartMs      = 0;
static int32_t  lastSentDistInt         = 0;
static const uint32_t MIN_SEND_MS           = 20;     // rate limit antar pengiriman (ms)
static const uint32_t INACTIVITY_TIMEOUT_MS = 60000;  // timeout 1 menit tanpa tombol (ms)

// --- Power Management ---
// Standby: matikan radio WiFi/BT & turunkan CPU clock.
// GPIO + UART tetap aktif, jadi RS485 tetap bisa terima perintah dari master.

static void enterStandby() {
  Serial.println("[STANDBY] Power down periferal...");
  Serial.flush();

  // Matikan radio WiFi (penghemat daya terbesar, ~80mA)
  WiFi.mode(WIFI_OFF);

  // Matikan Bluetooth
  btStop();

  // Turunkan CPU 240MHz -> 80MHz (APB tetap 80MHz, UART 230400 tetap akurat)
  setCpuFrequencyMhz(80);

  Serial.println("[STANDBY] CPU 80MHz, listening RS485 only");
}

static void exitStandby() {
  // Kembalikan CPU ke 240MHz untuk mode normal
  setCpuFrequencyMhz(240);
  Serial.println("[OPERATION] CPU 240MHz, mode normal");
}

// --- Feedback helpers ---

// Reply CMD 0x00: distance sent to master = internal distance + MEASUREMENT_TX_OFFSET_CM.
// Angle and local monitor values stay based on the raw internal measurement.
// Format: D5 AA 0C 89 [ADDR] 00 [DIST×3 ANGLE×3 STATUS×3] [CRC_H CRC_L]
static void feedbackMeasure(float lengthCm, float angleDeg, int32_t statusInt) {
  float   txLengthCm = lengthCm + MEASUREMENT_TX_OFFSET_CM;
  int32_t lengthInt  = (int32_t)(roundf(txLengthCm * 10.0f)) * 10;
  int32_t angleInt   = (int32_t)(roundf(angleDeg   * 10.0f)) * 10;

  uint8_t data[9];
  pack24(data + 0, (uint32_t)lengthInt);
  pack24(data + 3, (uint32_t)angleInt);
  pack24(data + 6, (uint32_t)statusInt);

  sendRS485(HdrMeasure, sizeof(HdrMeasure), data, sizeof(data));
  Serial.printf("[FB] Measure: internal=%.2f cm, tx=%.2f cm (%d), angle=%.2f deg (%d), status=%d\n",
                lengthCm, txLengthCm, lengthInt, angleDeg, angleInt, statusInt);
}

static void feedbackStandby()   { sendRS485(HdrStandby,   sizeof(HdrStandby),   nullptr, 0); Serial.println("[FB] Standby ack"); }
static void feedbackOperation() { sendRS485(HdrOperation, sizeof(HdrOperation), nullptr, 0); Serial.println("[FB] Operation ack"); }
static void feedbackTare()      { sendRS485(HdrTare,      sizeof(HdrTare),      nullptr, 0); Serial.println("[FB] Tare ack"); }
static void feedbackRestart()   { sendRS485(HdrRestart,   sizeof(HdrRestart),   nullptr, 0); Serial.println("[FB] Restart ack"); }
static void feedbackCancel()    { sendRS485(HdrCancel,    sizeof(HdrCancel),    nullptr, 0); Serial.println("[FB] Cancel ack"); }
static void feedbackError(uint8_t code) { sendError(code); }   // frame CMD 0x03 + kode error

// --- Actions ---

static void doStandby() {
  measurementRequested = false;
  state = Standby;
  enterStandby();
}

static void doOperation() {
  if (state == Standby) exitStandby();
  state = Operation;
}

static void doRestart() {
  ESP.restart();
}

static void doCancel() {
  measurementRequested = false;
  clearButtonGestures();
}

// --- Command handlers ---

static bool readEncoder(float &lengthCm, float &angleDeg) {
  if (!as5600.isConnected()) {
    Serial.println("[ENCODER] AS5600 tidak terdeteksi - reading timeout");
    return false;
  }
  int32_t rawPosition = as5600.getCumulativePosition();
  int32_t deltaRaw    = rawPosition - startRaw;
  float   degree      = (float)deltaRaw * 360.0f / 4096.0f;
  angleDeg = degree + 0.44f;          // degreeAdjusted, kembali ke 0.44 saat tare
  lengthCm = getDistance(angleDeg);   // ikuti reference: getDistance(degreeAdjusted)
  return true;
}

static void handleMeasurement() {
  if (data1 != 0x00) {               // reserved byte harus 0x00
    Serial.printf("[CMD 0x00] reserved tidak valid: 0x%02X\n", data1);
    feedbackError(ERR_CHECKSUM);
    return;
  }

  if (!as5600.isConnected()) {
    Serial.println("[CMD 0x00] AS5600 tidak terkoneksi");
    feedbackError(ERR_SENSOR_NO_RESPONSE);
    return;
  }

  // Posisi pita saat request BEBAS: urutan "request dulu baru tarik" maupun
  // "tarik dulu baru request" sama-sama valid — deltaRaw dari counter kumulatif
  // benar kapan pun dibaca. Integritas titik nol dijaga di jalur tare
  // (penjaga TARE_HOME_TOL_CM), bukan di sini.
  clearButtonGestures();             // buang gesture lama agar tidak langsung terkirim
  measurementStartMs  = millis();
  lastSentMs          = millis();
  lastSentDistInt     = INT32_MIN;   // force first monitor print
  measurementRequested = true;
  Serial.println("[CMD 0x00] Armed, monitoring on Serial — waiting for single click to send");
}

void loopMeasurement() {
  if (!measurementRequested) return;

  // Inactivity timeout: 1 menit tanpa tombol
  if (millis() - measurementStartMs >= INACTIVITY_TIMEOUT_MS) {
    Serial.println("[TIMEOUT] 1 min no button, measurement cancelled");
    feedbackError(ERR_TIMEOUT);
    measurementRequested = false;
    return;
  }

  // Klik tombol → kirim satu paket ke master, lalu stop
  if (takeSendGesture()) {
    measurementRequested = false;
    float lengthCm, angleAdj;
    if (!readEncoder(lengthCm, angleAdj)) {
      feedbackError(ERR_SENSOR_NO_RESPONSE);
      return;
    }
    feedbackMeasure(lengthCm, angleAdj, 100);
    startBeep(MEASURE_BEEP_MS);   // beep tanda measurement selesai dikirim ke master
    Serial.println("[CMD 0x00] Sent on single click");
    return;
  }

  // Rate limit polling encoder untuk Serial monitor
  if (millis() - lastSentMs < MIN_SEND_MS) return;
  lastSentMs = millis();

  float lengthCm, angleAdj;
  if (!readEncoder(lengthCm, angleAdj)) {
    feedbackError(ERR_SENSOR_NO_RESPONSE);
    measurementRequested = false;
    return;
  }

  int32_t currentDistInt = (int32_t)(roundf(lengthCm * 10.0f)) * 10;
  if (currentDistInt == lastSentDistInt) return;  // tidak ada perubahan, skip print

  Serial.printf("[MONITOR] dist=%.2f cm (%d), angle=%.2f deg — not sent\n",
                lengthCm, currentDistInt, angleAdj);
  lastSentDistInt = currentDistInt;
}

// Tare inisiatif user: tahan tombol >= 5 detik => tare lokal + lapor ke master
// (walaupun master belum meminta). Tare atas perintah master ditangani langsung
// di handleMode() case 0x03.
void loopTare() {
  if (takeTareGesture()) {           // tombol ditahan >= 5 detik
    float d = distFromSessionZero();
    if (d < TARE_HOME_TOL_CM) {      // pita sudah home => tare
      doTare();
      feedbackTare();                // beritahu master walau tidak diminta
      startBeeps(TARE_BEEP_COUNT, TARE_BEEP_MS, TARE_BEEP_MS);   // beep beep beep 3x
      Serial.printf("[TARE] Tahan 5 detik — pita home (%.1f cm), tare & lapor master\n", d);
    } else {                         // pita belum home => tolak
      feedbackError(ERR_TARE_INVALID);
      startBeep(TARE_INVALID_BEEP_MS);   // 1 beep panjang penanda tolak
      Serial.printf("[TARE] Tahan 5 detik — ditolak, pita belum home (%.1f cm >= %.1f)\n", d, TARE_HOME_TOL_CM);
    }
  }
}

static void handleMode() {
  switch (data1) {
    case 0x01:    // Standby
      Serial.println("Standby Mode");
      feedbackStandby();
      doStandby();
      break;

    case 0x02:    // Operation
      Serial.println("Operation Mode");
      doOperation();
      feedbackOperation();
      break;

    case 0x03: {  // Tare: hanya diterima bila pita sudah kembali home
      float d = distFromSessionZero();
      if (d < TARE_HOME_TOL_CM) {
        Serial.printf("Tare requested by master — pita home (%.1f cm), tare\n", d);
        doTare();
        feedbackTare();
        startBeeps(TARE_BEEP_COUNT, TARE_BEEP_MS, TARE_BEEP_MS);   // beep beep beep 3x
      } else {
        Serial.printf("Tare ditolak — pita belum home (%.1f cm >= %.1f)\n", d, TARE_HOME_TOL_CM);
        feedbackError(ERR_TARE_INVALID);
        startBeep(TARE_INVALID_BEEP_MS);   // 1 beep panjang penanda tolak
      }
      break;
    }

    case 0x04:    // Restart
      Serial.println("Device Restart");
      feedbackRestart();
      doRestart();
      break;

    case 0x05:    // Cancel measurement
      Serial.println("Cancel Measurement");
      doCancel();
      feedbackCancel();
      break;

    default:    // subcommand control tidak dikenal
      Serial.printf("data1 tidak dikenal: 0x%02X\n", data1);
      feedbackError(ERR_CHECKSUM);
      break;
  }
}

void handleCmd() {
  switch (cmdType) {
    case 0x00:    // Measurement
      if (state == Operation) handleMeasurement();
      else                    feedbackError(ERR_CHECKSUM);   // request measurement saat Standby
      break;

    case 0x02:    // Control
      handleMode();
      break;

    default:      // cmdType tidak dikenal
      Serial.printf("cmdType tidak dikenal: 0x%02X\n", cmdType);
      feedbackError(ERR_CHECKSUM);
      break;
  }
  resetBuff();
}
