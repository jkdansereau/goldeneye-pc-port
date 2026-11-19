# D379

<!-- from the old section-F/H table row -->
| D379 | **Crosshair colour: authored texture is red (2026-09-27).** full `## D379` entry at file tail | **FIXED (2026-09-28 bookkeeping: residual live-check notes waived by the user; shipped in v0.4.0, no reports).** Earlier: FIXED (cyan frame capture; live yellow/blue recheck owed): nonwhite tints use alpha silhouette instead of multiplying red RGB; default unchanged. |

## D379 — Crosshair colour is multiplied against a red texture (2026-09-27)

**User report:** Yellow/Cyan/Blue are visibly the wrong colours.
`gunDrawSight` (`src/game/gunfire.c:6550`) passes the RGB from
`portCrosshairTint` to `display_image_at_position`, whose
`G_CC_FADEA` combine mode multiplies TEXEL0.rgb by ENVIRONMENT.rgb.
The authored `IMAGE_CROSSHAIR1` is not a neutral white mask: captured
`-level_09` with GE_TEXDUMP, fmt=RGBA32, size=4096, tile=32x32;
`texdump/r331_f0_s3_32x32.bin` had 787 fully transparent black pixels,
232 nonzero RED pixels and essentially zero green/blue (examples
`(235,0,0,203)`, `(245,0,0,255)`). A blue tint multiplied by this image
remains dark/black/red; the user observation follows exactly. The texture
alpha retains the antialiased reticle silhouette (237 nonzero pixels).

**Fix:** preserve byte-identical default (White/0) and the N64 builds.
Only for selected nonwhite tints, `src/game/gunfire.c`'s existing
`#ifdef PORT` opt-in crosshair hook calls
`portCrosshairApplyTintCombine()` in `port/src/video.c`, replacing only
that sprite's generated combine command. Colour now uses
`ENVIRONMENT.rgb * TEXEL0_ALPHA`, transparency stays
`TEXEL0_ALPHA * ENVIRONMENT_ALPHA` (the original alpha expression).
The hook points at the command immediately after
`display_image_at_position`'s `gDPSetEnvColor`; the latter emits the
combine command next (`bondwalk2.c:136–155`). This is the same opt-in
crosshair-colour behavior previously authorized in D373, corrected to
actually show the named colours; not a new default-game behavior change.
No generic texture/combiner path is affected.

**Verification:** touched NTSC objects compile and alternate
`build-pc/ge007.review.exe` links. With user's Cyan (5) ini and a scripted
Bunker1 aim, GE_PCDUMP at frames 1050–1090 shows ~2130 cyan-dominant
pixels in the reticle region (x=664–774,y=385–505), vs zero in the same
region outside aim; screenshot-independent pixel count, no unsupported
combiner errors. The temporary GE_TEXDUMP 800-cap probes in gfx_pc/
gfx_opengl were reverted, and the user's ini was not edited for the
capture. Owed: human Blue/Yellow/Cyan and White default visuals in the
actual playtest window. Cross-ref D373.
