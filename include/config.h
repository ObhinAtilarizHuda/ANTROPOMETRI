#pragma once
#include <Arduino.h>

// Mode operasi firmware:
//   1 = Mode RS485 -> langsung berkomunikasi dengan master (operasi normal)
//   0 = Mode Test  -> hanya baca sensor, tampilkan Degree | Degree Adjust | Distance
//                     di Serial Monitor (1 angka di belakang koma)
#define RS485_MODE 1

// RS485 pins (ESP32-C3: GPIO max 21 — sesuaikan jika perlu)
#define RS485_RX   19   // RO
#define RS485_TX   18   // DI
#define RS485_EN   -1   // -1 = auto direction (hardware handle DE/RE), set GPIO pin for manual control

#define SLAVE_ADDRESS 0x06

// I2C pins — auto-selected by target chip
#if defined(CONFIG_IDF_TARGET_ESP32C3)
  #define SDA_PIN  8
  #define SCL_PIN  10
#else
  #define SDA_PIN  21
  #define SCL_PIN  22
#endif

// Pin DIR AS5600 (GPIO4). HIGH = counter-clockwise (CCW), LOW = clockwise.
#define DIR_PIN  4

#define MAX_PACKET 32

#define BUTTON_PIN 3

// Gesture tombol (GPIO BUTTON_PIN)
#define BTN_DEBOUNCE_MS    30      // waktu debounce pembacaan (ms)
#define TARE_HOLD_MS       5000    // tahan tombol 5 detik => Tare + lapor master
                                   // klik pendek (< TARE_HOLD_MS) => kirim measurement ke master

// Active buzzer di GPIO5. BUZZER_ON = level untuk bunyi.
// Modul active-buzzer umum: HIGH = bunyi. Ganti ke LOW jika modulmu active-low.
#define BUZZER_PIN 5
#define BUZZER_ON  HIGH
#define BUZZER_OFF LOW
#define MEASURE_BEEP_MS 200    // durasi beep konfirmasi setelah measurement terkirim ke master (ms)
#define TARE_BEEP_MS    200    // durasi tiap beep tare (ms)
#define TARE_BEEP_COUNT 3      // jumlah beep saat tare selesai dibalas ke master (mode RS485)
