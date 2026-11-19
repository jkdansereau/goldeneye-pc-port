# D474

<!-- from the old section-F/H table row -->
| D474 | **fast3d micro-optimisations: unchanged `set_tile`/`set_tile_size` no longer dirty the textures (each dirty = cache lookup + `gfx_flush()` batch break); per-triangle/per-vertex debug hooks (D75D/ZF/D303) behind one cached gate** — full `## D474` entry at file tail | FIXED 2026-10-01 (local); output-identical by construction; measured gain on the dev machine within noise |

## D474 — fast3d micro-optimisations from the 2026-10-01 profile (low-end, #92)

**Changes (`port/fast3d/gfx_pc.cpp`, output-identical by construction):** (1) `gfx_dp_set_tile` compares the post-normalisation tile fields with the stored ones and only sets `rdp.textures_changed[]` when something differs; `gfx_dp_set_tile_size` returns early when uls/ult/lrs/lrt are unchanged. A dirty flag costs a texture-cache lookup and forces `gfx_flush()` (a batch break, i.e. an extra draw call) on the next textured triangle. Texture/TLUT loads and G_TEXTURE still dirty the textures through their own paths, so a real change is never missed. Narrow stored fields compare conservatively (out-of-range args count as changed). (2) A single cached `tri_dbg()` gate (non-empty `GE_D75D`, `GE_ZF` or `GE_D303`, the same semantics as each hook's own lazy init) now guards the per-triangle emit/reject hooks and the per-G_VTX note, which previously each cost a call plus a frame-counter read even when disabled.

**Measured** (`GE_PERFSTAT`, Statue, the same sweep and settings as D473): display-list time in comparable windows 0.46 -> 0.42 and 0.55 -> 0.54 ms (one window 1.57 -> 0.80), batches 255 -> 254. On this machine the gain is within run-to-run noise: redundant tile setters rarely broke batches in this scene. Kept because it is free and the batch/lookup savings scale with weaker CPUs and per-draw driver cost. Frame 1350 dump checked by eye: normal textured rendering.
