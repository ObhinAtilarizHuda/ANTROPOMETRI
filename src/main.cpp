#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "rs485.h"
#include "encoder.h"
#include "command.h"

void setup() {
  delay(1000);
  Serial.begin(115200);
  init485();
  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.print("AS5600 Connected: ");
  Serial.println(as5600.isConnected());
  delay(500);
  startRaw = as5600.getCumulativePosition();
  Serial.println("Device Ready");
}

void loop() {
  read485();

  if (packetReady) {
    packetReady = false;
    Serial.printf("[LOOP] targetID=0x%02X, source=0x%02X, cmdType=0x%02X\n", targetID, source, cmdType);

    if (targetID == SLAVE_ADDRESS) {
      Serial.println("[OK] targetID match, execute handleCmd");
      handleCmd();
    } else {
      Serial.printf("[REJECT] targetID mismatch: expected 0x%02X, got 0x%02X\n", SLAVE_ADDRESS, targetID);
      resetBuff();
    }
  }
}
