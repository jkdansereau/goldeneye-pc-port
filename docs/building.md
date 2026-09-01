# Building the PC port

Building has two stages:

1. **Extract assets from your ROM** — a one-time step that uses the
   decompilation's own toolchain to pull levels, models, textures, fonts and
   music out of your ROM into `assets/`.
2. **Build the port** — a CMake build that compiles the game sources plus the
   `port/` layer into a native executable.

You need a GoldenEye 007 N64 ROM you legally own (`.z64`, big-endian). See the
[Requirements table in the README](../README.md#requirements) for accepted
versions and hashes.

---

## 1. Dependencies

### Port build

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
binutils), `make`, `git`, `python3`, and `qemu-irix` for the IRIX IDO
compiler. On Windows this is easiest under WSL or a Linux VM. Full details and
alternatives (Docker, the recompiled IDO toolchain) are in
[`SetupGuide.md`](SetupGuide.md).

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

## 4. Run

```sh
mkdir -p data
cp /path/to/your/rom.z64 data/ge007.ntsc-final.z64
./build-pc/ge007.x86_64          # run from the repo root
```

The port maps the ROM at the N64 cart address at runtime, so the ROM file
itself is still required to run — not just to build. `ge007.ini` is written
next to the working directory on first launch.

### Useful flags / env

| | |
|---|---|
| `-level_NN` | boot straight into a solo level (e.g. `-level_09` = Bunker 1) |
| `GE_PCDUMP="first-last:step"` | dump rendered frames as PPM (debugging) |

More diagnostic switches are catalogued in
[`dev/GE-ENV-PROBES.md`](dev/GE-ENV-PROBES.md).
