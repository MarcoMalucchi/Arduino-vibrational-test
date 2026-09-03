'''
LOGIC OF THE SCRIPT.
This script has the goal to create a live plot of the data acquired by the MPU6050, it may be useful for debugging.
How it does this? Firstly let's inspect what it has to do, secondly in which order it has to do it.

1. Read the data from the serial port
2. Decode the data
3. Update the plot

These are the main actions of the script. In which order it performs them? Here comes the intresting part.
If we would write a basic script, it would have performs its actions sequentially as reported above and repeates them until we stop it.

--> The problem is that "Read the data from the serial port" and "Update the plot" have different timing requirements. Nontheless both
	handle the same variables, i.e. the data buffers.
	So, it is important to syncronize them so that they would not accidentally overwrite the same variable desyncronizing the serial
	acquisition of data and the subsequent plot.
	(We don't want to upload the new timestamp to the plot and then attach to it a previous or subsequent acceleration value)

So how we can solve this problem? Making the two actions (execution workflow) to work concurrently --> using THREADS.
The trheading library gives a way to identify different operations perform by the same script and to decide when to start them and to 
control their access to eventually shared-between-several-threads variables.

So the global workflow of the script (considering that serial acquisition has to be updated at 200 Hz and plot refreshes each 20 Hz)
becomes divided into two seprated workflows or threads:

1. MAIN THREAD (which handle the plot updating)
	- Matplotlib GUI
	- Update plot
	- Update plot
	...

2. ACQUISITION THREAD (which handle the serial data acquisition)
	- Runs acquire_data()
	- Which in turns perform ser.read() - data-decoding - append-to-buffer

They run CONCURRENTLY

'''




import serial
import struct
from collections import deque	# it means "double-ended queue". It can have a fixed maximum lenght.
								# When it is full, appending on one end automatically discards an element from the opposite end.
								# That's one reason it is efficient for rolling buffers.
								# This way I can always display the latest "maxlen" data recorded
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation	# To animate the live plot
import threading	# To run both live plot and serial communication in parallel without conflicts

######################
# --- CONSTANTS. ---
######################
PORT = "/dev/ttyACM0"
BAUD_RATE = 115200

PACKET_LAYOUT = "<HBhhhI"
PACKET_SIZE = struct.calcsize(PACKET_LAYOUT)
PACKET_HEADER = 0xAAAA
HEADER_BYTES = struct.pack("<H", PACKET_HEADER)
HEADER_BYTE_1 = HEADER_BYTES[0:1]
HEADER_BYTE_2 = HEADER_BYTES[1:2]
# the layout of the struct used in the Arduino firmware
# H --> unsigned 16-bit integer --> header
# B --> unsigned 8-bit integer --> status (I2C communication status - diagnosis message)
# h --> signed 16-bit integer --> ax/y/z
# I --> unsigned 32-bit integer --> timesample
#NOTE: capital stands for unsigned viceversa lowercase stands for signed. Then h or H is related to shorts (16-bit integers) while I to ints (or 32-bit integers).
# The global size of our packet is PACKET_SIZE byte
# Finally '<' stands for LITTLE ENDIAN that is Arduino sends before the least significant byte and then the most significant one.

ACC_SENSITIVITY = 16384.0	#counts / g

SAMPLING_RATE = 200
WINDOW_SECONDS = 5
BUFFER_SIZE = SAMPLING_RATE * WINDOW_SECONDS	# Maximum lenght of the deque array. The goal is to plot the last 5 secons of acquired data.
												# They are recorded at 200 Hz, that means 200 * 5 = 1000 total data points.

PLOT_INTERVAL_MS = 50	# this is the interval at which the plot is updated. It is in milliseconds. It means 1000/50 = 20 updates per second


##########################
# --- INITIALIZATION. ---
##########################

time_buffer = deque(maxlen=BUFFER_SIZE)	# declaration of the deque buffer arrays
ax_buffer = deque(maxlen=BUFFER_SIZE)
ay_buffer = deque(maxlen=BUFFER_SIZE)
az_buffer = deque(maxlen=BUFFER_SIZE)

data_lock = threading.Lock()	# Only one tread at the time can access the protected block: we know that our two threads will have to work
								# onto the same variables, we want that they do it without entering in conflit. So while one is updating
								# the variables values the other waits seeing data_lock "taken" by the other

stop_event = threading.Event()	# A thread-safe flag, the main program can set it when we want the acquisition loop to stop


fig, axes = plt.subplots(3, 1, figsize=(12, 8), dpi=100, sharex=True, height_ratios=[1, 1, 1])	# Plot inizialization

line1, = axes[0].plot([], [], color="blue", label="X")
line2, = axes[1].plot([], [], color="green", label="Y")
line3, = axes[2].plot([], [], color="red", label="Z")

fig.suptitle("MPU6050 live acceleration")
axes[2].set_xlabel("Time [s]")

labels = ["$a_x$ [g]", "$a_y$ [g]", "$a_z$ [g]"]

for axis, label in zip(axes, labels):	# zip() function is used to iterate over two or more iterables at the same time, labels and axes in
										# this case
	axis.set_ylabel(label)
	axis.legend(loc="upper right")
	axis.grid(True)

######################
# --- FUNCTIONS. ---
######################

# --- To unpack the data structs send by Arduino ---
def decode_packet(data):
	header, status, ax, ay, az, timestamp = struct.unpack(PACKET_LAYOUT, data)

	if header != PACKET_HEADER:	# to disregard invalid packets
		print("Invalid header")
		return None		

	ax /= ACC_SENSITIVITY
	ay /= ACC_SENSITIVITY
	az /= ACC_SENSITIVITY

	return status, ax, ay, az, timestamp

#NOTE: this function doesn't care about serial ports, plotting or whatever the scrript is supposed to do. It only knows our Arduino 
# packet layout

# --- To correctly read the packet streamed by serial ---
#NOTE: we wait to read the header and then we read the next remaining bytes.
# Then to read the packets we rely on the fact that we know how they begin and which is their size
def read_packet(ser):
	previous_byte = None	# The idea is that, to find syncronization, we have to slide through the stream

	while not stop_event.is_set():	# Do it until the acquisition data thread is stopped by the main()
		current_byte = ser.read(1)	# take the first byte

		if not current_byte:	# if it is empty go to next cycle, this may happen cause of the serial timeout = 0.1
			continue

		if previous_byte == HEADER_BYTE_1 and current_byte == HEADER_BYTE_2:	# if both previous and current bytes correspond
			remaining = ser.read(PACKET_SIZE - 2)								# to the header, read the next remaining bytes of the packet

			if len(remaining) != PACKET_SIZE - 2:	# if we have read less then expected number of bytes, return nothing
				return None

			return HEADER_BYTES + remaining		# if the read operation went well return the entire packet

		previous_byte = current_byte	# if not both current and previous bytes correspond to the header's bytes, overwrite the previous
										# byte and return nothing
	return None
#NOTE: the output of the function follows the idea "either return exactly one complete PACKET_SIZE-bytes packet or return None"
# This is a good function design since each layer takes responsibility for one kind of validity

# --- To acquire data, i.e. control the serial communication ---
#NOTE: the design of this function is that it has the application logic, while read_packet has the framing/serial communication logic
# and decode_packet has the decoding logic
def acquire_data(ser):
	first_timestamp = None
	previous_timestamp = 0
	previous_status = None	# This variable belongs here because only the acquisition thread needs it
	count = 0

	while not stop_event.is_set():	# This peace of the function will work until the main code will set the stop_event
		data = read_packet(ser)	# This line is blocking: (actually is the istruction ser.read() which is blocking) python may wait
								# here until the requestd bytes arrive.
								# Is because of this line that we need two different threads
		
		if data is None:	# if read_packet failed we have a framing/serial acquisition problem, different from a decoding problem
			continue		# which is handle below.

		decoded = decode_packet(data)

		if decoded is None:	# decode has two possible outputs: None, if the packet is invalid, or a tuple if it is valid. So if the packet
			continue		# is invalid we exit from the while loop (this is what actually does the continue instrucion, it does not continue
							# inside the loop, it makes it stops, exiting from it and move to the next iteration).
							# So this control means: If decoding failed, abandon this iteration of the while loop and immediately try to
							# acquire the next packet.
							# This control is then called "guard clause", it aswers the question: "did the complete packet have the
							# structure/header I expected?"

		new_status, ax, ay, az, timestamp = decoded

		if first_timestamp is None:
			first_timestamp = timestamp

		# if count < 50:	# To check if data are actually send at 200Hz or if we lose something
		# 	dt = timestamp - previous_timestamp
		# 	print(dt)
		# 	previous_timestamp = timestamp
		# 	count += 1

		time_s = (timestamp - first_timestamp)/1000000.0

		# Status handling
		if new_status != 0:
			if previous_status is None:
				if new_status == 1:
					print(f"[MPU6050] I2C communication unavailable at startup: I2C_TX_ERROR")
				elif new_status == 2:
					print(f"[MPU6050] I2C communication unavailable at startup: I2C_READ_ERROR")
			elif previous_status == 0:
				if new_status == 1:
					print(f"[MPU6050] I2C communication LOST, I2C_TX_ERROR: failed while setting ACCEL_XOUT_H register pointer, at {time_s:.2f}s")
				elif new_status == 2:
					print(f"[MPU6050] I2C communication LOST, I2C_READ_ERROR: failed whiel requesting the six acceleration bytes, at {time_s:.2f}s")

			previous_status = new_status
			continue

		# if execution reaches here, it means new_status == 0
		if previous_status in (1,2):
			print(f"[MPU6050] I2C communication RECOVERED, at {time_s:.2f}s")

		previous_status = new_status

		# if count < 50:	# To check if data are actually send at 200Hz or if we lose something
		# 	dt = timestamp - previous_timestamp
		# 	print(dt)
		# 	previous_timestamp = timestamp
		# 	count += 1



		with data_lock:	# Here we find the threading.Lock() principle applied
			time_buffer.append(time_s)
			ax_buffer.append(ax)
			ay_buffer.append(ay)
			az_buffer.append(az)
		#NOTE: this piece roughly means "take the lock, append the data to the buffer, release the lock".
		# This way we can access the buffer from multiple threads without conflicts, in particular, in this case the acquire_data() can
		# update the buffers while the main thread is waiting not having the possibility to access to the buffers.
		# Somehow similar to the critical sessions in the Arduino's fimrware
		# Arduino noInterrupts()
		# → prevents interrupt servicing globally for that section.

		# Python Lock
		# → only blocks other threads that cooperate by acquiring the same Lock.


# --- To update the plot ---
def update_plot(frame):
	with data_lock:	# Same concept as above, but now the acquire_data() that can't access to the buffers until update_plot() has finished
		times = list(time_buffer)
		xs = list(ax_buffer)
		ys = list(ay_buffer)
		zs = list(az_buffer)

	if not times:					# times is not boolean, but python has the concept of TRUTHINESS.
		return line1, line2, line3	# This is "boolean interpretation" is applied to sequences and containers:
									# [] --> False		this is out case: if times is empty, we don't want to plot anything
									# [1, 2, 3] --> True
									# "" --> False
									# "abc" --> True
									# () --> False
									# (1, 2, 3) --> True
									# {} --> False
									# {1: "a", 2: "b", 3: "c"} --> True
									# eventually we might had written if len(times) == 0, but if not times is really idiomatic of python
	#NOTE: this guard cluse is necessary to avoid arriving to the plotting with empty list which will cause an error.
	#NOTE: the difference between return and continue: return means "stop the entire function call, return the control to whoever called it"
	# since update_plot is not running a loop we do not use continue, instead we use return

	line1.set_data(times, xs)	# This does not create another plot, just update the data inside the original one
	line2.set_data(times, ys)
	line3.set_data(times, zs)

	if len(times) > 1:
		axes[2].set_xlim(times[0], times[-1])

	for axis in axes:
		axis.relim()
		axis.autoscale_view(scalex=False, scaley=True)	# Recomputes the y-axis limits based on the new data
														# scalex=False since we control the x-axis manually

	return line1, line2, line3


#########################
# --- MAIN FUNCTION. ---
#########################

def main():
	ser = serial.Serial(PORT, BAUD_RATE, timeout=0.1)	# The timeout is useful to avoid that the acquisition thread may sit indefinitly
														# inside ser.read(PACKET_SIZE) once we close the graph

	acquisition_thread = threading.Thread(target=acquire_data, args=(ser,), daemon=True)
	#NOTE: This line is really important: it creates the second execution workflow, i.e. the second thread. Indeed it points to the relative
	#function acquire_data()
	#NOTE: we do not need to create the main thread, it is created automatically by the python interpreter.

	acquisition_thread.start()	# And here we simply start the second thread

	animation = FuncAnimation(fig, update_plot, interval=PLOT_INTERVAL_MS, cache_frame_data=False)	# This function just continuosly update
																									# the plot every 50ms passing the frame
																									# argument to update_plot

	try:
		plt.show()

	finally:
		stop_event.set()	# Here we set the stop_event so that the while piece of code of acquire_data won't run
		acquisition_thread.join()	# Wait for this thread to finish before continuing, so to avoid that it will stuck in the
									# ser.read(PACKET_SIZE) line
		ser.close()	# Then we wait ultil the acquisition thread is finished to close the serial port, rather than closing resources while
					# while another thread might still be using them


if __name__ == "__main__":
	main()


#NOTE (About the main() function and the strange pupping-out-from-nowhere __name__ variable): main by itself is just a function, nothing
#more, at least from the python interpreter point of view (we actuually now that it is out main thread and whatsoever), the point is that
# it bacomes tha real main code in the if control. So when the the condition in the if is satisfied?
# The __name__ variable is automatically created by python. The concept is that "EVERY PYTHON FILE IS TREATED AS A MODULE" and python
# automatically puts several SPECIAL variables into that module's global namespace, then __name__ is one of them, but an important one.
# Indeed if we execute directly this file (for example from the terminal as "python live_plot_debugger.py") python will set __name__ =
# "__main__", then the if condition will be satisfied and the main() will be run.
# Things might change if we decide to import this file as a module in another file, then the __name__ variable for this script will become
# "live_plot_debugger", that is its own name
# So the important consequence of this is that the code will run only if directly executed, not if will be imported. In this way we can
# import its functions in another script avoiding running matplotlib and opening the serial.


