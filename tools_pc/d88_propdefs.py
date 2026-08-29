#!/usr/bin/env python3
"""D88.4: convert the `propDefs` polymorphic record stream of a Usetup*Z stage
file from N64 layout to native PC layout.

Ground truth (all cross-checked against the retail ROM + the getools-generated
`assets/obseg/setup/Usetup*Z.c` sources, which tile every propDefs region
byte-for-byte):

  * The stream is a flat `s32[]`.  Each record starts with a
    PropDefHeaderRecord word: [u16 extrascale][u8 state][u8 type].  The `type`
    byte selects the record layout and its FIXED serialized word count
    (identical across all 21 levels -- see PROPDEF_N64_WORDS below).
  * Every pointer member inside a serialized record is 0 in the file
    (runtime-populated).  On PC those slots widen 4->8 bytes, so records that
    contain pointers grow and the walk stride changes.  This module emits each
    record at its native PC struct size (PROPDEF_PC_BYTES), zero-filling the
    runtime area, byte-swapping the meaningful scalar fields, and swapping the
    two halves of any `_mkword`-packed field independently.
  * The matching runtime change is in loadobjectmodel.c `sizepropdef()` under
    `#ifdef PORT`: it must return PROPDEF_PC_BYTES[type] / 4.

Only `OBJ_COPY_ITEM` (type 34) had a wrong N64 literal in `sizepropdef()`'s
`#if 1` branch (returned 1, real size 3); every other arm already matched.
"""
import struct

# ---------------------------------------------------------------- constants ---
# serialized N64 word count per PROPDEF type byte (getools / ROM verified).
PROPDEF_N64_WORDS = {
    1: 64,   # DOOR
    2: 2,    # DOOR_SCALE
    3: 32,   # PROP  (ObjectRecord)
    4: 33,   # KEY
    5: 32,   # ALARM (ObjectRecord)
    6: 59,   # CCTV
    7: 33,   # MAGAZINE / AmmoMag
    8: 34,   # COLLECTABLE
    9: 7,    # GUARD
    10: 64,  # MONITOR (SingleMonitor)
    11: 149, # MULTI_MONITOR
    12: 32,  # RACK / HangingMonitor (ObjectRecord)
    13: 54,  # AUTOGUN / Drone
    14: 3,   # LINK
    17: 32,  # HAT (ObjectRecord)
    18: 3,   # GUARD_ATTRIBUTE / SetGuardAttribute
    19: 4,   # SWITCH / LinkProps
    20: 45,  # AMMO / AmmoBox
    21: 34,  # ARMOUR
    22: 4,   # TAG
    23: 4,   # OBJECTIVE_START
    24: 1,   # OBJECTIVE_END
    25: 2,   # OBJ_DESTROY
    26: 2,   # OBJ_COMPLETE_COND
    27: 2,   # OBJ_FAIL_COND
    28: 2,   # OBJ_COLLECT
    29: 2,   # OBJ_DEPOSIT
    30: 4,   # OBJ_PHOTOGRAPH
    31: 1,   # OBJ_NULL
    32: 4,   # OBJ_ENTER_ROOM
    33: 5,   # OBJ_DEPOSIT_IN_ROOM
    34: 3,   # OBJ_COPY_ITEM  (N64 sizepropdef #if 1 wrongly says 1)
    35: 4,   # WATCH_MENU_OBJ_TEXT
    36: 32,  # GAS_RELEASING (ObjectRecord)
    37: 10,  # RENAME
    38: 4,   # LOCK_DOOR
    39: 44,  # VEHICHLE
    40: 45,  # AIRCRAFT
    42: 32,  # GLASS (ObjectRecord)
    43: 32,  # SAFE (ObjectRecord)
    44: 5,   # SAFE_ITEM
    45: 56,  # TANK
    46: 7,   # CAMERAPOS / Cutscene
    47: 37,  # TINTED_GLASS
    48: 1,   # END
}

# ObjectRecord: the shared 32-word N64 prefix of every "object" record.
# N64 word index -> kind.  words not listed are plain bswap32 (mostly zero).
#   0  header  [u16 extrascale][u8 state][u8 type]
#   1  [s16 obj][s16 pad]
#   4  prop*   5 model*   26 collisiondata*   27 projectile*
OBJ_PTR_WORDS = (4, 5, 26, 27)
OBJ_N64_WORDS = 32
OBJ_PC_BYTES = 144

# D122: ObjectRecord-derived types whose tail (N64 words >= 32) the converter
# must lay out with pointer widening.  {type: (ptr_word_set, hh_word_set)}.
#   47 TintedGlassRecord  tail: TintDist,CullDist,calculatedopacity,portalnum,unk90
#   39 VehichleRecord     ailist*@w32, [u16 aioffset|s16 aireturnlist]@w33, path*@w41, Sound*@w43
#   40 AircraftRecord     ailist*@w32, [u16|s16]@w33, path*@w43, Sound*@w44
#   45 TankRecord         collision*@w32 (rest scalar; struct locs unconfirmed)
OBJ_TAIL_DESC = {
    47: (frozenset(),             frozenset()),
    39: (frozenset((32, 41, 43)), frozenset((33,))),
    40: (frozenset((32, 43, 44)), frozenset((33,))),
    45: (frozenset((32,)),        frozenset()),
    # 13 AutogunRecord: unkC4*@w49 unkC8*@w50 beam*@w51 (is_active@w52 unkD4@w53)
    13: (frozenset((49, 50, 51)), frozenset()),
    # 20 MultiAmmoCrateRecord: slots[13] of [u16 modelnum][u16 quantity]
    20: (frozenset(),             frozenset(range(32, 45))),
}

# Per-type PC struct size (bytes) the converter emits and sizepropdef must
# return / 4.  For ObjectRecord-derived types this is 144 + tail growth.
PROPDEF_PC_BYTES = {
    1: 296,   # DoorRecord
    2: 8,     # GlobalDoorScaleRecord
    3: 144, 5: 144, 12: 144, 17: 144, 36: 144, 42: 144, 43: 144,  # ObjectRecord
    4: 152,   # KeyRecord
    6: 272,   # CCTVRecord
    7: 152,   # AmmoCrateRecord
    8: 160,   # WeaponObjRecord
    9: 32,    # GuardRecord
    10: 288,  # MonitorObjRecord
    11: 664,  # MultiMonitorObjRecord
    13: 248,  # AutogunRecord
    14: 24,   # LinkRecord (3w + next* grown, 8-aligned)
    18: 12,   # GuardAttributeRecord
    19: 24,   # SwitchRecord = LinkRecord
    20: 200,  # MultiAmmoCrateRecord
    21: 152,  # BodyArmourRecord
    22: 24,   # TagObjectRecord
    23: 16,   # OBJECTIVE_START  (4w, no serialized pointer)
    24: 4,    # OBJECTIVE_END
    25: 8, 26: 8, 27: 8, 28: 8, 29: 8,  # OBJ_* (2w)
    30: 16,   # OBJ_PHOTOGRAPH (4w serialized, lastprop* not serialized)
    31: 4,    # OBJ_NULL
    32: 16,   # OBJ_ENTER_ROOM
    33: 20,   # OBJ_DEPOSIT_IN_ROOM
    34: 12,   # OBJ_COPY_ITEM (3w)
    35: 16,   # WATCH_MENU_OBJ_TEXT (4w)
    37: 48,   # RenameObjectRecord
    38: 32,   # LockDoorRecord
    39: 208,  # VehichleRecord (D122: 144 prefix + widened tail)
    40: 208,  # AircraftRecord (D122)
    44: 24,   # SafeObjectRecord fragment (5w + next*), refine on use
    45: 248,  # TankRecord (D122: 144 prefix + 23-word scalar tail + collision*)
    46: 28,   # CutsceneRecord
    47: 168,  # TintedGlassRecord
    48: 4,    # END
}

# DoorRecord: N64 words (after the 32-word ObjectRecord prefix) that are
# pointers.  linkedDoor@0xc8 unkcc@0xcc openSoundState@0xf4 closeSoundState@0xf8
DOOR_TAIL_PTR_WORDS = (50, 51, 61, 62)


def _bswap32(b):
    return b[3:4] + b[2:3] + b[1:2] + b[0:1]


def _hdr_word(b):
    """[u16 extrascale][u8 state][u8 type] -> swap the u16, keep the 2 bytes."""
    return b[1:2] + b[0:1] + b[2:3] + b[3:4]


def _hh_word(b):
    """[hi16][lo16] -> byte-swap each half independently."""
    return b[1:2] + b[0:1] + b[3:4] + b[2:3]


class PropDefError(Exception):
    pass


def _emit_object_prefix(out, src, so):
    """Convert the 32-word N64 ObjectRecord prefix at src[so:] into the first
    144 PC bytes of `out` (a bytearray already zero-sized to >=144)."""
    # cursor walk so pointer slots get 8-byte alignment for free
    pc = 0
    for i in range(OBJ_N64_WORDS):
        w = src[so + 4 * i: so + 4 * i + 4]
        if i in OBJ_PTR_WORDS:
            pc = (pc + 7) & ~7
            # 8 zero bytes already present
            pc += 8
        elif i == 0:
            out[pc:pc + 4] = _hdr_word(w); pc += 4
        elif i == 1:
            out[pc:pc + 4] = _hh_word(w); pc += 4   # [s16 obj][s16 pad]
        elif i in (30, 31):
            out[pc:pc + 4] = w; pc += 4              # shadecol/nextcol rgba: verbatim
        else:
            out[pc:pc + 4] = _bswap32(w); pc += 4
    assert pc <= OBJ_PC_BYTES, pc
    return OBJ_PC_BYTES


def convert_record(src, so, type_byte):
    """Return the native-PC bytes for one propDef record whose N64 image starts
    at src[so:].  Length == PROPDEF_PC_BYTES[type_byte]."""
    n64w = PROPDEF_N64_WORDS[type_byte]
    pcb = PROPDEF_PC_BYTES[type_byte]
    out = bytearray(pcb)

    OBJ_TYPES = {3, 5, 12, 17, 36, 42, 43}
    if type_byte in OBJ_TYPES:
        _emit_object_prefix(out, src, so)
        return bytes(out)

    if type_byte == 1:  # DOOR
        _emit_object_prefix(out, src, so)
        # tail: N64 words 32..63 -> PC bytes 144..
        pc = OBJ_PC_BYTES
        for i in range(OBJ_N64_WORDS, n64w):
            w = src[so + 4 * i: so + 4 * i + 4]
            if i in DOOR_TAIL_PTR_WORDS:
                pc = (pc + 7) & ~7
                pc += 8
            elif i == 38:            # [u16 doorFlags][u16 doorType]
                out[pc:pc + 4] = _hh_word(w); pc += 4
            elif i == 47:            # [s8 openstate][s8 unkbd][s16 opacity]
                out[pc:pc + 4] = w[0:1] + w[1:2] + w[3:4] + w[2:3]; pc += 4
            elif i == 49:            # [s16 CullDist][s8 soundType][s8 fadeTime]
                out[pc:pc + 4] = w[1:2] + w[0:1] + w[2:3] + w[3:4]; pc += 4
            else:
                out[pc:pc + 4] = _bswap32(w); pc += 4
        assert pc <= pcb, (pc, pcb)
        return bytes(out)

    if type_byte in (4, 21, 7):  # KEY / ARMOUR / MAGAZINE: ObjectRecord + plain tail
        _emit_object_prefix(out, src, so)
        pc = OBJ_PC_BYTES
        for i in range(OBJ_N64_WORDS, n64w):
            w = src[so + 4 * i: so + 4 * i + 4]
            out[pc:pc + 4] = _bswap32(w); pc += 4
        return bytes(out)

    if type_byte == 8:  # COLLECTABLE / WeaponObjRecord
        _emit_object_prefix(out, src, so)
        # N64 w32 = [s8 weaponnum][s8 linked][s16 timer]; w33 = dualweapon*
        w = src[so + 4 * 32: so + 4 * 32 + 4]
        out[144:148] = w[0:1] + w[1:2] + w[3:4] + w[2:3]
        # dualweapon* -> zero @152 (already)
        return bytes(out)

    if type_byte in (10, 11):  # SingleMonitor / MultiMonitor
        _emit_object_prefix(out, src, so)
        nmon = 1 if type_byte == 10 else 4
        # MonitorRecord blocks are 29 N64 words each, entirely runtime -> zero.
        tail_start = OBJ_N64_WORDS + 29 * nmon
        pc = OBJ_PC_BYTES + 128 * nmon
        for i in range(tail_start, n64w):
            w = src[so + 4 * i: so + 4 * i + 4]
            if type_byte == 11:
                out[pc:pc + 4] = w            # ImageNums u8[4]: verbatim
            else:
                out[pc:pc + 4] = _bswap32(w)  # OwnerOffset/OwnerPart/ImageNum
            pc += 4
        return bytes(out)

    if type_byte == 6:  # CCTV
        _emit_object_prefix(out, src, so)
        # N64 w32 lookpad; w33..48 Mtxf (runtime zero); w49..58 config
        out[144:148] = _bswap32(src[so + 4 * 32: so + 4 * 32 + 4])
        pc = 212
        for i in range(49, n64w):
            out[pc:pc + 4] = _bswap32(src[so + 4 * i: so + 4 * i + 4]); pc += 4
        return bytes(out)

    if type_byte == 22:  # TAG: [hdr][u16 ID][s16 OffsetToObj] , NextTag*, TaggedObject*
        out[0:4] = _hdr_word(src[so:so + 4])
        out[4:8] = _hh_word(src[so + 4:so + 8])
        # words 2,3 = pointers -> zero @8, @16
        return bytes(out)

    if type_byte == 37:  # RENAME: hdr + 8 s32 + renobj*
        out[0:4] = _hdr_word(src[so:so + 4])
        for i in range(1, 9):
            out[4 * i:4 * i + 4] = _bswap32(src[so + 4 * i: so + 4 * i + 4])
        return bytes(out)  # renobj* @40 zero

    if type_byte in (14, 19, 38, 44):  # LINK / SWITCH / LOCK_DOOR / SAFE_ITEM
        out[0:4] = _hdr_word(src[so:so + 4])
        # remaining serialized words: Index1/Index2 are s32 in file (zero or id);
        # trailing next* is not serialized.
        for i in range(1, n64w):
            out[4 * i:4 * i + 4] = _bswap32(src[so + 4 * i: so + 4 * i + 4])
        return bytes(out)

    if type_byte == 18:  # SetGuardAttribute: hdr + s32 chrnum + [s16 unk8][s8 unkA][s8 GrenadeProb]
        out[0:4] = _hdr_word(src[so:so + 4])
        out[4:8] = _bswap32(src[so + 4:so + 8])
        w = src[so + 8:so + 12]
        out[8:12] = w[1:2] + w[0:1] + w[2:3] + w[3:4]
        return bytes(out)

    if type_byte == 9:  # GUARD: hdr + 5 u16-pair words + Data*
        out[0:4] = _hdr_word(src[so:so + 4])
        for i in range(1, 6):
            out[4 * i:4 * i + 4] = _hh_word(src[so + 4 * i: so + 4 * i + 4])
        # word 6 = Data* -> zero @24 (already; 20-byte u16 block then 8-align pad@20)
        return bytes(out)

    if type_byte in OBJ_TAIL_DESC:  # D122: ObjectRecord prefix + typed tail
        # 47 TINTED_GLASS / 39 VEHICHLE / 40 AIRCRAFT / 45 TANK.  These all
        # `inherits ObjectRecord` but had no handler -> fell through to the
        # generic arm, which bswap32'd N64 word 1 ([s16 obj][s16 pad]) as one
        # 32-bit value.  That put `obj` in the wrong half -> garbage modelid ->
        # OOB PitemZ_entries[] deref crash (loadobjectmodel.c:393 /
        # model.c:6249).  Emit the real 144B PC ObjectRecord prefix, then the
        # tail with pointer members widened 4->8B and 8-aligned.
        _emit_object_prefix(out, src, so)
        ptr_words, hh_words = OBJ_TAIL_DESC[type_byte]
        pc = OBJ_PC_BYTES
        for i in range(OBJ_N64_WORDS, n64w):
            w = src[so + 4 * i: so + 4 * i + 4]
            if i in ptr_words:
                pc = (pc + 7) & ~7
                pc += 8                      # 8 zero bytes already present
            elif i in hh_words:
                out[pc:pc + 4] = _hh_word(w); pc += 4
            else:
                out[pc:pc + 4] = _bswap32(w); pc += 4
        assert pc <= pcb, (type_byte, pc, pcb)
        return bytes(out)

    # ---- generic: header-only records (objectives, END, DOOR_SCALE, ...) ----
    # No serialized pointers.  header word special, rest plain bswap32.
    out[0:4] = _hdr_word(src[so:so + 4])
    for i in range(1, n64w):
        seg = src[so + 4 * i: so + 4 * i + 4]
        out[4 * i:4 * i + 4] = _bswap32(seg)
    return bytes(out)


def convert_stream(src, start, end):
    """Convert the whole propDefs region src[start:end] (N64) -> PC bytes.
    Returns (pc_bytes, n64_len, pc_len). Raises PropDefError on any walk
    inconsistency (unknown type / overrun / region size mismatch)."""
    out = bytearray()
    o = start
    n = 0
    while o < end:
        if o + 4 > end:
            raise PropDefError(f"propDefs: truncated header at {o:#x}")
        t = src[o + 3]
        if t not in PROPDEF_N64_WORDS:
            raise PropDefError(f"propDefs: unknown record type {t} at {o:#x} (rec #{n})")
        n64w = PROPDEF_N64_WORDS[t]
        if o + 4 * n64w > end:
            raise PropDefError(f"propDefs: record type {t} at {o:#x} overruns region end")
        out += convert_record(src, o, t)
        o += 4 * n64w
        n += 1
        if t == 48:  # END
            break
    if o != end:
        # trailing alignment padding (a few bytes) is tolerated
        pad = end - o
        if pad < 0 or pad >= 16 or any(src[o:end]):
            raise PropDefError(f"propDefs: walk ended at {o:#x}, region end {end:#x} "
                               f"(pad={pad})")
        out += src[o:end]
    return bytes(out), end - start, len(out)


if __name__ == "__main__":
    import csv, zlib, sys, glob, re
    rom = open("data/ge007.ntsc-final.z64", "rb").read()
    fl = {}
    for r in csv.reader(open("scripts/filelist.u.csv")):
        if len(r) >= 3 and r[2]:
            b = r[2].rsplit("/", 1)[-1]
            b = b[:-4] if b.endswith(".bin") else b
            fl[b] = (int(r[0]), int(r[1]))
    names = sorted(n for n in fl if n.startswith("Usetup") and n.endswith("Z"))
    fails = 0
    for nm in names:
        a, s = fl[nm]
        if s == 0:
            continue
        comp = rom[a:a + s]
        if comp[:2] != b"\x11\x72":
            continue
        dec = zlib.decompress(comp[2:], -15)
        be = lambda o: struct.unpack_from(">I", dec, o)[0]
        pd, intro = be(12), be(8)
        try:
            out, n64len, pclen = convert_stream(dec, pd, intro)
            grow = pclen - n64len
            print(f"OK   {nm:20} n64={n64len:6d} pc={pclen:6d}  +{grow}")
        except PropDefError as e:
            print(f"FAIL {nm:20} {e}")
            fails += 1
    # byte spot-checks on BUNKER1
    a, s = fl["UsetupsevbunkerZ"]
    dec = zlib.decompress(rom[a:a + s][2:], -15)
    be = lambda o: struct.unpack_from(">I", dec, o)[0]
    pd, intro = be(12), be(8)
    out, _, _ = convert_stream(dec, pd, intro)
    # walk PC output, find the first DOOR (type 1), check obj==138 pad==8
    o = 0
    doors = []
    while o < len(out):
        t = out[o + 3] if o + 4 <= len(out) else None
        if t not in PROPDEF_PC_BYTES:
            break
        if t == 1:
            obj = struct.unpack_from("<h", out, o + 4)[0]
            pad = struct.unpack_from("<h", out, o + 6)[0]
            flags = struct.unpack_from("<I", out, o + 8)[0]
            doors.append((obj, pad, hex(flags)))
        o += PROPDEF_PC_BYTES[t]
        if t == 48:
            break
    print("PC doors (obj,pad,flags):", doors[:4])
    assert doors and doors[0][:2] == (138, 8), doors[:1]
    print("spot-check OK" if not fails else f"{fails} FAILURES")
    sys.exit(1 if fails else 0)
