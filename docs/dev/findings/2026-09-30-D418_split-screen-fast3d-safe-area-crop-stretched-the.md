# D418

<!-- from the old section-F/H table row -->
| D418 | **Split-screen: fast3d safe-area crop stretched the last-set (half-height) viewport over the whole window, so each player's view filled the screen and the last-drawn (shuffled) player won -> alternating full-screen flicker** — full `## D418` entry at file tail | RESOLVED (`gfx_set_split_screen`, port-only). |

## D418 — Split-screen alternating full-screen views: safe-area crop remapped sub-rect viewports (issue #99)

**Symptom (user, live):** in 2P the view flickers between both players constantly. GE_MPDUMP (temp) frame capture: each frame is ONE player's view filling the whole window, alternating in shuffled draw order (`get_nth_player_from_shuffled`) — no split visible.

**Root cause:** `gfx_adjust_viewport_or_scissor` (fast3d) maps the last-set SP viewport's Y range onto the whole window when `Video.SafeAreaCrop` is on (default; designed for the solo 220-line viewport). In 2P each player's viewport is 109 lines, so each was stretched to full height and the last drawn covered the other.

**Fix (port only):** `gfx_set_split_screen()` turns the crop (Y and X margin) off while a 2+ player stage runs; `videoSyncSplitScreen()` (video.c) sets it from `getPlayerCount()` + stage != title, called before every `gfx_run` — **including the scheduler's direct call in `libultra.c`**, which bypasses `videoSubmitCommands` (my first attempt hooked only that and had no effect: worth remembering, see porting-notes). Verified by frame capture: two stable stacked views over 4 consecutive frames, original N64 letterbox, viewports y=119/230 h=109; solo golden gate 3/3.

**Status:** RESOLVED (harness); live pad play + HUD/widescreen (plan 2c, D334/D335 in MP) still owed.

**2026-09-30 note (merge of release/v0.4.1, D447 interaction):** D447 gated the crop cache so only viewports >= 90% of the canvas height define the crop band (Wide/Cinema and split halves keep their own letterbox) and added an output rect (Original aspect = centred 4:3/16:9 rect, `gfx_current_game_window_viewport` = that rect). The two compose without overlap: in a 2+ player stage D418's `g_split_screen` still bypasses the crop entirely (`safeTop/safeHeight = SCREEN_HEIGHT`, no X margin), so the N64 split layout maps 1:1 onto the canvas; D447's seed/gating only touches the cached band, which is unused while split. The canvas itself is then placed in the window or, in Original mode, inside the pillarboxed output rect by `gfx_start_frame`. Merge conflict was textual only (adjacent globals in gfx_pc.cpp).
