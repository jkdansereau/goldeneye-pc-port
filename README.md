# GoldenEye 007 PC Port

[![CI](https://github.com/jkdansereau/goldeneye-pc-port/actions/workflows/ci.yml/badge.svg)](https://github.com/jkdansereau/goldeneye-pc-port/actions/workflows/ci.yml)
[![Latest release](https://img.shields.io/github/v/release/jkdansereau/goldeneye-pc-port?include_prereleases&label=download)](https://github.com/jkdansereau/goldeneye-pc-port/releases)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20Steam%20Deck-blue)
![license](https://img.shields.io/badge/license-MIT-ffb454)

**GoldenEye 007 (Nintendo 64, 1997) as a native PC game for Windows, Linux
and Steam Deck.** Native widescreen, 60 fps or higher (90/120/144 on fast
displays), 2–4 player split-screen, mouse and keyboard or a modern controller, rebindable controls
and an in-game options menu. No
emulator, and not a binary recompilation: the game's own
reconstructed C, rebuilt from the
[GoldenEye 007 decompilation](https://github.com/n64decomp/007), compiled
for your PC and running from your own ROM.

**[Download the latest release](https://github.com/jkdansereau/goldeneye-pc-port/releases)**,
then follow the [Quick start](#quick-start).

[Quick start](#quick-start) · [Controls](#controls) · [Features](#features) · [Settings and files](#settings-and-files) · [Troubleshooting](#troubleshooting) · [Roadmap](#roadmap) · [Building](#building-from-source) · [Legal](#legal)

> [!IMPORTANT]
> **You must supply your own GoldenEye 007 ROM.** This repository and its
> downloads contain no Nintendo code or assets, and no ROM. See
> [What's a ROM?](#whats-a-rom) and [Legal](#legal).

**v0.6.0** is the current release. It is a **pre-1.0 release, not a
finished product**: the campaign and split-screen multiplayer play end to
end, but expect missing pieces (PAL/JP ROMs, a notarized macOS build) and the occasional
breaking change between versions. v1.0 is the target for a feature-complete
build.

**AI disclosure:** this port was developed using AI coding agents (Claude
Code and a local open-weight model), directed by one person in their spare time. The
project is as much a study of that process as it is a port. See
[How it was made](#how-it-was-made).

<p align="center">
  <img src="docs/media/goldeneye-gh-preview.gif" width="64%"
       alt="~29 s gameplay montage from live v0.5.0 play sessions">
  <br><em>All in-engine, running in the port: a ~29&nbsp;s montage from live
  v0.5.0 play sessions (the Facility intro, auto-aim in the Archives,
  the crosshair options and the Dam sniper rifle, Silo, a 2-player rocket kill,
  the Facility bathroom, the Dam ending) ·
  <a href="docs/index.md">14 stills in the project index</a></em>
</p>

---

## News

- **RELEASE-DATE** — **v0.6.0** ([release notes](https://github.com/jkdansereau/goldeneye-pc-port/releases/tag/v0.6.0)):
  - **Frame rates above 60.** `Video.FpsCap` now offers 90, 120 and 144, and
    an **Auto** setting (the new default on fresh installs) that matches your
    display's refresh rate. Frames between game ticks are blended from the
    previous and current frame; the game still simulates at its original
    rate. It costs one game tick of extra latency, does nothing on a 60 Hz
    display, and the *Original N64* preset caps at 60. This is a rework of
    f1zz1ec0ke's #137. I checked 120 Hz with VSync on a G-Sync display
    on PC and 90 fps on the Steam Deck.
  - **macOS on Apple Silicon, experimental, build from source.** Based on
    danturn's #95 (with Julio C. Rocha's earlier Intel groundwork in
    #88). There is no macOS download. See [Building](docs/building.md).
  - **Fixes:** geometry vanishing when you press against a wall (#150),
    the first launch opening on the wrong monitor (#151), a Linux crash in the
    end credits (#152), a crash when a guard spawns with a hat (#153), a
    sound/text pointer check that rejected valid pointers (#108), a Cradle
    catwalk shadow flicker, and stale watch pixels in native widescreen.
  - **Smaller changes:** the legacy front-end Velocity pointer mode is gone,
    the Linux/macOS update check no longer fails on a large response, every
    run is also logged to `ge007.log` next to the ini, and F10 slider labels
    fit at HUD 150.
  - **Known issues on the Steam Deck at 90 fps:** firefights in the centre of
    Bunker 1 and 2 can drop several fps, and Jungle can feel slightly less
    smooth than other levels. Cradle's turret explosions may draw wrongly
    (reported, not yet investigated).
- **2026-10-07** — **v0.5.0** ([release notes](https://github.com/jkdansereau/goldeneye-pc-port/releases/tag/v0.5.0)), the first release
  since v0.4.0; it includes the v0.4.1 work, which was never published on
  its own:
  - **2–4 player split-screen multiplayer** on every multiplayer map, each
    player with their own pad, controls and aim settings.
  - **One options menu** for every setting, laid out like the Perfect Dark
    port's, with the game's own crosshair as the mouse pointer. Pick the
    game's own N64 control styles (1.1–2.4) per player, or the Xbox release's
    presets with per-player pad rebinding; PlayStation and Nintendo pads show
    their own button names.
  - **Closer to the N64:** fog and haze (Surface 2's ground fog, the sky at
    the horizon), automatic-weapon fire rate, sniper and camera zoom, Dam and
    Caverns water, the Jungle boss fight's timing, guard visibility in
    widescreen, and the Dam ending camera.
  - **Fewer crashes and glitches:** a multiplayer crash with the crouched
    rifle, rockets passing through the ground, and broken geometry around
    Aztec's shuttle.
  - **A steady 60 fps on low-end GPUs** (tested on an Intel HD 3000 laptop),
    with lighter first-launch defaults on Atom/Celeron-class graphics.
  - **Emulator saves load directly:** copy a 1964 or Project64 save in as
    `data/ge007.eep` and it is converted on first launch.
  - Also: more audio, video and control settings (master volume, audio device,
    trilinear filtering, draw distance up to 800%, PC-friendly crouch and
    look-ahead defaults), and controllers are picked up again when replugged.
- **2026-09-28** — **v0.4.0**: native widescreen, a complete aim system for
  mouse and controller, in-game key rebinding, crosshair customization,
  rumble-pak haptics, a rebuilt options overlay, and a broad fidelity-fix
  pass (water, particles, billboard trees, front-end logo, gunshot SFX, a
  true stable 60 fps). [Release notes](https://github.com/jkdansereau/goldeneye-pc-port/releases/tag/v0.4.0).
- **2026-09-20** — **v0.3.0**: the first release with the complete campaign
  playable end to end at 60 fps on Windows, Linux, and Steam Deck.
  [Release notes](https://github.com/jkdansereau/goldeneye-pc-port/releases/tag/v0.3.0).
- **2026-09-04 → 2026-09-16** — **v0.1.0 – v0.2.2**: the alpha and beta
  cycle: build chain, software RSP, first rendered frames, front end, and
  per-level stabilization across the campaign.

---

## System requirements

**Windows:** Windows 10 or 11, 64-bit. Nothing else to install: the
download carries its own runtime libraries and the one-time asset tool (no
Python, no emulator).

**Linux / Steam Deck:** 64-bit x86 Linux with glibc 2.31 or newer (Ubuntu
20.04+, Debian 11+, Fedora 32+, SteamOS). SDL2 is bundled, so
the tarball runs as-is; it sideloads onto a Steam Deck with nothing installed.

**Both:** a GPU with OpenGL 3.0 drivers (practically any PC GPU from the last
fifteen years; keep the driver up to date). A 2011 laptop with Intel HD
3000 graphics holds 60 fps; on very weak integrated GPUs (Atom/Celeron class)
the first launch lowers draw distance and anti-aliasing to keep up. A
keyboard and mouse, or a controller (Xbox, PlayStation, Switch and most
SDL-supported pads); split-screen needs one controller per extra player.

**macOS (Apple Silicon, experimental):** an M1 or later Mac on macOS 14 or newer, using the `macos-arm64` download (nothing to install; it is not notarized, so see [macOS](#macos-apple-silicon-experimental) for the one-line unblock) or a [source build](#building-from-source). **Not yet:** ARM Linux and Windows 7 are on the [roadmap](#roadmap).

## Quick start

1. From [Releases](https://github.com/jkdansereau/goldeneye-pc-port/releases),
   download the **win64.zip** (Windows) or the **linux tarball** (Linux and
   Steam Deck), and extract it. On Windows, never run the exe from inside the
   zip.
2. Make a `data/` folder next to the executable and put your ROM in it as
   `ge007.ntsc-final.z64`. Only the **US (NTSC-U)** ROM works today; see
   [What's a ROM?](#whats-a-rom) to check yours.
3. Launch the executable from that folder. Windows may show "Windows
   protected your PC" (SmartScreen, because the exe is not code-signed):
   click **More info**, then **Run anyway**.
4. The first start takes a few extra seconds: the port reads your ROM and
   generates its asset files once. After that it starts normally.
5. Play. **F10** (pad: **Select**) opens the options overlay; on the file-select
   screen the *PC Options* entry opens the same overlay. The
   [Controls](#controls) table lists the default keys.

**Steam Deck:** the Linux tarball is the Deck build. Download it on the Deck
(or copy it over from your PC), extract it, add your ROM to `data/`, launch
it once, and add the executable as a non-Steam game. On first start it
applies Deck-friendly display defaults (native 1280×800 fullscreen); if the
resolution is wrong, set it under F10 → *Video → Resolution*.

---

## Controls

| Action              | Keyboard / mouse         | Controller    |
|---------------------|--------------------------|---------------|
| Move / strafe       | `W` `A` `S` `D`           | Left stick (or D-pad) |
| Move / turn         | Arrow keys               | —             |
| Aim / look          | Mouse                    | Right stick   |
| Fire                | Left mouse               | Right trigger |
| Aim mode            | Right mouse / `LShift`   | Left trigger / LB |
| Use / interact      | `E`                       | A             |
| Reload              | `R`                       | X             |
| Crouch (toggles)    | `LCtrl`                   | Left or right stick click |
| Cycle owned gadgets | Watch inventory          | B             |
| Next weapon         | Mouse wheel down / `Q`   | Y             |
| Previous weapon     | Mouse wheel up           | —             |
| Start               | `Enter` / `Tab`          | Start         |
| Options overlay     | `F10`                    | Select        |

The keyboard defaults are a GEPD-style layout. The controller layout matches
Rare's Xbox 1.1 (Jinx) button roles; the other Xbox styles (1.2 Christmas,
1.3 Frost, 1.4 Elektra, which swap the stick roles) are selectable under
*Controller* (pick a player first, then *Layout preset*). PlayStation and Nintendo controllers
show their own button names in the menus.

**Control style:** *Controller → Control style* (named after the watch's own
setting, with Perfect Dark's "Ext" for the port's controls) picks how the
game's controller style (the watch's 1.1–1.4 selector) is treated. **Ext**
(the default) keeps the port's feel and fixes the style itself, so the watch's
selector has no effect. **Original** stops the port from touching the style:
pick 1.1, 1.2, 1.3 or 1.4 on the watch (or in the multiplayer setup) and it
behaves as on the N64, and the Controller page then offers the game's own
styles ("Controller style", 1.1 Honey to 2.4 Goodhead, saved in the Bond file
like the watch's) instead of the layout presets, which apply in Ext only. Buttons are passed
through as the N64's buttons, so 1.3 and 1.4 move fire and aim exactly as the
console does. The 2.x dual-controller styles run faithfully too: the game
reads their second stick from the next controller slot, which the port does not
map yet (a second pad assigned to that slot should provide it, untested), so with
one pad pick a 1.x style. Saves made with earlier builds may hold 1.2 (the port wrote its
forced style back into the save), so when you switch to the Original control style, pick
your style on the watch once.

**Rebinding:** *Key Bindings* in the options overlay rebinds the
keyboard and mouse (one list: movement, then actions). Controller buttons are
rebindable per controller under *Controller* (pick a player first): set *Layout preset* to
*Custom*, select an action and press a pad button (tap B or Esc to cancel,
hold B to bind B itself, hold Back to clear; Y clears the selected slot).
Each action takes up to two buttons. Menus always keep A/X accept and B/Y
cancel, so you can't lock yourself out. There is no warning yet when two
actions share a button, and the Xbox release's "left stick aims the
crosshair" and "D-pad copies the left stick" behaviors are not reproduced
(here the right stick aims and the D-pad strafes, as on the N64).

**Split-screen:** pick Multiplayer from the main menu. With one controller,
keyboard/mouse is player 1 and the controller is player 2; with two or more
controllers, the controllers take players 1–4 (`Input.MPMode` in `ge007.ini`
changes this). A controller unplugged mid-match keeps its player when
plugged back in.

**Options overlay with a controller** (also how it works on the Deck):
**Select** opens it, the D-pad or left stick moves between rows and
left/right adjusts the selected value, **A** opens a page or toggles a row,
**B** goes back one page (and closes from the top), and **Start** (or Select
again) closes. With a keyboard, use `F10`, the arrow keys, Enter and
Backspace.

## Features

- **2–4 player split-screen multiplayer** on every MP map, with the N64's own
  setup flow and per-player HUD.
- **Native widescreen** at any aspect ratio: the world is undistorted, the HUD
  sits at the screen edges, and 4:3 menus are pillarboxed. FOV and draw/LOD
  distance are adjustable, and AI behaviour stays as on the N64 whatever you
  set them to.
- **The original look in one click:** the *Display mode* row (F10 → *Video*) switches
  between *Modern* and *Original N64* (it reads *Custom* once you edit a setting it covers),
  and *Aspect ratio → Original* shows the exact 4:3 frame.
- **A steady 60 fps**, with VSync and a frame cap (30 or 60), including on
  low-end GPUs.
- **Frame rates above 60:** 90, 120 or 144 fps, or **Auto** to match your
  display. Extra frames are blended between game ticks (the game still runs at
  its original rate), at the cost of one tick of latency. It has no effect on
  a 60 Hz display, and the *Original N64* preset keeps 60. See
  [framerate and pacing](docs/framerate-and-pacing.md).
- **macOS (Apple Silicon), experimental:** a `macos-arm64` download, not
  notarized (one Terminal command to unblock it, see [macOS](#macos-apple-silicon-experimental)),
  or build from source. I'd like test reports.
- **Graphics options:** resolution, borderless or exclusive fullscreen,
  a Center window action while windowed, MSAA (up to 16×), anisotropic
  filtering, and nearest, bilinear, trilinear or the N64's own 3-point
  texture filter.
- **Mouse aim** in your choice of style (the N64's, or centered FPS-style) with
  per-device sensitivity, smoothing and Y-inversion.
- **Modern controller support:** dual-stick layout, deadzones, sensitivity,
  southpaw, trigger thresholds, rumble-pak vibration, and PlayStation/Nintendo
  button names.
- **Rebindable controls** for keyboard, mouse and each controller.
- **HUD and crosshair options:** HUD scale (also scales the in-game options
  menu), an optional always-on crosshair, a custom RGB crosshair color
  with adjustable opacity and a health-based color, and a no-hit-flash option.
- **Full audio:** in-level music and sound effects, with music, FX and master
  volume and a choice of output device.
- **An in-game options overlay** (F10 / pad Select) for all of the above,
  saved automatically; the game's crosshair is the mouse pointer, and
  each page has a *Reset to defaults* row.
- **Faithful N64 progression by default.** Opt-in extras: *Skip intro* and
  *All unlocked* (every level, 007 mode and the full cheat menu, without
  touching your save), and *Check for updates* (off
  by default; one request to GitHub per launch when on, see Security).
- **Saves in a plain file** next to the game, so backing up is a file copy;
  1964 and Project64 saves load directly ([details](#using-an-emulator-save)).

## Status

**Fully playable, with a small set of known caveats.** All 20 solo missions
and the end-of-campaign credits load, render and run crash-free. v0.6.0 adds
frame rates above 60 and experimental macOS support; I checked the new
frame-rate path and the listed fixes by playing on Windows and Steam Deck (120 Hz
with VSync on a G-Sync display on PC, 90 fps on the Deck), and a macOS
Apple Silicon build ran the 21-level campaign sweep and live sessions without crashes. The 21-level pixel comparison of reference
frames against goldens (Windows, Linux, Steam Deck) was last done for v0.5.0. The
full campaign was last played end to end, at Agent difficulty on all three
platforms, for v0.4.0. Split-screen was played live in 2P on every
multiplayer map and in 4P on Temple.

**Known issues:** see the **[known-issues table](docs/ROADMAP.md#known-issues)**
(what you'll notice, impact, workarounds). The short version: NTSC-U ROMs
only in the release packages, and the Apple Silicon macOS download is
experimental and not notarized. On the Steam Deck at 90 fps, Bunker 1/2
centre firefights can drop several fps and Jungle can feel slightly less smooth
than other levels.

> ***All unlocked* never changes your save** since v0.5.0: it unlocks every
> level and cheat in memory only, and switching it off shows your real
> progress. v0.4.0 and earlier could write fake unlocks into the save, and
> those are not repaired; if you used it on an older build, keep a backup of
> `data/ge007.eep`.

What a release installs (no networking unless you turn on the opt-in update check, which is off by default and makes one HTTPS request to GitHub per launch; no telemetry, no ROM or game assets)
and how faithfully the port tracks the original game's logic:
[Security](docs/security.md) and [fidelity](docs/fidelity.md) status.
The binaries are not code-signed; from v0.5.0 on each release artifact also
carries a GitHub build-provenance attestation you can check with
`gh attestation verify <file> --repo jkdansereau/goldeneye-pc-port`
(see [SECURITY.md](.github/SECURITY.md)).

---

## Settings and files

**Options overlay:** press **F10** (pad: **Select**) in game or in the menus.
Changes are saved automatically. Most settings live in `ge007.ini`; the
ones that come from the game's own watch menu (auto-aim, aim
control, sight, look ahead, ammo on screen, screen size, screen ratio, music
and FX volume) are part of your save, as on the N64. Screen size (Full / Wide /
Cinema) and screen ratio (Normal / 16:9) combine with the PC **Aspect ratio**
setting: Aspect ratio owns the window shape, while the watch's two options
only change what the game renders inside it.

**Where files live:** everything stays inside the game's own folder. The port
never writes to `%APPDATA%`, the registry or any system location.

| File | What it is |
|---|---|
| `data/ge007.ntsc-final.z64` | Your ROM (you put it there). |
| `data/ge007.eep` | Your game progress (the N64 save). |
| `data/ge007.ini` | Settings, written on first start. |
| `data/pcmodels-<region>/`, `data/pccg-<region>/` | Asset files generated from your ROM on first start. Safe to delete: they are made again on the next start. |
| `data/ge007.shaders` | A list of the graphics shaders the game used, compiled at startup to avoid stutter. Safe to delete. |
| `ge007.crash.log` | Written in the folder you launched from if the game crashes. |

**Removing ROM-derived content:** the only files on your machine that come
from the game are the ROM you put there and the two generated asset folders
(`data/pcmodels-<region>/`, `data/pccg-<region>/`). Delete those and every
ROM-derived file is gone; your save (`ge007.eep`) and settings (`ge007.ini`)
are separate files, yours to keep or delete.

**`ge007.ini`** uses `[Section]` headers and `Key = value` lines. A few
common keys and their defaults:

```ini
[Video]
Fullscreen = 0
VSync = 1
FpsCap = -1
MSAA = 2
TextureFilter = 1
NativeWidescreen = 1
FovScale = 100
DrawDistance = 200

[Audio]
MasterVolume = 100
```

`Fullscreen` 1 is borderless fullscreen; `FpsCap` -1 is Auto (match the display,
up to 144), 0 is uncapped, otherwise a fps cap (30, 60, or 90/120/144 for frame
interpolation on a faster display; an existing ini keeps its value); `TextureFilter` is 0 nearest,
1 bilinear, 2 the N64 3-point filter, 3 trilinear; `FovScale` and `DrawDistance` are
percentages of the original (the options overlay shows the field of view in
degrees). On Atom/Celeron-class GPUs the first launch writes lighter values
for draw distance, LOD distance and `MSAA`. Edit the file with the game
closed: it is rewritten on a clean exit, and comments are dropped.

**Launch options:** `-fresh` wipes the save and settings before starting (a
clean-slate run). `--version` prints the build id.

## What's a ROM?

A ROM is a copy of the game cartridge's data, made with a cartridge dumper
from a cartridge you own. This project can't provide or link one. The port
needs the big-endian `.z64` format, and supports the US version today:

| Region | ROM filename (in `data/`) | SHA-1 | Supported |
|--------|---------------------------|-------|-----------|
| NTSC-U (US) | `ge007.ntsc-final.z64` | `abe01e4aeb033b6c0836819f549c791b26cfde83` | yes |
| PAL (EU)    | `ge007.pal-final.z64`  | `167c3c433dec1f1eb921736f7d53fac8cb45ee31` | not yet ([#85](https://github.com/jkdansereau/goldeneye-pc-port/issues/85)) |
| NTSC-J (JP) | `ge007.jpn-final.z64`  | `2a5dade32f7fad6c73c659d2026994632c1b3174` | not yet ([#85](https://github.com/jkdansereau/goldeneye-pc-port/issues/85)) |

To check which file you have, compute its SHA-1: on Windows,
`certutil -hashfile ge007.ntsc-final.z64 SHA1`; on Linux,
`sha1sum ge007.ntsc-final.z64`.

## Updating

Extract the new release into a new folder, then copy your ROM, `ge007.eep`
and `ge007.ini` from the old `data/` folder into the new one. Don't copy the
generated `pcmodels-*` and `pccg-*` folders: the new version makes its own on first start, and
stale ones from an older version can cause glitches.

## Using an emulator save

Saves from N64 emulators (1964, Project64, …) load directly: with the game
closed, just copy your `.eep` to `data/ge007.eep`. On first launch the game
detects the emulator byte order and converts it in place, keeping a backup of
the original file next to it as `ge007.eep.emulator.bak` (never overwriting an
existing one). Any slot that is corrupt in both formats is left alone and gets
reset by the game, exactly as before. An existing port save is untouched.

To take a save back into an emulator, use the small converter that ships in
the release folder as `tools/eep_convert.py`
(`tools_pc/eep_convert.py` in a source checkout; plain Python 3, no
dependencies):

```sh
python tools/eep_convert.py pc-to-n64 data/ge007.eep back-to-emulator.eep  # port -> emulator
python tools/eep_convert.py verify some-file.eep    # which format is this?
```

`verify` checks every save checksum and, without `--as n64|pc`, tells you
which convention the file uses.

## Uninstalling

Delete the folder. The port writes nothing anywhere else, so that removes the
game, your save and your settings; back up `data/ge007.eep` first if you
want to keep your progress.

## Troubleshooting

- **The game says the ROM is missing or wrong:** check it is in `data/`, named
  `ge007.ntsc-final.z64`, in `.z64` byte order, and that its SHA-1 matches the
  US row in [What's a ROM?](#whats-a-rom). PAL and JP ROMs are not supported
  yet.
- **Windows shows a SmartScreen warning:** the exe is not code-signed. Click
  **More info**, then **Run anyway**.
- **Black screen or no window:** update your graphics driver; the port needs
  OpenGL 3.0.
- **Wrong resolution on the Steam Deck:** F10 → *Video → Resolution*.
- **Where are my saves?** `data/ge007.eep`. Copy it somewhere safe while the
  game is not running to back it up.
- **The game crashes:** open an
  [issue](https://github.com/jkdansereau/goldeneye-pc-port/issues/new/choose)
  and attach `ge007.crash.log` (in the folder you launched from), the output of
  `--version`, your OS, your GPU and your ROM's SHA-1. **Never attach the ROM
  itself.**

---

## Roadmap

The goal is a **feature-complete, faithful** port: every mode and region the
N64 cartridge ships, behaving 1:1 with the original, with modern options on
top that can always be switched back to the original look and feel. The full
list of open items, and which ones are waiting on a decision, lives in one
place: [`docs/ROADMAP.md`](docs/ROADMAP.md). In brief:

- **PAL and JP ROM support** in the release packages
  ([#85](https://github.com/jkdansereau/goldeneye-pc-port/issues/85)). Both
  regions already convert, build and boot from source.
- **A notarized macOS build, Intel Macs and ARM Linux.** Apple Silicon macOS is
  an experimental, unsigned download in v0.6.0.
- The remaining small accuracy differences, each checked against the N64
  game in an emulator.
- **1.0 sign-off**: a full campaign playthrough at more than one difficulty
  plus a split-screen session, on every platform that ships.

This is spare-time work, so there's no timeline. Opt-in extras beyond the N64
game (LAN play, bots, co-op, HD textures, enhanced visuals) come after 1.0, and always off by default. Not planned: online
multiplayer over the internet, achievements or cloud saves, remake-scope
assets (new models, music or voice), and new movement mechanics. If any of
these matter to you, open an issue — it helps prioritize.

## How it was made

The port was built by several coding agents directed by one maintainer, with
contributions from a handful of people (credited in the release notes).
A local open-weight model (`unsloth/Qwen3.8-27B-GGUF` on one RTX 5090, via the
[pi](https://github.com/earendil-works/pi) agent) did the groundwork: build, boot chain,
software-RSP integration, asset pipeline, first frames. **Claude Code**
(Sonnet 5 and Opus 5, now 5.5; Opus for the hardest bugs) joined for the collaborative phase:
the 21-level sweep, the ABI finding catalog, SDL input, front end. The two
handed work back and forth through shared written notes. Up to v0.5.0: 51
days (16 Aug – 6 Oct), 1,537 commits (the public history squashes the
v0.4.1–v0.5.0 work into 17), and 419 findings root-caused and logged
(`D1`–`D564`).

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
| **How** | Decompilation-based source port: human-reconstructed C, compiled for the host; game logic runs as written | Static binary recompilation: the shipped machine code is auto-translated to C; the source is machine-generated |
| **Lineage** | [GoldenEye 007 decompilation](https://github.com/n64decomp/007) + [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark) engine family | Xbox 360 "…Recompiled" static-recompilation family |
| **Renderer** | Software RSP → OpenGL | Hardware (Vulkan) |
| **Status** | Pre-1.0 releases; full campaign and split-screen playable at 60 fps or higher (see [Status](#status)) | Playable full game |
| **Why it exists** | To run the *original* N64 game from source, and as a [case study in AI-agent collaboration](#how-it-was-made) on a hard low-level codebase | To get a playable PC release of the remaster |

---

## Building from source

Prerequisites: CMake >= 3.16, a C/C++ toolchain, SDL2, zlib, OpenGL and
Python 3, plus MIPS binutils, `make` and `git` for the one-time asset
extraction (no IDO/IRIX toolchain is involved; on Windows the extraction is
easiest under WSL). See [`docs/building.md`](docs/building.md) for the full
walkthrough and the asset-extraction details.

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
./build-pc.sh ntsc-final         # NTSC-U; PAL/JP: see docs/building.md
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
`build-pc/ge007.x86_64.exe`). To run a source build, create `data/` in the
repo root, put your ROM in it as in [What's a ROM?](#whats-a-rom), and run
`./build-pc/ge007.x86_64` from the repo root.

### macOS (Apple Silicon, experimental)

**Download:** `goldeneye-pc-port-<version>-macos-arm64.tar.gz` from the release
page, for Apple Silicon (M1 or later), macOS 14 or newer. It bundles SDL2 and the
C++ runtime, so Homebrew is not needed. The app is experimental and **not
signed with an Apple Developer ID or notarized**, so Gatekeeper blocks it when
it comes from a download. Unpack it, then run `xattr -dr com.apple.quarantine <folder>` in Terminal once, before the first launch:

```sh
xattr -dr com.apple.quarantine goldeneye-pc-port-<version>-macos-arm64
```

Then set up `data/` with your ROM as in the package README and run
`./ge007.aarch64` from that folder in Terminal. If you launch it first, the process
stalls before the window appears (it never reached `main` in my test; that is
most likely Gatekeeper, and clearing the flag afterwards did not unstick that
copy), so re-extract the archive and clear the flag before launching. I have run
the package on one M3 Mac only, so test reports (what Mac, what macOS, what
happened) are very welcome.

**Build from source:** use Homebrew's
GNU `gcc` (Apple's Clang can't compile the decomp), then the same
asset-extraction and `./build-pc.sh ntsc-final` steps as above. Details are in
[`docs/building.md`](docs/building.md).

```sh
brew install cmake gcc sdl2 zlib python3
```

On Apple Silicon the executable is tagged `aarch64` instead of `x86_64`.

## How it works

The game code in `src/` is the decompilation's, and its control flow is the
reference we preserve. Everything that would touch N64 hardware is redirected into
`port/`: a **software RSP** (`port/fast3d/`, adapted from the Perfect Dark
port) that interprets the graphics command stream (the GBI display list)
the game builds each frame and
emits OpenGL, bypassing the RDP; libultra OS shims (`port/src/libultra.c`,
game threads as real host threads) that let the game's own scheduler run,
with the software RSP on its own render thread as the RSP was its own chip;
and SDL2/OpenGL/filesystem backends for video, audio, input and storage.
Every port change inside `src/` is gated behind `#ifdef PORT` and
documented. They are few in kind, all narrow in scope: mechanical 32→64-bit
pointer/struct fixes (the pointer-width transition touches many structs, so
there are a lot of them), a few maintainer-approved timing fixes where
per-frame N64 code ran too fast at the PC's 60 fps (for example sniper zoom
and turret fire), the opt-in *All unlocked* hook, and env-gated diagnostic
probes tied to the bug catalogue (off by default, no behavior change). Where it diverges from the Perfect Dark port: GoldenEye's N64
serialized asset formats are converted in a separate step rather than
fixed up at load time — on first start for a download, or by the offline
Python converters in `tools_pc/` for a source build. Full detail:
[`docs/internals.md`](docs/internals.md) and
[`docs/porting-notes.md`](docs/porting-notes.md).

```
CMakeLists.txt      PC build (parallel to the decomp's Makefile, which is untouched)
build-pc.sh         configure + build helper
src/  include/      the decompilation (game + libultra); port edits are #ifdef PORT
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
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | The single tracker of open work and known issues. |
| [`docs/building.md`](docs/building.md) | Full build + asset-extraction guide. |
| [`docs/internals.md`](docs/internals.md) | Architecture, the RSP-emulation approach, GE-vs-PD engine differences, the phased plan. |
| [`docs/porting-notes.md`](docs/porting-notes.md) | The recurring N64→PC bug classes hit during the port, with fixes. |
| [`docs/dev/agentic-development.md`](docs/dev/agentic-development.md) | The research angle: the two-agent setup, timeline, handoff workflow, and an assessment of what did and didn't work. |
| [`docs/dev-process.md`](docs/dev-process.md) | The investigation workflow in detail: budgets, file partitioning, the finding-log discipline. |
| [`docs/dev/`](docs/dev/) | The raw engineering record: the full finding log, per-level status, playtest matrices, and [`docs/dev/game-behavior-reference.md`](docs/dev/game-behavior-reference.md) (a secondary-sourced playtest reference for how the retail game is meant to behave; repo-only, code is ground truth). |
| [`docs/SetupGuide.md`](docs/SetupGuide.md), [`docs/StyleGuide.md`](docs/StyleGuide.md) | Inherited from the decompilation this repo forks. |

## Contributing and forking

Issues and pull requests are welcome, and so are forks: the port layer, build
system and `tools_pc/` are MIT-licensed and exist to be extended. Read
[`CONTRIBUTING.md`](CONTRIBUTING.md) first: it has the ground rules that keep
the port faithful (game logic stays unmodified), what to verify before a
pull request, and what a fork is asked to keep.

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

**Contributors**

- **dolent** (#120, #121, #122, #123), **italoarruda** (#106, #107, #108, #109),
  **danturn** (#95 macOS, #96), **Julio C. Rocha** (#88 macOS Intel groundwork),
  **f1zz1ec0ke** (#137 frame interpolation), **JosephAHK** (#133), and
  **MST246** (the #125 investigation), plus everyone who filed issues.

**Consultation**

- **f1zz1ec0ke** ([GitHub](https://github.com/f1zz1ec0ke)): LLM consultation on model
  tuning, agent harnesses, and agentic strategy throughout the port's development
  (and the author of the #137 frame-interpolation design noted above).

**Tools and models used to develop the port**

Development ran for most of its life on a single local model — **Unsloth's
Qwen 3.8 27B** — which did nearly all of the local (groundwork) work on its
own. In the final week that grew into a small menu, and the **current standard
is NInfer + Strata**; tools are chosen per task, with local models carrying
the high-volume work and hosted models the exception. The full record of the
models used, and the harnesses that drove them:

| Model | Ran as | How it was used |
|---|---|---|
| **Qwen 3.8 27B** ([Unsloth](https://huggingface.co/unsloth/Qwen3.8-27B-GGUF)) | `UD-Q4_K_XL`, one RTX 5090 | The primary local workhorse for the bulk of the port's development. |
| **Qwen 3.8 27B** via [NInfer](https://github.com/natpate/ninfer-windows) (Windows) | base, NVFP4 and NVFP4-full artifacts; MTP (DFlash2) spec-decode; one 5090 | Trialed in the final week, now half of the standard; the fast day-to-day local driver. |
| **Qwen 3.8 27B** ([ISTA-DASLab](https://huggingface.co/ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF)) | `IQ3_S` (GSQ-RCO), larger context | Briefly trialed for larger-context workloads; shelved, little use since v0.2.0. |
| **Qwen 3.8 Flash-Next** via [Strata](https://github.com/Niko1221/Strata) | 125B MoE, `IQ1_M` (1-bit), one gaming GPU | The big-context local tier: a model that normally needs a server, run locally for the largest-context passes. |
| **Claude** (Anthropic) | Sonnet 5 / Opus 5, now **Sonnet 5.5 / Opus 5.5** (out in the last few weeks) | The orchestrator in the two-agent phase: held what the 27B couldn't close, took over on limit handoff; the 5.5 tier has driven the most recent work. |
| **GPT-6 (Sol)** (OpenAI), via [OpenRouter](https://openrouter.ai) | a frontier hosted model | Trialed. |

*Harnesses and tooling:*

- **[pi](https://github.com/earendil-works/pi)** — the local coding-agent
  harness that drives the local models.
- **[Claude Code](https://github.com/anthropics/claude-code)** (Anthropic) —
  the agentic harness that drove the Claude models above; the orchestrator in
  the two-agent phase.
- **[claude-code-delegate-local](https://github.com/fegone/claude-code-delegate-local)** —
  the MCP bridge that let Claude and the local models talk (delegating Claude's
  subagents to the local engines).

## Legal

This is a non-commercial fan preservation/research project, in the same
category as the many other N64 decompilation and native-port repositories on
GitHub. It is **not affiliated with, endorsed by, or sponsored by** Nintendo,
Rare, Microsoft, Valve, MGM, Danjaq, EON Productions, or any rights holder in
GoldenEye or James Bond. "GoldenEye 007", "007", "James Bond" and related
marks belong to their respective owners; they are used here only to say which
game this port runs. No official logos, box art, or marketing assets are used.

### What the download contains

- **The engine:** the `port/` layer plus the compiled decompilation.
- **Runtime libraries** under permissive licenses (SDL2, zlib, the MinGW
  runtime); their licenses travel in the download.
- **The one-time asset tool**, which reads your ROM on your machine.

It contains **no ROM and no game assets of any kind**. Any build, yours or a
release, is useless without a ROM you supply.

### What happens on your computer

Textures, audio, models, level data and in-game text are extracted from a ROM
*you already own*, on *your* machine, the first time you start the game. The
port has no network access unless you turn on the opt-in *Check for updates* option (`Game.CheckUpdates`, off by default). When on, it makes one HTTPS request per launch to GitHub's releases API to look for a newer release (GitHub sees the request and your IP, as with any web request), never downloads or installs anything, and never uploads anything.

### This repository

The repository is a fork of the public
[GoldenEye 007 decompilation](https://github.com/n64decomp/007) and inherits
its contents (see [`NOTICE`](NOTICE) for what that includes). The few
port-specific edits inside the decompiled code are marked `#ifdef PORT`.

The project mark / favicon is original *generated* art -- a generic version
of the game's default aim cross (a circle with N/S/E/W lines crossing its
edge), no game screenshots or logos in it.

### Your part

- Only use a ROM that you dumped from a cartridge you own.
- Never ask for, link to or share ROMs or extracted game files in issues,
  discussions or pull requests.

If you are a rights holder with a concern, open an issue and it will be
addressed.

## License

The original work in this repository, the port layer (`port/`), the PC build
system, `tools_pc/`, and the documentation, is released under the MIT License;
see [`LICENSE`](LICENSE). Everything inherited from the upstream decompilation
is covered by [`NOTICE`](NOTICE), not by that license.

---

*Last updated 2026-10-10 — v0.6.0 · <a href="https://github.com/jkdansereau/goldeneye-pc-port">GitHub</a> · <a href="https://github.com/jkdansereau/goldeneye-pc-port/releases">Releases</a>*
