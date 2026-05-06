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

static void feedbackMeasure(float distance) {
  uint16_t distInt = (uint16_t)(distance * 10.0f + 0.5f);
  uint8_t  distData[2];
  pack16(distData, distInt);
  sendRS485(HdrMeasure, sizeof(HdrMeasure), distData, sizeof(distData));
  Serial.printf("[FB] Distance sent: %d (=%.1f cm)\n", distInt, distance);
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

static void handleSingle() {
  Serial.println("[SINGLE] Membaca jarak...");

  // ===== DUMMY VALUE (komentari blok ini saat sensor I2C siap) =====
  float distance = 123.4f;
  Serial.printf("[SINGLE][DUMMY] dist=%.1f cm\n", distance);
  feedbackMeasure(distance);
  return;
  // ===== END DUMMY =====

  int32_t raw       = as5600.getCumulativePosition();
  int32_t delta     = raw - startRaw;
  float   degree    = delta * 360.0f / 4096.0f;
  float   degreeAdj = degree + 0.44f;
  float   distanceReal = getDistance(degreeAdj);

  Serial.printf("[SINGLE] raw=%d, delta=%d, degAdj=%.2f, dist=%.1f cm\n",
                raw, delta, degreeAdj, distanceReal);

  feedbackMeasure(distanceReal);
}

static void handleMode() {
  switch (data1) {
    case 0x01:
      Serial.println("Standby Mode");
      feedbackStandby();
      doStandby();
      break;

    case 0x02:
      Serial.println("Operation Mode");
      doOperation();
      feedbackOperation();
      break;

    case 0x03:
      Serial.println("Tare");
      doTare();
      feedbackTare();
      break;

    case 0x04:
      Serial.println("Device Restart");
      feedbackRestart();
      doRestart();
      break;

    default:
      Serial.println("data1 tidak dikenal");
      break;
  }
}

void handleCmd() {
  switch (cmdType) {
    case 0x00:
      if (state == Operation) handleSingle();
      else                    feedbackError();
      break;

    case 0x02:
      handleMode();
      break;

    default:
      Serial.println("cmdType tidak dikenal");
      break;
  }
  resetBuff();
}
