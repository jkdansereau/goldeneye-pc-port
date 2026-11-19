# D483

<!-- from the old section-F/H table row -->
| D483 | **#136: the PC crouch key stood Bond up when he switched to the sniper rifle; the N64 keeps the stance (the weapon's DISABLE_CROUCH flag blocks both crouch-down and crouch-up)** — full `## D483` entry at file tail | FIXED 2026-10-02 (local, `fix/d483-sniper`); scripted crouch -> switch -> release keeps SQUAT |

## D483 — #136: switching to the sniper rifle stood a crouched Bond up (PC crouch key)

**Report (#136, outside):** "crouching with sniper rifle is not possible".
**Original code:** the sniper rifle's stats carry `WEAPONSTATBITFLAG_DISABLE_CROUCH` (`assets/obseg/gun/gunWeaponStats.inc.c`). `bondview2.c` (both control paths, ~5177-5209 and ~5448-5468) then turns aim-mode C-up/C-down (or stick Y) into zoom and leaves `crouchDown`/`crouchUp` false. So on the N64 you cannot change stance while holding the sniper; you keep the stance you had when you switched to it. The report is therefore partly faithful.
**Port divergence:** the PC crouch key (`port/src/input.c`, the free-crouch block) treated a disable-crouch weapon like "crouch not allowed" and called `inputDropCrouch()`, which stood a port-crouched Bond up as soon as he switched to the sniper.
**Fix (port layer):** while a disable-crouch weapon is out, the free-crouch block does nothing: no new crouch, no stand-up, and no injected C-down (which zooms out with this weapon). Switching back resumes the normal rules. `GE_INPUTLOG` logs `free crouch frozen (weapon N disables crouch, crouchpos=, key=)` once per entry.
**Verified 2026-10-02 (Dam, `GE_STARTWEAPON=17`, scripted CHOLD / weapon switches / CREL, `GE_INPUTLOG`):** crouch key held with the sniper out while standing: frozen, no crouch (`crouchpos=2`); switched to another weapon: crouch applied; switched back to the sniper: frozen while crouched (`crouchpos=0`, SQUAT); key released: no stand-up. Matches the N64 rules above.
**Superseded 2026-10-03 by D494** (GEPD lets the crouch key work with the sniper; the freeze is gone).
