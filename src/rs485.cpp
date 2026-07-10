#include "rs485.h"
#include "crc.h"

HardwareSerial RS485Serial(1);

// Feedback packet headers — semua reply pakai REQ=0x89, ADDR=SLAVE_ADDRESS
// Reply CMD 0x00 (measurement) — D5 AA 0C 89 [ADDR] 00 + 3 byte distance + 3 byte angle + 3 byte status + CRC
uint8_t HdrMeasure[6]      = {0xD5, 0xAA, 0x0C, 0x89, SLAVE_ADDRESS, 0x00};
// Reply CMD 0x02 (control) — D5 AA 05 89 [ADDR] 02 00 [SUBCMD] + CRC
uint8_t HdrStandby[8]      = {0xD5, 0xAA, 0x05, 0x89, SLAVE_ADDRESS, 0x02, 0x00, 0x01};
uint8_t HdrOperation[8]    = {0xD5, 0xAA, 0x05, 0x89, SLAVE_ADDRESS, 0x02, 0x00, 0x02};
uint8_t HdrTare[8]         = {0xD5, 0xAA, 0x05, 0x89, SLAVE_ADDRESS, 0x02, 0x00, 0x03};
uint8_t HdrRestart[8]      = {0xD5, 0xAA, 0x05, 0x89, SLAVE_ADDRESS, 0x02, 0x00, 0x04};
uint8_t HdrCancel[8]       = {0xD5, 0xAA, 0x05, 0x89, SLAVE_ADDRESS, 0x02, 0x00, 0x05};
// Reply error — D5 AA 05 89 [ADDR] 03 00 [CODE] + CRC. Byte terakhir diisi kode error saat kirim.
uint8_t HdrError[8]        = {0xD5, 0xAA, 0x05, 0x89, SLAVE_ADDRESS, CMD_ERROR, 0x00, 0x00};

uint8_t buf[MAX_PACKET];
uint8_t idx        = 0;
int     startIndex = -1;
bool    packetReady = false;

uint8_t header, header2, source, length, cmdType, data1, data2, crcLow, crcHigh;
uint8_t targetID;

void init485() {
  if (RS485_EN >= 0) {
    pinMode(RS485_EN, OUTPUT);
    digitalWrite(RS485_EN, LOW);
  }
  RS485Serial.begin(230400, SERIAL_8N1, RS485_RX, RS485_TX);
}

void resetBuff() {
  idx = 0;
  startIndex  = -1;
  packetReady = false;
  memset(buf, 0, sizeof(buf));
}

void pack16(uint8_t *dest, uint16_t value) {
  dest[0] = (value >> 8) & 0xFF;
  dest[1] =  value       & 0xFF;
}

void pack24(uint8_t *dest, uint32_t value) {
  dest[0] = (value >> 16) & 0xFF;
  dest[1] = (value >> 8)  & 0xFF;
  dest[2] =  value        & 0xFF;
}

void debugPrintBuf() {
  Serial.print("[RAW] idx=");
  Serial.print(idx);
  Serial.print(" data: ");
  for (int i = 0; i < idx; i++) Serial.printf("%02X ", buf[i]);
  Serial.println();
}

void printHexBuf(const char *label, uint8_t *b, int len) {
  Serial.print(label);
  for (int i = 0; i < len; i++) {
    if (b[i] < 0x10) Serial.print("0");
    Serial.print(b[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

// Kirim paket RS485 half-duplex dengan CRC16-Modbus
void sendRS485(uint8_t *hdr, uint8_t hdrLen, uint8_t *data, uint16_t dataLen) {
  uint8_t tempBuf[hdrLen + dataLen];
  memcpy(tempBuf, hdr, hdrLen);
  if (data && dataLen > 0) memcpy(&tempBuf[hdrLen], data, dataLen);

  uint16_t crc  = crc16_modbus(tempBuf, hdrLen + dataLen);
  uint8_t  crcH = (crc >> 8) & 0xFF;
  uint8_t  crcL =  crc       & 0xFF;

  if (RS485_EN >= 0) {
    digitalWrite(RS485_EN, HIGH);
    delayMicroseconds(10);
  }

  RS485Serial.write(hdr, hdrLen);
  if (data && dataLen > 0) RS485Serial.write(data, dataLen);
  RS485Serial.write(crcH);
  RS485Serial.write(crcL);

  RS485Serial.flush();

  if (RS485_EN >= 0) {
    delayMicroseconds(500);
    digitalWrite(RS485_EN, LOW);
    delayMicroseconds(500);
  }

  while (RS485Serial.available()) RS485Serial.read();  // buang echo/garbage
}

// Kirim frame error ke master: D5 AA 05 89 [ADDR] 03 00 [code] + CRC
void sendError(uint8_t code) {
  HdrError[7] = code;
  sendRS485(HdrError, sizeof(HdrError), nullptr, 0);
  Serial.printf("[ERR] kirim error code=0x%02X\n", code);
}

// Terima dan parsing paket dari Mainboard
// Format: D5 AA [length] [source] [targetID] [cmdType] [data...] [CRCH] [CRCL]
void read485() {
  while (RS485Serial.available() && idx < MAX_PACKET) {
    buf[idx++] = RS485Serial.read();
  }

  if (idx == 0) return;

  packetReady = false;
  startIndex  = -1;

  if (idx < 2) {
    Serial.println("gagal idx < 2");
    debugPrintBuf();
    return;
  }

  for (int i = 0; i < idx - 1; i++) {
    if (buf[i] == 0xD5 && buf[i+1] == 0xAA) {
      startIndex = i;
      Serial.println("header ditemukan");
      break;
    }
  }

  if (startIndex == -1) {
    Serial.println("gagal index -1");
    debugPrintBuf();
    resetBuff();
    return;
  }

  if (idx < startIndex + 3) {
    Serial.println("kurang lengkap");
    debugPrintBuf();
    return;
  }

  header  = buf[startIndex];
  header2 = buf[startIndex+1];
  length  = buf[startIndex+2];

  Serial.printf("[DEBUG] Header OK, length=%d, idx=%d\n", length, idx);

  // Tunggu sampai SRC, DST, CMD ikut tersedia untuk peek
  if (idx < startIndex + 6) {
    return;
  }

  source   = buf[startIndex+3];
  targetID = buf[startIndex+4];
  cmdType  = buf[startIndex+5];

  if (source != 0x88) {
    Serial.printf("[REJECT] unsupported source/REQ: 0x%02X\n", source);
    resetBuff();
    return;
  }

  if (cmdType != 0x00 && cmdType != 0x02) {
    Serial.printf("[REJECT] unsupported cmdType: 0x%02X\n", cmdType);
    resetBuff();
    return;
  }

  uint8_t extraPayload = 0;

  if ((cmdType == 0x00 && length != 0x04) || (cmdType == 0x02 && length != 0x04)) {
    Serial.printf("[REJECT] invalid length=%d for cmdType=0x%02X\n", length, cmdType);
    resetBuff();
    return;
  }

  if (idx < startIndex + 3 + length + extraPayload + 2) {
    Serial.println("paket tidak sesuai length");
    debugPrintBuf();
    return;
  }

  if (cmdType == 0x00) {
    data1 = buf[startIndex+6];
    data2 = 0;
    Serial.printf("[PARSE] source=0x%02X, targetID=0x%02X, cmdType=0x%02X, reserved=0x%02X\n",
                  source, targetID, cmdType, data1);
  } else if (cmdType == 0x02) {
    data1 = buf[startIndex+6];
    data2 = 0;
    Serial.printf("[PARSE] source=0x%02X, targetID=0x%02X, cmdType=0x%02X, subcmd=0x%02X\n",
                  source, targetID, cmdType, data1);
  }

  crcHigh = buf[startIndex + 3 + length + extraPayload];
  crcLow  = buf[startIndex + 3 + length + extraPayload + 1];

  Serial.print("Header1  : 0x"); Serial.println(header,   HEX);
  Serial.print("Header2  : 0x"); Serial.println(header2,  HEX);
  Serial.print("Length   : ");   Serial.println(length);
  Serial.print("Source   : 0x"); Serial.println(source,   HEX);
  Serial.print("Target   : 0x"); Serial.println(targetID, HEX);
  Serial.print("CmdType  : 0x"); Serial.println(cmdType,  HEX);
  Serial.print("CRC High : 0x"); Serial.println(crcHigh,  HEX);
  Serial.print("CRC Low  : 0x"); Serial.println(crcLow,   HEX);

  uint8_t  crcDataLen = 3 + length + extraPayload;
  uint16_t crcCalc    = crc16_modbus(&buf[startIndex], crcDataLen);
  uint16_t crcRX      = ((uint16_t)crcHigh << 8) | crcLow;

  Serial.printf("[CRC] crcDataLen=%d, crcCalc=0x%04X, crcRX=0x%04X\n", crcDataLen, crcCalc, crcRX);

  if (crcRX != crcCalc) {
    Serial.printf("[WARN] CRC Gagal! Calc: 0x%04X, Recv: 0x%04X\n", crcCalc, crcRX);
    // Frame ditujukan ke address kita tapi CRC/data rusak => lapor checksum error.
    if (targetID == SLAVE_ADDRESS) sendError(ERR_CHECKSUM);
    resetBuff();
    return;
  }

  packetReady = true;
  Serial.println("[OK] packetReady = true");
}
