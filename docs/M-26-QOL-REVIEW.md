# M-26 QoL review — save-on-exit, window persistence, raw mouse, --help, STALLED verdict

Port-layer only. Zero `src/` / game-logic change. Every default reproduces
prior behaviour exactly (all new knobs default-off / default-current).

## A — `atexit()` save-on-exit + `[Window]` persistence

- `port/src/main.c`: `atexit(portAtExit)` registered right after
  `configLoad()`. `portAtExit` = `videoSaveWindowState()` then
  `configSave()`. The `exit(0)` in `videoPumpEvents` (SDL_QUIT / ESC /
  window-close) is reachable, so the handler fires on every clean quit.
  Crash / `sysFatalError` paths call `abort()` → handler does **not** run
  (intended — don't persist a broken state).
- `port/src/video.c`: new `[Window]` config section — `Width`, `Height`,
  `X`, `Y`, `Maximized`. Sentinels reproduce the old hardcoded behaviour:
  `Width/Height = 0` → native (640×480 / 640×400), `X/Y = -1` → SDL
  centres the window. `videoInit` uses them for the `GfxWindowInitSettings`.
- `videoSaveWindowState()` reads `wmAPI->get_fullscreen_state` /
  `get_maximized_state` / `get_dimensions` (all already in the SDL2 wapi
  backend) and writes geometry back into the cfg vars. A maximized or
  fullscreen window keeps its last *restored* size/pos on disk and only
  updates the flag. `Video.Fullscreen` now round-trips too.
- **Verified:** `-level_09` / `-level_20` boot crash-free (6/6 GE_PCDUMP
  frames each, unregressed); `ge007.ini` gains a populated `[Window]`
  block on exit; relaunch reads it back with no unknown-key warnings.

## B — `level_sweep.sh` STALLED verdict

Pure shell (not built). A level that renders a few frames then freezes
while the process stays alive (the Silo `-level_20` fly-down case,
D117/D134 machine-load sensitivity) previously reported **PASS** because
`last=$(ls ppm/*.ppm | tail -1)` was non-empty. Now the watchdog records
`stalled=1` when the frame count is frozen for `STALL_SECS`, and `alive=1`
if the process was still running when the watchdog gave up →
`STALLED (froze at N/M frames)`. A complete dump set is still a real PASS.
STALLED is retried once (like NO-FRAMES) before it's believed.

## C — raw mouse input (`Input.MouseRawInput`, default 0)

`port/src/input.c inputInit`: when `MouseRawInput=1`, sets
`SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE=0` and
`SDL_HINT_MOUSE_RELATIVE_MODE_WARP=0` before `SDL_SetRelativeMouseMode` —
removes OS pointer acceleration / warp emulation from aim. Off by default
so nothing changes unless the user opts in.

## E — `--help` / `--version`

`port/src/main.c`, handled before `crashInit()`:
- `--version` → rom id / target platform / build hash, exit 0.
- `--help` / `-h` → the above + usage + the 21 solo-level `-level_XX`
  table (matches `tools_pc/level_sweep.sh` / `playtest.sh --list`), exit 0.

## Not done (Tier 2/3 from the board)

F (minidump), G (PNG screenshots), H (rumble), I/J (in-render overlay /
toasts) — left for a dedicated session.
