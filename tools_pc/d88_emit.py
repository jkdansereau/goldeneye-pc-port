#!/usr/bin/env python3
"""D88: offline emit pass -- convert per-level "Usetup*Z" stage-setup files
to PC layout, appending them to the same pccg.bin/manifest.csv sidecar image
produced by d69_emit.py (port/src/pccg.c already serves any filename listed
in the manifest, so this script just needs to add rows).

Format spec: docs/PCPortResearch.md D88.x. Summary:

`struct stagesetup` (bondtypes.h) is a 10-pointer-field header (40B on N64,
80B on PC -- pointers grow 4->8B) whose fields store plain (non-segment)
byte offsets from the start of the (RZ-decompressed) file. Everything the
header points to is walked with **real C array indexing** against the live
struct types (waypoint/waygroup/PathRecord/AIListRecord/PadRecord/
BoundPadRecord/pname) -- each of which also has pointer fields that grow
4->8B on PC -- so, unlike D69's bg/stan (which could stay size-preserving
because the widened fields were never dereferenced with array-index
semantics), these 8 tables genuinely grow and must be resized with
delta-relocation (same technique as D69's D80 portal table / D81 stan
room-offset array, generalized to many interleaved regions).

Verified against BUNKER1 (UsetupsevbunkerZ) by full byte-level ROM
reconstruction (offsets/counts cross-checked against every pointer field in
the file -- see tools_pc/d88_emit.py history / PCPortResearch.md D88.2):
the file's leftover byte-stream regions (propDefs polymorphic prop-def
table, AI opcode streams, pad/boundpad `plink` name strings, pad/boundpad
name (`pname`) strings) all sit BETWEEN and interleaved with the 8 growing
tables, and were confirmed to exactly tile the file (zero gaps beyond a few
bytes of trailing alignment padding, zero overlaps) once every offset
reachable from a table field is treated as a region boundary.

Growing regions (delta-relocated, N64->PC per-record growth):
  header:          40B  -> 80B   (10 x 4B offset -> 10 x 8B pointer-shaped)
  waypoint:        16B  -> 24B   (padID s32, neighbours ptr, groupNum, dist)
  waygroup:        12B  -> 24B   (neighbours ptr, waypoints ptr, dist)
  PathRecord:        8B -> 16B   (waypoints ptr, ID u8, isLoop u8, len u16)
  AIListRecord:       8B -> 16B  (ailist ptr, ID s32)
  PadRecord:        44B -> 56B   (pos/up/look, plink ptr, stan ptr[dead-on-load])
  BoundPadRecord:   68B -> 80B   (PadRecord + bbox)
  pname:             4B -> 8B    (union{char*, s32})

Non-growing regions (bswap only, or pure byte passthrough, no delta beyond
the cumulative shift from growth ahead of them in the file):
  - propDefs: a polymorphic byte stream of ~40 prop-definition record types
    selected by a 1-byte `type` tag (sizepropdef() walk in
    loadobjectmodel.c). NOT byte-swapped or resized by this converter --
    the walk itself only reads the u8 `type` tag (endian-irrelevant) and
    calls sizepropdef(), whose native sizeof() is correct as long as PC/N64
    struct sizes match (out of scope here -- separate finding, D88.4).
    Treated as an opaque passthrough blob (relocated, bytes untouched).
  - waypoint.neighbours / waygroup.neighbours / waygroup.waypoints /
    PathRecord.waypoints target arrays: plain `s32` ID lists (NOT file
    offsets -- graph node indices), NULL/-1 terminated, walked with real
    `[]` indexing elsewhere (chrai.c/pathfinding) -- bswap32 per element,
    no resize (element width is 4B on both platforms).
  - PadRecord/BoundPadRecord.plink, pname.p: NUL-terminated C strings --
    byte passthrough, no swap.
  - AIListRecord.ailist: raw AI opcode byte stream -- byte passthrough, no
    swap (opcode args' endianness is unaudited; deferred, D88.4).
  - `intro` (SetupIntroXxx polymorphic records): DOES need bswap (the type
    discriminant is a full s32, unlike propDefs' u8) but no resize --
    SetupIntroCamera's prev/lang1c.lang_ptr/lang20.lang_ptr fields were
    narrowed to u32 under #ifdef PORT (dead-on-load, D88.3) specifically so
    every SetupIntroXxx record keeps its N64 byte size on PC.

Algorithm: discover every region (the header; the 8 growing tables via
terminator-walk; `intro` via type-tag walk; propDefs implicitly as
[boundpads_end, intro_start)) plus every LEAF region (every s32-array /
string / AI-stream start reachable from a table field, walked to its own
terminator or left as a bare start). Sort all region starts, tile the file
(each region's end = its own known size, or the next region's start),
assert full coverage, then emit with a running cumulative-delta: growing
regions insert `count * (new_recsize - old_recsize)` bytes; every pointer
field's new value = old (bswapped) offset + the delta recorded at ITS
target region's start.

Output: appended to the SAME data/pccg-<region>/pccg.bin + manifest.csv
produced by d69_emit.py (run d69_emit.py first, or this script alone if the
existing sidecar+manifest are present -- it loads and extends them).
"""
import csv, struct, zlib, os, re, sys
from d88_propdefs import convert_stream as convert_propdefs, PropDefError

REGION = "ntsc-final"
REGEN = "--regen" in sys.argv[1:]
for _a in sys.argv[1:]:
    if _a in ("ntsc-final", "pal-final", "jpn-final"):
        REGION = _a

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

def be32(b, o): return struct.unpack_from(">I", b, o)[0]
def bs32(b, o): return struct.unpack_from(">i", b, o)[0]
def bs16(b, o): return struct.unpack_from(">h", b, o)[0]
def bswap32_bytes(v): return struct.pack("<I", v & 0xFFFFFFFF)
def bswap16_bytes(v): return struct.pack("<H", v & 0xFFFF)
def bswap32_bytes_s(v): return struct.pack("<i", v)

FIELD_NAMES = ["pathwaypoints", "waypointgroups", "intro", "propDefs",
               "patrolpaths", "ailists", "pads", "boundpads", "padnames",
               "boundpadnames"]

INTRO_SZ = {0: 12, 1: 16, 2: 16, 3: 32, 4: 8, 5: 8, 6: 40, 7: 12, 8: 8, 9: 4}

errors = []

def convert_usetup(name, src):
    D = len(src)
    if D < 0x28:
        errors.append(f"{name}: too small ({D})")
        return None
    hdr = [be32(src, 4 * i) for i in range(10)]
    H = dict(zip(FIELD_NAMES, hdr))
    for fn in FIELD_NAMES:
        if H[fn] != 0 and H[fn] >= D:
            errors.append(f"{name}: header field {fn} offset {H[fn]:#x} >= filesize {D:#x}")
            return None

    # ---- walk the 8 growing tables (terminator-defined) ----
    def walk_fixed(off, recsize, term_off, term_kind):
        """term_kind: 's32neg' terminate when signed<0; 'u32zero' terminate
        when raw u32==0. Returns list of record-start offsets (excludes the
        terminator record) and the terminator record's own start."""
        if off == 0:
            return [], None
        starts = []
        o = off
        guard = 0
        while guard < 100000:
            guard += 1
            v = be32(src, o + term_off)
            term = (bs32(src, o + term_off) < 0) if term_kind == 's32neg' else (v == 0)
            if term:
                return starts, o
            starts.append(o)
            o += recsize
        errors.append(f"{name}: runaway walk_fixed at {off:#x}")
        return None, None

    wp_starts, wp_term = walk_fixed(H["pathwaypoints"], 16, 0, 's32neg')
    wg_starts, wg_term = walk_fixed(H["waypointgroups"], 12, 0, 'u32zero')
    pp_starts, pp_term = walk_fixed(H["patrolpaths"], 8, 0, 'u32zero')
    al_starts, al_term = walk_fixed(H["ailists"], 8, 0, 'u32zero')
    pd_starts, pd_term = walk_fixed(H["pads"], 44, 0x24, 'u32zero')
    bp_starts, bp_term = walk_fixed(H["boundpads"], 68, 0x24, 'u32zero')
    pn_starts, pn_term = walk_fixed(H["padnames"], 4, 0, 'u32zero')
    bpn_starts, bpn_term = walk_fixed(H["boundpadnames"], 4, 0, 'u32zero')
    GROW_TABLES = [
        ("pads", pd_starts, pd_term, 44, 56),
        ("boundpads", bp_starts, bp_term, 68, 80),
        ("waypointgroups", wg_starts, wg_term, 12, 24),
        ("pathwaypoints", wp_starts, wp_term, 16, 24),
        ("patrolpaths", pp_starts, pp_term, 8, 16),
        ("ailists", al_starts, al_term, 8, 16),
        ("padnames", pn_starts, pn_term, 4, 8),
        ("boundpadnames", bpn_starts, bpn_term, 4, 8),
    ]
    for fn, starts, term, oldsz, newsz in GROW_TABLES:
        if starts is None:
            return None

    # ---- intro: type-tag walk (fixed size per record, no growth) ----
    intro_recs = []
    intro_end = H["intro"]
    if H["intro"]:
        o = H["intro"]
        guard = 0
        while guard < 5000:
            guard += 1
            t = bs32(src, o)
            if t not in INTRO_SZ:
                errors.append(f"{name}: unknown intro type {t} at {o:#x}")
                return None
            intro_recs.append((o, t))
            sz = INTRO_SZ[t]
            o += sz
            if t == 9:  # INTROTYPE_END
                break
        else:
            errors.append(f"{name}: intro walk runaway")
            return None
        intro_end = o

    # ---- leaf sub-regions: s32 ID arrays / strings / AI streams ----
    def walk_s32arr(off):
        o = off
        guard = 0
        while guard < 100000:
            guard += 1
            if bs32(src, o) < 0:
                return o + 4
            o += 4
            guard += 1
        errors.append(f"{name}: runaway s32 array at {off:#x}")
        return None

    def walk_cstr(off):
        o = src.find(b"\x00", off)
        if o < 0:
            errors.append(f"{name}: unterminated string at {off:#x}")
            return None
        return o + 1

    leaves = {}  # start -> ('s32arr'|'cstr', end)

    def add_leaf(start, kind, end_fn):
        if start == 0 or start in leaves:
            return
        end = end_fn(start)
        if end is None:
            return
        leaves[start] = (kind, end)

    for (o, _) in [(s, None) for s in wp_starts]:
        add_leaf(be32(src, o + 4), 's32arr', walk_s32arr)
    for o in wg_starts:
        add_leaf(be32(src, o + 0), 's32arr', walk_s32arr)
        add_leaf(be32(src, o + 4), 's32arr', walk_s32arr)
    for o in pp_starts:
        add_leaf(be32(src, o + 0), 's32arr', walk_s32arr)
    for o in pd_starts:
        add_leaf(be32(src, o + 0x24), 'cstr', walk_cstr)
    for o in bp_starts:
        add_leaf(be32(src, o + 0x24), 'cstr', walk_cstr)
    for o in pn_starts:
        add_leaf(be32(src, o), 'cstr', walk_cstr)
    for o in bpn_starts:
        add_leaf(be32(src, o), 'cstr', walk_cstr)
    # ailist opcode streams: start known, but length is data-driven (no
    # generic terminator) -- treated as al_starts contributing ONLY a
    # region-boundary marker (zero-length placeholder); true extent is
    # whatever the tiling pass between sorted boundaries resolves to.
    ai_stream_starts = set()
    for o in al_starts:
        s = be32(src, o)
        if s and s not in leaves:
            ai_stream_starts.add(s)

    # ---- assemble full region list: (start, end_or_None, kind, meta) ----
    regions = []  # start, fixed_end (or None), kind
    regions.append((0, 0x28, 'header', None))
    for fn, starts, term, oldsz, newsz in GROW_TABLES:
        if term is None:
            continue
        end = term + oldsz
        regions.append((H[fn], end, 'grow:' + fn, (oldsz, newsz, starts + [term])))
    if H["intro"]:
        regions.append((H["intro"], intro_end, 'intro', intro_recs))
    if H["propDefs"]:
        regions.append((H["propDefs"], None, 'propdefs', None))
    for start, (kind, end) in leaves.items():
        regions.append((start, end, 'leaf:' + kind, None))
    for start in ai_stream_starts:
        regions.append((start, None, 'leaf:aistream', None))

    regions.sort(key=lambda r: r[0])
    # resolve None ends to next region's start (or EOF)
    resolved = []
    for i, (start, end, kind, meta) in enumerate(regions):
        if end is None:
            end = regions[i + 1][0] if i + 1 < len(regions) else D
        resolved.append((start, end, kind, meta))
    regions = resolved

    # verify tiling: contiguous, non-overlapping, covers [0, D); small
    # unclaimed gaps between two known regions (alignment padding -- seen
    # e.g. 4 bytes between the ailist table and the following plink-string
    # blob in BUNKER1/arch) become synthetic opaque passthrough regions
    # instead of a hard failure. Overlaps are still a hard error.
    cursor = 0
    tiled = []
    for (start, end, kind, meta) in regions:
        if start < cursor:
            errors.append(f"{name}: region overlap before {kind}@{start:#x} "
                           f"(cursor was {cursor:#x})")
            return None
        if start > cursor:
            tiled.append((cursor, start, 'gap', None))
        if end < start:
            errors.append(f"{name}: region {kind}@{start:#x} has negative length")
            return None
        tiled.append((start, end, kind, meta))
        cursor = end
    if cursor < D:
        tiled.append((cursor, D, 'gap', None))
    elif cursor > D:
        errors.append(f"{name}: regions overrun EOF ({cursor:#x} > {D:#x})")
        return None
    regions = tiled

    # ---- convert the propDefs polymorphic record stream (D88.4) ----
    # Every propDef record grows to its native PC struct size (pointer members
    # widen 4->8B). Done up front so the growth feeds the cumulative delta.
    propdefs_pc = None
    if H["propDefs"]:
        pd_start = H["propDefs"]
        pd_end = None
        for i, (start, end, kind, meta) in enumerate(regions):
            if kind == 'propdefs':
                pd_end = end
                break
        try:
            propdefs_pc, _n64len, _pclen = convert_propdefs(src, pd_start, pd_end)
        except PropDefError as e:
            errors.append(f"{name}: {e}")
            return None

    # ---- pass 1: compute cumulative delta at each region start ----
    delta_at = {}
    cum = 0x28  # header grows 0x28 -> 0x50
    for (start, end, kind, meta) in regions:
        if kind == 'header':
            delta_at[start] = 0  # header's own new start is always 0
            continue
        delta_at[start] = cum
        if kind.startswith('grow:'):
            oldsz, newsz, _starts = meta
            n = len(_starts) - 1  # records before terminator
            cum += (n + 1) * (newsz - oldsz)
        elif kind == 'propdefs' and propdefs_pc is not None:
            cum += len(propdefs_pc) - (end - start)
        # 'intro', 'leaf:*' -- no growth

    def reloc(off):
        """bswapped-original-offset semantics: off is a raw file offset (or
        0/absent); returns the relocated value (still a plain integer file
        offset, PC code adds the base pointer at runtime same as N64)."""
        if off == 0:
            return 0
        return off + delta_at.get(off, 0)

    total_new = D + cum
    out = bytearray(total_new)

    # ---- emit header (0x28 -> 0x50) ----
    for i, fn in enumerate(FIELD_NAMES):
        v = reloc(H[fn])
        out[8 * i:8 * i + 4] = bswap32_bytes(v)
        out[8 * i + 4:8 * i + 8] = b"\x00\x00\x00\x00"

    # ---- emit growing tables ----
    def emit_pad(dst_o, src_o, has_bbox):
        # pos/up/look: 9 floats, bswap32 each (bit-pattern preserving)
        for k in range(9):
            v = be32(src, src_o + 4 * k)
            out[dst_o + 4 * k:dst_o + 4 * k + 4] = bswap32_bytes(v)
        plink = be32(src, src_o + 0x24)
        out[dst_o + 0x28:dst_o + 0x2c] = bswap32_bytes(reloc(plink))
        out[dst_o + 0x2c:dst_o + 0x30] = b"\x00\x00\x00\x00"
        # stan: dead-on-load (init_pathtable_something always sets it before
        # use, D88 same class as D79) -- zero it, never read from file.
        out[dst_o + 0x30:dst_o + 0x38] = b"\x00\x00\x00\x00"
        if has_bbox:
            for k in range(6):
                v = be32(src, src_o + 0x2c + 4 * k)
                out[dst_o + 0x38 + 4 * k:dst_o + 0x38 + 4 * k + 4] = bswap32_bytes(v)

    for fn, starts, term, oldsz, newsz in GROW_TABLES:
        if term is None:
            continue
        all_starts = starts + [term]
        # NOTE: do NOT use reloc(so) per-record here -- delta_at only has an
        # entry for the region's OWN start (H[fn]); every record after the
        # first must be positioned at table_base + idx*newsz (all records in
        # a growing table share the same delta, but the delta is per-TABLE,
        # not per-record-start, since only the table's start offset is a key
        # in delta_at).
        table_base = reloc(H[fn])
        for idx, so in enumerate(all_starts):
            do = table_base + idx * newsz
            if fn == "pads":
                emit_pad(do, so, False)
            elif fn == "boundpads":
                emit_pad(do, so, True)
            elif fn == "waypointgroups":
                nb = be32(src, so + 0); wpv = be32(src, so + 4)
                dist = be32(src, so + 8)
                out[do:do + 4] = bswap32_bytes(reloc(nb)) if nb else b"\x00\x00\x00\x00"
                out[do + 4:do + 8] = b"\x00\x00\x00\x00"
                out[do + 8:do + 12] = bswap32_bytes(reloc(wpv)) if wpv else b"\x00\x00\x00\x00"
                out[do + 12:do + 16] = b"\x00\x00\x00\x00"
                out[do + 16:do + 20] = bswap32_bytes(dist)
                out[do + 20:do + 24] = b"\x00\x00\x00\x00"
            elif fn == "pathwaypoints":
                padID = be32(src, so + 0); nb = be32(src, so + 4)
                groupNum = be32(src, so + 8); dist = be32(src, so + 12)
                out[do:do + 4] = bswap32_bytes(padID)
                out[do + 4:do + 8] = b"\x00\x00\x00\x00"
                out[do + 8:do + 12] = bswap32_bytes(reloc(nb)) if nb else b"\x00\x00\x00\x00"
                out[do + 12:do + 16] = b"\x00\x00\x00\x00"
                out[do + 16:do + 20] = bswap32_bytes(groupNum)
                out[do + 20:do + 24] = bswap32_bytes(dist)
            elif fn == "patrolpaths":
                wpv = be32(src, so + 0)
                idb = src[so + 4]; isLoop = src[so + 5]
                length = struct.unpack_from(">H", src, so + 6)[0]
                out[do:do + 4] = bswap32_bytes(reloc(wpv)) if wpv else b"\x00\x00\x00\x00"
                out[do + 4:do + 8] = b"\x00\x00\x00\x00"
                out[do + 8] = idb
                out[do + 9] = isLoop
                out[do + 10:do + 12] = struct.pack("<H", length)
                out[do + 12:do + 16] = b"\x00\x00\x00\x00"
            elif fn == "ailists":
                al = be32(src, so + 0); ID = be32(src, so + 4)
                out[do:do + 4] = bswap32_bytes(reloc(al)) if al else b"\x00\x00\x00\x00"
                out[do + 4:do + 8] = b"\x00\x00\x00\x00"
                out[do + 8:do + 12] = bswap32_bytes(ID)
                out[do + 12:do + 16] = b"\x00\x00\x00\x00"
            elif fn in ("padnames", "boundpadnames"):
                p = be32(src, so)
                out[do:do + 4] = bswap32_bytes(reloc(p)) if p else b"\x00\x00\x00\x00"
                out[do + 4:do + 8] = b"\x00\x00\x00\x00"

    # ---- emit intro (bswap fields, same record size) ----
    # Same pitfall as the growing tables: only H["intro"] (the first
    # record's start) is a registered delta_at key; every later record's
    # start is derived, not a table field anyone points to, so it must
    # reuse the SAME (intro doesn't grow) base delta explicitly.
    intro_base_delta = delta_at.get(H["intro"], 0) if H["intro"] else 0
    for (o, t) in intro_recs:
        do = o + intro_base_delta
        sz = INTRO_SZ[t]
        if t == 6:  # CAMERA: 10 s32-sized fields then a 0 pad word (matches
                    # IntroCamera() macro emission: type,6xcoords/angles,
                    # lang1c,lang20,0 -- but consumed struct is 40B/10 words)
            for k in range(10):
                do_k = do + 4 * k
                if k == 7:
                    # lang1c is `union { u16 lang_index[2]; u32 lang_ptr; }`.
                    # bondview_r.c reads lang1c.lang_index[1] (the 2nd u16),
                    # so the two halves must keep their order -- bswap each
                    # u16 in place, NOT a whole-word bswap32 (which would
                    # swap element 0 and element 1). lang20 (k==8) really is
                    # an s32, so it stays a bswap32. (D88.6)
                    out[do_k:do_k + 2] = bswap16_bytes(bs16(src, o + 4 * k))
                    out[do_k + 2:do_k + 4] = bswap16_bytes(bs16(src, o + 4 * k + 2))
                else:
                    out[do_k:do_k + 4] = bswap32_bytes(be32(src, o + 4 * k))
        else:
            for k in range(sz // 4):
                v = be32(src, o + 4 * k)
                out[do + 4 * k:do + 4 * k + 4] = bswap32_bytes(v)

    # ---- emit leaves ----
    for start, (kind, end) in leaves.items():
        do = reloc(start)
        if kind == 's32arr':
            for o in range(start, end, 4):
                v = be32(src, o)
                out[do + (o - start):do + (o - start) + 4] = bswap32_bytes(v)
        elif kind == 'cstr':
            out[do:do + (end - start)] = src[start:end]

    # ---- emit propdefs (converted, D88.4) + aistreams / opaque verbatim ----
    for (start, end, kind, meta) in regions:
        if kind == 'propdefs':
            do = reloc(start)
            out[do:do + len(propdefs_pc)] = propdefs_pc
        elif kind in ('leaf:aistream', 'gap'):
            do = reloc(start)
            out[do:do + (end - start)] = src[start:end]

    return bytes(out)

# ------------------------------------------------------------------- main ---
table_names = []
for line in open(TABLE):
    m = re.search(r'\{\s*\w+\s*,\s*"([^"]+)"', line)
    if m:
        table_names.append(m.group(1))

usetup_names = sorted(set(n for n in table_names
                           if n.startswith("Usetup") and n.endswith("Z")))

manifest = []
chunks = []
cur_off = 0
existing_names = set()
os.makedirs(OUT_DIR, exist_ok=True)
bin_path = os.path.join(OUT_DIR, "pccg.bin")
man_path = os.path.join(OUT_DIR, "manifest.csv")
def is_usetup_row(nm):
    return nm.startswith("Usetup") and nm.endswith("Z")

def emit(name, data):
    global cur_off
    start = (cur_off + 15) & ~15
    if start > cur_off:
        chunks.append(b"\x00" * (start - cur_off))
        cur_off = start
    manifest.append((name, cur_off, len(data)))
    chunks.append(data)
    cur_off += len(data)

if os.path.exists(bin_path) and os.path.exists(man_path):
    old_rows = []
    with open(man_path, newline="") as f:
        r = csv.reader(f)
        next(r, None)
        for row in r:
            if len(row) == 3:
                old_rows.append((row[0], int(row[1]), int(row[2])))
    with open(bin_path, "rb") as f:
        base = f.read()
    if REGEN:
        # Drop every Usetup*Z row (manifest entry + its bytes) and rebuild the
        # sidecar from the surviving d69 rows only, so converter iteration
        # starts from a clean slate every run. Don't hand-edit manifest.csv.
        dropped = [nm for (nm, _o, _s) in old_rows if is_usetup_row(nm)]
        for (nm, o, s) in old_rows:
            if is_usetup_row(nm):
                continue
            emit(nm, base[o:o + s])
            existing_names.add(nm)
        print(f"--regen: kept {len(manifest)} d69 rows, dropped "
              f"{len(dropped)} Usetup*Z rows; sidecar rebuilt to {cur_off} bytes")
    else:
        manifest.extend(old_rows)
        for (nm, _o, _s) in old_rows:
            existing_names.add(nm)
        chunks.append(base)
        cur_off = len(base)
        print(f"extending existing sidecar: {len(base)} bytes, {len(manifest)} rows")

n_ok = 0
for name in usetup_names:
    if name in existing_names:
        continue
    row = fl_by_base.get(name)
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
    out = convert_usetup(name, dec)
    if out is not None:
        co = zlib.compressobj(6, zlib.DEFLATED, -15)
        comp_out = b"\x11\x72" + co.compress(out) + co.flush()
        emit(name, comp_out)
        n_ok += 1

print(f"converted: {n_ok}/{len(usetup_names)} Usetup*Z files")

if errors:
    print(f"\n{len(errors)} ERRORS:")
    for e in errors[:80]:
        print("  ", e)
    sys.exit(1)

with open(bin_path, "wb") as f:
    for c in chunks:
        f.write(c)
with open(man_path, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["name", "offset", "size"])
    for name, o, s in manifest:
        w.writerow([name, o, s])
print(f"wrote {len(manifest)} sidecars to {bin_path} ({cur_off} bytes total)")
print("\nALL CHECKS PASSED")
