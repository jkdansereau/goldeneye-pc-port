# D570

<!-- from the old section-F/H table row -->
| D570 | **Scaled crosshair (Crosshair size != 100%) showed a faint copy of the top bar's tip under the bottom bar (WRAP tile flags)** — full `## D570` entry at file tail | FIXED 2026-10-06 (port only: CLAMP when scaled in `gunDrawSight` + F10 pointer; 100% unchanged, Dam gate PASS) |

## D570: Scaled crosshair showed a faint speck under the bottom bar — FIXED 2026-10-06 (port only)

**Report (maintainer, Deck, 2026-10-06):** a tiny line just under the bottom of
the crosshair, with Custom and default colour. **Cause:** only at a Crosshair
size other than 100% (the maintainer's ini: 105). The sight's render tile uses
the image table's WRAP flags; a scaled `gSPTextureRectangle` puts the last pixel
row/column between the last texel and the next, so the filter blended in the
opposite edge (the top bar's tip): a 2-px faint copy ~6 px under the bottom bar.
The N64 never scales the sight, so 100% is clean (checked). Reproduced on
Windows (Custom green, 105%: rows 460-461), so not Deck-specific. **Fix:**
`gunfire.c gunDrawSight` (`#ifdef PORT`, presentation only) passes a copy of the
image-table entry with CLAMP flags to `texSelect` when the scale is not 1.0; at
100% the original call is unchanged (Dam gate PASS). The F10 crosshair pointer
(`optionsoverlay.c`, always non-integer scale) gets the same clamp.
**Verified:** 105% Custom green capture clean below the bar; Dam gate PASS.
