#!/usr/bin/env python3
"""Analyze G_VTX w1 seg-5 usage: do offsets match vertex-array file offsets?
Also: quantify trailing junk after ENDDL within each GDL span (for the converter's
packing decision), and per-opcode record counts across all 512 files."""
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

seg5_offs = Counter()
matched = 0; unmatched = []; total_seg5 = 0
files_with_seg5 = set()
opcounts = Counter()
junk_total = 0; junk_files = 0; tight = 0; loose = 0
nsz_bad = []

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
    seen=set(); stack=[R0]; vtoffs=set(); gdls=[]
    while stack:
        o=stack.pop()
        if o in seen or o>=D: continue
        seen.add(o)
        op=struct.unpack_from(">H",out,o)[0]&0xff
        data=be32(out,o+4)
        opcounts[op]+=1
        if op==4:
            vtoffs.add(be32(out,data+0xC))
            gdls += [x for x in (be32(out,data), be32(out,data+4)) if x]
        elif op==24:
            vtoffs.add(be32(out,data+8)); vtoffs.add(be32(out,data+0x10))
            gdls += [x for x in (be32(out,data), be32(out,data+4)) if x]
        elif op==22:
            vtoffs.add(be32(out,data+4))
            p=be32(out,data+8)
            if p: gdls.append(p)
        for q in (be32(out,o+0xC), be32(out,o+0x14)):
            if q: stack.append(q)
    # GDL spans in visit order = sorted unique offsets
    gs = sorted(set(gdls))
    for i,g in enumerate(gs):
        end = gs[i+1] if i+1 < len(gs) else D
        n=0; en=None
        o=g
        while o+8<=end:
            w0=be32r(out,o); c=w0>>24
            if c==0x04 and ((w1:=be32r(out,o+4))>>24)&0xf == 5:
                off = w1 & 0xFFFFFF
                seg5_offs[off]+=1; total_seg5+=1
                files_with_seg5.add(base)
                if off in vtoffs: matched+=1
                elif len(unmatched)<10: unmatched.append((base, hex(w1), sorted(vtoffs)[:6]))
            if c==0xB8: en=o+8; break
            o+=8; n+=1
        if en is None:
            junk_total += (end-g)
        elif en < end:
            loose += 1; junk_total += end-en
        else:
            tight += 1
    # N64 record size check for op5/op11 trailing bytes
    # (sizes already known from RSZ map; skip here)

print("G_VTX seg5 total:", total_seg5, "in files:", len(files_with_seg5))
print("matched to a vertex-array offset in same file:", matched)
print("unmatched samples:", unmatched[:6])
print("seg5 offset histogram (top 10):", [(hex(k),v) for k,v in seg5_offs.most_common(10)])
print()
print("GDL span tightness: tight=%d loose=%d total_trailing_bytes=%d" % (tight, loose, junk_total))
print()
print("opcode record counts across all files:")
for op in sorted(opcounts): print(f"  op{op}: {opcounts[op]}")
