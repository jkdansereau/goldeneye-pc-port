# GoldenEye 007 — PC Port

A work-in-progress port of [GoldenEye 007](https://en.wikipedia.org/wiki/GoldenEye_007)
to modern platforms (Windows / Linux / macOS), built on the decompilation in
this repository.

It follows the approach of the
[Perfect Dark PC port](https://github.com/perfect-dark-pc-port/perfect_dark),
which ports the same Rare "Indy" engine (a later revision).

> **Status: Phase 2 (rendering) in progress.** The game boots on PC, plays the
> intro music, and renders the entire intro (logo → gun barrel → cast);
> the current blocker is first stage load (BG-file endianness, finding D69).
> Current session state and immediate tasks: **[docs/HANDOFF.md](docs/HANDOFF.md)**.

## Read this first

* **[docs/PCPortResearch.md](docs/PCPortResearch.md)** — the preliminary
  research: how the PD port works, what GE-specific work remains (the `G_SETTEX`
  RSP command, custom color-combiner/render modes), the hardware surfaces that
  must be shimmed, and the phased plan. **Start here.**
* **[port/README.md](port/README.md)** — the port layer layout and how it fits
  together.

## How it works (one paragraph)

The N64 game code (`src/game`, `src`) is compiled **unmodified** for the host.
The **RSP (the N64's 3D coprocessor) is emulated in software**
(`port/fast3d/`): it interprets the display list the game builds and emits
OpenGL calls, bypassing the RDP. All other N64 hardware (VI, AI, PI, SI, OS
threads/timers) is shimmed in `port/src/libultra.c` and friends. Saves
(Memory Pak / EEPROM) are backed by files.

## Layout

```
CMakeLists.txt        # PC build (parallel to the N64 Makefile)
cmake/                # CMake helper modules
build-pc.sh           # configure + build helper
port/
├── fast3d/           # software RSP (adapted from the PD port)
├── include/          # port-facing headers
└── src/              # port layer (main, libultra shims, video, audio, input, ...)
docs/PCPortResearch.md
```

The N64 build (`Makefile`, `tools/`, `rsp/`, etc.) is **untouched**.

## Building

```sh
./build-pc.sh ntsc-final     # or pal-final / jpn-final
```

Then put your ROM in `./data/` and run the binary from the repo root.
(The ROM is not distributed; you must own the game.)

## Phased plan (summary)

| Phase | Goal | Status |
|-------|------|--------|
| 0 | Scaffolding: build system + port-layer interfaces | done |
| 1 (+1.5) | Boot to window / first frame: ROM map, OS shims, ABI reconciliation | done |
| 2 | Rendering: fast3d software RSP, asset conversion/fixups | **in progress** (full intro renders; stage load = D69) |
| 3 | Audio + input (SDL device, mixer, gamepad) | not started |
| 4 | Saves + polish (file-backed EEPROM, widescreen, config) | not started |

Details and risks: [docs/PCPortResearch.md §8–9](docs/PCPortResearch.md).
