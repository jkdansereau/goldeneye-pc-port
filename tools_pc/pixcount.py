#!/usr/bin/env python3
"""pixcount.py — count non-clear-color pixels in PPM frame dumps (GE_PCDUMP).

The game clears to black (gfx_opengl_clear_framebuffer: glClearColor 0,0,0),
so "non-clear" == any pixel that is not exactly (0,0,0). This turns "the
scene contains actual model geometry / glyphs" into a number: a blank frame
counts ~0, a rendered legal page counts thousands.

Usage (run from anywhere; paths relative to CWD):
  pixcount.py frame.ppm [more.ppm ...]   per-file stats (count + bbox)
  pixcount.py a.ppm b.ppm --diff         pixel diff between two captures

Exit status: 0 if every listed file has at least one non-clear pixel
(--diff: 0 if the two files differ at all), 1 otherwise, 2 on usage/IO error.
"""
import re
import sys

# any non-zero byte (channels are stored R,G,B interleaved)
_NZ = re.compile(rb"[^\x00]")


def load_ppm(path):
    """Parse a binary P6 PPM -> (width, height, bytes)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"P6":
        raise ValueError("%s: not a binary P6 PPM" % path)
    pos = 2
    fields = []
    while len(fields) < 3:
        # skip whitespace and '#' comments
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
    pos += 1  # single whitespace after maxval
    w, h, maxval = fields
    if maxval != 255:
        raise ValueError("%s: unsupported maxval %d" % (path, maxval))
    px = data[pos:pos + w * h * 3]
    if len(px) < w * h * 3:
        raise ValueError("%s: truncated pixel data (%d of %d bytes)"
                         % (path, len(px), w * h * 3))
    return w, h, px


def _bbox(indices, w):
    if not indices:
        return None
    minx = min(i % w for i in indices)
    maxx = max(i % w for i in indices)
    miny = min(i // w for i in indices)
    maxy = max(i // w for i in indices)
    return "(%d,%d)-(%d,%d)" % (minx, miny, maxx, maxy)


def stats(path):
    w, h, px = load_ppm(path)
    total = w * h
    # pixel indices where any channel byte is non-zero (C-speed regex scan)
    idx = set()
    for off in (0, 1, 2):
        ch = px[off::3]
        for m in _NZ.finditer(ch):
            idx.add(m.start())
    pct = 100.0 * len(idx) / total
    print("%s: %dx%d  non-clear %d (%.2f%%)  bbox %s"
          % (path, w, h, len(idx), pct, _bbox(idx, w) or "-"))
    return len(idx)


def diff(path_a, path_b):
    wa, ha, pa = load_ppm(path_a)
    wb, hb, pb = load_ppm(path_b)
    if (wa, ha) != (wb, hb):
        print("sizes differ: %dx%d vs %dx%d" % (wa, ha, wb, hb))
    w, h = min(wa, wb), min(ha, hb)
    n = w * h * 3
    pa, pb = pa[:n], pb[:n]
    # XOR as big ints, then scan the result for non-zero bytes (C-speed)
    x = (int.from_bytes(pa, "big") ^ int.from_bytes(pb, "big")).to_bytes(n, "big")
    idx = {m.start() // 3 for m in _NZ.finditer(x)}
    total = w * h
    print("diff %s vs %s: %d differing pixels (%.2f%% of %dx%d)  bbox %s"
          % (path_a, path_b, len(idx), 100.0 * len(idx) / total, w, h,
             _bbox(idx, w) or "-"))
    return len(idx)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    do_diff = "--diff" in argv
    files = [a for a in argv[1:] if a != "--diff"]
    try:
        if do_diff:
            if len(files) != 2:
                print("--diff needs exactly two files")
                return 2
            return 0 if diff(files[0], files[1]) else 1
        ok = True
        for f in files:
            if not stats(f):
                ok = False
        return 0 if ok else 1
    except (IOError, ValueError) as e:
        print("error: %s" % e)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
