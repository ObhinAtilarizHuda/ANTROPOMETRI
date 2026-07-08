#pragma once
#include <Arduino.h>

void initButton();
void handleButton();         // poll & deteksi gesture, panggil tiap loop
bool takeTareGesture();      // true sekali bila tombol ditahan >= TARE_HOLD_MS (long press)
bool takeSendGesture();      // true sekali bila klik pendek (tap) terdeteksi
void clearButtonGestures();  // buang event yang masih pending (mis. saat arming/cancel)
