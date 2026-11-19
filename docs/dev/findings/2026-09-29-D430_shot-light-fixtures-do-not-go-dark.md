# D430

<!-- from the old section-F/H table row -->
| D430 | **Shot light fixtures do not go dark: `lightfixture.c` `extract_vertex_indices_from_triangle` is an unported N64 GBI parser (issue #119 / D415).** — full `## D430` entry at file tail | FIXED in tree (D154-style words.w0/.w1 decode); user-verified vs GEPD/1964 on Caverns 2026-09-29. |

## D430 — Shot light fixtures do not darken: `extract_vertex_indices_from_triangle` reads the N64 8-byte Gfx layout from the 16-byte PC Gfx (issue #119, the D415 report)

**Root cause (code reading; D154/D135/D312 class, porting-notes B):** `lightFixtureBreak` -> `darken_triangle_in_room` / `extract_vertex_coords_from_triangle` all get their three vertex indices from `extract_vertex_indices_from_triangle` (`lightfixture.c`). Every case reads the display-list command as raw N64 storage -- `gfx->tri.tri.v[k]` for G_TRI1, `((u32*)gfx)[1]`, `((u8*)gfx)[7]`, `((u16*)gfx)[3]` ... for the four G_TRI4 triangles. The room DL on PC is the widened 16-byte Gfx (D85), where the two N64 words live in the low dwords of `words.w0`/`words.w1`, so all reads returned garbage (effectively index 0). A shot fixture therefore darkened vertex 0 of its vertex batch instead of its own vertices (and the glass-shard positions and "neighbouring triangle" search used the same wrong vertices). The sibling parser in `bg.c` (bullet hit test) was fixed for this in D154; `lightfixture.c` was missed.
**Fix (`#ifdef PORT`, no logic change):** decode from `words.w0`/`w1` with exactly D154's nibble mapping (case 0 = G_TRI1 bytes 5-7 / 10; cases 1-4 = G_TRI4 s2 0..3). The original N64 behaviour of ignoring the G_VTX v0 offset is kept. Each of the 15 index expressions was checked against the original raw-byte access. **No headless proof** (needs a shot at a fixture); verification is the Bunker/Caverns playtest.
**Note:** this explains "shooting the fixture does not kill the light". I found no flicker mechanism in the game code (the light system only darkens vertex colours >>2 and spawns glass), so if a flicker persists after this fix it is a separate effect.
**Status:** FIXED; user-verified vs GEPD/1964 (Caverns) 2026-09-29.
