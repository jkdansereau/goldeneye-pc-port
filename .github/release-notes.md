# GoldenEye 007 PC Port — v<version>

<p align="center">
  <img src="https://github.com/jkdansereau/goldeneye-pc-port/raw/v<version>/docs/img/shots/shot-28.jpg" width="340" alt="Streets, rendered by the port">
  <img src="https://github.com/jkdansereau/goldeneye-pc-port/raw/v<version>/docs/img/shots/shot-01.jpg" width="340" alt="Dam, rendered by the port">
</p>

<!-- MAINTAINER: header stills are from the public capture set (docs/img/shots/).
     The 15 MB gameplay-montage gif was dropped from the repo on the v0.5.0
     docs media pass (C3); the project index now uses a still carousel. -->

Platforms: **Windows x86-64** and **Linux x86-64 (including Steam Deck)**.
Region: **NTSC-U (US) only**. See [Known issues](#known-issues).

Played on Windows and Steam Deck: campaign spot-checks, 2-4 player
split-screen and controller-only sessions.

The headline is **one real options system**: both options
screens merged into a single menu, laid out like the Perfect Dark PC port's,
with the game's own N64 control styles as a per-seat preset, emulator save
files that load directly, and a scalable HUD overlay. A round of fidelity
fixes, checked frame-by-frame against the N64 game, rounds out the feature
list.

---

## New features

### One options menu, PD-style layout (D504, D519)

- The file-select **PC Options** entry now opens the same menu the F10 overlay
  shows — there is only one options UI, one place to learn (D519).
- Options are grouped into six sections in the Perfect Dark port's order
  (Video, Sound, Controls, Game, …), with **checkboxes, sliders and dropdowns**
  instead of the old numbered rows (D504).
- **Display mode** is a dropdown; **Backspace** goes back, matching the rest of
  the game's menus (D504 follow-up).

### Control styles: the game's own N64 layouts (D513, D516, D518)

- **Control style** offers **Ext** (this port's scheme, unchanged) and
  **Original**, which shows the game's own N64 styles 1.1–2.4 (D513, D516).
- The style is chosen **per seat** and saved with the Bond file, so each player
  keeps their own (D518, D516).
- The Xbox-release presets (1.1 Jinx, 1.2 Christmas, 1.3 Frost, 1.4 Elektra) and
  Custom rebinding from v0.4.x are unchanged. Preset names refer to the Xbox
  release's control styles; this project is not affiliated with Microsoft or Rare.
- Here the right stick aims and the D-pad strafes, as on the N64; the Xbox
  release's "left stick aims the crosshair" and "D-pad copies the left stick"
  behaviours are not reproduced.

### Emulator save files load directly (D514)

- An emulator-format `ge007.eep` sitting next to the executable is read on first
  launch, so a save from Project64, mupen or 1964 continues in the port without
  conversion. (Live playtest owed; the converter `eep_convert.py` still does
  explicit two-way conversion.)

### HUD and overlay scaling (D510, D512)

- The F10 overlay and the FPS counter are now the **same on-screen size** on the
  front end and inside a level (D510).
- **Game.HudScale** scales the F10 overlay and FPS counter along with the in-game
  HUD (D512).

### New settings (D511)

- **Fullscreen mode**, **Center window**, **crosshair opacity** and
  **crosshair colour by health** — the four settings the Perfect Dark port has
  that this port was missing.

---

## Fidelity and stability fixes

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
- **Fog:** objects far in fog are hidden as on the N64 (fog snap); in widescreen
  the wider view can still show a faint distant building edge (e.g. Surface's
  dish from the start area) — 4:3 matches the N64 (D503).
- **Aspect ratio:** forced ratios now letterbox/pillarbox without re-cropping the
  play area (D508); the rare gun/hand model glitch after changing ratio
  mid-level is largely fixed, a rare remainder is logged (D509).
- **Watch menu:** closing the in-level **Watch** menu now saves the watch's own
  screen size and ratio, and they no longer fight the front-end **Aspect ratio**
  setting; the earlier note in the findings log was corrected (D349).
- **Overlay controls:** slider wedges, slider markers and dropdown arrows no
  longer step in whole canvas pixels (which grew visibly at large window sizes);
  they are drawn smooth at every size (D520).
- **FPS counter:** holding left/right on a Bond settings row no longer drops the
  FPS counter (D517).
- **Build:** a fresh build directory no longer produces an untagged executable
  name (D515).
- **Tooling:** the asset converter's argument quoting and sidecar size check are
  tightened; the CI cache actions no longer carry stale ROM paths. The
  reference-frame tooling is now non-destructive and drift-free: a verification sweep used to
  overwrite your own `data/ge007.ini` and leave the previous level's save file
  behind, and now pins both idempotently (your ini and save are restored whatever
  the run does); the capture helper reads its frame windows out of the gate instead
  of a hardcoded copy that had gone stale (D524). The gate's recipe was re-based
  too: per-level windows that sit in settled gameplay rather than inside the intro
  flyby, and the save file pinned present (D522, D523).
- **Harness:** the debug capture/launch env vars are documented — `GE_PCDUMP` must be
  `first-last:step` (a colon where the dash belongs silently dumps every frame, D527),
  and a `GE_STARTMENU` boot skips the EEPROM import, so the imported-save test must run
  from a normal boot (D528). A per-triangle draw-state census probe
  (`GE_D526`/`GE_D526BOX`/`GE_D526MAX`) is available for transparency/texture triage
  (D526; the P13 Dam-ending grate show-through it targeted was confirmed faithful N64
  behaviour on 1964/GEPD, so no change shipped).

---

## The small stuff

27 menu-wording edits across both menus (option names say what they do, not
what the internal variable is called, D505); sliders carrying **real units** —
percent, frames, seconds, pixels — standardised across the two menus (D506);
a **one-line description** of the selected option in the same menu (D507);
and menu spelling standardised to one convention across both menus.

- **Settings standardised (D546):** Draw and LOD distance default to 2.0x
  (N64 = 1.0x), stick deadzone to 25%, and the mouse sensitivity maximum is 4x.
  **FOV** is now vertical degrees, 30-90, with 60 = the N64 view. The old
  "Guard AI uses full wide view" row is now **Gameplay view area**
  (Original / Extended). Untouched old values migrate once; existing
  `ge007.ini` files still load.
- **Optional update check (D551):** a new **Check for updates** option in Game,
  **off by default**. When on, it makes one HTTPS request to GitHub per launch
  and shows an "Update available" row linking to the releases page.
- **Settings menu wording pass (D554):** clearer tips that wrap to two lines
  instead of being cut off, one `Key: Action` style for the control hints,
  consistent casing and names (Original / Extended, "Original layout").
  *Skip intro* and *All unlocked* are no longer marked experimental.
  The game's own *Look up/down* option is gone from the menu; use *Invert look*
  on the Mouse and Controller pages (D564).
- **Crosshair mouse pointer and scrollbar in the settings menu (D555, D556):**
  the game's own crosshair is the pointer while F10 / PC Options is open (Mouse →
  Crosshair pointer to turn it off), long pages get a draggable scrollbar, and
  tips are kept only where a setting needs explaining. Long tips are no longer
  cut off inside a level (D558).
- **New defaults (D556, D557):** Crouch mode defaults to Toggle (new installs and
  Reset to defaults), and new profiles start with Look ahead off, which fights
  mouse look on PC; an existing save's untouched empty folders (no progress,
  factory options) get the same when first played (D559). Played or
  customised profiles and existing settings are not changed.
- **All unlocked** now applies live from the options menu, with no restart
  (D547).
- **F10 overlay mouse:** the OS cursor is always shown (no more flicker on the
  main menu), right-click goes back, and the tip line follows the hovered row
  (D544).
- **Window:** the title bar is now static (no per-second "NN fps" refresh; the
  FPS readout is still in the F10 overlay), and the window and taskbar icon is a
  new project mark instead of a game still (D550). Live icon/title check owed.

---

## Known issues

| Issue | Impact | Workaround |
|---|---|---|
| PAL and JP ROMs aren't supported in release packages | NTSC-U only | Use an NTSC-U ROM. Both regions convert, build and boot from source; packaging is still open |
| Changing aspect ratio inside a level can briefly glitch the gun/hand model, rarely | Cosmetic, one-off | Change the ratio from the front-end PC Options, or accept it |
| No macOS or ARM builds | Platform | — |
| Saves from builds before v0.4.1 can hold fake unlocks from `All unlocked` | Save data | Not repaired automatically. Since v0.4.1 the option never writes the save; keep a backup of `data/ge007.eep` from before you used it |
| The first frame of a level takes a little longer while its textures upload | Brief FPS-counter dip | None needed |
| Far objects almost fully in fog are now hidden as on the N64, except in widescreen where a faint distant building edge can still show (e.g. Surface's dish from the start area); 4:3 matches the N64 | Cosmetic | Higher Draw/LOD distance shows more |

The full list, with workarounds, is the
[known-issues table](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/ROADMAP.md#known-issues).

**Reference-frame gate for this release:** Windows, Linux and Steam Deck each
carry the full 21-level reference-frame set (63 frames each) at the same stems,
re-captured for this release's fog and near-plane changes and confirmed by two
independent capture passes per platform. The full pixel gate is green on all
three against their own goldens on the final build: Windows 21/21, Linux
(Intel HD 3000) 21/21 and Steam Deck 21/21. The recipe pins the run for you: the save file's
CONTENT is pinned (the gate installs its own canonical save per level, the same
way it pins the display ini — D529) and the PRNG seed is hard-pinned (a stray
`GE_RSEED` in your environment warns and is ignored rather than re-seeding the
gate — D532), so a gate run and a capture run see the same starting state. The
cross-platform spread on the shipped sets (0.188-4.553% of pixels over tol 2 on
the 20 non-Cuba levels) is informational only — it is not a cross-platform
pixel-parity claim.

---

## Thanks

Outside contributions merged for this release: dolent (PRs #120–#122), MST246
(#125 draw-distance investigation), italoarruda (#109 gamepad-preset ideas),
TenebrusoM (DexDrive / the D514 save-format sample), plus reporter credits on
the issue numbers named above.

## Downloads

| File | Platform |
|---|---|
| `goldeneye-pc-port-<version>-win64.zip` | Windows x86-64 |
| `goldeneye-pc-port-<version>-linux-x86_64.tar.gz` | Linux x86-64 (incl. Steam Deck) |

Each contains the engine executable, a README, license texts, and the
one-time asset tool. **No ROM, no game assets.** The Windows bundle carries
its runtime DLLs and the Linux bundle carries SDL2, so nothing needs to be
installed first.

## Running it

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
