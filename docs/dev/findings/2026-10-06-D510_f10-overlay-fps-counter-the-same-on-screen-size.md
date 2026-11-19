# D510

<!-- from the old section-F/H table row -->
| D510 | **F10 overlay + FPS counter the same on-screen size on the front end and in a level** — Port-only; D504 follow-up; full `## D510` entry at file tail | FIXED (live check owed) |

## D510: the F10 overlay rendered ~27 % smaller on the front end than in a level

**Symptom (maintainer):** at the same window size the overlay box and text were a different size on the main menu / File Select than in Bond's view. The FPS counter (same display list) too.

**Diagnosis (logged, 640x480 and 1280x720):** hypotheses (a) and (b) were both true, (c) was not. (b) `viGetX() x viGetY()` is 440x330 on the front end and 320x240 in a level (`front.c` `viSetXY(440, 330)`; `osViSetMode` resizes fast3d's SCREEN_WIDTH to match). The overlay laid out in those raw units (a fixed 196-unit hub card), so it covered 196/440 = 45 % of the window on the front end versus 196/320 = 61 % in a level (294 px vs 392 px at 640 wide). (a) The safe-area crop rect (`gfx_get_ui_screen_rect`) follows the last game viewport, so the overlay was also stretched by the in-level "Full" viewport inset and the 1-unit D246 edge trim (logged rect was the full window at 640x480 in both, but it is viewport-dependent).

**Fix (port-only):** the overlay is PC chrome. `optionsoverlay.c` now lays out on a canonical 320-wide canvas (`ovW()`/`ovH()`, height kept at the real aspect) and calls `gfx_set_overlay_scale(viGetX()/320)`; fast3d scales the overlay DL's rectangles/texrects by that factor about the origin (after the D226 HUD span scale) and, while the overlay DL runs (`g_overlay_window_space`), bypasses the safe-area crop and the D493b edge trim. `gfx_get_ui_screen_rect` returns the window-space rect, so the mouse mapping (now on the canonical canvas) follows. The overlay-closed frame is untouched (flag only set around the overlay DL; no overlay DL, no change).

**Verified (headless):** front end vs level overlay shots at 640x480 and 1280x720 identical in box size and position; also with native widescreen and with `SafeAreaCrop=0`; FPS counter identical size and top-right offset at 1280x720; tree / preset / reset probes PASS; 60 s `-level_09` smoke rc=0. **Owed (maintainer):** live check at the usual window size and fullscreen, on the front end and in a level; mouse hover/click rows in both; split-screen (2P) with the overlay open (safe-area crop is already off there). Not done: `Game.HudScale` on the menu (S4).
