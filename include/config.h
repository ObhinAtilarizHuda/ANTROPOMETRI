#pragma once
#include <Arduino.h>

// RS485 pins (ESP32-C3: GPIO max 21 — sesuaikan jika perlu)
#define RS485_RX   13   // RO
#define RS485_TX   14   // DI
#define RS485_EN   -1   // -1 = auto direction (hardware handle DE/RE), set GPIO pin for manual control

// I2C pins — auto-selected by target chip
#if defined(CONFIG_IDF_TARGET_ESP32C3)
  #define SDA_PIN  8
  #define SCL_PIN  9
#else
  #define SDA_PIN  21
  #define SCL_PIN  22
#endif

#define MAX_PACKET 32
