# D450

<!-- from the old section-F/H table row -->
| D450 | **Unplugging and replugging a controller was not picked up (user, GameSir-G7 Pro; 2026-09-29).** — full `## D450` entry at file tail | FIXED — port-only: the render thread's `gfx_sdl_handle_events` drains the same SDL queue as video.c's host pump but had no case for `SDL_CONTROLLERDEVICEADDED/REMOVED`, so it usually swallowed them and `inputRescanPads` never ran. Both pumps now post a request; the rescan runs at the top of `inputUpdate`, on the thread that reads the pads. |

## D450 — a replugged controller is never reopened: the render thread's event pump swallows the hot-plug events (2026-09-29)

**Status:** FIXED, port-only. User report (GameSir-G7 Pro): "disconnecting the controller, then reconnecting it did not seem to be picked up". The play log had only the startup `input: rescanned pads` line.

**Root cause.** Two threads drain the one SDL event queue:
- `port/src/video.c` `videoPumpEvents`: `case SDL_CONTROLLERDEVICEADDED / REMOVED: inputRescanPads();`.
- `port/fast3d/gfx_sdl2.cpp` `gfx_sdl_handle_events` (called from `gfx_run` on the render / scheduler thread every frame): **no case for them**. Whatever it dequeues is dropped.

Whichever loop runs `SDL_PollEvent` first gets the event, and the render loop usually wins (the same race WI-1 documented for ESC). So the pads were never rescanned and `pads[]` kept the closed controller's handle. Headless repro with a virtual pad (attach, detach, re-attach): one rescan at startup, none at the detach, and at the re-attach the process aborted with `malloc(): unaligned tcache chunk detected` (stale handle). A second latent problem: `inputRescanPads` ran on the host thread while the scheduler thread reads `pads[]` in `inputComputePad`; closing pads under a reader is a race.

**Fix.**
- `gfx_sdl2.cpp`: new `case SDL_CONTROLLERDEVICEADDED / REMOVED: inputRequestRescan();`.
- `video.c`: the case now calls `inputRequestRescan()` instead of `inputRescanPads()`.
- `input.c`: `inputRequestRescan()` sets an atomic; `inputUpdate()` consumes it first thing and runs `inputRescanPads()` there, i.e. on the thread that is about to read the pads (the scheduler thread, or the game thread under tick input).

**Verified** (on the netplay line, headless, virtual pads through a test seam): attach -> detach -> attach -> detach -> attach with no tick input: 5 rescans (startup + one per event), no crash, against 1 rescan then a heap-corruption abort before the fix; with tick input the pad's buttons are seen before, after the first and after the second re-attach; solo `-level_09` frame 1800 byte-identical to the base. Real-pad retest by the user (GameSir-G7 Pro, 2026-09-29): passed.
