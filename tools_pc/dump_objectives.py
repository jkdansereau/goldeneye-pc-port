#!/usr/bin/env python3
"""dump_objectives.py — dump per-level mission objectives + win/fail criteria
from the retail ROM, for human playtest validation (WS6).

Ground truth:
  * levelID -> stage file: `setup_text_pointers[]` in src/game/chraidata.c
    (parsed live from the source, index == levelID).
  * stage file = RZ-compressed `struct stagesetup`; header field [3]
    ("propDefs") is a plain byte offset into the decompressed file.
  * propDefs record walk: PROPDEF_N64_WORDS (tools_pc/d88_propdefs.py),
    same strides the runtime `sizepropdef()` uses.
  * criteria semantics: src/game/objective_status.c get_status_of_objective():
      25 DESTROY_OBJECT        tag id  -> incomplete while tagged object alive
      26 COMPLETE_CONDITION    flag id -> complete once stage flag set
      27 FAIL_CONDITION        flag id -> FAILED once stage flag set
      28 COLLECT_OBJECT        tag id  -> must be in Bond's inventory
      29 DEPOSIT_OBJECT        tag id  -> incomplete while in inventory
      30 PHOTOGRAPH            tag id  -> camera objective
      31 NULL                  (spacer)
      32 ENTER_ROOM            room ref @4 (runtime status word @8)
      33 DEPOSIT_IN_ROOM       refs @4/@8 (runtime status word @c)
      34 COPY_ITEM             key-analyzer / datavac copy objective
    Objective blocks run from record 23 (OBJECTIVE_START: obj index @4,
    briefing text id @8, min difficulty @c) to record 24 (OBJECTIVE_END).
    Tags: record 22 TagObjectRecord [u16 tagid @4][s16 offset-to-target @6];
    the target is another propDef record in the same stream.

Usage:
  python tools_pc/dump_objectives.py facility          # one level (name or -level_XX number)
  python tools_pc/dump_objectives.py all               # every solo level
"""
import csv
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from d88_propdefs import PROPDEF_N64_WORDS  # noqa: E402

REGION = "ntsc-final"
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ROM_PATH = os.path.join(ROOT, "data", f"ge007.{REGION}.z64")
FILELIST = os.path.join(ROOT, "scripts", "filelist.u.csv")
CHRAIDATA = os.path.join(ROOT, "src", "game", "chraidata.c")

DIFF_NAMES = {0: "Agent", 1: "Secret Agent", 2: "No One Lives Forever", 3: "GoldenEye"}
CRIT_NAMES = {
    25: "DESTROY_OBJECT", 26: "COMPLETE_CONDITION", 27: "FAIL_CONDITION",
    28: "COLLECT_OBJECT", 29: "DEPOSIT_OBJECT", 30: "PHOTOGRAPH",
    31: "NULL", 32: "ENTER_ROOM", 33: "DEPOSIT_IN_ROOM", 34: "COPY_ITEM",
}
TYPE_NAMES = {1: "DOOR", 2: "DOOR_SCALE", 3: "PROP", 4: "KEY", 5: "ALARM",
              6: "CCTV", 7: "MAGAZINE", 8: "COLLECTABLE", 9: "GUARD",
              10: "MONITOR", 11: "MULTI_MONITOR", 12: "RACK", 13: "AUTOGUN",
              14: "LINK", 17: "HAT", 18: "GUARD_ATTR", 19: "SWITCH",
              20: "AMMO", 21: "ARMOUR", 22: "TAG", 23: "OBJ_START",
              24: "OBJ_END", 25: "OBJ_DESTROY", 26: "OBJ_COMPLETE_COND",
              27: "OBJ_FAIL_COND", 28: "OBJ_COLLECT", 29: "OBJ_DEPOSIT",
              30: "OBJ_PHOTOGRAPH", 31: "OBJ_NULL", 32: "OBJ_ENTER_ROOM",
              33: "OBJ_DEPOSIT_IN_ROOM", 34: "OBJ_COPY_ITEM",
              35: "WATCH_OBJ_TEXT", 36: "GAS", 37: "RENAME", 38: "LOCK_DOOR",
              39: "VEHICLE", 40: "AIRCRAFT", 42: "GLASS", 43: "SAFE",
              44: "SAFE_ITEM", 45: "TANK", 46: "CAMERAPOS", 47: "TINTED_GLASS",
              48: "END"}


def be16(b, o): return struct.unpack_from(">H", b, o)[0]
def be32(b, o): return struct.unpack_from(">I", b, o)[0]
def bs32(b, o): return struct.unpack_from(">i", b, o)[0]


def load_level_table():
    """levelID -> Usetup file name, from setup_text_pointers[] in chraidata.c."""
    src = open(CHRAIDATA).read()
    m = re.search(r"setup_text_pointers\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        sys.exit("setup_text_pointers not found in chraidata.c")
    names = re.findall(r'"(Usetup\w+Z)"|NULL', m.group(1))
    return {i: n for i, n in enumerate(names) if n}


def load_rom_file(name):
    rom = open(ROM_PATH, "rb").read()
    fl = {}
    for r in csv.reader(open(FILELIST)):
        if len(r) >= 3 and r[2]:
            base = r[2].rsplit("/", 1)[-1]
            if base.endswith(".bin"):
                base = base[:-4]
            fl[base] = (int(r[0]), int(r[1]))
    if name not in fl:
        sys.exit(f"{name} not in filelist")
    addr, size = fl[name]
    comp = rom[addr:addr + size]
    if comp[:2] != b"\x11\x72":
        sys.exit(f"{name}: bad RZ magic {comp[:2].hex()}")
    return zlib.decompress(comp[2:], -15)


def walk_propdefs(buf, off):
    """Yield (offset, type, word0..n) for every record until PROPDEF_END."""
    recs = []
    while True:
        if off + 4 > len(buf):
            break
        w0 = be32(buf, off)
        rtype = w0 & 0xFF
        words = PROPDEF_N64_WORDS.get(rtype)
        if words is None:
            print(f"  !! unknown propdef type {rtype} at +{off:#x}", file=sys.stderr)
            break
        recs.append((off, rtype))
        off += words * 4
        if rtype == 48:
            break
    return recs


def dump_level(levelid, lav_name):
    fname = load_level_table().get(levelid)
    if not fname:
        print(f"[{lav_name}] level {levelid}: no stage file")
        return
    buf = load_rom_file(fname)
    poff = be32(buf, 12)  # header field [3] = propDefs offset
    recs = walk_propdefs(buf, poff)

    # tags: id -> (type of tagged record). OffsetToObj is a RECORD-INDEX
    # delta from the TAG record within the propDefs stream (verified:
    # values are tiny +/-2, never byte offsets).
    tags = {}
    for k, (off, t) in enumerate(recs):
        if t == 22:
            tid = be16(buf, off + 4)
            toff = struct.unpack_from(">h", buf, off + 6)[0]
            tt = recs[k + toff][1] if 0 <= k + toff < len(recs) else None
            tags[tid] = tt

    def tagdesc(tid):
        if tid in tags and tags[tid] is not None:
            tt = tags[tid]
            return f"tag {tid} -> {TYPE_NAMES.get(tt, 'type %s' % tt)}"
        return f"tag {tid} (unresolved)"

    print(f"\n=== {lav_name} (level {levelid}, {fname}) ===")
    # objective blocks: 23 ... 24
    i = 0
    nblocks = 0
    while i < len(recs):
        off, t = recs[i]
        if t != 23:
            i += 1
            continue
        nblocks += 1
        idx = bs32(buf, off + 4)
        textid = be32(buf, off + 8)
        # MinDificulty is a big-endian s32 at word 3 (offset 0xC); the value
        # lives in its low byte == offset 0xF.
        diff = buf[off + 15]
        print(f"Objective {idx}  [briefing text id {textid}]  min difficulty: "
              f"{DIFF_NAMES.get(diff, diff)}")
        j = i + 1
        while j < len(recs) and recs[j][1] != 24:
            coff, ct = recs[j]
            name = CRIT_NAMES.get(ct, f"TYPE_{ct}")
            a4 = bs32(buf, coff + 4)
            if ct in (25, 28, 29):
                extra = tagdesc(a4)
            elif ct in (26, 27):
                bits = [b for b in range(32) if a4 >> b & 1]
                extra = f"stage flag mask {a4:#x} (bit(s) {bits})"
                if ct == 27:
                    extra += " -- mission FAILS when set"
            elif ct == 30:
                extra = f"{tagdesc(a4)}; photo-flag word @8={be32(buf, coff + 8):#x}"
            elif ct == 32:
                extra = f"ref @4={a4} (room); runtime word @8={be32(buf, coff + 8):#x}"
            elif ct == 33:
                extra = (f"ref @4={a4} ref @8={bs32(buf, coff + 8)}; "
                         f"runtime word @c={be32(buf, coff + 12):#x}")
            elif ct == 34:
                extra = "(key-analyzer/datavac copy flag)"
            else:
                extra = ""
            print(f"   - {name:<20} {extra}")
            j += 1
        i = j + 1 if j < len(recs) else j
    if nblocks == 0:
        print("  (no objective records)")


NAMES = {"dam": 33, "facility": 34, "runway": 35, "surface1": 36, "bunker1": 9,
         "silo": 20, "frigate": 26, "surface2": 43, "bunker2": 27, "statue": 22,
         "archives": 24, "streets": 29, "depot": 30, "train": 25, "jungle": 37,
         "control": 23, "caverns": 39, "cradle": 41, "aztec": 28, "egypt": 32,
         "cuba": 54}

if __name__ == "__main__":
    arg = (sys.argv[1] if len(sys.argv) > 1 else "facility").lower()
    table = load_level_table()
    if arg in ("all", "*"):
        for lid in sorted(table):
            dump_level(lid, f"level_{lid:02d}")
    elif re.fullmatch(r"\d+", arg):
        dump_level(int(arg), f"level_{int(arg):02d}")
    else:
        key = arg.replace(" ", "").replace("_", "")
        if key not in NAMES:
            sys.exit(f"unknown level '{arg}' (try: {', '.join(NAMES)} or all)")
        dump_level(NAMES[key], key)
