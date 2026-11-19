# D997

## D997: GE+ sky and sea on water levels (Frigate): a live near sea, a baked far sea, half scale -- BUILT + MAINTAINER-CHECKED 2026-11-19 (console Frigate handheld, Ryubing; Switch build; Visuals >= Enhanced)

Issue #38 (fork): Scenery (Enhanced) was not applied on water levels; Frigate kept GE's sky. Design notes and what was tried and rejected: `docs/dev/shaders-sea.md`, `shaders-sky.md`, `shaders-general.md`.

**What ships.**
- The GE+ sky now draws on water levels, with a procedural sea on the plane at the level's water height. The near sea is computed live (moving waves, the sky panorama's reflection), at half width in its own target (`gfx_deko3d.cpp`, read back by the final pass).
- The far sea is a 4096x512 panorama (azimuth, sqrt of the elevation) baked once (`skyplus.frag.glsl` pass 4): still waves, the horizon colour as reflection, dimmed with distance. It blends in by depth along the view axis, 600..6000 (a plane cut by a plane: a straight line, not a circle around the player), and is read through the baked eye (`uBake*`, the free `w` of the corner rays): exact on a plane, so strafing does not shear it against the live sea.
- The bake: on the level's first frame, before its sky, from the player's start eye (`skyplus.c` `bakeEye` = `g_CurrentPlayer->field_488.pos`, set before the intro), so no image is drawn without it; again on the next frame (the first-frame bake comes out black: cause not found, its inputs are valid from frame 1); again only when the sun or moon moves more than 3 degrees or the level's sky or water colour changes; a new level or a restart (entering the intro) redoes it. Never for a move.
- Draw order: Drawing the sky after the opaque rooms and props, depth-tested at the far plane (a `src/game/bg.c` hook), was tried and dropped: once the sky and sea are drawn at half size, only the enlarging composite (one texture read per pixel) could be skipped behind the scenery, so it saved nothing worth a `src/game` edit. The sky is drawn at `skyRender`'s place, as GE does. Drawing only where the sky can show (a coarse mask) is a separate issue.
- Clouds: a low or set sun no longer lights the clouds overhead; only the side of the sky toward it stays bright (`skyColor`, `PRM(14)`, `PRM(15)`).
- `PRM(i, default)`: every tuned value of the sky and sea shaders is a constant in the game and an editable value in the viewers (block `Viewer`, binding 1, only under `VIEWER`).

**Tooling.** `tools_pc/seaview/` (CLI `seaview.py`, PC viewer, console viewer in deko3d; README there): one captured frame (`GE_SEADUMP=1` writes it; ROM-derived, kept out of git), the game's shaders, menus of variants, settings with save/load, render scale, GPU time; the console viewer compiles the `.ini`'s values in as constants (what the game's build does). Machine settings in `<repo>/.env` (`.env.example`).

**Measured (console, Frigate, handheld 1280x720).** Whole frame ~12.4 ms GPU while playing (11.8-13.1, 74 windows): within 60 fps. The console viewer gives the sky and sea alone at 8.6 ms full scale, 2.8 ms at half scale (docked 1080p: 7.8 / 2.5 ms).

**Pitfalls found.** The old Ryujinx (r.49574a9) renders the far sea with bright streaks, arcs and blocks that Ryubing and the console do not show: judge this sea on Ryubing or the console only. A PC GPU says nothing about the console's cost. Viewer and game must share the bake logic, or the viewer hides the game's bugs.

**Half scale.** The final pass is drawn into a half-size target and enlarged by a composite (`uPass 5`, `SKY_HALF_SCALE`); the sea target follows (a quarter of the width, half the height). Halving the final pass alone gained ~1 ms on the console (12.4 -> 11.4 ms): the waves are the heavy part. Both: **6.2 ms** median GPU per frame (5.5-6.9 in play, console, Frigate, handheld; maintainer-checked look). Ryubing shows the half-size image wrong (an enlarged sea) where the console is right: not explained.

**Open.** The first-frame black bake's cause. The panorama covers down to sin(elevation) 0.64 from the bake eye: sea closer than ~800 units under the bake point reads its last row (not seen in play). Dam (`water.frag`, the legacy blend), Caverns, Complex, co-op and docked looks are not checked.
