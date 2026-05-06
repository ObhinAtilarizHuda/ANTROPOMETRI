#include "command.h"
#include "rs485.h"
#include "encoder.h"
#include <WiFi.h>
#include "esp_bt.h"

State state = Operation;

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

// Reply CMD 0x00 (Measurement): jarak/panjang (cm × 100), 3 byte big-endian.
// Format: D5 AA 06 89 [ADDR] 00 [DIST_H DIST_M DIST_L] [CRC_H CRC_L]
static void feedbackMeasure(float lengthCm) {
  int32_t lengthInt = (int32_t)(lengthCm * 100.0f + (lengthCm >= 0 ? 0.5f : -0.5f));

  uint8_t data[3];
  pack24(data, (uint32_t)lengthInt);

  sendRS485(HdrMeasure, sizeof(HdrMeasure), data, sizeof(data));
  Serial.printf("[FB] Measure: distance=%.2f cm, encoded=%d\n", lengthCm, lengthInt);
}

static void feedbackStandby()   { sendRS485(HdrStandby,   sizeof(HdrStandby),   nullptr, 0); Serial.println("[FB] Standby ack"); }
static void feedbackOperation() { sendRS485(HdrOperation, sizeof(HdrOperation), nullptr, 0); Serial.println("[FB] Operation ack"); }
static void feedbackTare()      { sendRS485(HdrTare,      sizeof(HdrTare),      nullptr, 0); Serial.println("[FB] Tare ack"); }
static void feedbackRestart()   { sendRS485(HdrRestart,   sizeof(HdrRestart),   nullptr, 0); Serial.println("[FB] Restart ack"); }
static void feedbackError()     { sendRS485(HdrFbError,   sizeof(HdrFbError),   nullptr, 0); Serial.println("[FB] Error ack"); }

// --- Actions ---

static void doStandby() {
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

static void handleMeasurement() {
  Serial.println("[CMD 0x00] Length measurement request");

  if (data1 != 0x00) {
    Serial.printf("reserved measurement tidak valid: 0x%02X\n", data1);
    feedbackError();
    return;
  }

  int32_t rawPosition  = as5600.getCumulativePosition();
  int32_t deltaRaw     = rawPosition - startRaw;
  float   angleDeg     = (float)deltaRaw * 360.0f / 4096.0f;
  float   lengthCm     = getDistance(angleDeg + 0.44f);

  Serial.printf("[CMD 0x00] L=%.2fcm raw=%d delta=%d angle=%.2fdeg\n",
                lengthCm, rawPosition, deltaRaw, angleDeg);
  feedbackMeasure(lengthCm);
}

static void handleMode() {
  switch (data1) {
    case 0x01:    // Tare
      Serial.println("Tare");
      doTare();
      feedbackTare();
      break;

    case 0x0A:    // Standby
      Serial.println("Standby Mode");
      feedbackStandby();
      doStandby();
      break;

    case 0x0B:    // Operation
      Serial.println("Operation Mode");
      doOperation();
      feedbackOperation();
      break;

    case 0x0D:    // Restart
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
