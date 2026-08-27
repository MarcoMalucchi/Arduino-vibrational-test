/*
in version 6 we obtained a fully-working motor, now we want to perfect its design improving the machine-states management
*/

#include "pwm.h"  // the library which contains the instruction to control the PWM mode of the pins and the FspTimer commands


// Global variables for Arduino's motor digital pins
const int DIR_PIN = 2;  // to control the direction of rotation
const int STEP_PIN = 3; // to control the rotation of the motor shaft
const int ENABLE_PIN = 4; // to enable or disable the driver of the motor, so to stop it or start it

// Setting the STEP_PIN so that it can be used in PWM mode
PwmOut stepPWM(STEP_PIN);
float duty_cycle = 50.0;  // the duty cycle of the wave form have to be set everytime a new frequency is reached

// Number of steps needed for the motor to execute a complete turn: needed for the conversion from frequency to steps per second, in turn necessary to set the frequency of rotation
// of the shaft.
const int STEPS_PER_REVOLUTION = 400;

// The frequency of the internal clock of the Arduino's integrated microcontroller
uint32_t timerFrequency;

// Boolean variables which define the state of the motor while is running --> HOLD VERSION / LOGIC
// bool motorRunning = false; // This one says if the motor is actually rotating or not. Is it on/in motion? Then motorRunning = true, Is it not, i.e. is off? Then motorRunning = false.
// bool targetReached = false; // It says if the frequency of the motor need to be change (if false, as default) or not
bool pwmRunning = false; // The logical state of the PWM Arduino's pin used to rotate the motor, if HIGH it is rotating instead it is not.

// New version of machine states
enum MotorStates {
  STOPPED,
  RAMPING,
  AT_TARGET
};

MotorStates motorState = STOPPED;

// Variables needed to keep track of the frequency of the motor, last update and to perform its increase or decrease for the acceleration of the motor.
float currentFrequency = 0.0;
float targetFrequency = 0.0;
float frequencyAcceleration = 0.25;  // Hz/s set as default, may be increased
const unsigned long FREQUENCY_UPDATE_INTERVAL = 10000;  // The time that pass between subsequent frequency changes
const float FREQUENCY_INCREMENT = frequencyAcceleration * (FREQUENCY_UPDATE_INTERVAL / 1000000.0);  // Here we directly se as default the increment in frequency wanted, avoiding to make
                                                                                                    // it changes
unsigned long previousFrequencyUpdate = 0;  // The time at which the last frequency change was performed, important to know when to change the frequency (we can't change the frequency
                                            // continuosly, we have others things to do)


const float START_FREQUENCY = 0.25; // The default frequency of the motor at which it will starts rotate when started (then at the first accension when motorRunning = true for the 
// first time)

// ---Motor start-stop functions---
void startMotor() {

  unsigned long startTime = micros(); // take the time at which the motor starts (notice it is a local variable)
  //previousStepTime = startTime; // overwrite the last time at which the motor has taken a step (consider that I want to start the motor, so the time reference for updateMotor has to be
                                // that at which I type "GO" in the Serial Monitor)
  previousFrequencyUpdate = startTime; // Same idea as above, the reference time at which the state of the motor has to change (both its frequency and the fact that it is runnning or not)
                                        // has to be that at which the motor has been started.

  if (targetFrequency == 0.0){  // I want the motor to rotate at 0.25 Hz only when I start it for the first time, then, after subsequently stops, I want it to restart from the last
                                // target frequency set.
    targetFrequency = START_FREQUENCY; 
  }

  currentFrequency = 0.0;


  digitalWrite(ENABLE_PIN, LOW);  // If HIGH the driver is DISEABLE

  pwmRunning = false;

  motorState = RAMPING;

}

void stopMotor() {

  motorState = STOPPED;

  pwmRunning = false;

  //stepState = LOW;  // This change, as in startMotor, does nothing except to give the possibility to the software of keeping track of the state of the motor...actually stepState is
                    // potentially unuseful.

  // digitalWrite(STEP_PIN, LOW);  // To force the motor to do not rotate

  stepPWM.suspend();

  digitalWrite(ENABLE_PIN, HIGH); // To disable the driver

  currentFrequency = 0.0; // Here we overwrite the state of the motor so that it will starts from freq = 0 at the next GO, accelerating from 0 to last freq set befor stopping.

}
// ------

// ---To make the machine prints its status while running---
void printStatus() {
  // Machine state printing
  switch (motorState) {
    case STOPPED:
      Serial.println("STATE: STOPPED");
      break;

    case RAMPING:
      Serial.println("STATE: RAMPING");
      break;

    case AT_TARGET:
      Serial.println("STATE: AT_TARGET");
      break;
  }

  // Current frequency and target frequency of the motor printing
  Serial.print("CURRENT: ");
  Serial.println(currentFrequency);
  Serial.print("TARGET: ");
  Serial.println(targetFrequency);

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
  Serial.println("STOP\tStops the motor");
  Serial.println("FREQ <Hz>\tSets the target frequency");
  Serial.println("STATUS\tPrint the current motor status");
  Serial.println("HELP\tPrint this command list\n");
}
// ----

// ---First Parser Ever---
void updateSerial() {
  if (Serial.available() > 0) {

    String command = Serial.readStringUntil('\n');

    command.trim(); // to remove spaces between the characters of the strings

    if (command == "GO") {  // I am the one who has the possibility to actually change the state of the motor. So I run the changing-state-motorRiunning functions only trough
                            //  the keyboard
      startMotor(); // This function just set-up the motor to put it in a state in which it can actually starts running, but it will not starts it yet, this is a task for updateMotor
      if (motorState == RAMPING) { // startMotor() set motorRunning to "true", then the if condition is always satisfied
        Serial.print("Motor started: target frequency ");
        Serial.print(targetFrequency);
        Serial.println(" Hz.");
      }
    }

    else if (command == "STOP") { // Same idea as above
      stopMotor();  // This function put the motor in a state where the driver is disable and the function of update do nothing since it change the value of motorRunning
      Serial.println("Motor stopped\n");
    }

    else if (command.startsWith("FREQ ")) {
      String valueText = command.substring(5);
      float frequency = valueText.toFloat();
      if (frequency > 0) {
        if (motorState != STOPPED) {
          motorState = RAMPING;
        }
        targetFrequency = frequency;  // update targetFrequency
        previousFrequencyUpdate = micros(); // to re-set the previousFrequencyUpdate again so to avoid long deltaTime in updateFrequency and then shot of the motor
        Serial.print("Target frequency set to ");
        Serial.print(frequency);
        Serial.println(" Hz\n");
      } else {
        Serial.println("ERROR: frequency must be greater than 0 Hz\n");
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

// ---Non-blocking acceleration function---
void updateFrequency() {

  if (motorState != RAMPING) {  // if the motor is not running and the target frequency has already been reached, do nothing
    previousFrequencyUpdate = micros();
    return;
  }

  // What to do if both the condition above are not satisfied, i.e. motor ON (motorRunning = true) and target frequency to reach (targetReached = false).
  unsigned long currentTime = micros(); // Take the current time

  unsigned long elapsedMicros = currentTime - previousFrequencyUpdate;  // Measure the time interval since the last change in frequency performed (notice that previousFrequencyUpdate = 0.0
                                                                        // by default, so the first time elapsedtime will the time pass since the accension)

  if (elapsedMicros < FREQUENCY_UPDATE_INTERVAL) {  // At the begin we have defined how much time has to pass between two subsequent updates, i.e 10 ms. So if is not passed enough time
    return;                                         // do nothing
  }

  previousFrequencyUpdate = currentTime;  // Else overwrite the last time at which frequency was changed to the present time
  
  if (currentFrequency < targetFrequency) { // increase the frequency if the target is bigger than the current frequency, decrease it in the opposite situation
    currentFrequency += FREQUENCY_INCREMENT;
    // Serial.println(currentFrequency);
    if (currentFrequency >= targetFrequency) { // if we overcome the target in the increment process force the current to be equal to the target
      currentFrequency = targetFrequency;
    
      motorState = AT_TARGET; // Set the state of the machine as "frequency target reached (no more frequency update needed)". Then send the message.
                            // PAY ATTENTION: the state of the machine has to be changed only when the target is reached. But updateFrequency runs at every incrementation of
                            // the frequency, so targetReached has to changed at the right moment, so inside the if-frequency-exceeded control
      Serial.print("Target frequency ");
      Serial.print(currentFrequency);
      Serial.println(" Hz reached!\n");
    }
  }
  else if (currentFrequency > targetFrequency) {
    currentFrequency -= FREQUENCY_INCREMENT;
    // Serial.println(currentFrequency);
    if (currentFrequency <= targetFrequency) {
      currentFrequency = targetFrequency;

      motorState = AT_TARGET;
      Serial.print("Target frequency ");
      Serial.print(currentFrequency);
      Serial.println(" Hz reached!\n");
    }
  }

  applyFrequency(currentFrequency); // Here we apply the new frequency of rotation at the motor after the incrementation

  if (!pwmRunning) {
      pwmRunning = true;
      stepPWM.resume();
  }

}
// ------

// ---To set the velocity of the motor---
void applyFrequency(float frequency) {  // NOTE: this function just compute the new halfperiod of the signal that is send trough the STEP_PIN to the driver. Then updateMotor will
                                        // actually use this walue to set the new velocity in steps/second.

  float stepFrequency = STEPS_PER_REVOLUTION * frequency; // conversion frequency --> steps/second, which is the actual frequency of the waveform in output at STEP_PIN
  uint32_t periodCounts = timerFrequency / stepFrequency;

  stepPWM.get_timer()->set_period(periodCounts);

  stepPWM.pulse_perc(50.0);

}
// ------

void setup() {

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);

  Serial.begin(115200);

  digitalWrite(DIR_PIN, HIGH);

  // Inizialization and accension of the PWM pin.

  //stepPWM.pulse_perc(50.0); // this set the duty cycle alone
  stepPWM.begin(START_FREQUENCY*STEPS_PER_REVOLUTION, duty_cycle); // this starts the pin with the initial frequency

  timerFrequency = stepPWM.get_timer()->get_freq_hz();

  stepPWM.suspend();  //to suspend the rotation of the motor, i.e. the signal sent to the driver form the STEP_PIN

  // Initially keep the driver disabled
  digitalWrite(ENABLE_PIN, HIGH);

  // uint32_t timer_freq = stepPWM.get_timer()->get_freq_hz();
  // uint32_t n_counts = stepPWM.get_timer()->get_period_raw();

  // Serial.println(timer_freq);
  // Serial.println(n_counts);
  // Serial.println((float)timer_freq / n_counts);

}

void loop() { // What to do while the system is ON.

  updateFrequency(); // Firstly let's check if the frequency is OK and eventually updates it to reach the target requested
  //updateMotor();  // Secondly update the motor, i.e. change it's velocity, using the frequency computed above.
  updateSerial(); // Lastly update the serial communication so that the user can change the state of the motor (motorRunning, targetReached, stepState)

}

