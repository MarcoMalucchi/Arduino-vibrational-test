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
*/

const int PHOTOC_PIN = 2; // in Arudino uno r4 minima only pin D2 and D3 can be used in the interrupt mode
const int STEP_PIN = 3;

#include "pwm.h"
PwmOut stepPWM(STEP_PIN);
float duty_cycle = 50.0;
const int STEPS_PER_REVOLUTION = 400;

volatile unsigned long pulseCount = 0;  // This is a perticular type of variable which exists based onto the fact: the ISR changes the shared variables while the main program may also
                                        // read them. So these shared variable has to be declared as VOLATILE. They can change suddendly outside the normal flow of the main program and
                                        // then the CPU knows that it hasn't to optimize deleting their reads. it tells the compiler that the value may change through a mechanism it
                                        // cannot infer from the normal program flow. Therefore, when your normal code asks for pulseCount, the compiler must actually read its current
                                        // value rather than assuming an earlier value is still valid.

void photocellsPulse() {  // The function called by the ISR, it has to keep truck of the phc-signal frequency. There are 2 ways to do so: 1. coounting the transition of the signal from
                          // HIGH to LOW, 2. measuring the time between subsequent events. In this easy-version we just count them. This may become inefficient at low frequencies, and
                          // micros() function is really fast overall, so the 2 ways are basically similar in terms of computational-time efficiency
  pulseCount++;
}

void setup() {
  pinMode(PHOTOC_PIN, INPUT);
  pinMode(STEP_PIN, OUTPUT);

  stepPWM.begin(2*STEPS_PER_REVOLUTION, duty_cycle);

  attachInterrupt(digitalPinToInterrupt(PHOTOC_PIN), photocellsPulse, RISING); // the actual ISR, I have to specify the PIN, the event and finally the function to call when the event
                                                                                // happens. Is role is basically to configure the hardware so that a falling edge on the phs pin triggers
                                                                                // photocellsPulse()
  Serial.begin(115200);
}

void loop() {
  delay(1000);  // note that even if the loop (arduino) is waiting the incrementation of pulseCount can still happens, that is the foundation of ISR
  Serial.println(pulseCount);
}
