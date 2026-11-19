# D568

<!-- from the old section-F/H table row -->
| D568 | **F10 dropdown popup drawn empty and misplaced for a long list on a mid-page row (negative top)** — full `## D568` entry at file tail | FIXED 2026-10-06 (port only: `ddGeom` keeps the popup on screen; 1280x800 before/after capture) |

## D568: F10 dropdown popup drawn empty and misplaced for a long list on a mid-page row — FIXED 2026-10-06 (port only)

**Report (maintainer, Deck, 2026-10-06):** the Crosshair color dropdown opened
as an empty box with its items drawn too high. **Cause:** `optionsoverlay.c
ddGeom` opens the popup below the row, or above it when there is no room below;
with a 9-item list on a row in the middle of a scrolled page neither fits, and
the upward top went negative. The labels then drew above the screen while the
fill (negative top) ran from the row down past the bottom edge. Reproduced on
Windows at 1280x800 (Game page scrolled to Crosshair style, Custom colour), so
not Deck-specific. **Fix:** if the top is still off screen, the popup is placed
flush with the bottom margin (it always fits: at most 9 rows). Applies to every
dropdown. Verified by a 1280x800 in-level capture before/after.
