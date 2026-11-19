# D508

<!-- from the old section-F/H table row -->
| D508 | **Forced aspect ratios: Aspect ratio gains 16:9 and 21:9** — Port-only; D447 extension; full `## D508` entry at file tail | FIXED (by-eye pass owed) |

## D508: forced aspect ratios

Port-only (`port/src/video.c`, `optionsoverlay.c`). `Video.AspectMode` (D447) had Fill window (0) and Original (1). Maintainer ask: let players force other shapes. The D447 output-rect path already letter/pillarboxes to any target aspect (`gfx_set_output_aspect`) and sets `gfx_current_dimensions.aspect_ratio` to it, so Native widescreen projects the world at the forced aspect with no fast3d change. New values 2..3 = 16:9, 21:9 (7:3). 4:3 (same as Original unless the watch Ratio is 16:9) and Cinema 2.35:1 (near-identical to 21:9) were dropped as redundant after the first by-eye pass; ini range widened to 0..3, default and the Original N64 preset (1) unchanged. Any non-Fill, non-Original value counts as "Custom" in the Display mode row (it is a bundled key). The overlay and mouse mapping follow the output rect as before. A window narrower than the target letterboxes, wider pillarboxes.

**Verified (headless):** build rc=0; `-level_09` frame 400 at 1280x720 for modes 3, 4, 5: 16:9 fills the window, 21:9 and 2.35:1 letterbox with the expected bar sizes (`scratch/asp_sheet.png`); F10 Video dump shows "21:9" with Display mode "Custom"; tree / preset / reset probes pass. **Owed (maintainer):** by-eye in a level and the front end for each ratio, HUD/crosshair/menu placement at 21:9 and 2.35:1 (the HUD uses the same Hor+ path as a real ultrawide window), mouse aim at the letterbox edges, fullscreen on a 16:9 panel.
