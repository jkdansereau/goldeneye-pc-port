# D365

<!-- from the old section-F/H table row -->
| D365 | **F10 annotation hierarchy and visible mouse change targets (2026-09-27).** — full `## D365` entry at file tail | **CLOSED (2026-09-28).** Shipped in v0.4.0; accepted in the user's final review / campaign playtest. |

## D365 — F10 annotations and bounded mouse-change controls (2026-09-27)

**Reported:** `(profile)` is visibly larger than its setting label; the
Controls legend should read `Controls:` with smaller following instructions;
it isn't visually clear that a label click *selects* but the right column
*changes* a setting.

**Fix:** Bank Gothic (the smaller watch font already used for row labels)
now renders dim profile tags and section scope notes instead of the larger
Zurich Bold. The footer's `CONTROLS:` heading keeps Zurich Bold, but its
instructions use smaller, dim Bank Gothic, including `BOX: CHANGE` within
sections. Draw subtle green-bordered dark control boxes around every section
row's right-hand interactive area, brightening the hovered box without
moving keyboard selection. Restrict left/right mouse changes to the same
bounded x region; label clicks still select only. Coordinates are mapped
through `gfx_get_ui_screen_rect` for hover as for input, preserving the
safe-area crop behavior (D316). No game logic or saved data touched.

**Verification:** NTSC compile/link and 640x480 `GE_OPTIONSOVERLAY=2`
`-level_09` frame-160 capture with clean frame-180 quit; ini and EEPROM
restored byte-identically after testing. During compilation the user's
existing game process held the exe, so it was first linked to a staged
scratch path rather than overwritten; final build linked directly after
that process exited. Mouse hover/click in a live window is still owed.
