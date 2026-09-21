---
title: Building
description: Full build and asset-extraction guide for the GoldenEye 007 PC port on Windows, Linux, and macOS.
---

## Building the PC port

Stages:

1. **Extract assets from your ROM** (§2): a one-time step using the
   decompilation's own toolchain to pull levels, models, textures, fonts and
   music into `assets/`. *(Only needed to regenerate the committed data files;
   a plain `git clone` already has what the PC build compiles.)*
2. **Build the port** (§3): a CMake build compiling the game sources plus the
   `port/` layer into a native executable. Needs no ROM.
3. **Generate the PC asset sidecars** (§4): two pure-Python converters turn
   ROM model / stage data into the PC-layout `data/pcmodels-*` / `data/pccg-*`
   files the port loads at runtime. **Required to run.**

You need a GoldenEye 007 N64 ROM you legally own (`.z64`, big-endian). See the
[Requirements table in the README](https://github.com/jkdansereau/goldeneye-pc-port#requirements)
for accepted versions and hashes.

---

## 1. Dependencies

### Port build

> **The Windows (MSYS2 MINGW64) path is the primary one.** Linux is compiled
> by CI and ships in the release bundle. macOS is supported natively on both
> Intel and Apple Silicon (arm64); see the note below the table for the
> arm64-specific points.

| Need | Windows (MSYS2 MINGW64) | Debian/Ubuntu | macOS (Homebrew) |
|------|------------------------|---------------|------------------|
| toolchain | `mingw-w64-x86_64-toolchain` | `build-essential` | Xcode CLT + Homebrew `gcc` |
| CMake | `mingw-w64-x86_64-cmake` | `cmake` | `cmake` |
| SDL2 | `mingw-w64-x86_64-SDL2` | `libsdl2-dev` | `sdl2` |
| zlib | `mingw-w64-x86_64-zlib` | `zlib1g-dev` | `zlib` |
| OpenGL | (in the toolchain) | `libgl1-mesa-dev` | (system) |
| Python 3 | `mingw-w64-x86_64-python` | `python3` | `python3` |

On macOS, install the build dependencies with:

```sh
brew install cmake gcc sdl2 zlib python3
```

The project requires real GNU GCC for the decomp's Plan 9 struct extensions.
Apple's `/usr/bin/gcc` is Clang and is not compatible. CMake automatically
selects Homebrew's versioned `gcc-N`/`g++-N` executables.

**Apple Silicon (arm64).** Everything above applies as-is; `./build-pc.sh`
detects the architecture and produces `build-pc/ge007.aarch64`. Two arm64
specifics are handled automatically:

- Native arm64 cannot map the low 4 GiB (mandatory `__PAGEZERO`), so the N64
  address-space window is placed at a 16 TiB host base (`PORT_ADDR_BASE`,
  `port/include/port_addr.h`). It is a CMake cache var
  (`-DPORT_ADDR_BASE=...`) for tooling that needs it elsewhere; every
  game-visible 32-bit value is identical to the other platforms.
- GCC's `libstdc++`/`libgcc_s` and SDL2 come from Homebrew and are **not**
  system libraries, so a redistributable build must bundle them — use
  `tools_pc/bundle-mac.sh` (below), which produces a signed, double-clickable
  `GoldenEye.app`.

### Asset extraction (decompilation toolchain)

The extraction scripts need `binutils-mips-linux-gnu` (or an equivalent MIPS
binutils), `make`, `git`, and `python3`. They build a small host-compiled
`tools/extractor` and slice blobs straight out of the ROM; **no IDO / IRIX
toolchain is involved in extraction or in the PC build.** (The IDO toolchain is
only needed to build the N64 ROM itself, and its proprietary SGI binaries are
not distributed here; see [`SetupGuide.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/SetupGuide.md) "Recompile IDO".)
On Windows this is easiest under WSL or a Linux VM. Full details and
alternatives (Docker) are in [`SetupGuide.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/SetupGuide.md).

---

## 2. Extract assets

Put your **US** ROM at the repository root as `baserom.u.z64` (this name is
required by the extraction scripts; it is git-ignored and never committed),
then:

```sh
./scripts/extract_baserom.u.sh
```

For PAL or JP, additionally place `baserom.e.z64` / `baserom.j.z64` at the root
and run:

```sh
./scripts/extract_baserom.u.sh && ./scripts/extract_diff.e.sh   # PAL
./scripts/extract_baserom.u.sh && ./scripts/extract_diff.j.sh   # JP
```

(US extraction is a prerequisite for the others.)

This populates `assets/` with the generated `.bin` blobs the build needs. See
[`SetupGuide.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/SetupGuide.md) for the in-depth build/asset pipeline.

---

## 3. Build

```sh
./build-pc.sh ntsc-final        # or: pal-final / jpn-final
```

which is equivalent to:

```sh
cmake -S . -B build-pc -DROMID=ntsc-final
cmake --build build-pc -j
```

For PAL/JP you must first generate that region's ROM-asset symbol file
(the US one is committed):

```sh
python3 scripts/gen_romassets.py e     # PAL   -> port/src/romassets_e.s
python3 scripts/gen_romassets.py j     # JP    -> port/src/romassets_j.s
```

The executable lands at `build-pc/ge007.x86_64` (`.exe` on Windows). PAL/JP
builds are named `ge007.pal-final.x86_64` / `ge007.jpn-final.x86_64`.

---

## 4. Generate the PC asset sidecars (required to run)

The port does **not** read model geometry, stage bg/stan data, or per-level
setup data from the raw ROM at runtime; it reads them from PC-layout *sidecar*
files under `data/`, produced offline by three converters. **Without them the
game shows the intro logos and then crashes** in
`modelPromoteNodeOffsetsToPointers` (finding D179) or on the first level load
(missing stage setup).

Put your ROM in `data/` first (same file the game runs from):

```sh
mkdir -p data
cp /path/to/your/rom.z64 data/ge007.ntsc-final.z64     # or pal-final / jpn-final
```

Then run all three emit passes for that region, **in this order** (d88 appends
to d69's output):

```sh
python3 tools_pc/d43_emit.py ntsc-final          # -> data/pcmodels-ntsc-final/{pcmodels.bin,manifest.csv}  (~1.3 MB)
python3 tools_pc/d69_emit.py ntsc-final          # -> data/pccg-ntsc-final/{pccg.bin,manifest.csv}          (bg + stan)
python3 tools_pc/d88_emit.py ntsc-final --regen  #    appends the 21 per-level Usetup*Z stage-setup files -> ~3.6 MB
```

**PAL / JP note:** sidecar generation for these regions is currently broken at
the source-data level (finding D258); use an NTSC-U ROM until issue #85 lands.

These are **pure-stdlib Python 3** (no MIPS toolchain, independent of the
step-2 asset extraction) and read only the ROM plus files already committed to
the repo (`scripts/filelist.u.csv`, `assets/obseg/file_resource_table.inc.c`,
`assets/**/ModelFileHeader.inc.c`, the bg/stan `.inc.c`). Output is a
deterministic function of the ROM. Re-run after any change to `d43_emit.py` /
`d69_emit.py` / `d88_emit.py` or the model/bg converters (`d43_*`, `d69_*`,
`d88_propdefs.py`).

> The release bundle ships the same converter frozen as
> `prepare-assets/ge007-convert`; the game spawns it on first launch when the
> sidecars are missing, so end users never run it by hand. See the bundled
> `README.md`.

> `data/pcmodels-*/` and `data/pccg-*/` are gitignored ROM-derived game data;
> never commit or redistribute them.

---

## 5. Run

```sh
./build-pc/ge007.x86_64          # run from the repo root
```

The ROM in `data/` (from step 4) and the sidecars are both required at
runtime. `ge007.ini` is written under `data/` on first launch.

On macOS the binary is `./build-pc/ge007.aarch64` (Apple Silicon) or
`./build-pc/ge007.x86_64` (Intel).

### Debugging address bugs (`PORT_ADDR_STRICT`)

The port realises the N64 address space at a host base (`PORT_ADDR_BASE`,
non-zero only on arm64 macOS). The recurring failure mode is a 32-bit value
that is *not* a valid N64 address reaching `portN64ToHost()` — a truncated host
pointer, or one that was never re-based. The resulting fault usually happens
far from the cause, and sometimes there is no fault at all, just silent
corruption.

```sh
cmake -S . -B build-strict -DROMID=ntsc-final -DPORT_ADDR_STRICT=ON
cmake --build build-strict -j
```

Every conversion is then validated against the mapped regions and offenders are
reported at the conversion site with a backtrace naming the caller. It is
**off by default and compiled out entirely**, so normal builds pay nothing.

To prove the detector is live rather than merely silent:

```sh
GE_ADDRSTRICT_SELFTEST=1 ./build-strict/ge007.aarch64 -level_09
```

See findings D328–D334 for the bug family this exists to catch.

### Packaging a macOS app

```sh
tools_pc/bundle-mac.sh           # -> dist/GoldenEye.app + a .zip + sha256
```

Builds a double-clickable, ad-hoc-signed `GoldenEye.app` (the engine plus
bundled SDL2 / SDL3 / `libstdc++` / `libgcc_s`, README, licenses, and the
`prepare-assets` tool). Note that Homebrew's `sdl2` is **sdl2-compat**, a
shim over SDL3 that locates its backend through a *relative* rpath
(`@loader_path/../../../../opt/sdl3/lib`): copy it without SDL3 and
repointing that rpath, and it shows a modal error dialog from its
initializer and the app hangs before `main()`. The script does both. It contains **no ROM and no game data**: the player
drops their own ROM at
`GoldenEye.app/Contents/MacOS/data/ge007.ntsc-final.z64` and the app finds it
next to the executable. Because the app is ad-hoc signed rather than
notarised, first launch needs right-click → *Open* (or
`xattr -dr com.apple.quarantine GoldenEye.app`).

### Useful flags / env

| | |
|---|---|
| `-level_NN` | boot straight into a solo level (e.g. `-level_09` = Bunker 1) |
| `GE_PCDUMP="first-last:step"` | dump rendered frames as PPM (debugging) |

More diagnostic switches are cataloged in
[`dev/GE-ENV-PROBES.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/dev/GE-ENV-PROBES.md).
