# D639

<!-- from the old section-F/H table row -->
| D639 | **Exclusive fullscreen picked the wrong resolution: `SDL_WINDOW_FULLSCREEN` switches the monitor to the window's display mode, which SDL derives from the current window size when none is set** - `port/fast3d/gfx_sdl2.cpp` `set_fullscreen`; full `## D639` entry at file tail | FIX BUILT 2026-10-10 (`chore/v070-agentic-batch`): exclusive fullscreen now sets the desktop mode of the window's display first; maintainer check owed (NVIDIA "Prefer native") |

## D639: exclusive fullscreen took the window size as the monitor mode

**Status:** FIX BUILT 2026-10-10 on `chore/v070-agentic-batch`; port-only.
Not live-verified: needs the maintainer's NVIDIA setup ("OpenGL present
method = Prefer native", where it was seen 2026-10-09 during D583).

**Cause.** `set_fullscreen` calls `SDL_SetWindowFullscreen(wnd,
SDL_WINDOW_FULLSCREEN)` for `Video.FullscreenMode = 1`. Exclusive mode
switches the display to the window's fullscreen display mode; none was ever
set (`gfx_sdl_set_closest_resolution` exists in the window-manager API but
nothing calls it), so SDL picks the mode closest to the current window size -
e.g. the ini's 1440x1080 window - instead of the desktop resolution.
Borderless (`SDL_WINDOW_FULLSCREEN_DESKTOP`) never changes the mode, which is
why it was fine.

**Fix.** Before entering exclusive fullscreen, set the window's display mode
to the desktop mode of the display it is on; an explicit
`set_closest_resolution` (none today) still wins.

**Check owed:** Video.FullscreenMode = Exclusive, a non-native window size,
toggle fullscreen: the monitor should stay at its native resolution.
