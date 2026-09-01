/*
We create a scanner of all possible I2C device addresses and print the ones that respond.
NOTE: this scanner does not scan the MPU registers, they live inside the device. Here we are looking for I2C-connected devices
*/
#include "Wire.h"

const byte MPU_ADDRESS = 0X68;  //NOTE: this hexadecimal values are just bytes inside the decive, so they have to declare in this way, as bytes
const byte PWR_MGMT_1 = 0X6B;

byte identity;
byte pwrmgmt_before;
byte pwrmgmt_after;

void writeRegister(byte reg, byte value) {
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

byte readRegister(byte reg) {
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(reg);
  Wire.endTransmission(false);

  Wire.requestFrom(MPU_ADDRESS, 1);

  return Wire.read();
}

void setup() {
  Serial.begin(115200);

  delay(2000);

  Serial.println("Serial started");

  Wire.begin();

  Serial.println("Wire started");

  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);

    byte error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("Device found at: 0x");
      Serial.println(address, HEX);
    }
  }
}

void loop() {

}
