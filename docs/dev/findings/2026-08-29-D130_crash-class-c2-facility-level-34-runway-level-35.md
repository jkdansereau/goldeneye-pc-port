# D130

<!-- from the old section-F/H table row -->
| D130 | **crash class C2 (Facility `-level_34`, Runway `-level_35`):** — full `## D130` entry at file tail | resolved — stage all 6 N64 fields in a local `f[6]` before writing any. Facility + Runway now PASS (16→18/21). Probes reverted. |

## D130 — crash class C2 (Facility `-level_34`, Runway `-level_35`):

crash class C2 (Facility `-level_34`, Runway `-level_35`): `import_texture_i8` AV on a wild texture pointer (`0x72181ee8`), baked into the HUD glyph DL by `gDPLoadTextureBlock(gdl, curchar->pixeldata, …)` when the level-title string ("Chemical Warfare Facility #2") renders `#`/`"`. Root cause: `romdataFixupFont` (`port/src/romdata.c`) converts the N64 24B fontchar array to the PC 32B array **in place**, forward-field, backward-glyph. PC−N64 stride = 8, so for glyphs 0/1/2 `d` overlaps `s` by less than the 24B read span and an early field write clobbers a later field's source mid-`for k<5` loop → glyph 1's `width` = `bswap(index)` = `0x01000000`, glyph 2's `pixeldata` = `pcPixOff+(index−pixStart)` ≈ `0x020002e8` → `+= font_base` → wild → fast3d AV. On N64 the array is read-only rodata, never re-laid-out, so no bug there. NOT the model-GDL relocation (`sub_GAME_7F0762E0` / `texLoadFromGdl`) the D124-Facility addendum suspected — `gdl` there is still a segmented `0x05xxxxxx` value so the `& 0x00ffffff` masks are correct, and `texLoadFromGdl` never copies a `G_SETTIMG` for these levels.

**Status (from §F table):** resolved — stage all 6 N64 fields in a local `f[6]` before writing any. Facility + Runway now PASS (16→18/21). Probes reverted.
