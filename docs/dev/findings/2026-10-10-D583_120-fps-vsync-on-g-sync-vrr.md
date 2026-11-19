# D583

<!-- from the old section-F/H table row -->
| D583 | **120 fps + VSync on G-Sync/VRR: fullscreen black frames + OLED VRR flicker from the interpolation present scheduler.** Black frames fixed (VSync presents wait for their slot time; slot grid uses the reported refresh, not the measured swap gap). The OLED flicker was the present cadence (8% of ticks got 1 slot, early/late gap pairs, dropped slots); fixed by one continuous present grid snapped to tick/N, an 8-slot present ring (no drops), a capped draw-time peak for the ready time, and showing a waiting present before the next frame's draw. 120.0 presents/s, 99%+ of gaps in 7-9.5 ms. | FIXED on PC 2026-10-09 (G-Sync OLED, maintainer by eye); Deck 90 good on most levels (per-level pass 2026-10-10); open: Bunker fight draw cost, Jungle feel (see 2026-10-10 addendum) |

## D583: 120 fps + VSync on G-Sync/VRR — black frames (fullscreen) and OLED VRR flicker from the interpolation present scheduler — FIXED on PC 2026-10-09; Deck 90 fixed 2026-10-09 (PC 120 re-check owed)

**Report (maintainer, 2026-10-09):** at FpsCap 120 + VSync, whole-screen black
frames for a refresh, occasionally (1-2 per Dam walk, also in the attract/cutscenes);
absent at 60. Separately, a whole-screen brightness flicker on an LG OLED with
G-Sync (enabled for windowed + fullscreen) that other games and the PD port do not
show. NVIDIA "Vulkan/OpenGL present method" = Prefer layered on DXGI swapchain
(driver default).

**Bisection (one variable per run, maintainer by-eye + log):**
- `blackcap` probe (3x3 near-black readback of each stored slot and of the back
  buffer after show, before swap): 0 black stores / 0 black presents during the
  walk (only boot/fade frames). The black happens **after** our back buffer, so the
  third-party review's suspects (empty-pass replay, undefined back buffer,
  cfgVSync vs live interval) are all refuted for this symptom.
- Maximized window: clean. Exclusive and borderless fullscreen: flicker.
- NVIDIA present method = Prefer native: borderless clean (exclusive then picks a
  wrong resolution — separate, see ROADMAP). So the layered-DXGI fullscreen path
  is the place the black is produced.
- Period floor (clamp measured vblank period >= 0.95 x refresh) alone: still black,
  presents dropped to 81-101/s. Refuted, reverted.
- Post-swap `glFinish` removed (`nosync`): black gone but presents 91-100/s
  (the swap-return clock lost its anchor). Not the fix; kept as a dev word only.

**Root cause (high confidence for the mechanism, by construction + logs):** the
VSync ("swap-driven") present path assumed a **fixed refresh**:
1. Slots were presented **back-to-back**, relying on each blocking swap to space
   them one refresh apart (libultra.c render-worker loop). Under VRR a swap does
   not wait for a beat: slots went out bunched and ~2.5 ms early.
2. The slot grid's period was the **measured swap-return gap**. Under VRR that is
   our own cadence fed back: it drifted to 6.6-7.7 ms at 120 Hz, slots outran the
   panel and swaps stalled 15-40 ms. Those stalls are what the layered-DXGI
   fullscreen path turned into black frames.

**Landed (uncommitted, feat/interp-fps):**
- VSync presents wait for each slot's scheduled time minus a 1 ms lead (`nodue`
  d578_live restores back-to-back). On a fixed refresh the swap still lands on the
  same vblank.
- Slot times + the 0.7-period double-present gate use the display's reported
  refresh (1e6/Hz), not the measured gap (the measured value remains for the stats
  line and as the unknown-refresh fallback).
- Result (maintainer, 120 + VSync, layered, G-Sync): **no black frames**; slow
  swaps 8 -> 1 per run. Whole-screen shimmer reduced but **OLED VRR flicker still
  bad**.

**Remaining cause — cadence (measured, `D578 cadence` stats line, 3 x 10 s, Dam):**
game tick a steady 16 667 us; **8% of ticks get 1 slot instead of 2** (34-50 per
10 s) -> 66-83 gaps of 12-20 ms per 10 s; **~20% of gaps off-grid in early/late
pairs** (<7 ms: 53-136, 9.5-12 ms: 43-119); 5-18 slots dropped unshown; 111-115
presents/s instead of 120. A VRR OLED shifts brightness with frame-time changes,
so this cadence *is* the flicker. Causes in the scheduler
(`portRenderGfxTaskInterp`):
- A tick's slots must lie in `[first vblank after ready, A + tick)`: the grid
  phase vs the tick's arrival A + its render time decides whether one or two grid
  points fit.
- The grid is re-anchored every tick to `s_vbLast` (last swap return), which under
  VRR is our own present time + jitter: the grid wobbles tick to tick.
- A new tick drops the previous tick's unshown slots.
PD and other games present each rendered frame once at a steady cadence; nothing
can fit a variable number of presents per tick.

**Proposed rework (next session):**
1. **One continuous present grid** at the display period (1e6/refresh), anchored
   once; re-anchored only after a real stall (a swap > ~2 periods late) or a
   refresh/interval change. Never rebuilt per tick.
2. **Fixed slots per tick** = round(refresh / 60) (2 at 120, 3 at 180, 4 at 240;
   fractional refresh such as 90/144 keeps the current variable count but drawn
   from the continuous grid): the next N grid points after the passes are ready,
   even if the last one falls past A + tick. The existing lag/alpha logic (`want =
   last slot time - S`) already follows when slots are shown.
3. **No drop of pending slots on a new tick**: the present slots become a ring;
   the next tick writes the slots not pending. `GFX_INTERP_SLOTS` / `IP_SLOTS` = 4
   holds only two ticks at 120 Hz — raise to 8 (two ticks at 240) or cap N at
   slots/2. Slot indices are passed through `gfx_interp_tick` ->
   `gfx_opengl_interp_store` and `videoInterpPresent`; `gfx_opengl_interp_*`
   resize/holecap/selfcheck use slot 0..n-1 and need the base offset.
4. Keep the post-swap `glFinish` (swap-return timing) and the due-time lead.
5. Verify with the `D578 cadence` line: target 120.0 presents/s, >= 98% of gaps in
   7-9.5 ms, 0 dropped, slots/tick all 2; then the maintainer's OLED by eye; then
   fixed-refresh (VSync without G-Sync) + Deck 90 Hz regression; no golden
   impact expected (timing only).

**Rework landed (2026-10-09, `port/src/libultra.c` render worker; slot ring in
`gfx_pc.cpp` / `gfx_opengl.cpp`):**
1. **Continuous grid** (`s_gNext`): each game frame takes the next round(dS /
   period) grid points, fraction carried (90 Hz: 2, 1, 2, 1). Period = reported
   refresh with VSync, target rate without; **snapped to exactly tick/k when
   within 1%**, so a 60 Hz game on 120 Hz is exactly 2 presents per tick with no
   beat between the clocks (and an integer-reported 119 for 119.88 Hz lands on
   the right grid). Re-anchored only when a frame is ready after its first grid
   point (stall) or the period changes; a once-a-second trim (<= 1/4 period)
   takes back spare latency, and a 10-frame trim drops > 1 period of excess at
   once (after a load), discarding the waiting presents it overtakes.
2. **Slot ring**: `GFX_INTERP_SLOTS` 4 -> 8, `gfx_interp_tick(..., base)` stores
   pass i in slot (base + i) mod 8; slot FBO storage is allocated per slot on
   first use (120 Hz still uses 4). A new frame never drops waiting presents; a
   present whose successor is already due is skipped instead.
3. **Ready time from the real draw time**: a decaying peak of the whole frame's
   draw (turn-softened / exact restarts redraw the passes), capped at 0.4 tick
   so a shader-prewarm or load frame (50-100 ms) is not learned (it held ~40 ms
   of latency for seconds in the first build).
4. **Present before draw**: a waiting present that falls due while the next
   frame draws (it went out late by the draw time, then the next on time: an
   early/late pair) is shown first, but only when wait + present + draw stays
   under 60% of a tick (never on a compositor whose swap blocks a refresh).
5. Kept: post-swap `glFinish`, the 1 ms due lead, the 0.7-period double-present
   gate.

**Verified (windowed Dam, 120 + VSync, G-Sync, 10 s windows):** 120.0
presents/s; gaps in 7-9.5 ms 98.8 / 99.7 / 99.2% (the window holding the level
load: 97.7%); 0 dropped; slots/tick all 2. Timer pacing (VSync off, FpsCap 144,
2.4 per tick): 144.2 presents/s, slots 2/3 at 3:2. Maintainer, fullscreen OLED by
eye: flicker gone ("seems great"). Owed: fixed-refresh (G-Sync off) regression check.

**Deck 90 follow-up (2026-10-09, OPEN):** present timing on the Deck is even
(90.0/s, gaps 9.5-12 ms) and the maintainer called it "way better than before",
but it is not smooth: drops at spawn-in, and on the Dam wall (past the 2nd guard
tower) hitching that lasts until the level ends. Measured causes: (1) on the Dam
wall the REACH test fired every frame (7-25 rooms the replay reaches that the
game culled by distance), so 90-100% of frames were exact = 60 fps motion on 90
Hz; (2) ROOMCHANGE made every ~5th frame exact as distant rooms entered and left
the set; (3) draws of 8-10 ms (3-4 passes of 2.5-3 ms, turn-softened restarts
double them) hold queued presents; (4) game-side stalls (missed ticks). An
experiment series is parked on branch `wip/d583-deck-pacing` (5b66e012, not for
merge): REACH on display-list truth helped (10-21% exact on the wall), dropping
ROOMCHANGE brought guard-tower flicker back (entering rooms drawn with the
current camera's scissor), and presents between passes made it worse (the game
lost ticks: a swap that blocks under gamescope sat ahead of SP/DP done). Next
step: a holistic review (local HANDOFF), not more point fixes.

**Deck 90 fixed (2026-10-09, branch `feat/d583-deck90`, maintainer Deck by eye:
"looks really good and feels pretty good").** The review found that presents
were already even; the cost was redraws and the in-between images. Changes
(`gfx_pc.cpp` interpolation, `interpportal.c`, `libultra.c` stats):
1. **False turn detection (the main cost).** The turn gate measured the camera
   offset on the projection *after* the far-clip widening, which alone reads as
   ~0.16 deg, and divided it by (1 - alpha) floored at 0.05: a pass with alpha
   just under 1 (the lag falls slowly) read as ~3 deg/tick and every such frame
   was "softened" and redrawn from scratch (100-230 per 10 s). Now measured on
   the blended projection before widening, and only where (1 - alpha) > 0.1 or
   the offset is > 0.25 deg (float rounding in the angle is ~0.05 deg).
2. **No full redraws.** An exact fallback draws the exact image once and stores
   it in every slot (it drew n identical passes). A turn that needs softening is
   applied at the frame's start from the previous frame's measured turn (a turn
   is smooth); a frame is redrawn only when its own turn needs > 25% more. The
   turn is often first seen on a later projection load (GE loads one per room),
   so switching alpha in place mid-pass would tear between rooms; an in-place
   switch is kept for the first load only (`noinplace` in d578_live = off).
3. **Room scissor = union of the replayed box and the game's box.** The replay
   alone clipped distant Dam guard towers on in-between presents (flicker that
   depended on distance; `nounion` = replay box only). Rooms that enter the drawn
   set and that the replay does not reach are hidden on in-between passes
   (`nohide`); REACH holes are judged on display-list truth (a hole only if the
   previous DL drew the room and this one does not); ROOMCHANGE only when the
   replay did not run (from the parked WIP branch).
4. **Projection pairing.** All projection loads shared one pairing bucket with
   one-to-one claims; when the count changed (outside rooms seen from a guard
   tower window coming and going) a load ran out of partners or paired with a
   different kind of projection and its object was drawn at the wrong camera or
   depth for one in-between present (the tower's wall alarm vanished; burst
   capture, present 639). A projection now pairs with the nearest previous one
   even if claimed, and a per-pass memo gives identical source projections the
   identical blend (235-776 corrections per 10 s logged); `nomemo` = old.
   Shared partners leave previous projections unclaimed, so projections are
   left out of the set-change count (first PC 120 re-check: 23 "vanished"
   projections per frame made 71-100% of Dam frames exact; after: 1%).
5. **Present cost under gamescope.** The game itself needs ~0.15 ms per frame
   on the Deck and is almost never waiting on us (new `D583 game side` line:
   game work after its retrace / blocked by our previous frame / worker
   pickup). The real cost was the post-swap `glFinish`: 3.3 ms per present (up
   to 14) on the thread that also draws, so the worker picked up new frames
   3.5-6 ms late and dropped up to 40 presents per 10 s in NPC-heavy areas.
   Skipped under gamescope (`GAMESCOPE_WAYLAND_DISPLAY` / `SteamDeck`): 0.3 ms
   per present, pickup < 1 ms, 0 drops (maintainer: "definitely an
   improvement"). Kept on PC (the VRR fix relied on it); `finish` / `nofinish`
   in d578_live force it. Side effect: swap-return gaps no longer measure the
   real flip there (gamescope's stats pipe would).
6. **Cut detection replaces the set-change check** while modelviews are drawn as
   loaded (only the camera blends): an unmatched object costs nothing visually,
   and counted on matrices a crowd of animating guards made 30-50% of Deck
   frames exact (hold, jump; still 14% at a 60% limit). A frame is now exact
   when most of its projection loads have no plausible previous partner (the
   camera jumped). A first version that fired on any odd projection hit 8
   consecutive frames; it now needs a majority. The matrix set-change check
   runs only with `mvblend` or an explicit GE_INTERPSET_PCT (default 60% then).
   Maintainer, Deck crowd fight: "good now".
   A tried "tight budget" mode (<= 2 passes, no softened redraws while frames
   ran late) made heavy areas worse (32-35% exact) and was removed.
7. **Instruments:** cadence line + motion histogram and present cost; `D583
   decisions` (pre-softened / in-place / late redraws); `D583 projection memo`;
   `D583 ROOMSET` (room-set changes with hidden rooms, capped); `D583 SHRINK`
   (replayed box < half the game's); `D583 SLOW`; all to ge007.log.

**Deck result (Dam, 90 Hz, VSync):** draw peak 8-10 ms -> 3-5 ms; 88-90
presents/s with 0-3% exact frames on the wall walk; motion histogram mostly
`ok`. Remaining non-pacing items seen during the checks, present with every
present exact (so not interpolation): the outside seen through a guard tower's
window/doorway changes as its rooms leave/re-enter the drawn set, and on the
Dam wall the grate decals vanish behind a sliding edge and the wall lighting
shifts with view (ROADMAP). Both are faithful: also in v0.5.0, and the
maintainer saw the exact same behaviour on 1964 (GEPD). Also seen: a game-side delay of ~14 ms on every
other frame in some stretches (submit delay; present cost now logged to tell a
blocking swap from game CPU time). PC 120 + VSync re-check (2026-10-09,
windowed Dam, G-Sync): 120.0 presents/s, 99% of gaps 7-9.5 ms, 1% exact,
motion `ok`. Seen after the cut-detection build (maintainer, Deck): the Dam truck
can flicker briefly; Aztec M16 viewmodel flicker in some spots; rare dead-body
flicker before fade-out (all unexplained, need captures). Owed: a per-level Deck pass and a 90 fps
performance validation across levels (only Dam was exercised).

**Dev words removed at close-out:** `blackcap`, `blackpix`, `nosync`, `nodue`
(and `gfx_opengl_interp_black`). The `D578 cadence` stats line stays, now with
grid period, re-anchors, trims and draw peak.

**Also seen:** brief haziness in some spots / on guards at 120 (D578 interp
artifact, not pacing) — separate D578 item.

### Addendum 2026-10-10: Deck per-level pass, hold causes, Bunker draw cost

**Per-level pass (maintainer, Deck 90, all levels briefly):** "overall very strong". Fixed from it: props/shadows through walls at range (D578 addendum, widening off), Cradle catwalk flicker (D584). Turn-softening re-tune closed as not needed (Dam: 98-99.7% of gaps 9.5-12 ms, 0-5 dropped per 10 s, maintainer saw no tunnel holes). Open: Bunker 1/2 fight drops, Jungle feel, Cradle turret explosions (noted only).

**Hold causes (cadence line, 6d7aa851):** `motion hold:N (exact, a=0, late, other)`. Jungle: every hold is an exact fallback (1-7 per 10 s), almost all REACH (a room the previous display list drew and this one dropped; the trigger's log lines are capped, the count is in `room-change-exact`). `noreach` removed them with no visible holes in Jungle, but the maintainer felt no real difference ("not significantly"; Egyptian and others feel better). So the Jungle feel is not holes/holds and does not show in the pacing stats. Untested lead: content refreshed at the game tick inside a blended 90 Hz view (the sky is always this frame's; objects not paired).

**Bunker draw cost (SLOW line now carries batches, tris, texture lookups / misses / dyn-hash bytes / time in `import_texture`, 51f2282d):** fight frames draw 9-17 ms. Per pass ~550-800 batches, ~3,500-4,700 tris, ~4-5 ms on the Deck: ~6 us per batch. Texture lookups = about 1.2 per batch, 3-55 real misses, no dynamic-texture hashing; ~1 us per lookup (2.4-4.9 ms of a frame), mostly the GL bind and flush that come with each batch.
- **Refuted:** "the same texture is re-imported back to back". A skip-if-already-bound build (no flush/lookup/bind when the unit holds the same key) left batches per frame unchanged: consecutive batches really use different textures. Reverted; not worth its risk.
- **Landed: late-soften budget gate** (`gfx_interp_tick`, `nobudget` = old behaviour). A softened restart redraws every pass; when the pass just drawn says n+1 passes exceed 3/4 of a tick, the restart is skipped (that frame's in-between image keeps a little more turn blend). Bunker fight: 3-pass frames 28 of ~290 slow frames (was most of them), 17-35 restarts skipped per 10 s, goldens unaffected (60 fps path untouched). Drops remain: 2-pass frames still reach ~13 ms. Note: unlike the removed "tight budget" mode (item 6 above), this never forces exact frames.
- **Leads for the next session:** (1) spike frames with 150-180 texture MISSES costing 10-18 ms in `import_texture` (~70 us per miss): likely cache churn with many guards on screen (`TEXTURE_CACHE_MAX_SIZE` 1024, CI textures keyed on palette content) or first-use decode; (2) per-batch cost ~6 us: fewer state changes / persistent VBO streaming / batching across texture changes are the structural options; (3) the last windows of the final run showed split present gaps (7-9.5 ms: 444-565, 12-20 ms: 236-292 per 10 s) with no softening activity: unexplained, read with the game state in mind.

### Addendum 2026-10-10 (late): Bunker fight drops are present scheduling, not draw cost

Measured with the new Deck harness (`tools_pc/levelbench.sh` + `deckrunner.sh`, Game Mode, a recorded Bunker 1 fight replayed with `--replay`; replays drift a little with emergent AI but reproduce the same activity).

- **Symptom:** in the fight, presents stay at ~90/s (0-16 dropped per 10 s) but split into ~8 ms / ~14 ms gap pairs (up to 50-65% of a window's gaps; maintainer saw ~80 fps). Game side idle (~0.15 ms per tick).
- **Not CPU-bound:** `perf` over 20 s of fight: the render worker is on-CPU ~30% of one core, ~40% of that is its own pacing spin (`clock_gettime`, `sysCpuRelax`); Mesa's `gl0` / `gdrv0` threads ~6% each.
- **Not the GPU clock:** the governor holds 200 MHz for most of the fight (`pp_dpm_sclk`, 40-86% busy), but with Manual GPU Clock 1600 (QAM) the fight is unchanged (8.3% uneven, fight-frame draw ~10.5 ms).
- **Not the vertex upload:** a streaming ring VBO (glBufferSubData at stride-aligned offsets instead of a glBufferData per batch) measured 4.3% / 3.1% uneven vs 8.6% / 4.0% for two runs of the old build: inside replay noise. Dropped.
- **Cause (IPLOG, new `IPLOG draw` line + `GE_INTERPLOG=<n>` line cap):** of 793 present gaps >= 12 ms, 567 (72%) are a present that fell due while the single render worker was drawing the next frame; it goes out when the draw ends, median 6.8 ms late (draw median 8.8 ms in those frames). 556 of 618 overdue cases are an EARLIER frame's slot waiting behind the new draw; only 62 are the frame's own slot.
- **Tried: present a due slot between passes** (hook after `gfx_opengl_interp_store`, never waits, only while swaps are cheap): fired 41 times in 100 s; 5.8% -> 4.7% uneven, within noise. The due point usually falls in the last pass or a single long pass. Reverted.
- **Next (maintainer chose this before the v0.5.1 gates):** decouple present from draw: a present thread with a shared GL context that blits finished slot textures and swaps at their times while the worker draws. Mind D583's lesson that a refresh-blocking swap must never sit ahead of SP/DP done (under gamescope VSync is now off and swaps are ~0.3 ms).
