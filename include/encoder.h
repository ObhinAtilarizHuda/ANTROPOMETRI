#pragma once
#include <Arduino.h>
#include "AS5600.h"

extern AS5600   as5600;
extern int32_t  startRaw;

float getDistance(float x);
void  doTare();
void  pollEncoder();
void  testSensor();   // mode test: cetak Degree | Degree Adjust | Distance ke Serial Monitor
