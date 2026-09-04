# GoldenEye 007 PC Port

[![CI](https://github.com/jkdansereau/goldeneye-pc-port/actions/workflows/ci.yml/badge.svg)](https://github.com/jkdansereau/goldeneye-pc-port/actions/workflows/ci.yml)
![status](https://img.shields.io/badge/status-alpha_v0.1.0_(Phase_2_of_4)-orange)
![tested on](https://img.shields.io/badge/tested_on-Windows_%2B_Linux_x86--64-blue)
![license](https://img.shields.io/badge/license-MIT-green)
![built with](https://img.shields.io/badge/built_with-coding_agents-8A2BE2)

**A native PC port of _GoldenEye 007_ (Rare, 1997, Nintendo 64), compiled from
the [GoldenEye 007 decompilation](https://github.com/n64decomp/007) — the
original N64 game running from reconstructed source, not the Xbox 360 remaster.**
The first alpha, **[v0.1.0](../../releases)**, is out for Windows and Linux: it
boots, renders the full intro and front end, and runs all 21 solo missions — in
a full-campaign playtest 18 of the 21 were completable start to finish (two
crash mid-level; the final level can't be finished yet). **There is no audio
yet** and rough edges remain; this is
Phase 2 of 4. See [Status](#status) and [Background](#background).

It is also a **research project on AI-agent collaboration in a large,
unfamiliar, low-level codebase** — how far two coding agents (a local
open-weight model and Claude), driven by one person part-time, can be pushed
through ~230 translation units of unmodified big-endian MIPS game code and made
to run on a 64-bit desktop. See [Background](#background).

Technically, it follows the architecture of the
[Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark) — the same
Rare "Indy" engine family, one hardware generation apart. The unmodified game C
sources are compiled for the host; the N64's Reality Signal Processor (RSP) is
emulated in software; every other hardware surface (video, audio, input,
timers, save storage) is shimmed in a dedicated `port/` layer.

> [!IMPORTANT]
> **You must supply your own GoldenEye 007 ROM.** This repository contains no
> Nintendo code or assets, and no ROM. Nothing here is distributable as a
> playable game — see [Requirements](#requirements) and [Legal](#legal).

<p align="center">
  <img src="docs/img/attract-bunker1.png" width="32%" alt="Bunker 1 intro camera">
  <img src="docs/media/goldeneye-demo.gif" width="32%" alt="~15 s of the port running: mission dossier, Facility, Silo, Jungle, Archives">
  <img src="docs/img/attract-dam.png" width="32%" alt="Dam intro camera">
  <br><em>In-engine, running in the port — Bunker&nbsp;1 and Dam attract views, and a
  ~15&nbsp;s clip (no audio yet, Phase&nbsp;3): mission dossier &rarr; Facility &rarr; Silo &rarr;
  Jungle &rarr; Archives.</em>
</p>

## Quick start

You supply your own **NTSC-U GoldenEye 007 N64 ROM** (`.z64`, big-endian) — no
ROM or game asset is included or distributed. Then:

1. Download the Windows or Linux bundle from [Releases](../../releases) and unpack it.
2. Make a `data/` folder next to the executable and drop the ROM in as `ge007.ntsc-final.z64`.
3. Run the one-time asset step: `python3 prepare-assets/prepare-assets.py` (Python 3.8+, stdlib only).
4. Launch the executable from that folder.

Building from source instead: see [Building](#building). Read the
[Status](#status) caveats first — this is an early alpha.

## Contents

- [Quick start](#quick-start)
- [Background](#background)
- [How this differs from the other GoldenEye PC projects](#how-this-differs-from-the-other-goldeneye-pc-projects)
- [Status](#status)
- [Download](#download)
- [Requirements](#requirements)
- [Building](#building) · [Windows (MSYS2)](#windows-msys2) · [Linux](#linux)
- [Running](#running) · [Default controls](#default-controls)
- [How it works](#how-it-works)
- [Project layout](#project-layout)
- [Documentation](#documentation)
- [Credits](#credits)
- [Legal](#legal)
- [License](#license)

## Background

Beyond the port itself, this is primarily a **research project on agentic
software development** — specifically, on AI-agent collaboration in a large,
unfamiliar, low-level codebase: how far coding agents can be driven, by one
person working part-time, through ~230 translation units of unmodified
big-endian MIPS game code, made to run on a 64-bit desktop.

It used two agents:

- a **local open-weight model** —
  `unsloth/Qwen3.8-27B-GGUF:UD-Q4_K_XL` on a single **RTX 5090**, driven mainly
  through the **[pi](https://pi.dev/)** coding agent (Unsloth Desktop was also
  trialed) — which did the groundwork: the CMake build, the boot chain, the
  software-RSP integration, the offline asset-conversion pipeline, and the
  first rendered frames;
- **Claude** (via **Claude Code** — mostly **Sonnet 5**, with **Opus 5** as an
  escalation tier for the hardest bugs), which joined on 27 Aug 2026 for a
  collaborative phase: the 21-level load/render/no-crash sweep, the ABI/layout
  finding catalog, the SDL input layer, and the front-end flow.

The two **handed work back and forth** through shared written artifacts:

```mermaid
flowchart TD
    H["human<br/>direction · integration · playtest"]
    C["Claude<br/>(Claude Code)"]
    Q["Qwen 3.8<br/>(pi, local RTX 5090)"]
    D[("shared artifacts<br/>findings · notes · handoff doc")]
    H -->|"scoped task + budget"| C
    H -->|"scoped task + budget"| Q
    C -->|"patch + write-up"| D
    Q -->|"patch + write-up"| D
    C <-.->|handoff| Q
    D --> H
```

The handoff document was originally a session-to-session note; it became the
**interface between the two models** — when Claude hit a usage limit
mid-problem, the local model picked the task up from that state and continued.

**By the numbers:**

| Metric | Value |
|---|---|
| Timeline | ~3 weeks, one person part-time |
| Commits | ~320 |
| Root-caused bugs logged | ~190 (`D1`–`D196`; some later merged or withdrawn) |
| Handoff sessions | ~49 |
| ABI edits to game code | 69 files, all `#ifdef PORT` |
| Port layer / tooling | ~17k lines C/C++ · ~6k lines Python |
| Effort split (milestone-weighted) | ~60% Claude · ~40% local model |

The local model built the entire foundation — build, boot chain, RSP wiring,
asset converters — then both agents ran the debugging phase together:

- **Day 4** — the full ~230-unit codebase compiles and links
- **Day 8** — the whole intro renders
- **Day 13** — all 21 solo missions run crash-free
- **Day 14** — front end playable end to end
- **Week 3** — v0.1.0 alpha; a full-campaign playtest completes 18 of 21 missions

Full write-up, timeline chart, and an honest "what worked / what didn't":
[`docs/dev/agentic-development.md`](docs/dev/agentic-development.md). The
workflow itself: [`docs/dev-process.md`](docs/dev-process.md). To cite this
project or its findings, use [`CITATION.cff`](CITATION.cff) (GitHub's "Cite
this repository" menu).

## How this differs from the other GoldenEye PC projects

This is a **native port of the original 1997 Nintendo 64 game, built from its
actual reconstructed source code** — the same lineage as the Perfect Dark PC
port. The other well-known "GoldenEye on PC" projects are something different:
they machine-translate the shipped binary of the *unreleased Xbox 360 XBLA
remaster* — a different game, a different codebase, no shared code with this.

| | This project | [GoldenEye-Recomp](https://github.com/SunJaycy/GoldenEye-Recomp) / [Steam Deck build](https://github.com/couchk1ng/GoldenEye-Recomp-SteamDeck) |
|---|---|---|
| **What it ports** | The original **Nintendo 64** game (1997) | The **Xbox 360 XBLA** HD remaster (built ~2007, never released) |
| **How** | **Decompilation-based source port** — human-reconstructed C, compiled for the host; game logic runs as written | **Static binary recompilation** — the shipped machine code is auto-translated to C; no source-level understanding |
| **Lineage** | [GoldenEye 007 decompilation](https://github.com/n64decomp/007) + [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark) engine family | Xbox 360 "…Recompiled" static-recompilation family |
| **Renderer** | Software RSP → OpenGL | Hardware (Vulkan) |
| **Status** | WIP, Phase 2, alpha v0.1.0 — runs all 21 levels, 18/21 completable in a full playthrough; no audio; rough edges | Playable full game, higher frame rates, online multiplayer |
| **Why it exists** | A [case study in AI-agent collaboration](#background) on a hard low-level codebase; the port is the target, not a product | A polished, playable PC release of the remaster |

**If you just want to play GoldenEye on PC today, use one of the recompilation
projects — they are finished and this is not.** What is interesting here is the
other half: getting the *original* game running from source, and how much of
that work was driven by AI agents.

## Status

> [!WARNING]
> **Alpha — playable, not polished.** This is a work-in-progress research port.
> It runs the full single-player campaign, but there is **no audio**, some
> front-end 3D models and cutscenes are broken, AI characters move too slowly,
> input has known rough edges, and two levels (Bunker ii, Statue) still crash.
> Treat it as an early alpha of the porting work, not a finished way to play
> GoldenEye.

**Phase 2 of 4 (rendering). First alpha: [v0.1.0](../../releases).** The port
boots, renders, and plays through the front end and the campaign. It is not
finished and it is not fully stable.

**Working**

- Boot → Rare/Nintendo logos → gun-barrel → cast intro, fully rendered.
- Front end: main menu → mission select → difficulty → briefing → mission start.
- **All 21 solo missions load and render.** In a full-campaign playtest on the
  packaged Windows build, **18 of 21 were completable start to finish**: Bunker
  ii and Statue crash mid-level (one root cause, D191), and Cradle can't be
  finished because the final-level scripted sequence breaks (D193). See
  [`docs/dev/LEVEL-STATUS.md`](docs/dev/LEVEL-STATUS.md).
- Software RSP (fast3d): textured world geometry, skeletal characters, HUD,
  the GE-specific color-combiner / render modes and `G_TRI4`.
- Input: keyboard + mouse (with mode-aware mouse-look) and SDL game
  controllers, mapped onto the N64 pad. Tunable via `ge007.ini`.
- File-backed EEPROM saves.
- **Windows and Linux** (`x86_64`). Windows is the primary development and
  playtest path; the Linux build boots, renders, and passes the level sweep,
  with far less human playtime.

**Not yet working**

- **Audio** — not implemented (Phase 3). The game runs silent.
- **AI pacing** — scripted and combat AI characters travel to their
  destinations noticeably slower than on N64. This breaks Cradle (the final
  level) via Trevelyan's scripted progression. Top post-alpha fix (D193).
- **Cutscenes** — frequently glitch: skipped, wrong camera, misplaced or
  hovering actors, wrong timing (D148/D160).
- Some front-end 3D models — the spinning Nintendo logo, and the MISSION
  COMPLETE / mode-select models — are mispositioned or absent. (The
  gun-barrel Bond intro renders correctly.)
- Outdoor levels render with a **black sky**; assorted other cosmetic defects
  are tracked in
  [`docs/dev/GRAPHICS-BACKLOG.md`](docs/dev/GRAPHICS-BACKLOG.md).
- No macOS or ARM support; no controller rebinding UI; no widescreen.

Next up: audio (Phase 3), the AI-pacing fix, and the two remaining level
crashes. Cosmetic defects are tracked in
[`docs/dev/GRAPHICS-BACKLOG.md`](docs/dev/GRAPHICS-BACKLOG.md).

## Download

Pre-built **Windows** and **Linux** `x86_64` bundles are published under
[Releases](../../releases), starting with **v0.1.0**. Each contains the engine
executable, its runtime libraries, and the one-time `prepare-assets` tool —
**no ROM and no game assets**. See [Quick start](#quick-start) for the four
steps to get it running, [Requirements](#requirements) for accepted ROMs, and
[Status](#status) for the alpha caveats.

You can also build it yourself; see [Building](#building).

## Requirements

You need a **GoldenEye 007 (Nintendo 64) ROM** that you legally own, in
big-endian (`.z64`) format, matching one of:

| Region | ROMID | ROM filename (in `data/`) | SHA-1 |
|--------|-------|---------------------------|-------|
| NTSC-U (US)  | `ntsc-final` | `ge007.ntsc-final.z64` | `abe01e4aeb033b6c0836819f549c791b26cfde83` |
| PAL (EU)     | `pal-final`  | `ge007.pal-final.z64`  | `167c3c433dec1f1eb921736f7d53fac8cb45ee31` |
| NTSC-J (JP)  | `jpn-final`  | `ge007.jpn-final.z64`  | `2a5dade32f7fad6c73c659d2026994632c1b3174` |

US is the recommended and best-tested version.

The port also relies on the decompilation's asset-extraction step, which pulls
the level, model, texture and music data out of your ROM at build time. That
step, too, requires your ROM and is part of [Building](#building).

## Building

Prerequisites: CMake >= 3.16, a C/C++ toolchain, SDL2, zlib, OpenGL, Python 3,
plus the decompilation's own build dependencies (an IRIX MIPS toolchain via
`qemu-irix`, used only for the one-time asset extraction). See
[`docs/building.md`](docs/building.md) for the full walkthrough and the
asset-extraction details.

### Windows (MSYS2)

```sh
# in the MINGW64 shell
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-SDL2 \
          mingw-w64-x86_64-zlib mingw-w64-x86_64-cmake \
          mingw-w64-x86_64-python make git

git clone https://github.com/jkdansereau/goldeneye-pc-port.git
cd goldeneye-pc-port
# 1. extract assets from your ROM (see docs/building.md)
# 2. build the port
./build-pc.sh ntsc-final          # or pal-final / jpn-final
```

### Linux

> **Untested.** No one has built or run the port on Linux. The steps below are
> the intended path, not a verified one — expect to fix build issues yourself.

```sh
sudo apt install build-essential cmake python3 libsdl2-dev zlib1g-dev libgl1-mesa-dev
git clone https://github.com/jkdansereau/goldeneye-pc-port.git
cd goldeneye-pc-port
# extract assets (docs/building.md), then:
./build-pc.sh ntsc-final
```

The executable is written to `build-pc/ge007.x86_64` (on Windows,
`build-pc/ge007.x86_64.exe`).

## Running

1. Create a `data/` directory in the repo root.
2. Put your ROM in it, named as in the table above
   (e.g. `data/ge007.ntsc-final.z64`).
3. Run the executable **from the repo root**:
   `./build-pc/ge007.x86_64`.

Configuration is written to `ge007.ini` on first run.

### Default controls

| Action              | Keyboard / mouse         | Controller    |
|---------------------|--------------------------|---------------|
| Move / strafe       | `W` `A` `S` `D` / arrows  | Left stick    |
| Aim / look          | Mouse                    | Right stick   |
| Fire (Z)            | Left mouse / `LCtrl`     | Right trigger |
| Aim mode (R)        | Right mouse / `LShift`   | Left trigger  |
| Use / accept (A)    | `Space` / `E` / `X`            | A / X         |
| Reload / cancel (B) | `R` / `F`                | B / Y / RB    |
| L trigger           | `Q`                      | LB            |
| Start               | `Enter` / `Tab`          | Start         |

Mouse sensitivity, Y-inversion and the aim/turn split are tunable in the
`[Input]` section of `ge007.ini`.

## How it works

The R4300 game code in `src/` is compiled **completely unmodified** — the
decompilation's control flow is treated as ground truth. Everything that would
touch N64 hardware is redirected into `port/`:

- **`port/fast3d/`** — a software RSP. It interprets the GBI display list the
  game builds each frame and emits OpenGL, bypassing the RDP. Adapted from the
  Perfect Dark port and extended for GoldenEye's custom GBI.
- **`port/src/gesched.c`** — replaces the RCP scheduler; instead of poking RSP
  registers it drives the software RSP directly.
- **`port/src/libultra.c`** — single-threaded shims for the libultra OS API
  (threads, messages, timers, PI/SI/AI/VI) over the host.
- **`port/src/{video,audio,input,fs,romdata,config}.c`** — the SDL2 / OpenGL /
  filesystem backends.

The 32→64-bit transition forces a small, cataloged class of mechanical
ABI-only edits to ROM-serialized structs (pointer-width reconciliation); these
change no behavior and are documented individually.

Where it diverges from the Perfect Dark port: GoldenEye's N64 serialized asset
formats (level setup, models, backgrounds) are converted **offline** by a set
of Python "sidecar" converters in `tools_pc/`, rather than fixed up at load
time. See [`docs/internals.md`](docs/internals.md) and
[`docs/porting-notes.md`](docs/porting-notes.md).

## Project layout

```
CMakeLists.txt      PC build (parallel to the decomp's Makefile, which is untouched)
build-pc.sh         configure + build helper
src/  include/      the decompilation (game + libultra) - compiled unmodified
port/
  fast3d/           software RSP -> OpenGL
  src/              port layer (main, OS shims, video, audio, input, fs, ...)
  include/          port-facing headers
tools/  Makefile    the N64 build + asset extraction (from the decomp; do not modify)
tools_pc/           PC-port helper + analysis scripts
docs/               see below
```

## Documentation

Key docs are also published as a site:
**<https://jkdansereau.github.io/goldeneye-pc-port/>**.

| Doc | What's in it |
|---|---|
| [`docs/building.md`](docs/building.md) | Full build + asset-extraction guide. |
| [`docs/internals.md`](docs/internals.md) | Architecture, the RSP-emulation approach, GE-vs-PD engine differences, the phased plan. |
| [`docs/porting-notes.md`](docs/porting-notes.md) | The recurring N64→PC bug classes hit during the port, with fixes. |
| [`docs/dev/game-behavior-reference.md`](docs/dev/game-behavior-reference.md) | How the retail N64 game is meant to behave — combat/AI model, difficulty scaling, per-level objectives, timers, weapon data, original-game quirks. Playtest reference. |
| [`docs/dev/agentic-development.md`](docs/dev/agentic-development.md) | The research angle: the two-agent setup, timeline, handoff workflow, and an assessment of what did and didn't work. |
| [`docs/dev-process.md`](docs/dev-process.md) | The investigation workflow in detail — budgets, file partitioning, the finding-log discipline. |
| [`docs/dev/`](docs/dev/) | The raw engineering record: the full finding log, per-level status, graphics backlog, playtest matrices. |
| [`docs/SetupGuide.md`](docs/SetupGuide.md), [`docs/StructureGuide.md`](docs/StructureGuide.md), [`docs/StyleGuide.md`](docs/StyleGuide.md) | Inherited from the decompilation. |

## Credits

This port is a thin layer on a large amount of other people's work.

**Prior work it is built on**

- The [GoldenEye 007 decompilation](https://github.com/n64decomp/007) — years of
  effort by Larry Ficken ("kholdfuzion") and the project's contributors; plus
  zoinkity's GoldenEye documentation, which the decomp started from. This port
  is a fork of that repository.
- The [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark)
  (Ryan Dwyer and contributors) — the reference architecture for this port and
  the source of the `fast3d` software RSP.
- The [Perfect Dark decompilation](https://github.com/n64decomp/perfect_dark) —
  the sibling decomp the PD port is built on.

**Vendored / adapted code**

- `port/fast3d/` — the software RSP, adapted from the PD port. It originates
  with the [Ship of Harkinian](https://github.com/HarbourMasters) /
  libultraship fast3d (© Emill, MaikelChan; MIT — see `port/fast3d/LICENSE.txt`),
  which itself descends from
  [sm64-port](https://github.com/sm64-port/sm64-port)'s fast3d and audio mixer.
- `port/fast3d/glad/` — OpenGL loader generated by
  [glad](https://github.com/Dav1dde/glad) (David Herberth, MIT).
- The decompilation toolchain: [`ido-static-recomp`](https://github.com/decompals/ido-static-recomp)
  (Emill / decompals), [`qemu-irix`](https://github.com/n64decomp/qemu-irix) and
  `rabbitizer` (n64decomp).
- [SDL2](https://libsdl.org) and [zlib](https://zlib.net).

**Methodology**

- Chris Lewis, [*"Decompiling a Nintendo 64 Game in 84 Days"*](https://blog.chrislewis.au/decompiling-a-nintendo-64-game-in-84-days/) (the Snowboard
  Kids decompilation write-up) — the agent-workflow practices in
  [`docs/dev-process.md`](docs/dev-process.md) are adapted from it.

**Tools and models used to develop the port**

- Qwen 3.8 (Alibaba Qwen team), run locally as the
  [`unsloth/Qwen3.8-27B-GGUF`](https://huggingface.co/unsloth/Qwen3.8-27B-GGUF)
  `UD-Q4_K_XL` quant;
  [Unsloth](https://unsloth.ai) (the GGUF quantisation and Unsloth Desktop,
  used as the local model server).
- [pi](https://pi.dev/) — the local coding-agent harness.
- [Claude / Claude Code](https://claude.com/claude-code) (Anthropic).

## Legal

This is a non-commercial fan preservation/research project, in the same
category as the many other N64 decompilation and native-port repositories on
GitHub. It follows the same conventions they do:

- **No ROM and no game assets are distributed** — not in this repository and
  not in any release. Textures, audio, models, level data and in-game text are
  extracted from a ROM *you already own*, on *your* machine, at build time.
- The repository is a fork of the public
  [GoldenEye 007 decompilation](https://github.com/n64decomp/007) and inherits
  its contents unmodified (see [`NOTICE`](NOTICE) for what that includes).
- No official logos, box art, or marketing assets are used. "GoldenEye 007",
  "007", "James Bond" and related marks belong to their respective owners
  (Nintendo, Microsoft/Rare, MGM, Danjaq, EON Productions).
- **Pre-built binaries** published under [Releases](../../releases) contain
  only the engine — the `port/` layer plus the compiled decompilation, with no
  game data of any kind — bundled with permissively-licensed runtime libraries
  (SDL2, zlib, the MinGW runtime; their licenses travel in the download). Any
  build — yours or ours — is useless without a ROM you supply.

This project is **not affiliated with, endorsed by, or sponsored by** Nintendo,
Rare, Microsoft, MGM, Danjaq, EON Productions, or any rights holder in
GoldenEye or James Bond. If you are a rights holder with a concern, open an
issue and it will be addressed.

## License

The original work in this repository — the port layer (`port/`), the PC build
system, `tools_pc/`, and the documentation — is released under the MIT License,
see [`LICENSE`](LICENSE). Everything inherited from the upstream decompilation
is covered by [`NOTICE`](NOTICE), not by that license.
