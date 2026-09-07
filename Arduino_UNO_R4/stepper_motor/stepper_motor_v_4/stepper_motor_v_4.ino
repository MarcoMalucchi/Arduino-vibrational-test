/*
In this sketch we start removing the blocking code working onto the state of the
motor. Moreover we changed the unit of measurement of the velocity, moving to frequency,
intended as number of revolution per second.
Then we defined also the first machine states: motorRunning (is it rotating or not?), targetReached (do I need to change the frequency or I have already reached the target frequency?),
stepState (which is the state of the STEP_PIN, which I use to control the motion of the motor shaft?).
*/


// Global variables for Arduino's motor digital pins
const int DIR_PIN = 2;  // to control the direction of rotation
const int STEP_PIN = 3; // to control the rotation of the motor shaft
const int ENABLE_PIN = 4; // to enable or disable the driver of the motor, so to stop it or start it

// Number of steps needed for the motor to execute a complete turn: needed for the conversion from frequency to steps per second, in turn necessary to set the frequency of rotation
// of the shaft.
const int STEPS_PER_REVOLUTION = 400;

// Boolean variables which define the state of the motor while is running
bool motorRunning = false; // This one says if the motor is actually rotating or not. Is it on/in motion? Then motorRunning = true, Is it not, i.e. is off? Then motorRunning = false.
bool targetReached = false; // It says if the frequency of the motor need to be change (if false, as default) or not
bool stepState = LOW; // The logical state of the Arduino digital pin used to control the rotation of the motor (when it raise the motor makes a step)
bool acceleration = false;

// Variables for the non-blocking STEP generator, needed to make the shaft rotate
unsigned long previousStepTime = 0; // Time at which the motor did its last step
unsigned long halfperiod; // The duration of the half-period of the signal send at the motor driver from arduino, inversly linked to its frequency of rotation

// Variables needed to keep track of the frequency of the motor, last update and to perform its increase or decrease for the acceleration of the motor.
float currentFrequency = 0.0;
float targetFrequency = 0.0;
float frequencyAcceleration = 0.25;  // Hz/s set as default, may be increased
unsigned long previousFrequencyUpdate = 0;  // The time at which the last frequency change was performed
const unsigned long FREQUENCY_UPDATE_INTERVAL = 10000;  // The time that pass between subsequent frequency changes


const float START_FREQUENCY = 0.25; // The default frequency of the motor at which it will starts rotate when started (then at the first accension when motorRunning = true for the 
// first time)


void setup() {

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);

  Serial.begin(115200);

  digitalWrite(DIR_PIN, HIGH);

  // Initially keep the driver disabled
  digitalWrite(ENABLE_PIN, HIGH);

}

void loop() { // What to do while the system is ON.

  updateFrequency(); // Firstly let's check if the frequency is OK and eventually updates it to reach the target requested
  updateMotor();  // Secondly update the motor, i.e. change it's velocity, using the frequency computed above.
  updateSerial(); // Lastly update the serial communication so that the user can change the state of the motor (motorRunning, targetReached, stepState)

}

// ---Motor start-stop functions---
void startMotor() {

  // if (targetFrequency <= 0) { // negative frequency has no physicall meanings
  //   Serial.println("ERROR: set a frequency before starting.");
  //   return;
  // }

  unsigned long startTime = micros(); // take the time at which the motor starts (notice it is a local variable)
  previousStepTime = startTime; // overwrite the last time at which the motor has taken a step (consider that I want to start the motor, so the time reference for updateMotor has to be
                                // that at which I type "GO" in the Serial Monitor)
  previousFrequencyUpdate = startTime; // Same idea as above, the reference time at which the state of the motor has to change (both its frequency and the fact that it is runnning or not)
                                        // has to be that at which the motor has been started.

  if (targetFrequency == 0.0){  // I want the motor to rotate at 0.25 Hz only when I start it for the first time, then, after subsequently stops, I want it to restart from the last
                                // target frequency set.
    targetFrequency = START_FREQUENCY; 
  }

  //applyFrequency(currentFrequency); // Then I set the frequency of the motor equal to the start value

  stepState = LOW;  // Then I set up the state of the motor so that it can actually start

  digitalWrite(STEP_PIN, LOW);  // Here the substitution LOW --> stepState is irrilevant

  digitalWrite(ENABLE_PIN, LOW);  // If HIGH the driver is DISEABLE

  motorRunning = true;

}

void stopMotor() {

  motorRunning = false; // So that both updateMotor and updateFrequency don't start

  stepState = LOW;  // This change, as in startMotor, does nothing except to give the possibility to the software of keeping track of the state of the motor...actually stepState is
                    // potentially unuseful.

  digitalWrite(STEP_PIN, LOW);  // To force the motor to do not rotate

  digitalWrite(ENABLE_PIN, HIGH); // To disable the driver

  currentFrequency = 0.0; // Here we overwrite the state of the motor so that it will starts from freq = 0 at the next GO, accelerating from 0 to last freq set befor stopping.
  targetReached = false;  // NOTE: it is important that both currentFrequency and targetReached being changed so that updateFrequency can both run and perform the right acceleration
                          // from 0 to the last targetFrequency.

}
// ------

// ---Update the state-of-the-motor function, i.e the function that actually makes it rotate---
void updateMotor() {

  if (!motorRunning) {  // if the motor is not running do nothing
    return;
  }

  unsigned long currentTime = micros(); // take the current time

  if ((currentTime - previousStepTime) >= halfperiod) { // considering the halfperiod of the STEP signal set (see applyFrequency), if is not pass enough time yet the motor does not
                                                        // have to make another step, else it has to, so continue inside the if control
    previousStepTime = currentTime; // overwrite the last time at which the motor has performed a step to the current time

    stepState = !stepState; // change the state of the STEP pin to make the motor perform its steps

    digitalWrite(STEP_PIN, stepState);  // the actual change of the STEP_PIN level (NOTE: stepState is set LOW by defaul, so at the fisrt accension of the motor it will be set HIGH and
                                        // the motor will starts accordingly)
    
  }
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

  float deltaTime = elapsedMicros / 1000000.0;  // Convert the elapsedMicros in seconds for uniformity of units

  //Serial.println(deltaTime);

  //Serial.println(deltaTime);

  float deltaFrequency = frequencyAcceleration * deltaTime; // Compute the increment of frequency considering a costant increase in time (defined at the beggining)

  //Serial.print(deltaTime);
  
  if (currentFrequency < targetFrequency) { // increase the frequency if the target is bigger than the current frequency, decrease it in the opposite situation
    currentFrequency += deltaFrequency;
    Serial.println(currentFrequency);
    if (currentFrequency > targetFrequency) { // if we overcome the target in the increment process force the current to be equal to the target
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
    currentFrequency -= deltaFrequency;
    Serial.println(currentFrequency);
    if (currentFrequency < targetFrequency) {
      currentFrequency = targetFrequency;

      targetReached = true;
      Serial.print("Target frequency ");
      Serial.print(currentFrequency);
      Serial.println(" Hz reached!");
    }
  }

  applyFrequency(currentFrequency); // Here we apply the new frequency of rotation at the motor after the incrementation

}
// ------

// ---To set the velocity of the motor---
void applyFrequency(float frequency) {  // NOTE: this function just compute the new halfperiod of the signal that is send trough the STEP_PIN to the driver. Then updateMotor will
                                        // actually use this walue to set the new velocity in steps/second.

  float stepsPerSecond = STEPS_PER_REVOLUTION * frequency; // conversion frequency --> steps/second

  halfperiod = 1000000.0 / (2.0 * stepsPerSecond);  // halperiod computation

}
// ------