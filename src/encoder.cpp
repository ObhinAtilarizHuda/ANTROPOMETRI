#include "encoder.h"

AS5600  as5600;
int32_t startRaw = 0;

// Kalibrasi jarak piecewise-linear dari posisi encoder (derajat akumulatif)
float getDistance(float x) {
  if      (x >= 0.44   && x <= 399.2)   return 0.0374f*x + 0.0487f;
  else if (x > 399.2   && x <= 801.12)  return 0.0373f*x + 0.108f;
  else if (x > 801.12  && x <= 1216.67) return 0.0362f*x + 1.04f;
  else if (x > 1216.67 && x <= 1638.81) return 0.0355f*x + 1.83f;
  else if (x > 1638.81 && x <= 2068.33) return 0.0349f*x + 2.83f;
  else if (x > 2068.33 && x <= 2504.09) return 0.0342f*x + 4.26f;
  else if (x > 2504.09 && x <= 2956.73) return 0.0332f*x + 6.93f;
  else if (x > 2956.73 && x <= 3421.05) return 0.0330f*x + 7.49f;
  else if (x > 3421.05 && x <= 3879.49) return 0.0320f*x + 10.7f;
  else if (x > 3879.49 && x <= 4356.74) return 0.0314f*x + 13.4f;
  return 0.0f;
}

void doTare() {
  startRaw = as5600.getCumulativePosition();
  Serial.printf("[TARE] startRaw=%d (degree reset to 0.44)\n", startRaw);
}

void pollEncoder() {
  as5600.getCumulativePosition();
}
