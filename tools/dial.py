#!/usr/bin/env python3
"""Host-side helper for the MTG Dial serial debug console.

Usage: dial.py PORT CMD [CMD ...]
  Each CMD is sent as a line (e.g. r3, l, c, L, "t 120 200").
  "shot:FILE.png" grabs a screenshot. "wait:SECONDS" pauses.
"""
import struct, sys, time, zlib
import serial


def save_png(path, w, h, raw):
    rows = []
    for y in range(h):
        row = bytearray([0])
        for x in range(w):
            i = (y * w + x) * 2
            v = (raw[i] << 8) | raw[i + 1]  # sprite buffer is big-endian RGB565
            r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
            row += bytes([r * 255 // 31, g * 255 // 63, b * 255 // 31])
        rows.append(bytes(row))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def shot(s, path):
    s.reset_input_buffer()
    s.write(b"s\n")
    deadline = time.time() + 5
    buf = b""
    while b"SHOT " not in buf and time.time() < deadline:
        buf += s.read(64)
    start = buf.index(b"SHOT ")
    while b"\n" not in buf[start:]:
        buf += s.read(16)
    nl = buf.index(b"\n", start)
    w, h = map(int, buf[start + 5:nl].split())
    data = buf[nl + 1:]
    need = w * h * 2
    while len(data) < need and time.time() < deadline + 20:
        data += s.read(need - len(data))
    save_png(path, w, h, data[:need])
    print("saved", path)


def main():
    port, cmds = sys.argv[1], sys.argv[2:]
    # Keep DTR/RTS released: on the ESP32-S3 USB-Serial/JTAG they reset the chip.
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.5
    s.dtr = s.rts = False
    s.open()
    time.sleep(2.5)  # opening the port may reset the board
    for c in cmds:
        if c.startswith("shot:"):
            time.sleep(0.25)
            shot(s, c[5:])
        elif c.startswith("wait:"):
            time.sleep(float(c[5:]))
        else:
            s.write((c + "\n").encode())
            time.sleep(0.15)


if __name__ == "__main__":
    main()
