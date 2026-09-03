#!/usr/bin/env python3
"""One-time asset preparation for the GoldenEye 007 PC port (alpha).

The port loads PC-layout model / display-list / collision / nav data from two
directories that are derived from a *GoldenEye* ROM and therefore are NOT
shipped in this package:

    data/pcmodels-<region>/   (pcmodels.bin + manifest.csv)
    data/pccg-<region>/       (pccg.bin    + manifest.csv)

This script regenerates them from a ROM you legally own. It calls only the
Python standard library -- no compiler, no N64 toolchain, no network. The
conversion is deterministic: the same ROM always produces byte-identical
output.

Usage (from the folder this script lives in, or anywhere):

    python prepare-assets.py [--rom PATH] [--out DIR] [--keep-temp]

By default it looks for the ROM at  <bundle>/data/*.z64  (and a few other
obvious spots), verifies its SHA-1 to pick the region, and writes the two
directories under  <bundle>/data/ , where <bundle> is the folder that
contains this "prepare-assets" folder (i.e. next to ge007.x86_64.exe).
"""

import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
VENDOR = HERE / "vendor"

# SHA-1 of each supported retail ROM (big-endian .z64) -> region id the
# emit scripts expect. Mirrors ge007.{u,e,j}.sha1 in the source tree.
ROM_SHA1 = {
    "abe01e4aeb033b6c0836819f549c791b26cfde83": "ntsc-final",  # NTSC-U (US)
    "167c3c433dec1f1eb921736f7d53fac8cb45ee31": "pal-final",   # PAL (EU)
    "2a5dade32f7fad6c73c659d2026994632c1b3174": "jpn-final",   # NTSC-J (JP)
}

# (script, args, produces) — run in order. d88 has no output of its own: it
# appends the 21 per-level stage-setup files to the pccg dir d69 just made,
# and --regen makes that idempotent.
EMIT_STEPS = (
    ("d43_emit.py", (),          "pcmodels-{region}/pcmodels.bin"),
    ("d69_emit.py", (),          "pccg-{region}/pccg.bin"),
    ("d88_emit.py", ("--regen",), "pccg-{region}/pccg.bin"),
)
OUTPUT_DIRS = ("pcmodels-{region}", "pccg-{region}")


def die(msg):
    print(f"\nprepare-assets: error: {msg}\n", file=sys.stderr)
    sys.exit(1)


def sha1_of(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def find_rom(explicit, bundle_root):
    if explicit:
        p = Path(explicit).expanduser()
        if not p.is_file():
            die(f"--rom {p} : not a file")
        return p
    search = []
    for base in (bundle_root, bundle_root / "data", HERE, HERE / "vendor" / "data"):
        search += sorted(base.glob("*.z64")) + sorted(base.glob("*.n64")) + sorted(base.glob("*.v64"))
    # de-dup, keep order
    seen, cand = set(), []
    for p in search:
        if p.resolve() not in seen:
            seen.add(p.resolve())
            cand.append(p)
    if not cand:
        die("no .z64 ROM found. Put your GoldenEye ROM at "
            f"{bundle_root / 'data'} (create the 'data' folder) or pass --rom PATH.")
    if len(cand) > 1:
        print("prepare-assets: multiple ROM candidates found:")
        for p in cand:
            print(f"    {p}")
        print("prepare-assets: using the first; pass --rom PATH to choose.")
    return cand[0]


def main():
    ap = argparse.ArgumentParser(description="Generate the GoldenEye PC port's ROM-derived asset dirs.")
    ap.add_argument("--rom", help="path to your .z64 ROM (default: auto-detect near the bundle)")
    ap.add_argument("--out", help="bundle root to write data/ into (default: the folder holding prepare-assets)")
    ap.add_argument("--keep-temp", action="store_true", help="keep the temporary ROM copy under vendor/data/")
    args = ap.parse_args()

    if not VENDOR.is_dir():
        die(f"missing {VENDOR} -- this script must stay inside the 'prepare-assets' folder from the zip.")
    for script, _extra, _produces in EMIT_STEPS:
        if not (HERE / script).is_file():
            die(f"missing {HERE / script}")
    if not (HERE / "d88_propdefs.py").is_file():
        die(f"missing {HERE / 'd88_propdefs.py'} (imported by d88_emit.py)")

    bundle_root = Path(args.out).expanduser().resolve() if args.out else HERE.parent

    rom = find_rom(args.rom, bundle_root)
    print(f"prepare-assets: ROM      {rom}")
    digest = sha1_of(rom)
    region = ROM_SHA1.get(digest)
    if region is None:
        die(f"ROM SHA-1 {digest} is not a recognised retail GoldenEye 007 image.\n"
            "         Supported: NTSC-U / PAL / NTSC-J, big-endian .z64.\n"
            "         A byte-swapped (.n64/.v64) or overdumped ROM will not work -- convert to .z64 first.")
    print(f"prepare-assets: SHA-1    {digest}  ->  region '{region}'")

    # The emit scripts assume CWD is a tree with scripts/ assets/ data/ .
    work = VENDOR
    (work / "data").mkdir(exist_ok=True)
    staged_rom = work / "data" / f"ge007.{region}.z64"
    if not (staged_rom.exists() and staged_rom.stat().st_size == rom.stat().st_size):
        shutil.copy2(rom, staged_rom)

    # Fresh start so a re-run is deterministic (d69 recreates its dir; d88
    # --regen edits it in place).
    for d in OUTPUT_DIRS:
        p = work / "data" / d.format(region=region)
        if p.exists():
            shutil.rmtree(p)

    for script, extra, produces in EMIT_STEPS:
        argv = [sys.executable, str(HERE / script), region, *extra]
        print(f"prepare-assets: running  {' '.join(argv[1:])} ...")
        r = subprocess.run(argv, cwd=work)
        if r.returncode != 0:
            die(f"{script} exited {r.returncode}")
        want = work / "data" / produces.format(region=region)
        if not want.is_file():
            die(f"{script} did not produce {want}")

    dest_data = bundle_root / "data"
    dest_data.mkdir(parents=True, exist_ok=True)
    for d in OUTPUT_DIRS:
        src = work / "data" / d.format(region=region)
        dest = dest_data / d.format(region=region)
        if dest.resolve() == src.resolve():
            continue
        if dest.exists():
            shutil.rmtree(dest)
        shutil.move(str(src), str(dest))
        n = sum(1 for _ in dest.rglob("*") if _.is_file())
        print(f"prepare-assets: wrote    {dest}  ({n} files)")

    if not args.keep_temp and staged_rom.exists() and staged_rom.resolve() != rom.resolve():
        staged_rom.unlink()

    print("\nprepare-assets: done. You can now run ge007.x86_64.exe from the bundle folder.")
    print(f"                (Your ROM must also be at {dest_data / f'ge007.{region}.z64'} for the game itself.)")
    if region != "ntsc-final":
        print("                NOTE: only the NTSC-U ROM is validated in this alpha; "
              f"'{region}' is experimental.")


if __name__ == "__main__":
    main()
