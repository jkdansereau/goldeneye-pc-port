# D518

<!-- from the old section-F/H table row -->
| D518 | **Pad layout preset was one global value: setting another player's preset changed the main player's mapping too** — Port-only; D498 follow-up, maintainer report; full `## D518` entry at file tail | FIXED (live pass 2026-10-04 incl. split-screen 2P) |

## D518: The pad layout preset was one global value -- setting another player's preset changed the main player's mapping too

**Problem (maintainer report 2026-10-04, during the D516/D517 stress test).** In single player, opening Select player -> Player 2 and changing that seat's "Layout preset" also changed P1's live mapping -- "I don't want it to affect at all".

**Root cause.** `Input.PadPreset` was a single global int in input.c; the per-seat Controller pages (D469 Select player, `s_padSeat`) all edited the same value, and `padTabGet()` built every seat's table from it. The custom binds were already per seat (`Input.Pad[N].<Action>`); only the preset was shared.

**Fix (port-only).** input.c: `padPreset[MAX_PADS]`; ini keeps `Input.PadPreset` as **seat 1's** key (existing inis keep their P1 value) and adds `Input.PadPreset2/3/4` (default Jinx). All the preset-derived helpers (`padPresetCustom`, `padPresetEff`, `stickSolitare`, `stickSwap`, `styleIsSolitare`) take a seat; every call site is inside `inputComputePadSlot(int idx, ...)` and passes its own slot. The table signature hashes all four presets (custom bind strings included when any seat is Custom). New accessor `inputPadPresetPtr(seat)`. optionsoverlay.c: new `padSetSeat()` re-points the "Layout preset" row's `ptr` and the Custom-only bind rows' `showPtr` at the selected seat's value; all three seat-change sites (F10 link open, front-end `optionsRowLinkOpened`, ROW_PADSEAT step) go through it. GE_PADMAPTEST now drives the per-seat array (all seats Custom in the custom phase).

**Verified.** Build rc=0. Headless: `GE_PADMAPTEST` PASS (seat-independence checks c1-c11), `GE_OPTIONTREEPROBE` + `GE_PRESETPROBE` PASS, and a temporary `Input.PadPreset2 = 4` in the ini produced exactly `pad table rebuilt (presets 0/4/0/0)` (ini restored byte-identical after). **Live pass (maintainer, 2026-10-04):** single player, P2 preset changed -- P1's page and mapping untouched, bind rows follow the per-seat Custom state; "good". **Split-screen 2P live pass:** each seat ran its own preset in a mission ("all good").
