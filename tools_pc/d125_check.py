#!/usr/bin/env python3
"""D125 diagnostic: does the propDefs blob in the *emitted* pccg sidecar
(after RZ round-trip) byte-match tools_pc/d88_propdefs.convert_stream()?

Tests the M-14 claim that the RAM propDefs is "zeros/garbage after record 0".
If this script says MATCH, the corruption is downstream of the sidecar
(runtime rebase / bank alloc / sizepropdef walk); if MISMATCH, it is in
d88_emit.py's emit/placement or the RZ recompress.
"""
import csv, struct, zlib, sys
from d88_propdefs import convert_stream, PROPDEF_PC_BYTES

REGION = "ntsc-final"
rom = open(f"data/ge007.{REGION}.z64", "rb").read()
fl = {}
for r in csv.reader(open("scripts/filelist.u.csv")):
    if len(r) >= 3 and r[2]:
        b = r[2].rsplit("/", 1)[-1]
        b = b[:-4] if b.endswith(".bin") else b
        fl[b] = (int(r[0]), int(r[1]))

man = {}
for r in csv.reader(open(f"data/pccg-{REGION}/manifest.csv")):
    if r and r[0].startswith("Usetup"):
        man[r[0]] = (int(r[1]), int(r[2]))
sidebin = open(f"data/pccg-{REGION}/pccg.bin", "rb").read()

be = lambda b, o: struct.unpack_from(">I", b, o)[0]
targets = sys.argv[1:] or sorted(man)
for nm in targets:
    a, s = fl[nm]
    dec_n64 = zlib.decompress(rom[a:a + s][2:], -15)
    pd_n64, intro_n64 = be(dec_n64, 12), be(dec_n64, 8)
    want, n64len, pclen = convert_stream(dec_n64, pd_n64, intro_n64)

    off, size = man[nm]
    dec_pc = zlib.decompress(sidebin[off:off + size][2:], -15)
    # PC header: 8-byte slots, low 4 = LE offset
    pd_pc = struct.unpack_from("<I", dec_pc, 8 * 3)[0]      # propDefs = field 3
    intro_pc = struct.unpack_from("<I", dec_pc, 8 * 2)[0]   # intro = field 2
    got = dec_pc[pd_pc:pd_pc + len(want)]

    if got == want:
        print(f"MATCH    {nm:18} pd@{pd_pc:#x} len={len(want)}")
    else:
        # first differing byte + which record it lands in
        d = next((i for i in range(min(len(got), len(want))) if got[i] != want[i]), None)
        rec, o = 0, 0
        while o < (d or 0):
            t = want[o + 3]
            o += PROPDEF_PC_BYTES.get(t, 4)
            rec += 1
        print(f"MISMATCH {nm:18} pd@{pd_pc:#x} want={len(want)} got={len(got)} "
              f"first diff @+{d} (~record {rec}, off {o:#x}); "
              f"gotlen_vs_intro: {intro_pc - pd_pc} vs pclen {pclen}")
