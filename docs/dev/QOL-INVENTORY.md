# QoL Inventory — PD-vs-GE options diff

Status: **read-only research, parked** (input to project Phase 4). No code
changed. Companion to `WIDESCREEN-FOV-PLAN.md` and `UNLOCKED-FPS-PLAN.md`.

Method: diffed the config surface of both ports — every `configRegister*`
key in `pd_port/port/src/*.c` vs `port/src/config.c` + the F10 overlay
(`port/src/optionsoverlay.c`) + PD's `optionsmenu.c`. Principle applied:
**where PD has a QoL feature that is achievable here, it is the standard;
GE keeps its own extras.**

Classification key — **P** = port-only (no rules issues), **G** = needs a
game-code exception (rule #2 sign-off, per `WIDESCREEN-FOV-PLAN.md` framing).

## GE already has (baseline — keep)

Input: mouse enable/capture/aim+turn speed/invert Y/Y scale/smoothing/raw
input, aim band, hipfire pitch speed, menu pointer speed + mode (GE's menu
cursor is injected entirely in `port/src/input.c` — no game code), pad
deadzone/trigger %. Video: VSync, FpsCap, MSAA, TextureFilter,
FixMipTextures, WrapFix, Fullscreen, Window geometry. Game:
ScreenShakeIntensity. Debug: FrameDump, InputLog.

## PD has, GE lacks (candidates)

| Feature | PD location | Class | Effort | Notes / priority |
|---|---|---|---|---|
| **Key rebinding** (config-string binds per controller/action) | `input.c:69,627-675,1548` (`bindStrs`, `inputParseBindString`) | P | M | **High.** Explicit Phase 4 debt ("rebinding UI" — note PD has *no* UI either; config-file driven). Port-only in `port/src/input.c`. |
| **SkipIntro** _(DONE M-83, D216)_ | `Game.SkipIntro` -> `src/game/lv.c` | P | S | Boots to the SELECT FILE menu, skipping the legal screen + Nintendo/Rare/GoldenEye logo attract loop. Reuses the game's own post-intro route (sets `is_first_time_on_main_menu=FALSE` + `menu_update=MENU_FILE_SELECT`); one `#ifdef PORT` line in the existing GE_STARTMENU block. Verified headless (screenshots). |
| **DisplayFPS** (+ interval) | config `Video.DisplayFPS(Interval)` | P | S | Cheap; useful for FPS-plan verification too. |
| **Anisotropic filtering** (≤16×), MipmapFilter, TextureFilter2D | `gfx_opengl` texparam path | P | S–M | **High at 4K.** GE's `gfx_opengl.cpp` has mipmap nY but no aniso (`GL_TEXTURE_MAX_ANISOTROPY_EXT`). Port-only. |
| Per-pad tuning: per-stick deadzones, stick scale, rumble scale, device index, swap sticks, C-button mapping | `input.c` padsCfg block | P | M | Medium. Matters for real-controller users; PD pattern is a config section per pad. |
| FakeGamepads / FirstGamepadNum / UseHIDAPI | `input.c` | P | S–M | Niche (local MP on PC); defer until MP is actually played. |
| Window polish: DefaultFullscreen/Maximize, CenterWindow, AllowHiDpi, ExclusiveFullscreen | `video.c` vid* block | P | S | HiDpi + center are the useful ones on modern Windows. |
| VSync adaptive (−1..10) | `Video.VSync` range | P | S | Note for FPS plan — GE's 0/1 toggle is fine until then. |
| **CenterHUD** (0/1/2 = left/center/right) | `Game.CenterHUD` → `g_HudAlignModeL/R` | G* | — | Already scoped in `WIDESCREEN-FOV-PLAN.md` Option B (Phase 1 decision). Not a separate item. |
| Per-player FovY + FovAffectsZoom | `Game.Player%d.FovY`, `g_PlayerExtCfg` | G | M | PD does this in game code. GE's port-only RSP FOV slider (widescreen plan Phase 4) covers the global case; per-player is a stretch goal needing the exception class. |
| MemorySize (4–2048 MB emulated RAM) | `Game.MemorySize` → `g_OsMemSizeMb` | P? | S | Check how GE sizes its OS memory heap; if fixed, make it a config knob (stability lever for heavy levels). |
| MaxExplosions / GEMuzzleFlashes / DisableMpDeathMusic | PD decomp edits | G | S | Low priority; rule-#2 cost > benefit. Skip unless asked. |
| GlareBrightness / OverexposureScale / FramebufferEffects | PD post-FX | — | — | Not applicable: GE's fast3d has no framebuffer-effect path. Excluded. |

## GE has, PD lacks (keep as-is)

`Video.FixMipTextures`, `Video.WrapFix`, `Input.AimBand`,
`Input.HipfirePitchSpeed`, `Input.MouseSmoothing`, `Debug.FrameDump`,
`Debug.InputLog`. No action.

## Suggested Phase 4 order (after audio; ordering vs widescreen/FPS TBD)

1. **Quick wins (all P, small):** SkipIntro, DisplayFPS, aniso + mipmap
   filters, window HiDpi/center.
2. **Rebinding** (P, medium) — the headline Phase 4 item; PD pattern is
   config-string driven, no UI required for parity.
3. **Per-pad tuning** (P, medium).
4. Niche: fake gamepads / HIDAPI (when MP gets played); MemorySize knob
   (after the heap check).

Everything here is parked behind audio like the other plans; nothing in this
inventory implies an implementation decision yet.
