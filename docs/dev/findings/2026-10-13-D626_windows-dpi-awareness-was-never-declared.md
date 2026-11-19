# D626

<!-- from the old section-F/H table row -->
| D626 | **Windows DPI awareness was never declared: the hint sat behind PD's `PLATFORM_WIN32`, never defined here, so scaled displays got a stretched logical-size surface** (3072x1728 on a 125 % 4K panel) | FIXED 2026-10-10 (`gfx_sdl2.cpp`: `_WIN32` guard, `permonitorv2` always); fullscreen now native 3840x2160, Facility golden PASS; maintainer by-eye owed |

## D626: Windows DPI awareness was never declared — the hint sat behind PD's `PLATFORM_WIN32`, which this port never defines, so scaled displays got a stretched logical-size surface

**Status:** FIXED 2026-10-10 (`port/fast3d/gfx_sdl2.cpp`), ROADMAP §4 "Windows display scaling" row.

**Symptom.** On a 3840x2160 display at 125 % Windows scaling, fullscreen rendered a 3072x1728 surface that Windows stretched (soft picture); the maintainer's v0.5.0 4K captures were all 3072x1728. Workaround that worked: `SDL_WINDOWS_DPI_AWARENESS=permonitorv2`.

**Cause.** `gfx_sdl_init` set `SDL_HINT_WINDOWS_DPI_AWARENESS` only under `#if defined(PLATFORM_WIN32)` (inherited from the PD port) and only when HiDPI was disabled. `PLATFORM_WIN32` is defined nowhere here (`port/include/platform.h` defines `PLATFORM_WINDOWS`), so the hint never compiled in on any path.

**Fix.** Guard on `_WIN32` and set `permonitorv2` unconditionally on Windows, before `SDL_Init(SDL_INIT_VIDEO)`. Ini Width/Height are physical pixels (a 1280x720 window is now 1280x720 device pixels, smaller on screen than before at >100 % scaling).

**Verified.** Same machine (125 %): fullscreen frame dump 3072x1728 -> 3840x2160; windowed 1280x720 unchanged in pixels; Facility golden PASS. Audit: no other undefined `PLATFORM_*` guards in `port/` (the rest come from `platform.h`).
