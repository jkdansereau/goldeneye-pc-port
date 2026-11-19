# The GE+ sea (water levels: Frigate, and the sea of Dam / Caverns / Complex)

Issue #38. Read [shaders-general.md](shaders-general.md) first. Code: `port/switch/shaders/skycommon.glsl` (`waterNormal`, `waterShade`, `waveLayers`, `texWaves`), `skyplus.frag.glsl` (the passes), `port/fast3d/gfx_deko3d.cpp` (the sky pass driver), `port/src/skyplus.c` (the wave texture, per-level settings).

## What it is

A procedural sea drawn in the sky pass, under the horizon, as a plane at the level's water height `uSeaY`. Original mode keeps GE's own sky. Per-level look (tint, swell, chop, speed) comes from `skyplus.c` (`$S/water/<level>.ini` is the intended override).

The look target: realistic but stylised, a fine busy grain that tightens toward the horizon and **never turns into a mirror lake**; Frigate in blue hour (no warm tint); Dam calmer.

## Pieces

- **Wave texture**: 128x128 RGBA8, tiling fbm. R = height, G/B = slope (decoded as `(v*255 - 127.5) / (6*255)`, `WTEX_SLOPE_K = 6`). One uv unit = 128 texels. Mips, **anisotropic 2**.
- **`waveLayers` / `texWaves`**: sum of rotated, scaled, drifting reads of that texture (a medium swell, then layers 2-4 at decreasing scale). Layer 2 is cut with distance (`LAYER2_CUT`); the medium swell exists near only.
- **`waterNormal(p, dist)`**: a slope from the layers, scaled by calm/rough patches (`vnoise` of the position, drifting slowly with `uTime`), then flattened to 35% at long range (specular anti-aliasing). `gSeaMode` selects the wave set: 0 both sets crossfaded by distance (the level's own water draws, Dam), 1 the moving set only (the live near sea), 2 the still set only (the bake).
- **`waterShade`**: body colour under a Fresnel reflection of the sky (the 128x64 panorama `uSkyLut`, see the sky note), plus sun glints (none in blue hour). `gLite` swaps the sky reflection for the horizon colour.
- **Horizon**: `mix(horizonColor, sea, smoothstep(0, 0.006, -nd.y))`: sea and sky meet in the sky's own horizon colour.

## Passes (`uPass`, `uC0_.w`)

- 0: one pass (no reduced target).
- 1: the sea into a **half-width target of exactly the viewport's size** (above-horizon pixels write `horizonColor` so the bilinear read at the horizon does not blend in black).
- 2: the full-resolution pass reads that target below the horizon (`uHalfSea`, binding 3) and draws the sky above it.
- 4: **bake** the far sea into `uSeaPano` (binding 4).
- 5: enlarge the half-size final image onto the screen (half scale, below).

**Half scale** (deko3d, `SKY_HALF_SCALE`): the sea target is a quarter of the viewport's width and half its height, and the final pass (pass 2) is drawn into a half-size target (`s_skyr_fb`); pass 5 enlarges it onto the screen. Halving only the final pass gained ~1 ms on the console (12.4 -> 11.4 ms): the waves in the sea target are the heavy part, so the sea target has to follow the scale too. Both together: 12.4 -> **6.2 ms** median per frame (console, Frigate, handheld).

## The far sea (the part that took a week)

Observations that settled the design:

- Seen from far away, the sea reads as a still, fine grain; breathing/camera sway of a few pixels must not make it swim; it must lie on the sea plane (horizontal), with texels that tighten toward the horizon, like a photo.
- **The right far look is just the true plane, filtered.** The texture is busy enough that mips plus anisotropic 2 give streaks that tighten and lose contrast with distance. That is what the reference photo shows.
- It must be **static in time** beyond a distance (a wave's drift there is a pixel of motion; big moving layers made the whole background pulse), and the line where the moving set gives way to the still one must not be a circle around the player.

Current design:

1. **Near sea, live** (`gSeaMode = 1`): moving waves, per pixel. Blend weight `kf = smoothstep(NEAR_Z0, NEAR_Z1, zd)` = 600..6000, with `zd` the depth of the sea point **along the camera's view axis** (not the distance to the eye: distance draws a circle on the sea, depth draws a straight line).
2. **Far sea, baked** (`gSeaMode = 2`, pass 4): a panorama `uSeaPano`, 4096 x 512 RGBA8. `u = azimuth / 2pi + 0.5`; `v = sqrt(elevation sine) / panoMax`, `panoMax = 0.8` (finer rows near the horizon; the baked sea never needs more than ~0.64). The bake evaluates the still waves + a horizon-colour reflection (`gLite`, **no sky reflection out there**) with analytic footprints (`gDpdx` along azimuth, `gDpdy` along the range), clamped to 3000 units so the mip reads stay finite at the horizon. The final pass reads it with one `textureLod` per pixel, **through the baked eye**: the sea point under the pixel is looked up as the baked eye (`uBakeX/Z/H`, the free `w` of the corner rays) saw it. That is exact on a plane, so a strafe slides the far sea with the world like the live sea, and the blend shows no shear.
3. **When it is baked** (`gfx_deko3d.cpp`, `skyplus.c`): on the level's first frame, before that frame's sky, from the player's start eye (`bakeEye` = `g_CurrentPlayer->field_488.pos`, set before the intro: not the intro's flying camera), so no image is drawn without it; again on the next frame, because **the first-frame bake comes out black** (cause not found; its inputs are valid from frame 1) and the sea stays live until the second one is done; again only when the sun or moon direction moves more than 3 degrees or the level's sky or water colour changes (at most once per 60 frames). Entering the intro (a new level, a restart: `bakeReset`) redoes it. **Never for a move**: the reprojection keeps the far sea right wherever the player goes.
4. Because the far sea is a lookup, it is **still by construction**: no flicker, nothing to accumulate.

Cost: one bake is ~21 ms on the console (a bake forced every frame took the frame from 13.5 to 35 ms), twice per level start. Per frame the far sea is one texture read.

Coverage limit: the panorama goes down to sin(elevation) 0.64 from the bake eye, so sea closer than ~800 units under the bake point reads its last row. Not seen in play; `panoMax = 1.0` would cover it at the cost of coarser rows near the horizon.

## Things tried and rejected (do not redo)

| Idea | Why it failed |
|---|---|
| Replace the far layers with a static noise texture mapped by **view direction** (azimuth/elevation), flat or half-perspective | Reads as a vertical wall; tile repeats show as columns and grids; does not lie on the plane. |
| Continue the plane's mapping **by its tangent** at constant texel size | Squares curve into arcs (distance is radial), and at constant size the sea looks like a ramp rising toward the horizon ("la mer remonte"). Damping the continuation only trades it for vertical streaks. |
| Cheapest "reduced" far sea by dropping layers one by one | Each cut moved the problem (mud bubbles, standing-wave oscillation). |
| Quarter-width sea | Same cost as half. |
| **TAA** (full screen, then on the half-width sea target only, jitter + reprojected history + neighbourhood clip) | In the viewer it looked good and cost +0.1 ms on the sea target; in the game it came out flat and uniform (unresolved: new UBO binding, see general note), and the metric showed no flicker to fix to begin with. Replaced by the bake. Removed. |
| Frame interpolation / reprojection of the sea | The far sea costs nothing once baked; the near sea moves, so reuse leaves trails; the worst frame is the same. |
| Sky reflection in the far sea | Not needed; removing it makes the bake independent of the sky and of cloud drift. |

## Debug

- `GE_TMPDBG` (temporary, remove before commit) selects debug outputs in `skyplus.frag.glsl` through `uWave4.w`. Checkerboard mode 8 on the true plane (raw left, footprint-filtered right) is the quickest way to judge geometry and filtering.
- A black far sea in the game = the panorama read returns 0: not baked, baked for another eye height, or NaN. The tests that found it: output the read UV, output NaN/inf/black as colours.

## Open items

- Ryubing shows the half-scale image wrong (an enlarged sea; a test gradient drawn into the half-size target read back as one flat colour) where the console is right: not explained, judge half scale on the console.
- The first-frame black bake's cause.
- Looks owed: docked, co-op (two viewports share one panorama), Dam (legacy `gSeaMode 0`), Caverns, Complex.
- Judge this sea on Ryubing or the console only: the old Ryujinx (r.49574a9) draws bright streaks, arcs and blocks in the far sea that neither shows.
- The `$S/water/<level>.ini` override: document it in `docs/` when final.
