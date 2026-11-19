# D400

<!-- from the old section-F/H table row -->
| D400 | **File-select "PC Options" label: icon removed, text-only; fixed a swapped textMeasure() arg bug and a 7px vertical misalignment vs Copy/Erase (user sign-off 2026-09-27).** Full `## D400` entry at file tail. | **CLOSED (2026-09-28 bookkeeping: landed with user sign-off, shipped in v0.4.0).** Earlier: LANDED (port-only, `frontoptions.c`): D398's dot icon dropped on user instruction ("most straightforward design"); label now a plain `textRender` word, GAP 28 after Erase. Two measurement bugs fixed: (1) `textMeasure(textheight, textwidth)` args passed swapped -> label 35px above the bar, hot band transposed; (2) bare-label height measures 0 (vs 14 for newline-terminated Copy/Erase) -> 7px lower tops; fixed by restoring the `kLabelNL` height measure. PCDUMP frame 195: tops aligned 279.8 vs 279.3, 31px clearance from Erase; by-eye check **done 2026-09-28 (user: good) -- RESOLVED**. |

## D400 — File-select "PC Options" label: icon removed, text-only; `textMeasure` arg-swap and 7px vertical misalignment fixed (user sign-off 2026-09-27).

D398's ROM-native dot texture next to "PC Options" was removed on user sign-off
("most straightforward, no nonsense" design). The label is now text-only, same
size/look and highlight colour as the Copy/Erase labels: `optionsFileSelectLabel()`
(`port/src/frontoptions.c`) is one `textMeasure` + one `textRender` with the
game's own packed gold `0xEBD879FF` constant — no per-channel unpack, so the
D398 purple-hover bug class (`gDPSetEnvColor` AARRGGBB vs RRGGBBAA channel
unpack) is gone by construction. Removed includes: `image_bank.h`,
`othermodemicrocode.h`, `bondwalk2.h`, `assets/oddtextures.h`; `DOT_W` /
`DOT_MARGIN` gone.

Two measurement bugs found and fixed while landing D400:

1. **`textMeasure` argument swap.** The signature is
   `textMeasure(s32 *textheight, s32 *textwidth, ...)` — height FIRST
   (`textrelated.h:21`, impl `textrelated.c:766-843`). The first D400 rewrite
   passed `(&w, &h)`, drawing the label ~35px above the bar and transposing the
   hot/click band. Fixed to `(&h, &w)`.
2. **7px vertical misalignment vs Copy/Erase.** `textMeasure` only adds to
   `*textheight` on a `\n` character; Copy/Erase's localized strings end in a
   newline → h=14 → y = 285 - 7 = 278 (front.c:2778 does exactly this). The
   bare "PC Options" measures h=0 → y=285 → tops 7px low. Fixed by measuring
   height with `kLabelNL` ("PC Options\n") while rendering plain `kLabel`.

Constants: `LABEL_GAP` 20→28, `LABEL_X` 343→351 (plain-word gap after Erase; no
glyph to clear). Hitbox: `cursor_h [x-4, x+w+4]`, `cursor_v [y-4, y+h+4]` =
[274, 296] at h=14. PCDUMP frame 195 (NTSC): "PC Options" logical x 353..426,
top 279.8 vs Copy/Erase 279.3 (aligned); Erase ends 323 → 31px clearance; 14px
right margin (canvas 440). **Owed: by-eye check via `scratch\playtest_v040.bat`
(idle legibility, gold hover not purple, click opens the F10 overlay, vertical
alignment with Copy/Erase).** Cross-ref D348 (glyph lineage), D398
(superseded icon approach), D399 (bottom-row 15px shift).
