/*
Now we move to a more advance version: a data structure which will store the data and which will be send wia serial to the python interface.
In the void set-up there is the MPU configuration (clock source, low-pass filter, sample divider, accelerometer full-scale and INT PIN configuration). The data acquisition is performed
directly reading the ACCEL_X/Y/ZOUT_H/L registers at every INT rising edge trough a ISR design.
So the design (pretty simple actually) is about the following:
1. Start up and based configuration
2. Direct reading of the accelerometer storing registers
3. Storing the data in the structure and send it via serial

Next step create the python live plot
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
volatile uint32_t sampleTime; // The timestamp will be recorded inside the ISR

// Data packet
struct Packet {
  uint16_t header = 0xAAAA;
  uint8_t status; // this adds one more byte to the packet
  int16_t ax;
  int16_t ay;
  int16_t az;
  uint32_t timestamp;
}__attribute__((packed));
// NOTE: we decided to extend the packet with a STATUS FIELD, so that Arduino can send via serial (to the python interface) eventual diagnosis messages

// Here we define statuses
const byte STATUS_OK = 0; // valide accelerometer sample
const byte STATUS_I2C_TX_ERROR = 1; // failed while setting ACCEL_XOUT_H register pointer
const byte STATUS_I2C_READ_ERROR = 2; // failed whiel requesting the six acceleration bytes

Packet sample;  // Same logic of the object type used to store the states of the motor machine. Create an object named "sample" of type Packet
                // A sample is one of the instance/object of type Packet, ax/y/z, header (which is fixed)
                // and timestamp are called "members" of the struct.
                // This line means "create an object named "sample" whose type is "packed"".

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
  sampleTime = micros();
}
// ------

// ---Data Reader---
void dataReader() { // Modified to fill the packet instead of converting the data read from the device in float

  noInterrupts();
  bool localDataReady = dataReady;
  dataReady = false;  // I have to consume the flag once I copied it, so to avoid infinite loop. I have to clear the flag
  uint32_t localSampleTime = sampleTime;  // timestamp in packet updated
  interrupts();

  if (!localDataReady) {
    return;
  }
  
  sample.timestamp = localSampleTime;

  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(ACCEL_XOUT_H);
  byte txError = Wire.endTransmission(false);

  if (txError != 0) { // The idea is that we detect an I2C error occuring during the communication we must not update ax, ay and az
    sample.status = STATUS_I2C_TX_ERROR;
    Serial.write((uint8_t*)&sample, sizeof(sample));
    Wire.end(); // This two lines are needed to restore the I2C pins status and then restart the regular communication
    Wire.begin();
    return;
  }

  byte receivedBytes = Wire.requestFrom(MPU_ADDRESS, 6);

  if (receivedBytes != 6) {
    sample.status = STATUS_I2C_READ_ERROR;
    Serial.write((uint8_t*)&sample, sizeof(sample));
    Wire.end(); // This two lines are needed to restore the I2C pins status and then restart the regular communication
    Wire.begin();
    return;
  }
  // NOTE: putting the return command immediatly after the controls imply that the new packet that will be send, after the recording of the error, will have the old acceleration
  // measurement of the previous packet. That's not important since the status byte is placed before the acceleration measurement bytes and python will performs the control before
  // appending the new measurement bytes. From the design pov that means that:
  // if status == OK then append the new that
  // if status != OK ignore the new data
  
  // Then when only execute the sample filling after proving that all six bytes are arrived
  sample.ax = (int16_t)((uint16_t(Wire.read()) << 8) | Wire.read());
  sample.ay = (int16_t)((uint16_t(Wire.read()) << 8) | Wire.read());
  sample.az = (int16_t)((uint16_t(Wire.read()) << 8) | Wire.read());

  sample.status = STATUS_OK;

  Serial.write((uint8_t*)&sample, sizeof(sample));  // here the actual writer command. &sample is the Arduino's RAM address of the first byte of the struct, it says to Serial.wire
                                                    // where if the first byte of the struct. Then sizeof(sample) says to Serial.write() to continue writing for the number of bytes
                                                    // which constitute the packet.
                                                    // The cast uint8_t* says "consider those 12 bytes as a byte sequence", becasue this is what Serial.write() expects
                                                    // NOTE: &sample is a c++ pointer.

  // Serial.print("ax = ");
  // Serial.print(sample.ax);
  // Serial.print("  ay = ");
  // Serial.print(sample.ay);
  // Serial.print("  az = ");
  // Serial.println(sample.az);


}

void setup() {

  pinMode(INT_PIN, INPUT);

  

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

  // delay(2000);

  // Serial.println("Burst test before interrupt:");

  // Wire.beginTransmission(MPU_ADDRESS);
  // Wire.write(ACCEL_XOUT_H);
  // Wire.endTransmission(false);

  // Wire.requestFrom(MPU_ADDRESS, 6);

  // Serial.print("Bytes available: ");
  // Serial.println(Wire.available());

  // while (Wire.available()) {
  //     Serial.print(Wire.read(), HEX);
  //     Serial.print(" ");
  // }
  // Serial.println();

  // while (true) {
  // }


  attachInterrupt(digitalPinToInterrupt(INT_PIN), mpuDataReady, RISING);
}

void loop() {
  dataReader();
}
