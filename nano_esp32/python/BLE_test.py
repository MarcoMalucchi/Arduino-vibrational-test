'''
Minimal Bluetooth Low Energy scanning test using the Bleak library.

Architecture:

Ubuntu laptop
    │
    └── BLE central / GATT client
            │
            └── BleakScanner
                    │
                    └── searches for nearby BLE peripherals

The goal of this first test is simply:
1. Start a BLE scan from Python.
2. Wait for nearby BLE advertisements to be discovered.
3. Store the discovered devices.
4. Print every discovered BLE device.

This script also introduces asynchronous programming through asyncio.
BLE operations take time because the program has to wait for events
coming from external devices. Instead of blocking the entire workflow
while waiting, an asynchronous operation can yield control to the
asyncio event loop and continue when its result becomes available.
'''


# BleakScanner provides the BLE scanning functionality.
from bleak import BleakScanner

# asyncio provides Python's asynchronous event-loop infrastructure.
import asyncio


# "async def" defines main() as a coroutine function.
#
# Unlike a normal function, a coroutine can temporarily suspend its
# execution when it reaches an asynchronous operation that has to wait.
async def main():

    # Start BLE discovery and wait for the scan to complete.
    #
    # discover() is an asynchronous operation, therefore we use "await".
    # While this operation is waiting for BLE advertisements, the coroutine
    # can yield control to the asyncio event loop.
    #
    # Once discovery is complete, the returned collection of BLE devices
    # is stored in "devices".
    devices = await BleakScanner.discover()


    # From this point we are using ordinary synchronous Python again.
    #
    # Iterate over every BLE device returned by the scanner and print
    # the information Bleak associates with that device.
    for device in devices:

        print(device)


# Start Python's asyncio event loop and execute the main() coroutine.
#
# Calling main() alone would only create a coroutine object.
# asyncio.run() creates/manages the event loop, runs main() until it
# finishes, and then closes the event loop.
asyncio.run(main())


'''
HOW THIS SCRIPT ACTUALLY OPERATES

When main() reaches:

    devices = await BleakScanner.discover()

Bleak starts the BLE discovery operation and the main() coroutine is
suspended while it waits for the scan to complete.

The keyword "await" does NOT automatically mean that Python starts executing
something else in parallel. Instead, main() yields control to the asyncio
event loop because it cannot continue until discover() returns its result.

The event loop can then execute other asynchronous tasks that are ready to
run. In this simple program, however, we have created no other application
tasks: main() is our only coroutine. Therefore, while BLE discovery is in
progress, there is essentially no other application code to execute.

Nevertheless, the BLE discovery itself is still taking place outside the
suspended main() coroutine. The Bluetooth hardware and the Linux Bluetooth
stack receive BLE advertisements, BlueZ processes them, and Bleak interacts
with BlueZ through D-Bus. The asyncio event loop remains active while waiting
for the events associated with this operation.

Conceptually:

    main()
      |
      |-- start BleakScanner.discover()
      |
      |-- await --------------------------------------+
      |                                               |
      |      main() is suspended                      |
      |                                               |
      |      Bluetooth hardware / Linux / BlueZ       |
      |      perform BLE discovery while asyncio      |
      |      waits for the required events            |
      |                                               |
      |                         scan completes --------+
      |
      |-- main() resumes
      |
      |-- devices receives the discovery result
      |
      |-- print every discovered device
      |
      +-- main() terminates

The advantage of asyncio becomes more evident when several asynchronous
tasks exist. If one coroutine is waiting for a BLE event, the event loop can
execute another ready coroutine instead of forcing the whole application to
wait. This is CONCURRENCY: different tasks can make progress during the same
period of time, although they are not necessarily executing simultaneously
on different CPU cores as they would in true parallel execution.
'''