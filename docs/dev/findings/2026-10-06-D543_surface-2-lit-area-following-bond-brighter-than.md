# D543

<!-- from the old section-F/H table row -->
| D543 | **Surface 2: lit area following Bond brighter than 1964** — full `## D543` entry at file tail | FIXED 2026-10-06 (CPU RSP-style near clip in gfx_sp_tri1 + per-vertex fog default; build-verified, by-eye vs 1964 at next playtest) |

## D543: Surface 2 — the lit area that follows Bond looks brighter than on 1964 — OPEN (2026-10-05)

**Report:** after D540, the ground around Bond on Surface 2 reads as a bright
light following him; 1964/GEPD shows it dimmer. Maintainer wants the
accurate result, not a tuned one.

**Leading hypothesis:** D540's exact per-pixel fog leaves the near ground
unfogged, while the N64 (and 1964) shade per vertex and the RSP near-clips in
clip space, so the near-plane edge of a big ground triangle inherits fog lerped
from a corner behind the camera — a haze at the player's feet. D540 dropped
per-vertex fog on the GPU because GPU clipping of noperspective/RSP values
made that area cycle light/dark. **Accurate fix to evaluate:** RSP-style
near-plane clipping in fast3d (clip fogged triangles against the near plane on
the CPU, lerping per-vertex fog in clip space like the RSP), then per-vertex
fog screen-affine — N64 behaviour without the cycling; compare against 1964
at Bond's start on Surface 2. Second hypothesis to rule out: GE's own near-fog
record (`Visibility.Nfd` / `g_NearFogValuesP`, bgfog.c). `GE_FOGVERTEX=1`
shows the per-vertex look today (with the cycling).

**Implementation (2026-10-06):** CPU near-plane clip added in `gfx_sp_tri1` (`port/fast3d/gfx_pc.cpp`, D543 comment block before the emit loop): after the reject/cull gates, triangles are Sutherland-Hodgman clipped against z+w>eps in clip space (3 or 4 verts, fanned to 1-2 triangles), new vertices lerp x/y/z/w/u/v/colour/fog_n linearly (stack storage, not `rsp.loaded_vertices`); fully-behind triangles are dropped. Skipped for rects, `G_NO_CLIPPING_EXT`, `GE_NEARCLIP=0`. `GE_NEARCLIPFOG=recompute` re-derives the per-vertex fog byte from the new z/w instead of lerping it. Not built or run by the implementer; maintainer A/B owed on Surface 2 at Bond's start vs 1964 (default / `GE_FOGVERTEX=1` / `GE_NEARCLIPFOG=recompute` / `GE_NEARCLIP=0`). Confidence it compiles: high (read-checked); correct: medium until A/B.


**Decision (2026-10-06, lead, from code + maintainer feedback; no further
A/B by maintainer choice):** per-vertex RSP fog is the default again, on top
of the CPU near clip, with the clip-created vertex fog **lerped** (not
recomputed). Reasoning: the maintainer's report is "1964 is dimmer around
Bond"; the D540 per-pixel fog is zero at the feet (bright), and the recompute
rule gives zero at the near plane too (bright), while the RSP model (per-vertex
fog, behind-camera corner = full fog via the honest negative-w divide, lerped
into the near-plane vertex) leaves a haze at the feet -- the dimmer look. The
D540 reason for dropping per-vertex fog (light/dark cycling) was GPU
near-clipping of behind-camera corners, which the CPU clip removes. The fog
varying now shares the shade qualifier (`noperspective`): the RDP interpolates
fog screen-affine like shade. Switches: `GE_FOGPIXEL=1` = D540 per-pixel fog
(replaces `GE_FOGVERTEX`, tombstoned), `GE_NEARCLIP=0`, `GE_NEARCLIPFOG=recompute`.
Build-verified; by-eye check rides on the golden re-base contact sheets and
the next playtest (Surface 2 at the start vs 1964). Fogged-level goldens
change (re-base batched with D546).
