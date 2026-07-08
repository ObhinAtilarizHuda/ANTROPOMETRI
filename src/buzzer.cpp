#include "buzzer.h"
#include "config.h"

static uint8_t  beepsLeft  = 0;      // sisa jumlah ON yang harus dibunyikan
static bool     inOn       = false;  // sedang fase ON (true) atau OFF-gap (false)
static uint32_t phaseStart = 0;
static uint32_t onDur      = 0;
static uint32_t offDur     = 0;

void initBuzzer() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
}

// Mulai pola: count beep, tiap ON selama onMs, jeda OFF antar beep offMs.
void startBeeps(uint8_t count, uint32_t onMs, uint32_t offMs) {
  if (count == 0) return;
  onDur      = onMs;
  offDur     = offMs;
  beepsLeft  = count;
  inOn       = true;
  phaseStart = millis();
  digitalWrite(BUZZER_PIN, BUZZER_ON);
}

void startBeep(uint32_t durationMs) {
  startBeeps(1, durationMs, 0);
}

void handleBuzzer() {
  if (beepsLeft == 0 && !inOn) return;   // idle

  uint32_t now = millis();

  if (inOn) {
    if (now - phaseStart >= onDur) {     // selesai satu beep
      digitalWrite(BUZZER_PIN, BUZZER_OFF);
      beepsLeft--;
      inOn       = false;
      phaseStart = now;                  // mulai jeda OFF (jika masih ada sisa)
    }
  } else {
    if (beepsLeft > 0 && (now - phaseStart >= offDur)) {
      digitalWrite(BUZZER_PIN, BUZZER_ON);
      inOn       = true;
      phaseStart = now;
    }
  }
}
