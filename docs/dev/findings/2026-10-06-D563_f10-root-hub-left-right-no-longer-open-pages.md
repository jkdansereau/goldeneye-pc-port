# D563

<!-- from the old section-F/H table row -->
| D563 | **F10 root hub: Left/Right no longer open pages; held nav inputs latched on open/page change** — full `## D563` entry at file tail | FIXED 2026-10-06 |

## D563: F10 root hub: Left/Right no longer open pages; held nav inputs latched

Maintainer: holding the stick/D-pad/arrow on the root menu could enter a page (e.g. PC Options) and
keep acting inside it. (a) On the root hub (`s_section < 0`) header/link and action rows now activate
only on A / Enter (or a click); Left/Right/stick/D-pad/arrows do nothing there. Pages keep Left/Right
value changes. (b) Held-input latch: `s_latchArm` is set when the overlay opens (closed-state branch
of `overlayHandleInputLocked`, covers F10 and the front-end PC Options entry) and on every page
enter/back (`overlayOpenHeader`, `overlayBackOne`); the next poll snapshots held up/down/left/right/
accept/back and each stays masked (`s_latchMask`) until released once, so auto-repeat cannot fire
from a latched input. Verified by code reading + build rc 0 only: the overlay reads SDL pad/keyboard
state directly, which GE_INPUTSCRIPT does not drive, so a headless held-input repro is not possible; maintainer
to check front end (file select -> PC Options) and in-level F10 by hand.
