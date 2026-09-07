/*
Here we create an accelerometer-reader state machine. Thanks to this update, python can syncronize with Arduino avoiding I2C startup errors.
The states of the machine will be three:

1. INITIALIZING --> Arduino is working to correctly sets up the MPU

2. READY --> Arduino running + I2C initialized + MPU6050 detected + MPU6050 configured successfully + system ready to acquire + measurement NOT running

3. MEASURING --> MPU initialized + DATA_READY acquisition enabled + Arduino reading acceleration + Arduino transmitting measurement packets

The basic state machine becomes:

  - "Python requests START" --> "the machine goes to MEASURING from READY"
  - "Python requests STOP" --> "the machine goes to READY from MEASURING"

So basically:

  - Arduino initialized the MPU, sets in READY, tells python
  - Python recognize the state and ask for START
  - Arduino recognize the request and goes to MEASURING, tells python, which starts expecting packets to decode
  - The packets are finally send
  - Lastly, when the serial communication is stopped, python send a STOP comamnd which will reset Arduino to the READY state

That's agenuine HANDSHAKE
In this way python does not immediatly starts looking for Arduino packets, it first waits for Arduino to tell it which state it is in.
*/

#include "Wire.h"

// Device address
const byte MPU_ADDRESS = 0X68;  //NOTE: this hexadecimal values are just bytes inside the decive, so they have to declare in this way, as bytes
const byte WHO_AM_I = 0X75;

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

// INT PIN
const int INT_PIN = 3;

// for the interrupt, which will listen the INT PIN state
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
// NOTE: we decided to extend the packet with a STATUS FIELD, so that Arduino can send via serial (to the python interface) eventual diagnosis messages regarding the validity of the 
// packets

// Here we define packets statuses
const byte STATUS_OK = 0; // valide accelerometer sample
const byte STATUS_I2C_TX_ERROR = 1; // failed while setting ACCEL_XOUT_H register pointer
const byte STATUS_I2C_READ_ERROR = 2; // failed whiel requesting the six acceleration bytes

Packet sample;  // Same logic of the object type used to store the states of the motor machine. Create an object named "sample" of type Packet
                // A sample is one of the instance/object of type Packet, ax/y/z, header (which is fixed)
                // and timestamp are called "members" of the struct.
                // This line means "create an object named "sample" whose type is "packed"".

// Accelerometer states
enum AccelerometerState { // enum garantees that the state variable contains one state at a time
  INITIALIZING,
  READY,
  MEASURING
};

AccelerometerState accelerometerState = INITIALIZING; // So that we can distinguish between the not-READY-yet state and READY state, which are different states with respect MEASURING
                                                      // Now the booting process has a meaningful state

// ---I2C-TRANSACTION FUNCTIONS---

// ---To write in register---
// NOTE: to create an intelligent setup it is important to make writeRegister return the I2C result
byte writeRegister(byte reg, byte value) {
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission();  // Now we can check every configuration transaction
}

// ---To read inside register---
bool readRegister(byte reg, byte &value) {  // & here has the function to point at an existing global/local variable used/declaired elsewhere, which value may be changed by
  Wire.beginTransmission(MPU_ADDRESS);      // the function too. As direct example in the void setup() we have "identity", which will be passed to the function as an argument
  Wire.write(reg);                          // which correspond to value and the function will change it.

  byte txError = Wire.endTransmission(false);

  if (txError != 0) { // To track if the I2C communication has failed
    return false;
  }

  byte receiveBytes = Wire.requestFrom(MPU_ADDRESS, 1);

  if (receiveBytes != 1) {  // To track if the reading procedure has failed
    return false;
  }

  value = Wire.read();

  return true;
}

// ------

// ------

// --- THE MPU-INITIALAZATION/CONFIGURATION FUNCTIONS ---

// --- MPU initial configuration ---
// NOTE: what this function does:
/*
1. restart I2C if needed
2. give MPU a short settling time
3. werify the MPU responded
4. verify WHO_AM_I
5. configure each register
6. check every write
7. only return true if everything succeded
    That means:
    true --> MPU is genuinely configured and READY is valid
    false --> initialization failed; do not enter READY

NOTE: this function does not start the acquisition, it just initialize the MPU, it actually "establishes communication with the MPU, identifies it, configures it, and report whether
 that succeeded."
*/
bool initializeMPU() {

  delay(100);

  //check device identity
  byte identity;

  if (!readRegister(WHO_AM_I, identity)) {
    Wire.end(); // That's to restore the communication
    Wire.begin();
    return false; // communication itself failed: I2C transaction failed or the bytes requested were not been received 
  }

  if (identity != MPU_ADDRESS) {
    Wire.end();
    Wire.begin();
    return false; // // communication worked, but wrong device responded
  }

  // MPU configuration
 
  if (writeRegister(PWR_MGMT_1, 0b00000001) != 0) { // If we do not wake up the MPU before configure it, it won't configure. Here we select as clock source the PPL referenced
                                                    // to the gyroscope
    Wire.end();
    Wire.begin();
    return false;
  }

  delay(100);

  if (writeRegister(CONFIG, 0b00000011) != 0) { // Set the digital low-pass filter of the device to 44 Hz, contemporaneously setting the Gyroscope Output Rate to 1kHz
    Wire.end();
    Wire.begin();
    return false;
  }

  if (writeRegister(SMPLRT_DIV, 1000/SAMPLING_RATE - 1) != 0) { // Set the sample divider so to set the actual sampling rate of the device
    Wire.end();
    Wire.begin();
    return false;
  }

  if (writeRegister(ACCEL_CONFIG, 0b00000000) != 0) { // to set the full scale to 2g (maximum sensitivity, easier to saturate)
    Wire.end();
    Wire.begin();
    return false;
  }

  if (writeRegister(INT_PIN_CFG, 0b00000000) != 0) {  // to set the behaviour of the INT pin
    Wire.end();
    Wire.begin();
    return false;
  }

  if (writeRegister(INT_ENABLE, 0b00000000) != 0) { // Enable DATA_RDY interrupt:
                                                    // triggered when a new set of sensor data has been written to the sensor output registers
    Wire.end();
    Wire.begin();
    return false;
  }
/*
NOTE: we are not doing this initialization of the MPU INT PIN here since is something that has to be done once the MPU initialization is complete, so that the MEASURING state can
actually means "The MPU generates DATA_RDY interrupts and Arduino listens to them" (also the attachInterrupt instruction has to be remove from the setup then)
*/
  return true;
}

// --- Start-measuring function ---
/*
NOTE: this function has the job to:
1. clear any stale dataReady flag
2. enable DATA_RDY in MPU6050
3. attach Arduino interrupt
4. return true if successful
Basically set up the system so to it can be ready to acquired the measures
*/
bool startMeasurement() {
  noInterrupts();
  dataReady = false;
  interrupts();

  attachInterrupt(digitalPinToInterrupt(INT_PIN), mpuDataReady, RISING);

  if (writeRegister(INT_ENABLE, 0b00000001) != 0) { // Enable DATA_RDY interrupt:
                                                    // triggered when a new set of sensor data has been written to the sensor output registers
    detachInterrupt(digitalPinToInterrupt(INT_PIN));
    Wire.end();
    Wire.begin();
    return false;
  }

  return true;
  
}
// -------

// --- Stop measuring function ---
bool stopMeasurement() {

  if (writeRegister(INT_ENABLE, 0b00000000) != 0) { // Disenable DATA_RDY interrupt
      Wire.end();
      Wire.begin();
      return false;
  }

  detachInterrupt(digitalPinToInterrupt(INT_PIN));

  noInterrupts();
  dataReady = false;
  interrupts();

  return true;
}
// ------

// ------

// --- DATA READING FUNCTIONS ---

// ---The interrupt---
void mpuDataReady() {
  dataReady = true;
  sampleTime = micros();
}

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
// -------



// --- This function contains the serial command handling for the handshake ---

void handleControlMessage() {

  if (Serial.available() == 0) { // That checks if I have bytes waiting to be read in the Arduino serial buffer
    return; // If htere aren't bytes retunr
  }
  
  // else
  String command = Serial.readStringUntil('\n');
  command.trim();

  if (command == "STATE?") {  // Python continously requesting the state of the machine

    if (accelerometerState == INITIALIZING) {
      Serial.println("INITIALIZING"); // Arduino communicates its state to python so that it can send the START message
    }

    else if (accelerometerState == READY) {
      Serial.println("READY");
    }

  }

  else if (command == "START" && accelerometerState == READY) { // If you obtain the python START message, then set-up the MPU to start measuring and if it succedes change the state
    if (startMeasurement()) {
      accelerometerState = MEASURING;
      Serial.println("MEASURING");  // Once python has received the MEASURING-state-been-reached message, it can switch the binary packet decoder (important because I'm using the
                                    // same serial connection for text and binary data)
    }
  }
  else if (command == "STOP" && accelerometerState == MEASURING) {  // When python send the STOP message, set-up the MPU to return to the READY state
    if (stopMeasurement()) {
      accelerometerState = READY;
      Serial.println("READY");
    }
  }

}
// ------

/*
NOTE: here we implement an "intelligent" setup, where Arduino can actually check if the setup of the accelerometer has been performed correctly and consequently changes its state
Workflow:

1. setup()
2. Serial.begin()
3. Wire.begin()
4. initializeMPU()
5. Successed? Yes --> accelerometerState = READY, No --> stay outside READY/retry

*/
void setup() {

  pinMode(INT_PIN, INPUT);

  Serial.begin(115200);

  Wire.begin();

}

void loop() {

  handleControlMessage(); // Since python can always send messages to Arduino, even when it is trasmitting the data
                          // the Messages handler has to always run, so to detect these messages

  if (accelerometerState == INITIALIZING) {
    if (initializeMPU()) {
      accelerometerState = READY;
    }
  }
  
  else if (accelerometerState == MEASURING) {
    dataReader();
  }

}
