#include "button.h"
#include "config.h"

// Tombol active-low (INPUT_PULLUP): HIGH = lepas, LOW = ditekan.
static bool     lastRead     = HIGH;
static bool     stableState  = HIGH;
static uint32_t lastChangeMs = 0;

static bool tareEvt = false;
static bool sendEvt = false;

#if RS485_MODE
static uint32_t pressStartMs = 0;
static bool     longFired    = false;   // long-press sudah difire untuk tekanan ini
#endif

void initButton() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  lastRead = stableState = digitalRead(BUTTON_PIN);
}

void handleButton() {
  bool reading = digitalRead(BUTTON_PIN);

  // Reset timer debounce tiap pembacaan mentah berubah
  if (reading != lastRead) {
    lastRead     = reading;
    lastChangeMs = millis();
  }

  bool stateChanged = ((millis() - lastChangeMs) >= BTN_DEBOUNCE_MS && reading != stableState);

#if RS485_MODE
  // Mode RS485: tahan 5 detik => tare, klik sekali => kirim ke master
  if (stateChanged) {
    stableState = reading;

    if (stableState == LOW) {                 // falling edge: mulai ditekan
      pressStartMs = millis();
      longFired    = false;
    } else {                                   // rising edge: dilepas
      uint32_t held = millis() - pressStartMs;
      if (!longFired && held < TARE_HOLD_MS) { // tap pendek (bukan long-press) => kirim
        sendEvt = true;
      }
    }
  }

  // Long-press: masih ditekan dan sudah menahan >= TARE_HOLD_MS
  if (stableState == LOW && !longFired && (millis() - pressStartMs) >= TARE_HOLD_MS) {
    longFired = true;
    tareEvt   = true;
  }
#else
  // Mode Test: sekali klik => tare
  if (stateChanged) {
    stableState = reading;
    if (stableState == LOW) tareEvt = true;  // falling edge: klik = tare
  }
#endif
}

bool takeTareGesture() { if (tareEvt) { tareEvt = false; return true; } return false; }
bool takeSendGesture() { if (sendEvt) { sendEvt = false; return true; } return false; }

void clearButtonGestures() {
  tareEvt = false;
  sendEvt = false;
}
