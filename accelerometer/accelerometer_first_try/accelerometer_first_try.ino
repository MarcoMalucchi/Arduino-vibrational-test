/*
Just trying to ask at MPU6050 who it is by reading its address WHO_AM_I (0x75), directly over I2C.
Then trying to correctly wake up the device wirting onto the register PWR_MNGM_1
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

  identity = readRegister(0x75);

  pwrmgmt_before = readRegister(PWR_MGMT_1);

  writeRegister(PWR_MGMT_1, 0b00000000);

  pwrmgmt_after = readRegister(PWR_MGMT_1);

  Serial.print("WHO_AM_I: 0x");
  Serial.println(identity, HEX);  // Even if I declared the values above as byte, when I print them via serial I can decide in which rappresentation I can display them. Hexadecimal
                                  // in this case.

  Serial.print("PWR_MGMT_1 before: ");
  Serial.println(pwrmgmt_before, BIN);

  Serial.print("PWR_MGMT_1 after:  ");
  Serial.println(pwrmgmt_after, BIN);
}

void loop() {

}
