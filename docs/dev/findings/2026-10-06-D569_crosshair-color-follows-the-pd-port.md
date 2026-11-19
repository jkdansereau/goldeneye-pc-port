# D569

<!-- from the old section-F/H table row -->
| D569 | **Crosshair color follows the PD port: Original or Custom RGB, left/right stepper, 0-255 sliders** — full `## D569` entry at file tail | CHANGED 2026-10-06 (maintainer request; old presets migrate to Custom RGB) |

## D569: Crosshair color follows the PD port: Original or Custom RGB, left/right stepper — CHANGED 2026-10-06 (maintainer request)

The row was a 9-entry dropdown of named presets (Green, Red, Blue, Yellow, Cyan,
Magenta, White) plus Custom, with the Custom R/G/B sliders shown as percentages
and moving one unit per press. The PD port has no presets: a colour page with
0-255 sliders. GE keeps **Original** (the authored red sprite, drawn untouched,
which no tint reproduces) and **Custom** (stored 8, the existing silhouette tint
path). The row is now a left/right (or A) stepper with no popup; Red/Green/Blue
show 0-255 and step by 5; the existing value swatch is the live preview; new tip.
**Migration (`video.c videoInit`):** an ini holding a former preset 1..7 becomes
Custom with that preset's RGB, so no saved colour changes (checked: 3 -> 8 with
64/64/255). Display-mode presets and resets still use 0 (Original).
