# D512

<!-- from the old section-F/H table row -->
| D512 | **Game.HudScale also scales the F10 overlay and FPS counter** — Port-only; D226/D510 follow-up; full `## D512` entry at file tail | DONE headless (live check owed) |

## D512: Game.HudScale applies to the F10 overlay (S4)

**PD reference:** pd_port has no HUD/menu scale to follow (the only scale hit is the debug menu, `src/game/debug1.c`), so this is GE-native: the whole dialog scales (box, text, hint line, FPS counter), not only the text.

**Change (port-only, `optionsoverlay.c`):** D510's canonical canvas now also divides by the HUD scale: `ovW()/ovH()` shrink by 100/hud and `ovScale()` (the fast3d overlay scale) grows by hud/100. The box stays centred, always fully on screen, and a larger scale just shows fewer rows (the existing scroll). Mouse hit-testing, dropdown popups and the FPS corner already derive from `ovW/ovH`, so they follow. The text clip size passed to `textRender` is now the canonical size. Changing the slider re-lays out on the next frame (layout is recomputed every emit). 100 % is the D510 mapping exactly.

**Not done:** the front-end PC Options page (`frontoptions.c`) is hand-laid on the file-select paper art in the game's own 440x330 display list; scaling only its contents would misalign with the art, so it is left at 100 %. Say if you want it (it would need the paper drawn at the same scale).

**Verified (headless):** 100 %: overlay-open front-end and `-level_09` dumps match the release build apart from the animated title-bar shimmer (the release build differs from itself in the same rows); level box widths at 640x480 are 295 / 392 / 588 px at 75 / 100 / 150 % (expected 294 / 392 / 588), at 1280x720 588 / 784 / 1176, all inside the window; contact sheet shows the 150 % hub scrolling with the box on screen. Tree / preset / reset probes PASS; clean ini byte-identical to the release build's; 60 s `-level_09` smoke rc=0. Pointer mapping is correct by construction (same helpers) but was not exercised headless. **Owed (maintainer):** by-eye pass at 75 / 100 / 150 in a level and on the front end with mouse and pad; the front-end PC Options page decision above.
