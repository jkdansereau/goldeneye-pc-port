# D43: PC image size budget (final).
#
# Converter output layout (PC image, little-endian):
#   [switches NS*8][textures NT*12][nodes 48B + records pc-size, DFS preorder]
#   [bulk arrays: vertex/coll/pointusage, verbatim sizes, s16/f32 bswapped]
#   [GDLs at tail in visit order, 16B Gfx slots, no inter-GDL padding]
#
# sub_GAME_7F0762E0 mirror+expand contract:
#   D = pc_remaining (PC image size), g1 = first GDL offset, R = buffer.
#   texCopyGdls mirrors [g1,D) to [R-D+g1,R); texLoadFromGdl expands each GDL
#   rightward from g1 reading the mirror.  Each N64 8B slot becomes one PC 16B
#   slot, so the GDL region doubles: input extent of GDL k = 2*E_k (E_k = N64
#   extent bytes).  Fit and no-clobber both reduce to:
#       R >= P_final = B_pc + sum(2*E_k) + 16 * sum_markers(K_t - 1)
#   K_t = worst-case output slots a marker of type t emits (tex.c analysis;
#   only types 2/3/4 occur in ROM):
RSZ_N64 = {1:0x10, 2:0x1C, 4:0x14, 8:0x10, 9:0x24, 10:0x1C, 12:0x28,
           13:0x20, 15:0x1C, 17:0x20, 18:0x8, 21:0x14, 22:0x10, 23:2, 24:0x20}
RSZ_PC  = {1:0x18, 2:0x28, 4:0x28, 8:0x18, 9:0x30, 10:0x1C, 12:0x30,
           13:0x30, 15:0x1C, 17:0x28, 18:0x10, 21:0x14, 22:0x20, 23:2, 24:0x40}
NODE_N64, NODE_PC = 24, 48
# exact worst-case output slots per marker (tex.c helpers, maxlod<=8,
# all texTry* state guards emitting, valid=FALSE branch, water check on):
#   preamble = PipeSync(<=1) + texWriteTextureCmd(<=1) = 2
#   LoadToTmemAddr/Zero <= 9, TileFromDef <= 4, TileLods <= 3*maxlod,
#   Tile <= 3, light begin/end = 0, water sub = 1
K_BOUND = {0:2+9+4+3*7+1, 1:2+9+1+9+4+3*7+1, 2:2+9+3*8+1, 3:2+9+3+3+1, 4:2+9+3+1}
R_BUF = 0x14820  # size_item_buffer (gun.c), shared body/head/props per hand

import csv, struct, zlib, os, re
from collections import Counter
ROM = r"data/ge007.ntsc-final.z64"
rom = open(ROM, "rb").read()
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

def be32(b, o): return struct.unpack_from(">I", b, o)[0] & 0xFFFFFF

def gdl_marker_slots(out, g, e):
    # count texture markers (G_NOOP w1 type field) in [g,e); returns {type:count}
    c = Counter()
    j = g
    while j + 8 <= e:
        w0 = struct.unpack_from(">I", out, j)[0]
        if (w0 >> 24) == 0xC0:
            t = w0 & 7
            c[t] += 1
        j += 8
    return c

stats = []
nfiles = 0
for r in rows:
    if len(r) < 3 or not r[2]: continue
    base = r[2].rsplit("/", 1)[-1]
    if base.endswith(".bin"): base = base[:-4]
    if not (base[0] in "CGP" and base.endswith("Z") and "_stan" not in base): continue
    try: addr, size = int(r[0]), int(r[1])
    except ValueError: continue
    key = None
    for cand in (base, base[1:], base[:-1], base[1:-1]):
        if cand in nsnt: key = cand; break
    if not key: continue
    try: out = zlib.decompress(rom[addr:addr+size][2:], -15)
    except Exception: continue
    D = len(out); NS, NT = nsnt[key]
    R0n = 4*NS + 12*NT
    # walk nodes (DFS preorder), collect records + GDLs
    stack = [R0n]; seen = set(); nodes = []; gdls = []
    while stack:
        o = stack.pop()
        if o in seen or o >= D-23: break
        seen.add(o)
        op = struct.unpack_from(">H", out, o)[0] & 0xff
        data = be32(out, o+4)
        nodes.append((o, op, data))
        if op in (4, 24):
            p, s = be32(out, data), be32(out, data+4)
            if p: gdls.append(p)
            if s: gdls.append(s)
        elif op == 22:
            p = be32(out, data+8)
            if p: gdls.append(p)
        for q in (be32(out, o+0x14), be32(out, o+0xC)):
            if q: stack.append(q)
    gsorted = sorted(set(gdls))
    g1 = gsorted[0] if gsorted else D
    # bulk arrays (vertex/coll/pointusage + alignment padding) keep N64 byte
    # counts on PC: 16B/vertex, 16B/coll vertex, 2B/PU entry.
    bulk = (g1 - R0n) - len(nodes)*NODE_N64 - sum(RSZ_N64[op] for _, op, _ in nodes)
    B_pc = 8*NS + 12*NT + len(nodes)*NODE_PC + sum(RSZ_PC[op] for _, op, _ in nodes) + bulk
    G_in = 0; Kslack = 0
    for i, g in enumerate(gsorted):
        e = gsorted[i+1] if i+1 < len(gsorted) else D
        assert (e - g) % 8 == 0, (base, hex(g), e - g)
        G_in += 2 * (e - g)  # each N64 8B slot -> one PC 16B slot
        for t, n in gdl_marker_slots(out, g, e).items():
            Kslack += n * (K_BOUND.get(t, 44) - 1)
    P_final = B_pc + G_in + 16*Kslack
    R_need2 = B_pc + G_in + 32*Kslack
    stats.append((base, D, B_pc, G_in, 16*Kslack, P_final, Kslack, R_need2))
    nfiles += 1

print("files:", nfiles)
worst = sorted(stats, key=lambda s: -s[5])[:15]
for b, D, Bp, Gi, K, Pf, Ks, R2 in worst:
    print("%-22s N64=%7X  B_pc=%7X  G_in=%7X  Mtot=%5d  P_final=%8X  R_need2=%8X" % (b, D, Bp, Gi, Ks, Pf, R2))
over = [s for s in stats if s[5] > R_BUF]
print("max 16*M_total:", max(s[7]-s[5] for s in stats), "hex %X" % max(s[7]-s[5] for s in stats))
print("files over budget:", len(over), "of", nfiles)
totD = sum(s[1] for s in stats); totP = sum(s[5] for s in stats)
print("sum N64 bytes: %X  sum P_final: %X" % (totD, totP))
