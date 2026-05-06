#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include "config.h"

// Shared serial instance
extern HardwareSerial RS485Serial;

// Packet headers: D5 AA [length] 89 06 [cmdType] ...
extern uint8_t HdrMeasure[6];
extern uint8_t HdrStandby[8];
extern uint8_t HdrOperation[8];
extern uint8_t HdrTare[8];
extern uint8_t HdrRestart[8];
extern uint8_t HdrFbError[8];

// Receive buffer & parsed fields
extern uint8_t buf[MAX_PACKET];
extern uint8_t idx;
extern int     startIndex;
extern bool    packetReady;

extern uint8_t header, header2, source, length, cmdType, data1, data2, crcLow, crcHigh;
extern uint8_t targetID;

void     init485();
void     read485();
void     sendRS485(uint8_t *hdr, uint8_t hdrLen, uint8_t *data, uint16_t dataLen);
void     resetBuff();
void     pack16(uint8_t *dest, uint16_t value);
void     debugPrintBuf();
void     printHexBuf(const char *label, uint8_t *b, int len);
