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
	- `accelerometer/` — MPU6050 acceleration acquisition, here the python live plot debugger.
	- `R4_final_firmware` — The entire firmware with all the componets above merged in one unique software 

- `nano_esp32` — The folder containing all the Arduino-nano-esp32 based software
	- `firmware` — The complete firmware containing both stepper motor state machine and accelerometer state machine and data transmission architacture.
	- `python` — The python interfaces for data acquisition and plotting.
	- `data` — To store aventual data.

## Current development

The entire firmware of the project has been successfully developed in the Arduino UNO R4 enviroment, moreover a python interface for accelerometer-data live plotting has been developed too.
The nano esp32 firmware has been developed too. The bluetooth communication has been fully implemented. The python live plot debugger has been updated to a python interface with Arduino.

### BUGS:
- Eventual shut downs of the bluetooth communication possibly ascriable to an Ubuntu incopatibility with the Bluetooth Low Energy technology
- Photocells frequencymeter not working, possibly caused by the 3.3 V logic level of the nano (since it was working with the Arduino UNO which works at 5 V)

### POSSIBLE FUTURE IMPLEMENTATIONS:
- The data-saving system, with a proper runs log, is currenctly missing. It is necessary to implement it
- Wi-Fi based communication is possibly implementable
- Add a proper distinction between sweep mode and target-frequencies mode (see below)

### A POSSIBLE CORRECT USE OF THE CURRENT SYSTEM
- The python interface talks with the nano machine, sending to it the correct commands to both control the motor and the acquisition phase. It is actually thought to fill an array with the frequencies inteded to be explored, the interface will do the rest controlling the entire machine. Once the motor is arrived AT_TARGET state the acquisition will be started after a "time-sleep" needed to avoid transients, which can be set at the top of the script togheter with the "measurement-time", declaired as global variables.
- Once the acquisition is starded a the live plot will be updated, always showing the last 10 seconds of acquisition. Any data will be saved. The plot will show both the target frequency and the measured frequency.
- The system is basically composed by a couple of state machines: the motor (which states are AT_TARGET, STOPPED, RAMPING) and the accelerometer (INITIALIZING, READY, MEASURING), the transitions between those command is possible sending to nano the right commands (all the codes of the project are abundantly commented, to understand their working. For a better comprehension is suggested to check the USB version of the firmware, which with is possible to send the commands directly though serial monitor, interactively).


