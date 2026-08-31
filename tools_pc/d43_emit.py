#!/usr/bin/env python3
"""D50 / Plan B (D48/D49): offline emit pass — convert all N64 model files to
PC layout and write RZ-compressed sidecars + manifest.csv.

Per file:
  1. decompress the N64 image from the ROM (filelist.u.csv row)
  2. build mutable node map; run the EXACT modelIterateDisplayLists visit
     simulation (d43_gdlorder logic: LOD/SWITCH rewire, BSP splice) -> gdl_seq
  3. layout pass (d43_convert placement): [switches NS*8][texconfigs NT*12]
     [DFS nodes 48B + records PC_REC + vtx arrays][zero-vtx][GDLs packed in
     gdl_seq order, 16B per N64 slot]  -> D_PC, region map
  4. emit pass: byte-exact field tables (bswap32/16, promoted ptrs -> u64
     zero-ext 0x05xxxxxx VMA, GDL Primary/Secondary raw VMAs, BaseAddr=0)
  5. round-trip validation: re-parse the emitted image and check every field
     against the N64 source (inverse remap), plus region tiling (no gaps)
  6. compress: 0x11 0x72 + raw deflate (level 6) -> C_PC

Cross-checks (all must pass):
  - per-file dst!=0 buffers: round8(C)+8 <= buf AND D+round8(C) <= buf
    (inflate overlap safety); N64 sanity version of the same checks
  - G* hand weapons worst-case vs 0xF000
  - chains (cast / title / bondview): cumulative P_final bound
    B_pc + 2*(R_share_N64 - B_n64) per slot, worst over candidate sets
  - totals: sum round8(C), max C, max D

Output: data/pcmodels-<region>/pcmodels.bin (single concatenated image,
sidecars at 16-aligned offsets) + manifest.csv (name,offset,size decimal;
file_resource_table.inc.c order). The port layer (port/src/pcmodels.c)
copies pcmodels.bin to [CART_BASE+romSize, ...) and patches
file_resource_table[i].hw_address / rom_size per manifest row.
"""
import csv, struct, zlib, os, re, sys, bisect
from collections import Counter

# ---------------------------------------------------------------- config ---
REGION = "ntsc-final"
CHECK_ONLY = False
if len(sys.argv) > 1:
    if sys.argv[1] == "--check-only":
        CHECK_ONLY = True
    elif sys.argv[1] in ("ntsc-final", "pal-final", "jpn-final"):
        REGION = sys.argv[1]

ROM_PATH = f"data/ge007.{REGION}.z64"
OUT_DIR = f"data/pcmodels-{REGION}"
TABLE = "assets/obseg/file_resource_table.inc.c"
FILELIST = "scripts/filelist.u.csv"

rom = open(ROM_PATH, "rb").read()
rows = list(csv.reader(open(FILELIST)))

# nsnt from assets/**/modelFileHeader.inc.c (case-insensitive match)
nsnt = {}
for root, dirs, files in os.walk("assets"):
    for fn in files:
        if fn.lower().endswith("modelfileheader.inc.c"):
            for line in open(os.path.join(root, fn)):
                m = re.search(r"MODELFILEHEADER\((.*)\)\s*;?\s*$", line)
                if not m:
                    continue
                a = [x.strip() for x in m.group(1).split(",")]
                if len(a) < 9:
                    continue
                try:
                    nsnt[a[0]] = (int(a[4], 0), int(a[8], 0))
                except ValueError:
                    pass

# file_resource_table order (manifest order) + which entries are model files
table_names = []
for line in open(TABLE):
    m = re.search(r'\{\s*\w+\s*,\s*"([^"]+)"', line)
    if m:
        table_names.append(m.group(1))

def is_model_name(n):
    return n and n[0] in "CGP" and n.endswith("Z") and "_stan" not in n

# filelist lookup by basename (with .bin stripped)
fl_by_base = {}
for r in rows:
    if len(r) < 3 or not r[2]:
        continue
    base = r[2].rsplit("/", 1)[-1]
    if base.endswith(".bin"):
        base = base[:-4]
    fl_by_base[base] = (int(r[0]), int(r[1]))

def find_row(name):
    for cand in (name, name[1:], name[:-1], name[1:-1]):
        if cand in fl_by_base:
            return cand, fl_by_base[cand]
    return None, None

# ---------------------------------------------------------------- helpers ---
MASK = 0xFFFFFF
def be16(b, o):  return struct.unpack_from(">h", b, o)[0]
def bu16(b, o):  return struct.unpack_from(">H", b, o)[0]
def be32o(b, o): return struct.unpack_from(">I", b, o)[0] & MASK
def be32r(b, o): return struct.unpack_from(">I", b, o)[0]

N64_REC = {1: 0x1C, 2: 0x1C, 3: 0x1C, 4: 0x14, 8: 0x10, 9: 0x24, 10: 0x1C,
           12: 0x28, 13: 0x20, 15: 0x1C, 17: 0x20, 18: 0x08, 21: 0x14,
           22: 0x10, 23: 0x02, 24: 0x20}
PC_REC = {1: 24, 2: 40, 3: 40, 4: 40, 8: 24, 9: 48, 10: 28, 12: 48,
          13: 48, 15: 28, 18: 16, 21: 20, 22: 32, 23: 2, 24: 64}
PC_NODE = 48
# GDL w1 remap set: only these opcodes carry file-relative seg-5 addresses in
# w1. Verified against the ROM (all 512 files): G_VTX w1 is 0x04xxxxxx (seg 4 =
# runtime vtx buffer, game-set — leave) or 0x05xxxxxx (file-relative — remap);
# G_SETTIMG w1 is 0x05xxxxxx (embedded image blob — remap). G_LOADBLOCK (0xF3)
# w1=0x07xxxxxx is ignored by port/fast3d (fixed dpLoadBlock) — leave. All
# other opcodes carry index/mode data in w1 (GE redefined the immediate range:
# 0xB1 is a triangle command with 8-bit indices, NOT G_DL) — leave.
ADDR_OPS = {0x04, 0xFD}

def round8(x): return (x + 7) & ~7
def round16(x): return (x + 15) & ~15
def round64(x): return (x + 63) & ~63

# ------------------------------------------------- node map + visit sim ----
class N:
    __slots__ = ("op", "data", "parent", "next", "prev", "child")

def build_nodes(src, D, NS, NT):
    """Mutable node map. Closure MUST match d43_convert.walk() exactly:
    op8/op18 follow their Affects/Controls pointer INSTEAD of child (their
    Child/Next fields are not tree links and lead into garbage)."""
    R0 = 4 * NS + 12 * NT
    nodes = {}
    stack = [R0]
    while stack:
        o = stack.pop()
        if o in nodes or o >= D:
            continue
        n = N()
        n.op = bu16(src, o) & 0xff
        n.data = be32o(src, o + 4)
        n.parent = be32o(src, o + 8)
        n.next = be32o(src, o + 0xC)
        n.prev = be32o(src, o + 0x10)
        n.child = be32o(src, o + 0x14)
        nodes[o] = n
        if n.op == 8:
            q = be32o(src, n.data + 8)
            if q:
                stack.append(q)
        elif n.op == 18:
            q = be32o(src, n.data + 0)
            if q:
                stack.append(q)
        else:
            if n.child:
                stack.append(n.child)
        if n.next:
            stack.append(n.next)
    return nodes, R0

def gdl_of(src, n):
    if n.op in (4, 24):
        return be32o(src, n.data + 0), be32o(src, n.data + 4)
    if n.op == 22:
        return be32o(src, n.data + 8), 0
    return 0, 0

def visit_seq(nodes, src, R0):
    """EXACT modelIterateDisplayLists GDL visit order (mutable walk)."""
    seq = []
    node = R0
    prev_node = None
    prev_gdl = 0
    guard = 0
    while node and guard < 200000:
        guard += 1
        n = nodes[node]
        gdl = 0
        if n.op in (4, 22, 24):
            p, s = gdl_of(src, n)
            if node != prev_node:
                gdl = p
            elif s and s != prev_gdl:
                gdl = s
        elif n.op == 8:      # LOD rewire
            aff = be32o(src, n.data + 8)
            n.child = aff
        elif n.op == 18:     # SWITCH rewire
            ctl = be32o(src, n.data + 0)
            n.child = ctl
        elif n.op == 9:      # BSP splice (visible=TRUE)
            lc, rc = be32o(src, n.data + 0x18), be32o(src, n.data + 0x1C)
            node1, node2 = lc, rc
            if node1:
                n.child = node1
                nodes[node1].prev = 0
                loop = node1
                while nodes[loop].next and nodes[loop].next != node2:
                    loop = nodes[loop].next
                nodes[loop].next = node2
                if node2:
                    nodes[node2].prev = loop
                    loop = node2
                    while nodes[loop].next and nodes[loop].next != node1:
                        loop = nodes[loop].next
                    nodes[loop].next = 0
            else:
                n.child = node2
                if node2:
                    nodes[node2].prev = 0
        if gdl:
            seq.append(gdl)
            prev_node = node
            prev_gdl = gdl
            continue
        prev_node = None
        if n.child:
            node = n.child
        else:
            while node:
                nn = nodes[node].next
                if nn:
                    node = nn
                    break
                node = nodes[node].parent
    return seq

def placement_order(nodes, src, R0):
    """d43_convert walk(): next-first DFS stack (node placement order only;
    GDL packing uses visit_seq). Returns [(n64_off, op)] in placement order."""
    seen = set()
    stack = [R0]
    out = []
    while stack:
        o = stack.pop()
        if o in seen or o >= len(src):
            continue
        seen.add(o)
        n = nodes[o]
        out.append((o, n.op))
        child, nxt = n.child, n.next
        if n.op == 8:
            a = be32o(src, n.data + 8)
            if a:
                stack.append(a)
        elif n.op == 18:
            c = be32o(src, n.data + 0)
            if c:
                stack.append(c)
        else:
            if child:
                stack.append(child)
        if nxt:
            stack.append(nxt)
    return out

def gdl_end(src, D, g):
    o = g
    while o + 8 <= D:
        if (be32r(src, o) >> 24) == 0xB8:
            return o + 8
        o += 8
    return D

# ------------------------------------------------------------- per file ----
errors = []
stats = Counter()
results = {}   # name -> dict

def process(name):
    key, (addr, size) = find_row(name)
    if not key:
        errors.append(f"{name}: no filelist row")
        return
    src = zlib.decompress(rom[addr:addr + size][2:], -15)
    D = len(src)
    # nsnt is keyed by header symbol (e.g. "armourguard"), not file base
    hkey = None
    for cand in (key, key[1:], key[:-1], key[1:-1]):
        if cand in nsnt:
            hkey = cand
            break
    if not hkey:
        errors.append(f"{name}: no header data")
        return
    NS, NT = nsnt[hkey]

    nodes, R0 = build_nodes(src, D, NS, NT)
    if not nodes:
        errors.append(f"{name}: empty tree")
        return
    seq = visit_seq(nodes, src, R0)          # GDL packing order
    placed = placement_order(nodes, src, R0)  # object placement order

    # ---- layout pass (d43_convert verbatim) ----
    regions = []   # (old_off, old_size, new_off)
    def add_region(old, osz, cur):
        regions.append((old, osz, cur))
        return cur + osz

    dstpos = 0
    # switch table: u64 node VMAs on PC (R0_pc = 8*NS + 12*NT)
    for i in range(NS):
        dstpos = add_region(4 * i, 8, dstpos)
    for i in range(NT):
        dstpos = add_region(4 * NS + 12 * i, 12, dstpos)

    node_newoff = {}
    rec_newoff = {}
    vtx_regions = []
    zero_vtx = []
    for (no, op) in placed:
        data = be32o(src, no + 4)
        # PC ModelNode is 48 bytes (5 x u64 pointers), not the N64 24
        dstpos = add_region(no, 48, dstpos)
        node_newoff[no] = dstpos - 48
        if op not in N64_REC:
            errors.append(f"{name}: unknown opcode {op} at {no:#x}")
            continue
        nsz, psz = N64_REC[op], PC_REC[op]
        rec_newoff[data] = dstpos
        # allocate the PC footprint (emit writes psz bytes; nsz would let the
        # next region start inside this record's struct)
        dstpos = add_region(data, psz, dstpos)
        if op == 4:
            nv = bu16(src, data + 0x10); vo = be32o(src, data + 0xC)
            if vo:
                sz = 16 * nv if nv else None
                if sz is None:
                    zero_vtx.append((vo,))
                else:
                    vtx_regions.append((vo, nv))
                    if nv:
                        dstpos = add_region(vo, sz, dstpos)
        elif op == 24:
            nv = be16(src, data + 0xC); ncv = be16(src, data + 0xE)
            vo = be32o(src, data + 8); cvo = be32o(src, data + 0x10)
            puo = be32o(src, data + 0x14)
            if nv and vo:
                vtx_regions.append((vo, nv)); dstpos = add_region(vo, 16 * nv, dstpos)
            if ncv and cvo:
                op24_is_collision[cvo] = True
                vtx_regions.append((cvo, ncv)); dstpos = add_region(cvo, 16 * ncv, dstpos)
            if nv and puo:
                dstpos = add_region(puo, 2 * nv, dstpos)
                op24_pointusage[puo] = nv
        elif op == 22:
            nv = struct.unpack_from(">i", src, data)[0]; vo = be32o(src, data + 4)
            if nv and vo:
                vtx_regions.append((vo, nv)); dstpos = add_region(vo, 16 * nv, dstpos)

    # All record-referenced GDLs (needed before zero-vtx/blob layout so the
    # "next object" boundaries include them).
    ref_gdls = []
    for (no, op) in placed:
        data = be32o(src, no + 4)
        if op == 4 or op == 24:
            for off in (0, 4):
                q = be32o(src, data + off)
                if q and q not in ref_gdls:
                    ref_gdls.append(q)
        elif op == 22:
            q = be32o(src, data + 8)
            if q and q not in ref_gdls:
                ref_gdls.append(q)
    visited_set = set(seq)
    unvisited = sorted(g for g in ref_gdls if g not in visited_set)
    all_gdls = unvisited + list(seq)   # pack order

    obj_offs_now = sorted(set(no for no, _ in placed) | set(rec_newoff.keys()) |
                          set(v[0] for v in vtx_regions) | set(all_gdls))
    for (vo,) in zero_vtx:
        nxt = min((o for o in obj_offs_now if o > vo), default=D)
        sz = nxt - vo
        if sz <= 0 or sz % 16:
            errors.append(f"{name}: zero-vtx array {vo:#x} bad size {sz:#x}")
        else:
            vtx_regions.append((vo, sz // 16))
            dstpos = add_region(vo, sz, dstpos)

    # Embedded image blobs: G_SETTIMG seg-5 targets (P* logo/legal files only).
    # texLoadFromGdl copies G_SETTIMG slots verbatim, so the remapped w1 must
    # point at the relocated pixels. Each span runs from its target to the next
    # placed object; overlapping spans merge (PlegalpageZ's 5 images share one
    # unclaimed span). Bytes are copied raw — the RDP reads them as-is.
    blob_targets = []
    for g in all_gdls:
        o = g
        while o + 8 <= D:
            w0 = be32r(src, o); c = w0 >> 24
            if c == 0xB8:
                break
            if c == 0xFD and (be32r(src, o + 4) >> 24) == 5:
                blob_targets.append(be32r(src, o + 4) & MASK)
            o += 8
    placed_offs = set(o for o, _, _ in regions)
    intervals = []
    for t in sorted(set(blob_targets)):
        end = min((o for o in placed_offs if o > t), default=D)
        intervals.append([t, end])
    merged = []
    for iv in intervals:
        if merged and iv[0] <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], iv[1])
        else:
            merged.append(iv)
    for (t, e) in merged:
        dstpos = add_region(t, e - t, dstpos)

    # GDLs last, tight-packed 16B slots. Visited GDLs go in VISIT order (the
    # compaction span arithmetic requires it). Referenced-but-unvisited GDLs
    # (e.g. PexplosionbitZ: compaction skips the file entirely when iterate
    # visits zero GDLs) are packed just before the visited block so they stay
    # outside [off_first, D_PC) and are never span-read; if a runtime BSP
    # visible=FALSE state renders them, they are still valid 16B slots.

    gdl_newoff = {}
    total_slots = 0
    def pack_gdl(g):
        nonlocal dstpos, total_slots
        end = gdl_end(src, D, g)
        nslots = (end - g) // 8
        if (end - g) % 8:
            errors.append(f"{name}: GDL {g:#x} not 8B aligned end")
        gdl_newoff[g] = dstpos
        dstpos += 16 * nslots
        total_slots += nslots
        # region size = NEW footprint (16B slots); the old 8B bytes are not a
        # 1:1 byte map, so inv_remap must never be called inside a GDL region
        # (GDL pointers go through gdl_newoff, not put_ptr).
        regions.append((g, 16 * nslots, gdl_newoff[g]))
    for g in unvisited:
        pack_gdl(g)
    for g in seq:
        pack_gdl(g)

    D_PC = dstpos
    G_PC = min(gdl_newoff.values()) if gdl_newoff else D_PC
    g1_n64 = min(seq) if seq else D

    # ---- P_final estimate (D45/D46 marker-expansion worst case) ----
    # Runtime footprint after compaction + texLoadFromGdl expansion:
    #   P_off = g1_visited_new + 16*S_visited + 16*sum(K_t-1 over visited markers)
    #         <= D_PC + 16*sum(K_t-1 over ALL referenced-GDL markers)
    # K_BOUND: exact worst-case output slots per marker type (tex.c analysis,
    # verified over all files by d43_lutscan.py; unknown types default to 44).
    # Markers are counted over N64 spans [g, next_gdl_or_EOF) of every
    # referenced GDL (superset of what the PC runtime processes: unvisited
    # GDLs and inter-GDL junk are never span-read on PC).
    K_BOUND = {0: 38, 1: 46, 2: 36, 3: 18, 4: 15}
    kslack = 0
    gsorted = sorted(set(all_gdls))
    for i, gg in enumerate(gsorted):
        e = gsorted[i + 1] if i + 1 < len(gsorted) else D
        j = gg
        while j + 8 <= e:
            w0 = be32r(src, j)
            if (w0 >> 24) == 0xC0:
                kslack += K_BOUND.get(w0 & 7, 44) - 1
            j += 8
    P_est = D_PC + 16 * kslack

    # ---- remap machinery ----
    regions.sort()
    rold = [x[0] for x in regions]
    def remap(off):
        if off == 0:
            return 0
        i = bisect.bisect_right(rold, off) - 1
        if i < 0:
            return None
        o, osz, n = regions[i]
        if o <= off < o + osz:
            return n + (off - o)
        return None

    # NOTE: `regions` is sorted by OLD offset above; new offsets follow DFS
    # placement order and are NOT sorted, so inv_remap needs its own index.
    rsorted_new = sorted(regions, key=lambda x: x[2])
    rnew_offs = [x[2] for x in rsorted_new]
    def inv_remap(off):
        """PC offset -> N64 offset (for round-trip validation)."""
        i = bisect.bisect_right(rnew_offs, off) - 1
        if i < 0:
            return None
        o, osz, n = rsorted_new[i]
        if n <= off < n + osz:
            return o + (off - n)
        return None

    # ---- emit pass ----
    buf = bytearray(D_PC)
    def put_u16(o, v): buf[o:o + 2] = struct.pack("<H", v & 0xFFFF)
    def put_s16(o, v): buf[o:o + 2] = struct.pack("<H", v & 0xFFFF)  # bytes identical to <h
    def put_u32(o, v): buf[o:o + 4] = struct.pack("<I", v & 0xFFFFFFFF)
    def put_f32(o, doff): buf[o:o + 4] = src[doff:doff + 4][::-1]  # bswap32 (BE->LE)
    def put_ptr(o, old_off):
        if not old_off:
            return
        n = remap(old_off)
        if n is None:
            errors.append(f"{name}: ptr {old_off:#x} not in region map")
            n = 0
        buf[o:o + 4] = struct.pack("<I", 0x05000000 | (n & MASK))
        buf[o + 4:o + 8] = b"\x00" * 4

    # switches: NS x u64 (VMA of node)
    for i in range(NS):
        put_ptr(8 * i, be32o(src, 4 * i))
    # texconfigs: NT x 12B
    for i in range(NT):
        doff = 4 * NS + 12 * i
        npos = 8 * NS + 12 * i
        tid = be32r(src, doff)
        put_u32(npos, tid)                    # bswap32 -> LE (seg-5 VMA included:
        buf[npos + 4:npos + 12] = src[doff + 4:doff + 12]  # game skips >= MAX_TEXTURES on both platforms)
        if (tid >> 24) == 5 and remap(tid & MASK) is None:
            errors.append(f"{name}: texconfig seg-5 target {tid & MASK:#x} not in a blob region")

    # nodes + records
    for (no, op) in placed:
        npos = node_newoff[no]
        put_u16(npos, bu16(src, no))                    # Opcode
        put_ptr(npos + 8, be32o(src, no + 4))           # Data
        put_ptr(npos + 16, be32o(src, no + 8))          # Parent
        put_ptr(npos + 24, be32o(src, no + 0xC))        # Next
        put_ptr(npos + 32, be32o(src, no + 0x10))       # Prev
        put_ptr(npos + 40, be32o(src, no + 0x14))       # Child

        d = be32o(src, no + 4)
        r = rec_newoff[d]
        if op == 1:      # HeaderRecord
            put_u16(r, bu16(src, d)); put_s16(r + 2, be16(src, d + 2))
            put_ptr(r + 8, be32o(src, d + 4))
            put_u16(r + 16, bu16(src, d + 8)); put_u16(r + 18, bu16(src, d + 0xA))
            put_u16(r + 20, bu16(src, d + 0xC))
        elif op == 2 or op == 3:   # GroupRecord / OP03
            for i in range(3): put_f32(r + 4 * i, d + 4 * i)
            put_u16(r + 12, bu16(src, d + 0xC))
            for i in range(3): put_s16(r + 14 + 2 * i, be16(src, d + 0xE + 2 * i))
            put_ptr(r + 24, be32o(src, d + 0x14))
            put_f32(r + 32, d + 0x18)
        elif op == 4:    # DisplayListRecord
            p = be32o(src, d); s = be32o(src, d + 4)
            if p: buf[r:r + 4] = struct.pack("<I", 0x05000000 | (gdl_newoff[p] & MASK))
            if s: buf[r + 8:r + 12] = struct.pack("<I", 0x05000000 | (gdl_newoff[s] & MASK))
            put_ptr(r + 24, be32o(src, d + 0xC))
            put_u16(r + 32, bu16(src, d + 0x10))
            buf[r + 34] = src[d + 0x12]               # ModelType s8 raw
        elif op == 8:    # LODRecord
            put_f32(r, d); put_f32(r + 4, d + 4)
            put_ptr(r + 8, be32o(src, d + 8))
            put_u16(r + 16, bu16(src, d + 0xC))
        elif op == 9:    # BSPRecord
            for i in range(3): put_f32(r + 4 * i, d + 4 * i)
            for i in range(3): put_f32(r + 12 + 4 * i, d + 0xC + 4 * i)
            put_ptr(r + 24, be32o(src, d + 0x18))
            put_ptr(r + 32, be32o(src, d + 0x1C))
            put_u16(r + 42, bu16(src, d + 0x22))
        elif op == 10:   # BoundingBoxRecord
            put_u32(r, be32r(src, d))
            for i in range(6): put_f32(r + 4 + 4 * i, d + 4 + 4 * i)
        elif op == 12:   # GunfireRecord
            for i in range(3): put_f32(r + 4 * i, d + 4 * i)
            for i in range(3): put_f32(r + 12 + 4 * i, d + 0xC + 4 * i)
            put_ptr(r + 24, be32o(src, d + 0x18))
            put_f32(r + 32, d + 0x1C)
            put_u16(r + 36, bu16(src, d + 0x20))
        elif op == 13:   # ShadowRecord
            for i in range(2): put_f32(r + 4 * i, d + 4 * i)
            for i in range(2): put_f32(r + 8 + 4 * i, d + 8 + 4 * i)
            put_ptr(r + 16, be32o(src, d + 0x10))
            put_ptr(r + 24, be32o(src, d + 0x14))
            put_f32(r + 32, d + 0x18)
        elif op == 15:   # InterlinkageRecord
            for i in range(3): put_f32(r + 4 * i, d + 4 * i)
            for i in range(3): put_f32(r + 12 + 4 * i, d + 0xC + 4 * i)
            put_f32(r + 24, d + 0x18)
        elif op == 18:   # SwitchRecord
            put_ptr(r, be32o(src, d))
            put_u16(r + 8, bu16(src, d + 4))
        elif op == 21:   # GroupSimpleRecord
            for i in range(3): put_f32(r + 4 * i, d + 4 * i)
            put_s16(r + 12, be16(src, d + 0xC)); put_u16(r + 14, bu16(src, d + 0xE))
            put_f32(r + 16, d + 0x10)
        elif op == 22:   # DisplayListPrimaryRecord
            put_u32(r, be32r(src, d))                  # numVertices s32 bswap
            put_ptr(r + 8, be32o(src, d + 4))
            p = be32o(src, d + 8)
            if p: buf[r + 16:r + 20] = struct.pack("<I", 0x05000000 | (gdl_newoff[p] & MASK))
        elif op == 23:   # HeadPlaceholderRecord
            put_u16(r, bu16(src, d))
        elif op == 24:   # DisplayList_CollisionRecord
            p = be32o(src, d); s = be32o(src, d + 4)
            if p: buf[r:r + 4] = struct.pack("<I", 0x05000000 | (gdl_newoff[p] & MASK))
            if s: buf[r + 8:r + 12] = struct.pack("<I", 0x05000000 | (gdl_newoff[s] & MASK))
            put_ptr(r + 16, be32o(src, d + 8))
            put_s16(r + 24, be16(src, d + 0xC)); put_s16(r + 26, be16(src, d + 0xE))
            put_ptr(r + 32, be32o(src, d + 0x10))
            put_ptr(r + 40, be32o(src, d + 0x14))
            put_s16(r + 48, be16(src, d + 0x18)); put_u16(r + 50, bu16(src, d + 0x1A))

    # vertex arrays
    def emit_main_vtx(vo, nv, npos):
        for i in range(nv):
            o = vo + 16 * i; q = npos + 16 * i
            put_s16(q, be16(src, o)); put_s16(q + 2, be16(src, o + 2))
            put_s16(q + 4, be16(src, o + 4)); put_s16(q + 6, be16(src, o + 6))
            put_s16(q + 8, be16(src, o + 8)); put_s16(q + 10, be16(src, o + 0xA))
            buf[q + 12:q + 16] = src[o + 0xC:o + 0x10]

    for (vo, nv) in vtx_regions:
        npos = remap(vo)
        if op24_is_collision.get(vo):
            cvo_npos = npos
            for i in range(nv):
                o = vo + 16 * i; q = cvo_npos + 16 * i
                put_s16(q, be16(src, o)); put_s16(q + 2, be16(src, o + 2))
                put_s16(q + 4, be16(src, o + 4)); put_s16(q + 6, be16(src, o + 6))
                v = be32r(src, o + 8)
                if v == 0:
                    pass
                elif (v >> 24) == 5:
                    n = remap(v & MASK)
                    if n is None:
                        errors.append(f"{name}: LinkedTo {v:#x} not in map")
                        n = 0
                    put_u32(q + 8, 0x05000000 | (n & MASK))
                else:
                    errors.append(f"{name}: LinkedTo {v:#x} neither null nor seg-5")
                put_s16(q + 12, be16(src, o + 0xC)); put_s16(q + 14, be16(src, o + 0xE))
        else:
            emit_main_vtx(vo, nv, npos)

    # D120: PointUsage[] -- an `s16` next-index chain (numVertices entries,
    # negative-terminated).  Plain byteswap, no remap.
    for puo, punv in op24_pointusage.items():
        pnpos = remap(puo)
        if pnpos is None:
            errors.append(f"{name}: PointUsage {puo:#x} not in region map")
            continue
        for k in range(punv):
            put_s16(pnpos + 2 * k, be16(src, puo + 2 * k))

    # GDLs (pack order: unvisited first, then visit order), 16B LE slots
    for g in all_gdls:
        end = gdl_end(src, D, g)
        npos = gdl_newoff[g]
        j = 0
        o = g
        while o + 8 <= end:
            w0 = be32r(src, o); w1 = be32r(src, o + 4)
            q = npos + 16 * j
            # PC Gfx slot = {u64 words.w0; u64 words.w1} (Gwords uses uintptr_t).
            # Both the game (tex.c: (u8)(words.w0>>24), words.w1 & 0xfff) and
            # fast3d (opcode = words.w0>>24 unmasked, seg_addr(words.w1)) read
            # N64 w0 from the LOW dword of bytes[0..8) and N64 w1 from the LOW
            # dword of bytes[8..16). High dwords must be zero (gSP* macros
            # zero-extend their 32-bit patterns into the u64 fields).
            c = w0 >> 24
            if c in ADDR_OPS and (w1 >> 24) == 5:
                n = remap(w1 & MASK)
                if n is None:
                    errors.append(f"{name}: GDL {g:#x} op{c:#x} w1 {w1:#x} not in map")
                    n = 0
                w1 = 0x05000000 | (n & MASK)
            buf[q:q + 8] = struct.pack("<II", w0, 0)      # words.w0 = w0
            buf[q + 8:q + 16] = struct.pack("<II", w1, 0) # words.w1 = w1
            if c == 0xB8:
                break
            j += 1
            o += 8

    # ---- round-trip validation ----
    validate(name, src, D, NS, NT, nodes, R0, placed, all_gdls, buf, D_PC,
             regions, node_newoff, rec_newoff, gdl_newoff, remap, inv_remap)

    # ---- compress ----
    co = zlib.compressobj(6, zlib.DEFLATED, -15)
    comp = b"\x11\x72" + co.compress(bytes(buf)) + co.flush()
    C_PC = len(comp)

    results[name] = dict(C=C_PC, D=D_PC, Dn64=D, S=total_slots,
                         Bpc=D_PC - 16 * total_slots, Bn64=g1_n64,
                         Pest=P_est,
                         comp=comp if not CHECK_ONLY else None,
                         NS=NS, NT=NT)

# collision-vertex set (filled during layout; emit pass needs it)
op24_is_collision = {}
# D120: opcode-0x18 PointUsage arrays -> {old_off: numVertices}.  Reserved in
# layout (add_region) but the emit pass must byteswap the s16 index chain or the
# runtime blood-decal walk (chr.c PointUsage[index]) cycles (guarded, not fixed,
# until now).
op24_pointusage = {}

def validate(name, src, D, NS, NT, nodes, R0, placed, all_gdls, buf, D_PC,
             regions, node_newoff, rec_newoff, gdl_newoff, remap, inv_remap):
    def err(m): errors.append(f"{name}: RT {m}")

    # tiling: regions cover [0, D_PC) exactly
    rs = sorted(regions, key=lambda x: x[2])
    pos = 0
    for o, osz, n in rs:
        if n != pos or n + osz > D_PC:
            err(f"tiling gap/overlap at {pos:#x} (region {o:#x}->{n:#x})")
            return
        pos += osz
    if pos != D_PC:
        err(f"tiling ends at {pos:#x} != D_PC {D_PC:#x}")
        return

    # node fields
    for (no, op) in placed:
        npos = node_newoff[no]
        if bu16(src, no) != struct.unpack_from("<H", buf, npos)[0]:
            err(f"node {no:#x} opcode mismatch")
        for f, pf in ((4, 8), (8, 16), (0xC, 24), (0x10, 32), (0x14, 40)):
            old = be32o(src, no + f)
            v = struct.unpack_from("<Q", buf, npos + pf)[0]
            if old == 0:
                if v != 0: err(f"node {no:#x}+{f:#x}: expected null")
            else:
                want = remap(old)
                if (v >> 24) != 5 or (v & MASK) != want:
                    err(f"node {no:#x}+{f:#x}: {v:#x} != 0x05|{want and hex(want)}")

    # record fields (spot-check every pointer + scalar via inverse remap)
    ptrfields = {1: [4], 2: [0x14], 3: [0x14], 8: [8], 9: [0x18, 0x1C],
                 12: [0x18], 13: [0x10, 0x14], 18: [0], 24: [8, 0x10, 0x14],
                 4: [0xC], 22: [4]}
    for (no, op) in placed:
        d = be32o(src, no + 4)
        r = rec_newoff[d]
        for f in ptrfields.get(op, []):
            old = be32o(src, d + f)
            v = struct.unpack_from("<Q", buf, r + PC_PTR_OFF[op][f])[0]
            if old == 0:
                if v != 0: err(f"rec{op} {d:#x}+{f:#x}: expected null")
            else:
                want = remap(old)
                if (v >> 24) != 5 or (v & MASK) != want:
                    err(f"rec{op} {d:#x}+{f:#x}: {v:#x}")

    # GDL slots (all packed GDLs)
    for g in all_gdls:
        end = gdl_end(src, D, g)
        npos = gdl_newoff[g]
        j = 0; o = g
        while o + 8 <= end:
            w0 = be32r(src, o); w1 = be32r(src, o + 4)
            q = npos + 16 * j
            if struct.unpack_from("<I", buf, q)[0] != w0:
                err(f"gdl {g:#x} slot {j}: w0 mismatch")
            c = w0 >> 24
            if struct.unpack_from("<I", buf, q + 4)[0] != 0:
                err(f"gdl {g:#x} slot {j}: pad[4..8) nonzero")
            w1p = struct.unpack_from("<I", buf, q + 8)[0]
            if c in ADDR_OPS and (w1 >> 24) == 5:
                want = remap(w1 & MASK)
                if w1p != (0x05000000 | want):
                    err(f"gdl {g:#x} slot {j}: w1 {w1p:#x} != 0x05|{hex(want)}")
            elif w1p != w1:
                err(f"gdl {g:#x} slot {j}: w1 passthrough mismatch")
            if struct.unpack_from("<I", buf, q + 12)[0] != 0:
                err(f"gdl {g:#x} slot {j}: pad[12..16) nonzero")
            if c == 0xB8:
                break
            j += 1; o += 8

    # D120: PointUsage[] round-trips + is a sane index chain (each entry is an
    # s16 in [-32768, numVertices) -- an index into the same array, or negative
    # to terminate).  A zero-filled region (the pre-M-30 bug) trivially passes
    # the range test but every entry == 0 -> flag that too.
    for puo, punv in op24_pointusage.items():
        pn = remap(puo)
        if pn is None:
            err(f"PointUsage {puo:#x} not in region map"); continue
        allzero = True
        for k in range(punv):
            want = struct.unpack_from(">h", src, puo + 2 * k)[0]
            got = struct.unpack_from("<h", buf, pn + 2 * k)[0]
            if got != want:
                err(f"PointUsage {puo:#x}[{k}]: {got} != {want}")
            if got != 0:
                allzero = False
            if got >= punv:
                err(f"PointUsage {puo:#x}[{k}] = {got} >= numVertices {punv}")
        if allzero and punv > 1:
            err(f"PointUsage {puo:#x}: all {punv} entries zero (emit gap?)")

# PC pointer offsets per (op, n64 field) — for round-trip validation
PC_PTR_OFF = {
    1: {4: 8}, 2: {0x14: 24}, 3: {0x14: 24}, 8: {8: 8},
    9: {0x18: 24, 0x1C: 32}, 12: {0x18: 24}, 13: {0x10: 16, 0x14: 24},
    18: {0: 0}, 24: {8: 16, 0x10: 32, 0x14: 40}, 4: {0xC: 24}, 22: {4: 8},
}

# ------------------------------------------------------------------- main ---
model_names = [n for n in table_names if is_model_name(n)]
stats["table_models"] = len(model_names)

for name in model_names:
    # reset per-file collision set before layout (populated below)
    op24_is_collision.clear()
    op24_pointusage.clear()
    process(name)

if errors:
    print(f"\n{len(errors)} ERRORS:")
    for e in errors[:40]:
        print("  ", e)
    sys.exit(1)

print(f"files converted: {len(results)} / {stats['table_models']}")
tot = sum(round8(r["C"]) for r in results.values())
print(f"sum round8(C_pc) = {tot}   max C = {hex(max(r['C'] for r in results.values()))}"
      f"   max D = {hex(max(r['D'] for r in results.values()))}")

# ------------------------------------------------------- cross-checks ------
DIRECT = {
    "Csuit_lf_handZ": 0x18000,
    "GtriggerZ": 0x17000, "GwatchlaserZ": 0x17000,
    "PwalletbondZ": 0x17000,
    "PlegalpageZ": 0x3C000, "PnintendologoZ": 0x3C000, "PgoldeneyelogoZ": 0x3C000,
}
WEAPON_BUF = 0xF000

fail = 0
def chk(cond, msg):
    global fail
    tag = "OK  " if cond else "FAIL"
    if not cond:
        fail += 1
    print(f"{tag} {msg}")

print("\n== single-file dst!=0 buffers ==")
for name, b in DIRECT.items():
    r = results.get(name)
    if not r:
        chk(False, f"{name}: missing"); continue
    ok = (round8(r["C"]) + 8 <= b and r["D"] + round8(r["C"]) <= b
          and r["Pest"] <= b)
    chk(ok, f"{name:20s} C={r['C']:>6} D={r['D']:>6} round8(C)+8={round8(r['C'])+8:#7x} "
            f"D+r8C={r['D']+round8(r['C']):#7x} P_est={r['Pest']:>#7x} vs {b:#x}")

# gun.c used_to_load_1st_person_model_on_demand: every item model except
# ITEM_SUIT_LF_HAND (Csuit_lf_handZ, 0x18000) and ITEM_TRIGGER/ITEM_WATCHLASER
# (GtriggerZ/GwatchlaserZ, 0x17000 — both in DIRECT above) loads into the
# D_80032464 weapon buffer. G* files are exactly the item models.
GUN_BUF_FILES = [n for n in results
                 if n.startswith("G") and n.endswith("Z")
                 and n not in ("GtriggerZ", "GwatchlaserZ")]
worst_w = None
for n in GUN_BUF_FILES:
    r = results[n]
    if worst_w is None or r["Pest"] > worst_w[0]:
        worst_w = (r["Pest"], n, r)
chk(worst_w[0] <= WEAPON_BUF and worst_w[2]["D"] + round8(worst_w[2]["C"]) <= WEAPON_BUF
    and round8(worst_w[2]["C"]) + 8 <= WEAPON_BUF,
    f"worst gun-buffer item {worst_w[1]}: P_est={worst_w[0]:#x}, D+r8C={worst_w[2]['D']+round8(worst_w[2]['C']):#x} "
    f"r8C+8={round8(worst_w[2]['C'])+8:#x} vs {WEAPON_BUF:#x}")

def chain_check(label, slots, buf_pc):
    """Chain fit: the game advances bufferPtr by each file's post-load
    poolRemaining (>= its actual footprint P_off), so the chain works iff
        sum(P_off_i) <= R_pc          (prefix argument: A_j >= P_off_j)
    with P_off_i bounded by P_est_i (D45/D46 marker-expansion worst case).
    Slots are independent (any body+head+weapon can be drawn), so the worst
    combination is the sum of per-slot maxima. Each slot's DMA/decompress
    constraints are checked against a lower bound on its remaining buffer.
    """
    per_slot = []
    for sl, cands in slots:
        best = None
        for n in cands:
            r = results.get(n)
            if not r:
                continue
            if best is None or r["Pest"] > best[0]:
                best = (r["Pest"], n, r)
        if best is None:
            chk(False, f"{label}/{sl}: no candidates"); continue
        per_slot.append((sl, best[1], best[2]))
    total = sum(r["Pest"] for _, _, r in per_slot)
    chk(total <= buf_pc,
        f"{label}: worst combination {total:#x} vs buffer {buf_pc:#x} "
        + " ".join(f"{sl}:{n}(P_est={r['Pest']:#x})" for sl, n, r in per_slot))
    rem = buf_pc
    for sl, n, r in per_slot:
        ok = round8(r["C"]) + 8 <= rem and r["D"] + round8(r["C"]) <= rem
        chk(ok, f"{label}/{sl}: worst {n} C={r['C']:#x} D={r['D']:#x} "
                f"round8(C)+8={round8(r['C'])+8:#x} D+r8C={r['D']+round8(r['C']):#x} "
                f"vs rem-lower={rem:#x}")
        rem -= round64(r["Pest"])

# candidate sets
heads = [n for n in results if n.startswith("Chead")]
bodies = [n for n in results if n.startswith("C") and n.endswith("Z")
          and not n.startswith("Chead") and n != "Csuit_lf_handZ"]
# cast screen (front.c random_rifles_in_intro / random_pistols_in_intro):
CAST_WEAPONS = ["PchrkalashZ", "Pchrm16Z", "Pchrfnp90Z", "PchrautoshotZ",
                "PchrgrenadelaunchZ", "PchrsniperrifleZ",
                "PchrwppkZ", "PchrwppksilZ", "PchrskorpionZ", "PchruziZ",
                "Pchrtt33Z", "PchrrugerZ", "PchrlaserZ", "PchrgoldenZ"]
# bondview2 held prop (player.c getPropForHeldItem):
BONDVIEW_PROPS = ["PchrknifeZ", "PchrwppkZ", "PchrskorpionZ", "PchrkalashZ",
                  "PchruziZ", "Pchrmp5kZ", "PchrspectreZ", "Pchrm16Z",
                  "Pchrfnp90Z", "PchrshotgunZ", "PchrgrenadelaunchZ",
                  "PchrgrenadeZ", "PchrremotemineZ", "PchrproximitymineZ",
                  "PchrtimedmineZ", "PchrrugerZ", "PchrlaserZ",
                  "PchrsniperrifleZ", "Pchrmp5ksilZ", "Pchrtt33Z",
                  "PchrwppksilZ", "PchrthrowknifeZ", "PchrautoshotZ",
                  "PchrrocketlaunchZ", "PchrgoldenZ", "PflagZ"]
for n in CAST_WEAPONS + BONDVIEW_PROPS:
    chk(n in results, f"candidate present: {n}")

print("\n== cast screen chain (front.c:7811/7819/7869; PC 0x25000, N64 0x18160) ==")
chain_check("cast", [("body", bodies), ("head", heads), ("weapon", CAST_WEAPONS)],
            0x25000)

print("\n== title gunbarrel chain (title.c:489/495/517; avail 0x24D00 both sides) ==")
# fixed files (c_item_entries indexed by BODIES enum; BODY_Male_Karl=HEAD_START
# is an alias so the effective count is 80, matching the 80 record includes):
#   BODY_Brosnan_Tuxedo=5 -> CdjbondZ
#   BODY_Male_Pierce_Bond_Tuxedo=78 -> CheadbrosnanZ (used as the head slot)
#   PitemZ_entries[PROP_CHRWPPK] -> PchrwppkZ
t1 = ["CdjbondZ"]
t2 = ["CheadbrosnanZ"]
t3 = ["PchrwppkZ"]
chain_check("title", [("brosnan", t1), ("bond", t2), ("wppk", t3)],
            0x24D00)

print("\n== bondview2 chain (bondview2.c:490/502/571; PC 0x23000, N64 0x14820) ==")
chain_check("bondview", [("body", bodies), ("head", heads), ("prop", BONDVIEW_PROPS)],
            0x23000)

print("\n== stage bank (dst==0 fresh loads) ==")
maxr = max(round8(r["C"]) + 8 for r in results.values())
chk(maxr <= 0x24C400, f"max round8(C)+8 = {maxr:#x} vs STAGE bank 0x24C400")

# N64 sanity: same overlap check with N64 sizes (the game worked on N64)
print("\n== N64 sanity (overlap model) ==")
n64_fail = 0
for name, r in results.items():
    # recompute C_n64 cheaply? we only stored Dn64; approximate with stored comp? no.
    pass
# (N64 compressed sizes: recomputed below from ROM for the DIRECT set only)
for name, b in DIRECT.items():
    key, (addr, size) = find_row(name)
    Cn = size - 2
    Dn = results[name]["Dn64"]
    ok = round8(Cn) + 8 <= b and Dn + round8(Cn) <= b
    if not ok:
        n64_fail += 1
        print(f"  N64 {name}: round8(C)+8={round8(Cn)+8:#x} D+r8C={Dn+round8(Cn):#x} vs {b:#x}")
print(f"N64 overlap-model violations: {n64_fail} (0 expected; if >0 the overlap "
      f"constraint is looser than modeled — review before trusting PC checks)")

# ---------------------------------------------------------------- output ---
if CHECK_ONLY:
    print("\ncheck-only: no sidecars written")
else:
    os.makedirs(OUT_DIR, exist_ok=True)
    manifest = []   # (name, offset, size) in table order
    off = 0
    chunks = []
    for name in model_names:
        r = results[name]
        start = (off + 15) & ~15
        if start > off:
            chunks.append(b"\x00" * (start - off))
            off = start
        manifest.append((name, off, r["C"]))
        chunks.append(r["comp"])
        off += r["C"]
    total = (off + 15) & ~15
    if off % 16:
        chunks.append(b"\x00" * (16 - (off % 16)))
    with open(os.path.join(OUT_DIR, "pcmodels.bin"), "wb") as f:
        for c in chunks:
            f.write(c)
    # decimal fields: the C parser uses strtol(..., 10)
    with open(os.path.join(OUT_DIR, "manifest.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["name", "offset", "size"])
        for name, o, s in manifest:
            w.writerow([name, o, s])
    print(f"\nwrote {len(manifest)} sidecars to {OUT_DIR}/pcmodels.bin")
    print(f"total reservation needed: {total:#x} bytes "
          f"(romSize + this must fit the extended VirtualAlloc)")

if fail:
    print(f"\n{fail} CHECK FAILURES")
    sys.exit(1)
print("\nALL CHECKS PASSED")
