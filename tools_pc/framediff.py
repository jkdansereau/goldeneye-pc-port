#!/usr/bin/env python3
"""framediff.py - visual regression check for GE_PCDUMP frame captures.

Compares a directory of candidate frames against a golden baseline
(default tools_pc/golden/) and reports per-frame, per-region divergence.

WHY STRUCTURAL / TOLERANT BY DEFAULT
-----------------------------------
The PC port is NOT frame-deterministic (see docs/PCPortResearch.md
Section F "D117"): logic advances a wall-clock-variable number of 60Hz
ticks per rendered frame (src/game/frametiming.c waitForNextFrame /
boss.c mainTickElapsed gate driven by osGetCount(), which the port maps
to real time in port/src/libultra.c). Two runs of the same build diverge
15-40%% of pixels by frame ~200. So an exact compare is meaningless
unless/until a fixed-tick mode is added; the default mode here is a
structural comparison that tolerates that jitter but still catches a real
regression (geometry gone, screen flipped, textures corrupted, HUD
missing, colour/gamma shift).

Three independent signals per frame, each thresholded:
  * grid-cell mean-colour delta   - GxH cells (default 16x12); flags a
    cell whose mean RGB moved more than --cell-thresh (max abs channel).
  * non-clear-pixel %% delta       - whole-frame share of non-black
    pixels (reuses pixcount.py's "anything != (0,0,0)" rule); flags if
    it moved more than --nonclear-thresh percentage points.
  * perceptual hash (aHash)       - 16x16 grayscale mean-threshold hash;
    flags if Hamming distance > --phash-thresh (of 256 bits).

--mask X0,Y0,X1,Y1 (repeatable) excludes a rectangle (e.g. the animated
HUD / ammo counter) from all three signals.

EXACT MODE (--exact)
--------------------
For when determinism exists (a GE_DETERM-style fixed-tick build). Exact
per-pixel compare with a small tolerance for GL dithering / MSAA
nondeterminism: a pixel "differs" only if any channel is off by more than
--tol (default 2); the frame fails only if more than --tol-pct %% of
(unmasked) pixels differ.

USAGE
-----
  framediff.py <candidate_dir> [--golden tools_pc/golden]
  framediff.py <candidate_dir> --exact
  framediff.py <candidate_dir> --mask 0,400,640,480 --mask 540,0,640,40
  framediff.py <candidate_dir> --update        # refresh golden from candidate
  framediff.py <candidate_dir> --json

Frames are matched by filename stem (frame_000200). .ppm (P6) and .png
are both accepted on either side. Exit status: 0 = all frames within
threshold, 1 = at least one frame/region over threshold, 2 = usage / IO
error (e.g. a golden frame with no candidate).

Regenerate the golden set (after a deliberate visual change):
  GE_PCDUMP="200-445" ./build-pc/ge007.x86_64.exe -level_09 -ml0 -me0 \
      -mgfx100 -mvtx50 -mt700 -ma150
  python tools_pc/framediff.py ppm --update
"""
import argparse
import json
import os
import struct
import sys
import zlib


# --------------------------------------------------------------------------
# image loading -> (w, h, bytes) with 3 bytes/pixel RGB, no alpha
# --------------------------------------------------------------------------
def _load_ppm(data):
    if data[:2] != b"P6":
        raise ValueError("not a binary P6 PPM")
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
    pos += 1
    w, h, maxval = fields
    if maxval != 255:
        raise ValueError("unsupported PPM maxval %d" % maxval)
    px = data[pos:pos + w * h * 3]
    if len(px) < w * h * 3:
        raise ValueError("truncated PPM pixel data")
    return w, h, px


def _load_png(data):
    """Minimal PNG decoder: 8-bit, colour type 2 (RGB) or 6 (RGBA), no
    interlace - which is what tools write. Falls back to PIL if present."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos = 8
    w = h = bitdepth = colortype = interlace = None
    idat = bytearray()
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            w, h, bitdepth, colortype, _, _, interlace = struct.unpack(
                ">IIBBBBB", chunk)
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break
    if bitdepth != 8 or interlace != 0 or colortype not in (2, 6):
        try:
            from PIL import Image
            import io
            im = Image.open(io.BytesIO(data)).convert("RGB")
            return im.width, im.height, im.tobytes()
        except Exception:
            raise ValueError(
                "unsupported PNG (bitdepth=%s colortype=%s interlace=%s) "
                "and PIL unavailable" % (bitdepth, colortype, interlace))
    channels = 4 if colortype == 6 else 3
    raw = zlib.decompress(bytes(idat))
    stride = w * channels
    out = bytearray(w * h * 3)
    prev = bytearray(stride)
    rp = 0
    for y in range(h):
        ftype = raw[rp]
        rp += 1
        line = bytearray(raw[rp:rp + stride])
        rp += stride
        if ftype == 1:  # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ftype == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:  # Average
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:  # Paeth
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif ftype != 0:
            raise ValueError("bad PNG filter %d" % ftype)
        prev = line
        for x in range(w):
            out[(y * w + x) * 3:(y * w + x) * 3 + 3] = \
                line[x * channels:x * channels + 3]
    return w, h, bytes(out)


def load_image(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] == b"P6":
        return _load_ppm(data)
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return _load_png(data)
    raise ValueError("%s: unrecognised image format" % path)


# --------------------------------------------------------------------------
# frame discovery
# --------------------------------------------------------------------------
_EXTS = (".ppm", ".png")


def index_dir(d):
    """stem -> path, for frame_*.ppm / frame_*.png in d."""
    out = {}
    if not os.path.isdir(d):
        raise ValueError("not a directory: %s" % d)
    for name in os.listdir(d):
        stem, ext = os.path.splitext(name)
        if ext.lower() in _EXTS:
            out.setdefault(stem, os.path.join(d, name))
    return out


# --------------------------------------------------------------------------
# masking
# --------------------------------------------------------------------------
def parse_mask(spec):
    try:
        x0, y0, x1, y1 = (int(v) for v in spec.split(","))
    except ValueError:
        raise ValueError("bad --mask %r (want X0,Y0,X1,Y1)" % spec)
    return (min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1))


def masked(x, y, masks):
    for (x0, y0, x1, y1) in masks:
        if x0 <= x < x1 and y0 <= y < y1:
            return True
    return False


def build_mask_bitmap(w, h, masks):
    """row-major bytearray, 1 = excluded. None if no masks (fast path)."""
    if not masks:
        return None
    bm = bytearray(w * h)
    for (x0, y0, x1, y1) in masks:
        x0c, x1c = max(0, x0), min(w, x1)
        for y in range(max(0, y0), min(h, y1)):
            base = y * w
            for x in range(x0c, x1c):
                bm[base + x] = 1
    return bm


# --------------------------------------------------------------------------
# signals
# --------------------------------------------------------------------------
def grid_delta(pa, pb, w, h, gx, gy, mask):
    """Return list of (col, row, dmean) for every cell, max abs channel
    delta of the cell mean. Cells fully inside the mask are skipped."""
    cells = []
    for row in range(gy):
        y0 = row * h // gy
        y1 = (row + 1) * h // gy
        for col in range(gx):
            x0 = col * w // gx
            x1 = (col + 1) * w // gx
            sa = [0, 0, 0]
            sb = [0, 0, 0]
            n = 0
            for y in range(y0, y1):
                rowbase = y * w
                for x in range(x0, x1):
                    if mask is not None and mask[rowbase + x]:
                        continue
                    o = (rowbase + x) * 3
                    sa[0] += pa[o]; sa[1] += pa[o + 1]; sa[2] += pa[o + 2]
                    sb[0] += pb[o]; sb[1] += pb[o + 1]; sb[2] += pb[o + 2]
                    n += 1
            if n == 0:
                continue
            d = max(abs(sa[i] - sb[i]) / n for i in range(3))
            cells.append((col, row, d))
    return cells


def nonclear_pct(px, w, h, mask):
    n = 0
    hit = 0
    for i in range(w * h):
        if mask is not None and mask[i]:
            continue
        n += 1
        o = i * 3
        if px[o] or px[o + 1] or px[o + 2]:
            hit += 1
    return 100.0 * hit / n if n else 0.0


def ahash(px, w, h, mask, side=16):
    """side*side grayscale mean-threshold hash -> int (side*side bits)."""
    cell = []
    for row in range(side):
        y0 = row * h // side
        y1 = (row + 1) * h // side
        for col in range(side):
            x0 = col * w // side
            x1 = (col + 1) * w // side
            s = 0
            n = 0
            for y in range(y0, y1):
                rb = y * w
                for x in range(x0, x1):
                    if mask is not None and mask[rb + x]:
                        continue
                    o = (rb + x) * 3
                    s += (px[o] * 299 + px[o + 1] * 587 + px[o + 2] * 114) // 1000
                    n += 1
            cell.append(s / n if n else 0.0)
    avg = sum(cell) / len(cell)
    bits = 0
    for i, v in enumerate(cell):
        if v >= avg:
            bits |= (1 << i)
    return bits


def hamming(a, b):
    return bin(a ^ b).count("1")


def exact_diff(pa, pb, w, h, mask, tol):
    n = 0
    diff = 0
    maxd = 0
    for i in range(w * h):
        if mask is not None and mask[i]:
            continue
        n += 1
        o = i * 3
        d = max(abs(pa[o] - pb[o]), abs(pa[o + 1] - pb[o + 1]),
                abs(pa[o + 2] - pb[o + 2]))
        if d > maxd:
            maxd = d
        if d > tol:
            diff += 1
    return diff, n, maxd


# --------------------------------------------------------------------------
def save_png(path, w, h, px):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += px[y * w * 3:(y + 1) * w * 3]
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


# --------------------------------------------------------------------------
def main(argv):
    ap = argparse.ArgumentParser(
        description="visual regression check for GE_PCDUMP frames",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    ap.add_argument("candidate", help="directory of candidate frames")
    ap.add_argument("--golden", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "golden"),
        help="golden baseline dir (default: tools_pc/golden)")
    ap.add_argument("--update", action="store_true",
                    help="copy candidate frames onto the golden set (as PNG) "
                         "and exit")
    ap.add_argument("--exact", action="store_true",
                    help="exact per-pixel compare (only meaningful for a "
                         "deterministic build)")
    ap.add_argument("--mask", action="append", default=[], metavar="X0,Y0,X1,Y1",
                    help="exclude a rectangle from all signals (repeatable)")
    ap.add_argument("--grid", default="16x12", metavar="GxH",
                    help="grid-cell resolution (default 16x12)")
    ap.add_argument("--cell-thresh", type=float, default=40.0,
                    help="per-cell mean-channel delta that fails a frame "
                         "(default 40)")
    ap.add_argument("--nonclear-thresh", type=float, default=8.0,
                    help="non-clear-pixel %% swing that fails a frame "
                         "(default 8)")
    ap.add_argument("--phash-thresh", type=int, default=40,
                    help="aHash Hamming distance (of 256) that fails a frame "
                         "(default 40; two nondeterministic re-runs of the "
                         "same build sit around 4-25 by frame 200)")
    ap.add_argument("--tol", type=int, default=2,
                    help="[--exact] per-channel tolerance (default 2)")
    ap.add_argument("--tol-pct", type=float, default=0.5,
                    help="[--exact] %% of pixels allowed over tolerance "
                         "(default 0.5)")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    args = ap.parse_args(argv[1:])

    try:
        gx, gy = (int(v) for v in args.grid.lower().split("x"))
    except ValueError:
        print("bad --grid %r" % args.grid)
        return 2

    try:
        masks = [parse_mask(m) for m in args.mask]
        cand = index_dir(args.candidate)
    except ValueError as e:
        print("error: %s" % e)
        return 2

    if args.update:
        os.makedirs(args.golden, exist_ok=True)
        if not cand:
            print("error: no frames in %s" % args.candidate)
            return 2
        for stem, path in sorted(cand.items()):
            w, h, px = load_image(path)
            dst = os.path.join(args.golden, stem + ".png")
            save_png(dst, w, h, px)
            print("updated %s (%dx%d)" % (dst, w, h))
        return 0

    try:
        golden = index_dir(args.golden)
    except ValueError as e:
        print("error: %s" % e)
        return 2
    if not golden:
        print("error: no golden frames in %s" % args.golden)
        return 2

    results = []
    worst = 0
    for stem in sorted(golden):
        if stem not in cand:
            print("FAIL %s: no candidate frame" % stem)
            results.append({"frame": stem, "status": "missing"})
            worst = 2
            continue
        gw, gh, gp = load_image(golden[stem])
        cw, ch, cp = load_image(cand[stem])
        if (gw, gh) != (cw, ch):
            print("FAIL %s: size %dx%d vs golden %dx%d"
                  % (stem, cw, ch, gw, gh))
            results.append({"frame": stem, "status": "size"})
            worst = 2
            continue
        mask = build_mask_bitmap(gw, gh, masks)

        if args.exact:
            diff, n, maxd = exact_diff(gp, cp, gw, gh, mask, args.tol)
            pct = 100.0 * diff / n if n else 0.0
            ok = pct <= args.tol_pct
            print("%s %s: %d/%d px over tol (%.3f%%, maxchan=%d)"
                  % ("ok  " if ok else "FAIL", stem, diff, n, pct, maxd))
            results.append({"frame": stem, "status": "ok" if ok else "fail",
                            "diff_pct": pct, "max_chan": maxd})
            if not ok:
                worst = max(worst, 1)
            continue

        cells = grid_delta(gp, cp, gw, gh, gx, gy, mask)
        bad = [(c, r, d) for (c, r, d) in cells if d > args.cell_thresh]
        worstcell = max((d for _, _, d in cells), default=0.0)
        ncg = nonclear_pct(gp, gw, gh, mask)
        ncc = nonclear_pct(cp, gw, gh, mask)
        ncd = abs(ncg - ncc)
        hd = hamming(ahash(gp, gw, gh, mask), ahash(cp, cw, ch, mask))

        fail = (bool(bad) or ncd > args.nonclear_thresh
                or hd > args.phash_thresh)
        tag = "FAIL" if fail else "ok  "
        print("%s %s: cells>thr=%d (worst dmean=%.1f)  nonclear d=%.2fpp "
              "(%.2f->%.2f)  phash=%d"
              % (tag, stem, len(bad), worstcell, ncd, ncg, ncc, hd))
        if bad:
            preview = ", ".join("(%d,%d):%.0f" % (c, r, d)
                                for c, r, d in bad[:8])
            print("       cells: %s%s" % (preview,
                                          " ..." if len(bad) > 8 else ""))
        results.append({
            "frame": stem, "status": "fail" if fail else "ok",
            "cells_over": len(bad), "worst_cell": worstcell,
            "nonclear_delta": ncd, "nonclear_golden": ncg,
            "nonclear_cand": ncc, "phash_hamming": hd,
        })
        if fail:
            worst = max(worst, 1)

    if args.json:
        print(json.dumps({"results": results, "exit": worst}, indent=2))

    n_fail = sum(1 for r in results if r["status"] in ("fail", "missing", "size"))
    print("\n%d/%d frames within threshold%s"
          % (len(results) - n_fail, len(results),
             "" if worst == 0 else "  --  REGRESSION" if worst == 1
             else "  --  ERROR"))
    return worst


if __name__ == "__main__":
    sys.exit(main(sys.argv))
