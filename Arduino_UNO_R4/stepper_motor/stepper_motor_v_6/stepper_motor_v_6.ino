/*
In this sketch we use FspTimer module to make the motor rotate and we change the design of the updateFrequency function so that it performs an increment every 10 ms and doesn't lose
time in measuring continuosly the time, avoiding abrut changes in frequency.
The idea is that the microcontroller integrated inside the Arduino's chip has a piece of hardware which is totally dedicated to a timer, FspTimer is the lowest level software interface
able to configure and talk to this hardware, up of it we find pwd.h, which is though just to create a square wave form design at whish. Here we use both of them since this hardware can
be used in parallel to the CPU so that we can use it to do other stuff while others part of the chip keep the motor in rotation. Since the only use of pwm is insufficient cause of its
slowliness, we need to use a lower level interface to control this hardware, ie FspTimer. It can directly control how many clock cycles the period of the wave form last, so, since the
clock frequency is 48 MHz, we have a high-precision control onto the rotational frequency.
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

// Boolean variables which define the state of the motor while is running
bool motorRunning = false; // This one says if the motor is actually rotating or not. Is it on/in motion? Then motorRunning = true, Is it not, i.e. is off? Then motorRunning = false.
bool targetReached = false; // It says if the frequency of the motor need to be change (if false, as default) or not
bool pwmRunning = false; // The logical state of the PWM Arduino's pin used to rotate the motor, if HIGH it is rotating instead it is not.

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

  // if (targetFrequency <= 0) { // negative frequency has no physicall meanings
  //   Serial.println("ERROR: set a frequency before starting.");
  //   return;
  // }

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

  //applyFrequency(currentFrequency); // Then I set the frequency of the motor equal to the start value

  // stepState = LOW;  // Then I set up the state of the motor so that it can actually start

  // digitalWrite(STEP_PIN, LOW);  // Here the substitution LOW --> stepState is irrilevant

  digitalWrite(ENABLE_PIN, LOW);  // If HIGH the driver is DISEABLE

  pwmRunning = false;

  motorRunning = true;

}

void stopMotor() {

  motorRunning = false; // So that both updateMotor and updateFrequency don't start

  pwmRunning = false;

  //stepState = LOW;  // This change, as in startMotor, does nothing except to give the possibility to the software of keeping track of the state of the motor...actually stepState is
                    // potentially unuseful.

  // digitalWrite(STEP_PIN, LOW);  // To force the motor to do not rotate

  stepPWM.suspend();

  digitalWrite(ENABLE_PIN, HIGH); // To disable the driver

  currentFrequency = 0.0; // Here we overwrite the state of the motor so that it will starts from freq = 0 at the next GO, accelerating from 0 to last freq set befor stopping.
  targetReached = false;  // NOTE: it is important that both currentFrequency and targetReached being changed so that updateFrequency can both run and perform the right acceleration
                          // from 0 to the last targetFrequency.

}
// ------

// ---First Parser Ever---
void updateSerial() {
  if (Serial.available() > 0) {

    String command = Serial.readStringUntil('\n');

    command.trim(); // to remove spaces between the characters of the strings

    if (command == "GO") {  // I am the one who has the possibility to actually change the state of the motor. So I run the changing-state-motorRiunning functions only trough
                            //  the keyboard
      startMotor(); // This function just set-up the motor to put it in a state in which it can actually starts running, but it will not starts it yet, this is a task for updateMotor
      if (motorRunning) { // startMotor() set motorRunning to "true", then the if condition is always satisfied
        Serial.print("Motor started: frequency ");
        Serial.print(targetFrequency);
        Serial.println(" Hz.");
      }
    }

    else if (command == "STOP") { // Same idea as above
      stopMotor();  // This function put the motor in a state where the driver is disable and the function of update do nothing since it change the value of motorRunning
      Serial.println("Motor stopped");
    }

    else if (command.startsWith("FREQ ")) {
      String valueText = command.substring(5);
      float frequency = valueText.toFloat();
      if (frequency > 0) {
        targetFrequency = frequency;  // update targetFrequency
        targetReached = (currentFrequency == targetFrequency);  // put targetReached to false
        previousFrequencyUpdate = micros(); // to re-set the previousFrequencyUpdate again so to avoid long deltaTime in updateFrequency and then shot of the motor
        Serial.print("Target frequency set to ");
        Serial.print(frequency);
        Serial.println(" Hz");
      } else {
        Serial.println("ERROR: frequency must be greater than 0 Hz");
      }
    }

    else{
      Serial.println("Unknown command");
    }
  }
}
// ------

// ---Non-blocking acceleration function---
void updateFrequency() {

  if (!motorRunning || targetReached) {  // if the motor is not running and the target frequency has already been reached, do nothing
    //Serial.print("passo1");
    previousFrequencyUpdate = micros();
    return;
  }

  // if (targetReached) {  // if the target frequency has already been reached do nothing
  //   return;
  // }

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
    
      targetReached = true; // Set the state of the machine as "frequency target reached (no more frequency update needed)". Then send the message.
                            // PAY ATTENTION: the state of the machine has to be changed only when the target is reached. But updateFrequency runs at every incrementation of
                            // the frequency, so targetReached has to changed at the right moment, so inside the if-frequency-exceeded control
      Serial.print("Target frequency ");
      Serial.print(currentFrequency);
      Serial.println(" Hz reached!");
    }
  }
  else if (currentFrequency > targetFrequency) {
    currentFrequency -= FREQUENCY_INCREMENT;
    // Serial.println(currentFrequency);
    if (currentFrequency <= targetFrequency) {
      currentFrequency = targetFrequency;

      targetReached = true;
      Serial.print("Target frequency ");
      Serial.print(currentFrequency);
      Serial.println(" Hz reached!");
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

