# D43 helper: dump real GDL command streams from model files (N64 layout),
# showing w1 top-byte distribution per opcode. Usage: python d43_gdldump.py <name-substr>
import csv, struct, sys, zlib, os, re
from collections import Counter

ROM = r"data/ge007.ntsc-final.z64"
rom = open(ROM, "rb").read()
rows = list(csv.reader(open(r"scripts/filelist.u.csv")))

hdrfiles = set()
for root, dirs, files in os.walk("assets"):
    for fn in files:
        if fn.lower().endswith("modelfileheader.inc.c"):
            hdrfiles.add(os.path.join(root, fn))
nsnt = {}
for f in sorted(hdrfiles):
    for line in open(f):
        m = re.search(r"MODELFILEHEADER\((.*)\)\s*;?\s*$", line)
        if not m: continue
        a = [x.strip() for x in m.group(1).split(",")]
        if len(a) < 9: continue
        try: nsnt[a[0]] = (int(a[4], 0), int(a[8], 0))
        except ValueError: pass

def be32(b, o): return struct.unpack_from(">I", b, o)[0] & 0xFFFFFF

flt = sys.argv[1] if len(sys.argv) > 1 else ""
limit = int(sys.argv[2]) if len(sys.argv) > 2 else 4
shown = 0
for r in rows:
    if len(r) < 3 or not r[2]: continue
    base = r[2].rsplit("/", 1)[-1]
    if base.endswith(".bin"): base = base[:-4]
    if not (base[0] in "CGP" and base.endswith("Z") and "_stan" not in base): continue
    if flt and flt.lower() not in base.lower(): continue
    try: addr, size = int(r[0]), int(r[1])
    except ValueError: continue
    key = None
    for cand in (base, base[1:], base[:-1], base[1:-1]):
        if cand in nsnt: key = cand; break
    if not key: continue
    NS, NT = nsnt[key]
    try: out = zlib.decompress(rom[addr:addr + size][2:], -15)
    except Exception: continue
    D = len(out)
    R0 = 4 * NS + 12 * NT

    # collect DL / DLCOLLISION record gdl offsets (full walk: Child+Next+Parent)
    stack = [R0]; seen = set(); gdls = []
    while stack:
        o = stack.pop()
        if o in seen or o >= D: continue
        seen.add(o)
        op = struct.unpack_from(">H", out, o)[0] & 0xff
        data = be32(out, o + 4)
        parent = be32(out, o + 8); nxt = be32(out, o + 0xC)
        child = be32(out, o + 0x14)
        if op in (4, 24):
            p = be32(out, data); s = be32(out, data + 4)
            if p: gdls.append((p, "P"))
            if s: gdls.append((s, "S"))
        for q in (child, nxt, parent):
            if q: stack.append(q)

    print("== %s  D=0x%X  NS=%d NT=%d  gdls=%d" % (base, D, NS, NT, len(gdls)))
    topbytes = Counter(); opcounts = Counter()
    for goff, tag in gdls[:2]:
        # walk the GDL: commands are 8B slots; stop at ENDDL or invalid opcode
        i = goff; n = 0
        while i + 8 <= D and n < 400:
            w0 = be32(out, i); w1 = be32(out, i + 4)
            c = w0 >> 24
            opcounts[c] += 1
            if c == 0x1d: break  # ENDDL
            i += 8; n += 1
        print("   %s @0x%X (%d slots, ends at 0x%X)" % (tag, goff, n, i))
        for k in range(min(n, 14)):
            w0 = be32(out, goff + 8 * k); w1 = be32(out, goff + 8 * k + 4)
            print("      [%2d] c=%02X w0=%08X w1=%08X" % (k, w0 >> 24, w0, w1))
    for goff, tag in gdls:
        i = goff; n = 0
        while i + 8 <= D and n < 400:
            w0 = be32(out, i); w1 = be32(out, i + 4)
            c = w0 >> 24
            if c == 0x1d: break
            topbytes[(c, (w1 >> 24) & 0xff)] += 1
            i += 8; n += 1
    print("   opcode/topbyte histogram:")
    for (c, tb), cnt in sorted(topbytes.items()):
        print("      c=%02X top(w1)=%02X x%d" % (c, tb, cnt))
    shown += 1
    if shown >= limit: break
