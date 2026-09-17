# GoldenEye 007 PC Port

[![CI](https://github.com/jkdansereau/goldeneye-pc-port/actions/workflows/ci.yml/badge.svg)](https://github.com/jkdansereau/goldeneye-pc-port/actions/workflows/ci.yml)
![license](https://img.shields.io/badge/license-MIT-green)

A native PC port of _GoldenEye 007_ (Rare, 1997, Nintendo 64), compiled from
the [GoldenEye 007 decompilation](https://github.com/n64decomp/007): the
original N64 game running from reconstructed source, not the Xbox 360
remaster. The N64's graphics coprocessor (RSP) is emulated in software; every
other hardware surface (video, audio, input, timers, save storage) is shimmed
in a dedicated `port/` layer, following the architecture of the
[Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark), the same
Rare "Indy" engine family, one hardware generation apart.

**v0.2.2** is out for Windows and Linux (including Steam Deck, where the
Linux bundle sideloads as-is) and runs the full campaign at a steady 60 fps
with known rough edges ([Status](#status)). Free to download, build on and
modify (you bring the ROM).

**This is a pre-1.0 release, not a finished product.** v1.0 is the target
for a polished, feature-complete build; until then, expect rough edges,
missing features, and breaking changes between versions. See
[Status](#status) for what works today and [Roadmap](#roadmap) for where
this is headed.

**AI disclosure:** development here was agentic - Claude Pro plus a local
open-weight model on a single RTX 5090, as of August–September 2026. This
project is as much a study of *that process* as it is a port: whether
current LLMs can carry a codebase like this, and what actually goes wrong
along the way. Judge the result for yourself.
I'm one person doing this in my spare time, not a team. See
[Background](#background) for the full setup, timeline, and an honest
account of what worked and what didn't.

> [!IMPORTANT]
> **You must supply your own GoldenEye 007 ROM.** This repository contains no
> Nintendo code or assets, and no ROM. Nothing here is distributable as a
> playable game: see [Requirements](#requirements) and [Legal](#legal).

<p align="center">
  <img src="docs/media/goldeneye-gh-preview.gif" width="64%"
       alt="~32 s gameplay montage from live play sessions">
  <br><em>All in-engine, running in the port, a ~32&nbsp;s gameplay montage loop
  from the v0.2.0 playtest.</em>
</p>

## Download

| Platform | Bundle | Notes |
|---|---|---|
| **Windows** (x86_64) | [win64.zip](https://github.com/jkdansereau/goldeneye-pc-port/releases) | Engine + runtime DLLs + the one-time asset tool. |
| **Linux** (x86_64) / **Steam Deck** | [linux tarball](https://github.com/jkdansereau/goldeneye-pc-port/releases) | SDL2 is bundled, so it runs as-is on any distro, and sideloads onto a Deck with nothing installed. |

Both bundles contain **no ROM and no game assets**: you supply your own
(see [Requirements](#requirements)), which keeps the release legal to
distribute. Earlier builds: v0.2.1, v0.2.0 and v0.1.0 alpha, same page. You can also build it
yourself; see [Building](#building).

### Quick start

You need a GoldenEye 007 N64 ROM (`.z64`, big-endian). This release supports
the **NTSC-U (US)** version; PAL and JP are on the roadmap ([issue
#85](https://github.com/jkdansereau/goldeneye-pc-port/issues/85)). No ROM or game asset is
included or distributed. Then:

1. Download the Windows or Linux bundle from [Releases](https://github.com/jkdansereau/goldeneye-pc-port/releases) and unpack it.
2. Make a `data/` folder next to the executable and drop the ROM in as `ge007.ntsc-final.z64`.
3. Launch the executable from that folder. The first run takes a few extra seconds: it detects the ROM and generates the derived asset folders once (no Python or other tooling needed).

Read the [Status](#status) caveats first: v0.2.2 has known rough edges,
listed plainly there.

## Status

**v0.2.2 - playable, with known rough edges.** The full single-player
campaign is completable end to end (all 21 missions, Agent difficulty,
playtested), at a steady 60 fps; all 21 solo missions load, render and run
crash-free, verified on Windows, Linux and real Steam Deck hardware. Native
Intel macOS builds are supported. Feedback is very welcome.

**Working:** boot sequence and front end (menu → mission select → briefing →
start), front-end menu navigation on the left stick to match the F10 overlay
(D282); all 21 solo missions load, render and are crash-free (full campaign
playtested end to end at Agent difficulty); steady 60 fps
(software RSP off the presentation critical path); full audio: in-level music and SFX; keyboard + mouse (click-to-lock, proportional aim mode) and a modern
dual-stick controller layout; file-backed saves; faithful N64 progression by default
(F10 → *All unlocked* opens every level, 007 mode and the full cheat menu); F10 in-game
options overlay (resolution, frame cap, MSAA, filtering, FOV, sensitivity,
quit to desktop);
Windows, Linux, and Intel macOS.

**Known issues:**

- **Cutscenes still glitch, mostly with James Bond**: in scripted sequences
  Bond is the one who gets misplaced, hovers, or spins; the other actors are
  fine for the most part now. The Dam level-end cutscene is racy (D243).
  Still the most visible gap in this release.
- Particle colours drift through a rainbow palette instead of holding their
  grey/orange intent (bullet sparks, lingering smoke/explosion residue) (D252).
- Water levels show a moving seam between two water patterns (D245); thin
  pixel strips at the left/right screen edges at non-integer window scales
  (D246).
- Some front-end 3D models are off: the spinning Nintendo logo renders as two
  white blobs and the Rareware logo's texture filtering looks wrong (D75).
- The F10 overlay's bottom row duplicates whatever item is currently selected
  (D251).
- Surface 1's 2D billboard trees render as a solid wall of tree texture
  instead of discrete sprites (D236). Under active investigation.
- No true widescreen: 16:9 stretches the 4:3-authored view (world + HUD)
  rather than properly expanding the horizontal FOV; the F10 *FOV scale %*
  slider is a manual workaround, not real widescreen.
- Distant geometry can drop out on the biggest open levels (Streets,
  Egyptian) at default FOV — a culling/LOD issue that sometimes
  self-corrects as you keep moving (D249).
- Gunshot SFX can sound off during sustained/rapid fire: cadence can drift
  from the N64 original's rate, and PP7/AK47 fire can occasionally go silent
  under heavy automatic fire near another looping sound (D240/D241).
- **`All unlocked` is highly experimental — don't enable it until you have
  at least one save written** (complete a level normally first, e.g. Dam on
  Agent). Enabling it on a brand-new install with no prior save can still
  cause silent audio and odd right-mouse-aim behavior (D257/D259/D281).
- ~~Linux / Steam Deck: intermittent SIGSEGV when destroying objects
  (terminals, exploding grenades)~~ **FIXED (D255)** — a mistyped 4-byte-enum
  field truncated a 64-bit pointer; verified live on real Steam Deck
  hardware. A second, separate Steam Deck/Linux crash during
  explosion-heavy combat is also fixed (D285, an unlocked audio-thread
  list read racing a concurrent write) — live-tested on real Steam Deck
  hardware with sustained, dense overlapping-explosion combat, no crash.
  Please still report any remaining Deck/Linux crashes; current builds
  capture full faulting registers in `ge007.crash.log`.
- Assorted further cosmetic defects are tracked in
  [`docs/dev/GRAPHICS-BACKLOG.md`](docs/dev/GRAPHICS-BACKLOG.md).
- No ARM support; no controller rebinding UI.

Root causes and fix status for every item: the [release notes](https://github.com/jkdansereau/goldeneye-pc-port/releases)
and the finding log in [`docs/dev/findings.md`](docs/dev/findings.md).

### Steam Deck

The Linux bundle is the Deck build. SFTP it over from your PC, or download
it straight from the [releases page](https://github.com/jkdansereau/goldeneye-pc-port/releases) on the Deck itself:
unzip, drop your ROM in `data/`, launch it once (the first run generates the
derived assets), and add the executable as a non-Steam game. SDL2 is bundled, so no dependencies need
installing. On SteamOS the first launch seeds `ge007.ini` with Deck-friendly
defaults: native 1280×800 fullscreen, VSync, MSAA 4, and 150% draw/LOD
distance (the authored N64 fade distances read short on the close-up panel);
everything is changeable in the options overlay and persists afterwards.
**Do that first launch in Game Mode, not Desktop Mode** — an ini created by
an earlier Desktop Mode launch (e.g. while testing before adding it as a
Steam shortcut) permanently skips the Deck preset, since any existing ini
always wins over it (D283). If your resolution isn't 1280×800 on first
Game Mode boot, just set it manually: F10 → *Resolution*. The renderer is
CPU-bound (software RSP); expect original N64-era
performance at 60 fps rather than more. This release was playtested on real
Deck hardware; the v0.1.0-era Facility crash (D203) did not recur: its root
cause was identified and fixed (D253), verified on the Deck; one intermittent
SIGSEGV in heavy firefights remains open (D255).

**In-game settings on the Deck.** The options overlay is fully gamepad-driven:
it opens with **Select**, the D-pad or left stick (up/down) moves between
options, **A** steps the selected option forward, **B** steps it back, and
**Start** (or Select again) closes. Toggles flip, resolution / MSAA /
filtering cycle, sliders step in increments. With a keyboard attached the same
overlay is `F10` + arrows/Enter.

## Roadmap

No fixed timeline or committed feature list — this is spare-time work — but
directionally, on the way to v1.0:

- Working through the [known issues](#status) above and the fuller list in
  [`docs/dev/findings.md`](docs/dev/findings.md).
- **PAL and JP ROM support** ([issue #85](https://github.com/jkdansereau/goldeneye-pc-port/issues/85)); NTSC-U is the only supported region today.
- **Real widescreen** (properly expanding the field of view at 16:9, rather
  than today's 4:3-stretch).
- **Controller rebinding UI**, and ARM builds.
- **LAN multiplayer**: reviving GoldenEye's original split-screen/deathmatch
  netplay across multiple PCs on a local network. Genuinely under
  consideration, but early and not started; no ETA.
- General polish: performance, remaining rendering/audio defects, save/config
  robustness.

Not currently planned: new game modes GE never shipped (e.g. co-op), online (non-LAN)
multiplayer, ray tracing. If any of these matter to you, open an issue —
it helps prioritize.

## Beyond playing

- **Tweak it**: `ge007.ini` and the F10 in-game overlay expose resolution,
  frame cap, MSAA, texture filtering, FOV/draw distance and mouse feel;
  launch with `-fresh` for a clean-slate run.
- **Read it**: [`docs/internals.md`](docs/internals.md) maps the
  architecture and the software RSP; [`docs/porting-notes.md`](docs/porting-notes.md)
  is the catalogue of N64→PC bug classes hit along the way. Game logic in
  `src/` is unmodified decompilation; every hardware surface lives in the
  MIT-licensed `port/` layer.
- **Mod it**: the port layer, build system and `tools_pc/` are yours to
  extend (see [License](#license)); [`CONTRIBUTING.md`](CONTRIBUTING.md) has
  the ground rules for getting changes in, and [`docs/dev/`](docs/dev/) is
  the raw engineering record behind every fix.

## Background

The port was built by two coding agents, a local open-weight model
(`unsloth/Qwen3.8-27B-GGUF` on one RTX 5090, via the [pi](https://pi.dev/)
agent) doing the groundwork (build, boot chain, software-RSP integration,
asset pipeline, first frames), and **Claude Code** (Sonnet 5, Opus 5 for the
hardest bugs) joining for the collaborative phase (the 21-level sweep, the
ABI finding catalog, SDL input, front end), handing work back and forth
through shared written notes, directed by one person part-time. In short:
~3.5 weeks, ~430 commits, 200+ bugs logged and tracked to resolution.

The full write-up (timeline, handoff mechanism, effort breakdown, an honest
"what worked / what didn't"): [`docs/dev/agentic-development.md`](docs/dev/agentic-development.md).
The workflow itself: [`docs/dev-process.md`](docs/dev-process.md). To cite the
project or its findings: [`CITATION.cff`](CITATION.cff) (GitHub's "Cite this
repository" menu).

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
| **Status** | v0.2.2 public release; full campaign playable at 60 fps (see [Status](#status)) | Playable full game with online multiplayer |
| **Why it exists** | To run the *original* N64 game from source, and as a [case study in AI-agent collaboration](#background) on a hard low-level codebase | To get a playable PC release of the remaster |

They answer a different question: how to get the *remaster* onto PC by
machine translation. This project answers how to get the *original 1997
game* onto PC, running from its reconstructed source.

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
| Fire (Z)            | Left mouse / `LCtrl`     | Right trigger |
| Aim mode (R)        | Right mouse / `LShift`   | Left trigger  |
| Use / accept (A)    | `Space` / `E` / `X`      | A / X         |
| Reload / cancel (B) | `R` / `F`                | B / Y         |
| Next weapon         | Mouse wheel up           | RB            |
| Previous weapon     | Mouse wheel down         | LB            |
| Start               | `Enter` / `Tab`          | Start         |
| Options overlay     | `F10`                    | Select (D-pad/stick + A/B navigate, Start closes) |

The controller layout follows the modern dual-stick scheme used by the
console re-releases (left stick move, right stick look, triggers fire/aim,
shoulders cycle weapons).

Mouse sensitivity, Y-inversion and the aim/turn split are tunable in the
`[Input]` section of `ge007.ini`.

## How it works

The R4300 game code in `src/` is compiled completely unmodified; the
decompilation's control flow is ground truth. Everything that would touch N64
hardware is redirected into `port/`: a **software RSP** (`port/fast3d/`,
adapted from the Perfect Dark port) that interprets the GBI display list the
game builds each frame and emits OpenGL, bypassing the RDP; a scheduler
replacement (`port/src/gesched.c`) that drives it directly; single-threaded
libultra OS shims (`port/src/libultra.c`); and SDL2/OpenGL/filesystem backends
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

**Tools and models used to develop the port**

- Qwen 3.8 (Alibaba Qwen team), run locally as the
  [`unsloth/Qwen3.8-27B-GGUF`](https://huggingface.co/unsloth/Qwen3.8-27B-GGUF)
  `UD-Q4_K_XL` quant;
  [Unsloth](https://unsloth.ai) (the GGUF quantisation and Unsloth Desktop,
  used as the local model server).
- [pi](https://pi.dev/): the local coding-agent harness.
- [Claude / Claude Code](https://claude.com/claude-code) (Anthropic).

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
