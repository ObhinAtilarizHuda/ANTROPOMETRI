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

// Reply single measure: 5 nilai (W, TL, TR, BL, BR), masing-masing × 100, dikemas 3 byte big-endian
// Format: D5 AA 12 89 01 00 [W_H W_M W_L] [TL_H TL_M TL_L] [TR_...] [BL_...] [BR_...] [CRC_H CRC_L]
static void feedbackMeasure(float w, float tl, float tr, float bl, float br) {
  uint32_t wInt  = (uint32_t)(w  * 100.0f + 0.5f);
  uint32_t tlInt = (uint32_t)(tl * 100.0f + 0.5f);
  uint32_t trInt = (uint32_t)(tr * 100.0f + 0.5f);
  uint32_t blInt = (uint32_t)(bl * 100.0f + 0.5f);
  uint32_t brInt = (uint32_t)(br * 100.0f + 0.5f);

  uint8_t data[15];
  pack24(&data[0],  wInt);
  pack24(&data[3],  tlInt);
  pack24(&data[6],  trInt);
  pack24(&data[9],  blInt);
  pack24(&data[12], brInt);

  sendRS485(HdrMeasure, sizeof(HdrMeasure), data, sizeof(data));
  Serial.printf("[FB] Measure: W=%.2f TL=%.2f TR=%.2f BL=%.2f BR=%.2f\n", w, tl, tr, bl, br);
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
  Serial.println("[SINGLE] Membaca 5 nilai...");

  // ===== DUMMY VALUES (komentari blok ini saat sensor I2C siap) =====
  float w  = 100.00f;   // Width
  float tl = 25.50f;    // Top Left
  float tr = 25.75f;    // Top Right
  float bl = 26.00f;    // Bottom Left
  float br = 26.25f;    // Bottom Right
  Serial.printf("[SINGLE][DUMMY] W=%.2f TL=%.2f TR=%.2f BL=%.2f BR=%.2f\n", w, tl, tr, bl, br);
  feedbackMeasure(w, tl, tr, bl, br);
  return;
  // ===== END DUMMY =====

  // TODO: baca dari sensor I2C asli, lalu panggil feedbackMeasure(w, tl, tr, bl, br)
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
