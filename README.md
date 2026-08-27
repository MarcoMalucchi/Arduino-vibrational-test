# Arduino Virbational Test

Arduino-based control and data-acquisition system for a vibrational test on a two-floor building model

## Hardware

- Arduino Uno R4 Minima
- NEMA 17 stepper motor
- A4988 stepper motor driver
- Optical photocells
- MPU6050 accelerometer

## Project structure

- `stepper_motor/` — Stepper motor control and state-machine development.
- `photocells/` — Optical measurement of the motor rotational frequency.
- `stepper_photocells/` — Integrated motor controller and photocell frequency measurement.
- `accelerometer/` — MPU6050 acceleration acquisition.

## Current development

The motor controller and photocell frequency measurement have been integrated. Development is now moving toward MPU6050 acceleration acquisition and, later, a Python interface for control, live visualization, and data acquisition.
