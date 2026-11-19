# D598

<!-- from the old section-F/H table row -->
| D598 | **arm64 macOS gameplay is "a bit choppy" on heavy levels (Cradle-class) — unmeasured (maintainer live observation, 2026-10-08). First clue: the port's per-300-frame blocks complete in 6–17 ms on the M3, so the CPU side is far faster than the frame budget — the chop is more likely display/swap pacing (Metal-backend swap interval / vsync / the scheduler's 60 Hz pacing assumption) than software-RSP throughput; a measurement plan (reproduce → block-time A/B vs Win → display-path bisect) is in the entry.** — full `## D598` entry at file tail | **RESOLVED 2026-10-08 by D600** (display-locked retrace tick; live playtest confirmation owed) |

## D598: arm64 macOS — gameplay is "a bit choppy" on heavy levels (Cradle-class); unmeasured — OPEN 2026-10-08 (macos branch; maintainer live observation)

**Symptom.** During the 2026-10-08 maintainer live pass (Launch E,
`MENU-LIVE-CHECKLIST.md`), input/menus all behaved as on Win/Linux, but
gameplay is "a bit choppy on levels such as Cradle" (maintainer's words —
Cradle 41 is an AI-dense level; "levels such as" implies a load pattern,
not one level). Menus and lighter scenes read fine.

**First clue (not a diagnosis).** The live run's console log shows the
port's per-300-frame blocks completing in **6–17 ms** on the M3
(`frame N rendered in X us`) — i.e. the scheduler-thread work (software
RSP + game logic) is far faster than the 5 s a 300-frame block represents
at 60 Hz. If the CPU side is that idle, the choppiness is more likely in
the *display/swap pacing* layer (GL swap interval on the Metal backend,
vsync, or the scheduler's 60 Hz frame-timing/pacing assumption running
against a VSync-ed display) than in software-RSP throughput. This is a
hypothesis to test, not a conclusion — a heavy Cradle frame may still
spike the block time.

**Owed (measurement, before the merge is "final"):**
1. Reproduce: Cradle 41 live on the Mac; note the chop pattern
   (constant vs frame-aligned spikes; menu/Dam comparison).
2. Instrument: the port already logs per-300-frame block time (`NOTE
   frame N rendered in X us`) — run Cradle on Mac + Win with the same
   `GE_RSEED` and compare block times at matching frames (is the CPU side
   fast on both, or spiky only on Mac?).
3. If block times are fine on Mac, bisect the display path: GL swap
   interval / vsync state on the Metal backend, `SDL` window flags, and
   whether the scheduler's 60 Hz pacing loop assumes an *un*-VSynced
   swap (a VSync-ed swap adds display-timing coupling the N64/Win
   pacing logic never had).
4. Record the fix (or the "display-backend limitation" call, if that's
   what it is) with before/after block-time numbers.

**Cross-refs.** The D441 Mac sweep is a crash/corruption detector, not a
performance gate (frame counts, not pacing) — a clean sweep says nothing
about D598. `ge_keylog.txt` / the run log from the live pass are in
`~/ge007/cap33/` on the Mac (local note, not in the tree).

**Measurement 1 (2026-10-08, M3, scripted Cradle `-level_41`, `GE_PERFSTAT=1`,
2400 frames, same ini as the live pass, no crash log).** Frame interval is a
flat 16.66-16.67 ms (60.0 fps) in every 300-frame window; 0 frames >20 ms
after the 2-frame startup spike (78 ms at frame 2); max steady-state spike
17.6 ms; game-thread busy 0.02-0.03 ms/msg; `run` 10-11 ms of which GPU
1.4-1.6 ms; swap/pace 8-9 ms (idle headroom). So in-process pacing and CPU/GPU
cost are NOT the problem on the scripted spawn scene (tris ~2000). Caveat: the
scene is light (no firefight); a live heavy moment, or a display-side effect
(compositor, fullscreen mode; the M3 MacBook Air panel is fixed 60 Hz, not ProMotion) that
the in-process interval cannot see, is not excluded. Next: a `GE_PERFSTAT=1`
log captured during an actual chop; and compare the display refresh rate.

**Measurement 2 (2026-10-08, M3 MacBook Air, automated battery
`scratch/mac_preplay.sh` + A/B `mac_pace_ab.sh`/`mac_pace_rep.sh`, isolated run dir).**
Windowed 1280x720 vsync, 3000 frames: statue/train/runway/surface2 hold 60.0 fps;
facility/streets/control/archives/cradle ran 40-51 fps (interval 18-25 ms, mix of
16.7 and 33 ms frames) although dl 0.2-0.9 ms and GPU 0.7-1.3 ms, with swap/pace
14-15 ms. The slow mode is a per-RUN bimodal state, not a level property: facility
repeated 4x at cap60 = 60.0 x4; at FpsCap=0 = 60,60,43.1,60; an earlier identical
facility config ran 41.5. It does not follow vsync on/off, windowed vs fullscreen, or
FpsCap alone (all four vsync/fullscreen combos read 60.0 once). Cradle's slow run
recovered over time (46->51 fps as swap/pace fell). Reads as intermittent
vsync/compositor phase lock or CPU-core placement (fanless Air, uptime 31 d; idle CPU, no thermal flags, but memory pressure: 15 GB used / 0.4 GB free), not
render cost. Not a crash/correctness issue. Next: repeat n>=10 per config with
`taskpolicy`/QoS and `powermetrics` core residency to separate the two.

**Measurement 3 (2026-10-08).** 10 back-to-back facility launches (windowed
1280x720, vsync, FpsCap 60) with memory pressure sampled per run: 10/10 at 59.9-60.0
fps, system memory free 89-92 %, 0 swapouts. So memory pressure is not the cause and
the slow mode did not reproduce in a quiet 10-run series. The slow runs occurred
inside long back-to-back batteries launched over ssh (unfocused/occluded window,
possible App Nap / core demotion, or sustained-load heat soak on the fanless Air);
the maintainer's foreground fullscreen Cradle run was a flat 60.0. Working
conclusion (LOW confidence): harness artifact, not a port defect. Reopen only if the
maintainer sees sustained sub-60 in foreground live play; then capture `GE_PERFSTAT=1`.

**Measurement 4 (2026-10-08, maintainer live playtest with GE_PERFSTAT; SUPERSEDES the
"harness artifact" conclusion of measurement 3).** The maintainer reproduced it live:
random, session-wide, fps does not recover, sometimes survives one restart, gone after
two. The live log shows, within ONE process, `swap/pace` creeping 10.7 -> 15.1 ms over
~8700 frames (~0.5 us/frame) while real work (`dl`, `run - swap`) stays flat, and the
interval flipping 16.7 -> 22-25 ms once the wait passes ~14 ms; the next process
started at swap/pace 14.9 and hit the cliff within ~2700 frames. Nothing performance
related is cached across launches (ge007.ini holds settings only, ge007.shaders is a
shader-name list unchanged since 10-07). **Diagnosis (strong fit, not yet A/B'd):** the
VI retrace is posted by a free-running wall-clock pacemaker thread
(`portTickThread`, `g_tickIntervalUs = 1000000 / 60` = 16666 us, integer-truncated, i.e.
~40 ppm FAST vs 16666.67 us) that is NOT locked to the display's vsync. Pacemaker phase
slides ~0.67 us/frame against the swap, a ~7 min beat; when the tick lands within the
render+swap window every frame is late and the tick gate skips (33 ms frames). Each
launch starts at a random phase, so a restart re-rolls it (explains bimodal per-run
results, "two restarts fixes it", and Cradle's slow recovery). The same architecture
exists on every platform with vsync (D475 "frame limiter skipping a retrace", parked
Statue/Cradle hitching) — macOS just shows it with a slow, visible beat.
Next (after the maintainer's session): decisive A/B with a temporary `GE_TICKUS` override
(16666 vs 16667 vs 16670: drift sign/rate must change as predicted), then a
display-locked pacemaker (re-anchor `g_nextTickUs` to the swap-return time when
VSync=1) — port layer, determinism path untouched.

**Status: RESOLVED 2026-10-08 by D600 (display-locked retrace tick; live playtest confirmation owed). Measurements 1-4 above are the evidence trail; measurement 3's "harness artifact" conclusion was wrong.**
