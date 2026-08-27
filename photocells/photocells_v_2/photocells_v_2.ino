/*
Here we operate onto the photocells. This first version is just a set-up of the device, to understand how it works.
Actually, using the oscilloscope, we established that the signal from the photocells goes LOW at every passage and it stays HIGH when the optical path is interrupted,
the peak-to-peak amplitude is about 5 V and the frequency seems to be really precise (the frequency measured by the oscilloscope of the phc signal differ from that setedù
by the user trough serial by 1 part in 10 to the fourth, which is remarkable).

In this sketch we already try an "advance" design which makes use of ISR (Interrupt Service Routine). The key idea is that we create a sort of function which acts only when a precise 
event occurs (obviously I decide the event, which, in this case, will be the falling edge of the photocells signal). So once the ISR is triggered by the event, a function related to it
is immediatly execute by the CPU (note that this function has to perform as few operations as possible). What we obtain from this design? When save time for the CPU to perform the 
routine operations.

Take-away message: "Nothing concerning the interrupt needs to be repeatedly executed inside loop()"

WHAT'S CHENGED: instead of counting the passages, now we measure the time passed between two subsequent passages
*/

const int PHOTOC_PIN = 2; // in Arudino uno r4 minima only pin D2 and D3 can be used in the interrupt mode
const int STEP_PIN = 3;

#include "pwm.h"
PwmOut stepPWM(STEP_PIN);
float duty_cycle = 50.0;
const int STEPS_PER_REVOLUTION = 400;

volatile unsigned long passageCount = 0;
volatile unsigned long lastPassage = 0.0;
volatile unsigned long deltaTime = 0.0;

float frequency;

void photocellsPulse() {  // The function called by the ISR, now we try to make it measure the time passed between two subsequent passages

  unsigned long currentTime = micros(); // As soon as the ISR starts, take one snapshot of the event time

  if (passageCount == 0) {
    passageCount++;
    lastPassage = currentTime;
    return;
  }

  deltaTime = currentTime - lastPassage;

  lastPassage = currentTime;
  passageCount++;
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
  delay(1000);  // note that even if the loop (arduino) is waiting the incrementation of pulseCount can still happens, that is the foundation of ISR
  Serial.println(passageCount);
  if (passageCount >= 2) {
    frequency = 1000000.0/deltaTime;  // here we have data shared asynchronously between the ISR and the loop, this is something that must be fixed
                                      // We have to be sure that the loop actually reads a consistent photocells period while the ISR may update asyncrounsly. What may happen is that
                                      // loop() may read a value which is partly old and partly new, if during its reading ISR suddendly updates --> RACE CONDITION
    Serial.println(frequency);
  }
}
