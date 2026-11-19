# The GE+ sky

Read [shaders-general.md](shaders-general.md) first. Code: `port/switch/shaders/skycommon.glsl` (`skyColor`, `horizonColor`, `skyReflect`, `skyRay`), `skyplus.frag.glsl` (the pass), `skylut.frag.glsl` (the reflection panorama), `port/src/skyplus.c` and `port/include/skyplus.h` (what the game hands the shader), `port/fast3d/gfx_deko3d.cpp` (the pass driver).

Status: this note records how the sky works today and what was measured while working on the sea. The sky itself was **not** the subject of issue #38 and was not reworked; items marked *not investigated* are open.

## What it is

A fullscreen pass drawn behind the level. Every pixel casts a ray (`skyRay(u, v)` interpolates the four image-plane corner rays `uC0..uC3`, `u` and `v` in 0..1 from the top-left of the player's screen; `x` is first converted back to the game's screen x through `uAsp`). Above the horizon the pixel gets `skyColor(nd)`; below it, GE's clear colour or the sea.

## `skyColor(nd)`, in the order it builds the picture

1. **Base**: GE's sky colour at the horizon (`uSky`), a little deeper toward the zenith.
2. **Sun disc and halo** when the level has a sun up (`uSunGlow`, `sunUp`). A sun already set (blue hour, `uSunDir.y < 0`) has no disc and no halo, only the low glow of step 6.
3. **Stars** at night, hashed cells in azimuth/elevation.
4. **Cloud layers**, three, highest first; layer 1 is GE's own. Each layer reads the 64x64 cloud texture on a **curved shell** (`PLANET_R = 100000`), not a flat plane: overhead it is the plane, toward the horizon it bends down, so the distance stays finite and the texture never shrinks into a moire band. Layers 0 and 2 are scaled/rotated variants (`kH, kSc, kRo, kOp, kK`). Scrolling is `ofsOf(i)`, driven by the CPU (`uOfs01`, `uOfs2X`). A second texture fetch lights the cloud edges; haze fades clouds into the horizon colour just above it.
5. **Sun halo** shining through the clouds (tight core + bloom, dimmed by `cover`).
6. **Moon** and stars where the clouds let them through.
7. **Horizon band**: everything fades to the fog colour before the horizon (`smoothstep(uBand * 0.18, uBand, e)`), so whatever the level draws far away meets a flat colour as with GE's own plane. `uBand` is the elevation of the level's tallest scenery at its fog distance (high on Dam, thin over an open sea).
8. **Blue-hour glow**: a pale glow hugging the horizon on the side of the set sun.

## The reflection panorama

`skylut.frag.glsl` draws the sky once per frame into a **128 x 64** panorama (`u` = azimuth, `v` = sqrt of the elevation, finer near the horizon). The sea reads it (`skyReflect`) instead of evaluating the sky for each pixel of water. In that mode (`gReflect = true`) `skyColor`:

- uses **GE's cloud layer only** (layer 1);
- holds **still** (no scrolling offset): a scrolling reflection slid the background sideways;
- reads the clouds at a very low resolution (`textureLod`, level 3 = 8x8 of the 64x64): nothing fine to mirror, and it changes smoothly from pixel to pixel;
- skips the cloud-lighting fetch.

The far sea no longer uses this reflection at all (it reflects the horizon colour only, see the sea note); only the near, live sea does.

## Draw order

Drawing the sky after the opaque rooms and props, depth-tested at the far plane (a `src/game/bg.c` hook), was tried and dropped: once the sky and sea are drawn at half size, only the enlarging composite (one texture read per pixel) could be skipped behind the scenery, so it saved nothing worth a `src/game` edit. The sky is drawn at `skyRender`'s place, as GE does. Drawing only where the sky can show (a coarse mask) is a separate issue.

On deko3d the final pass is drawn at **half size** and enlarged (see the sea note, "Half scale").

## Clouds and the sun

A low or set sun no longer lights the clouds overhead: they fall back to the sky's colour, and only the side of the sky toward the sun (or the moon) stays bright (`PRM(14)` minimum light, `PRM(15)` sun side). A level with neither keeps GE's shading.

## Cost (measured, AMD integrated GPU in the standalone viewer, 1080p)

Whole frame 4.4-4.6 ms; skipping `skyColor` above the horizon saved about **1.2 ms** (cloud layers, sun, stars); skipping the sea shading saved about 0.3 ms; the rest (about 3.3 ms) is fixed cost of the passes and blits there. **Console numbers for the sky alone were not measured.**

## Ideas on the table (not done, not investigated)

- **Bake the sky too**, as for the far sea: a panorama redrawn only when the sky's inputs change, with the sun halo and the cloud drift as small per-pixel overlays. The sun is fixed in the world in the current levels, so the expensive part (three cloud layers) would be static except for the scrolling offsets; scrolling clouds mean a bake keyed on `ofsOf(i)` would re-bake continuously, so the clouds would have to be sampled live from the baked layer maps rather than baked into colour. Open question: whether the scrolling can be reproduced by shifting a baked image.

## Pitfalls seen

- **Black band above the gun / at the horizon** on the console: the reduced-width sea target was larger than the region read, so a sub-rectangle read returned unwritten (black) texels. Fixed by making the target the viewport's exact size and having above-horizon pixels write the horizon colour (see the sea note). Fixed in the Ryujinx build; **not yet confirmed on the console**.
- A pixel that is under the horizon must not early-return: Ryujinx ran the cloud code below the horizon anyway, giving clouds with negative shade (saturated blue) or the previous frame. The code picks the colour at a single write at the end.
- `skycommon.glsl` is compiled into three shaders; `water.frag.glsl` (Dam) has no view rays. Anything that uses `uC0..uC3` or `uEye` must degrade safely there.
- Render targets read by a later pass need the `1.0 - y` read (general note, trap 3).
