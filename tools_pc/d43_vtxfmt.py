#!/usr/bin/env python3
"""Verify collision-vertex format: @8 is 0x05xxxxxx-or-0 (LinkedTo node ptr),
@C is a small s16 index. Also verify normal-vertex arrays never have 0x05xxxxxx at @8."""
import csv, struct, zlib, os, re
from collections import Counter

rom = open('data/ge007.ntsc-final.z64','rb').read()
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

def be32(b,o): return struct.unpack_from(">I",b,o)[0] & 0xFFFFFF
def be32r(b,o): return struct.unpack_from(">I",b,o)[0]

coll_bad8 = Counter(); coll_idx_range = [1<<30, -1<<30]
norm_weird8 = 0; norm_total = 0; coll_total = 0
files_checked = 0
for r in rows:
    if len(r) < 3 or not r[2]: continue
    base = r[2].rsplit("/",1)[-1]
    if base.endswith(".bin"): base = base[:-4]
    if not (base[0] in "CGP" and base.endswith("Z") and "_stan" not in base): continue
    key=None
    for cand in (base, base[1:], base[:-1], base[1:-1]):
        if cand in nsnt: key=cand; break
    if not key: continue
    NS,NT = nsnt[key]
    try: addr,size=int(r[0]),int(r[1])
    except ValueError: continue
    try: out=zlib.decompress(rom[addr:addr+size][2:],-15)
    except Exception: continue
    D=len(out); R0=4*NS+12*NT
    files_checked += 1
    seen=set(); stack=[R0]
    while stack:
        o=stack.pop()
        if o in seen or o>=D: continue
        seen.add(o)
        op=struct.unpack_from(">H",out,o)[0]&0xff
        data=be32(out,o+4)
        child = be32(out,o+0x14); nxt = be32(out,o+0xC)
        if op==8:
            a = be32(out,data+8)
            if a: stack.append(a)
        elif op==18:
            c = be32(out,data)
            if c: stack.append(c)
        else:
            if child: stack.append(child)
        if nxt: stack.append(nxt)
        if op==4 or op==22:
            if op==4:
                nv=struct.unpack_from(">H",out,data+0x10)[0]; vo=be32(out,data+0xC)
            else:
                nv=struct.unpack_from(">i",out,data)[0]; vo=be32(out,data+4)
            for i in range(nv):
                v = vo + 16*i
                w8 = be32r(out, v+8)
                norm_total += 1
                if w8 != 0 and (w8 >> 24) != 5:
                    norm_weird8 += 1
        elif op==24:
            nv=struct.unpack_from(">h",out,data+0xC)[0]
            ncv=struct.unpack_from(">h",out,data+0xE)[0]
            vo=be32(out,data+8); cvo=be32(out,data+0x10)
            for i in range(nv):
                v = vo + 16*i
                w8 = be32r(out, v+8)
                norm_total += 1
                if w8 != 0 and (w8 >> 24) != 5:
                    norm_weird8 += 1
            for i in range(ncv):
                v = cvo + 16*i
                w8 = be32r(out, v+8)
                idx = struct.unpack_from(">h", out, v+0xC)[0]
                coll_total += 1
                if w8 != 0 and (w8 >> 24) != 5:
                    coll_bad8[w8 & 0xFFFFFF] += 1
                coll_idx_range[0] = min(coll_idx_range[0], idx)
                coll_idx_range[1] = max(coll_idx_range[1], idx)

print("files:", files_checked, " normal vtx:", norm_total, " weird @8 (non-0/non-seg5):", norm_weird8)
print("collision vtx:", coll_total, " bad @8 values:", dict(list(coll_bad8.items())[:10]))
print("CollisionRelatedIndex range:", coll_idx_range)
