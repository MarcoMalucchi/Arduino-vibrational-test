/*
HERE THE SECOND VERSION OF THE STEPPER-MOTOR CONTROLLER WITH PHOTOCELLS TO ACTIVELLY MEASURE ITS FREQUENCY
we have changed the way with which the measure is performed --> measure of the exact elapsed time between first and last photocells edges occuring during the window of measurements

Base idea: we use the internal hardware of the microcontroller inside the Arduino chip to generate a square waveform (considering its source, with high-precision frequency) which
is sent trough the STEP_PIN directly to the motor driver and which then control the rotation of the motor.
This is good because in this way we can use an already-existing hardware inside the Arduino's chip, avoiding overwhelming its CPU which remains relatively free for others operations
(I2C communication with the accelerometer and the photocells control).
Knowing that the motor has to accelerate, decelerate and keep a steady state configuration rotating at costant frequency and that the user (me or python controller) has to be able to 
stops it or starts it, the sketch is based onto the following idea:

While the Arduino send the square waveform signal to the driver trough the STEP_PIN it has to continuosly:

  1. Check the serial communication so to know if the state of the motor has to change.

  2. Update the state of the motor, which basically means start it (GO), stop it (STOP) or make it accelerate/decelerate

These are the actions continuosly repeated inside the void loop. This is implemented in practice basing onto the follwoing concept:

  "We know which are the actions that the motor can perform (see above) and is the user the only one which/who can decide what of those actions the motor as to perform instantly"

So we carachterize the "machine" (the motor in this case) with all its possible states (state machine): RAMPING, STOPPED and AT_TARGET. Once done that we build a serial parser ready
to catch a precise list of commands thought to change the state of the motor and consequently made it do its stuff. Then we create a functions that, considering the current state,
perform the relative operation.

So the design is basically: User says "Motor you have to stay in the state X" and Arduino take this message and change the state of the motor. How?

  STOPPED --> defaul state, the motor goes there when the user types "STOP" in the serial
  AT_RAMPING --> the motor goes there when the user types GO and set a new frequency
  AT_TARGET --> once the new freqeuncy is reached and the motor is running

Then who starts the motor: startMotor()    STOPPED --> RAMPING

Who stops the motor: stopMotor()    AT_TARGET/RAMPING --> STOPPED

Who changes the frequency of the motor: updateFrequency() and applyFrequency()    RAMPING --> AT_TARGET

NEW FREQUENCY MEASUREMENTS DESIGN:

1. Start of a new measurement window of 5 seconds (as default)
2. The main code has to remember the starting pulse count (how many counts at the begginign of the window) and the starting time stamp (the instant at which the window is opened)
3. Wait until the window close (wiat at least 5 seconds)
4. Main code takes a snapshot of current pulseCount (how many counts has been perfomed) and the timestamp of the most recent edge
5. Finally main code compute the number of periods performed by the photocells signal (currentPulseCount - startingPulseCount) and the elapsed edge-to-edge time (lastPulseTime - 
startingPulseTime), now compute the frequency

*/

#include "pwm.h"  // the library which contains the instruction to control the PWM mode of the pins and the FspTimer commands


// Global variables for Arduino's motor digital pins
const int DIR_PIN = 7;  // to control the direction of rotation
const int STEP_PIN = 5; // to control the rotation of the motor shaft
const int ENABLE_PIN = 4; // to enable or disable the driver of the motor, so to stop it or start it
const int PHOTOC_PIN = 2; // photocells pin, in Arudino uno r4 minima only pin D2 and D3 can be used in the interrupt mode


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

// ---THE PARSER---

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
  Serial.println("STOP\tStops the motor");
  Serial.println("FREQ <Hz>\tSets the target frequency");
  Serial.println("STATUS\tPrint the current motor status");
  Serial.println("HELP\tPrint this command list\n");
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
      Serial.println("Motor stopped\n");
    }

    else if (command.startsWith("FREQ ")) {
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
      Serial.println(measuredFrequency, 6);
      Serial.print("\n");
  }

// NOTE: I never reset pulseCount, which is the variable controlled by the ISR, the main loop just takes snapshots of it and I eventually use them to reset the reference from which
// the difference is calculated
}

// ------

void setup() {

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(PHOTOC_PIN, INPUT);

  Serial.begin(115200);

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

}

