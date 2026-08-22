# D43: EXACT modelIterateDisplayLists visit-order simulation (mutable nodes,
# LOD/SWITCH rewiring, BSP sibling splice). Verify per file that consecutive
# visited GDLs are offset-adjacent (count == cur size), i.e. a converter may
# emit GDLs in visit order and compaction will work.
import csv, struct, zlib, os, re

ROM=r"data/ge007.ntsc-final.z64"
rom=open(ROM,"rb").read()
rows=list(csv.reader(open(r"scripts/filelist.u.csv")))

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

class N:  # mutable node
    __slots__=("op","data","parent","next","prev","child")

bad=0; total=0; gdls_total=0
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
    NS,NT=nsnt[key]
    try: out=zlib.decompress(rom[addr:addr+size][2:],-15)
    except Exception: continue
    D=len(out); R0=4*NS+12*NT; total+=1

    nodes={}
    stack=[R0]
    while stack:
        o=stack.pop()
        if o in nodes: continue
        n=N(); n.op=be16(out,o)&0xff; n.data=be32(out,o+4)
        n.parent=be32(out,o+8); n.next=be32(out,o+0xC); n.prev=be32(out,o+0x10); n.child=be32(out,o+0x14)
        nodes[o]=n
        # Child+Next closure (mutable walk reaches siblings via Next), plus
        # LOD.Affects / SWITCH.Controls targets in case they lie outside it.
        for q in (n.child, n.next):
            if q: stack.append(q)
        if n.op==8:
            q=be32(out,n.data+8)
            if q: stack.append(q)
        elif n.op==18:
            q=be32(out,n.data+0)
            if q: stack.append(q)

    def gdl_of(n):
        if n.op==4 or n.op==24:
            p=be32(out,n.data+0); s=be32(out,n.data+4)
            return (p,s)
        if n.op==22:  # DLPRIMARY N64: numVertices s32@0, Vertices@4, Primary@8, BaseAddr@0xC
            return (be32(out,n.data+8), 0)
        return (0,0)

    # full visit sequence: iterate the function to exhaustion
    seq=[]
    node=R0; prev_node=None; prev_gdl=0; guard=0
    while node and guard<200000:
        guard+=1
        n=nodes[node]
        gdl=0
        if n.op in (4,22,24):
            p,s=gdl_of(n)
            if node!=prev_node:
                gdl=p
            elif s and s!=prev_gdl:
                gdl=s
        elif n.op==8:   # LOD rewire
            aff=be32(out,n.data+8)
            n.child=aff
        elif n.op==18:  # SWITCH rewire
            ctl=be32(out,n.data+0)
            n.child=ctl
        elif n.op==9:   # BSP splice (visible=TRUE)
            lc=be32(out,n.data+0x18); rc=be32(out,n.data+0x1C)
            node1,node2=lc,rc
            if node1:
                n.child=node1
                nodes[node1].prev=0
                loop=node1
                while nodes[loop].next and nodes[loop].next!=node2:
                    loop=nodes[loop].next
                nodes[loop].next=node2
                if node2:
                    nodes[node2].prev=loop
                    loop=node2
                    while nodes[loop].next and nodes[loop].next!=node1:
                        loop=nodes[loop].next
                    nodes[loop].next=0
            else:
                n.child=node2
                if node2: nodes[node2].prev=0
        if gdl:
            seq.append(gdl)
            prev_node=node; prev_gdl=gdl
            # next call resumes at same node (nodeptr unchanged) -> loop continues with same node
            continue
        prev_node=None
        if n.child:
            node=n.child
        else:
            while node:
                nn=nodes[node].next
                if nn: node=nn; break
                node=nodes[node].parent
    # verification: strictly ascending, no duplicates, each GDL ENDDLs before
    # the next visited start (so a converter may pack them contiguously).
    ok=True; why=""
    for i,g in enumerate(seq):
        end = seq[i+1] if i+1<len(seq) else D
        if i and g <= seq[i-1]: ok=False; why="not ascending @%X"%g; break
        if g in seq[:i]: ok=False; why="dup %X"%g; break
        j=g; n=0; ended=False
        while j+8<=end and n<100000:
            c=struct.unpack_from(">I",out,j)[0]>>24
            if c==0xB8: ended=True; break
            j+=8; n+=1
        if not ended: ok=False; why="no ENDDL in [%X,%X)"%(g,end); break
    if not ok:
        bad+=1
        if bad<=5: print("BAD",base,why,["%X"%v for v in seq[:10]],"D=%X"%D)
    gdls_total+=len(seq)

print("files:",total,"bad:",bad,"total gdls:",gdls_total)
