# D567

<!-- from the old section-F/H table row -->
| D567 | **Front-end PC Options with a pad pulled the game's crosshair to the top-left (overlay synced the cursor to the untouched OS mouse)** — full `## D567` entry at file tail | FIXED 2026-10-06 (port only: sync gated on `optionsOverlayMouseActive()`; build-verified Win/Linux; Deck pad check owed) |

## D567: Front-end PC Options with a pad pulled the game's crosshair to the top-left corner — FIXED 2026-10-06 (port only)

**Report (maintainer, Deck, 2026-10-06):** opening *PC Options* from the main
menu with a controller moved the front end's own crosshair cursor to the top
left. **Cause:** while the F10 overlay is open over the front end,
`port/src/input.c` called `overlayFrontEndCrosshair()` every frame (D519/D555:
the game cursor tracks the OS mouse so the overlay pointer sits exactly on it).
With a pad the OS pointer is never moved and reads 0,0, so the game cursor was
clamped to the top-left corner of the screen box. **Fix:** the sync runs only
while the mouse is the active input in the overlay (`s_mouseActive`, which
starts at 0 on open and becomes 1 on a mouse move or click; new accessor
`optionsOverlayMouseActive()`). With a pad the cursor stays where it was; mouse
users get the same tracking as before. **Verify:** builds on Windows and Linux
(X220); deployed to the Deck play folder 2026-10-06 21:03; maintainer pad check
owed (open PC Options from file select with the pad, the crosshair stays put).
