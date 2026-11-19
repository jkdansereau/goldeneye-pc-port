# D500

<!-- from the old section-F/H table row -->
| D500 | **Aztec shuttle area: polygons "fell apart"; the per-frame vtx pool (`dyn.c`, no bounds check, Aztec `-mvtx40`) could overrun into the half the render worker draws; moved to 2x384 KB at DRAM +0x700000 with a budget log** — full `## D500` entry at file tail | FIXED 2026-10-03, maintainer-verified at the Aztec spot |

## D500 — Aztec, by the shuttle (4 turrets + glass): polygons "fell apart" before the glass was shot (2026-10-03)

**Report:** maintainer playtest (D486 turret check, 60 fps): random triangles across the area in front of the glass between two turrets; recovered when walking on, and after the glass was shot.
**Cause (code-established, not confirmed live):** `src/game/dyn.c`'s per-frame "vtx" pool (`dynAllocateVertices`, `dynAllocateMatrix`, `dynAllocateLights`, `dynAllocate`) is a bump allocator with no bounds check, sized per level by `src/boss.c` `memallocstringtable` `-mvtx` (Aztec and Streets 40 KB per half, most levels 50, MP maps 100). The port's wider FOV / draw distance / LOD (porting-notes D18) puts more models and matrices in view than that budget assumed, so a busy frame can run past its half into the other half, which the D481 render worker may still be drawing. Code map: Qwen Q4 (glass shards, autogun beams, bullet sparks and model matrices all draw from this pool; spot-checked).
**Fix (`#ifdef PORT`, commit f7868308; N64 branch unchanged):** `g_VtxBuffers` point at a fixed pool of two 384 KB halves at `PORT_DRAM_V1_BASE + 0x700000` (free emulated DRAM above the D95 mempool end, below `animations_frame_buffer` at +0x7FFD30), so addresses stay 0x70xxxxxx and survive every s32 path. A first version placed it in the executable's .bss (above 4 GB); rejected before commit for that reason. The stage-bank allocation is no longer made. `dynSwapBuffers` logs once per level `D500: vtx pool: frame used N bytes, N64 budget B` when a frame exceeds the N64 budget, and `D500: vtx pool OVERRUN` past the port pool.
**Class:** porting-notes D18; cross-ref D95 (gfx pool scaled for 16-byte Gfx).
**Verified:** build; `-level_09` intro renders (Bond model) and exits cleanly. Maintainer sessions afterwards (Aztec, Cradle, ultrawide): no `D500` line logged, no crash.
**Owed:** none.
**Status:** FIXED, maintainer-verified 2026-10-03.
**Maintainer check 2026-10-03:** the Aztec shuttle spot (glass intact) no longer breaks up.
