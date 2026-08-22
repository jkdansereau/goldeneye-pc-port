import csv, struct, sys, zlib, os, re
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
    print("== %s D=0x%X NS=%d NT=%d R0=0x%X" % (base,D,NS,NT,R0))
    seen=set()
    def show(o, depth):
        if o in seen or o>=D: return
        seen.add(o)
        op=struct.unpack_from(">H",out,o)[0]&0xff
        data=be32(out,o+4); par=be32(out,o+8); nxt=be32(out,o+0xC); prv=be32(out,o+0x10); ch=be32(out,o+0x14)
        print("  %snode op=%-2d @%05X data=%05X par=%05X next=%05X prev=%05X child=%05X" % (" "*depth, op, o, data, par, nxt, prv, ch))
        if data < D:
            # print first 12 words of the record
            ws=[be32(out,data+4*k) for k in range(min(6,(D-data)//4))]
            print("      rec: "+ " ".join("%06X"%w for w in ws))
        if ch: show(ch, depth+1)
        if nxt: show(nxt, depth+1)
    show(R0, 0)
    break
