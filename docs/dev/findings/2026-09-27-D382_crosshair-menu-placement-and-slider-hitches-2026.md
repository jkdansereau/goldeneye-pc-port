# D382

<!-- from the old section-F/H table row -->
| D382 | **Crosshair menu placement and slider hitches (2026-09-27).** full `## D382` entry at file tail | **CLOSED (2026-09-28).** Shipped in v0.4.0; accepted in the user's final review / campaign playtest. |

## D382 — Functional crosshair page and full GPU-state reset on every slider detent (2026-09-27)

**Status: PARTIAL (built; live FPS/menus and 21-level golden gate owed).** User
asked to keep Turok-style functional categories/setting names and reported
hitches while dragging sliders, possibly when launched with the v0.4.0 batch
file. Evidence from the actual `scratch/playtest_v040.log`: **349**
`video: live config applied` passes. `optionsoverlay.c:rowSetCommit` called
`videoRequestLiveConfig()` after *every changed `Video.*` value* except
Draw/LOD and restart rows, including all RGB sliders, size, style, colour and
FOV. Each pass in `videoStartFrame` called `videoApplyTexFilter()` and
`videoApplyImageOptions()` regardless of the key. In
`port/fast3d/gfx_pc.cpp`, both texture-filter setters and the anisotropy
setter call `reset_texture_state()` (clears texture and shader caches). These
three resets and the log line per detent plausibly explain the crosshair/FOV
drag hitches; a high Draw/LOD setting independently increases real render
cost, and changing anisotropy/texture-filter themselves necessarily still
rebuilds texture state. The log does not tie each pass to a specific row, so
this is a proven redundant-work path, not a measured per-slider frame-time
attribution.

**Fix (port-only):** `videoRequestLiveConfigForKey()` uses atomic bitmask
coalescing across the options and render threads. VSync, FPS cap, texture
filter, FOV, anisotropy and safe-area crop each update *only themselves* on
render-thread frame start. Crosshair knobs and the other direct-read
world/HUD settings request no GL pass at all. No default, ini key or
rendered-image behavior changes; fullscreen still uses its host-thread
request. With the existing early-return on unchanged snapped values (D370),
mouse-held duplicate polls do no GL work. User's local
`scratch/playtest_v040.bat` still runs `build-pc/ge007.latest.exe` with the
same ini and logs output, but no longer forces `GE_D322`/`GE_D204` audio
telemetry on each run (these were cheap 5-s probes, not proven to cause
hitches); setting them in the calling environment restores diagnostics.
The outdated pre-D377 binary warning and launcher label were corrected.
No running game was interrupted, and no live ini was changed.

**UI decision:** Keep the Turok five functional headings and add only a
sixth *functional* CROSSHAIR page beside GAMEPLAY (not a "MODERN" provenance
bucket, and not a graphics-quality control). The front menu has a fixed
14-content-row cap; merging RGB/style/size into GRAPHICS or GAMEPLAY would
silently lose settings. It has its own reset and no change to the existing
per-profile `Bond.Sight` option in GAMEPLAY. Rename the ambiguous duplicate
"Crosshair" row to **Show crosshair** (`On`/`Off`, original On); colour 0 to
**Original (red)** (keeps the authored sprite), Beta style to the visible
shape **Thin cross**, custom channels to **Red / Green / Blue** with `N/255`
values only while Custom is selected. **Crosshair size** displays 50–200%,
100% default. Keys and existing numeric ini values are unchanged; selecting
Beta does not affect the front-end cursor or automatically select 007 mode.
The show-crosshair machine-level gate is separate from the original save-
scoped Sight-on-screen option; both may hide the aim sight.

**Verification:** MSYS2 MINGW64 NTSC rebuild linked after the selective
update; a first compile caught a MinGW SDL without `SDL_AtomicOr`, replaced
with `SDL_AtomicCAS` loop and then rebuilt successfully. Isolated data-dir
headless run `GE_OPTIONSOVERLAY=4 GE_PCDUMP=90-90 GE_QUITFRAME=125` booted
the sixth-section layout into CROSSHAIR and captured a frame with no
missing option or crash. GRAPHICS has 10 rows and CROSSHAIR has 8, both
within the 14-row front menu cap. `GE_OPTIONSOVERLAY=2..7` now supports
all six sections. **Not yet measured:** live FPS during drag with the new
binary; the 349-pass log is from the previous build. Human verification of
both menus' labels and sizing is still owed.
