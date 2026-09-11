# QoL Inventory — PD-vs-GE options diff

Status: **partially implemented.** M-83 landed three port-only quick wins —
DisplayFPS (D213), anisotropic filtering (D212), and the FOV slider (D211, via
`WIDESCREEN-FOV-PLAN.md` Phase 4). Rebinding / SkipIntro / per-pad tuning /
window HiDpi still open. Companion to `WIDESCREEN-FOV-PLAN.md` and
`UNLOCKED-FPS-PLAN.md`.

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
| **Key rebinding** _(DONE M-83, D214)_ | `port/src/input.c` `[Input.Bind]` section | P | M | Keyboard only, 12 actions, `Input.Bind.* = Key,Key` (SDL scancode names), file-driven like PD. Defaults = prior FPS layout (verified). Per-controller pad rebinding + a UI still open. |
| **SkipIntro** _(DONE M-83, D216)_ | `Game.SkipIntro` -> `src/game/lv.c` | P | S | Boots to the SELECT FILE menu, skipping the legal screen + Nintendo/Rare/GoldenEye logo attract loop. Reuses the game's own post-intro route (sets `is_first_time_on_main_menu=FALSE` + `menu_update=MENU_FILE_SELECT`); one `#ifdef PORT` line in the existing GE_STARTMENU block. Verified headless (screenshots). |
| **DisplayFPS** _(DONE M-83, D213)_ | config `Video.DisplayFPS` | P | S | Top-right readout, config-only; ~0.5 s sample window. |
| **Anisotropic filtering** _(DONE M-83, D212)_ | `Video.Anisotropy` (1–16, dflt 4) | P | S | fast3d already had the GL hook + a hardcoded 4×; M-83 exposed it as config. MipmapFilter / TextureFilter2D still not surfaced. |
| Per-pad tuning: per-stick deadzones, stick scale, rumble scale, device index, swap sticks, C-button mapping | `input.c` padsCfg block | P | M | Medium. Matters for real-controller users; PD pattern is a config section per pad. |
| **Rumble → D224 (M-87, new).** GE's port fully no-ops the N64 Rumble Pak (`port/src/libultra.c:1220-1223` `osMotorInit/Start/Stop` all stubbed "no accessories on the PC") — GE genuinely has a rumble subsystem (`src/motor.c`, `src/joy.c`) that never reaches any output. **PD's PC port wires this straight through to real gamepad rumble**: `inputRumbleSupported()`/`inputRumble()` call `SDL_GameControllerRumble()` when the connected pad reports haptics, `Input.PadN.RumbleScale` config + an options-menu slider, `osMemSize`-style "pretend the accessory is there" pattern applied to a real one instead of faked. | P | M | Concrete PD-port precedent, straightforward port-layer wiring (no `src/` game-logic touch — GE's `motor.c` calls already exist, just need `osMotorStart/Stop` in `libultra.c` to route to a real `SDL_GameControllerRumble` call the way PD's do). |
| FakeGamepads / FirstGamepadNum / UseHIDAPI | `input.c` | P | S–M | Niche (local MP on PC); defer until MP is actually played. |
| Window polish: DefaultFullscreen/Maximize, CenterWindow, AllowHiDpi, ExclusiveFullscreen | `video.c` vid* block | P | S | HiDpi + center are the useful ones on modern Windows. |
| VSync adaptive (−1..10) | `Video.VSync` range | P | S | Note for FPS plan — GE's 0/1 toggle is fine until then. |
| **CenterHUD** (0/1/2 = left/center/right) | `Game.CenterHUD` → `g_HudAlignModeL/R` | G* | — | Already scoped in `WIDESCREEN-FOV-PLAN.md` Option B (Phase 1 decision). Not a separate item. |
| Per-player FovY + FovAffectsZoom | `Game.Player%d.FovY`, `g_PlayerExtCfg` | G | M | PD does this in game code. GE's port-only RSP FOV slider (widescreen plan Phase 4) covers the global case; per-player is a stretch goal needing the exception class. |
| MemorySize (4–2048 MB emulated RAM) | `Game.MemorySize` → `g_OsMemSizeMb` | P? | S | Check how GE sizes its OS memory heap; if fixed, make it a config knob (stability lever for heavy levels). |
| MaxExplosions / GEMuzzleFlashes / DisableMpDeathMusic | PD decomp edits | G | S | Low priority; rule-#2 cost > benefit. Skip unless asked. |
| GlareBrightness / OverexposureScale / FramebufferEffects | PD post-FX | — | — | Not applicable: GE's fast3d has no framebuffer-effect path. Excluded. |
| **HUD scale → D226 (M-87, new).** Ammo counter + bottom-left pickup/status text + top-of-screen dialogue all funnel through one function, `textRenderOutlined()` (`textrelated.c:688`), from two `bondview2.c` call sites — plain RDP texture-rectangles at native glyph size, no scale factor anywhere. Menus/options/MP UI share the same function but must NOT be scaled by this (different constraint, own layout). | P | M | User-requested. No PD precedent. Scale-about-anchor (bottom stays bottom, top stays top), matching D211's HUD-static principle. F10 slider candidate alongside FovScale. |
| **Debug unlock-all → D225 (M-87, new, dev-tooling not player QoL).** One env-gated `#ifdef PORT` short-circuit at `fileGetIsCheatUnlocked()` (`src/game/file2.c:391`) unlocks **both** all-mission-select **and** the full cheat menu in one change — the two gates already share that single function. Bonus: exposes leftover Rare dev cheats (`CHEAT_LINEMODE` wireframe, `CHEAT_BONDPHASE`, `CHEAT_DEBUG_POS/UNK5`) that are otherwise unreachable. | P | S | User-requested testing convenience ("always enable all level access + all cheats... goes a long way"). Default off, pure query-time override, zero save-data/gameplay-logic touch. |

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
