#!/usr/bin/env python3
"""D295/M-148: reproduce fileGenerateCRC (src/game/crc.c) with both the pre-D284
(buggy, shipped in v0.2.x) and post-D284 (HEAD) PRNG semantics, verify against the
user's saved slot CRCs, and emit a patched eep whose slots carry POST-FIX CRCs so
a HEAD build accepts them (proves the difficultytext overflow is still live).

Save layout in ge007.eep (verified M-148):
  bytes 0..31   : smallSave header (block 0)
  slot i        : file offset 32 + i*96, i = 0..5 (sizeof(save_data)=96, padded)
  slot fields   : c1(s32) c2(s32) bitflags(1) flag_007(1) music(1) sfx(1)
                  options(2) cheats(3) pad(1) times[76]
CRC region      : &completion_bitflags .. save+1  => slot bytes [8..96), 88 bytes.
"""
import struct, sys

M64 = (1 << 64) - 1

def make_rng(buggy: bool):
    def dsll32(x, n):
        if buggy: return (x << n) & 0xFFFFFFFF
        return (x << (n + 32)) & M64
    def dsrl32(x, n):
        if buggy: return (x >> n) & 0xFFFFFFFF
        return x >> (n + 32)
    def step(seed: int) -> int:
        # Mirrors randomGetNextFrom line-by-line. NOTE the plain dsrl/dsll lines
        # (a2 >>= 31, a1 = a3 << 31, a2 >>= 20) are NOT the *32 helpers.
        a3 = seed & M64
        a2 = dsll32(a3, 0x1F) >> 0x1F          # dsll32 then plain dsrl 31
        a1 = dsrl32((a3 << 0x1F) & M64, 0)     # plain dsll 31 then dsrl32 0
        a3 = dsrl32(dsll32(a3, 0xC), 0)        # dsll32 then dsrl32 0
        a2 = (a2 | a1) ^ a3
        a3 = ((a2 >> 0x14) & 0xFFF) ^ a2       # plain dsrl 20, andi, xor
        return a3 & M64
    return step

def file_generate_crc(data: bytes, buggy: bool):
    """Mirror crc.c exactly: shift is NOT reset between the two passes."""
    step = make_rng(buggy)
    poly = 0x8F809F473108B3C1 & M64
    c1 = 0; c2 = 0; shift = 0
    for b in data:                                  # forward pass, +7/byte
        poly = (poly + ((b << (shift & 0xF)) & M64)) & M64
        shift += 7
        poly = step(poly)                           # randomGetNextFrom updates the seed IN PLACE
        c1 ^= poly & 0xFFFFFFFF
    for b in reversed(data):                        # backward pass, +3/byte, shift continues
        poly = (poly + ((b << (shift & 0xF)) & M64)) & M64
        shift += 3
        poly = step(poly)
        c2 ^= poly & 0xFFFFFFFF
    return c1 & 0xFFFFFFFF, c2 & 0xFFFFFFFF

def main():
    if len(sys.argv) < 2:
        sys.exit("usage: d295_crc.py <ge007.eep> [out.eep]")
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else None
    eep = bytearray(open(src, "rb").read())
    assert len(eep) == 2048, len(eep)

    print("slot | stored c1,c2            | oldPRNG c1,c2           | newPRNG c1,c2           | old? new?")
    ok_old = ok_new = True
    for i in range(5):
        off = 32 + i * 96
        sc1, sc2 = struct.unpack_from("<II", eep, off)
        region = bytes(eep[off + 8: off + 96])
        oc1, oc2 = file_generate_crc(region, buggy=True)
        nc1, nc2 = file_generate_crc(region, buggy=False)
        o = (oc1, oc2) == (sc1, sc2); n = (nc1, nc2) == (sc1, sc2)
        ok_old &= o; ok_new &= n
        print(f"  {i} | {sc1:08x},{sc2:08x} | {oc1:08x},{oc2:08x} | {nc1:08x},{nc2:08x} | {o=} {n=}")
    print(f"old PRNG matches all 5 stored CRCs: {ok_old}")
    print(f"new PRNG matches all 5 stored CRCs: {ok_new}")

    if dst:
        for i in range(5):
            off = 32 + i * 96
            region = bytes(eep[off + 8: off + 96])
            nc1, nc2 = file_generate_crc(region, buggy=False)
            struct.pack_into("<II", eep, off, nc1, nc2)
        open(dst, "wb").write(eep)
        print(f"patched (post-fix CRCs, slots 0-4) -> {dst}")

if __name__ == "__main__":
    main()
