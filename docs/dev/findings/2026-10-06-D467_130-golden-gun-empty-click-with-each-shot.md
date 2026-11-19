# D467

<!-- from the old section-F/H table row -->
| D467 | **#130 Golden Gun empty click with each shot: code audit finds the path faithful (2026-10-01)**: the dry-fire click only plays when the trigger is still held ~14 ticks after the shot; no simultaneous play at 60 fps. Open: by-ear/input-length check. | CLOSED: faithful by code + log (audited 2026-10-01; by-ear check of held-trigger behaviour vs 1964 is maintainer-owned) |

## D467 -- #130: Golden Gun "plays the empty click with each shot" (code audit, 2026-10-01; AUDITED, faithful by code)

**Report (outside, #130):** the Golden Gun plays the out-of-ammo sound together with the fire sound; in the original it does not play immediately after.
**Code path.** The click is `sndPlaySfx(EMPTY_GUN_FIRE_SFX)` at `gunfire.c` in `gunTickHandState`, only on the FIRE-state `else` arm: ammo type != 0, magazine == 0, `field_88C == 0` (first FIRE frame) -> `GUN_ANIM_STATE_DRY_FIRE`. The Golden Gun has WEAPONSTATBITFLAG_CLICKY (0x10; runtime bits `0x170f99`), so the IDLE arm turns a held trigger into TRIGGER_PRESS even with an empty magazine (and that also pre-empts the reload check in the same IDLE block). Sequence for a 1-round magazine: FIRE (mag 1->0, shot sound 117) -> next call `88C > 0` -> RECOIL1 -> IDLE after the recoil -> if the trigger is STILL down: TRIGGER_PRESS -> FIRE -> DRY_FIRE + click. The shot's own frame never reaches the DRY_FIRE arm.
**Measured (headless `-level_09`, `GE_STARTWEAPON=19`, `GE_D433LOG` TEMP probe, 60 fps, clk=1 every frame):** tap (4-frame hold): SHOT+FIRESND, no click. Held 9 frames: SHOT gt=872, DRYCLICK gt=886 (14 ticks later, when the recoil ended with the trigger still down). Sustained hold: SHOT, then DRYCLICK every 21 ticks (DRY_FIRE exit). Never a click on the shot tick.
**Why this is the N64 behaviour:** every transition on the path is tick-based (`field_890` accumulates `g_ClockTimer`) or one-shot per state entry (`88C == 0`), so at 2-3 ticks/frame the console gets the same order and the same ~14-tick spacing. `g_ClockTimer` is 0 only when controls are locked or paused, as on N64. `field_A48` stays an 8-byte handle (D208), no layout tear.
**Conclusion:** no port bug found; the click-after-shot happens only when the trigger is held ~0.23 s or longer after the shot (a long click/hold, or a held mouse button), exactly as on the console. If a reporter hears it on every ordinary tap, the suspects are input-side (how long the port holds `triggerOn` for a mouse/pad tap) not game logic; that is a by-ear/input measurement the maintainer owns (ROADMAP row #130). Not settled here: the audible spacing relative to the shot tail, and 1964's behaviour for a held trigger.
**Bucket:** faithful (no `src/game` change, no Rule-2 request needed).
**Probes left on the branch (TEMP, `GE_D433LOG`, cached getenv):** `D433P` macro + SHOT/FIRESND/DRYCLICK/STATS hooks in `src/game/gunfire.c`. Strip before merge.
**Confidence:** high for the code path and the log; medium for "the reporter's trigger-hold explains it" (unmeasured).

**Maintainer check 2026-10-02:** taps never click; a held trigger dry-clicks until release, same as the other guns and the N64. #130 closes (the report predated the D427/D432 fire-rate fixes). CLOSED faithful.
