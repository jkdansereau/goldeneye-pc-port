# GoldenEye 007 PC Port — v<version>

<p align="center">
  <img src="https://github.com/jkdansereau/goldeneye-pc-port/raw/v<version>/docs/img/shots/feature-splitscreen.jpg" width="340" alt="2-player split-screen multiplayer in the port">
  <img src="https://github.com/jkdansereau/goldeneye-pc-port/raw/v<version>/docs/img/shots/feature-options.jpg" width="340" alt="The PC options menu: custom crosshair colour sliders">
</p>

Platforms: **Windows x86-64** and **Linux x86-64 (including Steam Deck)**, plus
an **experimental macOS Apple Silicon** download (not notarized; see
[Running it](#running-it)). Region: **NTSC-U (US) only**. See [Known issues](#known-issues).

## Highlights

This release is v0.5.0 plus frame rates above 60, an experimental macOS port
and a batch of fixes.

- **Frame rates above 60.** The *Frame rate cap* (`Video.FpsCap`) now offers
  **90, 120 and 144**, and an **Auto** setting that matches your display's
  refresh rate (up to 144). Auto is the default on fresh installs; an existing
  `ge007.ini` keeps its value. Extra frames are blended between game ticks, so
  the game still simulates at its original rate. It costs one game tick of
  extra latency, does nothing on a 60 Hz display, and the *Original N64*
  display mode caps at 60. This is my rework of f1zz1ec0ke's #137.
- **macOS on Apple Silicon (experimental).** There is now a
  `macos-arm64` download (or build from source; see `docs/building.md`). Based
  on danturn's #95. A 21-level campaign sweep and live sessions ran without
  crashes on one M3 Mac, but it has not had the wider testing the Windows and
  Linux builds have, and the download is not notarized. Test reports are very
  welcome.
- **Fixes you can see:** geometry vanishing when you stand against a wall
  (#150), the Cradle catwalk shadow flicker, stale pixels beside the
  native-widescreen picture on front-end screens, and the first launch opening
  on the wrong monitor (#151).
- **Crash fixes:** the Linux end-credits crash (#152) and a crash when a guard
  spawned wearing a hat (#153).

Checked on PC at 120 Hz with VSync on a G-Sync display, and on the Steam Deck
at 90 fps.

---

## What's new

### Frame rate

- **Auto, 90, 120 and 144** join 30 and 60 in the F10 *Frame rate cap* row.
  Above 60 the port draws extra frames between game ticks by blending the
  previous and current frame. It only applies when the display is faster than
  60 Hz; on a 60 Hz display nothing changes.
- A fresh install uses **Auto**, so a 90 Hz screen runs at 90 and a 144 Hz
  monitor at 144. The *Original N64* display mode sets the cap to 60.
- A fast camera turn, or a large change in what is on screen, falls back to a
  full frame instead of a blend.
- The Steam Deck at 90 is good on most levels (see the known issues).

### macOS (Apple Silicon, experimental)

- On arm64 macOS the N64 address window sits at a high host base (set
  automatically by CMake). The 64-bit address fixes this needed (textures,
  input, animation and multiplayer crashes) are in the shared code and have no
  effect at base 0 on Windows and Linux.
- Build from source with Homebrew's GNU `gcc`; there is no download. The
  x86_64 macOS code (Julio C. Rocha's #88) is in the tree but was never
  verified on Intel hardware.

### Settings and diagnostics

- The old front-end **Velocity** pointer mode is removed; the retired ini keys
  are ignored quietly (D574).
- The Linux/macOS **update check** no longer fails on a response over 64 KB
  (D575).
- Every log line is also written to **`ge007.log`** next to the ini; the
  previous run is kept as `ge007.prev.log` (D576).
- F10 slider labels no longer end in ".." at HUD scale 150 (D577).
- The crosshair look (colour, size, style, alpha, health colour) is your
  preference and is no longer reset by the *Display mode* presets (D440).
- The sound-effect and text-bank pointer guards check the real address range,
  so they work at any address base (D573, #108 with italoarruda).

---

## Install and upgrade

### Downloads

| File | Platform |
|---|---|
| `goldeneye-pc-port-<version>-win64.zip` | Windows x86-64 |
| `goldeneye-pc-port-<version>-linux-x86_64.tar.gz` | Linux x86-64 (incl. Steam Deck) |
| `goldeneye-pc-port-<version>-macos-arm64.tar.gz` | macOS Apple Silicon, experimental |

Each contains the engine executable, a README, license texts, and the
one-time asset tool. **No ROM, no game assets.** The Windows bundle carries
its runtime DLLs and the Linux bundle carries SDL2, so nothing needs to be
installed first. The Linux bundle runs on glibc 2.31 or newer (Ubuntu 20.04+,
Debian 11+, SteamOS). The macOS bundle carries SDL2 and the C++ runtime and
needs an M1 or later Mac on macOS 14 or newer.

### Running it

You need your own **GoldenEye 007 N64 ROM** (US version, `.z64`).

1. Unpack the archive.
2. Make a `data/` folder next to the executable and put the ROM in it, named
   `ge007.ntsc-final.z64`.
3. Run the executable from that folder. The first start takes a few extra
   seconds while it generates its asset files from your ROM.

**Updating from an earlier version:** unpack into a new folder and copy your
ROM, `ge007.eep` (your progress) and `ge007.ini` (your settings) into its
`data/` folder. Don't copy the old `pcmodels-*` / `pccg-*` folders: the new
version makes its own.

**macOS (experimental):** the app is not signed with a Developer ID or
notarized, so Gatekeeper blocks a downloaded copy. After unpacking and before the
first launch, run this once in Terminal, then start `./ge007.aarch64` from
the folder:

```
xattr -dr com.apple.quarantine goldeneye-pc-port-<version>-macos-arm64
```

I have only run it on one M3 Mac, so please report what Mac and macOS you
tried and what happened.

**Steam Deck:** do the steps above on the Deck, then add the executable as a
non-Steam game. The options overlay opens with **Select** and is fully
controller-driven.

Full steps, controls and troubleshooting are in the bundled `README.md`.

---

## Known issues

| Issue | Impact | Workaround |
|---|---|---|
| On the Steam Deck at 90 fps, firefights in the centre of Bunker 1 and 2 can drop several fps | Performance, Deck only | Set the frame cap to 60 for those levels |
| Jungle can feel slightly less smooth at 90 fps than other levels | Feel | Frame cap 60 |
| Cradle's turret explosions may draw wrongly | Cosmetic; reported, not yet investigated | None |
| After alt-tabbing away, the picture can look hazy until it settles; fast repeated alt-tab is wonkier and takes longer | Cosmetic, Windows | It recovers on its own; alt-tab again or give it a couple of seconds |
| macOS (Apple Silicon) is experimental, tested on one Mac, and not notarized | Platform | Run `xattr -dr com.apple.quarantine <folder>` before the first launch; reports welcome |
| PAL and JP ROMs aren't supported in release packages | NTSC-U only | Use an NTSC-U ROM. Both regions convert, build and boot from source; packaging is still open |
| Changing aspect ratio inside a level can briefly glitch the gun/hand model, rarely | Cosmetic, one-off | Change the ratio from the front-end PC Options, or accept it |
| Saves from v0.4.0 and earlier can hold fake unlocks from `All unlocked` | Save data | Not repaired automatically. Since v0.5.0 the option never writes the save; keep a backup of `data/ge007.eep` from before you used it |
| The first frame of a level takes a little longer while its textures upload | Brief FPS-counter dip | None needed |
| Far objects almost fully in fog are now hidden as on the N64, except in widescreen where a faint distant building edge can still show (e.g. Surface's dish from the start area); 4:3 matches the N64 | Cosmetic | Higher Draw/LOD distance shows more |

Faithful original-game quirks are not port bugs. The full list, with
workarounds, is the
[known-issues table](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/ROADMAP.md#known-issues).

---

## Fixes and changes, with finding labels

- **Frame interpolation above 60 fps** (D578, rework of #137). 120 fps with
  VSync on G-Sync/VRR: black frames and OLED flicker fixed (D583). Steam Deck
  90 Hz pacing (D583).
- **#150 geometry disappearing when too close to a wall:** the near-plane clip
  now cuts at the camera plane (D579).
- **Cradle catwalk shadow flicker:** a D579 regression, fixed (D584).
- **Native-widescreen stale pixels** on front-end screens: the canvas sides are
  cleared (D579 follow-up).
- **#151** the first launch opens on the primary monitor (D581).
- **#152** Linux end-credits crash: a garbage text pointer is rejected (D582).
- **#153** hat-spawn crash: the object record type was read from the wrong byte
  on a little-endian machine (D580).
- **#108** SFX / text-bank pointer guards (D573).
- Velocity removal, update-check fix, `ge007.log`, HUD-150 sliders, preset
  scope (D574-D577, D440).
- **macOS arm64:** one address-model header, the 64-bit pointer fixes and a
  display-locked tick (D586, D591, D594, D595, D599-D604, D607-D613).

## Verification

I did not re-run the three-platform reference-frame gate for this release; the
last full run was for v0.5.0. For v0.6.0 I checked the frame-rate path and the
fixes above by playing on Windows and the Steam Deck, and the macOS build with
a 21-level sweep and live sessions.

## Thanks

f1zz1ec0ke (#137, the frame-interpolation design this release reworks),
danturn (#95, macOS), Julio C. Rocha (#88, the first macOS groundwork),
italoarruda (#108), dolent (#120-#123, already in v0.5.0), plus reporter
credits on the issue numbers named above.

## Verify the download

```
sha256sum -c goldeneye-pc-port-<version>-win64.zip.sha256
sha256sum -c goldeneye-pc-port-<version>-linux-x86_64.tar.gz.sha256
shasum -a 256 -c goldeneye-pc-port-<version>-macos-arm64.tar.gz.sha256   # macOS
```

## Source & docs

<https://github.com/jkdansereau/goldeneye-pc-port>, built on the
[GoldenEye 007 decompilation](https://github.com/n64decomp/007), with its
architecture modelled on the [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark).
Non-commercial fan preservation/research project; not affiliated with any
rights holder. **AI disclosure:** built through agentic AI coding (Claude
Code + a local open-weight model), directed by one person in their spare
time. See the README's
[How it was made](https://github.com/jkdansereau/goldeneye-pc-port#how-it-was-made)
section for the full account.
