# D363

<!-- from the old section-F/H table row -->
| D363 | **F10 return to a dark overlay with the N64 watch's green/Bank Gothic visual language (2026-09-27).** — full `## D363` entry at file tail | FIXED (palette/font user-approved, 2026-09-27); D364 follows up on profile tags, click alignment and slider calibration. |

## D363 — F10 returns to a dark overlay, restyled after GE's green watch (2026-09-27)

**User feedback on D362:** "make it more like how it was but use the watch
style as a reference — green, that other font etc." The watch's actual
options pages (`src/game/options.c:3524+`, `:3825+`) use Bank Gothic and
mint/bright green ink (`0xA0FFA0F0` selected, muted green unselected).
The paper card, Zurich Bold, gold, red, numbered categories and dossier
branding from D362 were not the desired direction.

**Fix (port-only):** restore the original F10's simple dark translucent
panel and full-screen dim; use the existing Bank Gothic font for all rows,
subdued green for ordinary text and bright watch green for selection,
active values and slider knobs/fill. The `PC OPTIONS` title and close `X`
replace MI6/CLOSE; category names return to their uppercase row labels.
Keep D360's two-level navigation and D362's shared layout/hit geometry,
scrolling and pinned section heading; no setting behavior or save changes.
The mixed-scope profile marker stays short to avoid 320px overlap.

**Verification:** NTSC build/link clean. `GE_OPTIONSOVERLAY=4`,
`GE_PCDUMP=160-160` with native Windows `GE_QUITFRAME=170` passed in both
front/menu and `-level_09` stage contexts at 640x480: dark panel and green
pixels present (front/stage captures in `scratch/d363-test/`), no errors,
orderly exit. User ini and EEPROM restored byte-identically after both.
Live review of colour, text, click/drag/wheel, controller, ESC/close and
F10 Audio remains owed.
**User follow-up:** palette/font approved, but P shorthand, low-contrast
legend, slider mouse hit position and off-centre input defaults need work;
see D364.
