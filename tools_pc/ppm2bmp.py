#!/usr/bin/env python3
"""ppm2bmp.py — convert binary P6 PPM to 24-bit BMP (no dependencies).

Usage: ppm2bmp.py in.ppm out.bmp [scale]   (scale = integer, default 1)
Useful for eyeballing GE_PCDUMP frames without an image library.
"""
import struct
import sys


def load_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"P6":
        raise ValueError("%s: not a binary P6 PPM" % path)
    pos = 2
    fields = []
    while len(fields) < 3:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos] != 0x0A:
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    w, h, maxval = fields
    if maxval != 255:
        raise ValueError("unsupported maxval %d" % maxval)
    px = data[pos + 1:pos + 1 + w * h * 3]
    if len(px) < w * h * 3:
        raise ValueError("truncated pixel data")
    return w, h, px


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    scale = int(argv[3]) if len(argv) > 3 else 1
    w, h, px = load_ppm(argv[1])
    w *= scale
    h *= scale
    row_size = (w * 3 + 3) & ~3
    out = bytearray()
    out += struct.pack("<2sIHHI", b"BM", 54 + row_size * h, 0, 0, 54)
    out += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, w * h * 3, 2835, 2835, 0, 0)
    for y in range(h - 1, -1, -1):  # BMP is bottom-up
        src_y = (y // scale) * (w // scale) * 3
        row = bytearray(row_size)
        for x in range(w):
            i = src_y + (x // scale) * 3
            row[x * 3] = px[i + 2]      # B
            row[x * 3 + 1] = px[i + 1]  # G
            row[x * 3 + 2] = px[i]      # R
        out += row
    with open(argv[2], "wb") as f:
        f.write(out)
    print("%s -> %s (%dx%d)" % (argv[1], argv[2], w, h))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
