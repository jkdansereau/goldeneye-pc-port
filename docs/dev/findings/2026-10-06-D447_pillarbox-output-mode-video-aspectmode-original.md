# D447

<!-- from the old section-F/H table row -->
| D447 | **Pillarbox output mode (`Video.AspectMode` = Original) for the Original N64 preset; crop overscan no longer cancels Wide/Cinema.** — full `## D447` entry at file tail | FIXED (build + 3 captures verified; Wide/Cinema, 16:9 Ratio, mouse and 1964 comparison owed to a human) |

## D447 - Pillarbox output mode for the Original N64 preset (port-only)

Design: `docs/dev/notes/PILLARBOX-DESIGN.md` (local). Maintainer decisions 2026-09-30: exact 4:3 (full 320x240 frame); 16:9 while the game's watch-menu Ratio is 16:9; Crop overscan must never remove the player's Wide/Cinema bars; F10 full-window deferred.

**Done (steps 1-5, 7 partly).**
- `port/fast3d/gfx_pc.cpp`: `gfx_set_output_aspect()` / `gfx_get_output_rect()` (declared in `gfx_api.h`). `gfx_start_frame` computes a centred, even-sized rect for the target aspect; `gfx_current_dimensions` and `gfx_current_game_window_viewport` both become the rect, so `different_size` stays false and the inherited PD offset path in `gfx_adjust_viewport_or_scissor` places every viewport/scissor/rect inside it (also per-player viewports: nothing is hard-coded to one viewport). Bars are the frame clear. Aspect 0 = bit-identical path. Also: mode-0 `aspect_scale` uses `gfx_current_dimensions.aspect_ratio`; autoresize fullscreen FBs and `gfx_copy_framebuffer` use window size / rect offsets. MSAA FB stays window-sized (same-size branch).
- `port/src/video.c`: `Video.AspectMode` (0 Window, 1 Original), read every frame in `videoStartFrame`; target = `get_screen_ratio()==SCREEN_RATIO_16_9 ? 16:9 : 4:3` (read only, no `src/game` edit). Preset row (N64 = 1, port = 0). GE_PCDUMP / screenshots now capture the whole window (bars included).
- `port/src/optionsoverlay.c`: "Aspect" row (Window/Original) and `kResetDefaults` entry (0). Front PC Options reuses the overlay row table, so it gets the row and pages automatically.
- Crop gating: the safe-area cache only adopts SP viewports >= 90% of the canvas height. Wide/Cinema (and split-screen half viewports) no longer define the crop band, so their letterbox survives `SafeAreaCrop=1`. If no full viewport has been seen yet, a centred 220-line band is seeded. Port-only.
- Mouse: `input.c` front-end pointer maps across the output rect (`videoGetOutputRectFrac`); the F10 overlay mouse already inverts through `gfx_get_ui_screen_rect`, which includes the rect offset. F10 stays drawn inside the rect (step 6 deferred).

**Measured (1280x720 window, `-level_09`, AspectMode=1, SafeAreaCrop=0, NativeWidescreen=0, MSAA 2):** columns 160..1119, i.e. 160/160 px side bars (expected 160). Top/bottom 30/30 px = the game's own 10/240 overscan fill at 720 px. 800x800 window, MSAA 1: full width, 125/125 px top/bottom = 100 letterbox + 25 game fill. Base smoke (AspectMode=0, frame 300, self-quit) OK.

**Not done / owed:** runtime check of Wide/Cinema crop gating and of Ratio 16:9 (code path only); mouse mapping not exercised; golden bunker1 at Window mode not run (identical by construction); MSAA 4/8/16, fullscreen toggle, PAL, Linux CI and the MP branch not run; step 6 (F10 full window) deferred by decision. Known cosmetic: with crop off, D246's 1-unit dark-blue edge line is visible at the rect's left/right edges. Human: compare against 1964.

Confidence: high for the rect and letterbox geometry (measured), medium for crop gating and Ratio 16:9 (unverified at runtime).
