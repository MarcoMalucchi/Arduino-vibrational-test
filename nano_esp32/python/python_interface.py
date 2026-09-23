import struct
from collections import deque
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import threading
from bleak import BleakClient
import asyncio


NANO_ADDRESS = "E4:B0:63:AD:4F:E5"
COMMAND_CHARACTERISTIC_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"
DATA_CHARACTERISTIC_UUID = "19B10002-E8F2-537E-4F6C-D104768A1214"

PACKET_LAYOUT = "<BhhhI"
PACKET_SIZE = struct.calcsize(PACKET_LAYOUT)

ACC_SENSITIVITY = 16384.0
SAMPLING_RATE = 200

WINDOW_SECONDS = 5
BUFFER_SIZE = SAMPLING_RATE * WINDOW_SECONDS
PLOT_INTERVAL_MS = 50

FREQUENCIES = [0.5, 1.0, 1.5, 2.0]

settling_time = 5
acquisition_time = 10

first_timestamp = None
previous_status = None

target_frequency = None
measured_frequency = None

motor_state = None
acc_state = None

time_buffer = deque(maxlen=BUFFER_SIZE)
ax_buffer = deque(maxlen=BUFFER_SIZE)
ay_buffer = deque(maxlen=BUFFER_SIZE)
az_buffer = deque(maxlen=BUFFER_SIZE)

at_target_event = asyncio.Event()
ready_event = asyncio.Event()
measuring_event = asyncio.Event()
state_received_event = asyncio.Event()

data_lock = threading.Lock()
stop_event = threading.Event()
experiment_finished_event = threading.Event()


fig, axes = plt.subplots(
    3,
    1,
    figsize=(12, 8),
    dpi=100,
    sharex=True,
    height_ratios=[1, 1, 1]
)

line1, = axes[0].plot([], [], color="blue", label="X")
line2, = axes[1].plot([], [], color="green", label="Y")
line3, = axes[2].plot([], [], color="red", label="Z")

fig.suptitle("MPU6050 live acceleration")
axes[2].set_xlabel("Time [s]")

labels = ["$a_x$ [g]", "$a_y$ [g]", "$a_z$ [g]"]

for axis, label in zip(axes, labels):
    axis.set_ylabel(label)
    axis.legend(loc="upper right")
    axis.grid(True)

def decode_packet(data):

    status, ax, ay, az, timestamp = struct.unpack(PACKET_LAYOUT, data)

    ax /= ACC_SENSITIVITY
    ay /= ACC_SENSITIVITY
    az /= ACC_SENSITIVITY

    return status, ax, ay, az, timestamp


def acquire_data(characteristic, data):

    global first_timestamp, previous_status
    # previous_timestamp = 0
    # count = 0

    #print(f"DATA notification: {len(data)} bytes")

    if len(data) != PACKET_SIZE:
        print(f"Invalid packet size: {len(data)} bytes")
        return

    decoded = decode_packet(data)

    new_status, ax, ay, az, timestamp = decoded

    if first_timestamp is None:
        first_timestamp = timestamp

    time_s = (timestamp - first_timestamp) / 1000000.0

    if new_status != 0:

        if previous_status is None:

            if new_status == 1:
                print(
                    "[MPU6050] I2C communication unavailable at startup: "
                    "I2C_TX_ERROR"
                )

            elif new_status == 2:
                print(
                    "[MPU6050] I2C communication unavailable at startup: "
                    "I2C_READ_ERROR"
                )

        elif previous_status == 0:

            if new_status == 1:
                print(
                    f"[MPU6050] I2C communication LOST, "
                    f"I2C_TX_ERROR: failed while setting "
                    f"ACCEL_XOUT_H register pointer, at {time_s:.2f}s"
                )

            elif new_status == 2:
                print(
                    f"[MPU6050] I2C communication LOST, "
                    f"I2C_READ_ERROR: failed while requesting "
                    f"the six acceleration bytes, at {time_s:.2f}s"
                )

        previous_status = new_status
        return

    if previous_status in (1, 2):
        print(
            f"[MPU6050] I2C communication RECOVERED, "
            f"at {time_s:.2f}s"
        )

    previous_status = new_status

    with data_lock:
        time_buffer.append(time_s)
        ax_buffer.append(ax)
        ay_buffer.append(ay)
        az_buffer.append(az)

def control_callback(characteristic, data):

    global motor_state, acc_state
    global measured_frequency

    message = data.decode()

    # Show every state message received from Nano.
    # Useful for debugging the state machine.
    print(f"[NANO] {message}")

    if message.startswith("FREQ_MEASURED:"):
        value_text = message.split(":")[1]
        measured_frequency = float(value_text)
        return

    # Ignore notifications that aren't state messages.
    if not message.startswith("MOTOR:"):
        return

    motor_part, acc_part = message.split(";")
    motor_label, motor_state = motor_part.split(":")
    acc_label, acc_state = acc_part.split(":")

    # --------------------------------
    # UPDATE SYNCHRONIZATION EVENTS
    # --------------------------------

    if motor_state == "AT_TARGET":
        at_target_event.set()
    else:
        at_target_event.clear()

    if acc_state == "READY":
        ready_event.set()
    else:
        ready_event.clear()

    if acc_state == "MEASURING":
        measuring_event.set()
    else:
        measuring_event.clear()

    # A complete state notification has been received.
    state_received_event.set()

async def wait_or_stop(awaitable):

    operation_task = asyncio.create_task(awaitable)

    stop_task = asyncio.create_task(
        asyncio.to_thread(stop_event.wait)
    )

    done, pending = await asyncio.wait(
        [operation_task, stop_task],
        return_when=asyncio.FIRST_COMPLETED
    )

    for task in pending:
        task.cancel()

    if stop_task in done:
        raise asyncio.CancelledError

    return await operation_task

async def run_experiment():

    global first_timestamp, previous_status
    global motor_state, acc_state
    global target_frequency, measured_frequency

    client = BleakClient(NANO_ADDRESS)

    try:
        # --------------------------------
        # BLE CONNECTION
        # --------------------------------

        await client.connect()

        if client.is_connected:
            print("Nano connected")

        # characteristic = client.services.get_characteristic(
        #     COMMAND_CHARACTERISTIC_UUID
        # )

        # print(
        #     "Command characteristic properties:",
        #     characteristic.properties
        # )

        # Subscribe before sending commands so that
        # immediate Nano responses cannot be missed.
        await client.start_notify(COMMAND_CHARACTERISTIC_UUID, control_callback)

        await client.start_notify(DATA_CHARACTERISTIC_UUID, acquire_data)


        # --------------------------------
        # INITIAL SYNCHRONIZATION
        # --------------------------------

        state_received_event.clear()

        await client.write_gatt_char(COMMAND_CHARACTERISTIC_UUID, b"STATE?\n")

        await wait_or_stop(asyncio.wait_for(state_received_event.wait(), timeout=10.0))

        print(
            f"Initial state: "
            f"MOTOR={motor_state}, ACC={acc_state}"
        )


        # --------------------------------
        # FREQUENCY SWEEP
        # --------------------------------

        for frequency in FREQUENCIES:

            measured_frequency = None

            if stop_event.is_set():
                break

            print(f"Setting frequency to {frequency} Hz")

            # The previous AT_TARGET state must not satisfy
            # the wait for the new target frequency.
            at_target_event.clear()
            state_received_event.clear()

            command = f"FREQ {frequency}\n"

            await client.write_gatt_char(COMMAND_CHARACTERISTIC_UUID, command.encode())

            # Wait for Nano's response to FREQ before inspecting motor_state.
            await wait_or_stop(asyncio.wait_for(state_received_event.wait(), timeout=10.0))

            # FREQ changes the target but does not start a
            # motor which is currently stopped.
            #
            # If the motor was already running, FREQ itself
            # puts it into RAMPING and GO must NOT be sent.
            if motor_state == "STOPPED":

                await client.write_gatt_char(COMMAND_CHARACTERISTIC_UUID, b"GO\n")

            # Continue only after Nano reports AT_TARGET.
            await wait_or_stop(asyncio.wait_for(at_target_event.wait(), timeout=100.0))

            print(f"Motor reached {frequency} Hz")

            target_frequency = frequency


            # --------------------------------
            # SETTLING
            # --------------------------------

            await wait_or_stop(asyncio.sleep(settling_time))

            if stop_event.is_set():
                break


            # --------------------------------
            # START ACQUISITION
            # --------------------------------

            # Each acquisition gets its own t = 0 and
            # independent I2C diagnostic history.
            first_timestamp = None
            previous_status = None

            with data_lock:
                time_buffer.clear()
                ax_buffer.clear()
                ay_buffer.clear()
                az_buffer.clear()

            measuring_event.clear()

            await client.write_gatt_char(COMMAND_CHARACTERISTIC_UUID, b"MEASURE\n")

            # Continue only after Nano reports
            # ACC:MEASURING.
            await wait_or_stop(asyncio.wait_for(measuring_event.wait(), timeout=10.0))

            print("Measurement started")


            # Data notifications are automatically handled
            # by acquire_data() during this period.
            await wait_or_stop(asyncio.sleep(acquisition_time))


            # --------------------------------
            # STOP ACQUISITION
            # --------------------------------

            ready_event.clear()

            await client.write_gatt_char(COMMAND_CHARACTERISTIC_UUID, b"STOP_MEASURE\n")

            # Continue only after Nano reports ACC:READY.
            await wait_or_stop(asyncio.wait_for(ready_event.wait(), timeout=10.0))

            print("Measurement stopped")

    except asyncio.CancelledError:
        print("Experiment interrupted by user")

    
    # --------------------------------
    # END OF EXPERIMENT
    # --------------------------------
    
    finally:

        if client.is_connected:
            try:
                print("Sending final STOP...")
                await client.write_gatt_char(
                    COMMAND_CHARACTERISTIC_UUID,
                    b"STOP\n"
                )
                print("Final STOP sent")
            except Exception as error:
                print(f"FINAL STOP FAILED: {error}")

            await client.disconnect()

        experiment_finished_event.set()

        print("Nano disconnected")

def update_plot(frame):

    if target_frequency is not None:
        if measured_frequency is None:
            fig.suptitle(
                f"Target frequency: {target_frequency:.4f} Hz   |   "
                f"Measured frequency: --- Hz"
            )
        else:
            fig.suptitle(
                f"Target frequency: {target_frequency:.4f} Hz   |   "
                f"Measured frequency: {measured_frequency:.4f} Hz"
            )

    with data_lock:
        times = list(time_buffer)
        xs = list(ax_buffer)
        ys = list(ay_buffer)
        zs = list(az_buffer)

    if not times:
        return line1, line2, line3

    line1.set_data(times, xs)
    line2.set_data(times, ys)
    line3.set_data(times, zs)

    if len(times) > 1:
        axes[2].set_xlim(times[0], times[-1])

    for axis in axes:
        axis.relim()
        axis.autoscale_view(scalex=False, scaley=True)

    return line1, line2, line3

def check_experiment_finished():
    if experiment_finished_event.is_set():
        plt.close(fig)


def main():

    experiment_thread = threading.Thread(
        target=lambda: asyncio.run(run_experiment()),
        daemon=True
    )

    experiment_thread.start()

    print("Experiment thread started", "\n")

    animation = FuncAnimation(
        fig,
        update_plot,
        interval=PLOT_INTERVAL_MS,
        cache_frame_data=False
    )

    shutdown_timer = fig.canvas.new_timer(interval=100)
    shutdown_timer.add_callback(check_experiment_finished)
    shutdown_timer.start()

    try:
        plt.show()

    finally:

        stop_event.set()
        experiment_thread.join()

        print("Experiment finished")

if __name__ == "__main__":
    main()