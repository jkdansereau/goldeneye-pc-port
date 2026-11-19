# D544

<!-- from the old section-F/H table row -->
| D544 | **OS cursor flickers with F10 open on the main menu** — show/hide requests race (inputSuspendForOverlay vs the D519 front-end pointer); full `## D544` entry at file tail | FIXED 2026-10-05 (port-only, input.c; build-verified, maintainer mouse check owed) |

## D544: OS mouse cursor flickers with the F10 overlay open on the main menu — FIXED 2026-10-05 (maintainer report, same day)

**Report:** "sometimes the mouse cursor flickers when the F10 menu is open on
the main menu".

**Cause (code read, not yet run):** two requests race every overlay tick.
`inputSuspendForOverlay()` (`port/src/input.c` ~3155, called from
`overlayHandleInputLocked`, `optionsoverlay.c` ~2300) always calls
`mouseRequestCursor(1)` (show the OS cursor); on the front end the D519 path
(`input.c` ~2174: `overlayFrontEndCrosshair(); mouseRequestCursor(0);`)
hides it because the game crosshair is the pointer there. Whichever request is
applied last on the event thread (`inputApplyMouseRequests`) wins, so the
cursor toggles. In a level only the show request runs (no flicker).

**Proposed fix (port-only, ~3 lines):** in `inputSuspendForOverlay`, skip
`mouseRequestCursor(1)` when the D519 front-end pointer is active
(`mouseEnabled && current_menu != GE_MENU_RUN_STAGE && current_menu !=
GE_MENU_INVALID`), so only one owner sets the cursor per context. Verify:
F10 on the main menu with a mouse -- one pointer (the crosshair), no flicker;
F10 in a level -- OS cursor visible as before.

**Fix (2026-10-05/06, revised after the maintainer check):** the first cut
(skip the show request while the D519 pointer is active) stopped the flicker,
but the maintainer wants the pointer drawn OVER the overlay for mouse users.
Final: `inputSuspendForOverlay` always shows the OS cursor, and the D519
front-end path no longer hides it (the game crosshair keeps tracking the
mouse underneath the box); one owner, so no race. Same pass, F10 mouse QoL
(`optionsoverlay.c`): right-click = Esc/back (closes a dropdown, then one
page, then the overlay; replaces the old right-click "cycle value back";
blocked until release after a binding capture so a bound right-click does
not also back out), and the tip line follows the hovered row while the mouse
is in use. Maintainer check owed. Mouse scrollbar: backlog (post-release QoL).
