# Building the PC port

Stages:

1. **Extract assets from your ROM** (§2) — a one-time step using the
   decompilation's own toolchain to pull levels, models, textures, fonts and
   music into `assets/`. *(Only needed to regenerate the committed data files;
   a plain `git clone` already has what the PC build compiles.)*
2. **Build the port** (§3) — a CMake build compiling the game sources plus the
   `port/` layer into a native executable. Needs no ROM.
3. **Generate the PC asset sidecars** (§4) — two pure-Python converters turn
   ROM model / stage data into the PC-layout `data/pcmodels-*` / `data/pccg-*`
   files the port loads at runtime. **Required to run.**

You need a GoldenEye 007 N64 ROM you legally own (`.z64`, big-endian). See the
[Requirements table in the README](https://github.com/jkdansereau/goldeneye-pc-port#requirements)
for accepted versions and hashes.

---

## 1. Dependencies

### Port build

> **Only the Windows (MSYS2 MINGW64) path is tested.** The Debian/Ubuntu and
> macOS columns below are best-effort guidance — the port has never been built
> or run on Linux or macOS. Expect to fix build breaks yourself on those
> platforms.

| Need | Windows (MSYS2 MINGW64) | Debian/Ubuntu | macOS (Homebrew) |
|------|------------------------|---------------|------------------|
| toolchain | `mingw-w64-x86_64-toolchain` | `build-essential` | Xcode CLT / `gcc` |
| CMake | `mingw-w64-x86_64-cmake` | `cmake` | `cmake` |
| SDL2 | `mingw-w64-x86_64-SDL2` | `libsdl2-dev` | `sdl2` |
| zlib | `mingw-w64-x86_64-zlib` | `zlib1g-dev` | `zlib` |
| OpenGL | (in the toolchain) | `libgl1-mesa-dev` | (system) |
| Python 3 | `mingw-w64-x86_64-python` | `python3` | `python3` |

### Asset extraction (decompilation toolchain)

The extraction scripts need `binutils-mips-linux-gnu` (or an equivalent MIPS
binutils), `make`, `git`, and `python3`. They build a small host-compiled
`tools/extractor` and slice blobs straight out of the ROM — **no IDO / IRIX
toolchain is involved in extraction or in the PC build.** (The IDO toolchain is
only needed to build the N64 ROM itself, and its proprietary SGI binaries are
not distributed here — see [`SetupGuide.md`](SetupGuide.md) "Recompile IDO".)
On Windows this is easiest under WSL or a Linux VM. Full details and
alternatives (Docker) are in [`SetupGuide.md`](SetupGuide.md).

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
[`SetupGuide.md`](SetupGuide.md) for the in-depth build/asset pipeline.

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
setup data from the raw ROM at runtime — it reads them from PC-layout *sidecar*
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

These are **pure-stdlib Python 3** (no MIPS toolchain, independent of the
step-2 asset extraction) and read only the ROM plus files already committed to
the repo (`scripts/filelist.u.csv`, `assets/obseg/file_resource_table.inc.c`,
`assets/**/ModelFileHeader.inc.c`, the bg/stan `.inc.c`). Output is a
deterministic function of the ROM. Re-run after any change to `d43_emit.py` /
`d69_emit.py` / `d88_emit.py` or the model/bg converters (`d43_*`, `d69_*`,
`d88_propdefs.py`).

> The release bundle ships `prepare-assets/prepare-assets.py`, which runs all
> three passes against your ROM automatically — see the bundled `README.md`.

> `data/pcmodels-*/` and `data/pccg-*/` are gitignored ROM-derived game data —
> never commit or redistribute them.

---

## 5. Run

```sh
./build-pc/ge007.x86_64          # run from the repo root
```

The ROM in `data/` (from step 4) and the sidecars are both required at
runtime. `ge007.ini` is written under `data/` on first launch.

### Useful flags / env

| | |
|---|---|
| `-level_NN` | boot straight into a solo level (e.g. `-level_09` = Bunker 1) |
| `GE_PCDUMP="first-last:step"` | dump rendered frames as PPM (debugging) |

More diagnostic switches are cataloged in
[`dev/GE-ENV-PROBES.md`](dev/GE-ENV-PROBES.md).
