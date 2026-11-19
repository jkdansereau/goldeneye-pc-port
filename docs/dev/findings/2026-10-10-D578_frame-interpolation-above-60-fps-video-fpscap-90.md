# D578

<!-- from the old section-F/H table row -->
| D578 | **Frame interpolation above 60 fps (`Video.FpsCap` 90/120/144): extra matrix-blended frames between game frames, paced by the D481 render worker (rework of PR #137 by f1zz1ec0ke)** — full `## D578` entry at file tail | BUILT 2026-10-07 (Windows: identity gates; live 120 Hz pacing exact at 90/120; by-eye high-refresh + Deck 90 Hz check owed) |

## D578: frame interpolation above 60 fps (rework of PR #137) — BUILT 2026-10-07

**Credit.** Design and first implementation by f1zz1ec0ke (#137, their D409):
blend every `G_MTX` between the previous and current game frame, match
matrices across frames by the geometry they transform plus an occurrence
count, reject implausible pairs, port-only, inert at 60. Reworked here onto
v0.5.0's D481 render worker and generalised from a fixed 2x to any present
rate, so the Steam Deck OLED's 90 Hz works.

**Why a rework.** #137 rendered a 50/50 pass plus the real pass on the
scheduler thread and deferred the real swap into `osRecvMesg`. On v0.5.0 the
GL context belongs to the render worker (D481), so that swap would come from
the wrong thread, unlocked against `gfx_run`. A fixed 50/50 split also cannot
do 90 Hz (1.5 presents per game frame).

**Design (port-only, no game state touched).**
- *When:* `Video.FpsCap` above the VI rate (menu 90/120/144) and a display
  faster than the VI rate. The present rate is `min(cap, display refresh)`.
  Presents cannot outrun the display: VSync, or the compositor, holds the swap
  (measured ~8 ms per swap on a 60 Hz Windows panel with VSync off). So on a
  60 Hz display interpolation stays off and the log says so. Refresh is polled
  on the host thread (`gfx_sdl_poll_refresh_rate`). Off under GE_DETERM and
  GE_RENDERINLINE (no worker).
- *Timeline:* every game frame is stamped with the scheduled time S of the
  retrace it was submitted after (`g_lastViUs`, a regular grid). A present at
  wall time t shows sim time t - L, with L = one tick + the smoothed submit
  delay: alpha = (t - L - S_prev) / (S - S_prev). Any present rate lands each
  refresh at its true place between two game frames. A 2-tick game frame is
  handled, and a stall (> 4 ticks) resets. The cost is one tick of extra
  latency.
- *Rendering:* all passes for a game frame are drawn when its DL arrives,
  BEFORE SP/DP done is posted. The game double-buffers its DL/vtx pools
  (`dyn.c`), but textures and CPU-written data may change once it moves on.
  Each pass is copied to one of 4 present slots (`gfx_opengl_interp_store`,
  window-size RGBA8 FBOs, nearest blit). The worker then swaps the slots on
  its present clock, sleeping on the mailbox condition so a new frame always
  wins. A new frame drops unshown slots of the previous one, except that a
  frame never shown at all gets its first slot first. The first slot of a
  frame is never scheduled later than half a tick. If presents block, the
  clock re-locks just before the next refresh.
- *Pass identity:* the RSP/RDP emulation state, segment table and safe-area
  viewport cache are snapshotted at the start of a game frame and restored
  before each further pass. The GL backend's noise seed (`frame_count`)
  advances once per game frame. The port overlay is emitted once and
  replayed. D509 aspect settle and shader prewarm happen once per frame.
  `rendering_state` is not restored, because it mirrors the live GL bindings.
- *Blend:* plain lerp for projections. For modelviews (GE keeps the camera
  there), each basis row is blended and then rescaled to the blended length,
  which fixes #137's cos(theta/2) shrink on fast turns. alpha = 1 leaves the
  matrix untouched.
- *Cost guard:* the passes per frame are limited so they take at most 60% of
  a tick (EMA of the per-pass CPU cost). On a slow box this falls back to one
  pass.
- Original N64 preset: drops an interpolated cap back to 60 (not a bundle key,
  so picking 120 does not flip Display mode). F10 tip added.
- Dev switches: `GE_INTERP_ALPHA1` (every pass exact), `GE_INTERP_SELFCHECK`
  (pass 0 vs last pass pixel diff), `GE_INTERPLOG` (pacing trace),
  `GE_INTERP_FORCE` (ignore the refresh guard), `GE_PCDUMP_PRESENTS` (one
  capture per present). `verify.sh` gains `VERIFY_INI_EXTRA`.

**Verified (Windows, RTX-class GPU, 60 Hz panel, so `GE_INTERP_FORCE` and
VSync off were needed to run it).**
- Default 60: `verify.sh` facility/egypt PASS (one earlier egypt frame-900
  flake, then PASS x2).
- Identity: with every pass exact at 120, Facility 900/1200/1500 differ from
  the golden by the same gun-model region and amount as a 60 fps capture of
  the same build (3-5k px each, both). SELFCHECK: pass 0 vs pass 1 are
  identical except on ~1% of frames, by 1-4 px (known residue: a DL's first
  draws can use the GL texture bound at the end of the previous frame, and a
  later pass inherits pass 0's last binding; forcing a re-bind measured
  worse). The noise-seed fix took the early-level frames from thousands of
  differing px to 0.
- Pacing trace at 120: two slots per game frame, alpha exactly 0.5 apart,
  2-tick game frames handled. On the 60 Hz panel the second present rarely
  fits (the swap blocks ~8 ms), which is why the refresh guard exists.
- Live pacing on a 120 Hz Windows display (VSync on, `GE_INTERPLOG`, 500
  presents): 120 cap -> interval 8.333 ms avg (max 8.44), 90 cap -> 11.110 ms
  (max 11.64), no present > 2 ms late, sim 300/300 ticks at full rate. Two
  fixes got there: the first slot is scheduled no earlier than arrival + the
  expected draw time, and waits no longer use an absolute CLOCK_REALTIME
  `pthread_cond_timedwait` (MinGW: coarse, overshot 5-10 ms and dropped half
  the second presents); while a present is pending the worker sleeps 1 ms
  at a time, then spins the last 2 ms, polling the mailbox.
- Maintainer live test (120 Hz display): smoother at 120, but the F10
  overlay doubled/flickered above 60. Cause: emitting the overlay sets its
  scale (D510 `gfx_set_overlay_scale`) as a side effect; the replay in pass
  1 ran with the reset 1.0, so every other present showed a smaller panel.
  Fixed (the emitted scale is reused). A few px of text still differed
  between passes in a level, so while F10 is open the worker draws one
  pass per game frame (it is a menu; the timeline continues). Regression
  set after the fix: F10 front end + in level 0 px between passes, 2P
  split-screen 2 frames x 1 px, pacing at 120 exact, default-60 goldens
  facility/bunker1 PASS. The F10 frame-cap dropdown also listed only
  30/60 (count hard-coded to 2); fixed.
- Second maintainer pass (120 Hz): flicker on the Facility bathroom stall
  doors, ghost pieces of sinks. Identical models share their geometry key
  and the game reorders them between frames; matching by draw order paired
  a door with its neighbour. Now each matrix takes the nearest unused
  previous matrix of its group (position + rotation scaled by distance).
  Also: the fixed one-tick lag gave alpha ~0 / 0.5, so blended matrices
  trailed everything not blended (sky, effects) by 0.5-1 tick. The lag now
  follows each frame's last present, which shows the frame exactly
  (measured 0.500 / 1.000 at 120; 0.333 / 1.000, 0.683 at 90), with half
  the latency. With VSync the worker now waits for the swap to complete
  (glFinish): drivers queue VSync swaps and return at once, so the present
  clock ran on its own 120.000 Hz timer against the real panel rate (a
  dropped/repeated frame every few seconds). Swap-completion pacing at 120:
  8.334 ms avg, sd 0.23 ms, no gap > 1.5x; cost ~0.5 ms per present. The
  FPS counter now shows presents (it counted overlay emits, so read 60).
  Regression set unchanged (F10 0 px, 2P <= 4 px on 2 frames, goldens PASS).
- Third maintainer pass (120 Hz, Dam tunnel, F12 shots): rectangular holes
  showing the blue fog colour in walls/side openings while turning. GE clips
  each room seen through a portal to the portal's screen rectangle
  (`bg.c bgScissorCurrentPlayerView`), computed for the current camera; the
  in-between pass draws the room at the blended camera and the stale
  rectangle cut it. In passes with alpha < 1, while a perspective projection
  is loaded, a scissor inside the 3D viewport is widened to the viewport
  (the room set stays the game's; depth still occludes). Also: a modelview
  LOAD with no previous match (a room just come into view) now gets the
  camera correction measured on the last matched LOAD of the pass
  (inverse(cur) * blended, the same for every static object), so it moves
  with its neighbours. Regression set green (2P residue 6 frames <= 12 px
  that run; alpha-1 runs cannot reach either change).
- Fourth pass: (a) everything flickered while walking forward. The widened
  scissor let rooms drawn WITHOUT depth test (`bg.c` render modes
  G_RM_AA_OPA_SURF2 / OPA_TERR2 rely on the portal clip and draw order)
  paint over nearer ones in every in-between pass. Widening removed; the
  portal rectangle is now MOVED instead (`gfx_interp_scissor`): its corners
  as view directions are rotated by the drawing room's camera correction
  and re-projected, in N64 units, then window-mapped. (b) Holes remained
  on very fast flicks: the game culls rooms for the current camera only,
  so an in-between view can look at rooms that are not in the DL at all.
  A frame whose camera turns > 6 deg per tick (measured on the first 8
  modelview LOADs) is re-drawn with every present exact.
- Dumps: every game frame is captured once (fixed two ways in which a slow
  present could skip a frame entirely). Clean `GE_QUITFRAME` exits.

**Owed (maintainer).**
- A 120/144 Hz display: fast mouse flicks at high sensitivity, camera cuts
  (Dam bungee, intros), 2P split-screen, Facility explosions.
- The Steam Deck OLED at 90 Hz (Linux build).
- The X220: the cost guard should keep it at one pass.

**Not verified.** PAL (50 Hz sim). Split-screen matching across viewports
(the plausibility check should catch mismatches).

### D578 addendum (2026-10-07): exact-frame fallback + far-clip widening

**Tunnel/room holes on turns.** Cause: in-between passes blend the list the
game culled for camera N with the camera at N+alpha, so rooms newly entering
view during a turn are missing. GE folds the lookat into the *projection*
(room modelviews are scale*translation only), so the committed 6 deg
modelview gate could never see a turn.

**Fix (port-only, `gfx_pc.cpp`).** `gfx_interp_cam_offset_deg()` measures the
view angle between the raw and blended folded projections; divided by
(1-alpha) it is a pacing-independent per-tick turn. Above 1.5 deg/tick the
frame is presented exact (the gate's restart machinery); hysteresis releases
only below 0.5 deg/tick so a turn stays exact end-to-end. Default ON;
`GE_INTERPFALLBACK_OFF=1` disables, `GE_INTERPFALLBACK_DEG` tunes. The
`GE_INTERPCLIP_*` far-clip widening is kept: removal (cap-fb3) brought back
the far-clip corner-cut vanish of ridgelines on blended frames.

**Evidence.** Detector: OFF segment worst 276 px, 17 confirmed-hole frames;
ON segment (non-normalised, jittery alpha) worst 1777 px. Alpha-normalised
build (cap-fb2): maintainer "mostly worked", holes gone. Slim far-clip +
hysteresis (cap-fb4): "not all fixed yet", residual artifact not identified.

**Known limitation.** An unidentified residual artifact remains in cap-fb4
(candidates: distant flicker at the widened-band pop, post-turn settle, or
holes on gentle turns below 1.5 deg/tick). Default-60 goldens unaffected
(PASS). Owed: live 120 Hz Windows, Deck OLED 90 Hz, X220 checks.

**Far-clip tuning (Dam playtest, same day).** Widening 1.5x: distant NPCs flicker
through solid walls; 1.25x: partial NPC flicker, tunnel phasing unchanged; off:
distant geometry flicker returns. Default is now 1.1x
(`GE_INTERPCLIP_FAR`; `GE_INTERPCLIP_OFF=1` disables). Remaining limitation:
upper tunnel / outside rooms phase in and out on blended frames when they sit at
the game's per-tick visibility edge (not in that tick's display list; no blend
can draw them). Portal scissors (`GE_INTERPSCIS_OFF`) not conclusively ruled out.

**Set-change fallback + pacing (same day).** A frame whose set of matched
matrices changed since the last tick (new/vanished rooms or objects) is now
presented exact (`GE_INTERPSET_OFF=1` disables; Dam playtest: "pretty good").
Cost on a Steam Deck OLED at 90 Hz: ~0.5 ms per in-between pass, no
measurable effect on present rate (set check / fallback / clip / scissors
A/B all within noise). Pacing: game-frame arrival jitters by up to 7 ms, so
an early frame dropped a still-pending second slot (~10% of slots; 79 of 90
presents/s on the Deck). The worker now presents a pending slot that is due
within one present period before drawing the new frame
(`GE_INTERP_NOFLUSH=1` restores dropping): Deck 90 Hz median 78-79 -> 82-83
presents/s. A first slot that falls before the frame is drawn is shifted
alone instead of shifting the whole grid. Remaining ~8% shortfall at 90 Hz is
unexplained (present gaps of 13-20 ms on the Wayland compositor path).
`GE_PERFSTAT=1` now also prints the per-second present rate. X220 (60 Hz
panel, `GE_INTERP_FORCE=1` 120 stress): 116-120 presents/s, no crashes in
six levels; Deck six-level sweep: no crashes.

### D578 addendum: swap-driven present pacing (2026-10-07)

Symptom: uneven presents at 90/120 and Steam Deck Game Mode could not hold 90 fps at 15-25% load. Cause: the render worker made a blocking vsync swap (the "never shown" present and the flush present) BEFORE drawing the next game frame, and the game waits for SP/DP done, which is posted only after the draw; under gamescope a swap blocks a full refresh, so game frames were missed. Slot times were also a free-running timer grid while the image appeared at whatever vblank the swap landed on.

Fix (port/src/libultra.c, VSync on only): (1) no swap before SP/DP done. The finished passes already live in the present-slot FBOs (gfx_opengl_interp_store), so gfx_pc retains everything needed and nothing in the game's buffers is read after the draw; leftover slots of the previous frame are simply dropped. (2) Presents run back-to-back, each blocking swap = one refresh; slot times (hence alphas) are predicted vblanks from an EMA of swap-return gaps (s_vbLast/s_vbP), first slot = first vblank after the passes are drawn. A min-gap guard (0.7 of the target period) covers a swap that does not actually block. Passes are still all drawn up front (gfx_interp_tick is not lazy), only the times are vblank-aligned. (3) The flush workaround and GE_INTERP_NOFLUSH were removed. VSync off keeps the old timer grid and pre-draw never-shown present (swap does not block there). (4) Always-on 10 s line "D578 interp: ... presents/s, target, refresh, swap interval, vblank period, % exact" plus a start line (libultra.c; helpers videoInterpVSync/videoInterpInfo in video.c, gfx_interp_last_exact in gfx_pc.cpp).

Smoke (Windows, 120 Hz, FpsCap 120, GE_INTERP_FORCE, level default): 115-119 presents/s. Deck Game Mode unverified. Confidence medium: the remaining risk is a swap held by gamescope for more than one refresh delaying the worker's pickup of the next game frame (at most one refresh, no longer two).

D578 addendum (swap-driven vblank period): s_vbP is now seeded from the real refresh (videoInterpInfo) and its gap filter is relative to the s_vbP estimate (0.75..1.3x), with a median re-seed after 16 consecutive off-period gaps; minGap is 0.7*s_vbP; s_vbLast/s_vbP/s_ipLastN reset on the interp-off transition. Fixes target != refresh (120 on 90 Hz Deck, gamescope caps). Build-verified only; confidence medium until Deck-checked.

D578 addendum (turn fallback softened; Deck 90 Hz: 9-64% exact frames): the exact fallback fired on ordinary stick turns because the projection-offset trigger engaged at 1.5 deg/tick (alpha-normalised) with hysteresis release at 0.5, i.e. any turn above ~1.5 deg/tick stayed exact for its whole duration. Now two-level (gfx_pc.cpp, gfx_interp_load_matrix projection branch + gfx_interp_tick): perTick in (GE_INTERPFALLBACK_DEG=1.5, GE_INTERPFALLBACK_HARD=8 default] re-runs the frame once with its alphas pulled toward 1 by softDeg/perTick (a' = 1-(1-a)*s), so the blended offset from camera N equals the threshold-level offset that showed no portal holes, yet the view still advances smoothly; only perTick > 8 (hysteresis release at 70%) or the existing 6 deg modelview gate or a set change goes fully exact. The 10 s log gains "D578 interp triggers: N turn-exact, N turn-softened, N set-change-exact". Set-change (GE_INTERPSET_MAX=0, any unmatched matrix) is untouched; the counters will show whether it is the remaining source. Risk: the Dam tunnel holes were seen at ~2.5 deg/tick with the old exact fallback OFF; the softened offset is bounded to the 1.5 deg/tick-equivalent level but that is not playtested. Build-verified only; confidence medium-low until Deck-checked (tune GE_INTERPFALLBACK_HARD down if holes return).

D578 addendum (task 2, draw-on-swap with VSync, NOT done, blocker): gfx_interp_tick walks the game's live display list directly (vtx, matrix, texture/palette pointers, segment tables point into game memory; nothing is converted into a persistent per-frame form), and the game starts building the next frame into reused DL/vertex/matrix buffers as soon as SP/DP done is posted. Drawing later passes after posting SP/DP done would therefore read data the game is rewriting. Retaining it would need either a full copy of every referenced buffer per frame or a persistent converted command stream (a new renderer layer), both out of budget. The current design (all passes drawn into present-slot FBOs before SP/DP done, presented afterwards) is the safe form; the residual cost is a frame arriving while a swap blocks the worker waits at most one refresh. Confidence high on the blocker.

### D578 addendum (Deck Cradle evidence: 69-100% exact frames)
Build-verified only, not run (confidence medium). (a) The modelview 6 deg/tick gate in gfx_interp_matrix was the real turn-exact trigger (cliff); now graded like the projection check (soft from 1.5, exact above max(fast-turn gate, GE_INTERPFALLBACK_HARD=8)). (b) Set-change exact now needs changed > max(GE_INTERPSET_MAX, GE_INTERPSET_MIN=4) and > GE_INTERPSET_PCT=20% of the previous matrix set; unmatched matrices already draw exact with the camera correction. (c) s_ipPassEma divides by passes actually drawn (gfx_interp_body_runs), clamps a sample to 1.5x EMA, and the pass budget is 0.8*tick (was 0.6). Dam tunnel scissor/clip protection untouched.

### D578 addendum (Auto frame-rate cap, gamescope VSync default)
Build-verified only, not run (confidence medium). `Video.FpsCap = -1` is the new Auto value and the fresh-install default (was 60; registered range now -1..1000; 0 stays "uncapped", so existing inis keep their meaning). Auto resolves each frame in `videoApplyFpsCap` via `videoAutoFpsCap()`: the measured display refresh (`gfx_sdl_get_refresh_rate`), clamped to 144, and the VI rate (60 NTSC / 50 PAL) when the refresh is unknown or <= VI+5 -- so a 60 Hz panel takes the same non-interpolated path as the old default 60. F10 row: "Auto (N FPS)" first in `kFpsCapSeq`; reset default -1; the N64 preset also maps Auto to 60. Gamescope: `videoInit` defaults `Video.VSync` to 0 when `GAMESCOPE_WAYLAND_DISPLAY` is set or `SteamDeck=1` AND the ini had no `Video.VSync` key (new `configIntKeyWasLoaded`, since configLoad had already written VSync=1); logged once at LOG_NOTE and the ini re-saved so the choice persists. An ini that has the key is never touched. Risks: old builds clamp -1 to 0 (uncapped) if an ini is shared downgrade-wise; Auto on a 75 Hz desktop panel now interpolates to 75 by default; an ini that predates `Video.VSync` counts as fresh under gamescope.

### D578 addendum: room-change-exact + room-scissor recompute scoping (2026-10-08)

**Task 1 (implemented, build-verified only; Deck/Dam runtime check owed).** bg.c emits every room as `gSPSegment(SPSEGMENT_BG_VTX = 14, room vertices)` + `gSPDisplayList` (`bgRenderRoomPrimary/Secondary`, bg.c ~2932/2967), each inside a per-room `gDPSetScissor` from `bgScissorCurrentPlayerViewF(room bbox)`. The segment-14 base is unique per room, so `gfx_sp_moveword` (G_MW_SEGMENT) records it per pass in `s_interp_rooms_pass` (gfx_pc.cpp); after each pass the list is sorted/uniqued and compared with the previous game frame's list. A difference on a blended pass sets `s_interp_fast` with `s_interp_why = 3`, so the whole frame is re-run exact; counted as `room-change-exact` in the 10 s "D578 interp triggers" log line (libultra.c) plus a capped `D578 ROOMCHANGE` stderr line. `GE_INTERPROOM_OFF=1` disables. The object set-change 20% gate is untouched. Exact (alpha 1) frames are not affected (the check runs only when alpha < 1). Confidence: medium-high that the detection is right; unknown how often it fires on Dam (watch the counter; if it fires on most turning frames the exact fraction rises and a hysteresis/min-size rule may be wanted).

**Task 2 (not implemented; scoping).** A room scissor is NOT a single portal's projection. `sub_GAME_7F0B7F84` (portal descent, bg.c ~4100, depth <= 16) recurses room->portal->room carrying `parentbox`; each portal's screen box comes from `sub_GAME_7F0B5864` (projects the portal polygon points via `sub_GAME_7F0B5528` with the CURRENT camera, cached per frame in `table_for_portals`) and is intersected with the parent box. `sub_GAME_7F0B39BC` stores the resulting box per drawn room in `dword_CODE_bss_8007FFA0[i].bbox` (+roomid, unk1 = draw order); `bgScissorCurrentPlayerViewF` clamps it to the player view. A port-side recompute needs: (1) the portal graph (room -> portals -> other room, polygon vertices in world space; `table_for_portals` / portal data in `ptr_bg_data`), (2) the descent path that reached each drawn room (not stored; only the final intersected bbox is kept), (3) the same near-plane clipping as `sub_GAME_7F0B5528`. Mapping scissor -> room is available (the segment-14 id seen before the scissor-using DL, or roomid via the bbox list), but path-dependent intersection means a recompute must replay the descent per pass with the interpolated view-projection. Feasible only with a read-only accessor that exports the per-room path (list of portal ids) captured in `sub_GAME_7F0B39BC`; the existing matrix-delta correction in `gfx_interp_scissor` remains the fallback.

### D578 addendum: port-side portal-scissor replay (2026-10-08)

**Implemented (build-verified; replay validated at alpha = 1 on Dam; interpolated look not seen by eye).** Task 2 of the previous addendum, done without touching `src/game`. `port/src/interpportal.c` (+ `port/include/interpportal.h`) transcribes bg.c's visibility traversal (`bgDetermineVisibleRoomsImpl` final pass: the global vis command list `parse_global_vis_command_list`, the portal queue/descent `sub_GAME_7F0B7F84` with the per-room visit cap and depth caps, `sub_GAME_7F0B5864` portal projection cache + D106/D271 guard, `sub_GAME_7F0B5528` near-plane clipping, `bgIsRoomOnScreen`, `sub_GAME_7F0B39BC` room list/union) against a private copy of all mutable state; portal polygons and the vis list are read in place, portal metrics via the pure `sub_GAME_7F0B96CC`. Scissor output goes through the same `bgScissorCurrentPlayerViewF` clamp/ceil.

- **Threading (the surprise).** The render worker (D481) draws frame N while the game thread is already building N+1, so live game state (`dword_CODE_bss_8007FFA0`, `g_BgNumberOfRoomsDrawn`, the player's matrices) is not frame N's. The first check run showed it directly (`extra`/`game=` counts changing mid-compare). So `interpPortalOnSend` (called from the `osSendMesg` shim, i.e. on the game thread inside `rspGfxTaskStart`, recognised as `mq == sched_cmdQ && msg == g_gfxTaskSettingsList`) snapshots everything the replay needs (camera `field_10CC`/`projmatrixf`, player scale/view params, screensize, zrange, level scalars, per-room bounds, portal control bytes/connections, the vis stack, and the game's own per-room boxes) into a 4-slot ring keyed by the display-list pointer; `gfx_frame_body` selects it with `interpPortalPassBegin(commands)`. This also means any other render-side read of live bg state (e.g. `viGetZRange` in the clip widening) is one frame ahead; harmless there, noted for later.
- **Camera for a blended pass.** GE loads the room projection `field_10E0 = lookat(room-scaled pos) * persp` (row vectors; `matrix_4x4_multiply(a,b,r)` is `r = b * a`). With `persp = projmatrixf`, `L = P * persp^-1` for the raw and the blended (pre-clip-widening) projection gives both rotations and camera positions in the room-scaled space; `dcam = (spos_blend - spos_raw) / D_800364CC` is applied to the world camera recovered from `field_10CC`, the blended rotation is renormalised (undo the lerp shrink), and Bond's position moves by `dcam`. Exact passes use `field_10CC` itself. A projection pair is accepted as the room projection only if the derived raw rotation matches `field_10CC` (max 0.05): the pure-persp, HUD and menu loads fail that and are ignored (the replay stays cached per pass, so a persp load between room draws cannot flip a room back to the old correction).
- **Mapping and fallback.** `gfx_interp_scissor` (blend passes only) asks `interpPortalScissor(seg14, game scissor)`. seg14 (`gfx_sp_moveword`) identifies the room by `OS_K0_TO_PHYSICAL(g_BgRoomInfo[r].vertices)`; the replacement is used only if the game's own scissor for that room (from the snapshot) equals the scissor being applied (1 px tolerance), and the room was reached by the replay. Otherwise (full-view HUD scissors, props drawn between rooms, split-screen players, unreached rooms) the old matrix correction runs unchanged. Replacement is the faithful replayed box (no union with the game's box; `GE_INTERPPORTAL_UNION=1` adds the union).
- **Switches.** Default ON. `GE_INTERPPORTAL_OFF=1` (also keeps the snapshot off) restores the previous behaviour; `GE_INTERPPORTAL_CHECK=1` logs `D578 PORTALCHECK` lines: on every exact (alpha = 1) pass the replay with the unmodified camera is diffed against the game's per-room boxes (works without interpolation; log shows `miss/extra/bad`, camera `rotdiff`/`scalediff`), and on blended passes a sanity line (`maxshift` px, `dcam`). Exact frames are byte-identical to before (no scissor touched).
- **Validation (Windows, Dam `-level_33`, 120 Hz interpolation on this PC, `GE_QUITFRAME`).** 1200 alpha-1 passes checked: 0 frames with any missing/extra/different room box (maxdiff 0.0000 px), camera derivation `rotdiff` 1e-5, `scalediff` 0.003 (scaled units), i.e. the transcription is bit-exact for the covered views (Dam spawn/intro, 5-8 rooms). Covered camera motion was small, so deeper chains (the tunnel) are not exercised; run the check while walking the tunnel on the Deck/PC and expect `bad=0` throughout. Counters in the 4000-replacement stats line: `fullview` (non-room scissors left alone), `gatefail` (sub-rect scissors not matching their room), `noroom`, `unreached`.
- **Confidence / risks.** Medium-high that the replay equals the game's traversal (0 mismatches, same code path); medium that the blended camera derivation is right for large turns (rotation lerp, linear in the projection; the existing turn/set-change fallbacks still gate those). Untested by eye: whether the replayed boxes remove the tunnel pop at 90 fps (maintainer check on the Deck). Known gaps: split-screen (replay skipped via the rotation check/gate), `VISOP_PRELOAD_*` skipped (no effect on boxes), the vis command `Unk80081600` function-static is replicated, not read. If the faithful box shows seams, try `GE_INTERPPORTAL_UNION=1` before reverting to the old correction with `GE_INTERPPORTAL_OFF=1`.

### D578 addendum (2026-10-08, uncommitted): undrawn-room fallback for the Dam tunnel holes

**Claim (confidence MEDIUM, unverified on the Deck):** tunnel holes come from rooms that the in-between camera can see but the game's display list never drew. The list is built for the current frame's camera; the existing room-set-change gate only compares the previous and current frame's lists, so a room absent from both but reachable mid-way is missed.

**Evidence for it:** (a) `allexact` removed almost all flicker on the Deck, so it is in the in-between passes; (b) the 10-07 diagnosis note shows holes absent in both exact neighbours; (c) rect size is not the cause: widening the replay rect to the union with the game's rect changed nothing on the Deck (reverted); (d) the replay's `extra` rooms exist as a signal (`rp_check` already counted them).

**Change:** `interpportal.c` `rp_replay_now` counts rooms the replay reaches at the interpolated camera with no game-drawn room and a rect of at least 3x3 px; `gfx_interp_tick` forces exact presents for that game frame (same class as a room change, trigger counter shared with `room-change-exact`). Switches: `GE_INTERPREACH_OFF=1` or `noreach` in `d578_live`. Log: `D578 REACH` lines and an "undrawn-room replays" counter on the 10 s portal line.

**Verified:** builds on Windows; 1500-frame headless Dam run at 120 is crash-free and the fallback fired on 5 of 2557 replays (the route did not reach the tunnels). **Not verified:** that it removes the tunnel flicker. Maintainer test: Dam tunnels at 90/120, then flip `noreach` in `d578_live` and compare.

**Deck result (2026-10-08, same day): NOT the main cause.** With the fallback on (e2b988f0) the flicker remained; the fallback fired on only 11-13 of ~11k replays. Live A/B: `noportal` = no change; `noportal nosciss noclip noreach` = slightly MORE frequent, so the extras help a little but the base cause is elsewhere. The maintainer notes it mostly appears when SPRINTING toward the tunnel (large per-tick translation, so the in-between camera is far from the list-owning camera). A 900-present quarter-size burst taken inside the tunnel found no wall-hole outliers (temporal-outlier detector, tracers masked); the only one-present anomalies were bullet tracers. Detector or capture may have missed a thin or distant artifact: describe the exact visual first. Tooling kept (dev only): `burst` word in `d578_live` + `touch d578_burst` saves 900 quarter-size presents to `ppm/` (Deck has no keyboard, so ssh-triggered; delete them after pulling).

**If it does not fix it:** the next causes in order are (1) rooms drawn without depth test (OPA_SURF2/TERR2) reordering under the interpolated camera, (2) a replay that stops early (`unreached` counter), (3) hole-fill in the present path (last resort: needs a sky mask and both neighbour frames, fragile).

### D578 addendum (2026-10-08, uncommitted): Dam in-between flicker -- diagnostics, `dclamp` and `projset` A/B switches

**Status: built, NOT runtime-verified (confidence LOW that either switch fixes the flicker; HIGH on the code-reading facts below).** Defaults are unchanged: with `d578_live` empty the output is identical to the previous build, except for the added `D578 PROJDIAG` stderr line.

**Code facts that differ from the review's premises:**
1. `GL_DEPTH_CLAMP` is already enabled at GL init (`gfx_opengl.cpp`, whenever `GLAD_GL_ARB_depth_clamp`, which is forced true on GL >= 3.2). So `dclamp` alone cannot add a clamp; the A/B therefore *disables* the clamp for the exact pass and relies on it for in-between passes.
2. The software RSP rejects whole triangles on the CPU when all three vertices have `z > w` (`CLIP_FAR`, `gfx_sp_vertex` and the float-vertex path). GL depth clamp cannot rescue those, and the lerped far plane sits closer than both source planes, so triangles between the lerped and the real plane were dropped before GL ever saw them. `dclamp` therefore also drops the CLIP_FAR bit on in-between passes.
3. The room projection is `lookat * persp` (row vectors; `bondview2.c` `matrix_4x4_multiply(projmtx, &spC4, &sp60)` means `sp60 = spC4 * proj`, D32). The lookat is built from `scaledpos = (cam - current_model_pos) * D_800364CC`, so its translation T is nonzero. Hence `M[i][2] = A*L[i][2]` (i < 3) and `M[3][2] = A*T.z + B`, with `A = (n+f)/(n-f)`, `B = 2nf/(n-f)`. `P[3][0..2]` is the lookat translation row times the perspective diagonal, not `k*L[2]`. The knorm test in `gfx_interp_conservative_clip` passes only because `M[3][2]` is dominated by B while the camera is near the model origin; when T grows it can flap, switching the widening on and off between ticks. `l22 = M[3][2]/k` is about 1, not `L[2][2]`, so the live code adds `dA*l22` to `M[2][2]` (should be `dA*L[2][2]`) and never scales `M[0][2]`/`M[1][2]`: a heading-dependent tilt. The review's corrected formula (`M[i][2] *= A2/A` for i < 3; `M[3][2] = (M[3][2]-B)*A2/A + B2`) matches this derivation. It is in the tree as `gfx_interp_conservative_clip_fixed`, UNUSED.
4. Matches the review: all projection LOADs share match key 0, so several per frame are paired by distance only. `PROJDIAG` counts how often the matched index differs from load order.

**Switches (`d578_live`, sampled once per game frame):** `dclamp` -- in-between passes: GL clamp on, CLIP_FAR reject off, `gfx_interp_conservative_clip` skipped; exact pass: clamp off; clamp restored to the default after the frame; silently ignored (logged once) without GL support. `projset` -- each tick records, on pass 0, one signature byte per projection LOAD (1 = `|P[3][0..2]|` within 5% of `|k|` from the live n,f); a count or signature change vs the previous tick forces exact presents (`why = 4`, counted in `PROJDIAG projset-exact`).

**Log:** every ~10 s while interpolating, `D578 PROJDIAG passes=.. loads/pass min/avg/max | clip applied/disabled/badnf/knorm-skip/dclamp | knorm/|k| range, l22 range | pairs N order-mismatch M | projset-exact=.. [dclamp=0|1 projset=0|1]`, then one `last pass proj[i]: knorm |k| l22 code paired-with` line per projection of the most recent in-between pass (`code` 0 applied, 1 disabled, 2 bad n/f, 3 knorm mismatch, 4 dclamp; `paired-with` -1 = no match, -2 = not paired). The classification mirrors the live early-outs; a pair rejected by `gfx_interp_plausible` still shows its pre-clip code.

**Maintainer test (Deck, Dam, 90 Hz):** sprint toward the tunnel with `d578_live` empty, then `dclamp`, then `dclamp projset`; report flicker yes/no for each and send the PROJDIAG lines.

**Deck results (2026-10-08, Dam tunnel sprint, 90 Hz, Game Mode) -- NONE of the five variants removed the flicker:** empty, `dclamp`, `dclamp projset`, `exactproj` (in-between passes keep the exact room projection, modelview blend kept), `clipfix` (both source projections widened before the lerp). Ruled out as the prime cause: far-plane clip range or asymmetry, the CPU far reject, the projection-LOAD set changing between ticks (`projset-exact=0` in the baseline), the projection lerp itself (second order, and `exactproj` removes it), and matrix pairing by shared key 0 (order-mismatch 0.2-1% of pairs).

**New facts from `PROJDIAG`:** (1) The live `knorm ~ |k|` gate skips every folded room projection (knorm 26-290 vs |k| = 10.00): ~1,100-1,900 skips per 10 s, `applied` counts only pure-perspective loads. The widening therefore never touched room geometry at Dam. (2) The structural gate `M[i][2] + A*M[i][3] = 0` (rows 0-2), `M[3][2] + A*M[3][3] = B` holds for the room loads to ~1e-5 (r0 max 1.5e-5, r1 -1.5e-5..2.8e-5; 100% pass in clean windows) and failed 7 loads in the load-screen window (r1 to -50: the menu/watch projections it is meant to reject). `gfx_interp_clipfix_widen` uses it and is the correct widening if the clip is ever wanted; it is behind the `clipfix` live word, off by default. (3) 29-227 projection LOADs per pass.

**Still open:** `allexact` removes the flicker, so it lives in the blended (alpha < 1) passes, in something other than the projection. Remaining suspects: per-object modelview pairing/ghosting, rooms drawn without depth test reordering under the interpolated camera (OPA_SURF2/TERR2), the deep tunnel portal chain, and a hole-fill in the present path. Next step is a pixel-level look at what actually changes between presents (burst capture), not another switch.

### D578 addendum (2026-10-08, later): Dam flicker narrowed to per-pixel content of the blended passes; stage-elimination + hole-capture tooling

**Status: NOT FIXED. Everything below is measured on the Deck (Game Mode, Dam, 90 Hz) unless stated. Symptom (maintainer, repeated): geometry (walls/terrain) drops out and the skybox shows through; worst at the END of the first tunnel / the gap before the second, while moving FORWARD (turns are already covered by the exact-frame fallback). `allexact` removes it, so it lives in the alpha < 1 passes. The truck passing through the tunnel eases it briefly (consistent with the set-change exact fallback firing on its matrices, not a truck-specific mechanism).**

**Measured (`tristat` word, per-room triangle fate, in-between vs the exact pass of the same game frame, ~1,950 passes):** `seen` is identical in every room (the DL is the same), `eye`-plane crossings identical, net emitted-triangle difference is ~0 on average (+79, +147, -66, -175, +38 ... per 10 s) and only 1 of ~1,950 passes emitted < 97% of the exact pass. The four loss events logged first (frames 1769/2161: backface-cull +82/+93 in room 5d1f0; 5371/5375/6038: top/left trivial reject) were selected by logging only losses; they do not show a systematic loss. **The CPU never drops materially more triangles in blended passes. The dropout is not a count of missing triangles.**

**Eliminated by Deck A/B (each flickered the same):** empty switch; `dclamp` (GL clamp on in-between, CPU CLIP_FAR reject off); `dclamp projset`; `exactproj` (exact room projection, modelview blend kept); `clipfix` (both source projections widened before the lerp, structural gate); `nodepth` (no depth test on in-between passes); `nocull` (no backface cull); `fullsciss` (room scissors = whole viewport). Earlier, from the 10-08 morning: `noportal`, `nosciss`, `noclip`, `noreach` (no change). So far-plane clipping, depth testing, backface culling, scissors, portal replay, projection lerp, projection count, matrix pairing (order-mismatch 0.2-1%) and the visible-set gates are all NOT the cause.

**What is left:** triangles that exist, are emitted, are not depth/cull/scissor-rejected, yet the pixels are sky/background: (a) vertices transformed to the wrong place (modelview or projection for a subset of loads: not tested directly -- `exactproj` fixed only the projection side, and 29-227 projection loads per pass are paired by distance so a wrong pairing for a few would not show in the order-mismatch count if the matrices are near-identical); (b) fog turning distant geometry into sky colour in the blended pass; (c) colour-combiner / blend / texture state differing between passes (rendering_state is NOT restored between passes and mirrors live GL bindings; the "known residue" note in gfx_interp_tick); (d) draw order (sky drawn after rooms in a blended pass); (e) GL state the interp store/restore leaves behind. Do not guess among these: capture the failing frame (below).

**Tooling added (all behind `d578_live` words, off by default):** `PROJDIAG` (10 s line: projection-load counts, structural-gate pass/fail and residuals, widening skips), `tristat`, `exactproj`, `clipfix`, `dclamp`, `projset`, `nodepth`, `nocull`, `fullsciss`, `holecap`. All diagnostic lines also go to `ge007.log` (Game Mode swallows stderr). **`holecap` is the next instrument and has not yet been run in the tunnel:** every game frame it compares the first in-between slot with the exact slot at 1/4 resolution, and with the PREVIOUS exact frame; a pixel that differs from BOTH exact neighbours cannot be explained by motion (an interpolated image only contains what the two neighbouring exact frames contain), so 16x16 blocks that are >= 14/16 unexplained are a hole. On a hit it saves `holecap_<frame>_{ib,ex,prev}.ppm` (ib/ex full resolution, prev at 1/4) in the game's working directory, up to 8, and logs `D578 HOLECAP`. A first version (blocks that merely differ between ib and ex) false-triggered on turning frames at level start and burned its cap; the both-neighbours rule fixes that. `<repo>/scratch` is not needed: pull the PPMs, convert with a PNG writer, view `ib | ex | diff` side by side; delete the Deck files afterwards.

**Structural clip gate (kept, correct, unused by default):** the room projection is `lookat * persp` (row vectors), so `M[i][2] + A*M[i][3] = 0` (rows 0-2) and `M[3][2] + A*M[3][3] = B`, with `A = (n+f)/(n-f)`, `B = 2nf/(n-f)`. Verified on the Deck: 100% pass on room loads, residuals ~1e-5; rejects menu/watch projections. The old `|P[3][0..2]| ~ |k|` gate skipped every folded room projection (knorm 26-290 vs |k| = 10) so the far-clip widening never touched room geometry at Dam.

### Addendum 2026-10-09: Dam tunnel flicker fixed (two causes), shadow-flicker trade-off

**Cause 1 (portal replay):** the port-side portal-chain replay (`interpPortalScissor`) sometimes returned a near-full-view rect for a far room (seen: room 131, 128..1164 x 33..600 slot px, against a small portal opening). That room draws without depth, so it painted over the nearer tunnel (holecap frame 4745, Deck). **Fix:** the replayed rect is clamped to the union of the room's game rects in the previous and current exact frames, padded by 8 px (`port/src/interpportal.c`).

**Cause 2 (modelview pairing), the main flicker:** in-between passes paired each modelview with a previous-frame matrix by nearest transform, then blended. Room and prop matrices were matched to the wrong instance and drawn between two places, so geometry phased in and out while the camera moved. Live Deck A/B (the `d578_live` words): `mvexact` was clean and survived a level restart; `nocorr` alone still flickered. Excluding only unkeyed or ambiguous pairs was not enough. **Fix:** in-between passes draw modelviews exactly as the game loaded them, and only the projection (which carries the camera) is blended. The `mvblend` word restores the old blend for development.

**Found by:** an inventory of every in-between-versus-exact difference (dev-process §8 "Regression triage first", applied to a new feature), then one live switch per uncovered difference, flipped over ssh in a single session. **Verified:** maintainer, Dam on Deck at 90 Hz and on PC at 120 Hz. Accuracy at 240 fps (PC, forced): the 21-level golden sweep matches the 60 fps goldens on 20/21; Streets is the D579 near-clip re-base, and the 4 sweep misses passed when run singly. Engine headroom is about 0.5 ms per in-between pass, so 240 fps on this PC is limited by the display.

**Known trade-offs (open):** (1) moving models update at the game-tick cadence in in-between passes; (2) decal shadows flicker at high FPS (Dam, Cradle catwalks; `mvblend` stops it), likely because the shadow's exact modelview disagrees with the blended surface under it. (3) The holecap trigger now needs 30 or more blocks, so one-frame tracers and sparks no longer use up the capture cap. (4) Harness: `verify.sh` pins `Video.DeckPresetApplied = 1`; Deck captures with interpolation on still come out at 1280x800 against 640x480 goldens (open tooling item).

### Addendum 2026-10-09 (late): exact-frame regression + VSync 60 lock fixed; black-frame flicker open

**Regression from the morning fix (17ec86c4), now fixed:** the early return that draws modelviews exact skipped the pairing, so no previous-frame modelview was marked taken. The set-change detector counted them all as changed and made ~96-100% of frames exact, so 120 fps showed duplicated exact frames and looked like 60. The pairing now runs for bookkeeping and the modelview is still drawn exact. Log after the fix: 0-6% exact at ~120 presents/s. The earlier "Dam clean" verdict was taken while most frames were exact, so the tunnel and gap need a re-check with real in-between frames.

**VSync held at 60, fixed (`port/src/libultra.c`):** (1) swap-driven pacing keyed off the VSync setting, not the live swap interval, so a driver that forces VSync off produced non-blocking swaps that were measured as the display period; it now reads the live interval (~1/s). (2) The display-period estimator could re-seed to ~16.7 ms after a run of slow swaps, and the 0.7-period gate then held presents at 60 on a 120 Hz panel; the estimate is now capped at the real refresh period +10%. Verified: 120.0 presents/s, swap interval 1, period 8.33 ms, 2-6% exact.

**Black frames at 120 with VSync on:** moved to D583 (present scheduler under G-Sync/VRR; the back-buffer hypothesis was refuted there).

### Addendum 2026-10-10: in-between depth-clip widening now default OFF

**Symptom (maintainer, Deck 90, per-level pass):** above 60 fps only, at one long range: a Frigate computer terminal's shadow visible through a wall from outside; Statue's Valentin visible through the shipping crate he spawns in until approached. Clean at 60.

**Cause:** `gfx_interp_conservative_clip` (the far-clip widening on in-between projection loads, FAR 1.1). Its own comments already recorded that 1.25-1.5 "made distant NPCs flicker through walls" and that the live formula adds a heading-dependent tilt (`gfx_interp_conservative_clip_fixed` is the unused corrected form). Live `noclip` on the running Deck game removed the Frigate glitch immediately.

**Fix:** `interp_clip_off()` gates all three widening paths (`gfx_interp_conservative_clip`, `gfx_interp_clipfix_widen`, the PROJDIAG gate) and is ON by default; `GE_INTERPCLIP_ON=1` or `clipon` in d578_live opts back in (`GE_INTERPCLIP_OFF` / `noclip` still force it off). Commit ce809cd4. **Owed:** the Dam distant-ridgeline check without it (the reason it was added, cap-fb3 2026-10-07); if the ridgelines vanish again, try the `_fixed` form instead of the old one.

**Release strip (2026-10-10, v0.5.1):** the `d578_live` live-word file and every word it read, `holecap`, `burst` capture, `tristat`, `GE_INTERP_SELFCHECK`, `GE_INTERPPORTAL_CHECK`, PROJDIAG, the `GE_INTERPSCIS_*` / `GE_INTERPSCISSOR` / `GE_INTERPGATE_ABFILE` knobs and the matrix-counting set-change check (`GE_INTERPSET_*`, which only ran with `mvblend`) were removed; each site collapsed to its default branch (clip widening off, replay box unioned with the game box, in-place restart on, glFinish skipped under gamescope). Kept: the 10 s stats lines, `D583 SLOW`, IPLOG/`GE_INTERPLOG`, `GE_INTERPCLIP_ON`, `GE_INTERPFALLBACK_*`, `GE_INTERP_FAST_TURN`, `GE_INTERPROOM/REACH_OFF`, `GE_INTERPPORTAL_OFF/_UNION`. The removed switches are in `feat/d583-deck90` history for a future debug build.

### D578 addendum (2026-10-13): Auto follows the display up to 240; 165 and 240 in the menu

Maintainer ask (v0.6.0 release day): allow 240 and let Auto follow the user's display. The old Auto clamp
of 144 had no recorded reason; it was the top menu entry when Auto was added. The real ceiling is the
present ring: `IP_SLOTS` 8 with `maxN = IP_SLOTS / 2` (libultra.c) gives at most 4 presents per sim tick,
i.e. 240 NTSC / 200 PAL. `videoAutoFpsCap()` now returns the refresh clamped to `4 * vi` (was 144), and
`kFpsCapSeq` gains 165 and 240. A display above 240 Hz holds at 240 under Auto (raising it needs a bigger
ring and the matching FBO slots, `GFX_INTERP_SLOTS`). A separate "uncapped" entry was not added: presents
are paced to the refresh (VSync or the compositor), so uncapped interpolation = Auto, and `FpsCap 0`
stays the old non-interpolated path (D186). Slow GPUs are already covered by the per-frame `s_ipPassEma`
pass reduction. Verified on the dev box (120 Hz panel), Dam, 30 s, `GE_INTERP_FORCE=1 FpsCap=240`: with VSync
on, swap-driven at 120 (2 slots/tick, as before); with VSync off, timer-paced 240.0 presents/s, 4 slots/tick
on 600/600 frames, 0 dropped after warm-up, draw peak < 1 ms, clean GE_QUITFRAME exit. Not yet seen on a
real 165/240 Hz panel -- confidence medium-high.
