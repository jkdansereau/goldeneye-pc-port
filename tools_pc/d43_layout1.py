import csv, struct, zlib, os, re, sys
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
flt = sys.argv[1]
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
    print("== %s NS=%d NT=%d R0=%X D=%X" % (base,NS,NT,R0,D))
    stack=[R0]; seen=set(); objs=[]
    while stack:
        o=stack.pop()
        if o in seen or o>=D-23: break
        seen.add(o)
        op=struct.unpack_from(">H",out,o)[0]&0xff
        data=be32(out,o+4)
        objs.append((o,o+24,"node%d"%op))
        if op in RSZ: objs.append((data,data+RSZ[op],"rec%d"%op))
        if op==4:
            p=be32(out,data); s=be32(out,data+4); v=be32(out,data+0xC)
            if p: objs.append((p,None,"gdlP"))
            if s: objs.append((s,None,"gdlS"))
            if v: objs.append((v,None,"vtx?"))
        elif op==24:
            p=be32(out,data); s=be32(out,data+4)
            v=be32(out,data+8); nv=be16(out,data+0xC)
            cv=be32(out,data+0x10); nc=be16(out,data+0xE)
            pu=be32(out,data+0x14)
            if p: objs.append((p,None,"gdlP"))
            if s: objs.append((s,None,"gdlS"))
            if v and nv: objs.append((v,v+16*nv,"vtx(%d)"%nv))
            if cv and nc: objs.append((cv,cv+16*nc,"coll(%d)"%nc))
            if pu: objs.append((pu,None,"pointusage?"))
        for q in (be32(out,o+0x14), be32(out,o+0xC)):
            if q: stack.append(q)
    known = sorted(set(s for s,e,k in objs if e is not None))
    res=[]
    for (s,e,k) in objs:
        if e is None:
            nxt=[x for x in known if x>s]
            e=min(nxt) if nxt else D
        res.append((s,e,k))
    res.sort()
    pos=0
    for s,e,k in res:
        mark = ""
        if s<pos: mark=" <<< OVERLAP (prev ends %X)"%pos
        elif s>pos: mark=" <<< GAP %d B"% (s-pos)
        print("  %05X..%05X %-14s%s" % (s,e,k,mark))
        pos=max(pos,e)
    break
