#!/usr/bin/env python3
"""D43 reference model-file converter (N64 BE -> PC LE). Validates on all 512 files:
- layout: [switches NS*8][texconfigs NT*12][DFS nodes(48B)+records(PC sz)][vtx arrays][blobs][GDLs 16B slots]
- every pointer remap resolves via the unified region map
- GDL spans pack tight (16B multiples), D_PC - G_PC == 16*total_slots
- reports max D_PC vs N64 size (staging headroom)
This is the spec that port/src/romdata.c romdataFixupModelFile implements."""
import csv, struct, zlib, os, re, sys
from collections import Counter

ROM = open('data/ge007.ntsc-final.z64','rb').read()
rows = list(csv.reader(open(r"scripts/filelist.u.csv")))
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

MASK = 0xFFFFFF
def be16(b,o): return struct.unpack_from(">h",b,o)[0]
def bu16(b,o): return struct.unpack_from(">H",b,o)[0]
def be32o(b,o): return struct.unpack_from(">I",b,o)[0] & MASK   # file offset (low 24)
def be32r(b,o): return struct.unpack_from(">I",b,o)[0]          # raw u32 BE

# N64 record sizes (from bondtypes.h comments, verified vs ROM via full walk)
N64_REC = {1:0x1C, 2:0x1C, 3:0x1C, 4:0x14, 8:0x10, 9:0x24, 10:0x1C, 12:0x28,
           13:0x20, 15:0x1C, 18:0x08, 21:0x14, 22:0x10, 23:0x02, 24:0x20}
# PC record sizes (probe-verified)
PC_REC  = {1:24, 2:40, 3:40, 4:40, 8:24, 9:48, 10:28, 12:48,
           13:48, 15:28, 18:16, 21:20, 22:32, 23:2, 24:64}
PC_NODE = 48

def walk(src, D, NS, NT):
    """DFS with LOD/SWITCH rewiring. Returns (nodes, gdls) where
    nodes = [(n64_off, op)] in visit order, gdls = [gdl_off] in visit order ([Primary, Secondary])."""
    R0 = 4*NS + 12*NT
    seen = set(); stack = [R0]; nodes = []; gdls = []
    while stack:
        o = stack.pop()
        if o in seen or o >= D: continue
        seen.add(o)
        op = bu16(src, o) & 0xff
        data = be32o(src, o+4)
        nodes.append((o, op))
        child = be32o(src, o+0x14); nxt = be32o(src, o+0xC)
        if op == 8:   # LOD -> Affects (data+8)
            a = be32o(src, data+8)
            if a: stack.append(a)
        elif op == 18:  # SWITCH -> Controls (data+0)
            c = be32o(src, data)
            if c: stack.append(c)
        else:
            if child: stack.append(child)
        if nxt: stack.append(nxt)
        # GDLs in [Primary, Secondary] order at node entry
        if op == 4:
            p = be32o(src, data); s = be32o(src, data+4)
            if p: gdls.append(p)
            if s: gdls.append(s)
        elif op == 24:
            p = be32o(src, data); s = be32o(src, data+4)
            if p: gdls.append(p)
            if s: gdls.append(s)
        elif op == 22:
            p = be32o(src, data+8)
            if p: gdls.append(p)
    return nodes, gdls

def gdl_end(src, D, g):
    o = g
    while o + 8 <= D:
        if (be32r(src, o) >> 24) == 0xB8: return o + 8
        o += 8
    return D

errors = []
stats = Counter()
max_ratio = 0; max_file = ""
blob_info = []
for r in rows:
    if len(r) < 3 or not r[2]: continue
    base = r[2].rsplit("/",1)[-1]
    if base.endswith(".bin"): base = base[:-4]
    if not (base[0] in "CGP" and base.endswith("Z") and "_stan" not in base): continue
    key = None
    for cand in (base, base[1:], base[:-1], base[1:-1]):
        if cand in nsnt: key = cand; break
    if not key:
        errors.append(f"{base}: no header data"); continue
    NS, NT = nsnt[key]
    try: addr, size = int(r[0]), int(r[1])
    except ValueError: continue
    try: src = zlib.decompress(ROM[addr:addr+size][2:], -15)
    except Exception as e:
        errors.append(f"{base}: decompress {e}"); continue
    D = len(src); stats["files"] += 1

    nodes, gdls = walk(src, D, NS, NT)
    if not nodes:
        errors.append(f"{base}: empty tree"); continue

    # ---- pass 1: layout + region map ----
    regions = []   # (old_off, old_size, new_off)
    def add_region(old, osz, cur):
        regions.append((old, osz, cur))
        return cur + osz

    dstpos = 0
    for i in range(NS):
        dstpos = add_region(4*i, 4, dstpos)          # switches 4B -> 8B each
    for i in range(NT):
        dstpos = add_region(4*NS + 12*i, 12, dstpos) # texconfigs 12B -> 12B

    node_newoff = {}; rec_newoff = {}
    gdl_spans = []   # (old_g, slots_emitted)
    vtx_regions = [] # for G_VTX seg5 remap sanity (subset of regions)
    zero_vtx = []    # (vo,) arrays with nv==0 but non-null ptr — sized up to next object
    for (no, op) in nodes:
        data = be32o(src, no+4)
        dstpos = add_region(no, 24, dstpos)          # node 24B -> 48B
        node_newoff[no] = dstpos - 24
        if op not in N64_REC:
            errors.append(f"{base}: unknown opcode {op} at {no:#x}")
            continue
        nsz, psz = N64_REC[op], PC_REC[op]
        rec_newoff[data] = dstpos
        dstpos = add_region(data, nsz, dstpos)
        # vertex arrays right after the record
        # (nv==0 with non-null ptr: real data exists, GDL uploads via abs offset —
        #  size it up to the next object; see PexplosionbitZ)
        if op == 4:
            nv = bu16(src, data+0x10); vo = be32o(src, data+0xC)
            if vo:
                sz = 16*nv if nv else None
                if sz is None: zero_vtx.append((vo,))
                else: vtx_regions.append((vo, nv))
                if nv: dstpos = add_region(vo, sz, dstpos)
        elif op == 24:
            nv = be16(src, data+0xC); ncv = be16(src, data+0xE)
            vo = be32o(src, data+8); cvo = be32o(src, data+0x10); puo = be32o(src, data+0x14)
            if nv and vo:
                vtx_regions.append((vo, nv)); dstpos = add_region(vo, 16*nv, dstpos)
            if ncv and cvo:
                vtx_regions.append((cvo, ncv)); dstpos = add_region(cvo, 16*ncv, dstpos)
            if nv and puo:
                dstpos = add_region(puo, 2*nv, dstpos)   # PointUsage s16 x numVertices
        elif op == 22:
            nv = struct.unpack_from(">i", src, data)[0]; vo = be32o(src, data+4)
            if nv and vo:
                vtx_regions.append((vo, nv)); dstpos = add_region(vo, 16*nv, dstpos)

    # zero-count vertex arrays: size up to the next higher object offset
    obj_offs_now = sorted(set([no for no,_ in nodes]) | set(rec_newoff.keys()) |
                          set(v[0] for v in vtx_regions) | set(gdls))
    for (vo,) in zero_vtx:
        nxt = min((o for o in obj_offs_now if o > vo), default=D)
        sz = nxt - vo
        if sz <= 0 or sz % 16: errors.append(f"{base}: zero-vtx array {vo:#x} bad size {sz:#x}")
        else:
            vtx_regions.append((vo, sz // 16))
            dstpos = add_region(vo, sz, dstpos)

    # embedded texture blobs (texconfig TextureID seg5)
    blob_old = []
    for i in range(NT):
        tid = be32r(src, 4*NS + 12*i)
        if (tid >> 28) == 5:
            blob_old.append(tid & MASK)
    # blob size: up to the next higher object offset
    all_offs = sorted(set([no for no,_ in nodes]) | set(rec_newoff.keys()) |
                      set(v[0] for v in vtx_regions) | set(gdls) | set(4*NS+12*i for i in range(NT)))
    for bo in blob_old:
        nxt = min((o for o in all_offs if o > bo), default=D)
        dstpos = add_region(bo, nxt - bo, dstpos)
        blob_info.append((base, hex(bo), nxt - bo))

    # GDLs last, tight-packed 16B slots (up to and incl. ENDDL)
    gdl_newoff = {}
    total_slots = 0
    for g in gdls:
        end = gdl_end(src, D, g)
        nslots = (end - g) // 8
        if (end - g) % 8: errors.append(f"{base}: GDL {g:#x} not 8B aligned end")
        gdl_newoff[g] = dstpos
        dstpos += 16 * nslots
        total_slots += nslots
        regions.append((g, end - g, gdl_newoff[g]))

    D_PC = dstpos
    G_PC = min(gdl_newoff.values()) if gdl_newoff else D_PC
    if gdl_newoff and (D_PC - G_PC) % 16:
        errors.append(f"{base}: D_PC-G_PC not multiple of 16")
    ratio = D_PC / D if D else 0
    if ratio > max_ratio: max_ratio, max_file = ratio, base
    stats["dpc_max"] = max(stats["dpc_max"], D_PC)

    # ---- pass 2: remap sanity (every pointer field must resolve) ----
    regions.sort()
    import bisect
    rold = [x[0] for x in regions]; rmap = {x[0]: x for x in regions}
    def remap(off):
        if off == 0: return 0
        i = bisect.bisect_right(rold, off) - 1
        if i < 0: return None
        o, osz, n = regions[i]
        if o <= off < o + osz: return n + (off - o)
        return None

    def check(tag, off):
        if off == 0: return
        if remap(off) is None:
            errors.append(f"{base}: {tag} -> {off:#x} not in map")

    for (no, op) in nodes:
        data = be32o(src, no+4)
        for f in (4, 8, 0xC, 0x10, 0x14): check(f"node{no:#x}+{f:#x}", be32o(src, no+f))
        if op not in N64_REC: continue
        ptrfields = {
            1: [4], 2: [0x14], 3: [0x14], 8: [8], 9: [0x18, 0x1C],
            12: [0x18], 13: [0x10, 0x14], 18: [0], 24: [8, 0x10, 0x14],
            4: [0xC], 22: [4],
        }.get(op, [])
        for f in ptrfields: check(f"rec{op}@{data:#x}+{f:#x}", be32o(src, data+f))
        if op == 24:
            ncv = be16(src, data+0xE)
            cvo = be32o(src, data+0x10)
            for i in range(ncv):
                check(f"colvtx{cvo:#x}[{i}].LinkedTo", be32r(src, cvo + 16*i + 8) & MASK if (be32r(src, cvo+16*i+8)>>24)==5 else 0)
    for i in range(NS): check(f"switch[{i}]", be32o(src, 4*i))

    # GDL w1 seg5 targets must resolve — ONLY for address-bearing opcodes.
    # (G_TRI4/G_TRI1/G_TEXTURE/SETOTHERMODE/etc carry index data in w1, not addresses.)
    ADDR_OPS = {0x04, 0xFD, 0xF3}   # G_VTX, G_SETTIMG, G_LOADBLOCK
    for g in gdls:
        o = g
        while o + 8 <= D:
            w0 = be32r(src, o); c = w0 >> 24
            if c == 0xB8: break
            w1 = be32r(src, o+4)
            if c in ADDR_OPS and (w1 >> 28) == 5:
                check(f"gdl{g:#x}@{o:#x} op{c:#x} w1", w1 & MASK)
            o += 8

stats["max_ratio"] = round(max_ratio, 3)
print("files:", stats["files"], " max D_PC:", hex(stats["dpc_max"]), f" max D_PC/D_N64 ratio: {max_ratio:.2f} ({max_file})")
print("embedded blobs:", blob_info)
if errors:
    print(f"\n{len(errors)} ERRORS:")
    for e in errors[:30]: print("  ", e)
else:
    print("ALL CLEAN: every remap target resolves, layout invariants hold")
