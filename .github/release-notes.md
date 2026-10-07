# GoldenEye 007 PC Port — v<version>

<p align="center">
  <img src="https://github.com/jkdansereau/goldeneye-pc-port/raw/v<version>/docs/img/shots/feature-splitscreen.jpg" width="340" alt="2-player split-screen multiplayer in the port">
  <img src="https://github.com/jkdansereau/goldeneye-pc-port/raw/v<version>/docs/img/shots/feature-options.jpg" width="340" alt="The PC options menu: custom crosshair colour sliders">
</p>

<!-- MAINTAINER: header stills are from the public capture set (docs/img/shots/).
     The 15 MB gameplay-montage gif was dropped from the repo on the v0.5.0
     docs media pass (C3); the project index now uses a still carousel. -->

Platforms: **Windows x86-64** and **Linux x86-64 (including Steam Deck)**.
Region: **NTSC-U (US) only**. See [Known issues](#known-issues).

## Highlights

This is the first release since v0.4.0. It includes everything that was
staged as v0.4.1, which was never published on its own.

- **2-4 player split-screen.** The game's multiplayer runs with a pad per
  player, on every map, with each seat keeping its own controls.
- **One options menu.** The file-select **PC Options** entry and the F10 overlay
  are now the same menu, laid out like the Perfect Dark PC port's, with
  checkboxes, sliders and dropdowns. The game's own crosshair is the mouse
  pointer, long pages scroll, and a tip explains the selected option.
- **The game's N64 control styles, per player.** Pick **Original** to use the
  game's own styles 1.1-2.4; each seat keeps its own, saved with the Bond file.
- **Emulator saves load directly.** Copy a Project64, mupen or 1964 save in as
  `data/ge007.eep` and it is converted on first launch.
- **Fog and haze closer to the N64.** Ground fog no longer chunks away tile by
  tile, the sky fades into fog at the horizon, and the haze around Bond stays
  steady as he moves. A new **Fog distance** setting (100-800%, 100% = N64) is separate
  from Draw distance, and both apply live.
- **Fixes you can see:** the Dam ending camera swivels onto Bond as on the N64,
  and rockets no longer pass
  through the ground.
- **New settings:** an optional always-on crosshair, a custom
  crosshair colour (0-255 RGB, like the Perfect Dark port), fullscreen mode,
  center window, crosshair opacity and crosshair colour by health, a scalable
  HUD overlay, and an optional update check (off by default).

Played on Windows and Steam Deck: campaign spot-checks, 2-4 player split-screen
and controller-only sessions.

---

## What's new

### Options menu

- Six sections in the Perfect Dark port's order: Video, Audio, Mouse,
  Controller, Key Bindings and Game. **Display mode** is a dropdown; **Backspace** goes back.
- The F10 overlay and FPS counter are the same on-screen size in the front end
  and in a level, and **Game.HudScale** scales them with the in-game HUD.
  Sliders and arrows are drawn smooth at any window size.
- The OS cursor stays visible over the overlay (no main-menu flicker),
  right-click goes back, and the tip follows the hovered row.
- Clearer wording: 27 menu-wording edits, sliders with real units (percent,
  frames, seconds, pixels), wrapped tips, one `Key: Action` hint style and
  consistent names (Original / Extended, "Original layout"). *Skip intro* and
  *All unlocked* are no longer marked experimental, and *All unlocked* applies
  live. The game's own *Look up/down* row is gone; use *Invert look* on the
  Mouse and Controller pages.

### Controls

- **Control style:** **Ext** (this port's scheme, unchanged) or **Original**
  (the game's N64 styles 1.1-2.4), chosen per seat.
- The Xbox-release presets (1.1 Jinx, 1.2 Christmas, 1.3 Frost, 1.4 Elektra)
  and Custom rebinding carry over. Preset names refer to the Xbox
  release's control styles; this project is not affiliated with Microsoft or Rare.
- The right stick aims and the D-pad strafes, as on the N64; the Xbox
  release's "left stick aims the crosshair" and "D-pad copies the left stick"
  behaviours are not reproduced.
- New profiles default to Crouch Toggle and Look ahead off (it fights mouse look
  on PC). Existing untouched empty save folders get the same on first play;
  played or customised profiles are not changed.

### Saves

- An emulator-format `data/ge007.eep` is converted on first launch (the
  original is kept as `ge007.eep.emulator.bak`). The converter `eep_convert.py` still does explicit two-way conversion.
  (Live playtest owed.)

### Picture and settings

- Fog and near-plane clipping closer to the N64 (details under the technical
  section below). Forced aspect ratios letterbox/pillarbox without re-cropping
  the play area.
- Draw and LOD distance default to 2.0x (N64 = 1.0x), stick deadzone to 25%,
  mouse sensitivity maximum is 4x. **FOV** is vertical degrees, 30-90, with
  60 = the N64 view. "Guard AI uses full wide view" is now **Gameplay view
  area** (Original / Extended). Old values migrate once; existing `ge007.ini`
  files still load.

### Window and updates

- The title bar is static (the FPS readout is still in the F10 overlay) and
  the window/taskbar icon is a new project mark.
- **Check for updates** (Game, **off by default**): when on, one HTTPS request
  to GitHub per launch and an "Update available" row linking to the releases page.

### Also new since v0.4.0

- **Split-screen multiplayer** for 2-4 players, played live in 2P on every
  multiplayer map and in 4P on Temple.
- Controller presets and rebinding, PlayStation and Nintendo button names,
  master volume and audio-device selection.
- A steady 60 fps on low-end GPUs (tested on an Intel HD 3000 laptop).
- Fidelity fixes checked against the N64 game: AI visibility at long draw
  distances, sniper zoom, turret pacing, Dam/Caverns water, weapons.
- Fixes for reported issues: fire rate (#114), the tank-crush sound loop
  (#115), tank movement (#116), weapon sway (#117), recoil on some GPUs (#118)
  and light fixtures that didn't react to shots (#119).

---

## Install and upgrade

### Downloads

| File | Platform |
|---|---|
| `goldeneye-pc-port-<version>-win64.zip` | Windows x86-64 |
| `goldeneye-pc-port-<version>-linux-x86_64.tar.gz` | Linux x86-64 (incl. Steam Deck) |

Each contains the engine executable, a README, license texts, and the
one-time asset tool. **No ROM, no game assets.** The Windows bundle carries
its runtime DLLs and the Linux bundle carries SDL2, so nothing needs to be
installed first. The Linux bundle runs on glibc 2.31 or newer (Ubuntu 20.04+,
Debian 11+, SteamOS).

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

**Steam Deck:** do the steps above on the Deck, then add the executable as a
non-Steam game. The options overlay opens with **Select** and is fully
controller-driven.

Full steps, controls and troubleshooting are in the bundled `README.md`.

---

## Known issues

| Issue | Impact | Workaround |
|---|---|---|
| PAL and JP ROMs aren't supported in release packages | NTSC-U only | Use an NTSC-U ROM. Both regions convert, build and boot from source; packaging is still open |
| Changing aspect ratio inside a level can briefly glitch the gun/hand model, rarely | Cosmetic, one-off | Change the ratio from the front-end PC Options, or accept it |
| No macOS or ARM builds | Platform | — |
| Saves from v0.4.0 and earlier can hold fake unlocks from `All unlocked` | Save data | Not repaired automatically. Since v0.5.0 the option never writes the save; keep a backup of `data/ge007.eep` from before you used it |
| The first frame of a level takes a little longer while its textures upload | Brief FPS-counter dip | None needed |
| Far objects almost fully in fog are now hidden as on the N64, except in widescreen where a faint distant building edge can still show (e.g. Surface's dish from the start area); 4:3 matches the N64 | Cosmetic | Higher Draw/LOD distance shows more |

The full list, with workarounds, is the
[known-issues table](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/ROADMAP.md#known-issues).

---

## Fixes and changes, with finding labels

Menu and settings work:

- One options UI: the file-select entry opens the F10 menu (D519). Six
  PD-ordered sections with checkboxes, sliders and dropdowns (D504); display
  mode dropdown and Backspace-back (D504 follow-up).
- Control styles: Ext / Original with the N64 styles 1.1-2.4 (D513, D516),
  chosen per seat and saved with the Bond file (D518, D516).
- Emulator `ge007.eep` import (D514).
- F10 overlay and FPS counter match in size front-end vs level (D510); HudScale
  covers them (D512).
- Fullscreen mode, Center window, crosshair opacity and crosshair colour by
  health: the four settings the Perfect Dark port had that this port lacked (D511).
- Wording, units, tips and spelling: D505, D506, D507; settings wording pass
  and tip wrapping (D554); Look up/down row removed (D564).
- Settings standardised: defaults, FOV in vertical degrees, Gameplay view area (D546).
- Optional update check (D551). All unlocked applies live (D547).
- Crosshair mouse pointer and scrollbar (D555, D556); long tips no longer cut
  off inside a level (D558). New defaults: Crouch Toggle, Look ahead off (D556,
  D557); same for untouched empty save folders (D559).
- Crosshair always on, opt-in, off by default (D436, #123 by dolent). Crosshair colour is
  Original or Custom RGB with 0-255 sliders, as in the Perfect Dark port; a
  saved named preset carries over as Custom (D569). Long dropdowns stay on
  screen (D568).
- F10 overlay mouse behaviour (D544). Static title bar and new icon (D550).
- Overlay slider wedges, markers and dropdown arrows used to step in whole
  canvas pixels and grew visibly at large window sizes; now smooth (D520).
  Holding left/right on a Bond settings row no longer drops the FPS counter (D517).
- Watch menu: closing the in-level **Watch** menu saves the watch's own screen
  size and ratio, which no longer fight the front-end **Aspect ratio** setting;
  the earlier findings-log note was corrected (D349).

Rendering and gameplay fidelity:

- **Dam ending camera:** the first shot of the ending cutscene swivels up onto
  Bond again, as on the N64; an old port workaround snapped it straight onto
  him (D552).
- **Fog rework (Surface 2, from Deck playtest):** ground fog no longer "chunks"
  away tile by tile, and the sky fades into fog toward the horizon as on the
  N64 (D540). Large near-camera triangles are now clipped against the near
  plane the way the N64's RSP does, with per-vertex fog recomputed on the
  clipped edge, so the haze around Bond stays steady as he moves instead of
  brightening and darkening (D543, D553; compared against hardware-level
  emulation). A new **Fog distance** setting (100% = N64) is independent of
  Draw distance, and Draw and Fog distance now apply live instead of on the
  next level load.
- **Banded walls near explosions:** in Aztec's dark corridor, walls near rocket
  fire turned into black/white/yellow/blue bands (also seen on the Deck); fixed
  by clamping colour-combiner inputs to the range the N64 can produce (D548).
  Build-verified; a live Aztec re-check is owed.
- **Rocket and thrown-item crashes/clipping:** a player-fired rocket could pass
  through the ground when fired near Bond's feet (a stack-layout difference
  from the N64, D545), and the same class of bug is fixed in thrown grenades,
  knives and objects (D549).
- **Menu double-trigger (Steam Deck):** one A press could advance two menu
  screens, and one B could back out two overlay pages. A short release
  hold-off on menu A/B/Start and a single back action per press are now in
  place. This is a mitigation: the exact cause is unconfirmed (the Deck
  re-check passed) (D541).
- **Fog snap:** objects far in fog are hidden as on the N64; in widescreen
  the wider view can still show a faint distant building edge (e.g. Surface's
  dish from the start area); 4:3 matches the N64 (D503).
- **Aspect ratio:** forced ratios letterbox/pillarbox without re-cropping the
  play area (D508); the rare gun/hand model glitch after changing ratio
  mid-level is largely fixed, a rare remainder is logged (D509).

Build, tooling and harness:

- A fresh build directory no longer produces an untagged executable name (D515).
- The asset converter's argument quoting and sidecar size check are
  tightened; the CI cache actions no longer carry stale ROM paths.
- The reference-frame tooling is non-destructive and drift-free: a verification
  sweep used to overwrite your own `data/ge007.ini` and leave the previous
  level's save file behind, and now pins both idempotently (your ini and save
  are restored whatever the run does); the capture helper reads its frame
  windows out of the gate instead of a stale hardcoded copy (D524). The gate's
  recipe was re-based: per-level windows in settled gameplay rather than the
  intro flyby, and the save file pinned present (D522, D523).
- Debug env vars are documented: `GE_PCDUMP` must be `first-last:step` (a colon
  where the dash belongs silently dumps every frame, D527), and a
  `GE_STARTMENU` boot skips the EEPROM import, so the imported-save test must
  run from a normal boot (D528). A per-triangle draw-state census probe
  (`GE_D526`/`GE_D526BOX`/`GE_D526MAX`) is available for transparency/texture
  triage (D526; the P13 Dam-ending grate show-through it targeted was confirmed
  faithful N64 behaviour on 1964/GEPD, so no change shipped).

## Verification

**Reference-frame gate for this release:** Windows, Linux and Steam Deck each
carry the full 21-level reference-frame set (63 frames each) at the same stems,
re-captured for this release's fog and near-plane changes and confirmed by two
independent capture passes per platform. The full pixel gate is green on all
three against their own goldens on the final build: Windows 21/21, Linux
(Intel HD 3000) 21/21 and Steam Deck 21/21. The recipe pins the run for you: the
save file's CONTENT is pinned (the gate installs its own canonical save per
level, the same way it pins the display ini — D529) and the PRNG seed is
hard-pinned (a stray `GE_RSEED` in your environment warns and is ignored rather
than re-seeding the gate — D532), so a gate run and a capture run see the same
starting state. The cross-platform spread on the shipped sets (0.188-4.553% of
pixels over tol 2 on the 20 non-Cuba levels) is informational only — it is not
a cross-platform pixel-parity claim.

## Thanks

Outside contributions merged for this release: dolent (PRs #120–#123), MST246
(#125 draw-distance investigation), italoarruda (#109 gamepad-preset ideas),
TenebrusoM (DexDrive / the D514 save-format sample), plus reporter credits on
the issue numbers named above.

## Verify the download

```
sha256sum -c goldeneye-pc-port-<version>-win64.zip.sha256
sha256sum -c goldeneye-pc-port-<version>-linux-x86_64.tar.gz.sha256
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
