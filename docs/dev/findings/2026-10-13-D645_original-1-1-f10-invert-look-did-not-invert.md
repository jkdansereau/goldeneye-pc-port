# D645

<!-- from the old section-F/H table row -->
| D645 | **Original 1.1: F10 "Invert look" did not invert vertical look (#145 a): D605 flipped the move stick; the game's own Look up/down drives C-button/aim pitch** — full `## D645` entry at file tail | FIXED 2026-10-12 (F10 row and watch row synced in Original; maintainer-verified) |

## D645: Original 1.1: F10 "Invert look" did not invert vertical look (#145 a, follow-up to D605/D643)

**Symptom.** Maintainer playtest 2026-10-12 (Original control style, 1.1): toggling F10 "Invert look" did not change vertical look.

**Cause.** D605 inverted the left stick's Y in the HONEY role. In 1.1 the left stick moves; hip-fire pitch is C-up/C-down (the right stick under D643's strict pad) and the game applies its own "Look up/down" option (`moveData.invertPitch`) to C-buttons and the aim-mode stick. The port's row never reached that path.

**Fix (port only).** `port/src/input.c`: the HONEY-role stick flip is skipped in Original; accessors `inputPadLookInvert` / `inputSetPadLookInvert`. `port/src/watchsettings.c`: in Original, F10 "Invert look" and the watch "Look up/down" row are one setting: whichever changed since the last tick wins, and on stage entry the F10 row wins. Modern scheme unchanged (the D564 pin still applies there).

**Status.** FIXED, maintainer-verified 2026-10-12 on Windows: inverts in 1.1; the F10 and watch rows stay in sync.
