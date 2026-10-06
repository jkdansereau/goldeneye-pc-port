# GoldenEye 007 PC Port

[![CI](https://github.com/jkdansereau/goldeneye-pc-port/actions/workflows/ci.yml/badge.svg)](https://github.com/jkdansereau/goldeneye-pc-port/actions/workflows/ci.yml)
![license](https://img.shields.io/badge/license-MIT-ffb454)

<p align="center"><em>GoldenEye 007 (Nintendo 64, 1997) on the PC —
decompiled, ported, and playable at 60 fps.</em></p>

[Download](#download) · [News](#news) · [Status](#status) · [Roadmap](#roadmap) · [Building](#building) · [Docs](#documentation) · [Legal](#legal)

A native PC port of _GoldenEye 007_ (Rare, 1997, Nintendo 64), compiled from
the [GoldenEye 007 decompilation](https://github.com/n64decomp/007): the
original N64 game running from reconstructed source, not the Xbox 360
remaster. The N64's graphics coprocessor (RSP) is emulated in software; every
other hardware surface (video, audio, input, timers, save storage) is shimmed
in a dedicated `port/` layer, following the architecture of the
[Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark), the same
Rare "Indy" engine family, one hardware generation apart.

**v0.4.0** (released 2026-09-28) is out for Windows and Linux (including
Steam Deck, where the Linux bundle sideloads as-is). It is the most complete
release to date: the full campaign runs at a steady 60 fps with the known
issues now few and mostly cosmetic ([Status](#status)). Free to download,
build on and modify (you bring the ROM).

**This is a pre-1.0 release, not a finished product.** v1.0 is the target
for a polished, feature-complete build; until then, expect missing features
and the occasional breaking change between versions. See [Status](#status)
for what works today and [Roadmap](#roadmap) for where this is headed.

**AI disclosure:** development here was agentic - Claude Pro plus a local
open-weight model on a single RTX 5090, as of August–September 2026. This
project is as much a study of *that process* as it is a port: whether
current LLMs can carry a codebase like this, and what actually goes wrong
along the way. Judge the result for yourself. I'm one person doing this in my
spare time, not a team. See [Background](#background) for the full setup,
timeline, and an honest account of what worked and what didn't.

> [!IMPORTANT]
> **You must supply your own GoldenEye 007 ROM.** This repository contains no
> Nintendo code or assets, and no ROM. Nothing here is distributable as a
> playable game: see [Requirements](#requirements) and [Legal](#legal).

<p align="center">
  <img src="docs/media/goldeneye-gh-preview.gif" width="64%"
       alt="~24 s gameplay montage from live play sessions">
  <br><em>All in-engine, running in the port — a ~24&nbsp;s gameplay montage
  from live v0.4.0 play sessions, opening on the Runway tank ·
  <a href="docs/index.md">12 level stills in the project index</a></em>
</p>

---

## News

- **2026-09-28** — **v0.4.0**: native widescreen, a complete aim system for
  mouse and controller, in-game key rebinding, crosshair customization,
  rumble-pak haptics, a rebuilt options overlay, and a broad fidelity-fix
  pass (water, particles, billboard trees, front-end logo, gunshot SFX, a
  true stable 60 fps). [Release notes](https://github.com/jkdansereau/goldeneye-pc-port/releases/tag/v0.4.0) ·
  [downloads](#download).
- **2026-09-20** — **v0.3.0**: the first release with the complete campaign
  playable end to end at 60 fps on Windows, Linux, and Steam Deck.
  [Release notes](https://github.com/jkdansereau/goldeneye-pc-port/releases/tag/v0.3.0).
- **2026-09-04 → 2026-09-16** — **v0.1.0 – v0.2.2**: the alpha and beta
  cycle — build chain, software RSP, first rendered frames, front end, and
  per-level stabilization across the campaign.

---

## Download

| Platform | Bundle | Notes |
|---|---|---|
| **Windows** (x86_64) | [win64.zip](https://github.com/jkdansereau/goldeneye-pc-port/releases) | Engine + runtime DLLs + the one-time asset tool. |
| **Linux** (x86_64) / **Steam Deck** | [linux tarball](https://github.com/jkdansereau/goldeneye-pc-port/releases) | SDL2 is bundled, so it runs as-is on any distro, and sideloads onto a Deck with nothing installed. |

Both bundles contain **no ROM and no game assets**: you supply your own
(see [Requirements](#requirements)), which keeps the release legal to
distribute. Earlier builds: v0.3.0, v0.2.2, v0.2.1, v0.2.0, and v0.1.0
alpha, same page. You can also build it yourself; see [Building](#building).

### Quick start

You need a GoldenEye 007 N64 ROM (`.z64`, big-endian). This release supports
the **NTSC-U (US)** version; PAL and JP are on the roadmap ([issue
#85](https://github.com/jkdansereau/goldeneye-pc-port/issues/85)). No ROM or game asset is
included or distributed. Then:

1. Download the Windows or Linux bundle from [Releases](https://github.com/jkdansereau/goldeneye-pc-port/releases) and unpack it.
2. Make a `data/` folder next to the executable and drop the ROM in as `ge007.ntsc-final.z64`.
3. Launch the executable from that folder. The first run takes a few extra seconds: it detects the ROM and generates the derived asset folders once (no Python or other tooling needed).

Read the [Status](#status) section first: the small number of known issues
in this release are listed plainly there.

---

## Status

**v0.4.0 - fully playable, with a small set of known caveats.** The full
single-player campaign is completable end to end (all 20 missions, Agent
difficulty, playtested), at a steady 60 fps; all 20 solo missions — plus
the end-of-campaign credits sequence — load, render and run crash-free,
verified on Windows, Linux and real Steam Deck hardware. The most common
defect classes from earlier releases — particle colour drift, water seams,
z-fighting, billboard trees, muzzle flashes, the front-end Nintendo logo,
gunshot SFX — are fixed in this version; what remains is a short list,
below. Feedback is very welcome.

What a release actually installs (no networking, no telemetry, no ROM or
game assets shipped) and how faithfully the port tracks the original N64
game's logic: [Security & fidelity status](docs/security-and-fidelity-status.md).

**Working:** boot sequence and front end (menu → mission select → briefing →
start), front-end menu navigation on the left stick to match the F10 overlay
(D282); all 20 solo missions load, render and are crash-free (full campaign
playtested end to end at Agent difficulty, including the ending sequence); steady 60 fps
(software RSP off the presentation critical path); full audio: in-level music and SFX; keyboard + mouse (click-to-lock, an aim style of your
choice — N64 or centred FPS-style — with per-device sensitivity) and a modern
dual-stick controller layout (use/reload/weapon-cycle on A/X/Y, rumble-pak
vibration on supported pads); native widescreen at any aspect ratio
(undistorted world, HUD anchored to the screen edges, 4:3 menus pillarboxed);
in-game key rebinding (keyboard/mouse) with a GEPD-style default layout;
crosshair customization (opt-in); Bond is fixed in cutscenes (no more
floating or spin-glitching) and his third-person model positioning generally
is right the large majority of the time now, at most a small drift when off; file-backed saves; faithful N64 progression
by default (F10 → *All unlocked* opens every level, 007 mode and the full
cheat menu); F10 in-game options overlay (video, input, gameplay, HUD,
graphics, audio; frame cap, MSAA, filtering, FOV, sensitivity, key rebinding,
crosshair, vibration); Windows and
Linux, including Steam Deck.

**Known issues:** see the **[known-issues table](docs/ROADMAP.md#known-issues)**
(what you'll notice, impact, workarounds). The short version: no multiplayer
yet, NTSC-U ROMs only, no macOS/ARM builds, and a couple of cosmetic defects.

> **Before first using `All unlocked`, back up `data/ge007.eep`.** Saves made
> with it on no longer write the fake unlocks to disk (D387), but a save that an
> earlier build already polluted stays polluted, and cheats you earn while it is
> on are not kept. It applies at the next launch.

### Steam Deck

The Linux bundle is the Deck build. SFTP it over from your PC, or download
it straight from the [releases page](https://github.com/jkdansereau/goldeneye-pc-port/releases) on the Deck itself:
extract, drop your ROM in `data/`, launch it once (the first run generates the
derived assets), and add the executable as a non-Steam game. SDL2 is bundled, so no dependencies need
installing. The port detects a Steam Deck by its hardware id (Linux DMI, or
`STEAMOS`) and, once per `ge007.ini`, applies Deck-friendly display defaults:
native 1280×800 fullscreen. VSync, 2× MSAA and 250% draw/LOD distance are
already the port's defaults (the N64-authored fade distances read short on the
close-up 7" panel). The preset only applies while the display settings are still
untouched, so an ini created earlier (for example by a Desktop Mode test launch)
no longer skips it for good (D283); everything is changeable in the options
overlay and persists afterwards. If your resolution isn't 1280×800 on first
boot, just set it manually: F10 → *Resolution*. The
renderer is CPU-bound (software RSP); expect original N64-era performance at
60 fps rather than more. This release was playtested on real Deck hardware;
the v0.1.0-era Facility crash (D203) did not recur: its root cause was
identified and fixed (D253), and a separate intermittent SIGSEGV in heavy
firefights/terminal destruction (D255) and an audio-thread crash (D305) are
both fixed and live-verified on real hardware as of this release.

**In-game settings on the Deck.** The options overlay is fully gamepad-driven:
it opens with **Select**, the D-pad or left stick (up/down) moves between
options, **A** steps the selected option forward, **B** steps it back, and
**Start** (or Select again) closes. Toggles flip, resolution / MSAA /
filtering cycle, sliders step in increments. With a keyboard attached the same
overlay is `F10` + arrows/Enter.

---

## Roadmap

The goal is a **feature-complete, faithful** port: every mode and region the
N64 cartridge ships, behaving 1:1 with the original. The full list of open
items, and which ones are waiting on a decision, lives in one place:
[`docs/ROADMAP.md`](docs/ROADMAP.md). In brief:

- **Split-screen multiplayer** (2–4 players), the largest missing N64 feature
  ([#99](https://github.com/jkdansereau/goldeneye-pc-port/issues/99)).
- **PAL and JP ROM support**. NTSC-U is the only supported region today.
- The remaining small accuracy differences (audio timbre, a few cosmetic
  rendering details).
- **macOS/ARM builds**.
- **Ongoing security/fidelity review**: a review pass after each release to keep
  the [security policy](.github/SECURITY.md) and the known-issues list current,
  moving the mechanical parts (dependency/secret scanning) toward automation.

This is spare-time work, so there's no timeline. Opt-in extras beyond the N64
game (LAN play, bots, HD textures) come after feature-complete, if at all.
Not currently planned: co-op or other modes GE never shipped, online
multiplayer, ray tracing. If any of these matter to you, open an issue — it
helps prioritize.

---

## Beyond playing

- **Tweak it**: `ge007.ini` and the F10 in-game overlay expose resolution,
  frame cap, MSAA, texture filtering, FOV/draw distance, mouse feel, key
  rebinding, crosshair and vibration; launch with `-fresh` for a clean-slate
  run.
- **Read it**: [`docs/internals.md`](docs/internals.md) maps the
  architecture and the software RSP; [`docs/porting-notes.md`](docs/porting-notes.md)
  is the catalogue of N64→PC bug classes hit along the way. Game logic in
  `src/` is unmodified decompilation; every hardware surface lives in the
  MIT-licensed `port/` layer.
- **Mod it**: the port layer, build system and `tools_pc/` are yours to
  extend (see [License](#license)); [`CONTRIBUTING.md`](CONTRIBUTING.md) has
  the ground rules for getting changes in, and [`docs/dev/`](docs/dev/) is
  the raw engineering record behind every fix.

---

## Background

The port was built by two coding agents, a local open-weight model
(`unsloth/Qwen3.8-27B-GGUF` on one RTX 5090, via the [pi](https://pi.dev/)
agent) doing the groundwork (build, boot chain, software-RSP integration,
asset pipeline, first frames), and **Claude Code** (Sonnet 5, Opus 5 for the
hardest bugs) joining for the collaborative phase (the 21-level sweep, the
ABI finding catalog, SDL input, front end), handing work back and forth
through shared written notes, directed by one person part-time. In short:
43 days (16 Aug – 28 Sep), 1005 commits, 286 findings root-caused and logged
(`D1`–`D408`).

The full write-up (timeline, handoff mechanism, effort breakdown, an honest
"what worked / what didn't"): [`docs/dev/agentic-development.md`](docs/dev/agentic-development.md).
The workflow itself: [`docs/dev-process.md`](docs/dev-process.md). To cite the
project or its findings: [`CITATION.cff`](CITATION.cff) (GitHub's "Cite this
repository" menu).

---

## How this differs from the other GoldenEye PC projects

This is a native port of the original 1997 Nintendo 64 game, built from its
actual reconstructed source code, the same lineage as the Perfect Dark PC
port. The other well-known "GoldenEye on PC" projects are something
different: they machine-translate the shipped binary of the *unreleased Xbox
360 XBLA remaster*, a different game on a different codebase with no shared
code with this one.

| | This project | The other well-known "GoldenEye on PC" recompilation projects (and their Steam Deck builds) |
|---|---|---|
| **What it ports** | The original **Nintendo 64** game (1997) | The **Xbox 360 XBLA** HD remaster (built ~2007, never released) |
| **How** | Decompilation-based source port: human-reconstructed C, compiled for the host; game logic runs as written | Static binary recompilation: the shipped machine code is auto-translated to C; no source-level understanding |
| **Lineage** | [GoldenEye 007 decompilation](https://github.com/n64decomp/007) + [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark) engine family | Xbox 360 "…Recompiled" static-recompilation family |
| **Renderer** | Software RSP → OpenGL | Hardware (Vulkan) |
| **Status** | v0.4.0 public release; full campaign playable at 60 fps (see [Status](#status)) | Playable full game |
| **Why it exists** | To run the *original* N64 game from source, and as a [case study in AI-agent collaboration](#background) on a hard low-level codebase | To get a playable PC release of the remaster |

They answer a different question: how to get the *remaster* onto PC by
machine translation. This project answers how to get the *original 1997
game* onto PC, running from its reconstructed source.

---

## Requirements

You need a GoldenEye 007 (Nintendo 64) ROM that you legally own, in
big-endian (`.z64`) format, matching one of:

| Region | ROMID | ROM filename (in `data/`) | SHA-1 |
|--------|-------|---------------------------|-------|
| NTSC-U (US)  | `ntsc-final` | `ge007.ntsc-final.z64` | `abe01e4aeb033b6c0836819f549c791b26cfde83` |
| PAL (EU)     | `pal-final`  | `ge007.pal-final.z64`  | `167c3c433dec1f1eb921736f7d53fac8cb45ee31` |
| NTSC-J (JP)  | `jpn-final`  | `ge007.jpn-final.z64`  | `2a5dade32f7fad6c73c659d2026994632c1b3174` |

This release supports the US (NTSC-U) version; PAL and JP ROMs are
recognised by region but not yet supported (on the roadmap, [issue
#85](https://github.com/jkdansereau/goldeneye-pc-port/issues/85)).

The port also relies on the decompilation's asset-extraction step, which pulls
the level, model, texture and music data out of your ROM at build time. That
step, too, requires your ROM and is part of [Building](#building).

---

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
./build-pc.sh ntsc-final         # NTSC-U; PAL/JP engine builds compile, but their asset sidecars are not yet producible (issue #85)
```

### Linux

The Linux build is compiled on every push by CI (ubuntu-24.04); the release
bundle additionally bundles SDL2 so no system packages are needed at runtime.

```sh
sudo apt install build-essential cmake python3 libsdl2-dev zlib1g-dev libgl1-mesa-dev
git clone https://github.com/jkdansereau/goldeneye-pc-port.git
cd goldeneye-pc-port
# extract assets (docs/building.md), then:
./build-pc.sh ntsc-final
```

The executable is written to `build-pc/ge007.x86_64` (on Windows,
`build-pc/ge007.x86_64.exe`).

---

## Running

1. Create a `data/` directory in the repo root.
2. Put your ROM in it, named as in the table above
   (e.g. `data/ge007.ntsc-final.z64`).
3. Run the executable from the repo root:
   `./build-pc/ge007.x86_64`.

Configuration is written to `ge007.ini` on first run; game progress
lives in `ge007.eep`. Launch with `-fresh` to wipe both before starting
(a clean-slate run for playtesting).

### Default controls

| Action              | Keyboard / mouse         | Controller    |
|---------------------|--------------------------|---------------|
| Move / strafe       | `W` `A` `S` `D` / arrows  | Left stick (or D-pad) |
| Aim / look          | Mouse                    | Right stick   |
| Fire                | Left mouse               | Right trigger |
| Aim mode            | Right mouse / `LShift`   | Left trigger / LB |
| Use / interact      | `E`                       | A             |
| Reload              | `R`                       | X             |
| Crouch              | `LCtrl`                   | Left or right stick click |
| Cycle owned gadgets | Watch inventory          | B             |
| Next weapon         | Mouse wheel down / `Q`   | Y             |
| Previous weapon     | Mouse wheel up           | —             |
| Start               | `Enter` / `Tab`          | Start         |
| Options overlay     | `F10`                    | Select (A toggles/steps; B backs; D-pad/stick adjusts sliders; Start closes) |

The default layout is the GEPD-style preset on the keyboard and the standard
dual-stick scheme on controllers (left stick move, right stick look,
triggers fire/aim, A use, X reload, B gadget cycle, Y weapon cycle, stick
click crouch). The watch gadget cycle selects the next owned gadget in the
N64 inventory. The controller layout matches Rare's Xbox 1.1 (Jinx) button
roles (user-tested on a physical pad; D394), and the remaining Xbox schemes
(1.2 Christmas, 1.3 Frost, 1.4 Elektra) are planned as selectable presets
and not yet available. Keyboard → Bindings edits **keyboard/mouse only**.
Controller buttons are rebindable per controller under *Input → Controller*
(F10 overlay or the front-end PC Options): pick *Custom* and bind each
action by pressing a pad button (tap B or Back to cancel, hold Back to
clear, hold B to bind B). Menus always keep A/X accept and B/Y cancel.

Mouse sensitivity, Y-inversion and the aim/turn split are tunable in the
F10 overlay (*Keyboard → Sensitivity*) or the `[Input]` section of
`ge007.ini`.

---

## How it works

The R4300 game code in `src/` is compiled completely unmodified; the
decompilation's control flow is ground truth. Everything that would touch N64
hardware is redirected into `port/`: a **software RSP** (`port/fast3d/`,
adapted from the Perfect Dark port) that interprets the GBI display list the
game builds each frame and emits OpenGL, bypassing the RDP; libultra OS shims
(`port/src/libultra.c`, game threads as real host threads) that let the
game's own scheduler run and drive the software RSP inline; and SDL2/OpenGL/filesystem backends
for video, audio, input and storage. The 32→64-bit transition forces a small,
cataloged class of mechanical ABI-only edits to ROM-serialized structs
(pointer-width reconciliation); these change no behavior and are documented
individually. Where it diverges from the Perfect Dark port: GoldenEye's N64
serialized asset formats are converted offline by Python "sidecar" converters
in `tools_pc/` rather than fixed up at load time. Full detail:
[`docs/internals.md`](docs/internals.md) and
[`docs/porting-notes.md`](docs/porting-notes.md).

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

---

## Documentation

Key docs are also published as a site:
<https://jkdansereau.github.io/goldeneye-pc-port/>.

| Doc | What's in it |
|---|---|
| [`docs/building.md`](docs/building.md) | Full build + asset-extraction guide. |
| [`docs/internals.md`](docs/internals.md) | Architecture, the RSP-emulation approach, GE-vs-PD engine differences, the phased plan. |
| [`docs/porting-notes.md`](docs/porting-notes.md) | The recurring N64→PC bug classes hit during the port, with fixes. |
| [`docs/dev/agentic-development.md`](docs/dev/agentic-development.md) | The research angle: the two-agent setup, timeline, handoff workflow, and an assessment of what did and didn't work. |
| [`docs/dev-process.md`](docs/dev-process.md) | The investigation workflow in detail: budgets, file partitioning, the finding-log discipline. |
| [`docs/dev/`](docs/dev/) | The raw engineering record: the full finding log, per-level status, graphics backlog, playtest matrices, and [`docs/dev/game-behavior-reference.md`](docs/dev/game-behavior-reference.md) (a secondary-sourced playtest reference for how the retail game is meant to behave; repo-only, code is ground truth). |
| [`docs/SetupGuide.md`](docs/SetupGuide.md), [`docs/StyleGuide.md`](docs/StyleGuide.md) | Inherited from the decompilation this repo forks. |

---

## Credits

This port is a thin layer on a large amount of other people's work.

**Prior work it is built on**

- The [GoldenEye 007 decompilation](https://github.com/n64decomp/007), years of
  effort by Larry Ficken ("kholdfuzion") and the project's contributors; plus
  zoinkity's GoldenEye documentation, which the decomp started from. This port
  is a fork of that repository.
- The [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark)
  (Ryan Dwyer and contributors), the reference architecture for this port and
  the source of the `fast3d` software RSP.
- The [Perfect Dark decompilation](https://github.com/n64decomp/perfect_dark),
  the sibling decomp the PD port is built on.
- **Carnivorous**: author of the *Mouse Injector* input plugin for 1964 (the
  "GEPD Edition" bundle). Its mouse-aim behaviour for GoldenEye/Perfect Dark is
  what the in-game GEPD-style aim mode is modelled on; the implementation here
  is an independent reimplementation of that behaviour, not derived code.
  Thanks also to **Rice** and **schibo** of the 1964 team for the emulator it
  shipped with.

**Vendored / adapted code**

- `port/fast3d/`: the software RSP, adapted from the PD port. It originates
  with the [Ship of Harkinian](https://github.com/HarbourMasters) /
  libultraship fast3d (© Emill, MaikelChan; MIT, see `port/fast3d/LICENSE.txt`),
  which itself descends from
  [sm64-port](https://github.com/sm64-port/sm64-port)'s fast3d and audio mixer.
- `port/fast3d/glad/`: OpenGL loader generated by
  [glad](https://github.com/Dav1dde/glad) (David Herberth, MIT).
- The decompilation toolchain: [`ido-static-recomp`](https://github.com/decompals/ido-static-recomp)
  (Emill / decompals), [`qemu-irix`](https://github.com/n64decomp/qemu-irix) and
  `rabbitizer` (n64decomp).
- [SDL2](https://libsdl.org) and [zlib](https://zlib.net).

**Methodology**

- Chris Lewis, [*"Decompiling a Nintendo 64 Game in 84 Days"*](https://blog.chrislewis.au/decompiling-a-nintendo-64-game-in-84-days/) (the Snowboard
  Kids decompilation write-up), the agent-workflow practices in
  [`docs/dev-process.md`](docs/dev-process.md) are adapted from it.

**Consultation**

- **f1zz1ec0ke** ([GitHub](https://github.com/f1zz1ec0ke)): LLM consultation on model
  tuning, agent harnesses, and agentic strategy throughout the port's development.

**Tools and models used to develop the port**

- Qwen 3.8 (Alibaba Qwen team), run locally from the
  [`unsloth/Qwen3.8-27B-GGUF`](https://huggingface.co/unsloth/Qwen3.8-27B-GGUF)
  repo — `UD-Q4_K_XL` as the primary quant, with other quants used along the
  way; [Unsloth](https://unsloth.ai) (the GGUF quantisation and Unsloth
  Desktop) as the local model server.
- [pi](https://pi.dev/): the local coding-agent harness.
- [Claude / Claude Code](https://claude.com/claude-code) (Anthropic).

---

## Legal

This is a non-commercial fan preservation/research project, in the same
category as the many other N64 decompilation and native-port repositories on
GitHub. It follows the same conventions they do:

- **No ROM and no game assets are distributed**: not in this repository and
  not in any release. Textures, audio, models, level data and in-game text are
  extracted from a ROM *you already own*, on *your* machine, at build time.
- The repository is a fork of the public
  [GoldenEye 007 decompilation](https://github.com/n64decomp/007) and inherits
  its contents unmodified (see [`NOTICE`](NOTICE) for what that includes).
- No official logos, box art, or marketing assets are used. "GoldenEye 007",
  "007", "James Bond" and related marks belong to their respective owners
  (Nintendo, Microsoft/Rare, MGM, Danjaq, EON Productions).
- Pre-built binaries published under [Releases](https://github.com/jkdansereau/goldeneye-pc-port/releases) contain
  only the engine (the `port/` layer plus the compiled decompilation, with no
  game data of any kind), bundled with permissively-licensed runtime libraries
  (SDL2, zlib, the MinGW runtime; their licenses travel in the download). Any
  build, yours or ours, is useless without a ROM you supply.

This project is **not affiliated with, endorsed by, or sponsored by** Nintendo,
Rare, Microsoft, MGM, Danjaq, EON Productions, or any rights holder in
GoldenEye or James Bond. If you are a rights holder with a concern, open an
issue and it will be addressed.

## License

The original work in this repository, the port layer (`port/`), the PC build
system, `tools_pc/`, and the documentation, is released under the MIT License;
see [`LICENSE`](LICENSE). Everything inherited from the upstream decompilation
is covered by [`NOTICE`](NOTICE), not by that license.

---

*Last updated 2026-09-28 — v0.4.0 · <a href="https://github.com/jkdansereau/goldeneye-pc-port">GitHub</a> · <a href="https://github.com/jkdansereau/goldeneye-pc-port/releases">Releases</a>*
