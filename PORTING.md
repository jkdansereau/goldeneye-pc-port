# GoldenEye 007 — PC Port

A work-in-progress port of [GoldenEye 007](https://en.wikipedia.org/wiki/GoldenEye_007)
to modern platforms (Windows / Linux / macOS), built on the decompilation in
this repository.

It follows the approach of the
[Perfect Dark PC port](https://github.com/perfect-dark-pc-port/perfect_dark),
which ports the same Rare "Indy" engine (a later revision).

> **Status: scaffolding.** The directory structure, build system, and port-layer
> interfaces are in place. The actual implementation is being brought in phase
> by phase. See below.

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

## Building (once implementation lands)

```sh
./build-pc.sh ntsc-final     # or pal-final / jpn-final
```

Then put your ROM in `./data/` and run the binary. (The ROM is not
distributed; you must own the game.)

## Phased plan (summary)

| Phase | Goal |
|-------|------|
| 0 | Scaffolding (this change) |
| 1 | Boot to a window: system/fs/rom/config/video + OS shims |
| 2 | Rendering: bring in fast3d, add `G_SETTEX`, port scheduler |
| 3 | Audio + input |
| 4 | Saves + polish (widescreen, mouse-look, FPS, config) |

Details and risks: [docs/PCPortResearch.md §8–9](docs/PCPortResearch.md).
