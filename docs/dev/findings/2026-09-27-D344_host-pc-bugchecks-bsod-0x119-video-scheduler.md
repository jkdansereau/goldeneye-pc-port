# D344

<!-- from the old section-F/H table row -->
| D344 | **Host PC bugchecks (BSOD 0x119 VIDEO_SCHEDULER_INTERNAL_ERROR) during game test runs; probable cause: quit called exit() while the render thread could be inside the GL driver (2026-09-27).** — full `## D344` entry at file tail | FIXED (probable cause; orderly quit + GE_QUITFRAME), headless + user-verified; no dump, watch for recurrence. |

## D344 — Host PC bugchecks (0x119) during game runs: unclean multi-threaded quit (2026-09-27)

**Report:** the maintainer's PC blue-screened twice while headless test runs were going (10:17 and 10:22), and once before (2026-09-14 14:38, during M-123 captures). All three are bugcheck **0x119 VIDEO_SCHEDULER_INTERNAL_ERROR, parameter 1 = 0x3000**, raised by the Windows GPU scheduler (dxgkrnl) on an invalid driver state. No minidump was written (volmgr 161, "Dump file creation failed"). Host: RTX 5090 (driver 2026-09-03) + AMD iGPU + Parsec virtual display + Microsoft Remote Display adapter; hardware-accelerated GPU scheduling on (`HwSchMode = 2`). The first crash hit while `data/ge007.ini` was being restored, leaving it as 1465 zero bytes (restored from backup).

**Evidence (event logs only; runs were stopped):**
- Both of today's crashes fell within seconds of the harness ending a run with `timeout`: the ini restore that runs right after the kill never reached disk, and the second reboot came about 30 s after its kill was due.
- A killed run is not an abrupt kill: SDL turns the signal into `SDL_QUIT`, the logs end with `config: wrote data\ge007.ini`, and the process then ends in `abort()` (WER holds thousands of ge007 `0x40000015` reports at shifting in-binary offsets, plus older `0xC0000374` heap-corruption reports; Windows has applied the Fault Tolerant Heap to ge007). The teardown is not clean even when nothing crashes.

**Probable cause (medium confidence, no dump):** the GL context is created on the host thread but bound to the scheduler thread, which does all rendering and `SDL_GL_SwapWindow` (`gfx_sdl2.cpp`). Quit was `exit(0)` straight from whichever SDL pump dequeued the event: the host thread (`video.c`, three sites) or fast3d's render-thread pump (`gfx_sdl2.cpp`, three sites), plus the overlay's "Quit to desktop". From the host thread, process teardown could run while the render thread was mid-frame inside the NVIDIA driver. A user-mode program shouldn't be able to bugcheck the machine, so the driver/HAGS stack is at fault too, but the port was feeding it a hazard on every exit.

**Fix (port-only, `port/src/video.c`, `port/fast3d/gfx_sdl2.cpp`, `port/src/optionsoverlay.c`):**
- Every quit site calls `videoRequestQuit(why)` instead of `exit(0)`.
- The render thread brackets each frame with an `s_inFrame` atomic. At a frame boundary after a quit request (`videoStartFrame` before binding, or `videoEndFrame` after the swap) it runs `gfx_sdl_park_for_exit()` (`glFinish` + unbind the context) and parks for good.
- The host thread (`videoHostExitIfRequested`, each pump) waits until the render thread is parked or outside a frame (at most 2 s), logs `video: exiting (render parked|idle|STILL IN FRAME (timeout) after N ms)`, then `exit(0)`, so atexit still saves the config.
- Ordering: the render thread sets `s_inFrame` before reading `s_quitReq`; the host sets `s_quitReq` before reading `s_inFrame` (SDL atomics are sequentially consistent), so no frame can start after the host decides to exit.
- **Harness:** `GE_QUITFRAME=<n>` requests the same orderly quit after frame n. `tools_pc/golden_gate.sh` now quits 30 frames after its last capture; `timeout` stays as a backstop only. `verify.sh`'s sweep still uses `timeout` (follow-up).

**Not changed (noted):** fast3d's render-thread SDL pump still drains the shared event queue alongside the host pump. On Windows only the window's creating thread should pump; worth revisiting. The audio device is still open at exit.

**Verification owed (runs paused at the user's request):** with the user's OK (ideally HAGS off first), run `tools_pc/golden_gate.sh` and a few `GE_QUITFRAME` runs. Check each log ends with `video: exiting (render parked ...)`, check no new ge007 `0x40000015` WER reports appear, and check a window-close / Alt+F4 by hand. A crash dump (fix the host's dump creation) would confirm or rule out the cause.

**Verified (2026-09-27):** golden gate 3/3 with self-quit; five self-quitting runs all logged `render parked after 0-17 ms`; the user closed the game by window close and Alt+F4 with no crash. Not proven as the bugcheck's root cause (no dump): watch for recurrence.

**Status: FIXED (probable cause), user-verified.**
