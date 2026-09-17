'''
Minimal Bluetooth Low Energy bidirectional communication test using Bleak.

Architecture:

Ubuntu laptop
    │
    └── BLE central / GATT client
            │
            ├── BleakScanner
            │       └── discovers nearby BLE peripherals
            │
            └── BleakClient
                    │
                    ├── connects to the Nano ESP32
                    │
                    ├── WRITE characteristic
                    │       Python -> Nano
                    │
                    └── NOTIFY characteristic
                            Nano -> Python

The goal of this test is:
1. Scan for nearby BLE peripherals and print the discovered devices.
2. Create a BleakClient associated with the known Nano ESP32 address.
3. Establish and verify the BLE connection.
4. Inspect the GATT services and characteristics exposed by the Nano.
5. Write the text command "GO" to commandCharacteristic.
6. Subscribe to notifications from dataCharacteristic.
7. Keep the asyncio event loop available for 10 seconds so that incoming
   notifications can be received and processed.
8. Disconnect cleanly.

The two custom characteristics implement the two communication directions:

    commandCharacteristic:
        WRITE
        Python -> Nano

    dataCharacteristic:
        NOTIFY
        Nano -> Python

BLE transports bytes rather than Python strings. Commands are therefore
encoded before transmission, while incoming text notifications are decoded
back into Python strings by the notification callback.

At this stage notifications will contain simple text for testing.
Later, dataCharacteristic will transport the binary MPU6050 packets instead.
'''


# BleakScanner provides BLE device-discovery functionality.
# BleakClient provides the interface used to establish a connection and interact with the GATT server exposed by a BLE peripheral.
from bleak import BleakScanner, BleakClient

# asyncio provides Python's asynchronous event-loop infrastructure.
import asyncio


# BLE address identifying the Nano ESP32 peripheral.
NANO_ADDRESS = "E4:B0:63:AD:4F:E5"

# UUID of the characteristic used by Python to WRITE commands to the Nano.
COMMAND_CHARACTERISTIC_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"

# UUID of the characteristic used by the Nano to NOTIFY Python when new data is available.
DATA_CHARACTERISTIC_UUID = "19B10002-E8F2-537E-4F6C-D104768A1214"


# Callback function executed by Bleak whenever a notification is received
# from dataCharacteristic.
#
# We do NOT call this function directly. Instead, the function itself is
# passed to Bleak through start_notify(). Bleak will call it whenever the
# subscribed characteristic generates a notification.
#
# characteristic:
#   Identifies the GATT characteristic that generated the notification.
#
# data:
#   Contains the payload received through BLE as a bytearray.
#
# For this first test the Nano will transmit text, so decode() converts the
# received bytes into a Python String representation.
#
# Later, when data contains the 13-byte binary MPU6050 packet, it must NOT
# be decoded as text. It will instead be interpreted using struct.unpack().
def notification_handler(characteristic, data):

    print("Received:", data.decode())


# "async def" defines main() as a coroutine function.
#
# Unlike a normal function, a coroutine can temporarily suspend its
# execution when it reaches an asynchronous operation that has to wait,
# giving control back to the asyncio event loop.
async def main():

    # Start BLE discovery and wait for the scan to complete.
    # discover() is asynchronous because BLE discovery requires waiting for advertisements transmitted by nearby peripherals.
    # "await" suspends main() while the operation is waiting and gives control back to the asyncio event loop.
    devices = await BleakScanner.discover()


    # Once discover() has completed, "devices" contains the BLE peripherals found during the scan.
    # Iterating over this collection is ordinary synchronous Python.
    for device in devices:

        print(device)


    # Create a BleakClient object associated with the Nano's BLE address.
    # This only creates the Python client object. It does NOT establish the physical/logical BLE connection yet.
    client = BleakClient(NANO_ADDRESS)


    # Establish the actual BLE connection.
    # Connecting requires communication with an external device and is therefore an asynchronous operation.
    await client.connect()


    # is_connected is a property representing the current connection state.
    if client.is_connected:

        print("Nano connected")


    # Runtime command that will be transmitted to the Nano.
    command = "GO"


    # Inspect the GATT database discovered through the connected Nano.
    # Each service may contain one or more characteristics. The properties of each characteristic describe the operations supported by it, such as WRITE or NOTIFY.
    for service in client.services:

        print("Service:", service.uuid)

        for characteristic in service.characteristics:

            print("     Characteristic:", characteristic.uuid)
            print("          Properties:", characteristic.properties)


    # Subscribe to notifications generated by dataCharacteristic.
    #
    # The second argument is the callback FUNCTION itself, not a call to it:
    #
    #     notification_handler      -->     give the function to Bleak
    #     notification_handler()    -->     execute the function immediately
    #
    # After this operation, Bleak knows that whenever a notification arrives from DATA_CHARACTERISTIC_UUID it must invoke notification_handler(), provide
    # the characteristic and received data as arguments.
    await client.start_notify(DATA_CHARACTERISTIC_UUID, notification_handler)

    # Send the command to commandCharacteristic.
    # command is a Python str, while BLE transports bytes. encode() therefore converts the text into its byte representation before Bleak sends it through the GATT
    # characteristic.
    await client.write_gatt_char(COMMAND_CHARACTERISTIC_UUID, command.encode())


    # Keep main() suspended for 10 seconds while leaving the asyncio event loop active.
    #
    # During this period main() has no work to perform, but the event loop remains able to process BLE events. If the Nano generates a notification,
    # Bleak can therefore receive it and execute notification_handler().
    # This is different from simply thinking of the whole Python program as "stopped for 10 seconds": only this coroutine is deliberately waiting.
    await asyncio.sleep(10)

    # Disconnect cleanly from the Nano once the test period has finished.
    await client.disconnect()


# Start Python's asyncio event loop and execute the main() coroutine.
# Calling main() alone would only create a coroutine object. asyncio.run() creates/manages the event loop, runs main() until it finishes, and then closes the
# event loop.
asyncio.run(main())


'''
HOW THIS SCRIPT ACTUALLY OPERATES

The program now contains both synchronous operations and asynchronous operations coordinated by the asyncio event loop.

Conceptually:

    main()
        |
        |-- start BleakScanner.discover()
        |
        |-- await --------------------------------------+
        |                                               |
        |      main() is suspended while BLE            |
        |      advertisements are collected             |
        |                                               |
        |                         scan completes --------+
        |
        |-- print discovered devices
        |
        |-- create BleakClient
        |
        |-- await client.connect()
        |       |
        |       +-- establish BLE connection
        |
        |-- inspect GATT services and characteristics
        |
        |-- encode "GO" into bytes
        |
        |-- await client.write_gatt_char()
        |       |
        |       +-- Python -------- "GO" --------> Nano
        |
        |-- await client.start_notify()
        |       |
        |       +-- subscribe to dataCharacteristic
        |       +-- register notification_handler as callback
        |
        |-- await asyncio.sleep(10) --------------------+
        |                                               |
        |      main() is suspended                      |
        |                                               |
        |      asyncio event loop remains active        |
        |                                               |
        |            BLE notification arrives           |
        |                       |                       |
        |                       v                       |
        |            notification_handler()             |
        |                       |                       |
        |                       v                       |
        |                decode and print               |
        |                                               |
        |                    10 s elapsed ---------------+
        |
        |-- await client.disconnect()
        |
        +-- main() terminates


The notification mechanism is EVENT-DRIVEN.

Python does not repeatedly ask the Nano whether new data exists. Instead, Python subscribes once to dataCharacteristic. When the Nano produces new
data, the BLE system generates an event and Bleak invokes the callback function associated with that characteristic.

The important relationship is therefore:

    Nano
      |
      | new data
      v
    dataCharacteristic
      |
      | BLE notification
      v
    Bleak / asyncio event handling
      |
      | callback invocation
      v
    notification_handler(characteristic, data)
      |
      | data.decode()
      v
    Python String


This is also a concrete example of asyncio concurrency.

While main() is suspended inside asyncio.sleep(10), the event loop is not forced to remain inactive. It can react to BLE events and cause the notification
callback to execute.

Later, the same event-driven architecture will be used for measurement data. The important difference will be the interpretation of the received bytes:

    CURRENT TEST:

        BLE bytes -> data.decode() -> text


    FINAL MPU DATA:

        BLE bytes -> binary packet decoding -> status, ax, ay, az, timestamp

The transport mechanism and notification concept remain essentially the same; only the interpretation and handling of the received payload changes.
'''