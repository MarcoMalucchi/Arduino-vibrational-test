/*
Here the first version of an Arduino-based accelerometer reader.
In the void set-up there is the MPU configuration (clock source, low-pass filter, sample divider, accelerometer full-scale and INT PIN configuration). The data acquisition is performed
directly reading the ACCEL_X/Y/ZOUT_H/L registers at every INT rising edge trough a ISR design.
So the design (pretty simple actually) is about the following:
1. Start up and based configuration
2. Direct reading of the accelerometer storing registers
3. Conversion of the signed 2 bytes values in units of g and print in the serial.

Next step is to create a data structure and biuld the serial comunication which will be used by a python interface for a live plot.
*/
#include "Wire.h"

const byte MPU_ADDRESS = 0X68;  //NOTE: this hexadecimal values are just bytes inside the decive, so they have to declare in this way, as bytes

// General configuration of the device
const unsigned long SAMPLING_RATE = 200; // the sampling rate which will be setted using the SMPLRT_DIV register, through the formula SAMPLING_RATE = GYRO_OUTPUT_FREQ/(1 + SMPLRT_DIV)
const byte CONFIG       = 0x1A; // to set the digital low-pass filter of the device
const byte SMPLRT_DIV   = 0x19; // sample rate divider
const byte ACCEL_CONFIG = 0x1C; // accelerometer full-scale

// INT-configuration register to syncronize Arduino and MPU for data reading
const byte INT_PIN_CFG = 0x37;
const byte INT_ENABLE   = 0x38;
const byte INT_STATUS   = 0x3A;

// Acceleration-measurements registers
const byte ACCEL_XOUT_H = 0x3B;
const byte ACCEL_XOUT_L = 0x3C;
const byte ACCEL_YOUT_H = 0x3D;
const byte ACCEL_YOUT_L = 0x3E;
const byte ACCEL_ZOUT_H = 0x3F;
const byte ACCEL_ZOUT_L = 0x40;

// power management register for global configuration
const byte PWR_MGMT_1   = 0x6B;

const int INT_PIN = 3;

// for the interrupt which will listen the INT PIN state
volatile bool dataReady = false;  // this variable will be modify by the ISR asynchronously

// ---I2C transaction function and MPU-data reader

// ---To write in register---
void writeRegister(byte reg, byte value) {
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}
// ------

// ---To read inside register---
byte readRegister(byte reg) {
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(reg);
  Wire.endTransmission(false);

  Wire.requestFrom(MPU_ADDRESS, 1);

  return Wire.read();
}
// ------

// ---The interrupt---
void mpuDataReady() {
  dataReady = true;
}
// ------

// ---Data Reader---
void dataReader() {

  noInterrupts();
  bool localDataReady = dataReady;
  dataReady = false;  // I have to consume the flag once I copied it, so to avoid infinite loop. I have to clear the flag
  interrupts();

  if (!localDataReady) {
    return;
  }
  
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(ACCEL_XOUT_H);
  Wire.endTransmission(false);

  Wire.requestFrom(MPU_ADDRESS, 6); // What change from the readRegister function is that here I ask MPU for 6 bytes, since I want to perform a burnst read of the 6 acceleration-
                                    // measurement registers
  
  float ax = ((int16_t)((uint16_t(Wire.read()) << 8) | Wire.read()))/16384.0f; // Here I reconstruct the 2 bytes signed values of the acceleration measurements performed by the device
  float ay = ((int16_t)((uint16_t(Wire.read()) << 8) | Wire.read()))/16384.0f; // and I convert them in units of g using the sensitivity per LSB of the accelerometer gives by setting the
  float az = ((int16_t)((uint16_t(Wire.read()) << 8) | Wire.read()))/16384.0f; // full scale of the instrument trough the ACCEL_CONFIG register

  Serial.print("X Acceleration: ");
  Serial.println(ax, 6);
  Serial.print("Y Acceleration: ");
  Serial.println(ay, 6);
  Serial.print("Z  Acceleration: ");
  Serial.println(az, 6);
}

void setup() {

  pinMode(INT_PIN, INPUT);

  attachInterrupt(digitalPinToInterrupt(INT_PIN), mpuDataReady, RISING);

  Serial.begin(115200);

  Wire.begin();

  // MPU configuration
  writeRegister(PWR_MGMT_1, 0b00000001);  // If we do not wake up the MPU before configure it, it won't configure. Here we select as clock source the PPL referenced to the gyroscope
  writeRegister(CONFIG, 0b00000011); // Set the digital low-pass filter of the device to 44 Hz, contemporaneously setting the Gyroscope Output Rate to 1kHz
  writeRegister(SMPLRT_DIV, 1000/SAMPLING_RATE - 1);  // Set the sample divider so to set the actual sampling rate of the device
  writeRegister(ACCEL_CONFIG, 0b00000000);  // to set the full scale to 2g (maximum sensitivity, easier to saturate)
  writeRegister(INT_PIN_CFG, 0b00000000); // to set the behaviour of the INT pin
  writeRegister(INT_ENABLE, 0b00000001);  // Enable DATA_RDY interrupt:
                                          // triggered when a new set of sensor data has been written to the sensor output registers

}

void loop() {
  dataReader();
  delay(2000);
}
