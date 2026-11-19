# D558

<!-- from the old section-F/H table row -->
| D558 | **F10 overlay tips cut off in a level (not on the front end)** — full `## D558` entry at file tail | FIXED 2026-10-06 (`optionsoverlay.c` `textRenderWide`; verified by in-level capture) |

## D558: F10 overlay tips cut off in a level, fine on the front end — FIXED 2026-10-06

**Symptom.** At 1280x720, AspectMode=1, in a level, long Game-page tips ("Hides the
white flash when you take dama", "Reverse inverts up and down, like Invert lo") were
cut off; the front end showed every tip whole.

**Root cause (not the aspect/HUD geometry).** Card geometry is identical in both
contexts (the overlay lays out on its own canvas: `ovW()` x `ovH()`, card/tip budget
from the same `overlayLayout()`; tip wrap, centring and clip all use it).
The cut is in the game's `textRenderGlyph` (`src/game/textrelated.c:241`): it drops any
glyph whose UNSCALED x is `> viGetX()` (or y `> viGetY()`) -- the game's 2D canvas, 320x240
in a level, 440x330 on the front end. `drawBody` draws through the 78% span scale about
the string start, so the x the game tests is the start plus the unscaled advance (drawn
width / 0.78): a tip starting at x=46 died at 320 (214 drawn units in), on the front end
the 440 limit hid it. Right-aligned values and, at HUD 75 (ovW 427), more text were exposed
to the same limit.

**Fix (port layer only).** `port/src/optionsoverlay.c` `textRenderWide()` widens the
canvas the game tests (`viSetXY`, restored right after) for the duration of each
`textRender` call, to the overlay's unscaled layout extent (`ovW()*100/OV_TEXT_PCT`).
`tipSelfCheck` already used the narrowest reachable budget (HUD 150 -> card 197, av 189);
unchanged, still 0 over 2 lines.

**Test hooks.** `GE_OPTIONSOVERLAY_ATFRAME=<n>` re-applies the GE_OPTIONSOVERLAY /
_SECTION request at emit n (survives level load; read once, cached);
`GE_OPTIONSOVERLAY_SELECT=<row key>` selects a row on the opened page.

**Verified** by in-level captures (`-level_09`, GE_INPUTSCRIPT="20:START", ATFRAME=200):
before: NoHitFlash and Bond.Look tips clipped at the same screen x; after: whole and
centred at HudScale 100 (AspectMode 1 and 0) and wrapped to two centred lines at HudScale 150.
