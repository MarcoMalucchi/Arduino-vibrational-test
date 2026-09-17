/*
  Minimal Bluetooth Low Energy bidirectional communication test
  using the ArduinoBLE library.

  Architecture:

  Nano ESP32
      │
      └── BLE peripheral / GATT server
              │
              └── testService
                      │
                      ├── commandCharacteristic
                      │       WRITE
                      │       Central -> Nano
                      │
                      └── dataCharacteristic
                              NOTIFY
                              Nano -> Central

  The goal of this test is:
  1. Initialize BLE.
  2. Advertise the Nano as "VibrationTest".
  3. Expose one writable characteristic for commands sent
     from the central to the Nano.
  4. Expose one notification characteristic for data sent
     from the Nano to the central.
  5. Print over Serial anything written by the central into
     commandCharacteristic.
  6. When a command is received, write "Hello from Nano" into
     dataCharacteristic so that a subscribed central receives
     the value through a BLE notification.

*/

#include <ArduinoBLE.h>


// Create the BLE service.
//
// The string is the UUID identifying this particular service.
// The service acts as a container grouping the characteristics used
// by our application.
BLEService testService(
  "19B10000-E8F2-537E-4F6C-D104768A1214"
);


// Create the characteristic used to receive commands from the central.
//
// BLEWrite:
//   A connected central is allowed to WRITE to this characteristic.
//
// 40:
//   Maximum length of the String, in bytes.
//
// Communication direction:
//   Central -> Nano
BLEStringCharacteristic commandCharacteristic(
  "19B10001-E8F2-537E-4F6C-D104768A1214",
  BLEWrite,
  40
);


// Create the characteristic used to send data from the Nano to the central.
//
// BLENotify:
//   A connected central can subscribe to notifications from this
//   characteristic. Once subscribed, the Nano can send a new value to
//   the central without requiring the central to continuously poll it.
//
// 40:
//   Maximum length of the String, in bytes.
//
// Communication direction:
//   Nano -> Central
//
// For this first experiment the characteristic contains Strings.
// Later, the same communication mechanism will be adapted to transmit
// the binary MPU6050 measurement packets.
BLEStringCharacteristic dataCharacteristic(
  "19B10002-E8F2-537E-4F6C-D104768A1214",
  BLENotify,
  40
);


void setup() {

  Serial.begin(115200);

  // Do NOT wait for Serial here.
  //
  // The Nano must also be able to initialize BLE when powered without
  // an active USB serial connection.
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

  BLE.setLocalName("VibrationTest");  // Set the local BLE name: this is the human-readable name that a scanner may display.

  BLE.setAdvertisedService(testService);  // Tell the advertising system that testService is the service
                                          // we want to advertise.


  /*
    Add both application characteristics to testService.

    Structure after these operations:

    testService
        │
        ├── commandCharacteristic
        │       UUID: ...0001...
        │       Property: WRITE
        │       Direction: Central -> Nano
        │
        └── dataCharacteristic
                UUID: ...0002...
                Property: NOTIFY
                Direction: Nano -> Central

    Notice that creating a BLE characteristic object is not enough
    to expose it through the GATT server: the characteristic must
    also be associated with a service.
  */
  testService.addCharacteristic(commandCharacteristic);
  testService.addCharacteristic(dataCharacteristic);

  BLE.addService(testService);  // Add the completed service to the local GATT server.
                                // At this point testService and both of its characteristics become
                                // part of the GATT database exposed by the Nano.

  BLE.advertise();  // Start BLE advertising.
                    // Nearby BLE central devices can now discover the Nano
                    // and potentially connect to it.

  Serial.println("BLE active");
  Serial.println("Waiting for connection...");
}


void loop() {

  /*
    BLE.poll() gives the ArduinoBLE library an opportunity
    to process BLE events.

    These events include, for example, connections, disconnections,
    characteristic writes and other BLE protocol activity.
  */
  BLE.poll();


  if (commandCharacteristic.written()) {  // written() becomes true when a connected central has written
                                          // a new value into commandCharacteristic.

    String command = commandCharacteristic.value(); // Retrieve the String that the central wrote.

    Serial.print("Received over BLE: ");
    Serial.println(command);

    dataCharacteristic.writeValue("Hello from Nano");
  }

}
/*
  When a command is received, the Nano updates dataCharacteristic with
  the test message "Hello from Nano".

  Since dataCharacteristic has the BLENotify property, a connected
  central that has subscribed to this characteristic can receive the
  updated value as a BLE notification.

  For this first test the transmitted value is a String. Later this
  mechanism will be used to transmit the binary MPU6050 data packets.
*/
