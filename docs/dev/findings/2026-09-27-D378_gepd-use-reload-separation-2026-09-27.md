# D378

<!-- from the old section-F/H table row -->
| D378 | **GEPD use/reload separation (2026-09-27).** full `## D378` entry at file tail | **FIXED (2026-09-28 bookkeeping: residual live-check notes waived by the user; shipped in v0.4.0, no reports).** Earlier: FIXED (headless edges tested; user says E/R seemed good; targeted door/partial-mag cases owed): R reload only, E interaction only in gameplay, menu/watch/tank retains B. |

## D378 — GEPD E/R were both B (use/reload separation, 2026-09-27)

**User report:** R opens doors; question whether E is use-only. **Confirmed:**
D371 mapped `IA_RELOAD` *and* `IA_CANCEL` (GEPD E) to `GE_CONT_B` in
`port/src/input.c`. `bondview2.c:5642/5712` sets `field_D0` on a B tap;
`lv.c:796` calls `bond_interact_object()` and reloads both hands via
`attempt_reload_item_in_hand()` only if the interaction found no target.
Therefore **R opens doors and E reloads with no nearby target**. The M3
plan's "we get it for free" premise was incorrect: the B context alone
cannot implement two separate actions. Native single-button B stays intact
for N64 controls; this is a PC input feature, not an N64 game-code bug.

**Fix (`port/src/input.c` only):** with GEPD layout in a playable stage,
E's rising edge calls the existing `bond_interact_object()` (its return
value is ignored: no fallback reload), while R's rising edge calls the
existing `attempt_reload_item_in_hand(GUNRIGHT/GUNLEFT)` without B/door
interaction. During front menus, watch, tank, pause/cutscenes, E retains
native B/cancel; R does not fire while controls are unavailable. Gamepad
B/Y still drives crouch in stages and B/cancel in menus (D377).
Use/reload edges are tracked across polls and the F10 overlay to avoid
held-key repeat or action leaks. **Caveat:** input sampling runs before
`lv.c`'s original after-`propsTick` interaction site, so the port call can
use the previous frame's on-screen prop list. A real door/terminal playtest
is needed to check for a one-frame targeting discrepancy; no `src/game`
behavior edits were made. Special N64 B combos (e.g. remote-mine A+B)
are not promised on GEPD's dedicated E key.

**Verification:** touched object compiled (only pre-existing warnings),
`build-pc/ge007.review.exe` linked (normal exe can be locked by a running
playtest). Headless Bunker1 `-level_09` with
`GE_INPUTSCRIPT='30:START;300:UHOLD;315:UREL;450:RELOADHOLD;465:RELOADREL'`
logged exactly one `dedicated use (no target=1)` and one `dedicated reload
(no use)`; exit clean at GE_QUITFRAME=800. The scripted scene had no door
target and does not prove interaction targeting or a visible magazine refill;
live R-near-door, E-near-door, E-no-target, R-with-partial-mag verification
remains owed. Cross-ref D371/D376/D377.
