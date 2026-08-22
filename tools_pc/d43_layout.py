# D43: full layout analysis of all model files with CORRECT N64 sizes.
# node = 24B; records per opcode; vertex/coll arrays; GDLs. Report gaps/overlaps.
import csv, struct, zlib, os, re
from collections import Counter

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
def be32raw(b, o): return struct.unpack_from(">I", b, o)[0]

gapstats = Counter(); problems = []
nfiles = 0; totalnodes = 0; opcodes = Counter()
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
    if not key: problems.append((base,"nohdr")); continue
    NS,NT = nsnt[key]
    try: out = zlib.decompress(rom[addr:addr+size][2:], -15)
    except Exception: problems.append((base,"decomp")); continue
    D = len(out); R0 = 4*NS + 12*NT; nfiles += 1
    stack=[R0]; seen=set(); objs=[]
    while stack:
        o=stack.pop()
        if o in seen or o>=D-23: problems.append((base,"badnode %X"%o)); break
        seen.add(o)
        op=struct.unpack_from(">H",out,o)[0]&0xff
        opcodes[op]+=1
        data=be32(out,o+4)
        objs.append((o,o+24,"node"))
        if op in RSZ:
            rsz = RSZ[op]
            if data + rsz > D: problems.append((base,"rec %X+%X>D"%(data,rsz)))
            else: objs.append((data,data+rsz,"rec%d"%op))
        if op==22:
            nv=struct.unpack_from(">i",out,data)[0]
            v=be32(out,data+4)
            p=be32(out,data+8)
            if p: objs.append((p,None,"gdl"))
            if v and nv>0: objs.append((v,v+16*nv,"vtx22"))
        if op==4:
            p=be32(out,data); s=be32(out,data+4)
            v=be32(out,data+0xC)
            if p: objs.append((p,None,"gdl"))
            if s: objs.append((s,None,"gdl"))
            if v: objs.append((v,None,"vtx?"))   # size unknown (numVertices may be 0)
        elif op==24:
            p=be32(out,data); s=be32(out,data+4)
            v=be32(out,data+8); nv=be16(out,data+0xC)
            cv=be32(out,data+0x10); nc=be16(out,data+0xE)
            pu=be32(out,data+0x14)
            if p: objs.append((p,None,"gdl"))
            if s: objs.append((s,None,"gdl"))
            if v and nv: objs.append((v,v+16*nv,"vtx"))
            if cv and nc: objs.append((cv,cv+16*nc,"coll"))
            if pu: objs.append((pu,None,"pointusage?"))
        for q in (be32(out,o+0x14), be32(out,o+0xC)):
            if q: stack.append(q)
    totalnodes += len(seen)
    # resolve open-ended objects using sorted starts
    known = sorted(set(s for s,e,k in objs if e is not None))
    for i,(s,e,k) in enumerate(objs):
        if e is None:
            nxt = [x for x in known if x > s]
            objs[i] = (s, min(nxt) if nxt else D, k)
    ivs = sorted((s,e) for s,e,k in [(0,R0,"hdr")]+objs)
    pos = 0
    for s,e in ivs:
        if s < pos: problems.append((base,"overlap %X<%X"%(s,pos))); break
        if s > pos:
            gapstats[(s-pos)] += 1
            if len(problems) < 40:
                problems.append((base, "gap %X..%X (%d B)" % (pos, s, s - pos)))
            break
        pos = e
    else:
        if pos != D: problems.append((base,"tail gap %X!=D=%X"%(pos,D)))

print("files:",nfiles,"nodes:",totalnodes)
print("opcodes:",dict(sorted(opcodes.items())))
print("gap-size histogram (first gap per file):",dict(sorted(gapstats.items())[:20]))
print("problems:",len(problems))
for p in problems[:40]: print("  ",p)
