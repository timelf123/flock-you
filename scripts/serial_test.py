import serial
import time
import sys

port = "COM5"
seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 20

s = serial.Serial(port, 115200, timeout=1)
time.sleep(0.3)
s.setDTR(False)
s.setRTS(True)
time.sleep(0.1)
s.setRTS(False)

deadline = time.time() + seconds
while time.time() < deadline:
    line = s.readline()
    if line:
        print(line.decode("utf-8", errors="replace"), end="")

s.close()
