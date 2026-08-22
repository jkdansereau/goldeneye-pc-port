# Verify every seg-5 VTX offset and SETTIMG w1 target falls inside a modeled object.
import struct, zlib
exec(open("tools_pc/d43_sizes.py").read().split("stats = []")[0])

RSZ = {1:0x10,2:0x1C,4:0x14,8:0x10,9:0x24,10:0x1C,12:0x28,13:0x20,
       15:0x1C,17:0x20,18:0x8,21:0x14,22:0x10,23:2,24:0x20}

def be32(b,o): return struct.unpack_from(">I",b,o)[0]
def off(p):    # vma 0x05xxxxxx -> file offset; non-vma pointers are invalid
    return (p & 0xFFFFFF) if (p >> 24) == 5 else None

def walk(out,D,NS,NT):
    R0n=4*NS+12*NT
    stack=[R0n]; seen=set(); objs=[]; gdls=[]
    while stack:
        o=stack.pop()
        if o in seen or o>=D-23: break
        seen.add(o)
        op=struct.unpack_from(">H",out,o)[0]&0xff
        data=be32(out,o+4)
        objs.append((o,24,"node"))
        rec=off(data)
        if rec is not None and rec<D: objs.append((rec,RSZ[op],"record"))
        if op in (4,24) and rec is not None and rec<D:
            p,s=off(be32(out,rec)),off(be32(out,rec+4))
            v=off(be32(out,(rec+8) if op==24 else (rec+0xC)))
            nv=struct.unpack_from(">h",out,rec+0xC)[0] if op==24 else struct.unpack_from(">H",out,rec+0x10)[0]
            if nv>0 and v is not None and v<D: objs.append((v,16*nv,"verts"))
            if p is not None: gdls.append(p)
            if s is not None: gdls.append(s)
            if op==24:
                nc=struct.unpack_from(">h",out,rec+0xE)[0]
                cv=off(be32(out,rec+0x10)); pu=off(be32(out,rec+0x14))
                if nc>0 and cv is not None and cv<D: objs.append((cv,16*nc,"collverts"))
                if nv>0 and pu is not None and pu<D: objs.append((pu,2*nv,"pointusage"))
        elif op==22 and rec is not None and rec<D:
            nv=struct.unpack_from(">i",out,rec)[0]; v=off(be32(out,rec+4))
            p=off(be32(out,rec+8))
            if nv>0 and v is not None and v<D: objs.append((v,16*nv,"verts"))
            if p is not None: gdls.append(p)
        for q in (be32(out,o+0x14), be32(out,o+0xC)):
            x=off(q)
            if x is not None and x<D-23: stack.append(x)
    return sorted(set(gdls)), objs

bad_vtx=0; bad_settimg=0; tot_vtx=0; tot_settimg=0; files=0
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
    if not key: continue
    try: out=zlib.decompress(rom[addr:addr+size][2:],-15)
    except Exception: continue
    D=len(out); NS,NT=nsnt[key]
    gs,objs=walk(out,D,NS,NT)
    for i,g in enumerate(gs):
        e=gs[i+1] if i+1<len(gs) else D
        o=g
        while o+8<=e:
            w0=be32(out,o); t=w0>>24
            w1=be32(out,o+4)
            top=(w1>>24)&0xff
            if t==4 and top==5:
                tot_vtx+=1
                x=w1&0xffffff
                if not any(s<=x<s+n for s,n,_ in objs):
                    bad_vtx+=1
                    if bad_vtx<6: print("VTX outside object:",base,hex(g),hex(o),hex(w1))
            elif t==0xFD and top==5:
                tot_settimg+=1
                x=w1&0xffffff
                if not any(s<=x<s+n for s,n,_ in objs):
                    bad_settimg+=1
                    if bad_settimg<6: print("SETTIMG outside object:",base,hex(g),hex(o),hex(w1))
            o+=8
    files+=1
print(f"files={files} vtx_seg5={tot_vtx} (outside objs: {bad_vtx})  settimg={tot_settimg} (outside objs: {bad_settimg})")
