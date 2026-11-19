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
3. **Generate the PC asset sidecars** (§4): the C converter (`port/convert/`) turns
   ROM model / stage data into the PC-layout `data/pcmodels-*` / `data/pccg-*`
   files the port loads at runtime. **Required to run.**

You need a GoldenEye 007 N64 ROM you legally own (`.z64`, big-endian). See the
[ROM table in the user guide](user-guide.md#whats-a-rom)
for accepted versions and hashes.

---

## 1. Dependencies

### Port build

> **This fork (Project Dam) builds in Docker**: the Nintendo Switch NRO is
> what it ships, and the Linux and Windows builds come from the same compose
> file (§3, "Docker builds"). The dependency tables below are for native
> builds. **Upstream's primary path is Windows (MSYS2 MINGW64)**; upstream CI
> also compiles Linux and ships it in its release bundle. **macOS on Apple Silicon is
> experimental as of v0.6.0**: the release has a `macos-arm64` download
> (`scripts/package-macos.sh` makes it on a Mac; CI runs it) that is not
> notarized, so clear the quarantine flag once before the first launch with
> `xattr -dr com.apple.quarantine <folder>`. This guide covers building it from
> source. On an arm64 Mac the build moves the N64 address window to a
> high host base (`PORT_ADDR_BASE`, set automatically); Windows and Linux builds
> are unaffected. The x86_64 macOS path (Julio C. Rocha, #88) is still in the
> tree but was never verified on Intel hardware.

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

### Asset extraction (decompilation toolchain)

The extraction scripts need `binutils-mips-linux-gnu` (or an equivalent MIPS
binutils), `make`, `git`, and `python3`. They build a small host-compiled
`tools/extractor` and slice blobs straight out of the ROM; **no IDO / IRIX
toolchain is involved in extraction or in the PC build.** (The IDO toolchain is
only needed to build the N64 ROM itself, and its proprietary SGI binaries are
not distributed here; see [`SetupGuide.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/SetupGuide.md) "Recompile IDO".)
On Windows this is easiest under WSL or a Linux VM. Full details and
alternatives (Docker) are in `SetupGuide.md`.

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

On Windows, `build-pc.sh` also locates the MSYS2 MinGW64 toolchain itself
(so it works from Git Bash or a non-login shell too, even with another
`cmake` earlier on PATH) and, if native child processes would get an
unwritable `TMP`, re-runs the configure+build under PowerShell with a
writable one. If MSYS2 is not installed at `C:\msys64`, set
`GE_MSYS2_ROOT` (e.g. `GE_MSYS2_ROOT='C:\tools\msys64'` for a custom
install location). Re-exec logs:
`build-pc/ge007-native-reexec-*.log`.

For PAL/JP you must first generate that region's ROM-asset symbol file
(the US one is committed):

```sh
python3 scripts/gen_romassets.py e     # PAL   -> port/src/romassets_e.s
python3 scripts/gen_romassets.py j     # JP    -> port/src/romassets_j.s
```

The executable lands at `build-pc/ge007.x86_64` (`.exe` on Windows). PAL/JP
builds are named `ge007.pal-final.x86_64` / `ge007.jpn-final.x86_64`.

### Docker builds

`compose.yaml` builds every target this fork cares about, from any
machine with Docker (Linux, WSL, macOS or Windows). The repository is
bind-mounted at `/src`, so a source change needs no image rebuild. No ROM is
needed to build. The PAL/JP symbol files of §3 are still needed for those
regions.

| Service | Image | Output |
|---|---|---|
| `switch` | `Dockerfile.build` (pinned devkitA64, libnx, Switch portlibs) | `build-switch/ge007.aarch64.nro` |
| `linux` | `Dockerfile.build` (same image, host SDL2/GL/zlib) | `build-linux/ge007.x86_64` |
| `windows` | `Dockerfile.windows` (Fedora 43, MinGW-w64) | `build-win/ge007.x86_64.exe` + DLLs |
| `shell` | `Dockerfile.build` | interactive bash in the toolchain |

```sh
docker compose build             # once: Dockerfile.build (switch, linux, shell)
docker compose run --rm switch
docker compose run --rm linux
```

(The repository's `Dockerfile` and `docker-compose.yml` (service `ge007`) are
upstream's N64 matching-build environment, unrelated to these. When both
files are present, `docker compose` reads `compose.yaml`.)

#### Nintendo Switch

`.github/workflows/switch.yml` runs the same build in the same pinned image on
every push and pull request, and it is the only workflow that runs on this
fork. A `project-dam-vX.Y.Z` tag also publishes a release.

- **SD card for a test install:** after the build, from the `shell` service,
  `tools/switch/make_sd.sh <rom.z64> [out_dir]` (default `build-switch/sd`)
  assembles `switch/project-dam/` with the NRO, a copy of the ROM and the
  converted sidecars (§4). Copy its `switch/` folder to the root of the SD
  card.
- **Release archive:** `tools/switch/package_release.sh <version> [out_dir]`
  (default `dist`) zips the NRO, `README.txt`,
  `LICENSE` and `NOTICE`. It ships no game content. CI passes
  `-DPROJECT_DAM_VERSION=<version>` to the build; a local build shows
  `dev-<hash>` in the homebrew menu.
- **Test builds over nxlink:** a separate build directory (`build-switch-link`)
  configured with `-DNX_LINKSTDIO=ON -DNX_ARGS=ON` sends the log and crash dump
  back to the PC and reads `data/ge007.args`. These NROs are never shipped.
  Procedure and client: `docs/switch/nxlink-stdio.md`. Address model:
  `docs/switch/address-model.md`.

#### Windows (cross-compilation)

A Windows x86_64 executable builds without MSYS2:

```sh
docker compose build windows                      # once: the toolchain image
docker compose run --rm windows                   # ntsc-final
docker compose run --rm -e ROMID=pal-final windows
```

The output lands in `build-win/`: `ge007.x86_64.exe` plus the DLLs it needs
(`SDL2.dll`, `zlib1.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`), so
the folder runs as-is. Run it from the repo root like any other build
(`build-win\ge007.x86_64.exe`), with the ROM and the step-4 sidecars in
`data/`.

The image (`Dockerfile.windows`) is Fedora 43 with its MinGW-w64 packages
(GCC 15, mingw-w64 13, zlib) and libsdl.org's prebuilt SDL2 MinGW package;
the base image and the SDL2 archive are pinned. Fedora 43 is the minimum:
older mingw-w64 headers define `sprintf`/`snprintf` as `static inline`,
which clashes with the decomp's own prototypes. The MSYS2 build above is
upstream's reference and the one upstream CI ships; this fork uses the
Docker build to check that the PC port still compiles. CMake handles three differences of the
cross toolchain, only when cross-compiling with MinGW (the MSYS2 build is
unchanged):

- the host `string.h`/`sched.h`/`stdlib.h` are taken from the compiler's
  include dirs (the compiler sits in `/usr/bin`, so `bin/../include` would
  be the Linux glibc headers);
- `_WIN32_WINNT=0x0601`, the API level of MSYS2's headers (Fedora defaults to
  Windows 10, which declares `GetCurrentThreadStackLimits` with a signature
  that conflicts with `port/src/crash.c`);
- `--defsym=objectiveGetStatus_WEAK=get_status_of_objective`: upstream
  binutils does not resolve a cross-object reference to a `#pragma weak`
  alias on PE (MSYS2's patched binutils does).

In a git worktree the version line says `build : unknown`: the container
cannot follow the worktree's `.git` file. A normal clone shows the hash.

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

The game converts the ROM itself on first launch, when the sidecars are
missing, so normally you do nothing. To generate them by hand (or for another
region), use the `ge007-convert` command-line tool built next to the engine
(`build-pc/port/convert/ge007-convert`, or `build-linux/port/convert/` /
`build-win/port/convert/` from the Docker builds):

```sh
ge007-convert --rom data/ge007.ntsc-final.z64 --out .    # writes ./data/pcmodels-*/ and ./data/pccg-*/
```

The region comes from the ROM header; `--region pal-final|jpn-final` overrides
it, and `--only d43|d69|d88` runs a single pass (d43 = models, d69 = stage bg +
stan, d88 = stage setups; d88 appends to d69's output, so run it after d69).
Output is a deterministic function of the ROM (~1.3 MB models + ~3.6 MB
stage data).

**PAL / JP note:** both regions convert, build and boot from source (finding
D258, fixed 2026-10-02; D487/D488 fixed the PAL frame height and TV type).
Build into a separate directory, for example
`BUILD_DIR=build-pal ./build-pc.sh pal-final`. Release packages are still
NTSC-U only until per-region packaging lands (issue #85).

The converter is plain C (libc + zlib) and carries the decomp metadata it needs
(`scripts/filelist.*.csv`, `assets/obseg/file_resource_table.inc.c`,
`assets/**/ModelFileHeader.inc.c`) as constants in
`port/convert/convert_data.c`. Regenerate that file with
`python tools_pc/gen_convert_data.py` (repo root) only when those decomp inputs
change; this is the one remaining dev-time Python step. The format rationale
for each pass is in the header comments of `port/convert/conv_d43.c`,
`conv_d69.c` and `conv_d88.c` and in findings D43..D132.

> Release bundles ship no converter binary; the engine does this conversion
> in-process on first launch (also on Switch, where the files land in
> `switch/project-dam/data/`).

> `data/pcmodels-*/` and `data/pccg-*/` are gitignored ROM-derived game data;
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
[`dev/GE-ENV-PROBES.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/dev/GE-ENV-PROBES.md).

## Windows build environment troubleshooting

Moved from `AGENTS.md`, which keeps a one-line summary of each.
Three recurring failure modes; diagnose in this order.

1. **`Cannot create temporary file in C:\Windows\: Permission denied`** at the
   link step. The PE toolchain (ninja → cmd → gcc/ld) needs a writable
   TMP/TEMP; the msys→native env conversion drops it in non-login shells
   (agent harnesses; the `C:/msys64` tree here was built for a relocated MSYS2
   root — `<msys>` in the placeholder legend — so its path conversion is
   unreliable). `build-pc.sh` now **self-heals**: it
   probes a native child's TMP (via a file — piped `cmd.exe` stdout is
   unreliable under msys console emulation) and, if broken, re-runs
   cmake+build under PowerShell with `TMP`/`TEMP` set natively (the
   native→native boundary passes env through intact; only msys→native is
   broken). If you run cmake/ninja *by hand* from a broken shell: run them
   from cmd/PowerShell with `C:\msys64\mingw64\bin` on PATH.
   **Guard revision (2026-09-28):** the re-exec writes a self-contained
   `.ps1` to a temp file and runs `powershell -File` with plain path/word
   args only (the msys→native argv conversion mangles `$`-bearing
   `-Command` strings; a file write is conversion-free). Inside: PATH is
   built explicitly (never trust the inherited value); TMP is chosen from
   writable candidates (`%LocalAppData%\Temp` first —
   `[System.IO.Path]::GetTempPath()` honours the inherited *broken* TMP,
   observed as `C:\Windows\`); every native step self-logs to
   `build-pc/ge007-native-reexec-{diag,cmake,build}.log`; a null
   `$LASTEXITCODE` ("did not execute") is a distinct failure (exit 2/3) —
   in PS 5.1 a piped native command errors out non-terminating and never
   sets it, so an unguarded `if ($LASTEXITCODE -ne 0)` passes vacuously.
   In `.ps1`, `-DROMID=$Var` is a literal (bare tokens don't expand) — it
   must be `"-DROMID=$Var"`. Launch powershell plainly — `env -i` before it
   breaks the nested cmake launch (verified 2026-09-28).
   **FIXED 2026-09-30 (`fix/build-pc-tmp-reexec`):** the heredoc `.ps1`
   was byte-identical to a hand-extracted copy — not line endings. Three
   real bugs: (a) the TMP probe's inline `cmd //c "... \"path\" ..."` never
   ran (msys re-escapes embedded quotes as `\"`, which cmd rejects), so
   *every* build took the re-exec, and it only tested that TMP existed;
   (b) the `.ps1`'s toolchain dirs were derived from `command -v cmake`,
   which in Git Bash/agent shells is a pip cmake (no MSYS2 toolchain) —
   the hand-run worked because it passed explicit `C:\msys64\...` paths;
   (c) `/tmp` + `cygpath` disagree when Git Bash and MSYS2 `usr/bin` are
   both on PATH, so the `.ps1` could be written to one `/tmp` and
   powershell pointed at the other (it then exits 0 having done nothing).
   Now: the MSYS2 `mingw64/bin` is located and validated (override:
   `GE_MSYS2_ROOT`) and prepended to PATH; the probe is a `.cmd` file that
   tests TMP *writability*; all native-facing files live in the build dir
   with paths from bash's own `pwd -W`. Verified from an agent Git Bash
   shell: fresh + incremental builds, both on the direct path and with a
   forced unwritable TMP (`TMP=C:\Windows\`) through the re-exec.
   **FIXED 2026-09-30 (`fix/build-pc-reexec-noop`): the re-exec exited rc=2
   with the diag log ending `cmake ran LASTEXITCODE=[]`.** From a shell with
   MSYS2 `usr/bin` first on PATH, powershell inherits a stripped ~13-var env
   with `PATHEXT=.CPL` and no ComSpec/TMP/TEMP, so `& cmake.exe` silently
   launches nothing. The `.ps1` now restores PATHEXT and ComSpec. In that
   shell the re-exec itself is legitimately needed (no TMP), so the probe
   is correct. Verified: MSYS2-PATH shell, forced `TMP=C:\Windows\`, and a
   no-change incremental run all exit 0 with a fresh exe.
   **FIXED 2026-10-04 (`fix/build-arch-tag`, D515):** a FRESH build dir
   configured through the re-exec cached `CMAKE_HOST_SYSTEM_PROCESSOR ""` /
   `CMAKE_SYSTEM_PROCESSOR ""` (the stripped env also drops
   `PROCESSOR_ARCHITECTURE`, which CMake reads for the host processor — same
   class as PATHEXT/ComSpec), so `cmake/TargetArch.cmake` produced an empty
   arch tag: `Target arch:  (64bit=FALSE)`, binary named `ge007..exe`
   (a correct 64-bit build; only the name was wrong — found during T7, D513).
   The `.ps1` now restores `PROCESSOR_ARCHITECTURE` from the machine
   environment (fallback `AMD64`; `SystemRoot`/`windir` likewise) and logs
   the four vars to the diag log; `cmake/TargetArch.cmake` falls back to
   `${CMAKE_C_COMPILER} -dumpmachine` when `CMAKE_SYSTEM_PROCESSOR` is empty
   and FATAL_ERRORs instead of emitting an untagged binary. Existing build
   dirs are unaffected (value already cached); a dir configured before the
   fix picks up x86_64 on its next in-place reconfigure.
2. **`cannot open output file ge007.x86_64.exe: Permission denied`.** A
   **running** `ge007.x86_64.exe` locks the output file (Windows rule; you
   can't relink over a live PE). Check with `Get-Process | Where-Object {
   $_.ProcessName -like '*ge007*' }` and close the game before rebuilding.
   An agent must not kill the user's game process to make a link succeed.
3. **`cc1.exe: ... libmpfr-6.dll: cannot open shared object file`.** The
   calling PATH lacks `C:\msys64\mingw64\bin` (the gcc driver finds cc1 via
   its own directory, but the child needs the mingw DLL dir on PATH). In an
   msys shell: `export PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH"`
   before building.
