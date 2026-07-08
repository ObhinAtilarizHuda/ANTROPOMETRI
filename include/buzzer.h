#pragma once
#include <Arduino.h>

void initBuzzer();
void startBeep(uint32_t durationMs);                            // satu beep selama durationMs
void startBeeps(uint8_t count, uint32_t onMs, uint32_t offMs);  // beep berulang count kali (pola)
void handleBuzzer();                                            // dipanggil tiap loop, non-blocking
