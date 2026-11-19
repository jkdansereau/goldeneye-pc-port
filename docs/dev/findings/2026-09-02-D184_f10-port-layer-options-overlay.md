# D184

<!-- from the old section-F/H table row -->
| D184 | **F10 port-layer options overlay — the approach-(C) surface from `OPTIONS-MENU-PLAN.md` (M-37).** — full `## D184` entry at file tail | FIXED (2026-09-26 bookkeeping: shipped; user reports it feels good in v0.4.0 play). Earlier: **LANDED, port-only, no `#ifdef PORT` in `src/`.** Build links 241/241.… |

## D184 — F10 port-layer options overlay — the approach-(C) surface from `OPTIONS-MENU-PLAN.md` (M-37).

**F10 port-layer options overlay — the approach-(C) surface from `OPTIONS-MENU-PLAN.md` (M-37).** New `port/src/optionsoverlay.c` + `port/include/optionsoverlay.h`: an immediate-mode overlay drawn as its own fast3d 2D DL, appended after the game DL in `gfx_pc.cpp gfx_run()` (`optionsOverlayEmit()` → NULL when closed ⇒ **zero bytes appended, golden dumps byte-identical**). F10 toggles (`video.c videoPumpEvents`, next to F12); while open, `input.c inputComputePad(0)` returns a neutral pad and routes arrows/enter/wheel/mouse-buttons to `optionsOverlayHandleInput()` (mirrors the WI-1/D180 swallow). ESC closes; `configSave()` on close. `config.c` gains `configForEachOption()` + a `configSetOptionMeta()` label/step/enum side table (no per-key knowledge in config.c). Live knobs (VSync/FpsCap/TextureFilter via `videoRequestLiveConfig()`; mouse + screen-shake via the registered pointer) apply immediately; MSAA tagged "(restart)". v1 rows per plan §3: VSync, FpsCap, MSAA, TextureFilter, MouseAimSpeed, MouseTurnSpeed, MouseInvertY, MouseCaptureMode, ScreenShakeIntensity.

**Status (from §F table):** **LANDED, port-only, no `#ifdef PORT` in `src/`.** Build links 241/241. **Runtime verification NOT run this session** — the headless env kills the GUI process after ~8 s and `gfx_opengl_dump_bound_fbo` writes 0-byte PPMs here, so framediff / 60 s soak / overlay-frame eyeball are all owed on an interactive machine. Overlay-closed no-op is code-evident (emit returns NULL before touching the DL buffer).
