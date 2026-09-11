/*
  Minimal Bluetooth Low Energy test using the ArduinoBLE library.

  Architecture:

  Nano ESP32
      │
      └── BLE peripheral / GATT server
              │
              └── testService
                      │
                      └── commandCharacteristic
                              WRITE
                              Central -> Nano

  The goal is simply:
  1. Initialize BLE
  2. Advertise the Nano as "VibrationTest"
  3. Expose one writable characteristic
  4. Print anything written to that characteristic over Serial
*/

#include <ArduinoBLE.h>


// Create the BLE service.
// The string is the UUID identifying this particular service.
BLEService testService(
  "19B10000-E8F2-537E-4F6C-D104768A1214"
);


// Create a characteristic containing a String.
//
// BLEWrite:
//   A connected central is allowed to WRITE to this characteristic.
//
// 40:
//   Maximum length of the String, in bytes.
BLEStringCharacteristic commandCharacteristic(
  "19B10001-E8F2-537E-4F6C-D104768A1214",
  BLEWrite,
  40
);


void setup() {

  Serial.begin(115200);

  // Wait until the Serial connection is available.
  //while (!Serial);


  /*
    Initialize the BLE subsystem.

    BLE.begin() returns true if initialization succeeds
    and false if it fails.
  */
  if (!BLE.begin()) {

    Serial.println("BLE initialization failed");

    // Infinite loop: stop the program here if BLE initialization failed.
    while (1);
  }


  /*
    Set the local BLE name.

    This is the human-readable name that a scanner may display.
  */
  BLE.setLocalName("VibrationTest");


  /*
    Tell the advertising system that testService is the service
    we want to advertise.
  */
  BLE.setAdvertisedService(testService);


  /*
    Add our command characteristic to the service.

    Structure after this operation:

    testService
        │
        └── commandCharacteristic
  */
  testService.addCharacteristic(commandCharacteristic);


  /*
    Add the completed service to the local GATT server.
  */
  BLE.addService(testService);


  /*
    Start BLE advertising.

    Nearby BLE central devices can now discover the Nano
    and potentially connect to it.
  */
  BLE.advertise();


  Serial.println("BLE active");
  Serial.println("Waiting for connection...");
}


void loop() {

  /*
    BLE.poll() gives the ArduinoBLE library an opportunity
    to process BLE events.
  */
  BLE.poll();


  /*
    written() becomes true when a connected central has written
    a new value into commandCharacteristic.
  */
  if (commandCharacteristic.written()) {

    /*
      Retrieve the String that the central wrote.
    */
    String command = commandCharacteristic.value();


    Serial.print("Received over BLE: ");
    Serial.println(command);
  }
}