# Arduino Vibrational Test

Arduino-based control and data-acquisition system for a vibrational test on a two-floor building model

## Hardware

- Arduino UNO R4 Minima
- Arduino Nano esp32
- NEMA 17 stepper motor
- A4988 stepper motor driver
- Optical photocells
- MPU6050 accelerometer

## Project structure
- `Arduino_UNO_R4/` — The parent folder containing all the Arduino-UNO-R4 based software 
	- `stepper_motor/` — Stepper motor control and state-machine development.
	- `photocells/` — Optical measurement of the motor rotational frequency.
	- `stepper_photocells/` — Integrated motor controller and photocell frequency measurement.
	- `accelerometer/` — MPU6050 acceleration acquisition.
	- `R4_final_firmware` — The entire firmware with all the componets above merged in one unique software 

- `nano_esp32` — The folder containing all the Arduino-nano-esp32 based software
	- `firmware` — The complete firmware containing both stepper motor state machine and accelerometer state machine and data transmission architacture.
	- `python` — The python interfaces for data acquisition and plotting.
	- `data` — To store aventual data

## Current development

The entire firmware of the project has been successfully developed in the Arduino UNO R4 enviroment, moreover a python interface for accelerometer-data live plotting has been developed too.
At the actual state the complete R4 firmware is being adappted to the nano esp32 enviroment.
The next step it to change the data communication from serial to bluetooth.
