# D498

<!-- from the old section-F/H table row -->
| D498 | **Xbox 1.2 Christmas / 1.3 Frost / 1.4 Elektra gamepad presets: `Input.PadPreset` gains 1/2/4 (stick roles per D394, buttons unchanged); Jinx and Custom keep `Input.NaturalPitch`** — full `## D498` entry at file tail | FIXED 2026-10-03, maintainer-verified with a pad |

## D498 — Xbox 1.2 Christmas / 1.3 Frost / 1.4 Elektra gamepad presets (2026-10-03)

ROADMAP 5a "Gamepad rebinding + modern controller presets"; reference table in `## D394`.
**Change (port only; commits e94621b5 by Qwen, merge fb8ed152, follow-up c12fc99e):** `Input.PadPreset` 0 = 1.1 Jinx, 1 = 1.2 Christmas, 2 = 1.3 Frost, 3 = Custom (unchanged value), 4 = 1.4 Elektra; menu order Jinx, Christmas, Frost, Elektra, Custom (`inputPadPresetStep`, `port/src/input.c`). Buttons are identical for all non-Custom presets. `stickSolitare()`: Christmas/Frost force HONEY, Elektra forces SOLITARE; Jinx and Custom keep `Input.NaturalPitch` (c12fc99e fixed the first version, which forced SOLITARE for Jinx too and broke the NaturalPitch=0 escape hatch). `stickSwap()`: Frost and Elektra swap the physical sticks in-stage (never in menus).
**Known limits:** the forced style applies to mouse and keyboard too (one control style per player), so a mouse user on Christmas/Frost gets the legacy D166 pitch pulses; after a swap the deadzone/sensitivity settings follow the function (look settings apply to whichever stick looks), not the physical stick; the Xbox "left stick aims the crosshair" behaviour and the D-pad-copies-left-stick behaviour are not reproduced (see D394).
**Verified:** build only.
**Owed:** none.
**Status:** FIXED, maintainer-verified 2026-10-03.
**Maintainer pad check 2026-10-03:** 1.2 Christmas, 1.3 Frost and 1.4 Elektra work as described.
