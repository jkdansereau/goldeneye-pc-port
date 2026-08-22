#!/usr/bin/env python3
"""Seg-5 G_VTX analysis WITH LOD/SWITCH rewiring (as in d43_gdlorder.py).
Question: does every seg-5 G_VTX offset point INSIDE a vertex array of the same file?
(vo + d where d % 16 == 0 and d < 16*nv) — i.e. is it (array_base_file_offset + displacement)?"""
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

total_seg5 = 0; inside = 0; outside = []
files_with = set()
vtx_regions = Counter()  # (d mod 16) histogram of displacement
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

    # walk with LOD/SWITCH rewiring; collect vertex regions (vo, nv) and GDL offsets per record
    seen=set(); stack=[R0]; regions=[]; gdls=[]
    while stack:
        o=stack.pop()
        if o in seen or o>=D: continue
        seen.add(o)
        op=struct.unpack_from(">H",out,o)[0]&0xff
        data=be32(out,o+4)
        child = be32(out,o+0x14); nxt = be32(out,o+0xC)
        if op==8:   # LOD: Child := Affects (data+8)
            a = be32(out,data+8)
            if a: stack.append(a)
        elif op==18:  # SWITCH: Child := Controls (data+0)
            c = be32(out,data)
            if c: stack.append(c)
        else:
            if child: stack.append(child)
        if nxt: stack.append(nxt)
        if op==4:
            nv=struct.unpack_from(">H",out,data+0x10)[0]
            vo=be32(out,data+0xC)
            regions.append((vo,nv))
            gdls += [x for x in (be32(out,data), be32(out,data+4)) if x]
        elif op==24:
            nv=struct.unpack_from(">h",out,data+0xC)[0]
            cv=struct.unpack_from(">h",out,data+0xE)[0]
            vo=be32(out,data+8); cvo=be32(out,data+0x10)
            regions.append((vo,nv))
            if cv: regions.append((cvo,cv))
            gdls += [x for x in (be32(out,data), be32(out,data+4)) if x]
        elif op==22:
            nv=struct.unpack_from(">i",out,data)[0]
            vo=be32(out,data+4)
            regions.append((vo,nv))
            p=be32(out,data+8)
            if p: gdls.append(p)

    gs = sorted(set(gdls))
    for i,g in enumerate(gs):
        end = gs[i+1] if i+1 < len(gs) else D
        o=g
        while o+8<=end:
            w0=be32r(out,o); c=w0>>24
            if c==0x04:
                w1=be32r(out,o+4)
                if (w1>>24)&0xf == 5:
                    off = w1 & 0xFFFFFF
                    total_seg5 += 1; files_with.add(base)
                    hit=False
                    for vo,nv in regions:
                        d = off - vo
                        if 0 <= d < 16*nv and nv>0:
                            vtx_regions[d & 0xf]+=1
                            hit=True; break
                    if hit: inside+=1
                    elif len(outside)<12: outside.append((base, hex(off), [(hex(a),b) for a,b in regions[:8]]))
            o+=8

print("seg5 G_VTX total:", total_seg5, "files:", len(files_with))
print("pointing inside a vertex array (vo+d, d<16nv):", inside)
print("displacement mod 16 histogram:", dict(vtx_regions))
print("outside samples:")
for s in outside: print("  ", s)
