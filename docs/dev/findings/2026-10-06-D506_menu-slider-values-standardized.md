# D506

<!-- from the old section-F/H table row -->
| D506 | **Menu slider values standardized: real units everywhere, no `n/100` or `n/255` scales** — Display only; D505 follow-up; full `## D506` entry at file tail | FIXED (port-only; by-eye pass owed) |

## D506: slider values standardized (display only)

Port-only (`port/src/optionsoverlay.c`; both UIs share `valueText`). Maintainer asked for matching values across rows and no odd defaults (e.g. `41/200`). The calibrated "50/100 at the default" mapping (`calibratedDefault`) is retired (now returns "none"); every slider is a linear bar in a real unit. Storage, ini keys, ranges and defaults are unchanged.

**Now shown as:** multipliers (raw 100 = 1.0x) for Horizontal / Vertical sensitivity and Horizontal / Vertical look sensitivity (vertical mouse and both pad look rows gained linear bars; mouse vertical uses UI range 10..300 like the horizontal row); `%` for Trigger threshold (raw is already a percent, default 23%), Look smoothing (raw 0-10 -> 0..100%), deadzones, volumes, vibration, shake, smoothing, crosshair size, HUD scale and the crosshair R/G/B channels (percent of full); `x` for Draw / LOD distance (ini 100..800 -> 1.0x..8.0x, default 2.5x), anisotropy. Field of view stays in degrees (it is a real unit and depends on the window aspect). The unused `dispMax` row field was removed.

**Verified (headless):** build rc=0; `GE_OPTIONTREEPROBE` PASS; `GE_WSPROBE_RESET` 6/6 sections 0 failures; F10 Mouse / Controller / Video / Game dumps show the new values (`scratch/vals_sheet.png`). **Owed (maintainer):** drag each changed slider with mouse and pad, both UIs; the vertical-mouse and pad-look bars now start at ~35-57% instead of the middle.
