#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "rs485.h"
#include "encoder.h"
#include "command.h"
#include "buzzer.h"
#include "button.h"

void setup() {
  delay(1000);
  Serial.begin(115200);

#if RS485_MODE
  init485();
#endif

  // Tombol — RS485: tahan 5 detik (tare) / klik (kirim); Test: klik (tare)
  initButton();
  initBuzzer();

  Wire.begin(SDA_PIN, SCL_PIN);
  as5600.begin(DIR_PIN);                          // DIR (GPIO4) dikontrol via library
  as5600.setDirection(AS5600_COUNTERCLOCK_WISE);  // DIR = HIGH => arah hitung CCW
  Serial.print("AS5600 Connected: ");
  Serial.println(as5600.isConnected());
  delay(500);
  doTare();
  sessionZeroRaw = startRaw;   // nol-sesi (home fisik) — referensi diam, tak digeser tare berikutnya

#if RS485_MODE
  Serial.println("Device Ready (RS485 Mode)");
#else
  Serial.println("Sensor Test Mode — klik tombol untuk Tare");
#endif
}

void loop() {
  handleButton();   // deteksi gesture tombol (per mode)
  handleBuzzer();   // matikan buzzer otomatis saat durasi beep habis

#if RS485_MODE
  read485();
  pollEncoder();
  loopTare();         // tahan tombol 5 detik => tare + lapor ke master (inisiatif user)
  loopMeasurement();

  if (packetReady) {
    packetReady = false;
    Serial.printf("[LOOP] targetID=0x%02X, source=0x%02X, cmdType=0x%02X\n", targetID, source, cmdType);

    if (targetID == SLAVE_ADDRESS) {
      Serial.println("[OK] targetID match, execute handleCmd");
      handleCmd();
    } else {
      Serial.printf("[REJECT] targetID mismatch: expected 0x%02X, got 0x%02X\n", SLAVE_ADDRESS, targetID);
      resetBuff();
    }
  }
#else
  if (takeTareGesture()) {           // sekali klik => tare (bila pita home) + beep
    float d = distFromSessionZero();
    if (d < TARE_HOME_TOL_CM) {
      doTare();
      startBeep(TARE_BEEP_MS);
    } else {
      startBeep(TARE_INVALID_BEEP_MS);   // beep panjang: tare ditolak, pita belum home
      Serial.printf("[TEST][TARE] ditolak — pita belum home (%.1f cm >= %.1f)\n", d, TARE_HOME_TOL_CM);
    }
  }
  testSensor();   // baca & tampilkan sensor di Serial Monitor
#endif
}
