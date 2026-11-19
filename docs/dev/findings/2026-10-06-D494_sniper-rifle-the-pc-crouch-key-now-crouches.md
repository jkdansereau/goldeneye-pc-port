# D494

<!-- from the old section-F/H table row -->
| D494 | **Sniper rifle: the PC crouch key now crouches/stands freely with a disable-crouch weapon (GEPD parity), never injecting C-down for it; supersedes D483's stance freeze** — full `## D494` entry at file tail | FIXED 2026-10-03, maintainer-verified in play |

## D494 — Sniper rifle: the PC crouch key now crouches/stands freely with a disable-crouch weapon (2026-10-03)

#136 follow-up; supersedes D483's freeze.
**Report:** maintainer, controller and keyboard, hold and toggle crouch; 1964 GEPD lets Bond crouch and stand freely with the sniper.
**Cause:** D483 froze the port's free-crouch stance while a `WEAPONSTATBITFLAG_DISABLE_CROUCH` weapon is out, matching the N64 pad (`bondview2.c` turns aim-mode C-up/down into zoom for that weapon). GEPD's crouch key (the mouse injector, the reference for this preset) writes the stance directly and ignores the flag.
**Fix (port only, commit 0f24f407):** the free-crouch block (`port/src/input.c`) now crouches/stands for every weapon; for a disable-crouch weapon it never injects C-down (that would zoom out) and the "keep native C-down crouch" release exception does not apply. The N64 pad rules are untouched.
**Verified:** build only.
**Owed:** none.
**Status:** FIXED, maintainer-verified 2026-10-03.
**Maintainer play check 2026-10-03:** crouch and stand with the sniper fixed.
