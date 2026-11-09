#!/usr/bin/env python3
"""Drive two dials at once. Usage: duo.py PORT1 PORT2 CMD...
Each CMD is prefixed with the dial number, e.g. "1:r3", "2:shot:b.png", "1:n";
"wait:SECONDS" applies to both. Same commands as dial.py; "N:n" prints link status.
"""
import sys, time
import serial
from dial import shot


def open_port(port):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.5
    s.dtr = s.rts = False
    s.open()
    return s


def main():
    ports = [open_port(sys.argv[1]), open_port(sys.argv[2])]
    time.sleep(3)  # opening may reset the boards; let them boot and rejoin
    for p in ports:
        p.reset_input_buffer()
    for c in sys.argv[3:]:
        if c.startswith("wait:"):
            time.sleep(float(c[5:]))
            continue
        idx, cmd = int(c[0]) - 1, c[2:]
        s = ports[idx]
        if cmd.startswith("shot:"):
            time.sleep(0.25)
            shot(s, cmd[5:])
        elif cmd == "n":
            s.reset_input_buffer()
            s.write(b"n\n")
            time.sleep(0.3)
            print(f"[{idx + 1}]", s.read(s.in_waiting or 1).decode("utf8", "replace").strip())
        else:
            s.write((cmd + "\n").encode())
            time.sleep(0.15)


if __name__ == "__main__":
    main()
