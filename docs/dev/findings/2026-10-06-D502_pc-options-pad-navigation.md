# D502

<!-- from the old section-F/H table row -->
| D502 | **PC Options pad navigation: highlight on enter/back/page, back lands on the origin section, crosshair snap moved left; Trilinear listed after Bilinear** — full `## D502` entry at file tail | FIXED 2026-10-03, maintainer-verified with a pad |

## D502 — PC Options pad navigation: no highlight on entering, wrong section on leaving (2026-10-03)

**Report:** maintainer (pad): entering a section highlights nothing; leaving can land on another section (leave Input, land on Graphics); the snapped crosshair overlaps the row text.
**Cause (`port/src/frontoptions.c`):** `goBack` snapped the cursor to the origin section but left `s_hl` at the row index of the sub-page (row 1 inside Input -> section 1, Graphics); the snap also updates the "previous cursor", so hover never corrected it. Entering set `s_hl = -1` for every input device.
**Fix (port only, commit fa0adbf2):** `navHighlight(k)`: pad/keyboard (pointer not live) get item k highlighted on enter, back and page turn; with a live mouse the pointer still owns the selection. Back lands on the origin section/row. Crosshair snap x 44 -> 32, `ROW_HIT_X0` 40 -> 24. Same commit: the Texture filter row cycles Nearest, Bilinear, Trilinear, 3-Point (stored 0, 1, 3, 2) (D499).
**Verified:** build only.
**Owed:** none.
**Status:** FIXED, maintainer-verified 2026-10-03.
**Maintainer pad check 2026-10-03:** first item highlighted on entering, back lands on the origin section, crosshair clear of the text.
