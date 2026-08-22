# D43: for every op24 record, compare PointUsage byte extent (to next owned object)
# against numVertices*2 and other candidates.
import csv, struct, zlib, os, re
ROM = r"data/ge007.ntsc-final.z64"
rom = open(ROM, "rb").read()
rows = list(csv.reader(open(r"scripts/filelist.u.csv")))
RSZ = {1:0x10, 2:0x1C, 4:0x14, 8:0x10, 9:0x24, 10:0x1C, 12:0x28, 13:0x20, 15:0x1C, 17:0x20, 18:0x8, 21:0x14, 22:0x10, 23:0x2, 24:0x20}
hdrfiles = set()
for root, dirs, files in os.walk("assets"):
    for fn in files:
        if fn.lower().endswith("modelfileheader.inc.c"): hdrfiles.add(os.path.join(root, fn))
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
def be16(b, o): return struct.unpack_from(">H", b, o)[0]
from collections import Counter
rel = Counter(); examples=[]
nrec=0
for r in rows:
    if len(r) < 3 or not r[2]: continue
    base = r[2].rsplit("/",1)[-1]
    if base.endswith(".bin"): base = base[:-4]
    if not (base[0] in "CGP" and base.endswith("Z") and "_stan" not in base): continue
    try: addr,size = int(r[0]),int(r[1])
    except ValueError: continue
    key=None
    for cand in (base, base[1:], base[:-1], base[1:-1]):
        if cand in nsnt: key=cand; break
    if not key: continue
    NS,NT = nsnt[key]
    try: out = zlib.decompress(rom[addr:addr+size][2:], -15)
    except Exception: continue
    D = len(out); R0 = 4*NS + 12*NT
    stack=[R0]; seen=set(); objs=[]; recs=[]
    while stack:
        o=stack.pop()
        if o in seen or o>=D-23: break
        seen.add(o)
        op=struct.unpack_from(">H",out,o)[0]&0xff
        data=be32(out,o+4)
        objs.append((o,o+24,"node"))
        if op in RSZ: objs.append((data,data+RSZ[op],"rec"))
        if op==4:
            p=be32(out,data); s=be32(out,data+4); v=be32(out,data+0xC)
            if p: objs.append((p,None,"gdl"))
            if s: objs.append((s,None,"gdl"))
            if v: objs.append((v,None,"vtx"))
        elif op==24:
            p=be32(out,data); s=be32(out,data+4)
            v=be32(out,data+8); nv=be16(out,data+0xC)
            nc=be16(out,data+0xE); cv=be32(out,data+0x10)
            pu=be32(out,data+0x14)
            if p: objs.append((p,None,"gdl"))
            if s: objs.append((s,None,"gdl"))
            if v and nv: objs.append((v,v+16*nv,"vtx"))
            if cv and nc: objs.append((cv,cv+16*nc,"coll"))
            if pu: objs.append((pu,None,"pu"))
            recs.append((base,pu,nv,nc))
        for q in (be32(out,o+0x14), be32(out,o+0xC)):
            if q: stack.append(q)
    known = sorted(set(s for s,e,k in objs if e is not None))
    ends={}
    for (s,e,k) in objs:
        if e is None:
            nxt=[x for x in known if x>s]
            e=min(nxt) if nxt else D
        ends[s]=e
    for (base,pu,nv,nc) in recs:
        nrec+=1
        ext = ends[pu]-pu
        rel[ext - 2*nv]+=1
        if ext != 2*nv and len(examples)<10:
            examples.append((base,hex(pu),ext,nv,nc))
print("op24 records:",nrec)
print("(PointUsage extent - 2*numVertices) histogram:")
for k,c in sorted(rel.items()): print("  %+d x%d" % (k,c))
print("mismatch examples (base, pu, extent, nv, nc):")
for e in examples: print("  ",e)
