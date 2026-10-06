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

<!-- MAINTAINER: after the play session, add one line here saying what this
     build was played on (e.g. "Played on Windows and Steam Deck: campaign
     spot-checks, 2-4P split-screen, a controller-only session"). -->

The headline is **the settings interface, rebuilt**. Both options screens are
merged into one, laid out like the Perfect Dark PC port's menu, with plain-English
wording, real-unit sliders and a one-line description under whichever option you
select. Alongside it: the game's own N64 control styles as a per-seat preset,
emulator save files that load directly, a scalable HUD overlay, and a round of
fidelity fixes checked frame-by-frame against the N64 game.

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

### Menu wording, units and help text (D505–D507)

- 27 maintainer-approved wording edits across both menus: option names say what
  they do, not what the internal variable is called (D505).
- Sliders carry **real units** — percent, frames, seconds, pixels — and their
  values are standardised across the two menus (D506).
- Selecting an option shows a **one-line description** of it in the same menu
  (D507).

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

### Overlay controls are antialiased (D520)

- Slider wedges, slider markers and dropdown arrows no longer step in whole
  canvas pixels (which grew visibly at large window sizes); they are drawn
  smooth at every size.

### Watch menu keeps its own settings (D349, corrected)

- Closing the in-level **Watch** menu now saves the watch's own screen size and
  ratio, and they no longer fight the front-end **Aspect ratio** setting. The
  earlier note in the findings log was corrected.

---

## Fidelity and stability fixes

- **Fog:** objects far in fog are hidden as on the N64 (fog snap); in widescreen
  the wider view can still show a faint distant building edge (e.g. Surface's
  dish from the start area) — 4:3 matches the N64 (D503).
- **Aspect ratio:** forced ratios now letterbox/pillarbox without re-cropping the
  play area (D508); the rare gun/hand model glitch after changing ratio
  mid-level is largely fixed, a rare remainder is logged (D509).
- **FPS counter:** holding left/right on a Bond settings row no longer drops the
  FPS counter (D517).
- **Build:** a fresh build directory no longer produces an untagged executable
  name (D515).
- **Tooling:** the asset converter's argument quoting and sidecar size check are
  tightened; the CI cache actions no longer carry stale ROM paths. The golden-frame
  tooling is now non-destructive and drift-free: a verification sweep used to
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

## Known issues

| Issue | Impact | Workaround |
|---|---|---|
| PAL and JP ROMs aren't supported in release packages | NTSC-U only | Use an NTSC-U ROM. Both regions convert, build and boot from source; packaging is still open |
| Changing aspect ratio inside a level can briefly glitch the gun/hand model, rarely | Cosmetic, one-off | Change the ratio from the front-end PC Options, or accept it |
| No macOS or ARM builds | Platform | — |
| Saves from builds before v0.4.1 can hold fake unlocks from `All unlocked` | Save data | Not repaired automatically. Since v0.4.1 the option never writes the save; keep a backup of `data/ge007.eep` from before you used it |
| `Skip intro` and `All unlocked` are experimental | Opt-in options | Leave them off for a normal playthrough |
| The first frame of a level takes a little longer while its textures upload | Brief FPS-counter dip | None needed |
| Far objects almost fully in fog are now hidden as on the N64, except in widescreen where a faint distant building edge can still show (e.g. Surface's dish from the start area); 4:3 matches the N64 | Cosmetic | Higher Draw/LOD distance shows more |

The full list, with workarounds, is the
[known-issues table](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/ROADMAP.md#known-issues).

**Not yet playtested live** (merged, build- and review-verified, owed a play
session before this release is announced): the four new settings (D511),
`Game.HudScale` (D512), Control style Original on a pad (D513/D516 per seat),
emulator-save import (D514), and the D519 profile-chooser row / no-click-through
follow-ups.
<!-- MAINTAINER: if the play session did not cover all of the above, keep the
     sentence "these were merged and review-verified but not playtested live"
     in the body; never imply a full playtest. -->

**Golden-gate state for this release:** both platforms carry the full 21-level
reference-frame set (63 frames each) at the same stems, and each platform's gate
is green against its own goldens. The cross-platform spread (0.348-3.983% of
pixels over tol 2 on the 20 non-Cuba levels, structural tier 21/21 clean) is
informational only — it is not a cross-platform pixel-parity claim, and the
linux *pixel* gate did not run on the capture box (its GL probe fails closed
there; the linux crash gate did). The Dam ending cutscene has not been checked
by eye in a live session.

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
