/*
In this sketch we compute the velocity of the motor, giving it in the void loop
before uploading the sketch, measured in °/s, knowing that one revolution takes
400 steps, which gives the conversion factor from steps to degrees or vice-versa.
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

void moveSteps(int steps, float stepPerSecond){
  if (steps > 0){
    digitalWrite(DIR_PIN, HIGH);
  }
  else{
    digitalWrite(DIR_PIN, LOW);
    steps = -steps;
  }

  unsigned long halfperiod = 1000000 / (2 * stepPerSecond);

  for (int i = 0; i < steps; i++){
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(halfperiod);

    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(halfperiod);
  }

}

void moveDegree(float angle, float velDegreePerSecond){

  int steps = round(angle * STEPS_PER_REVOLUTION / 360);

  float stepPerSecond = velDegreePerSecond * (STEPS_PER_REVOLUTION / 360.0);

  moveSteps(steps, stepPerSecond);
}

void loop() {

  moveDegree(90, 450);
  delay(2000);

  moveDegree(-180, 450);
  delay(2000);

}
