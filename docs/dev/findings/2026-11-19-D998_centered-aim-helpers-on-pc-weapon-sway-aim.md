# D998

## D998: Centered aim helpers on PC -- weapon sway, Aim assist, Aim entry, Stop on aim -- BUILT 2026-11-19 (Windows, Linux and Switch builds; not played with a pad here)

**Problem.** On a gamepad in Centered aim style the PC had neither the weapon sway nor the helpers the Switch has: the weapon stayed fixed while turning, and no aim assist, aim entry or stop-on-aim. All of it was compiled for the Switch only (`PLATFORM_SWITCH` in `input.c`, the whole aim part of `nxgame.c`, the hook in `lv.c`).

**Change (port layer only).**
- `input.c`: the weapon sway (`padAimSway`, called from `padDirectCompute`), the aim-sensitivity multiplier and the Aim entry / Stop on aim steps are built for every platform. `Input.PadAimLookSens` is registered everywhere (default 100 on PC, 75 on the Switch). The Centered stick curve, turn acceleration and Look ahead stay Switch-only.
- `nxgame.c`: Aim assist, Aim entry and Stop on aim are built everywhere. `Game.AimZoom` is built too but `nxIsHandheld()` is 0 off the Switch, so it does nothing there (a later change can give it Steam Deck detection); its row and `Look ahead` stay in the Switch menu.
- `src/game/lv.c`: the existing port hook `nxAimAssistFrame` (before the player's own shot tests) is now `#if defined(PORT)` instead of `PORT && __SWITCH__`. No game-logic change; the call is a no-op unless Aim assist is on and the aim style is Centered.
- `optionsoverlay.c`: Aim sensitivity, Aim assist, Aim entry and Stop on aim rows on the gamepad part of each player's Input page, shown while the aim style is Centered and the mode is Enhanced. Defaults as on the Switch (assist High, entry Move view, stop on).

**Verified.** Windows, Linux and Switch builds; the option tree probe is unchanged. **Not exercised.** Playing with a pad (no controller here): the sway, the slow-down on a guard, the aim entry and stop on aim on PC. Keyboard + mouse Centered aim has no sway or helpers (they are pad paths).

**Confidence.** High that the shared code is the Switch's; the PC feel is untested.
