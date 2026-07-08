#pragma once
#include <Arduino.h>

enum State { Operation, Standby };
extern State state;

extern bool measurementRequested;

void handleCmd();
void loopMeasurement();
void loopTare();
