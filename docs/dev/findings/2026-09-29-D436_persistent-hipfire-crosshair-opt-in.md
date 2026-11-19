# D436

<!-- from the old section-F/H table row -->
| D436 | **Persistent (hipfire) crosshair, opt-in: `Video.CrosshairPersistent` / F10 > Gameplay > HUD > "Crosshair when not aiming" draws the native sight at `crosshair_angle` outside aim mode (2026-09-29).** — full `## D436` entry at file tail | FIXED (shipped 2026-09-29; rule-2 sign-off: user direction; presentation-only masked condition in `gunDrawSight` + `port/src/video.c`). Default off byte-identical; aim mode unchanged; netplay SH identical mixed off/on. |

## D436 — persistent (hipfire) crosshair, opt-in: the native sight drawn outside aim mode (2026-09-29)

**Status:** SHIPPED (offered from the netplay line): port-only logic + one presentation-only `#ifdef PORT` condition in `src/game/gunfire.c` `gunDrawSight` (rule-2 sign-off: user direction 2026-09-29, "constant on screen crosshair that could be enabled or disabled"; same class and same function as upstream's D373/D381 hide/colour/size/style hooks). Default off = N64.

**Spec:** `docs/dev/RESEARCH-FREE-AIM.md` option A'. The N64 sets `GUNSIGHTREASON_NOTAIMING` every tick from `moveData.aiming` (`bondview2.c:5753`) and `gunDrawSight` draws only when `gunsightmode == 0`. Every shot, hipfire included, leaves from the screen point `crosshair_angle`, recomputed every tick in both modes (auto-aim pull, stick lag), so the native sprite drawn there in hipfire shows the true aim point. `gunsightmode` has no reader but `gunDrawSight` (draw-only state).

**Implementation:**
- `port/src/video.c`: `Video.CrosshairPersistent` (0/1) next to the other crosshair options; `portCrosshairPersistent()`.
- `src/game/gunfire.c` `gunDrawSight`: under `#ifdef PORT` the visibility test becomes `(gunsightmode & ~(persistent ? NOTAIMING : 0)) == 0 && !mpmenuon`. Every other reason still hides the sprite: `GUNSIGHTREASON_1` (the "Sight on screen" option in solo, the lobby-agreed `sight` flag in MP/netplay), `NOCONTROL` (cutscenes), `DAMAGE` (the damage/death sequence) and the MP menu. In aim mode nothing changes (one sprite, the game's own). Non-PORT builds keep the original condition byte-identical. 
- `port/src/optionsoverlay.c`: HUD row "Crosshair when not aiming" (Off/On) after "Show crosshair", plus its reset default (0). The front-end PC Options screen picks the row up through `optionsRowCount()`.

**Netplay / fairness:** render-only and per machine (nothing is written; `texSelect` + a texrect, the same variance upstream's "Show crosshair" off already introduces). A hipfire sight that visibly slides onto auto-aim targets is extra information in a match; today it follows the lobby's agreed per-player `sight` flag (a player whose sight is off sees nothing either way). Whether the lobby should pin the persistent mode too is a **user decision** (RESEARCH-FREE-AIM decision 3).

**Verification** (headless, silent, llvmpipe, mp-sandbox data, `GE_FIXEDSTEP=1 GE_TICKINPUT=1 GE_RSEED=12345`; sandboxes `build-pc/xh-*`):

| Check | Result |
|---|---|
| F10 > Gameplay > HUD auto-opened (`GE_OPTIONSOVERLAY=3 GE_OPTIONSOVERLAY_SECTION=__HdrHUD`), frame 240 | row renders between "Show crosshair" and "Crosshair colour", value Off; page self-check count 12 <= 14 |
| Solo `-level_09` hipfire, option ON, frame 1800 (unseeded first pass) | the red sight at the centre of the view, ammo/gun unchanged (PNG viewed) |
| Solo `-level_09` hipfire, option OFF, frame 1800 | no sight (PNG viewed) |
| Solo `-level_09`, option OFF, frames 600/1200/1800, new binary vs the pre-change binary (ad7b63d9) | **BYTE-IDENTICAL** all three; frame 1800 md5 `1561622c8eed` = the golden in CONTROLLER-PAGES-RESULTS |
| Solo `-level_09` hipfire, OFF vs ON, frames 600/1200/1800 | 2 561 px differ, bbox x 391-475 y 278-373 on the 869x652 canvas (the sprite at the view centre); nothing else |
| Solo `-level_09`, R held from frame 700 (aim mode), OFF vs ON, frames 1200/1800 | **identical** (one sprite, the game's own; no double draw) |
| Sanity: OFF hipfire vs OFF aim mode, frame 1800 | the same 2 561 px / bbox as the row above: the persistent sprite is pixel-for-pixel the aim-mode sprite |
| Netplay 2P loopback (Temple, MULTIPLAYER-DEV script), both peers OFF | SH identical on 4 615 common ticks, 0 DESYNC |
| Both peers ON | SH identical on 4 616 ticks, 0 DESYNC |
| **Mixed: A OFF, B ON** | SH identical on 4 610 common ticks, 0 DESYNC (render-only confirmed) |

**Not verified:** a real play session (feel, the sight over the watch while paused: the N64 draws its own aim-mode sight the same way, so it is fidelity-consistent), split-screen and the own-view transform (the D13 texrect signature should match the same sprite; check on the next couch/own-view pass).

**Confidence: [H]** for correctness and neutrality (measured); [M] for the user-facing wording of the row.

**v0.5.0 integration (2026-10-06, maintainer request "roll the always on crosshair option in"):**
merged as dolent's original commit (#123, true merge, so the PR shows Merged).
The review conditions from 2026-10-01 are met as follows: D436 is free in this
tree (no relabel needed); both display-mode presets (Modern / Original N64) set
`Video.CrosshairPersistent` 0 (`port/src/video.c` preset table), so Original N64
stays N64; the row got a tip; the `gunDrawSight` condition is presentation only
(draw test, no gameplay reader of `gunsightmode`) and is signed off by the
maintainer with this request (Rule 2). Placement: Game page, right after "Show
crosshair". **Owed (maintainer, by eye):** hipfire on/off, sniper zoom, and
split-screen 2P with the option on.
**Relabel (2026-10-06, maintainer):** the row reads "Crosshair always on" (was
"Crosshair when not aiming"); off by default (code default, reset table and both
display-mode presets all 0); new tip. The ini key `Video.CrosshairPersistent` is
unchanged.
