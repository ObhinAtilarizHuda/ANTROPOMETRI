#include "encoder.h"
#include "config.h"

AS5600  as5600;
int32_t startRaw       = 0;
int32_t sessionZeroRaw = 0;

// Kalibrasi jarak piecewise-linear dari posisi encoder (derajat akumulatif)
float getDistance(float x) {
  float d = 0.0f;
  if      (x >= 0.0   && x <= 597.3)   d = 0.0352f*x + -0.031f;
  else if (x > 597.3   && x <= 1476.4)  d = 0.0341f*x + 0.712f;
  else if (x > 1476.4  && x <= 2304.8) d = 0.0326f*x + 2.9f;
  else if (x > 2304.8 && x <= 3068.4) d =  0.0315*x + 5.57f;
  else if (x > 3068.4 && x <= 3656.6) d = 0.0305*x +  8.45f;
  else if (x > 3656.6 && x <= 4689.3) d = 0.0291*x +  13.6f;
  else if (x > 4689.3)                d = 150.0f;   // di luar rentang kalibrasi: tahan di 150 cm
  if (d < 0.0f) d = 0.0f;   // clamp negatif ke 0 (mis. -0.015 saat tare)
  return d;
}

void doTare() {
  startRaw = as5600.getCumulativePosition();
  Serial.printf("[TARE] startRaw=%d (degree reset to 0.44)\n", startRaw);
}

// Jarak (cm) posisi encoder saat ini relatif ke nol-sesi (home fisik saat boot).
// Dipakai penjaga tare: pita dianggap sudah "home" bila hasilnya < TARE_HOME_TOL_CM.
// Beda dari readEncoder() yang mengukur dari startRaw (nol tare yang bergeser),
// fungsi ini selalu mengukur dari sessionZeroRaw yang tetap.
float distFromSessionZero() {
  int32_t raw    = as5600.getCumulativePosition();
  float   degree = (float)(raw - sessionZeroRaw) * 360.0f / 4096.0f;
  return getDistance(degree + 0.44f);
}

void pollEncoder() {
  as5600.getCumulativePosition();
}

// Mode test sensor: baca encoder lalu cetak Degree, Degree Adjust, dan Distance
// ke Serial Monitor dengan 1 angka di belakang koma. Tanpa komunikasi RS485.
void testSensor() {
  // getCumulativePosition() tetap dipanggil tiap loop agar counter akumulatif
  // tidak ketinggalan saat encoder diputar cepat.
  int32_t rawPosition  = as5600.getCumulativePosition();
  int32_t deltaRaw     = rawPosition - startRaw;
  float   degree       = (float)deltaRaw * 360.0f / 4096.0f;
  float   degreeAdjust = degree + 0.44f;          // sama seperti operasi normal (tare -> 0.44)
  float   distance     = getDistance(degreeAdjust);

  // Batasi laju cetak ~10 Hz agar Serial Monitor tidak banjir.
  static uint32_t lastPrintMs = 0;
  if (millis() - lastPrintMs < 100) return;
  lastPrintMs = millis();

  Serial.printf("Degree: %.1f | Degree Adjust: %.1f | Distance: %.1f\n",
                degree, degreeAdjust, distance);
}
