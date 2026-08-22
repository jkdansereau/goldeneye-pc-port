#!/usr/bin/env python3
"""Cast-chain strict overlap/fit bound (D46 follow-up).

Formula (derived from N64 ground truth: the N64 game works, so per file
8*M_a <= R_share_N64 - B_n64 - E; PC slots are 2x wide with the same M_a):

    sum(P_final_actual) <= sum(B_pc) + 2*(R_chain_N64 - sum(B_n64))

where R_chain_N64 = 0x18160 (N64 cast-screen bufferRemaining).
Compares against the PC bufferRemaining = 0x1C000 currently in front.c.
"""
import csv, struct, zlib, os, re
from collections import Counter

ROM = r"data/ge007.ntsc-final.z64"
rom = open(ROM, "rb").read()
rows = list(csv.reader(open(r"scripts/filelist.u.csv")))

RSZ_N64 = {1: 0x10, 2: 0x1C, 4: 0x14, 8: 0x10, 9: 0x24, 10: 0x1C,
           12: 0x28, 13: 0x20, 15: 0x1C, 17: 0x20, 18: 0x8, 21: 0x14,
           22: 0x10, 23: 2, 24: 0x20}
RSZ_PC = {1: 0x18, 2: 0x28, 4: 0x28, 8: 0x18, 9: 0x30, 10: 0x1C,
          12: 0x30, 13: 0x30, 15: 0x1C, 17: 0x28, 18: 0x10, 21: 0x14,
          22: 0x20, 23: 2, 24: 0x40}

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

def be32(b, o):
    return struct.unpack_from(">I", b, o)[0] & 0xFFFFFF

def analyze(base_want):
    for r in rows:
        if len(r) < 3 or not r[2]:
            continue
        base = r[2].rsplit("/", 1)[-1]
        if base.endswith(".bin"):
            base = base[:-4]
        key = None
        for cand in (base, base[1:], base[:-1], base[1:-1]):
            if cand in nsnt:
                key = cand
                break
        if key != base_want:
            continue
        addr, size = int(r[0]), int(r[1])
        out = zlib.decompress(rom[addr:addr + size][2:], -15)
        D = len(out)
        NS, NT = nsnt[key]
        R0n = 4 * NS + 12 * NT
        stack = [R0n]
        seen = set()
        nodes = []
        gdls = []
        while stack:
            o = stack.pop()
            if o in seen or o >= D - 23:
                break
            seen.add(o)
            op = struct.unpack_from(">H", out, o)[0] & 0xff
            data = be32(out, o + 4)
            nodes.append((o, op, data))
            if op in (4, 24):
                p, s = be32(out, data), be32(out, data + 4)
                if p: gdls.append(p)
                if s: gdls.append(s)
            elif op == 22:
                p = be32(out, data + 8)
                if p: gdls.append(p)
            for q in (be32(out, o + 0x14), be32(out, o + 0xC)):
                if q:
                    stack.append(q)
        gsorted = sorted(set(gdls))
        g1 = gsorted[0] if gsorted else D
        bulk = (g1 - R0n) - len(nodes) * 24 - sum(RSZ_N64[op] for _, op, _ in nodes)
        B_n64 = 4 * NS + 12 * NT + len(nodes) * 24 + sum(RSZ_N64[op] for _, op, _ in nodes) + bulk
        B_pc = 8 * NS + 12 * NT + len(nodes) * 48 + sum(RSZ_PC[op] for _, op, _ in nodes) + bulk
        E = D - g1
        return dict(base=base_want, D=D, B_n64=B_n64, B_pc=B_pc, E=E)

# cast-screen weapon sets (front.c ~867-891)
rifles = ["kalash", "m16", "fnp90", "autoshot", "grenadelaunch", "sniperrifle"]
pistols = ["wppk", "wppksil", "skorpion", "uzi", "tt33", "ruger", "laser", "golden"]
heads = []
# find all head files (Chead*Z)
for r in rows:
    if len(r) < 3 or not r[2]:
        continue
    base = r[2].rsplit("/", 1)[-1]
    if base.endswith(".bin"):
        base = base[:-4]
    if base.startswith("Chead") and base.endswith("Z"):
        heads.append(base)

def worst_in(names):
    best = None
    for n in names:
        a = analyze(n)
        if not a:
            print("  !! not found:", n)
            continue
        # P_final(worst K) proxy: B_pc + 2E (marker slack handled separately)
        pf = a["B_pc"] + 2 * a["E"]
        if best is None or pf > best[0]:
            best = (pf, a)
    return best

print("== worst head ==")
hp, ha = worst_in([h[1:-1] for h in heads])
print("%s P_conv=%X B_pc=%X B_n64=%X" % (ha["base"], hp, ha["B_pc"], ha["B_n64"]))

print("== worst rifle ==")
rp, ra = worst_in(rifles)
print("%s P_conv=%X B_pc=%X B_n64=%X" % (ra["base"], rp, ra["B_pc"], ra["B_n64"]))

print("== worst pistol ==")
pp, pa_ = worst_in(pistols)
print("%s P_conv=%X B_pc=%X B_n64=%X" % (pa_["base"], pp, pa_["B_pc"], pa_["B_n64"]))

# worst body: scan C* bodies (exclude heads/suits/props)
bodies = []
for r in rows:
    if len(r) < 3 or not r[2]:
        continue
    base = r[2].rsplit("/", 1)[-1]
    if base.endswith(".bin"):
        base = base[:-4]
    if base.startswith("C") and base.endswith("Z") and "head" not in base.lower() \
       and "suit" not in base.lower():
        bodies.append(base[1:-1])
bp, ba = worst_in(bodies)
print("== worst body ==")
print("%s P_conv=%X B_pc=%X B_n64=%X" % (ba["base"], bp, ba["B_pc"], ba["B_n64"]))

for label, weapon in (("rifle", ra), ("pistol", pa_)):
    sBpc = ba["B_pc"] + ha["B_pc"] + weapon["B_pc"]
    sBn64 = ba["B_n64"] + ha["B_n64"] + weapon["B_n64"]
    bound = sBpc + 2 * (0x18160 - sBn64)
    print("chain(%s): sumB_pc=%X sumB_n64=%X strict-bound=%X vs PC region 0x1C000 -> %s"
          % (label, sBpc, sBn64, bound, "FITS" if bound <= 0x1C000 else "SHORT by %X" % (bound - 0x1C000)))

# ---- single-file strict bounds: P_final_actual <= B_pc + 2*(R_share_N64 - B_n64)
print()
print("== single-file strict bounds ==")
def check(name, r_n64, region_pc):
    a = analyze(name)
    if not a:
        print("%-16s NOT FOUND" % name); return
    bound = a["B_pc"] + 2 * (r_n64 - a["B_n64"])
    ok = "OK " if bound <= region_pc else "SHORT by %X" % (bound - region_pc)
    print("%-16s B_pc=%6X B_n64=%6X bound=%7X vs region %X -> %s"
          % (name, a["B_pc"], a["B_n64"], bound, region_pc, ok))

check("suit_lf_hand", 0xBD70, 0x18000)
check("trigger", 0xAFD0, 0x17000)
check("watchlaser", 0xAFD0, 0x17000)
check("walletbond", 0xA000, 0x17000)

# weapons: worst over all G* files with R_share_N64 = 0x7530
weapons = []
for r in rows:
    if len(r) < 3 or not r[2]:
        continue
    base = r[2].rsplit("/", 1)[-1]
    if base.endswith(".bin"):
        base = base[:-4]
    if base.startswith("G") and base.endswith("Z"):
        weapons.append(base[1:-1])
worst_w = None
for w in weapons:
    a = analyze(w)
    if not a:
        continue
    bound = a["B_pc"] + 2 * (0x7530 - a["B_n64"])
    if worst_w is None or bound > worst_w[0]:
        worst_w = (bound, w, a)
print("worst weapon: %-14s bound=%X vs 0xF000 -> %s"
      % (worst_w[1], worst_w[0], "OK" if worst_w[0] <= 0xF000 else "SHORT by %X" % (worst_w[0]-0xF000)))

# bondview chain: body+head+weapon, R_chain_N64 = 0x14820 (size_item_buffer)
sBpc = ba["B_pc"] + ha["B_pc"] + ra["B_pc"]
sBn64 = ba["B_n64"] + ha["B_n64"] + ra["B_n64"]
bound = sBpc + 2 * (0x14820 - sBn64)
print("bondview chain: bound=%X vs 0x23000 -> %s"
      % (bound, "OK" if bound <= 0x23000 else "SHORT by %X" % (bound - 0x23000)))
