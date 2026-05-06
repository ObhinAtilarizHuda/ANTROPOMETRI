#pragma once
#include <Arduino.h>

uint16_t crc16_modbus(const uint8_t *data, uint16_t len);
