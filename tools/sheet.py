#!/usr/bin/env python3
"""Tile PNG screenshots into one contact sheet. Usage: sheet.py OUT.png IN.png [IN.png ...]"""
import struct, sys, zlib


def readpng(p):
    d = open(p, "rb").read()
    i, idat = 8, b""
    while i < len(d):
        l = struct.unpack(">I", d[i:i + 4])[0]
        t = d[i + 4:i + 8]
        if t == b"IHDR":
            w, h = struct.unpack(">II", d[i + 8:i + 16])
        if t == b"IDAT":
            idat += d[i + 8:i + 8 + l]
        i += 12 + l
    raw = zlib.decompress(idat)
    return w, h, [raw[y * (w * 3 + 1) + 1:(y + 1) * (w * 3 + 1)] for y in range(h)]


def chunk(t, d):
    c = struct.pack(">I", len(d)) + t + d
    return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)


out_path, files = sys.argv[1], sys.argv[2:]
cols = min(3, len(files))
nrows = (len(files) + cols - 1) // cols
W, H = cols * 250 - 10, nrows * 250 - 10
out = [bytearray(b"\x80" * (W * 3)) for _ in range(H)]
for k, f in enumerate(files):
    w, h, rows = readpng(f)
    ox, oy = (k % cols) * 250, (k // cols) * 250
    for y in range(h):
        out[oy + y][ox * 3:ox * 3 + w * 3] = rows[y]
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(b"".join(b"\x00" + bytes(r) for r in out))) + chunk(b"IEND", b"")
open(out_path, "wb").write(png)
