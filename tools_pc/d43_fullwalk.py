# D43: definitive N64 model-file layout analysis.
# Walks nodes via Child(+0x14) AND Next(+0x0C). Records all objects with sizes.
# Checks: full tiling [0,D), GDL contiguity at tail, node/record invariants.
import csv, struct, zlib, os, re, sys
from collections import Counter

ROM = r"data/ge007.ntsc-final.z64"
rom = open(ROM, "rb").read()
rows = list(csv.reader(open(r"scripts/filelist.u.csv")))

# N64 record sizes per opcode (bondtypes.h, verified: no rec-overflow in 512 files)
RSZ = {1:0x10, 2:0x1C, 3:0x1C, 4:0x14, 5:0x1AC, 6:0x18, 7:0x1B0, 8:0x10,
       9:0x24, 10:0x1C, 11:0x50, 12:0x28, 13:0x20, 14:0x10, 15:0x1C,
       16:0x18, 17:0x20, 18:0x08, 21:0x14, 22:0x10, 23:0x02, 24:0x20}

hdrfiles = set()
for root, dirs, files in os.walk("assets"):
    for fn in files:
        if fn.lower().endswith("modelfileheader.inc.c"):
            hdrfiles.add(os.path.join(root, fn))
nsnt = {}
for f in sorted(hdrfiles):
    for line in open(f):
        m = re.search(r"MODELFILEHEADER\((.*)\)\s*;?\s*$", line)
        if not m: continue
        a = [x.strip() for x in m.group(1).split(",")]
        if len(a) < 9: continue
        try: nsnt[a[0]] = (int(a[4], 0), int(a[8], 0))
        except ValueError: pass

def be16(b, o): return struct.unpack_from(">H", b, o)[0]
def be32(b, o): return struct.unpack_from(">I", b, o)[0] & 0xFFFFFF
def be32s(b, o):
    v = struct.unpack_from(">i", b, o)[0]
    return v & 0xFFFFFF if v >= 0 else -v

nfiles = 0; fails = []; gaphist = Counter(); gdlnotatend = []
nodecount_total = 0; sibling_only = 0
onlychild_counts = Counter()

for r in rows:
    if len(r) < 3 or not r[2]: continue
    base = r[2].rsplit("/",1)[-1]
    if base.endswith(".bin"): base = base[:-4]
    if not (base[0] in "CGP" and base.endswith("Z") and "_stan" not in base): continue
    try: addr,size = int(r[0]),int(r[1])
    except ValueError: continue
    key = None
    for cand in (base, base[1:], base[:-1], base[1:-1]):
        if cand in nsnt: key = cand; break
    if not key: fails.append((base,"nohdr")); continue
    NS,NT = nsnt[key]
    try: out = zlib.decompress(rom[addr:addr+size][2:], -15)
    except Exception: fails.append((base,"decomp")); continue
    D = len(out); nfiles += 1
    R0 = 4*NS + 12*NT

    # --- walk nodes: Child + Next ---
    objs = [(0, R0, "hdr")]
    stack = [(R0, 0)]; seen = set()
    while stack:
        o, via = stack.pop()
        if o in seen or not (0 <= o < D): fails.append((base,f"badnode {o:#x}")); break
        seen.add(o)
        op = be16(out, o) & 0xff
        data = be32(out, o+4)
        child = be32(out, o+0x14); nxt = be32(out, o+0xC)
        objs.append((o, o+24, "node"))
        rsz = RSZ.get(op, 0)
        if not rsz: fails.append((base,f"noRSZ op{op} @ {o:#x}")); break
        if data + rsz > D: fails.append((base,f"recovf op{op} {data:#x}+{rsz:#x}>{D:#x}")); break
        objs.append((data, data+rsz, f"rec{op}"))
        # per-opcode sub-objects
        if op == 4:      # DL
            p = be32(out, data); s = be32(out, data+4)
            v = be32(out, data+0xC); nv = be16(out, data+0x10)
            if p: objs.append((p, None, "gdl"))
            if s: objs.append((s, None, "gdl"))
            if v and nv: objs.append((v, v+16*nv, "vtx"))
        elif op == 24:   # DLCOLLISION
            p = be32(out, data); s = be32(out, data+4)
            v = be32(out, data+8); nv = be16(out, data+0xC); nc = be16(out, data+0xE)
            cv = be32(out, data+0x10); puo = be32(out, data+0x14)
            if p: objs.append((p, None, "gdl"))
            if s: objs.append((s, None, "gdl"))
            if v and nv: objs.append((v, v+16*nv, "vtx"))
            if cv and nc: objs.append((cv, cv+16*nc, "coll"))
            if puo: objs.append((puo, None, "pu"))
        elif op == 22:   # DLPRIMARY
            nv = be32s(out, data)
            v = be32(out, data+4); p = be32(out, data+8)
            if p: objs.append((p, None, "gdl"))
            if v and nv > 0: objs.append((v, v+16*nv, "vtx22"))
        elif op == 9:    # BSP: check for embedded pointers? (size 0x24)
            pass
        if child: stack.append((child, 1))
        if nxt: stack.append((nxt, 2))
    else:
        nodecount_total += len(seen)
        # count nodes not reachable as anyone's Child (sibling-only, excl. root)
        childtargets = set()
        for o in seen:
            c = be32(out, o+0x14)
            if c: childtargets.add(c)
        sibling_only += len(seen - childtargets - {R0})
        # resolve open-ended (gdl/pu): sorted starts
        starts = sorted(set(s for s,e,k in objs))
        fixed = []
        for (s,e,k) in objs:
            if e is None:
                nxts = [x for x in starts if x > s]
                fixed.append((s, min(nxts) if nxts else D, k))
            else:
                fixed.append((s,e,k))
        # tiling
        ivs = sorted((s,e) for s,e,k in fixed)
        pos = 0; gaps = []
        for s,e in ivs:
            if s < pos: fails.append((base,f"overlap {s:#x}<pos{pos:#x}")); break
            if s > pos: gaps.append(s-pos)
            pos = e
        else:
            if pos != D: fails.append((base,f"tail {pos:#x}!={D:#x}"))
            for g in gaps: gaphist[g] += 1
            # GDL tail contiguity: all gdl objs must form one contiguous run ending at D
            gs = sorted(set(s for s,e,k in fixed if k=="gdl"))
            if gs:
                # check union of gdl intervals == [min(gs), D) with no non-gdl inside
                merged = []
                for s,e,k in sorted(fixed):
                    if k == "gdl":
                        if merged and s <= merged[-1][1]: merged[-1] = (merged[-1][0], max(merged[-1][1], e))
                        else: merged.append([s,e])
                if len(merged) > 1 or merged[0][1] != D:
                    gdlnotatend.append((base, [f"{a:#x}-{b:#x}" for a,b in merged]))

print(f"files: {nfiles}  fails: {len(fails)}")
for f in fails[:20]: print("   ", f)
print(f"total nodes (Child+Next walk): {nodecount_total}")
print(f"nodes reached only via Next (not first sibling): {sibling_only}")
print(f"gap-size histogram: {dict(sorted(gaphist.items()))}")
print(f"GDL-not-single-tail-run files: {len(gdlnotatend)}")
for b, m in gdlnotatend[:10]: print("   ", b, m)
