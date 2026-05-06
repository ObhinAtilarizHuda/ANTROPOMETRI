#pragma once
#include <Arduino.h>

enum State { Operation, Standby };
extern State state;

void handleCmd();
