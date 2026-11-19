# D574

<!-- from the old section-F/H table row -->
| D574 | **Legacy front-end menu-pointer Velocity mode deleted (`Input.MenuPointerMode` / `Input.MenuPointerSpeed`); retired ini keys dropped silently** — full `## D574` entry at file tail | FIXED 2026-10-07 (D561 follow-up; build + ini runtime check) |

## D574: legacy menu-pointer Velocity mode deleted — FIXED 2026-10-07 (D561 follow-up)

D561 removed the F10 row for `Input.MenuPointerMode` but kept the key and the
D165 velocity path for one release. Now deleted: the `menuMode &&
!menuPointerMode` branch in `port/src/input.c` (mouse velocity fed to the
front-end cursor as stick input), `MENU_POINTER_GAIN`, both config
registrations, and the `kResetDefaults` row in `optionsoverlay.c`. The Direct
(1:1) pointer is the only behaviour, as it has been by default. Existing inis
still carry the two keys, and an unregistered key would surface the D472
"unknown key" notice in F10, so `port/src/config.c` gains `kRetiredKeys[]`:
those keys are dropped without a log line or notice, and `configSave` (which
writes only registered keys) removes them on the next save.

Implemented by the local model from a scoped brief; reviewed by the lead.
**Verified:** build rc 0; grep leaves only the two retired-key strings; a
Windows run with an ini holding `MenuPointerMode = 0` / `MenuPointerSpeed = 150`
logged no unknown key and wrote the ini back without them.
