#pragma once
#include <Arduino.h>
#include "AS5600.h"

extern AS5600   as5600;
extern int32_t  startRaw;
extern int32_t  sessionZeroRaw;   // nol-sesi (home fisik) di-capture saat boot, tak digeser tare

float getDistance(float x);
void  doTare();
float distFromSessionZero();      // jarak (cm) posisi sekarang thd nol-sesi — untuk penjaga tare
void  pollEncoder();
void  testSensor();   // mode test: cetak Degree | Degree Adjust | Distance ke Serial Monitor
