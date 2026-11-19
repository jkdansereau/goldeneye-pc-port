# D485

<!-- from the old section-F/H table row -->
| D485 | **#136: sniper/camera zoom stepped once per game frame with no tick scaling, so at the port's 1 tick/frame it zoomed 2x faster in real time than the N64 at 2 ticks/frame; steps now scale to base^(ticks/2) (RULE-2, maintainer-approved)** — full `## D485` entry at file tail | FIXED 2026-10-02 (local, `fix/d483-sniper`); 15 -> 7 takes 16 frames at 60 fps (was 8) = the N64 at 30 fps |

## D485 — #136: sniper/camera zoom speed follows game ticks (RULE-2)

**RULE-2-SIGNOFF:** maintainer, 2026-10-02: "I approve the rule 2 change to match the N64 real time speed and be more accurate here."
**Divergence (class D13/D329):** `camera_sniper_zoom_in/out` (`src/game/gun.c`) multiply or divide the zoom FOV by `1 + rate*0.1` once per game frame, with no `g_ClockTimer` scaling. The N64 ran these frames at ~2 ticks each (30 fps; 3 at 20 fps); the port runs 1 tick per frame at 60 fps. Measured on the port before the fix: 15 -> 7 degrees in 8 frames, every call at `clk=1`; the full 60 -> 7 range ~22 frames (~0.4 s) against ~0.75 s on an N64 at 30 fps. Only the sniper and the camera gadget use this per-frame step; all other zooms advance by elapsed ticks (`zoomintime += speedgraphframes`, `bondview2.c`) and were already frame-rate independent.
**Fix (`#ifdef PORT`; the N64 build expands to the original expression):** the four call sites use `GUN_ZOOM_STEP(zoom)`. On PORT it is `sqrt(1 + rate*0.1)` multiplied once per tick in this frame, i.e. `base^(ticks/2)`: an N64-style frame of 2 ticks gets exactly the original factor, a 1-tick port frame gets its square root, and `g_ClockTimer == 0` (paused/locked) gives no step. Same "N64 frame = 2 ticks" convention as the D427 fire-gate sign-off. Behaviour at other port frame rates follows automatically (a 30 fps port frame has 2 ticks).
**Verified 2026-10-02 (Dam, `GE_STARTWEAPON=17`, scripted aim + C-up, temporary `SNIPERZOOM` log, removed before commit):** 15 -> 7 in 16 frames (947 -> 962) at `clk=1`, exactly twice the pre-fix 8 frames, i.e. the N64's real-time speed at 30 fps. Build clean; `-level_09` with `GE_MEMPREDZONE=1` rc=0, 0 hits, no crash log.
**Owed (maintainer):** feel check against 1964 GEPD (zoom level per D484 and speed per D485).
