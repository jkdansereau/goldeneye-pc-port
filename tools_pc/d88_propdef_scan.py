#!/usr/bin/env python3
"""D88.4 analysis helper -- walk the `propDefs` region of every Usetup*Z file
and print the PROPDEF_* record-type sequence + histogram.

Read-only. Does NOT modify the sidecar. Purpose: ground the D88.4 spec work
(which propDef record types actually appear in shipped levels, in what
order, so only those need a byte-accurate field spec + converter bswap).

Uses the same ROM/RZ path as d88_emit.py. Stride per record = the N64 word
count from sizepropdef() (loadobjectmodel.c:47, the `#if 1` branch) * 4.
Types whose size is `sizeof(Record)/4` are marked SIZEOF and assumed here
from the N64 struct layout notes in bondtypes.h -- VERIFY before trusting
the walk past the first such record.
"""
import csv, struct, zlib, os, re, sys

REGION = sys.argv[1] if len(sys.argv) > 1 else "ntsc-final"
ROM_PATH = f"data/ge007.{REGION}.z64"
FILELIST = "scripts/filelist.u.csv"

if not os.path.exists(ROM_PATH):
    print(f"SKIP: {ROM_PATH} not present", file=sys.stderr)
    sys.exit(0)

rom = open(ROM_PATH, "rb").read()
fl_by_base = {}
for r in csv.reader(open(FILELIST)):
    if len(r) < 3 or not r[2]:
        continue
    base = r[2].rsplit("/", 1)[-1]
    if base.endswith(".bin"):
        base = base[:-4]
    fl_by_base[base] = (int(r[0]), int(r[1]))

def be32(b, o): return struct.unpack_from(">I", b, o)[0]

# PROPDEF_TYPE enum (bondconstants.h:4307)
NAMES = ["NOTHING","DOOR","DOOR_SCALE","PROP","KEY","ALARM","CCTV","MAGAZINE",
    "COLLECTABLE","GUARD","MONITOR","MULTI_MONITOR","RACK","AUTOGUN","LINK",
    "DEBRIS","UNK16","HAT","GUARD_ATTRIBUTE","SWITCH","AMMO","ARMOUR","TAG",
    "OBJECTIVE_START","OBJECTIVE_END","OBJ_DESTROY","OBJ_COMPLETE_COND",
    "OBJ_FAIL_COND","OBJ_COLLECT","OBJ_DEPOSIT","OBJ_PHOTOGRAPH","OBJ_NULL",
    "OBJ_ENTER_ROOM","OBJ_DEPOSIT_IN_ROOM","OBJ_COPY_ITEM","WATCH_MENU_OBJ_TEXT",
    "GAS_RELEASING","RENAME","LOCK_DOOR","VEHICHLE","AIRCRAFT","UNK41","GLASS",
    "SAFE","SAFE_ITEM","TANK","CAMERAPOS","TINTED_GLASS","END","MAX"]
IDX = {n: i for i, n in enumerate(NAMES)}

# word counts from sizepropdef() loadobjectmodel.c:47 (#if 1 branch).
# literal-count entries are exact; SIZEOF entries need N64-layout confirmation.
WC = {
    "DOOR_SCALE": ("SIZEOF", None), "PROP": ("SIZEOF", None),
    "GLASS": ("SIZEOF", None), "TINTED_GLASS": ("SIZEOF", None),
    "SAFE": ("SIZEOF", None), "GAS_RELEASING": ("SIZEOF", None),
    "KEY": ("SIZEOF", None), "ALARM": ("SIZEOF", None), "RACK": ("SIZEOF", None),
    "HAT": ("SIZEOF", None), "GUARD": ("SIZEOF", None), "DOOR": ("SIZEOF", None),
    "CCTV": ("LIT", 0x3b), "MAGAZINE": ("LIT", 0x21), "COLLECTABLE": ("LIT", 0x22),
    "MONITOR": ("LIT", 0x40), "MULTI_MONITOR": ("LIT", 0x95), "AUTOGUN": ("LIT", 0x36),
    "LINK": ("LIT", 3), "GUARD_ATTRIBUTE": ("LIT", 3), "SWITCH": ("LIT", 4),
    "SAFE_ITEM": ("LIT", 5), "AMMO": ("LIT", 0x2d), "ARMOUR": ("LIT", 0x22),
    "TAG": ("LIT", 4), "RENAME": ("LIT", 10), "OBJECTIVE_START": ("LIT", 4),
    "OBJECTIVE_END": ("LIT", 1), "OBJ_DESTROY": ("LIT", 2), "OBJ_COMPLETE_COND": ("LIT", 2),
    "OBJ_FAIL_COND": ("LIT", 2), "OBJ_COLLECT": ("LIT", 2), "OBJ_DEPOSIT": ("LIT", 2),
    "OBJ_PHOTOGRAPH": ("LIT", 4), "OBJ_NULL": ("LIT", 1), "OBJ_ENTER_ROOM": ("LIT", 4),
    "OBJ_DEPOSIT_IN_ROOM": ("LIT", 5), "OBJ_COPY_ITEM": ("LIT", 1),
    "WATCH_MENU_OBJ_TEXT": ("LIT", 4), "LOCK_DOOR": ("LIT", 4), "VEHICHLE": ("LIT", 0x2c),
    "AIRCRAFT": ("LIT", 0x2d), "TANK": ("LIT", 0x38), "CAMERAPOS": ("LIT", 7),
}
# N64 sizeof guesses (words) for the SIZEOF arms -- from bondtypes.h layout
# comments; CONFIRM against a raw hex dump / N64 build before relying on these.
SIZEOF_N64 = {
    "DOOR": 0x1a, "GUARD": 0x0e, "PROP": 0x0a, "GLASS": 0x0a, "KEY": 0x08,
    "ALARM": 0x0a, "RACK": 0x0a, "HAT": 0x0a, "SAFE": 0x0a, "GAS_RELEASING": 0x0a,
    "TINTED_GLASS": 0x0c, "DOOR_SCALE": 0x02,
}

from collections import Counter

for name, (addr, size) in sorted(fl_by_base.items()):
    if not (name.startswith("Usetup") and name.endswith("Z")) or size == 0:
        continue
    comp = rom[addr:addr + size]
    if comp[:2] != b"\x11\x72":
        continue
    src = zlib.decompress(comp[2:], -15)
    pd = be32(src, 3 * 4)  # header field 3 = propDefs
    if pd == 0:
        print(f"{name}: no propDefs"); continue
    o = pd
    hist = Counter()
    unknown = False
    seq = []
    while o + 4 <= len(src):
        t = src[o + 3]
        tn = NAMES[t] if t < len(NAMES) else f"?{t}"
        if tn == "END":
            break
        hist[tn] += 1
        seq.append(tn)
        kind, val = WC.get(tn, (None, None))
        if kind == "LIT":
            words = val
        elif kind == "SIZEOF":
            words = SIZEOF_N64.get(tn)
            if words is None:
                print(f"{name}: STOP at {o:#x} -- {tn} size unknown"); unknown = True; break
        else:
            print(f"{name}: STOP at {o:#x} -- unmapped type {tn}"); unknown = True; break
        o += words * 4
    tag = " (INCOMPLETE)" if unknown else ""
    print(f"{name}{tag}: {sum(hist.values())} records  " +
          "  ".join(f"{k}={v}" for k, v in hist.most_common()))
