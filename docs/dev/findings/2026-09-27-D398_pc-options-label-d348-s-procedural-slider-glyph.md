# D398

<!-- from the old section-F/H table row -->
| D398 | **PC Options label: D348's procedural slider glyph replaced with an existing mainfolderimages texture (2026-09-27).** Full `## D398` entry at file tail. | SUPERSEDED by D400 (icon removed, text-only label; purple-hover bug moot; the D400 `textMeasure` arg-swap bug was found and fixed there). |

## D398 — PC Options label: IMG_DOT texture replaces the D348 procedural slider glyph (2026-09-27)

User request: drop the D348 mini-slider glyph in favour of a real sprite from
the existing `mainfolderimages` table — the option-1 path of the D348
discussion, zero new art. Chose `IMG_DOT` (the table's sixth entry;
`assets/oddtextures.h:57-64`, entry `{IMAGE_DOT, 0x10, 0x10, 0, G_IM_FMT_I,
G_IM_SIZ_8b, ...}` at `assets/oddtextures.c:657`).

Implementation (port-only, `optionsFileSelectLabel()` in `frontoptions.c`):
the D348 `microcode_constructor_related_to_menus` block is replaced with the
exact Copy/Erase pattern (`src/game/front.c:2816-2848`):
`texSelect(&gdl, mainfolderimages + IMG_DOT, 4, 0, 0)` +
`display_image_at_position(&gdl, &dotpos, &dothalf, 16, 16, 0, 0, 1, r, g, b,
0xFF, 0, 0)`. Two signature gotchas: `texSelect` (`game/othermodemicrocode.h`)
*and* `display_image_at_position` (`game/bondwalk2.h:6`) both return `void`
and advance the DL in place through `Gfx **` — front.c never assigns their
return values either, so neither is assigned here. IMG_DOT is already in
ROM's Globalimagetable (D39 table rebase covers the whole `s_mainfolderimages`
array), so no asset-pipeline work. Layout: dot centred on `LABEL_CY`, right
edge keeps the old glyph's 2px margin before the label (`DOT_MARGIN`), so it
tracks the dynamic `x` (JP's wider Erase pushes it along) and text position is
unchanged; `GLYPH_W` is superseded by `DOT_W` (16) / `DOT_MARGIN` (2) and the
hot region's left edge extends to the dot's left edge. I8 texels are
multiplied by vertex RGB, so the hot state passes the label's gold per channel
(0xEB/0xD8/0x79, the same tinting trick front.c:3829 uses for its red I8
tint), white otherwise. No texture-state leak: front.c re-binds its own
Copy/Erase/Select icons immediately after this hook returns. No new strings
(issue #87).

**Verification:** `./build-pc.sh ntsc-final` clean (first pass failed on the
`gdl = display_image_at_position(...)` assignment — void return; fixed).
Visual check owed: the dot's actual appearance is the ROM's `IMAGE_DOT`
texture (D348's audit noted nothing in the game art reads as "settings"; the
dot is the user-chosen compromise, not a settings icon).
