import serial
import struct

ser = serial.Serial("/dev/ttyACM0", 115200)

data = ser.read(12)

print(data)