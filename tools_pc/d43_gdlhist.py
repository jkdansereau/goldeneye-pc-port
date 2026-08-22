# D43: histogram of (opcode, w1 top byte) across ALL model GDLs, plus per-file
# max GDL extent checks. Also verifies every GDL ends in ENDDL (0xB8).
import csv, struct, zlib, os, re
from collections import Counter

ROM = r"data/ge007.ntsc-final.z64"
rom = open(ROM, "rb").read()
rows = list(csv.reader(open(r"scripts/filelist.u.csv")))
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
def be32raw(b, o): return struct.unpack_from(">I", b, o)[0]

hist = Counter(); seg5 = Counter(); problems = []
nfiles = 0; ngdl = 0; maxslots = 0
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
    D = len(out); R0 = 4*NS + 12*NT
    stack=[R0]; seen=set(); gdls=[]
    while stack:
        o=stack.pop()
        if o in seen or o>=D: problems.append((base,"badnode")); break
        seen.add(o)
        op=struct.unpack_from(">H",out,o)[0]&0xff
        data=be32(out,o+4)
        ch=be32(out,o+0x14)
        if op in (4,24):
            p=be32(out,data); s=be32(out,data+4)
            if p: gdls.append(p)
            if s: gdls.append(s)
        # Child+Next only: matches modelIterateDisplayLists reachability
        for q in (ch, be32(out,o+0xC)):
            if q: stack.append(q)
    nfiles += 1
    for g in set(gdls):
        ngdl += 1
        i=g; n=0; ended=False
        while i+8 <= D and n < 100000:
            w0=be32raw(out,i); w1=be32raw(out,i+4)
            c=w0>>24
            hist[(c,(w1>>24)&0xff)] += 1
            if (w1>>24)&0xff == 5 and c in (1,3,4,6,7,0xfd,0xfe,0xff):
                seg5[(c,w1&0xffffff)] += 1
            if c==0xB8: ended=True; break
            i+=8; n+=1
        maxslots=max(maxslots,n)
        if not ended: problems.append((base,"gdl %X no ENDDL (n=%d)"%(g,n)))
print("files:",nfiles,"gdls:",ngdl,"max slots in one gdl:",maxslots)
print("\n(opcode, w1 top byte) histogram:")
for (c,tb),cnt in sorted(hist.items()):
    print("  c=%02X seg=%02X x%d" % (c,tb,cnt))
print("\nseg-5 address commands (opcode, offset):")
for (c,off),cnt in sorted(seg5.items()):
    print("  c=%02X off=%06X x%d" % (c,off,cnt))
print("\nproblems:",len(problems))
for p in problems[:10]: print("  ",p)
