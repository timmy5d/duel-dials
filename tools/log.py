#!/usr/bin/env python3
"""Reset the board and print its serial log. Usage: log.py PORT [SECONDS]"""
import sys, time
import serial

port = sys.argv[1]
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 5


def open_port():
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = s.rts = False
    s.open()
    return s


s = open_port()
s.rts = True
time.sleep(0.1)
s.rts = False
s.close()
end = time.time() + secs
out = b""
while time.time() < end:
    try:
        s = open_port()
        while time.time() < end:
            out += s.read(1024)
    except Exception:
        time.sleep(0.05)
sys.stdout.write(out.decode("utf8", "replace"))
