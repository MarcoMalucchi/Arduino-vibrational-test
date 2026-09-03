/*
We create a scanner of all MPU registes addresses.
NOTE: if with the I2C-connected-devices scanner gives back the addresses of all the I2C-connected DEVICES connected to the Arduino I2C bus.
This is not possible to obtain with the registers addresses since you generally cannot discover which register addresses "exist" simply by
trying to read every address.
An I²C device acknowledges its device address, not each internal register address individually.
So with the command "Wire.write(0x42)" I'm not asking "Does register 0x42 exist", but rather "Set your internal register pointer to 0x42".
The intresting part is that at this point I can potentially request a byte from any register, even from reserved/undocumented addresses, getting
some values back. So there is not an equivalent of "if (addressDeviceExist)" with register.
*/
#include "Wire.h"

const byte MPU_ADDRESS = 0X68;  //NOTE: this hexadecimal values are just bytes inside the decive, so they have to declare in this way, as bytes

byte identity;

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

  Serial.println("I2C-connected devices addresses list");

  for (byte address = 0; address < 127; address++) {
    Wire.beginTransmission(address);

    byte error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("Device found at: 0x");
      Serial.println(address, HEX);
    }
  }

  Serial.println("MPU registers addresses list");

  for (int address = 0; address <= 255; address++) {
    
    byte value = readRegister((byte)address);

    Serial.print("Register 0x");
    Serial.print(address, HEX);
    Serial.print(" -> value 0x");
    Serial.println(value, HEX);

  }

  Serial.println("Testing 6-byte burst read...");

  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(0x3B);
  Wire.endTransmission(false);

  Wire.requestFrom(MPU_ADDRESS, 6);

  Serial.print("Bytes available: ");
  Serial.println(Wire.available());
}

void loop() {

}
