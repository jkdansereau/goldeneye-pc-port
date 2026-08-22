import csv, struct, zlib, os, re, sys
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
flt = sys.argv[1] if len(sys.argv) > 1 else "CboilerbondZ"
for r in rows:
    if len(r) < 3 or not r[2]: continue
    base = r[2].rsplit("/",1)[-1]
    if base.endswith(".bin"): base = base[:-4]
    if flt.lower() not in base.lower(): continue
    key=None
    for cand in (base, base[1:], base[:-1], base[1:-1]):
        if cand in nsnt: key=cand; break
    if not key: continue
    NS,NT = nsnt[key]
    addr,size=int(r[0]),int(r[1])
    out=zlib.decompress(rom[addr:addr+size][2:],-15)
    D=len(out); R0=4*NS+12*NT
    stack=[R0]; seen=set(); gdls=[]
    while stack:
        o=stack.pop()
        if o in seen or o>=D: break
        seen.add(o)
        op=struct.unpack_from(">H",out,o)[0]&0xff
        data=be32(out,o+4); ch=be32(out,o+0x14)
        if op in (4,24):
            p=be32(out,data); s=be32(out,data+4)
            if p: gdls.append((p,"P",o))
            if s: gdls.append((s,"S",o))
        for q in (ch, be32(out,o+0xC)):
            if q: stack.append(q)
    print("== %s D=0x%X gdls=%d" % (base,D,len(gdls)))
    g = min(x[0] for x in gdls)
    i=g; n=0
    while i+8 <= D and n < 120:
        w0=be32raw(out,i); w1=be32raw(out,i+4)
        c=w0>>24
        print("  [%3d] c=%02X w0=%08X w1=%08X" % (n,c,w0,w1))
        if c==0xB8: break
        i+=8; n+=1
    break
