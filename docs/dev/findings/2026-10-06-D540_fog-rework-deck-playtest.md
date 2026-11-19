# D540

<!-- from the old section-F/H table row -->
| D540 | **Fog rework (Deck playtest): ground fog chunked (D503 snap) and cycled light/dark at Bond's feet (behind-camera vertex fog), sky horizon fade squeezed (perspective shade), fog tied to Draw distance** — full `## D540` entry at file tail | FIXED 2026-10-05 (port: exact per-pixel fog, snap off, noperspective shade, new `Video.FogDistance` default 1.0x = N64 (far clip = min(Fog, Draw) x authored), Draw/Fog distance live; maintainer A/B vs 1964 OK; goldens re-based win/linux/deck); Deck re-check owed |

## D540: Fog rework from the Deck playtest — ground fog "chunked" and cycled light/dark around Bond, sky horizon fade squeezed, fog tied to Draw distance; new Fog distance slider, Draw/Fog distance apply live (2026-10-05)

**Reports (v0.5.0-rc, maintainer, Deck + Windows A/B against 1964/GEPD on
Surface 2):** ground fog "blocky — updates one piece of the ground at a time /
chunks away" as Bond walks; the ground around Bond's feet cycled bright/dark
as he moved or turned; the sky's clouds stayed bright down to the horizon
where 1964 fades them into fog; no way to set fog independently ("fog, draw
and LOD distance all seem independent"); Draw distance only applied on the
next level load.

**Causes (port renderer, `port/fast3d/`):**
1. *Chunking* — the D503 full-fog snap (fog >= 247/255 -> full). Its hard 97%
   edge followed the per-vertex fog's triangle-shaped iso-lines and swept
   across the ground tile by tile. Maintainer A/B with the snap disabled:
   smooth. (The rc1 per-vertex form of the snap was worse.)
2. *Light/dark cycling at Bond's feet* — per-vertex fog for triangle corners
   behind the camera. The old code forced `winv = +32767` for `w < 0`
   (sign flip -> no-fog/full-fog jumps); even with the RSP's honest
   negative-w divide (full fog), that corner's value is blended into the
   visible ground after GPU near-clipping, so the area at the player's feet
   changed with whichever corners sat behind the camera.
3. *Sky horizon fade* — the RDP interpolates shade colour affinely in screen
   space; the GPU default is perspective-correct. On a sky triangle running
   from overhead to the horizon that kept the bright cloud colour almost to
   the horizon and squeezed the authored fade into a thin band.
4. *Fog vs Draw distance* — D218 scaled the far clip and kept the authored
   per-mille fog positions, so fog moved out with Draw distance (2.5x
   default): fogged levels were not N64 at the defaults. Both were computed
   only at level load (`fogLoadCurrentEnvironment`).

**Fixes:**
- Fog is exact per pixel: the vertex sends `z*fog_mul + w*fog_offset`
  (fog x w, linear in clip space, clips correctly) and the fragment shader
  divides by `gl_FragCoord.w`. Same fog function, colour and positions as the
  RSP; corners behind the camera no longer contribute. Differs from the N64
  only in the in-between shading of large triangles (the N64 is per-vertex).
- D503 snap off by default (`GE_FOGSNAP=1` restores it).
- Shade colour (`vInputN`) is `noperspective` (desktop GLSL >= 1.30; GLSL ES
  keeps perspective) — RDP-style screen-affine Gouraud; textures stay
  perspective-correct.
- `Video.FogDistance` (new, 50..800 %, default 100 = N64; F10 Graphics
  "Fog distance", both display presets 100): `bgfog.c` (`#ifdef PORT`, the
  D218 site) now scales the level's far clip/fog by
  `min(FogDistance, DrawDistance)` instead of DrawDistance alone, and keeps
  the game's own per-mille fog positions and intensity math unchanged (the
  D218 mechanism, driven by fog). At Fog 1.0x that is the authored far: exact
  N64 fog at any Draw distance (Cradle: Draw 2.5x vs 1.0x 0.05% px, the
  distant truss a ghost as on 1964). Draw distance still scales prop draw
  distance (`propobj.c`), the room pool and the D466 split within that clip.
  **Rejected first cut (same evening):** keep the far clip at Draw x and
  convert the per-mille fog positions to distances and back. Two defects:
  (1) the game reads the two fractions as LINEAR distance for culling but as
  DEPTH-BUFFER per-mille for the prop/chr fade (`sp20`/`sp1C` ->
  `fogGetPropDistColor`), and feeding both the linear rescale faded props and
  characters out (Train attract: no crates, no Bond); (2) with the clip far
  out, the fog sits within a few per-mille of 1000, so the s32 rounding
  pulled the fog visibly in (Cradle truss fogged out), and the NDC conversion
  assumed `viSetZRange`'s first argument (`BlendMultiplier`) is the
  projection near plane -- Fog 2.0x fogged the walkway at Bond's feet. Lesson:
  extend the clip only as far as the fog needs and leave the game's fog math
  alone.
- Live apply: `fogSetRenderFogColor` re-runs `fogLoadCurrentEnvironment` on
  the game thread when either multiplier changed (pointer reset per level).
- A/B switches (registered): `GE_FOGVERTEX=1` (per-vertex RSP fog),
  `GE_SHADEPERSP=1` (perspective shade), `GE_FOGSNAP=1`.

**Verified:** maintainer side-by-side vs 1964/GEPD on Surface 2 at Bond's
start and while moving: ground fog smooth, no cycling at Bond's feet, horizon
fade matches; "everything else looks solid". Maintainer 1964 stills of the
Frigate and Cradle starts: Frigate's sea is dark navy (the new build; the old
goldens' teal was the 2.5x fog), Cradle's distant truss is a faint ghost
(matched by the final clip design above). Runway/Surface 1/Dam/Jungle
headless captures inspected during the per-pixel step. Goldens re-based on
all three platforms the same evening (win 21/21 confirming pass; linux on the
X220; first `deck` set, see tools_pc/golden/README.md). **Owed:** maintainer
re-check on the Deck (fog walk on Surface 2; Fog/Draw sliders live). A remaining "slight scale or rotation" in
the sky clouds vs 1964 was attributed by the maintainer to 1964's non-4:3
window aspect; re-check only against a correct-aspect reference (simple64 /
ParaLLEl-RDP) before opening a row.
