# D43: for every seg-5 VTX/MOVEMEM/MTX/DL reference in every GDL, check whether
# the offset falls inside a known vertex/coll array or elsewhere.
import csv, struct, zlib, os, re
from collections import Counter

ROM = r"data/ge007.ntsc-final.z64"
rom = open(ROM, "rb").read()
rows = list(csv.reader(open(r"scripts/filelist.u.csv")))
RSZ={1:0x10,2:0x1C,4:0x14,8:0x10,9:0x24,10:0x1C,18:0x08,21:0x14,23:0x02,24:0x20}
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

where = Counter(); examples = []
nfiles=0
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
    D = len(out); R0 = 4*NS + 12*NT; nfiles+=1
    stack=[R0]; seen=set(); gdls=[]; arrays=[]
    while stack:
        o=stack.pop()
        if o in seen or o>=D: break
        seen.add(o)
        op=struct.unpack_from(">H",out,o)[0]&0xff
        data=be32(out,o+4); ch=be32(out,o+0x14)
        if op==4:
            p=be32(out,data); s=be32(out,data+4)
            if p: gdls.append(p)
            if s: gdls.append(s)
            v=be32(out,data+0xC); nv=be16(out,data+0x10)
            if v: arrays.append((v, v+16*nv, "vtx(op4)"))
        elif op==24:
            p=be32(out,data); s=be32(out,data+4)
            if p: gdls.append(p)
            if s: gdls.append(s)
            v=be32(out,data+8); nv=be16(out,data+0xC)
            cv=be32(out,data+0x10); nc=be16(out,data+0xE)
            if v: arrays.append((v, v+16*nv, "vtx(op24)"))
            if cv: arrays.append((cv, cv+16*nc, "coll(op24)"))
        for q in (ch, be32(out,o+0xC)):
            if q: stack.append(q)
    for g in set(gdls):
        i=g; n=0
        while i+8 <= D and n < 100000:
            w0=be32raw(out,i); w1=be32raw(out,i+4)
            c=w0>>24
            if c in (1,3,4,6,7) and ((w1>>24)&0xff)==5:
                off = w1 & 0xFFFFFF
                hit=None
                for a,b,k in arrays:
                    if a <= off < b: hit=k; break
                where[(c,hit)] += 1
                if hit is None and len(examples) < 8:
                    examples.append((base, c, hex(off)))
            if c==0xB8: break
            i+=8; n+=1
print("files:",nfiles)
print("(opcode, region) for seg-5 refs:")
for (c,hit),cnt in sorted(where.items(), key=lambda x:(x[0][0],str(x[0][1]))):
    print("  c=%02X %-10s x%d" % (c, str(hit), cnt))
print("non-array examples:", examples)
