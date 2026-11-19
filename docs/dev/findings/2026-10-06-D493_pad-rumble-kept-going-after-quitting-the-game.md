# D493

<!-- from the old section-F/H table row -->
| D493 | **Pad rumble kept going after quitting the game: the clean exit never runs `inputDestroy`, so `portAtExit` now calls `inputRumbleStopAll()`; same commit fixes D493b, a 1-unit live-pixel strip at the pillarbox edges in Original mode** — full `## D493` entry at file tail (D493b is a sub-section) | FIXED 2026-10-03, maintainer-verified in play (DualSense; Original-mode edges incl. ultrawide) |

## D493 — Pad rumble kept going after quitting the game (2026-10-03)

**Report:** maintainer, DualSense, quitting mid-firefight left the pad rumbling.
**Cause:** `SDL_GameControllerRumble` runs on the pad for its duration; the pad-close path (`inputDestroy`) never runs because the host loop exits via `exit(0)` from the clean-exit path (only the atexit handler in `main.c` then runs).
**Fix (port only, commit 44728eb1):** `inputRumbleStopAll()` (`port/src/input.c`, declared in `port/include/input.h`) sends a zero rumble to every open pad and sets `s_rumbleStopped`, which makes `inputRumble` a no-op afterwards (the game thread is still running during atexit); called first in `main.c` `portAtExit`.
**Verified:** build only.
**Owed:** none.
**Status:** FIXED, maintainer-verified 2026-10-03.

**D493b — Original N64 pillarbox showed live pixels in a 1-unit strip at the left/right edges (maintainer playtest of D447).**
**Cause:** the Original preset sets `Video.SafeAreaCrop = 0`, which also turns off D246's 1-unit horizontal trim, so D246's gap (unexplained; see D246) showed scene/background pixels. D447 had listed it as a known cosmetic.
**Fix (port only, same commit 44728eb1):** `gfx_dp_set_scissor` (`port/fast3d/gfx_pc.cpp`) clamps the RDP scissor to logical x [1, 319] when the crop is off and not split-screen; those columns show the frame clear (black). Geometry is not rescaled; crop-on (the default) is unchanged by construction.
**Verified:** headless `-level_09`, 1280x720, AspectMode=1, SafeAreaCrop=0, MSAA 1: output columns 160-162 and 1117-1119 (1 unit at 3x) are black, run exited cleanly.
**Owed:** none (maintainer-verified 2026-10-03).
**Status:** FIXED.
**Maintainer play check 2026-10-03:** rumble stops on quit (DualSense); Original-mode edges clean, also at ultrawide.
