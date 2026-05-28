#!/usr/bin/env python3
import sys
import time
import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM5"
s = serial.Serial(port, 115200, timeout=0.2)
s.setRTS(True)
time.sleep(0.15)
s.setRTS(False)
time.sleep(4)
for _ in range(200):
    line = s.readline()
    if line:
        print(line.decode("utf-8", errors="replace"), end="")
s.close()
