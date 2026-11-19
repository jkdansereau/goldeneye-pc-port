# D180

<!-- from the old section-F/H table row -->
| D180 | **Native-PC input pass (M-XX QoL run, `qol/native-pc-input-menu`, port-only).** — full `## D180` entry at file tail | FIXED (2026-09-26 bookkeeping: shipped; user reports it feels good in v0.4.0 play). Earlier: LANDED, port-only, no `#ifdef PORT`.… |

## D180 — Native-PC input pass (M-XX QoL run, `qol/native-pc-input-menu`, port-only).

**Native-PC input pass (M-XX QoL run, `qol/native-pc-input-menu`, port-only).** Three parts, all config-gated with default = prior behaviour: **(WI-1) `Input.MouseCaptureMode`** (0 = legacy always-grab; 1 = Quake-style click-to-lock) in `port/src/input.c` + `port/src/video.c` — in mode 1 the OS cursor is free until a click lands in the game window (`SDL_MOUSEBUTTONDOWN` → `inputNotifyClick`), and ESC / focus-loss / entering any front-end menu (`current_menu != MENU_RUN_STAGE`) frees it again; re-entering a stage while still "armed" re-locks so unpausing needs no click. `reconcileGrab()` runs once per controller-0 poll. Mouse buttons are suppressed from the game while the cursor is free in-stage (no phantom fire). Controller paths untouched. **(WI-2) absolute-cursor menu tracking** — when capture mode is on and the cursor is free in a menu, the D165/D169 pointer P-controller takes its target from the real cursor's absolute window position mapped onto the live virtual front-end rect (`getPlayer_c_screen*`), giving true 1:1 tracking instead of the relative-delta estimator. Relative path unchanged for legacy/grabbed. **(B3) `Input.MouseAimSpeed` default 25 → 16** (aim mode still overshot at 25 per the backlog).

**Status (from §F table):** LANDED, port-only, no `#ifdef PORT`. Build 241/241, `-level_09` framediff 3/3 within threshold (unregressed), `GE_STARTMENU` menu boot crash-free. **Feel-checks owed** (headless can't drive the mouse): click-to-lock ergonomics, absolute menu tracking, the new aim-speed default.
