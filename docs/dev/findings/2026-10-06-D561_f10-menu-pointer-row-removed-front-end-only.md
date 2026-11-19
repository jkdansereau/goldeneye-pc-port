# D561

<!-- from the old section-F/H table row -->
| D561 | **F10: "Menu pointer" row removed (front-end-only legacy option; confused players)** — full `## D561` entry at file tail | FIXED 2026-10-06 (row/tip/names removed; `Input.MenuPointerMode` ini key, Direct default and the legacy code path kept for one release) |

## D561: F10 "Menu pointer" row removed — FIXED 2026-10-06 (maintainer request)

`Input.MenuPointerMode` (Direct = 1:1 mouse position, default; Velocity (old) =
D165's mouse-as-analog-stick nudge) only affects the GAME'S front-end menu
cursor: it is read solely under `menuMode` (`port/src/input.c`, "not in a
stage"), never in gameplay, never by the F10 overlay (which handles the mouse
itself), never on pad/Deck paths. Maintainer: an option only a legacy user would
want "will only confuse and cast doubt". Removed the F10 row, its tip and its
value names (`port/src/optionsoverlay.c`); kept the ini key (default Direct, and
its Reset-to-defaults entry) and the Velocity code path so a hand-set ini still
works for this release. Follow-up (next release): delete the Velocity path and
`Input.MenuPointerSpeed`. Verified: build rc 0; in-level Mouse page capture at
1280x720 shows the row gone; tip/hint checks 0 over.
