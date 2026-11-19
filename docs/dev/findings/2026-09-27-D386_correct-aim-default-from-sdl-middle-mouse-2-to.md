# D386

<!-- from the old section-F/H table row -->
| D386 | **Correct Aim default from SDL middle (Mouse 2) to right (Mouse 3) (2026-09-27).** full `## D386` entry at file tail | **CLOSED (2026-09-28).** Shipped in v0.4.0; accepted in the user's final review / campaign playtest. |

## D386 — Aim default must use SDL Mouse 3, not Mouse 2 (2026-09-27)

**Status: PARTIAL (NTSC build and isolated migration verified; physical retest
owed).** User caught that SDL numbers mouse buttons as left=1, middle=2,
right=3. D385 used `Mouse 2` for Aim, so RMB stopped aiming and MMB took its
place. This was a port binding-default error, not a gameplay change.

**Fix:** default Aim is `Mouse 3,Left Shift`; pre-D385 migrations now put
SDL_BUTTON_RIGHT (3) into Aim. Increment `Input.BindingsVersion` to 3 so a
version-2 ini with the **exact** erroneous default `Mouse 2,Left Shift` is
corrected on launch. Any other version-2 Aim string is left alone, preserving
explicit middle-button assignments and other user changes. Mouse-button
capture/serialization and fixed front-menu clicks still use SDL's 1–5 values.

**Verification:** NTSC MINGW64 build; isolated version-2 ini (Fire custom
`Mouse 5,F2`, Aim old default `Mouse 2,Left Shift`) migrates just Aim to
`Mouse 3,Left Shift` and version 3; version-3 restart is stable. User ini was
not edited for the probe. Live RMB/MMB test and release gate remain open.
