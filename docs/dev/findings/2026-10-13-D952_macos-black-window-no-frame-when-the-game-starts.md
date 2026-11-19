# D952

<!-- from the old section-F/H table row -->
| D952 | **macOS: black window, no frame, when the game starts while the display is asleep (SDL3 Cocoa VSync waits forever on a CVDisplayLink that never started)** — full `## D952` entry at file tail | FIXED 2026-10-13 (port-only, `gfx_sdl2.cpp`; Mac A/B: unfixed 3/3 hang, fixed 0/5) |

## D952: macOS first-start black window = SDL3 VSync waiting on a CVDisplayLink that never started (display asleep) — FIXED 2026-10-13

**Symptom.** "If the window stays black on the very first start, close it and start it again" (v0.6.0 known issue): 4 hangs on 10-12 and 2 of 13 cold first launches on 10-13, always the first launch after the Mac had sat idle (which is when a new CI build arrives, so it looked like "first launch of a new binary").

**Evidence.** Hung run (`kernel heartbeat: no frame rendered`, frames=0) sampled with `sample`: the D481 render worker sits in `portRenderGfxTask -> Cocoa_GL_SwapWindow -> SDL_WaitConditionTimeoutNS -> __psynch_cvwait`; the scheduler, `amMain` (`src/audi.c` reply wait) and `bossMainloop` only wait behind it. The process has NO `CVDisplayLink` thread; a healthy run has one. SDL 3.4.14 (`SDL_cocoaopengl.m`): with a swap interval set, `Cocoa_GL_SwapWindow` waits on a condition that only the display-link callback signals, with no timeout; the link is created and started once, in `initWithFormat`, from the active displays.

**Trigger (deterministic).** `pmset displaysleepnow`, then launch: unfixed download 3/3 hang. Re-signing the binary (new cdhash) with the display awake: 0/10. The "new binary" correlation was the idle gap before it.

**Fix (`port/fast3d/gfx_sdl2.cpp`, `__APPLE__` only).** SDL's swap interval stays 0, so SDL's swap never waits; VSync is set on the context with `CGLSetParameter(kCGLCPSwapInterval)` (as SDL2 did on macOS), which blocks in `flushBuffer` on the driver's own refresh. `get_swap_interval` returns the CGL value, so D578/D600 pacing sees the same interval as before. Logs `SDL: swap interval N via CGL (read back N)`.

**Verified (Mac M3, macOS 15.6.1, fix built with the CI recipe, run against the download's own SDL 3.4.14 dylibs).** Display asleep: fixed 0/5 hang. Display awake: 60.0 presents/s as before; VSync on/off read back 1/0 from CGL. 21-level sweep (mac_sweep.sh, fixed binary): 21/21 OK. Windows build clean; Windows/Linux code paths unchanged (`#else` = the old lines).

**Status.** FIXED.
