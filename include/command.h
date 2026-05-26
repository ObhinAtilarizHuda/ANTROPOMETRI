#pragma once
#include <Arduino.h>

enum State { Operation, Standby };
extern State state;

extern volatile bool buttonPressed;
extern bool measurementRequested;

void handleCmd();
void loopMeasurement();
