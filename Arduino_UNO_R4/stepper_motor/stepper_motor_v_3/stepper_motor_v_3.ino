/*
In this sketch we accelerate the motor with a constant acceleration, setting an
initial frequency and a target one. We use micros() to kepp track of the time passed
from the starting of the motor. This has also the good property of not being a blocking
function, which will help us later.
Moreover we decelerate it in the same way. Ultimatly we wrote a moveForTime function
that instead making the motor moving for a certain angle, it makes it move for a 
certain time.
*/


const int DIR_PIN = 2;
const int STEP_PIN = 3;
const int ENABLE_PIN = 4;


const int STEPS_PER_REVOLUTION = 400;

void setup() {
  // put your setup code here, to run once:
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);

  // driver ENABLE is actively LOW
  digitalWrite(ENABLE_PIN, LOW);
}

void Accelerate(float initialVel, float finalVel, float acceleration) {

  unsigned long startTime = micros();

  float currentVel = initialVel;

  while (
    (acceleration > 0 && currentVel < finalVel) ||
    (acceleration < 0 && currentVel > finalVel)
  )
  {

    unsigned long currentTime = micros();

    float elapsedTime = (currentTime - startTime)/1000000.0;

    currentVel = initialVel + acceleration*elapsedTime;
  
    if (acceleration > 0 && currentVel > finalVel) {
        currentVel = finalVel;
    }

    if (acceleration < 0 && currentVel < finalVel) {
        currentVel = finalVel;
    }

    float stepsPerSecond = currentVel * STEPS_PER_REVOLUTION/360.0;

    unsigned long halfperiod = 1000000.0 / (2.0 * stepsPerSecond);

    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(halfperiod);

    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(halfperiod);

  }
}

void moveForTime(float velocity, float duration) {

  float stepPerSecond = velocity * (STEPS_PER_REVOLUTION / 360.0);

  unsigned long halfperiod = 1000000.0 / (2.0 * stepPerSecond);

  unsigned long startTime = micros();

  while ((micros() - startTime) < duration * 1000000.0) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(halfperiod);

    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(halfperiod);
  }
}

void loop() {

  digitalWrite(DIR_PIN, HIGH);

  Serial.print("Acceleration started!\n");  
  Accelerate(90, 450, 180);
  Serial.print("Acceleration done!\n");
  moveForTime(450, 3);
  Serial.print("Deceleration started!\n");  
  Accelerate(450, 90, -180);
  Serial.print("Deceleration done!\n");
  moveForTime(90, 3);

}
