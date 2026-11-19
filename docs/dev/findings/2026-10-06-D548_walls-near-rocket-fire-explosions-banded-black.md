# D548

<!-- from the old section-F/H table row -->
| D548 | **Walls near rocket fire/explosions banded black/white/yellow/blue (Aztec dark corridor; also seen on Deck)** — D540 `noperspective` combiner inputs extrapolated past 1.0 by GPU near-clipping, then the CC wrap emulation wrapped them; full `## D548` entry at file tail | FIXED 2026-10-06 (gfx_opengl.cpp input clamp; root fix = D543 CPU near clip; maintainer Aztec re-check owed) |

## D548: walls near rocket fire banded black/white/yellow/blue in dark areas (Aztec; also seen on the Deck) — FIXED 2026-10-06 (maintainer screenshots, A/B)

**Report:** in Aztec's dark corridor, walls beside Bond near rocket fire and
explosions turned into posterised white/yellow/blue/black bands following the
texture detail. `GE_SHADEPERSP=1` (perspective shade) did not reproduce it.

**Cause:** D540 made every combiner input varying (`vInputN`) `noperspective`.
When the GPU clips a big near-camera triangle, those screen-affine values can
be extrapolated past 1.0; the GLideN64-style CC wrap emulation
(`WRAP(texel, -1.01, 1.01)` / `(-0.51, 1.51)`, gfx_opengl.cpp) then wrapped the
out-of-range result, per channel -> the bands.

**Fix:** the fragment shader clamps each combiner input to 0..1 into a local
(`cInputN`) before the combine. N64 shade/prim/env are 8-bit, so this only
removes values the hardware cannot produce. Root fix: D543's CPU near-plane
clip (no GPU near-clipping of affine attributes). Maintainer Aztec re-check owed.
