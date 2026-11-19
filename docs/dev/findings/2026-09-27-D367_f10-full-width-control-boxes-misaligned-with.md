# D367

<!-- from the old section-F/H table row -->
| D367 | **F10 full-width control boxes misaligned with their contents (2026-09-27).** — full `## D367` entry at file tail | **CLOSED (2026-09-28 bookkeeping: superseded by D368).** Earlier: SUPERSEDED by D368 after user playtest: per-value boxes remained confusing. |

## D367 — F10 control frames aligned with their actual clickable contents (2026-09-27)

**Reported:** D365's full-width rectangles do not align with their text
contents. Root cause: every row used a fixed `[barX0, right-7)` box despite
right-aligned toggles/enum text near `right-12`, slider bars ending much
earlier at `barX1`, and reset actions having their button in the *label*
column (empty value until armed). This advertised empty space as clickable
and left the reset's real target unframed.

**Fix:** `overlayControlSpan()` now calculates the actual button region per
row: tight around slider tracks, around measured right-aligned values, or
around the reset label; non-interactive/unavailable slider rows get no box.
Both mouse buttons and hover use that same geometry, so moving the mouse
only highlights a genuinely clickable area. The narrower border fits the
Bank Gothic text's vertical footprint. Label clicks on ordinary settings
still only select; clicking the framed reset label invokes its existing
two-step confirmation. No game logic or config/default value changes.

**Verification:** NTSC compile/link passed, native 640x480
`GE_OPTIONSOVERLAY=2 -level_09` stage capture and orderly frame-180 exit;
ini/EEPROM restored byte-for-byte. Mouse click/hover and confirmation remain
to be feel-checked live. User's immediate playtest found remaining boxes
wrong; replaced by the simpler D368 rule.
