# D562

<!-- from the old section-F/H table row -->
| D562 | **F10 crosshair pointer hidden while a pad/keyboard drives the menu** — full `## D562` entry at file tail | FIXED 2026-10-06 (`drawCrosshairPointer` draws only while `s_mouseActive`) |

## D562: F10 crosshair pointer hidden while a pad/keyboard drives the menu

Maintainer: the pointer "just hovers over and blocks UI elements" when a controller drives the menu.
`drawCrosshairPointer()` (`port/src/optionsoverlay.c`) now returns early unless `s_mouseActive`
(the existing overlay state: set by mouse move/click, cleared by pad/D-pad/stick/keyboard nav,
accept, wheel and on open), so the pointer reappears on the next mouse movement. No second tracker.
The OS cursor stays hidden while the crosshair pointer mode is on (`inputApplyMouseRequests`
hides it whenever `cfgCrosshairCursor && optionsOverlayIsOpen()`), so no arrow appears.
Front end: the game's own D519 cursor (`overlayFrontEndCrosshair`, `input.c`) is left as is: it sits
UNDER the overlay card (the card does not let it block rows) and is clamped by the game each tick, so
there is no clean port-side park (the -1000 attempt is not repeated). Build rc 0.
