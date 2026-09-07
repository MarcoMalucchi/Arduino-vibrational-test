/*
HERE THE COMPLETE FIRMWARE OF THE SYSTEM: STEPPER MOTOR + PHOTOCELLS MACHINE AND ACCELEROMETER MACHINE

The important point here is that the MPU is autorize to change its state from READY to MEASURING only when the motor is in the AT_TARGET state. Said so the logic of the two machines
is basically the samebuilt until now

*/

#include "pwm.h"  // the library which contains the instruction to control the PWM mode of the pins and the FspTimer commands
#include "Wire.h" // which contains the instructions to perform the I2C communication with the MPU

// Global variables for Arduino's motor and photocells digital pins
const int DIR_PIN = 7;  // to control the direction of rotation
const int STEP_PIN = 5; // to control the rotation of the motor shaft
const int ENABLE_PIN = 4; // to enable or disable the driver of the motor, so to stop it or start it
const int PHOTOC_PIN = 2; // photocells pin, in Arudino uno r4 minima only pin D2 and D3 can be used in the interrupt mode

// INT MPU PIN
const int INT_PIN = 3;

// Setting the STEP_PIN so that it can be used in PWM mode
PwmOut stepPWM(STEP_PIN);
float duty_cycle = 50.0;  // the duty cycle of the wave form have to be set everytime a new frequency is reached, but it is always the same, so has sense to declare it as a global
                          // variable

// Number of steps needed for the motor to execute a complete turn: needed for the conversion from frequency to steps per second (or frequency of the STEP_PIN signale), in turn
// necessary to set the frequency of rotation of the shaft.
const int STEPS_PER_REVOLUTION = 400;

// The frequency of the internal clock of the Arduino's integrated microcontroller used by stepPWM, needed to computed how many clock counts a complete period of the STEP waveform
// has to last, so to send a properly made signal to the motor
uint32_t timerFrequency;

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

// The declareation of the states of the motor.

bool pwmRunning = false; // The logical state of the PWM Arduino's pin used to rotate the motor, if HIGH it is rotating instead it is not. That's not a real steate of the motor
                         // is more like something needed to control the signal at STEP_PIN, at least conceputually speaking

// enum --> enumeration, like the python dictionaries
enum MotorStates {
  STOPPED,
  RAMPING,
  AT_TARGET
};

MotorStates motorState = STOPPED; // Setting the defaul state of the motor

// Variables needed to keep track of the frequency of the motor, last of its update and to perform its increase or decrease for the acceleration of the motor.
float currentFrequency = 0.0;
float targetFrequency = 0.0;
float frequencyAcceleration = 0.25;  // Hz/s set as default, may be increased
const unsigned long FREQUENCY_UPDATE_INTERVAL = 10000;  // The time that pass between subsequent frequency changes
const float FREQUENCY_INCREMENT = frequencyAcceleration * (FREQUENCY_UPDATE_INTERVAL / 1000000.0);  // Here we directly set as default the increment in frequency wanted

// NOTE: the incrementations is done wit constante acceleration that means the each increment in frequency is computed as delte_time times acceleration. I can measure the delta_time
// each time I change the frequency or set as constant, oviding abdrupt changes, so to have each time the same increment in frequency, then smooth accelerations.

unsigned long previousFrequencyUpdate = 0;  // The time at which the last frequency change was performed, important to know when to change the frequency (we can't change the frequency
                                            // continuosly, we have others things to do)


const float START_FREQUENCY = 0.25; // The default frequency of the motor at which it will starts rotate when started (when the user will write the first GO in the serial door)

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

// for the interrupt used for the accelerometer, which will listen the INT PIN state
volatile bool dataReady = false;  // this variable will be modify by the ISR asynchronously
volatile uint32_t sampleTime; // The timestamp will be recorded inside the ISR

// Variables needed to measure the frequency from the photocells's signal, those will be used by the ISR mainly
volatile unsigned long pulseCount = 0;  // the pulse counter variable shared between ISR and loop()
volatile unsigned long lastPulseTime = 0; // the time of the last pulse detected by the ISR

// Variables needed by frequencyMeter()
// These two below will be the startin reference for time and pulse counts
unsigned long windowStartPulseCount = 0; // starting pulse count of a new peasurement window (startingPulseTime)
                                         // NOTE: immediatly after the start-up there isn't no meaningful windowStartPulseTime yet
unsigned long windowStartPulseTime = 0; // time stamp of the starting edge of the new measurement window (startingTimeStamp)
bool measurementStarted = false;  // This keeps track of the information "Are we measuring the frequency trough the photocells or not?"

const unsigned long MEASUREMENT_INTERVAL = 5000000; // The amount of time which has to pass between subsequent frequency measurements

float measuredFrequency;


// ---START/STOP MOTOR FUNCTIONS---
void startMotor() { // Here STOPPED --> RAMPING

  unsigned long startTime = micros(); // take the time at which the motor starts (notice it is a local variable)

  previousFrequencyUpdate = startTime; // Same idea as above, the reference time at which the state of the motor has to change (both its frequency and the fact that it is runnning or not)
                                        // has to be that at which the motor has been started.

  if (targetFrequency == 0.0){  // I want the motor to rotate at 0.25 Hz only when I start it for the first time, then, after subsequently stops, I want it to restart from the last
                                // target frequency seted.
    targetFrequency = START_FREQUENCY; 
  }

  currentFrequency = 0.0; // I set the current frequency to 0 Hz since the motor is not started yet, it just changed its state. It is an updateFrequency's task to make it actually start


  digitalWrite(ENABLE_PIN, LOW);  // If HIGH the driver is DISEABLE

  pwmRunning = false; // Again not yet rotating

  motorState = RAMPING; // Finally I change its state

}

void stopMotor() {  // Here AT_TARGET/RAMPING --> STOPPED

  motorState = STOPPED; //change the state

  pwmRunning = false; //Actually stop it from rotate
  stepPWM.suspend();

  measurementStarted = false; // invalidate photocells measurement window, the frequency has to measured only when the motor is actaully running

  digitalWrite(ENABLE_PIN, HIGH); // To disable the driver

  currentFrequency = 0.0; // Here we overwrite the state of the motor so that it will starts from freq = 0 at the next GO, accelerating from 0 to last freq set befor stopping.
                          // It is actually redoundant with what we do in startMotor, nontheless it's just fine

}
// ------

// ---NON-BLOCKING ACCELERATION FUNCTION (where the motor actually changes its frequency)---
void updateFrequency() {

  if (motorState != RAMPING) {  // if the motor is not running (STOPPED) and the target frequency has already been reached (AT_TARGET), do nothing
    previousFrequencyUpdate = micros();
    return;
  }

  // What to do if both the condition above are not satisfied, i.e. motor ON and target frequency to reach (stateMotor = RAMPING).
  unsigned long currentTime = micros(); // Take the current time

  unsigned long elapsedMicros = currentTime - previousFrequencyUpdate;  // Measure the time interval since the last change in frequency performed (notice that previousFrequencyUpdate = 0.0
                                                                        // by default, so the first time elapsedtime will be the time pass since the accension, or maybe not, since all
                                                                        // the overwriting of previousFrequencyUpdate performed above)

  if (elapsedMicros < FREQUENCY_UPDATE_INTERVAL) {  // At the begin we have defined how much time has to pass between two subsequent updates, i.e. 10 ms. So if is not passed enough time
    return;                                         // do nothing, here why this function is non-bloking
  }

  previousFrequencyUpdate = currentTime;  // Else overwrite the last time at which frequency was changed to the present time
  
  if (currentFrequency < targetFrequency) { // increase the frequency if the target is bigger than the current frequency, decrease it in the opposite situation
    currentFrequency += FREQUENCY_INCREMENT;  // the increment in frequency is now costant, I no longer waste time in unuseful operations
    if (currentFrequency >= targetFrequency) { // if we overcome the target in the increment process force the current to be equal to the target
      currentFrequency = targetFrequency;
    
      motorState = AT_TARGET; // Set the state of the machine as "frequency target reached (no more frequency update needed)", i.e. motorState = AT_TARGET. Then send the message.
                            // PAY ATTENTION: the state of the machine has to be changed only when the target is reached. But updateFrequency runs at every incrementation of
                            // the frequency, so motorState has to changed at the right moment, so inside the if-frequency-exceeded control
      Serial.print("Target frequency ");
      Serial.print(currentFrequency);
      Serial.println(" Hz reached!\n");
    }
  }
  else if (currentFrequency > targetFrequency) {
    currentFrequency -= FREQUENCY_INCREMENT;
    if (currentFrequency <= targetFrequency) {
      currentFrequency = targetFrequency;

      motorState = AT_TARGET;
      Serial.print("Target frequency ");
      Serial.print(currentFrequency);
      Serial.println(" Hz reached!\n");
    }
  }

  applyFrequency(currentFrequency); // Here we apply the new frequency of rotation at the motor after the incrementation

  if (!pwmRunning) {  // this if-control is needed for the first accension, but also in general everytime we want to make the motor start from a steady state.
                      // The motor starts from 0 Hz, but its target is 0.25 Hz (at the first accension, or some other value in subsequenct starts), so it has
                      // to increment its frequency and after the first incrementation effectively starts the signal at STEP_PIN.
      pwmRunning = true;
      stepPWM.resume(); // Here we restart the signal
  }

}
// ------

// ---To set the velocity of the motor---
void applyFrequency(float frequency) {  // This function compute the number of clock counts corresponding to a complete period of the STEP waveform

  float stepFrequency = STEPS_PER_REVOLUTION * frequency; // conversion frequency --> steps/second, which is the actual frequency of the waveform in output at STEP_PIN
  uint32_t periodCounts = timerFrequency / stepFrequency; // convert in clock counts

  stepPWM.get_timer()->set_period(periodCounts); // set them

  stepPWM.pulse_perc(50.0); // reset the duty-cycle

}
// ------

// ---The frequencymeter---

// ISR function
void photocellsPulse() {  // The function called by the ISR, now we try to make it measure the time passed between two subsequent passages
  lastPulseTime = micros();
  pulseCount++;
}

// The actual frequencymeter (note the non-blocking design)
void frequencyMeter() {

  // Take a snapshot of pulseCount and lastPulseTime
  // CRITICAL SECTION --> always as short as possible
  noInterrupts(); // those two functions respectively disable and enable the ISR, noInterrups() temporarily prevents interrupt handling in general
  unsigned long localPulseCount = pulseCount; // Declearing them here means make them really local variables: "GIVE VARIABLES THE SMALLEST SCOPE NECESSARY"
  unsigned long localLastPulseTime = lastPulseTime;
  interrupts();

  if (!measurementStarted) { // if we have not started a measurement window yet, do the following
    if (localPulseCount >= 1) { // if at least one pulse exists
      windowStartPulseCount = localPulseCount;  // store starting pulse count
      windowStartPulseTime = localLastPulseTime; // store starting pulse time
      measurementStarted = true; // mark window started
    }
    return;

  } else if ((localLastPulseTime - windowStartPulseTime) < MEASUREMENT_INTERVAL) { // has at least 5 seconds elapsed?
      return; // No, then return

  } else { // Yes, then perform the computation of the frequency
      measuredFrequency = (1000000.0*(localPulseCount - windowStartPulseCount))/(localLastPulseTime - windowStartPulseTime);
      windowStartPulseTime = localLastPulseTime; // Start a new window using the latest edge --> once a window finished the last edge of the old window becomes the first edge of the 
                                               // subsequent one.
      windowStartPulseCount = localPulseCount;
      // Serial.println(measuredFrequency, 6);
      // Serial.print("\n");
  }

// NOTE: I never reset pulseCount, which is the variable controlled by the ISR, the main loop just takes snapshots of it and I eventually use them to reset the reference from which
// the difference is calculated
}

// ------

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

  byte startError = writeRegister(INT_ENABLE, 0b00000001);

  if (startError != 0) { // Enable DATA_RDY interrupt:
                                                    // triggered when a new set of sensor data has been written to the sensor output registers
    Serial.print("STOP I2C ERROR: ");
    Serial.println(startError);
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

    detachInterrupt(digitalPinToInterrupt(INT_PIN));

    noInterrupts();
    dataReady = false;
    interrupts();

    byte stopError = writeRegister(INT_ENABLE, 0b00000000);

    if (stopError != 0) {
      Serial.print("STOP I2C ERROR: ");
      Serial.println(stopError);

      Wire.end();
      Wire.begin();

      //return false;
    }

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

  //Serial.write((uint8_t*)&sample, sizeof(sample));  // here the actual writer command. &sample is the Arduino's RAM address of the first byte of the struct, it says to Serial.wire
                                                    // where if the first byte of the struct. Then sizeof(sample) says to Serial.write() to continue writing for the number of bytes
                                                    // which constitute the packet.
                                                    // The cast uint8_t* says "consider those 12 bytes as a byte sequence", becasue this is what Serial.write() expects
                                                    // NOTE: &sample is a c++ pointer.

  delay(1000);

  Serial.print("ax = ");
  Serial.print(sample.ax);
  Serial.print("  ay = ");
  Serial.print(sample.ay);
  Serial.print("  az = ");
  Serial.println(sample.az);


}
// -------


// ---THE PARSER---

// ---To make the machine prints its status while running---
void printStatus() {
  // Machine state printing
  switch (motorState) {
    case STOPPED:
      Serial.println("MOTOR STATE: STOPPED");
      break;

    case RAMPING:
      Serial.println("MOTOR STATE: RAMPING");
      break;

    case AT_TARGET:
      Serial.println("MOTOR STATE: AT_TARGET");
      break;
  }

  switch (accelerometerState) {
    case INITIALIZING:
      Serial.println("ACCELEROMETER STATE: INITIALIZING");
      break;
    
    case READY:
      Serial.println("ACCELEROMETER STATE: READY");
      break;

    case MEASURING:
      Serial.println("ACCELEROMETER STATE: MEASURING");
      break;
  }

  // Printing of current frequency and target frequency of the motor
  Serial.print("CURRENT: ");
  Serial.println(currentFrequency, 3);
  Serial.print("TARGET: ");
  Serial.println(targetFrequency, 3);
  Serial.print("MEASURED: ");
  Serial.print(measuredFrequency, 3);
  Serial.println(" (Meaningful only if the motor is rotating)");

  // PWM state printing
  if (pwmRunning) {
    Serial.println("PWM: RUNNING\n");
  } else {
    Serial.println("PWM: DISABLED\n");
  }

}
// ------

// ---Printing function for HELP via serial. To see all the possible commands---
void printHelp() {
  Serial.println("AVAILABLE COMMANDS:");
  Serial.println("GO\tStarts the motor");
  Serial.println("STOP\tStops the motor and the data acquisition: end of the experiment");
  Serial.println("FREQ <Hz>\tSets the target frequency");
  Serial.println("STATUS\tPrint the current motor status");
  Serial.println("HELP\tPrint this command list");
  Serial.println("STATE?\tPrint the current accelerometer state");
  Serial.println("MEASURE\tStart the data acquisition");
  Serial.println("STOP_MEASURE\tStop the data acquisition\N");
}
// ----

// ---The actual Parser---
void updateSerial() {

  if (Serial.available() > 0) {

    String command = Serial.readStringUntil('\n');

    command.trim(); // to remove spaces between the characters of the strings

    if (command == "GO") {  // I am the one who has the possibility to actually change the state of the motor. So I run the changing-state-motorRiunning functions only trough
                            //  the keyboard
      startMotor(); // This function just set-up the motor to put it in a state in which it can actually starts running, but it will not starts it yet, this is a task for updateFrequency()
      if (motorState == RAMPING) { // Say to the user that the motor has started once its state has changed (notice that I'm deliberatly lying since startMotor() just change the state
                                   // of the machine, I'll have to wait another loop cycle to actually see the motor moving, but the cycle are really fast...)
        Serial.print("Motor started: target frequency ");
        Serial.print(targetFrequency);
        Serial.println(" Hz.");
      }
    }

    else if (command == "STOP") { // Same idea as above
      stopMotor();  // This function put the motor in a state where the driver is disable and the function of update do nothing since it change the state (note that stopMotor actually
                    // stop the motor, differently from startMotor which has a more "logical" task)

      if (accelerometerState == MEASURING) {  // When python send the STOP message, set-up the MPU to return to the READY state
        if (stopMeasurement()) {
          accelerometerState = READY;
          Serial.println("READY");
        }
      }
      
      if (accelerometerState == READY) {
        Serial.println("READY");
      }
    }

    else if (command.startsWith("FREQ ")) {
      if (accelerometerState == MEASURING) {
        Serial.println("Frequency change currently not permitted");
      }

      else {
          String valueText = command.substring(5);
          float frequency = valueText.toFloat();
          if (frequency > 0) {
            if (motorState != STOPPED) {  // If the motor is STOPPED changing the frequency does not have to change its state, it has to stay steady until I say to him to start
              motorState = RAMPING;
            }
            targetFrequency = frequency;  // update targetFrequency
            previousFrequencyUpdate = micros(); // to re-set the previousFrequencyUpdate again so to avoid long deltaTime in updateFrequency and then shot of the motor, now unuseful since
                                                // the delta time are set as default
            Serial.print("Target frequency set to ");
            Serial.print(frequency);
            Serial.println(" Hz\n");
          } else {
            Serial.println("ERROR: frequency must be greater than 0 Hz\n");
          }
      }
    }

    else if (command == "STATE?") {  // Python continously requesting the state of the machine

      if (accelerometerState == INITIALIZING) {
        Serial.println("INITIALIZING"); // Arduino communicates its state to python so that it can send the START message
      }

      else if (accelerometerState == READY) {
        Serial.println("READY");
      }

    }

    else if (command == "MEASURE") { // If you obtain the python START message, then set-up the MPU to start measuring and if it succedes change the state
      if (motorState == AT_TARGET || motorState == STOPPED) {
        if (accelerometerState == READY) {
          if (startMeasurement()) {
            accelerometerState = MEASURING;
            Serial.println("MEASURING");  // Once python has received the MEASURING-state-been-reached message, it can switch the binary packet decoder (important because I'm using the
                                          // same serial connection for text and binary data)
          }
        } else {
          Serial.println("Accelerometer NOT READY");
        }
      }

      else {
        Serial.println("MEASURING currently not permitted");
      }
    }

    else if (command == "STOP_MEASURE") {
      if (accelerometerState == MEASURING) {
        if (stopMeasurement()) {
          accelerometerState = READY;
          Serial.println("READY");
        }
      }

    }

    else if (command == "STATUS"){
      printStatus();
    }

    else if (command == "HELP") {
      printHelp();
    }

    else{
      Serial.println("Unknown command. Type HELP to visualize the command list\n");
    }
  }
}
// ------



void setup() {

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(PHOTOC_PIN, INPUT);

  pinMode(INT_PIN, INPUT);

  Serial.begin(115200);

  Wire.begin();

  digitalWrite(DIR_PIN, HIGH);

  // Initially keep the driver disabled
  digitalWrite(ENABLE_PIN, HIGH);

  // Inizialization and accension of the PWM pin.

  //stepPWM.pulse_perc(50.0); // this set the duty cycle alone
  stepPWM.begin(START_FREQUENCY*STEPS_PER_REVOLUTION, duty_cycle); // this starts the pin with the initial frequency, and that's why the startMotor function does not actually start the
                                                                   // motor but just changes its state: if it would be directly started it would not accelerate and immediatly starts at
                                                                   // 0.25 Hz, so that task is give only to updateFrequency. Actually the motor is not able to accelerate from 0 to 0.25
                                                                   // Hz, too little steps, so all this caution reveals to be unnecessary

  timerFrequency = stepPWM.get_timer()->get_freq_hz();  // this syntax is curious, form the function takes one of its methods, whatever this is, I know anything of C++, so sad

  stepPWM.suspend();  //to suspend the rotation of the motor, i.e. the signal sent to the driver form the STEP_PIN

  attachInterrupt(digitalPinToInterrupt(PHOTOC_PIN), photocellsPulse, RISING); // the actual ISR, I have to specify the PIN, the event and finally the function to call when the event
                                                                              // happens. Is role is basically to configure the hardware so that a rising edge on the phs pin triggers
                                                                              // photocellsPulse()

}

void loop() { // What to do while the system is ON.

  updateFrequency(); // Firstly let's check if the frequency is OK and eventually updates it to reach the target requested
  updateSerial(); // Secondly update the serial communication so that the user can change the state of the motor
  frequencyMeter(); // Lastly measure the frequency from the photocells signal.

  if (accelerometerState == INITIALIZING) {
    if (initializeMPU()) {
      accelerometerState = READY;
    }
  }
  
  else if (accelerometerState == MEASURING) {
    dataReader();
  }

}

