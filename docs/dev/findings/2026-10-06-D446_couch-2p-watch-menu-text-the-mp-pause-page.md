# D446

<!-- from the old section-F/H table row -->
| D446 | **Couch 2P watch-menu text (the MP pause page) differs by +-1-2 per channel between builds whose only difference is data layout (2026-09-29).** — full `## D446` entry at file tail | FIXED — port-only (fast3d): `GL_TEXTURE_MAX_ANISOTROPY` is per-texture-object state that `gfx_opengl_set_sampler_parameters` set only when mipmapped and never reset; a GL texture id freed by `gfx_texture_cache_delete_range` (unordered_map order = texture address order) and reused for a non-mipmapped glyph kept 4x. Now always written (level or 1). Render-only; sim never affected. |

## D446 — couch 2P watch-menu text differs between builds that only differ in data layout: stale per-texture anisotropy (2026-09-29)

**Status:** FIXED, port-only (`port/fast3d/gfx_opengl.cpp`, `#ifdef PORT`). Render-only; the sim was never involved (state hashes identical throughout).

**Symptom.** The couch 2P script (MULTIPLAYER-DEV §6), frame 2400 (the MP pause/watch menu, "PLAY / RANK: 1ST / SCORES") is not byte-identical between two builds that differ only in data layout:
- 8b080caa vs 8b080caa + an unused 3 KB array in `expinput.c`;
- the controller-pages merge;
- R1's front.c/front.h array widening (programmer 1's bisect).

About 55-922 pixels differ, a few glyphs only, each by +-1-2 (mostly green). Every build is deterministic against itself. A 4 KB array at the end of `expinput.c` did not reproduce it, and neither did the pre-sync base with R1: only some layouts trigger it.

**Method (probes in fast3d, removed).** A (6c8915d7) vs B (A + the 3 KB array), frames 2398-2401:
1. Every texture import (format, size, content hash): **identical** sequences in A and B.
2. Every `gfx_flush` batch (VBO hash, depth mode, blend, viewport, scissor, combine mode, other modes, and for both texture slots the GL texture id, filter, clamp, content hash): **identical except the GL texture ids.** The same content is bound under different ids in A and B.
3. So the difference lives in GL texture-object state.

**Root cause.**
- `gfx_opengl_set_sampler_parameters` (`gfx_opengl.cpp`, the `if (mipmaps) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, current_anisotropy_level);` line) sets anisotropy only for mipmapped samplers and never resets it. Anisotropy is per-texture-object state; `Video.Anisotropy` defaults to 4.
- GL ids are recycled: `gfx_texture_cache_delete_range` (`gfx_pc.cpp`) pushes freed ids onto `free_texture_ids` while walking the `std::unordered_map` in hash-bucket order. The key hash is the texture **address**, so the recycling order depends on where textures live.
- A glyph texture (bilinear, no mipmaps) that inherits an id last used by a mipmapped world texture keeps `MAX_ANISOTROPY = 4`. llvmpipe then filters it slightly differently: the +-1-2 values.
- A layout shift changes which glyph inherits which id, hence which frames differ.
- Nothing in `src/game` is involved: no uninitialised read, no out-of-bounds read, no stale texture content (all ruled out by the probes and the earlier UBSan run).

**Fix.** Always write the anisotropy when sampler parameters are set: the level when mipmapped, 1.0 (off) otherwise, so a recycled id carries nothing over. This is identity for any texture whose id never had a mipmapped owner, which is why the base frames are unchanged.

**Verified (headless, silent, llvmpipe, scratch HOME, mp-sandbox data; 6c8915d7 base):**

| Check | Result |
|---|---|
| Couch 2P frames 2300-2500 (step 10), fix vs fix + 3 KB array (the reproducing variant) | **0 of 21 frames differ** (before the fix: frames 2300-2420 differed) |
| Couch 2P frames 2300-2500, base vs fix | 0 of 21 differ |
| Solo `-level_09` frame 1800, base vs fix | byte-identical |
| Couch 4P frame 2400, base vs fix | byte-identical |
| Netplay 2P (local relay, 60 s) | SH identical over 2 671 ticks |

**Residual risk (not fixed, noted).** Other per-object state can still follow a recycled id:
- mip levels 1..n of a previous owner, if a texture uploaded without mipmaps is later sampled with a mipmapped min filter;
- the free-id order is still address-dependent.

The general fix would reset the whole object on reuse, or sort the ids that `delete_range` frees. Neither is needed for this symptom. **Confidence: [H]** for the cause and the fix (A/B reproduced, fixed, bisected to the one GL parameter).
