#!/usr/bin/env python3
"""Scan all model files' texture tables -> image headers -> format/maxlod.
Determines whether any LUT textures (formats 9-12) exist, which affects
worst-case marker expansion sizing (D45)."""
import csv, struct, zlib, os, re
from collections import Counter

ROM = r"data/ge007.ntsc-final.z64"
rom = open(ROM, "rb").read()

imgrows = list(csv.reader(open("imagelist.u.csv")))
img_off = [int(r[0]) for r in imgrows]
img_sz = [int(r[1]) for r in imgrows]

rows = list(csv.reader(open(r"scripts/filelist.u.csv")))

nsnt = {}
for root, dirs, files in os.walk("assets"):
    for fn in files:
        if fn.lower().endswith("modelfileheader.inc.c"):
            f = os.path.join(root, fn)
            for line in open(f):
                m = re.search(r"MODELFILEHEADER\((.*)\)\s*;?\s*$", line)
                if not m:
                    continue
                a = [x.strip() for x in m.group(1).split(",")]
                if len(a) < 9:
                    continue
                try:
                    nsnt[a[0]] = (int(a[4], 0), int(a[8], 0))
                except ValueError:
                    pass

def bitread(buf, pos, n):
    val = 0
    for i in range(n):
        byte = buf[pos >> 3]
        bit = 7 - ((pos & 7) % 8)
        val = (val << 1) | ((byte >> bit) & 1)
        pos += 1
    return val, pos

fmtall = Counter()
fmtlut = Counter()
maxlod_dist = Counter()
nimg = 0
bad = 0
zc = 0
matched_files = 0
lut_examples = []

for r in rows:
    if len(r) < 3 or not r[2]:
        continue
    base = r[2].rsplit("/", 1)[-1]
    if base.endswith(".bin"):
        base = base[:-4]
    key = None
    for cand in (base, base[1:], base[:-1], base[1:-1]):
        if cand in nsnt:
            key = cand
            break
    if not key:
        continue
    matched_files += 1
    NS, NT = nsnt[key]
    addr, size = int(r[0]), int(r[1])
    out = zlib.decompress(rom[addr:addr + size][2:], -15)
    D = len(out)
    texoff = 4 * NS + 12 * NT
    for i in range(NT):
        tid = struct.unpack_from(">I", out, texoff + i * 12)[0]
        if tid >= len(img_off):
            bad += 1
            continue
        nimg += 1
        o = img_off[tid]
        s = img_sz[tid]
        hdr = rom[o]
        iszlib = (hdr & 0x40) >> 6
        lod = hdr & 0x3f
        maxlod_dist[min(lod, 7)] += 1
        if iszlib:
            zc += 1
            fmt = (rom[o + 1] >> 4) & 0xf
        else:
            b2 = rom[o + 1:o + s]
            fmt, _ = bitread(b2, 0, 4)
        fmtall[fmt] += 1
        if fmt in (9, 10, 11, 12):
            fmtlut[fmt] += 1
            if len(lut_examples) < 10:
                lut_examples.append((key, i, tid, fmt))

print("nsnt entries:", len(nsnt))
print("matched model files:", matched_files, "tex refs:", nimg, "bad:", bad, "zlib:", zc)
print("formats used:", dict(sorted(fmtall.items())))
print("LUT formats (9-12):", dict(fmtlut))
print("maxlod dist:", dict(sorted(maxlod_dist.items())))
for e in lut_examples:
    print("  LUT example:", e)
