# D348

<!-- from the old section-F/H table row -->
| D348 | **File-select "PC Options" label: no settings icon exists in the game art (mainfolderimages = copy/del/select-arrow/X/check/dot only) -- a mini slider glyph is drawn instead (2026-09-27).** — full `## D348` entry at file tail | **CLOSED (2026-09-28 bookkeeping: superseded by D398).** Earlier: SUPERSEDED by D398 (glyph replaced with the IMG_DOT texture; no new art). PCDUMP pixel-verified when shipped (135 knob px vs 9 paper). |

## D348 — File-select "PC Options" label gets a mini slider glyph (2026-09-27)

User request: an icon next to the "PC Options" bottom-bar label, like Copy/Erase
have theirs. There is no settings-style sprite anywhere in the game art — the
`mainfolderimages` bank (`assets/oddtextures.h:57-64`) holds exactly six:
COPY, DEL, SEL (select arrow), X, CHECK, DOT. Per user choice, a glyph is drawn
instead of borrowing an off-meaning sprite.

Implementation (port-only, `optionsFileSelectLabel()` in frontoptions.c):
three 2px menu-box tracks with offset 3×4 knobs (a miniature slider control),
emitted with the same `microcode_constructor_related_to_menus` primitive the
options screen uses for its slider bars. Tracks are `INK_DIM`; knobs match the
label ink (white, gold while hot). The glyph is 10px wide (`GLYPH_W`) and sits
INSIDE the existing 14px `LABEL_GAP` after Erase (2px margins each side), so it
tracks the dynamic label x — JP's wider Erase pushes glyph + label together —
and the text position / right-edge clearance are unchanged. The hot region
extends left to include the glyph. No new strings (issue #87).

**Verification:** `build-pc.sh ntsc-final` clean; headless file-select capture
(`GE_STARTMENU=5 GE_PCDUMP=200-201`, 1088×816 = 440×330 canvas × 2.473): the
glyph box (logical x 339–351, y 278–292) contains 135 pure-white pixels — the
three knobs — vs 9 on an equivalent empty-paper strip. Visual feel-check owed
to the user (proportions/knob offsets are one-constant tweaks if it looks off).
