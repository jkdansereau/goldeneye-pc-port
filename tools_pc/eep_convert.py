#!/usr/bin/env python3
"""eep_convert.py — convert GoldenEye 007 `.eep` saves between N64 emulators
(1964, Project64, ...) and this PC port (D492).

Why conversion is needed: the save layout is byte-identical on both sides
(2048 B; block 0 `smallSave`, five 96-byte `save_data` slots from block 4 —
see `src/game/file.h`), but this port stores a few multi-byte fields in
little-endian host order: per slot the `chksum1`/`chksum2` words (@0/@4) and
the u16 `options` (@12), plus block 0's checksum pair. Emulator saves store
them big-endian. The CRC (`fileGenerateCRC`, `src/game/crc.c`) hashes raw
bytes, so the checksum VALUE is endian-independent — but an unconverted save
fails validation and all five slots are wiped on first boot.

Only regions that validate in the source convention are converted; a
failing region is left as-is so the game wipes it on either side, and a file
where nothing validates (wrong direction, already converted) is refused.

Conversion per slot: byte-swap `options` (it lies INSIDE the slot CRC range
[8..96), so a plain word swap would still fail), then recompute
`chksum1/2` over [8..96) and store them in the target endianness. Block 0
has no multi-byte field inside its CRC range, so its checksum pair is simply
re-stored in the other byte order.

The CRC replica below is a faithful transcription of `fileGenerateCRC` +
`randomGetNextFrom` (`src/random.s`) and was verified against the binary in
the D492 investigation.

Usage:
  eep_convert.py n64-to-pc IN.eep OUT.eep   # emulator save -> port save
  eep_convert.py pc-to-n64 IN.eep OUT.eep   # port save -> emulator save
  eep_convert.py verify IN.eep [--as n64|pc]
      Check every checksum without writing. Without --as, tries both
      conventions and reports which one validates.

Exit codes: 0 ok / validated, 1 validation failed (convert: nothing in the
source validates, so nothing is written), 2 usage or bad input.
IN may equal OUT (the file is read fully before it is written), but make a
backup first anyway.
"""
import argparse
import struct
import sys

M64 = (1 << 64) - 1


def randomGetNextFrom(seed):
    """Port of src/random.s (verified against the binary, D492)."""
    a3 = seed
    a2 = (((a3 << 63) & M64)) >> 31       # dsll32 .1f then plain dsrl .1f
    a1 = ((a3 << 31) & M64) >> 32         # dsll .1f then dsrl32 0
    a3 = ((a3 << 44) & M64) >> 32         # dsll32 .c then dsrl32 0
    a2 = (a2 | a1) ^ a3
    a3 = ((a2 >> 20) & 0xFFF) ^ a2
    return a3, a3 & 0xFFFFFFFF


def fileGenerateCRC(data):
    """Port of src/game/crc.c fileGenerateCRC (verified against the binary)."""
    poly = 0x8F809F473108B3C1
    c1 = c2 = 0
    shift = 0
    for b in data:
        poly = (poly + (b << (shift & 0xF))) & M64
        poly, v = randomGetNextFrom(poly)
        c1 ^= v
        shift += 7
    for b in reversed(data):
        poly = (poly + (b << (shift & 0xF))) & M64
        poly, v = randomGetNextFrom(poly)
        c2 ^= v
        shift += 3
    return c1, c2


LEVELS = ["Dam", "Facility", "Runway", "Surface 1", "Bunker 1", "Silo",
          "Frigate", "Surface 2", "Bunker 2", "Statue", "Archives", "Streets",
          "Depot", "Train", "Jungle", "Control", "Caverns", "Cradle", "Aztec",
          "Egypt"]


def decode_times(t):
    """t: the slot's 76-byte times[] bitfield -> {(level, diff): time}."""
    out = {}
    for d in range(3):                      # 007 mode has no per-level field
        for l in range(20):
            off = (d * 20 + l) * 10
            i = off >> 3
            r = 7 - (off & 7)
            if r == 7:   v = ((t[i] & 0xFF) << 2) | ((t[i + 1] & 0xC0) >> 6)
            elif r == 5: v = ((t[i] & 0x3F) << 4) | ((t[i + 1] & 0xF0) >> 4)
            elif r == 3: v = ((t[i] & 0x0F) << 6) | ((t[i + 1] & 0xFC) >> 2)
            elif r == 1: v = ((t[i] & 0x03) << 8) | (t[i + 1] & 0xFF)
            else:        v = -1
            if v:
                out[(l, d)] = v
    return out


def read_eep(path):
    try:
        raw = open(path, "rb").read()
    except OSError as e:
        print(f"error: cannot read {path}: {e}", file=sys.stderr)
        sys.exit(2)
    if len(raw) != 2048:
        print(f"error: {path} is {len(raw)} bytes; a ge007.eep is 2048",
              file=sys.stderr)
        sys.exit(2)
    return bytearray(raw)


def verify(data, endian):
    """Check block 0 + all five slots as stored in `endian` ('>' or '<').
    Returns (all_ok, [per-region 'OK'/'FAIL' lines])."""
    lines = []
    ok = True
    # CRC ranges are slot-relative: block 0 covers unk[0..24) at [8..32),
    # each slot covers [8..96) of its 96 bytes.
    for name, off, cfrom, cto in (("block0", 0, 8, 32),
                                  *[(f"slot {i}", 32 + i * 96, 8, 96)
                                    for i in range(5)]):
        stored = struct.unpack(endian + "II", bytes(data[off:off + 8]))
        calc = fileGenerateCRC(bytes(data[off + cfrom:off + cto]))
        good = stored == calc
        ok &= good
        lines.append(f"  {name}: stored={stored[0]:08x},{stored[1]:08x} "
                     f"calc={calc[0]:08x},{calc[1]:08x} {'OK' if good else 'FAIL'}")
    return ok, lines


def summary(data):
    """One line per non-empty slot (decoded before any conversion)."""
    for i in range(5):
        off = 32 + i * 96
        s = bytes(data[off:off + 96])
        flags = s[8]
        done = decode_times(s[18:94])
        folder = (flags >> 5) & 7
        if done or folder:
            lvls = sorted({l for l, d in done})
            print(f"slot {i}: folder={folder} bond={(flags >> 1) & 3} "
                  f"completed {len(done)} (level,diff) entries; levels: "
                  f"{[LEVELS[l] for l in lvls]}")


def region_ok(data, off, cfrom, cto, endian):
    stored = struct.unpack(endian + "II", bytes(data[off:off + 8]))
    return stored == fileGenerateCRC(bytes(data[off + cfrom:off + cto]))


def convert(data, src_endian, dst_endian):
    """Byte-swap the multi-byte fields and re-stamp the checksum pairs of
    every region that validates in the SOURCE convention. A region that fails
    in the source is left untouched, so it also fails in the target and the
    game wipes it exactly as it would have on the original side (re-stamping
    it would turn garbage into a 'valid' slot). Returns the list of
    (name, converted) pairs."""
    done = []
    # Block 0: no multi-byte field inside its CRC range -> plain word swap.
    ok = region_ok(data, 0, 8, 32, src_endian)
    if ok:
        st = struct.unpack(src_endian + "II", bytes(data[0:8]))
        data[0:8] = struct.pack(dst_endian + "II", *st)
    done.append(("block0", ok))
    # Slots: options (u16 @12) is INSIDE the CRC range [8..96): swap it,
    # then recompute chksum1/2 over the new bytes and store in dst order.
    for i in range(5):
        off = 32 + i * 96
        ok = region_ok(data, off, 8, 96, src_endian)
        if ok:
            w = struct.unpack_from(src_endian + "H", data, off + 12)[0]
            struct.pack_into(dst_endian + "H", data, off + 12, w)
            c1, c2 = fileGenerateCRC(bytes(data[off + 8:off + 96]))
            struct.pack_into(dst_endian + "II", data, off, c1, c2)
        done.append((f"slot {i}", ok))
    return done


def main():
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    def add_convert(name, help_):
        sp = sub.add_parser(name, help=help_)
        sp.add_argument("INFILE", metavar="IN.eep")
        sp.add_argument("OUTFILE", metavar="OUT.eep")

    add_convert("n64-to-pc", "convert an emulator (1964/Project64) save to the port format")
    add_convert("pc-to-n64", "convert a port save back to the emulator format")

    sp = sub.add_parser("verify", help="check every checksum without writing")
    sp.add_argument("INFILE", metavar="IN.eep")
    sp.add_argument("--as", dest="endian", choices=("n64", "pc"),
                    help="which convention to check (default: try both)")

    a = p.parse_args()
    data = read_eep(a.INFILE)
    summary(data)

    if a.cmd == "verify":
        results = {}
        for label, endian in (("n64", ">"), ("pc", "<")):
            if a.endian and a.endian != label:
                continue
            ok, lines = verify(data, endian)
            results[label] = ok
            print(f"as {label} ({'big' if label == 'n64' else 'little'}-endian words):")
            for ln in lines:
                print(ln)
        if a.endian:
            sys.exit(0 if results[a.endian] else 1)
        n = sum(results.values())
        if n == 2:
            print("result: valid under BOTH conventions")
            sys.exit(0)
        if n == 1:
            good = next(k for k, v in results.items() if v)
            bad = next(k for k, v in results.items() if not v)
            cmd = "n64-to-pc" if good == "n64" else "pc-to-n64"
            dest = "the port" if good == "n64" else "an emulator"
            print(f"result: valid as {good}; NOT valid as {bad} — to use it "
                  f"with {dest}: eep_convert.py {cmd} IN.eep OUT.eep")
            sys.exit(0)
        print("result: INVALID under both conventions (corrupt or foreign file)")
        sys.exit(1)

    # convert commands
    src, dst = (">", "<") if a.cmd == "n64-to-pc" else ("<", ">")
    src_label = "n64" if src == ">" else "pc"
    done = convert(data, src, dst)
    if not any(ok for _, ok in done):
        # Nothing validates as the claimed source: wrong direction, an
        # already-converted file, or not a GoldenEye save. Write nothing.
        article = "an" if src_label == "n64" else "a"   # no nested quotes: Python < 3.12
        print(f"error: {a.INFILE} does not validate as {article} {src_label} save; "
              f"nothing written. Run 'eep_convert.py verify {a.INFILE}' to "
              f"see which format it is.", file=sys.stderr)
        sys.exit(1)
    for name, ok in done:
        if not ok:
            print(f"  {name}: invalid in the source, left as-is "
                  f"(the game will wipe it, as it would have before)")
    with open(a.OUTFILE, "wb") as f:
        f.write(data)
    n = sum(ok_ for _, ok_ in done)
    print(f"wrote {a.OUTFILE} ({n}/6 regions converted)")
    sys.exit(0)


if __name__ == "__main__":
    main()
