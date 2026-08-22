# D43: full ownership tiling check per file.
# objects: switches[0,4NS) textures[4NS,4NS+12NT) nodes(20B) records(RSZ)
#          vtx(nv*16) coll(nc*16) gdl(up to next gdl start or D)
# PointUsage: [puo, min(next owned start))  -- verify even & >0
# then verify union == [0,D) exactly (no gaps/overlaps).
import csv, struct, zlib, os, re
from collections import Counter

ROM = r"data/ge007.ntsc-final.z64"
rom = open(ROM, "rb").read()
rows = list(csv.reader(open(r"scripts/filelist.u.csv")))
RSZ={1:0x10,2:0x1C,4:0x14,8:0x10,9:0x24,10:0x1C,18:0x08,21:0x14,23:0x02,24:0x20}

hdrfiles=set()
for root,dirs,files in os.walk("assets"):
    for fn in files:
        if fn.lower().endswith("modelfileheader.inc.c"): hdrfiles.add(os.path.join(root,fn))
nsnt={}
for f in sorted(hdrfiles):
    for line in open(f):
        m=re.search(r"MODELFILEHEADER\((.*)\)\s*;?\s*$",line)
        if not m: continue
        a=[x.strip() for x in m.group(1).split(",")]
        if len(a)<9: continue
        try: nsnt[a[0]]=(int(a[4],0),int(a[8],0))
        except ValueError: pass

def be16(b,o): return struct.unpack_from(">H",b,o)[0]
def be32(b,o): return struct.unpack_from(">I",b,o)[0]&0xFFFFFF

fails=[]; pu_sizes=Counter(); nfiles=0; gapfiles=[]
for r in rows:
    if len(r)<3 or not r[2]: continue
    base=r[2].rsplit("/",1)[-1]
    if base.endswith(".bin"): base=base[:-4]
    if not (base[0] in "CGP" and base.endswith("Z") and "_stan" not in base): continue
    try: addr,size=int(r[0]),int(r[1])
    except ValueError: continue
    key=None
    for cand in (base,base[1:],base[:-1],base[1:-1]):
        if cand in nsnt: key=cand; break
    if not key: fails.append((base,"no header")); continue
    NS,NT=nsnt[key]
    try: out=zlib.decompress(rom[addr:addr+size][2:],-15)
    except Exception: fails.append((base,"decomp")); continue
    D=len(out); nfiles+=1
    R0=4*NS+12*NT

    objs=[]   # (start,end,kind)
    pu_starts=[]
    stack=[R0]; nodeset=set()
    while stack:
        o=stack.pop()
        if o in nodeset or o>=D: fails.append((base,"bad node %X"%o)); break
        nodeset.add(o)
        op=be16(out,o)&0xff
        data=be32(out,o+4); child=be32(out,o+20)
        objs.append((o,o+20,"node"))
        rsz=RSZ.get(op,0)
        if not rsz or data+rsz>D: fails.append((base,"bad rec op%d"%op)); break
        objs.append((data,data+rsz,"rec"))
        if op==4:
            for f_off in (0,4):
                v=be32(out,data+f_off)
                if v: objs.append((v,None,"gdl"))
            v=be32(out,data+0xC); nv=be16(out,data+0x10)
            if v: objs.append((v,v+16*nv,"vtx"))
        elif op==24:
            for f_off in (0,4):
                v=be32(out,data+f_off)
                if v: objs.append((v,None,"gdl"))
            v=be32(out,data+8); nv=be16(out,data+0xC); nc=be16(out,data+0xE)
            cv=be32(out,data+0x10); puo=be32(out,data+0x14)
            if v: objs.append((v,v+16*nv,"vtx"))
            if cv: objs.append((cv,cv+16*nc,"coll"))
            if puo: pu_starts.append(puo)
        if child: stack.append(child)
    else:
        # gdl extents: sorted gdl starts, each extends to next gdl start or D
        gs=sorted(set(s for s,e,k in objs if k=="gdl"))
        fixed=[]
        for i,(s,e,k) in enumerate(objs):
            if k=="gdl":
                end = gs[gs.index(s)+1] if gs.index(s)+1<len(gs) else D
                fixed.append((s,end,"gdl"))
            else: fixed.append((s,e,k))
        objs=fixed
        # pointusage: [puo, min next start)
        starts=sorted(set(s for s,e,k in objs))
        for puo in pu_starts:
            nxt=[s for s in starts if s>puo]
            end=min(nxt) if nxt else D
            sz=end-puo
            if sz<=0 or sz%2: fails.append((base,"pu bad size %d @%X"%(sz,puo))); break
            objs.append((puo,end,"pu"))
            pu_sizes[sz]+=1
        else:
            # tiling check
            ivs=sorted((s,e) for s,e,k in objs)
            ok=True; pos=0
            for s,e in ivs:
                if s<pos: fails.append((base,"overlap @%X (pos %X)"%(s,pos))); ok=False; break
                if s>pos: gapfiles.append((base,hex(pos),hex(s))); ok=False; break
                pos=e
            if ok and pos!=D: fails.append((base,"short end %X vs D=%X"%(pos,D)))

print("files:",nfiles,"fails:",len(fails))
for f in fails[:15]: print("  ",f)
print("gap files:",len(gapfiles))
for g in gapfiles[:10]: print("   ",g)
print("\nPointUsage sizes:",dict(sorted(pu_sizes.items())))
