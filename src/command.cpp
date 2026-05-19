#include "command.h"
#include "rs485.h"
#include "encoder.h"
#include <WiFi.h>
#include "esp_bt.h"

State    state              = Operation;
volatile bool buttonPressed  = false;
bool     measurementRequested = false;

static uint32_t lastSentMs        = 0;
static const uint32_t AUTO_SEND_MS = 3000;  // auto-send jika tidak ada pengiriman selama 3 detik

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

// Reply CMD 0x00: distance (cm×100) + angle (deg×100) + statusInt (×100), 3 byte big-endian each.
// Format: D5 AA 0C 89 [ADDR] 00 [DIST×3 ANGLE×3 STATUS×3] [CRC_H CRC_L]
static void feedbackMeasure(float lengthCm, float angleDeg, int32_t statusInt) {
  int32_t lengthInt = (int32_t)(roundf(lengthCm * 10.0f)) * 10;
  int32_t angleInt  = (int32_t)(roundf(angleDeg  * 10.0f)) * 10;

  uint8_t data[9];
  pack24(data + 0, (uint32_t)lengthInt);
  pack24(data + 3, (uint32_t)angleInt);
  pack24(data + 6, (uint32_t)statusInt);

  sendRS485(HdrMeasure, sizeof(HdrMeasure), data, sizeof(data));
  Serial.printf("[FB] Measure: dist=%.2f cm (%d), angle=%.2f deg (%d), status=%d\n",
                lengthCm, lengthInt, angleDeg, angleInt, statusInt);
}

static void feedbackStandby()   { sendRS485(HdrStandby,   sizeof(HdrStandby),   nullptr, 0); Serial.println("[FB] Standby ack"); }
static void feedbackOperation() { sendRS485(HdrOperation, sizeof(HdrOperation), nullptr, 0); Serial.println("[FB] Operation ack"); }
static void feedbackTare()      { sendRS485(HdrTare,      sizeof(HdrTare),      nullptr, 0); Serial.println("[FB] Tare ack"); }
static void feedbackRestart()   { sendRS485(HdrRestart,   sizeof(HdrRestart),   nullptr, 0); Serial.println("[FB] Restart ack"); }
static void feedbackError()     { sendRS485(HdrFbError,   sizeof(HdrFbError),   nullptr, 0); Serial.println("[FB] Error ack"); }

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
  if (data1 != 0x00) {
    Serial.printf("[CMD 0x00] reserved tidak valid: 0x%02X\n", data1);
    feedbackError();
    return;
  }

  buttonPressed = false;
  float lengthCm, angleAdj;
  if (!readEncoder(lengthCm, angleAdj)) {
    feedbackError();
    return;
  }
  feedbackMeasure(lengthCm, angleAdj, 0);
  lastSentMs = millis();
  measurementRequested = true;
  Serial.println("[CMD 0x00] Single measurement sent, auto-send active");
}

void loopMeasurement() {
  if (!measurementRequested) return;

  if (buttonPressed) {
    buttonPressed = false;
    measurementRequested = false;
    float lengthCm, angleAdj;
    if (!readEncoder(lengthCm, angleAdj)) {
      feedbackError();
      return;
    }
    feedbackMeasure(lengthCm, angleAdj, 100);
    Serial.println("[CMD 0x00] Stopped by GPIO0");
    return;
  }

  if (millis() - lastSentMs < AUTO_SEND_MS) return;

  float lengthCm, angleAdj;
  if (!readEncoder(lengthCm, angleAdj)) {
    feedbackError();
    measurementRequested = false;
    return;
  }
  feedbackMeasure(lengthCm, angleAdj, 0);
  lastSentMs = millis();
  Serial.println("[AUTO] Measurement auto-send");
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

    case 0x03:    // Tare
      Serial.println("Tare");
      doTare();
      feedbackTare();
      break;

    case 0x04:    // Restart
      Serial.println("Device Restart");
      feedbackRestart();
      doRestart();
      break;

    default:
      Serial.printf("data1 tidak dikenal: 0x%02X\n", data1);
      feedbackError();
      break;
  }
}

void handleCmd() {
  switch (cmdType) {
    case 0x00:    // Measurement
      if (state == Operation) handleMeasurement();
      else                    feedbackError();
      break;

    case 0x02:    // Control
      handleMode();
      break;

    default:
      Serial.printf("cmdType tidak dikenal: 0x%02X\n", cmdType);
      feedbackError();
      break;
  }
  resetBuff();
}
