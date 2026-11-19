# D351

<!-- from the old section-F/H table row -->
| D351 | **Existing D346 value/unit rows had positional initializers shifted into `hidePtr`: %/x rows hidden or formatted without units.** — full `## D351` entry at file tail | FIXED (port-only): designated row initializers restore unit, deadzone divisor and hide-if-auto fields; build clean, headless smoke clean. Manual settings visual check owed. |

## D351 — D346 unit-row initializers shifted into `hidePtr` (2026-09-27)

The existing positional `struct Row` initializers for FOV, anisotropy, draw/LOD distance, HUD scale, deadzone and trigger threshold omitted the `hidePtr` placeholder. GCC warned about assigning a `char *` unit ("%"/"x") to `int *hidePtr` and assigning deadzone's numeric display divisor to `unit`. The visible-list builder dereferences non-null hidePtr, so these rows were hidden or misformatted despite D346's intended unit labels. Corrected *only* these rows to designated initializers, preserving their key, label, clamp, step and intended `hiddenIfOn`/unit/divisor. New watch rows also use designated initializers to prevent repeats. Build has no new optionsoverlay warnings; headless menu smoke/golden gate clean. Maintainer reports the implementation tests good, but did not separately report a unit-label/auto-hide visual check; carry that into the new-session review.
