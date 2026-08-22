# D43 helper: walk every model file's node tree (N64 layout) and report
# opcode histogram + structural sanity. Usage: python d43_walk.py
import csv, struct, zlib, sys
from collections import Counter

ROM = r"data/ge007.ntsc-final.z64"
rom = open(ROM, "rb").read()
rows = list(csv.reader(open(r"scripts/filelist.u.csv")))

OP={1:"HEADER",2:"GROUP",3:"OP03",4:"DL",5:"OP05",6:"OP06",7:"OP07",8:"LOD",9:"BSP",10:"BOX",11:"OP11",12:"GUNFIRE",13:"SHADOW",14:"OP14",15:"INTERLINK",16:"OP16",17:"OP17",18:"SWITCH",21:"GROUPSIMPLE",22:"DLPRIMARY",23:"HEADPH",24:"DLCOLLISION"}

# N64 record sizes (bytes) per opcode, from bondtypes.h
RSZ={1:0x10,2:0x1C,3:0x1C,4:0x14,5:0x1AC,6:0x18,7:0x1B0,8:0x10,9:0x24,10:0x1C,
     11:0x50,12:0x28,13:0x20,14:0x10,15:0x1C,16:0x18,17:0x20,18:0x08,21:0x14,
     22:0x10,23:0x02,24:0x20}

# (numSwitches, numtextures) per model from the exe headers would be ideal;
# instead parse assets/obseg/**/ModelFileHeader.inc.c + body records.
import re, glob
nsnt = {}
import os
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
        name = a[0]
        try:
            NS = int(a[4], 0); NT = int(a[8], 0)
        except ValueError:
            continue
        nsnt[name] = (NS, NT)

# body/chr records live in other .inc.c files with the same macro? check c_item_entries etc.
print("headers parsed:", len(nsnt))

opcount = Counter()
files_ok = 0
files_bad = []
total_nodes = 0
maxP = 0
for r in rows:
    if len(r) < 3 or not r[2]: continue
    base = r[2].rsplit("/",1)[-1]
    if base.endswith(".bin"): base = base[:-4]
    if not (base[0] in "CGP" and base.endswith("Z") and "_stan" not in base): continue
    try: addr,size=int(r[0]),int(r[1])
    except ValueError: continue
    try: out = zlib.decompress(rom[addr:addr+size][2:], -15)
    except Exception: files_bad.append((base,"decomp")); continue
    D = len(out)
    key = None
    for cand in (base, base[1:], base[:-1], base[1:-1]):
        if cand in nsnt:
            key = cand; break
    NS,NT = nsnt.get(key, (0,0)) if key else (0,0)
    if NT is None:
        # try to find NT from a record table elsewhere; fallback scan
        files_bad.append((base,"no-NT"))
        continue
    root_off = 4*NS + 12*NT
    def be16(o): return struct.unpack_from(">H", out, o)[0]
    def be32(o): return struct.unpack_from(">I", out, o)[0] & 0xFFFFFF
    seen = set()
    ok = True
    n = 0
    stack = [root_off] if root_off < D else []
    while stack:
        o = stack.pop()
        if o in seen or o >= D: ok=False; break
        seen.add(o)
        op = be16(o) & 0xff
        opcount[op] += 1
        n += 1
        data = be32(o+4); child = be32(o+20)
        if not (0 < data < D): ok=False; break
        # validate record fits
        rsz = RSZ.get(op, 0)
        if rsz and data + rsz > D: ok=False; files_bad.append((base,"rec-overflow op%d @%X+%X>%X"%(op,data,rsz,D))); break
        if child: stack.append(child)
    total_nodes += n
    if ok: files_ok += 1
    else: files_bad.append((base,"walk-fail root=%X NS=%d NT=%d D=%X"%(root_off,NS,NT,D)))

print("files ok:", files_ok, " bad:", len(files_bad))
for b in files_bad[:20]: print("   ", b)
print("total nodes:", total_nodes)
print("opcode histogram:")
for op in sorted(opcount):
    print("  op%-3d %-12s x%d" % (op, OP.get(op,"??"), opcount[op]))
