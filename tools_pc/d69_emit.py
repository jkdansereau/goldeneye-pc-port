#!/usr/bin/env python3
"""D69: offline emit pass -- convert all stage bg/*.seg + Tbg_*_stanZ files
to PC layout and write a concatenated sidecar image + manifest.csv (same
shape/pattern as tools_pc/d43_emit.py / port/src/pcmodels.c).

Format spec: docs/internals.md D78-D82. Summary:

bg/*.seg (raw, uncompressed on ROM; stays uncompressed on PC):
  header (0x40B, only words 0-4 consulted): word0 must be 0 (bswap32,
  harmless); word1/2/3/4 are 0x0Fxxxxxx self-relative offsets (room table,
  portal table, envdata table [0=absent], optional f32 array [unused in
  this ROM -- word4 always 0, asserted]).
  - room table [word1, word3-or-word2): bg_room_data, 24B, NO resize
    (D79 makes 3 offset fields u32 on PC) -- blanket bswap32 all 6 words.
  - envdata table [word3, word2) if word3!=0: 8B records (type u8 + pad[3]
    + s32 data), NO resize -- bswap32 data; ENVIRONMENTDATA_ALT (100)
    records ALSO get +portal_delta (same offset space as offset_portal).
  - portal table [word2, ...): bg_portal_data_entry grows 8B->16B on PC
    (offset_portal stays a real pointer, dereferenced elsewhere in bg.c --
    D80). portal_delta = 8*(N+1) inserted bytes. offset_portal values
    (this table AND ALT envdata) get bswap32 + portal_delta. The
    bg_portal_entry point-data blob that follows (16B on both platforms,
    no resize) is relocated by +portal_delta as a block; numPoints/pad
    copied verbatim, point (3xf32) bswapped.
  Net: file grows by exactly portal_delta bytes; nothing before word2
  needs remapping.

Tbg_*_stanZ (RZ-compressed: 0x11 0x72 + raw deflate, decompress/recompress):
  StanPrefixRecord { s32 stanfile; StandTile *ptr_firstroom; } is
  dereferenced directly against the loaded buffer, so ptr_firstroom's file
  position must match the PC-compiled offset (8, not 4 -- pointer
  alignment). Converter inserts a 4B pad after stanfile, then widens each
  room-offset array slot 4B->8B (array_delta = 4*(N+2), N = entries before
  the NULL terminator). Tile data (from the old array end to EOF) shifts by
  +array_delta as a block; tiles need NO resize (D78 restores the exact
  N64 8-byte header stride). Per tile: id/room word gets its idHi half
  (bytes 0-1) byte-swapped so stanMatchTileName's `(u16)tile->x` alias
  reads stanIdHi on LE; idLo (byte 2) + room (byte 3) stay put (D88.5).
  mid.half/tail.half bswap16;
  pointCount (top nibble of raw BE tail half) selects record size via
  list_of_tilesizes (8+8*pointCount); each point (x/y/z s16 + link u16)
  bswap16.

Output: data/pccg-<region>/pccg.bin (concatenated, 16-aligned) +
manifest.csv (name,offset,size decimal; file_resource_table.inc.c order).
"""
import csv, struct, zlib, os, re, sys

REGION = "ntsc-final"
if len(sys.argv) > 1 and sys.argv[1] in ("ntsc-final", "pal-final", "jpn-final"):
    REGION = sys.argv[1]

ROM_PATH = f"data/ge007.{REGION}.z64"
OUT_DIR = f"data/pccg-{REGION}"
TABLE = "assets/obseg/file_resource_table.inc.c"
FILELIST = "scripts/filelist.u.csv"

if not os.path.exists(ROM_PATH):
    print(f"SKIP: {ROM_PATH} not present in this environment", file=sys.stderr)
    sys.exit(0)

rom = open(ROM_PATH, "rb").read()
rows = list(csv.reader(open(FILELIST)))
fl_by_base = {}
for r in rows:
    if len(r) < 3 or not r[2]:
        continue
    base = r[2].rsplit("/", 1)[-1]
    if base.endswith(".bin"):
        base = base[:-4]
    fl_by_base[base] = (int(r[0]), int(r[1]))

def find_row(basename):
    return fl_by_base.get(basename)

MASK = 0xFFFFFF
def be32(b, o): return struct.unpack_from(">I", b, o)[0]
def bs16(b, o): return struct.unpack_from(">h", b, o)[0]

def bswap32_bytes(v): return struct.pack("<I", v & 0xFFFFFFFF)
def bswap16_bytes(v): return struct.pack("<H", v & 0xFFFF)

errors = []
manifest = []   # (name, offset, size) in table order
chunks = []
cur_off = 0

def emit(name, data):
    global cur_off
    start = (cur_off + 15) & ~15
    if start > cur_off:
        chunks.append(b"\x00" * (start - cur_off))
        cur_off = start
    manifest.append((name, cur_off, len(data)))
    chunks.append(data)
    cur_off += len(data)

# --------------------------------------------------------------- bg .seg ---
def convert_seg(name, src):
    D = len(src)
    if D < 0x40:
        errors.append(f"{name}: too small ({D})")
        return None
    h0 = be32(src, 0)
    h = [be32(src, 4 * i) & MASK for i in range(5)]
    h1, h2, h3, h4 = h[1], h[2], h[3], h[4]
    if h0 != 0:
        errors.append(f"{name}: header word0 != 0 ({h0:#x})")
    if h4 != 0:
        errors.append(f"{name}: header word4 != 0 ({h4:#x}) -- f32 array "
                       f"path not implemented (spec D80); needs manual review")
        return None
    if h2 == 0 or h2 <= h1:
        errors.append(f"{name}: portal offset {h2:#x} <= room offset {h1:#x}")
        return None
    room_end = h3 if (h3 and h3 > h1 and h3 < h2) else h2
    if (room_end - h1) % 24 != 0:
        errors.append(f"{name}: room table extent {room_end - h1:#x} not a "
                       f"multiple of 24")
        return None
    nrooms = (room_end - h1) // 24

    # ---- walk portal table to find N (record count before terminator) and
    # collect every offset_portal target (raw file offsets into the
    # point-data blob that follows) ----
    o = h2
    n_portals = 0
    portal_targets = []
    while o + 8 <= D:
        v = be32(src, o) & MASK
        if v == 0:
            break
        portal_targets.append(v)
        n_portals += 1
        o += 8
    else:
        errors.append(f"{name}: portal table terminator not found before EOF")
        return None
    portal_table_end_n64 = o + 8  # includes terminator
    portal_delta = 8 * (n_portals + 1)  # 16B/record on PC vs 8B on N64

    # ---- envdata ALT targets also live in the portal point-data blob's
    # offset space (D80) -- collect them too (read-only pre-scan; envdata
    # itself is written below). ----
    ENVIRONMENTDATA_ALT = 100
    alt_targets = []
    if h3 and room_end == h3:
        o2 = h3
        while o2 < h2:
            etype = src[o2]
            data = struct.unpack_from(">i", src, o2 + 4)[0]
            if etype == ENVIRONMENTDATA_ALT and data:
                alt_targets.append(data & MASK)
            o2 += 8
            if etype == 0:
                break

    # ---- build the [portal_table_end_n64, D) region map from the actual
    # referenced targets (NOT a blind sequential walk: bg_portal_entry is
    # variable-sized -- `4 + 12*numPoints` bytes, consumer indexes
    # `(&entry->point)[i]` for i in [0, numPoints), MAXPORTALSPERROOM=20 --
    # and some targets can be unreferenced/interleaved in ways a naive walk
    # misreads). Every distinct target's size is read from its own
    # numPoints byte; targets are sorted and must tile the blob exactly
    # (gaps -- if any -- are copied verbatim, unconverted; overlaps are a
    # hard error). Only the region list's cumulative start shifts (by
    # portal_delta); relative order/spacing is preserved untouched. ----
    targets = sorted(set(portal_targets) | set(alt_targets))
    blob_regions = []  # (start, size) sorted, tiling [portal_table_end_n64, D)
    cursor = portal_table_end_n64
    for t in targets:
        if t < cursor:
            errors.append(f"{name}: portal target {t:#x} overlaps previous "
                           f"region (cursor {cursor:#x})")
            return None
        if t > cursor:
            blob_regions.append((cursor, t - cursor, False))  # gap, verbatim
        numPoints = src[t]
        if numPoints == 0 or numPoints > 20:
            errors.append(f"{name}: portal target {t:#x} numPoints="
                           f"{numPoints} implausible")
            return None
        recsize = 4 + 12 * numPoints
        if t + recsize > D:
            errors.append(f"{name}: portal target {t:#x} size {recsize:#x} "
                           f"overruns EOF")
            return None
        blob_regions.append((t, recsize, True))  # real record, convert
        cursor = t + recsize
    if cursor < D:
        blob_regions.append((cursor, D - cursor, False))  # trailing pad

    out = bytearray(D + portal_delta)

    # header: bswap all 5 words (word2 gets +portal_delta; word1/word3
    # (if present) are unaffected -- they sit before word2's table).
    # Only the CONTENT after the portal table shifts (+portal_delta); the
    # table's own start position is unchanged (we grow forward from h2, we
    # don't relocate h2 itself) -- so every header word stays put.
    out[0:4] = bswap32_bytes(h0)
    out[4:8] = bswap32_bytes(0x0F000000 | h1)
    out[8:12] = bswap32_bytes(0x0F000000 | h2)
    out[12:16] = bswap32_bytes(0x0F000000 | h3 if h3 else 0)
    out[16:20] = bswap32_bytes(0)
    # bytes [20, 0x40) of the header are never read by load_bg_file; copy
    # verbatim (harmless either way, kept for byte-for-byte accountability).
    out[20:0x40] = src[20:0x40]

    # room table: [h1, room_end), 24B records, no resize. The first 3 words
    # (pPointTableBin/pPriMappingBin/pSecMappingBin, D79) are 0x0Fxxxxxx
    # offsets INTO the portal point-data blob's tail (verified: BUNKER1
    # room[1].pPointTableBin == the exact byte where the portal blob's
    # trailing per-room RZ-compressed DL/point-index data begins) -- so
    # they need +portal_delta like every other forward reference into that
    # region, in addition to bswap32. `pos` (3xf32) is a plain bswap32,
    # no delta. Only load_bg_file's own use (subtracting two such fields to
    # get a byte count) doesn't strictly need this -- delta cancels in a
    # subtraction -- but downstream room-streaming code (out of scope for
    # this milestone, D75-adjacent) may dereference them directly, so shift
    # them for consistency; the referenced bytes themselves are left
    # untouched (RZ-compressed room binaries, byte-stream, D80/out of
    # scope for the "loads without fault + plausible pixels" bar).
    for o2 in range(h1, room_end, 24):
        for w in range(3):
            v = be32(src, o2 + 4 * w) & MASK
            nv = (0x0F000000 | ((v + portal_delta) & MASK)) if v else 0
            out[o2 + 4 * w:o2 + 4 * w + 4] = bswap32_bytes(nv)
        for w in range(3):
            fo = o2 + 12 + 4 * w
            out[fo:fo + 4] = bswap32_bytes(be32(src, fo))

    # envdata table (if present): [room_end, h2) when h3 nonzero and
    # room_end == h3; 8B records, type+pad verbatim, data bswap32 (+delta
    # for ENVIRONMENTDATA_ALT == 100).
    if h3 and room_end == h3:
        o2 = h3
        while o2 < h2:
            etype = src[o2]
            out[o2] = etype
            out[o2 + 1:o2 + 4] = src[o2 + 1:o2 + 4]
            data = struct.unpack_from(">i", src, o2 + 4)[0]
            if etype == ENVIRONMENTDATA_ALT:
                data = (data + portal_delta) if data else data
            out[o2 + 4:o2 + 8] = struct.pack("<i", data)
            o2 += 8
            if etype == 0:
                break

    # portal table: [h2, portal_table_end_n64) N64 8B records -> PC 16B.
    o2 = h2
    # The PC portal table starts at the SAME file offset h2 (we grow
    # forward, not shift the table's own start); only what comes AFTER
    # the (now-larger) table moves.
    pc_pos = h2
    idx = 0
    while idx <= n_portals:  # n_portals data records + 1 terminator
        off_val = be32(src, o2) & MASK
        cr1, cr2, cb1, cb2 = src[o2 + 4], src[o2 + 5], src[o2 + 6], src[o2 + 7]
        if off_val:
            # offset_portal keeps the native 0x0Fxxxxxx encoding
            # (BG_SEG_TO_PTR folds it, same as every other bg offset) --
            # low 4 bytes = relocated pattern, high 4 bytes zero (pointer
            # field growth, D80).
            val = 0x0F000000 | ((off_val + portal_delta) & MASK)
            out[pc_pos:pc_pos + 4] = bswap32_bytes(val)
        else:
            out[pc_pos:pc_pos + 4] = b"\x00\x00\x00\x00"
        out[pc_pos + 4:pc_pos + 8] = b"\x00\x00\x00\x00"
        out[pc_pos + 8] = cr1
        out[pc_pos + 9] = cr2
        out[pc_pos + 10] = cb1
        out[pc_pos + 11] = cb2
        out[pc_pos + 12:pc_pos + 16] = b"\x00\x00\x00\x00"
        o2 += 8
        pc_pos += 16
        idx += 1
    assert o2 == portal_table_end_n64, (name, hex(o2), hex(portal_table_end_n64))
    assert pc_pos == h2 + 16 * (n_portals + 1)

    # portal point-data blob: write blob_regions (computed above), each
    # relocated by +portal_delta; real records get numPoints/pad verbatim +
    # bswap32 per f32; gaps/trailing pad copied verbatim (checked non-zero
    # as a sanity signal, but copied either way -- never addressed by any
    # offset).
    for (start, size, is_record) in blob_regions:
        dst = start + portal_delta
        if not is_record:
            # Not portal-entry data or padding -- this is the per-room
            # RZ-compressed DL/point-index binary tail (D80: referenced by
            # bg_room_data's pPointTableBin/pPriMappingBin/pSecMappingBin,
            # not by any offset_portal/ALT-envdata value). load_bg_file
            # itself never dereferences it (only computes byte-count
            # deltas between adjacent room offsets); actual room streaming
            # is out of scope for this milestone (D75-adjacent). Byte
            # stream -- copy verbatim, relocated.
            out[dst:dst + size] = src[start:start + size]
            continue
        out[dst] = src[start]                              # numPoints
        out[dst + 1:dst + 4] = src[start + 1:start + 4]     # pad verbatim
        for k in range((size - 4) // 4):
            v = struct.unpack_from(">I", src, start + 4 + 4 * k)[0]
            out[dst + 4 + 4 * k:dst + 8 + 4 * k] = bswap32_bytes(v)

    return bytes(out)

# ------------------------------------------------------------- stan file ---
TILESIZES = [0x20, 0x20, 0x20, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0]

def convert_stan(name, src):
    D = len(src)
    if D < 8:
        errors.append(f"{name}: too small ({D})")
        return None
    stanfile = struct.unpack_from(">i", src, 0)[0]
    # room-offset array: starts at file offset 4 on N64, u32 entries,
    # NULL-terminated.
    offs = []
    o = 4
    while o + 4 <= D:
        v = be32(src, o) & 0xFFFFFFFF
        offs.append(v)
        o += 4
        if v == 0:
            break
    else:
        errors.append(f"{name}: room-offset array terminator not found")
        return None
    N = len(offs) - 1  # entries before the terminator
    old_array_end = o           # == 4 + 4*(N+1)
    array_delta = 4 * (N + 2)   # (8 + 8*(N+1)) - (4 + 4*(N+1))
    new_tiledata_start = old_array_end + array_delta
    assert new_tiledata_start == 8 + 8 * (N + 1)

    out = bytearray(D + array_delta)
    out[0:4] = struct.pack("<i", stanfile)
    out[4:8] = b"\x00\x00\x00\x00"  # PC pointer-alignment pad
    p = 8
    for v in offs:
        if v:
            nv = v + array_delta
            out[p:p + 4] = bswap32_bytes(nv)
        else:
            out[p:p + 4] = b"\x00\x00\x00\x00"
        out[p + 4:p + 8] = b"\x00\x00\x00\x00"
        p += 8
    assert p == new_tiledata_start

    # tile records: old_array_end .. D (N64 offsets) -> +array_delta (PC).
    src_o = old_array_end
    dst_o = new_tiledata_start
    ntiles = 0
    while src_o + 4 <= D:
        if src[src_o:src_o + 4] == b"\x00\x00\x00\x00":
            out[dst_o:dst_o + 4] = b"\x00\x00\x00\x00"
            break
        # id/room word. N64 (BE) bytes = [id23:16, id15:8, id7:0, room].
        # D88.5: stanMatchTileName reads the id via a StandTilePoint alias --
        # `(u16)tile->x` (a little-endian scalar load of bytes 0-1) must equal
        # stanIdHi, and `*((u8*)&tile->y)` (byte 2) must equal stanIdLo. So the
        # idHi half (bytes 0-1) has to be byte-swapped for the LE read; byte 2
        # (idLo) and byte 3 (room, read as tile->room) stay put. D78's "id is
        # provably dead" missed this aliased scalar read.
        w = src[src_o:src_o + 4]
        out[dst_o:dst_o + 4] = bytes((w[1], w[0], w[2], w[3]))
        midhalf = bs16(src, src_o + 4)
        tailhalf = bs16(src, src_o + 6)
        out[dst_o + 4:dst_o + 6] = bswap16_bytes(midhalf)
        out[dst_o + 6:dst_o + 8] = bswap16_bytes(tailhalf)
        pointCount = (tailhalf >> 12) & 0xF
        sz = TILESIZES[pointCount]
        if sz == 0:
            errors.append(f"{name}: bad pointCount {pointCount} at "
                           f"src {src_o:#x}")
            return None
        if src_o + sz > D:
            errors.append(f"{name}: tile at {src_o:#x} size {sz:#x} "
                           f"overruns EOF {D:#x}")
            return None
        for i in range(pointCount):
            po = src_o + 8 + 8 * i
            qo = dst_o + 8 + 8 * i
            x = bs16(src, po); y = bs16(src, po + 2); z = bs16(src, po + 4)
            link = struct.unpack_from(">H", src, po + 6)[0]
            out[qo:qo + 2] = bswap16_bytes(x)
            out[qo + 2:qo + 4] = bswap16_bytes(y)
            out[qo + 4:qo + 6] = bswap16_bytes(z)
            out[qo + 6:qo + 8] = bswap16_bytes(link)
        src_o += sz
        dst_o += sz
        ntiles += 1
    else:
        errors.append(f"{name}: ran off EOF without an all-zero EOF sentinel")
        return None

    # trailing pad bytes (if any) after the EOF sentinel: copy verbatim,
    # shifted by array_delta (never read by the game, but keeps file size
    # accounting exact / round-trippable).
    tail_src_o = src_o + 4
    tail_dst_o = dst_o + 4
    if tail_src_o < D:
        out[tail_dst_o:tail_dst_o + (D - tail_src_o)] = src[tail_src_o:D]

    return bytes(out)

# ------------------------------------------------------------------- main ---
table_names = []
for line in open(TABLE):
    m = re.search(r'\{\s*\w+\s*,\s*"([^"]+)"', line)
    if m:
        table_names.append(m.group(1))

n_seg = n_stan = 0
for name in table_names:
    if name.startswith("bg/") and name.endswith(".seg"):
        base = name.rsplit("/", 1)[-1][:-4]
        row = find_row(base)
        if not row:
            continue  # unused in this ROM build (size-0 filelist rows / absent)
        addr, size = row
        if size == 0:
            continue
        src = rom[addr:addr + size]
        out = convert_seg(name, src)
        if out is not None:
            emit(name, out)
            n_seg += 1
    elif (name.startswith("Tbg_") or name == "TbgxZ") and name.endswith("Z"):
        row = find_row(name)
        if not row:
            continue
        addr, size = row
        if size == 0:
            continue
        comp = rom[addr:addr + size]
        if comp[:2] != b"\x11\x72":
            errors.append(f"{name}: bad RZ magic {comp[:2].hex()}")
            continue
        try:
            dec = zlib.decompress(comp[2:], -15)
        except zlib.error as e:
            errors.append(f"{name}: decompress failed: {e}")
            continue
        out = convert_stan(name, dec)
        if out is not None:
            co = zlib.compressobj(6, zlib.DEFLATED, -15)
            comp_out = b"\x11\x72" + co.compress(out) + co.flush()
            emit(name, comp_out)
            n_stan += 1

print(f"converted: {n_seg} bg/*.seg + {n_stan} Tbg_*_stanZ files")

if errors:
    print(f"\n{len(errors)} ERRORS:")
    for e in errors[:60]:
        print("  ", e)
    sys.exit(1)

os.makedirs(OUT_DIR, exist_ok=True)
with open(os.path.join(OUT_DIR, "pccg.bin"), "wb") as f:
    for c in chunks:
        f.write(c)
with open(os.path.join(OUT_DIR, "manifest.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["name", "offset", "size"])
    for name, o, s in manifest:
        w.writerow([name, o, s])
print(f"wrote {len(manifest)} sidecars to {OUT_DIR}/pccg.bin "
      f"({cur_off} bytes total)")
print("\nALL CHECKS PASSED")
