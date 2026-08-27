/*
Finally we create a non-blocking version of the photocells-control sketch which can be implemented in the motor-stepper-control sketch. Here we compute the frequency every 5 seconds
avareging on the total number of passeges, designing a non-blocking logical structure.

Every 5 seconds the sketch will take all the passeges registered from the ISR and the total time passed from the last measure of the frequency. Using these two information we can
compute the new frequency. All of this may be inserted inside a new function which will be called inside the loop()
*/

const int PHOTOC_PIN = 2; // in Arudino uno r4 minima only pin D2 and D3 can be used in the interrupt mode
const int STEP_PIN = 3;

#include "pwm.h"
PwmOut stepPWM(STEP_PIN);
float duty_cycle = 50.0;
const int STEPS_PER_REVOLUTION = 400;

volatile unsigned long pulseCount = 0;

unsigned long previousTime = 0;
unsigned long previousPulseCount = 0;

const unsigned long MEASUREMENT_INTERVAL = 5000; // The amount of time which has to pass between subsequent measurement

// float frequency; --> This may become a local variable inside frequencyMeter()

void photocellsPulse() {  // The function called by the ISR, now we try to make it measure the time passed between two subsequent passages
  pulseCount++;
}

void frequencyMeter() {

  unsigned long currentTime = millis();

  if (currentTime - previousTime < MEASUREMENT_INTERVAL) {  // measure the frequency every 5 seconds
    return;
  }

  // CRITICAL SECTION --> always as short as possible
  noInterrupts(); // those two functions respectively disable and enable the ISR, noInterrups() temporarily prevents interrupt handling in general
  unsigned long localPulseCount = pulseCount; // Declearing them here means make them really local variables: "GIVE VARIABLES THE SMALLEST SCOPE NECESSARY"
  interrupts();

  float frequency = (1000.0*(localPulseCount - previousPulseCount))/(currentTime - previousTime);

  previousTime = currentTime;
  previousPulseCount = localPulseCount;

  Serial.println(localPulseCount);
  Serial.println(frequency);

// NOTE: I never reset pulseCount, which is the variable controlled by the ISR, the main loop just takes snapshots of it and I eventually use them to reset the value of the passages
// counter
}

void setup() {
  pinMode(PHOTOC_PIN, INPUT);
  pinMode(STEP_PIN, OUTPUT);

  stepPWM.begin(2*STEPS_PER_REVOLUTION, duty_cycle);

  attachInterrupt(digitalPinToInterrupt(PHOTOC_PIN), photocellsPulse, RISING); // the actual ISR, I have to specify the PIN, the event and finally the function to call when the event
                                                                                // happens. Is role is basically to configure the hardware so that a rising edge on the phs pin triggers
                                                                                // photocellsPulse()
  Serial.begin(115200);
}

void loop() {
  frequencyMeter();
}
