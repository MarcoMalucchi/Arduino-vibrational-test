const int DIR_PIN = 2;
const int STEP_PIN = 3;
const int ENABLE_PIN = 4;

const int N_STEPS = 200;

void setup() {
  // put your setup code here, to run once:
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
}

void loop() {

  digitalWrite(DIR_PIN, HIGH);

  for (int i = 0; i < N_STEPS; i++){
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(5000);

    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(5000);
  }

  delay(2000);

  digitalWrite(DIR_PIN, LOW);

  for (int i = 0; i < N_STEPS; i++){
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(5000);

    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(5000);
  }

  delay(2000);

}
