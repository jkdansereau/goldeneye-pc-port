# D293

<!-- from the old section-F/H table row -->
| D293 | **QoL ask: no discoverable way to quit the game from the UI (M-141, Steam Deck user playtest — "couldn't see a way to exit the game. Maybe build this into the…** — full `## D293` entry at file tail | FIXED (M-141) — "Quit to desktop" row added to the F10 overlay; build-verified Win+Linux, not yet live-confirmed.… |

## D293 — QoL ask: no discoverable way to quit the game from the UI (M-141, Steam Deck user playtest — "couldn't see a way to exit the game. Maybe build this into the…

**QoL ask: no discoverable way to quit the game from the UI (M-141, Steam Deck user playtest — "couldn't see a way to exit the game. Maybe build this into the settings/options menu.").** Confirmed in code: the only quit path is `SDL_QUIT` (`port/src/video.c:515`, the OS window-close event / Alt+F4) — there is no Escape-key handler and no "Quit" entry in the front-end menus or the F10 options overlay. Fine on a desktop with visible window chrome; a real gap on Steam Deck / controller-only / fullscreen setups with no easy way to reach the OS window controls.

**Status (from §F table):** FIXED (M-141, `release/v0.3.0` local). Added a "Quit to desktop" row to the F10 overlay (`port/src/optionsoverlay.c`) — a new `ROW_ACTION` kind (not config-backed, same non-config pattern as the existing `__Resolution` row) that calls the identical `sysLogPrintf` + `configSave()` + `exit(0)` path `SDL_QUIT`/Alt+F4 already use. Reachable by keyboard (arrow/Enter when the row is selected), mouse click, or gamepad A/X — no new Escape binding was added (D145 already dedicates ESC to menu-back / cursor-release). Build-verified on Windows (`ninja`, clean link) and Linux (WSL `Ubuntu-24.04` cross-build); `tools_pc/verify.sh dam` PASS, no regression. **Not yet live-confirmed** — needs a human to actually select the row on a real session (headless/SSH-launched smoke tests can't reach the Deck's own X/gamescope display to drive input).
