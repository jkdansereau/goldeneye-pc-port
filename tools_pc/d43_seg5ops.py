#!/usr/bin/env python3
"""Dump opcode + words for every seg-5 w1 slot in model GDLs, grouped by opcode."""
import csv, struct, zlib, os, re
from collections import Counter, defaultdict

ROM = open('data/ge007.ntsc-final.z64','rb').read()
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

MASK = 0xFFFFFF
def be32o(b,o): return struct.unpack_from(">I",b,o)[0] & MASK
def be32r(b,o): return struct.unpack_from(">I",b,o)[0]

by_op = defaultdict(Counter)     # opcode -> Counter of w1 values (seg5 only)
inrange = Counter(); outrange = Counter()
samples = defaultdict(list)
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
    try: src=zlib.decompress(ROM[addr:addr+size][2:],-15)
    except Exception: continue
    D=len(src); R0=4*NS+12*NT
    seen=set(); stack=[R0]; gdls=[]
    while stack:
        o=stack.pop()
        if o in seen or o>=D: continue
        seen.add(o)
        op=struct.unpack_from(">H",src,o)[0]&0xff
        data=be32o(src,o+4)
        child=be32o(src,o+0x14); nxt=be32o(src,o+0xC)
        if op==8:
            a=be32o(src,data+8)
            if a: stack.append(a)
        elif op==18:
            c=be32o(src,data)
            if c: stack.append(c)
        else:
            if child: stack.append(child)
        if nxt: stack.append(nxt)
        if op in (4,24):
            p=be32o(src,data); s=be32o(src,data+4)
            gdls += [x for x in (p,s) if x]
        elif op==22:
            p=be32o(src,data+8)
            if p: gdls.append(p)
    for g in sorted(set(gdls)):
        o=g
        while o+8<=D:
            w0=be32r(src,o); c=w0>>24
            if c==0xB8: break
            w1=be32r(src,o+4)
            if (w1>>28)==5:
                off=w1&MASK
                by_op[c][off]+=1
                if off < D: inrange[c]+=1
                else: outrange[c]+=1
                if len(samples[c])<6 and off>=D: samples[c].append((base,hex(o),hex(w0),hex(w1)))
            o+=8

print("opcode -> (in-range count, out-of-range count):")
for c in sorted(set(list(inrange)+list(outrange))):
    print(f"  0x{c:02X}: in={inrange[c]} out={outrange[c]}  distinct_vals={len(by_op[c])}")
print()
for c,v in samples.items():
    print(f"samples opcode 0x{c:02X} (file, slot_off, w0, w1):")
    for s in v: print("   ", s)
