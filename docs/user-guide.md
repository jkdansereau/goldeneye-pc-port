---
title: User guide
description: Controls, settings, files, ROM check, updating, emulator saves, macOS setup and troubleshooting for the GoldenEye 007 PC port.
---

# User guide

Everything beyond the [Quick start](../README.md#quick-start): the full
controls, every setting and file, and fixes for common problems.

- [Controls](#controls)
- [Feature list](#feature-list)
- [Settings and files](#settings-and-files)
- [What's a ROM?](#whats-a-rom)
- [macOS (Apple Silicon, experimental)](#macos-apple-silicon-experimental)
- [Updating](#updating) · [Using an emulator save](#using-an-emulator-save) · [Uninstalling](#uninstalling)
- [Troubleshooting](#troubleshooting)


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
*Controller* (pick a player first, then *Layout presets*). PlayStation and Nintendo controllers
show their own button names in the menus.

**Control style:** *Controller → Control style* (named after the watch's own
setting, with **Extended**, after Perfect Dark's "Ext", for the port's controls) picks how the
game's controller style (the watch's 1.1–1.4 selector) is treated. **Extended**
(the default) keeps the port's feel and fixes the style itself, so the watch's
selector has no effect. **Original** stops the port from touching the style:
pick 1.1, 1.2, 1.3 or 1.4 on the watch (or in the multiplayer setup) and it
behaves as on the N64, and the Controller page then offers the game's own
styles ("Controller style", 1.1 Honey to 2.4 Goodhead, saved in the Bond file
like the watch's) instead of the layout presets, which apply in Extended only. Buttons are passed
through as the N64's buttons, so 1.3 and 1.4 move fire and aim exactly as the
console does. The 2.x dual-controller styles run faithfully too: the game
reads their second stick from the next controller slot, which the port does not
map yet (a second pad assigned to that slot should provide it, untested), so with
one pad pick a 1.x style. Saves made with earlier builds may hold 1.2 (the port wrote its
forced style back into the save), so when you switch to the Original control style, pick
your style on the watch once.

In the Original control style the pad is a strict virtual N64 controller in
every context (stage, menus, watch, tank): each press sends the native N64
button and the game decides what it means (so *B* uses a door or reloads, as
on the console). Default layout, remappable per player in the ini
(`Input.N64Pad.<Button>`, `Input.N64Pad2..4` for players 2-4; up to two pad
buttons each) and with `Input.N64StickFeel` (1 = linear +/-80 stick with a
small dead centre, 0 = the Extended deadzone):

| Pad | N64 | Pad | N64 |
|---|---|---|---|
| South (A) | A | Right stick | C buttons |
| West (X) | B | Left stick | Analog stick |
| Right trigger | Z | D-pad | D-pad |
| Left trigger / RB | R | Start | Start |
| LB | L | Back | PC options overlay |

**Rebinding:** *Key Bindings* in the options overlay rebinds the
keyboard and mouse (one list: movement, then actions). Controller buttons are
rebindable per controller under *Controller* (pick a player first): set *Layout presets* to
*Custom*, select an action and press a pad button (tap B or Esc to cancel,
hold B to bind B itself, hold Back to clear; Y clears the selected slot).
Each action takes up to two buttons. Menus always keep A/X accept and B/Y
cancel, so you can't lock yourself out. There is no warning yet when two
actions share a button, and the Xbox release's "left stick aims the
crosshair" and "D-pad copies the left stick" behaviors are not reproduced
(here the right stick aims and the D-pad strafes, as on the N64).

**Input (per player):** in the options overlay, *Input* lists the four
players. Player 1 chooses *Input type*: *Auto* (the default: keyboard, mouse
and pad all work, whichever you use), *Keyboard + mouse* or *Gamepad*; players
2–4 are always gamepads. With one of the last two, only that device plays in a
level (with no gamepad connected, player 1 stays on the keyboard + mouse).
Every device can use the menus, and a pad's Select opens the options overlay
even when the pad is not the one playing, so the type can be changed from the
pad alone. A gamepad player can also pick *Controller*: *Auto* (the
first free one) or a specific connected pad. *Mode* (Enhanced or Original) and the rows that follow depend on the
chosen type; the key bindings are a sub-menu of player 1's page.

**Split-screen:** pick Multiplayer from the main menu. With *Auto*: one
controller makes the keyboard/mouse player 1 and the controller player 2; two
or more controllers take players 1–4. With *Keyboard + mouse* the controllers
are always players 2–4; with *Gamepad* they take players 1–4 (one controller
alone leaves multiplayer locked, as on the console). A controller unplugged
mid-match keeps its player when plugged back in.

**Options overlay with a controller** (also how it works on the Deck):
**Select** opens it, the D-pad or left stick moves between rows and
left/right adjusts the selected value, **A** opens a page or toggles a row,
**B** goes back one page (and closes from the top), and **Start** (or Select
again) closes. With a keyboard, use `F10`, the arrow keys, Enter and
Backspace.


## Feature list

- **Split-screen multiplayer for up to 4 players**, with the N64's own setup
  flow and per-player HUD.
- **Native widescreen** at any aspect ratio: the world is undistorted, the HUD
  sits at the screen edges, and 4:3 menus are pillarboxed. FOV and draw/LOD
  distance are adjustable, and AI behaviour stays as on the N64 whatever you
  set them to.
- **The original look in one click:** the *Display mode* row (F10 → *Video*) switches
  between *Modern* and *Original N64* (it reads *Custom* once you edit a setting it covers),
  and *Aspect ratio → Original* shows the exact 4:3 frame.
- **A steady 60 fps**, with VSync and a frame cap (30 or 60), including on
  the low-end GPUs I test on.
- **Frame rates above 60:** 90, 120, 144, 165 or 240 fps, or **Auto** to match
  your display (up to 240). Extra frames are blended between game ticks (the game still runs at
  its original rate), at the cost of one tick of latency. It has no effect on
  a 60 Hz display, and the *Original N64* preset keeps 60. See
  [framerate and pacing](framerate-and-pacing.md).
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
  by default; one request to GitHub per launch when on, see [Security](security.md)).
- **Saves in a plain file** next to the game, so backing up is a file copy;
  1964 and Project64 saves load directly ([details](#using-an-emulator-save)).

## Settings and files

**Options overlay:** press **F10** (pad: **Select**) in game or in the menus;
on the file-select screen the *PC Options* entry opens it too.
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
up to 240), 0 is uncapped, otherwise a fps cap (30, 60, or 90 to 240 for frame
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
`sha1sum ge007.ntsc-final.z64`; on macOS, `shasum ge007.ntsc-final.z64`.

## macOS (Apple Silicon, experimental)

**Download:** `goldeneye-pc-port-<version>-macos-arm64.tar.gz` from the release
page, for Apple Silicon (M1 or later), macOS 14 or newer. It bundles SDL2 and the
C++ runtime, so Homebrew is not needed. The app is experimental and **not
signed with an Apple Developer ID or notarized**, so Gatekeeper blocks it when
it comes from a download. Unpack it, then run `xattr -dr com.apple.quarantine <folder>` in Terminal once, before the first launch:

```sh
xattr -dr com.apple.quarantine goldeneye-pc-port-<version>-macos-arm64
```

**Or skip the quarantine step** by downloading in Terminal instead of a browser
(`curl` does not mark the file, so Gatekeeper never checks it):

```sh
curl -L https://github.com/jkdansereau/goldeneye-pc-port/releases/download/v<version>/goldeneye-pc-port-<version>-macos-arm64.tar.gz | tar xz
```

**Without Terminal:** after macOS blocks the first launch, open System Settings →
Privacy & Security, scroll down to the message about `ge007.aarch64` and click
**Open Anyway** (macOS 15 removed the old right-click → Open bypass).

Then set up `data/` with your ROM as in the package README and run
`./ge007.aarch64` from that folder in Terminal. If you launch it first, the process
stalls before the window appears (it never reached `main` in my test; that is
most likely Gatekeeper, and clearing the flag afterwards did not unstick that
copy), so re-extract the archive and clear the flag before launching. I have run
the package on one M3 Mac only, so test reports (what Mac, what macOS, what
happened) are very welcome.

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
