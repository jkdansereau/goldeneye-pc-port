# Unlocked FPS Plan

Status: **proposal, parked** — documented while audio work (project Phase 3) is
active. Do not start Phase 0 until audio is done; first action when resumed is
the Phase 0 measurement pass.

**M-119 note:** this plan's premise below ("default preset = ~30fps") is now
stale — **D248** (M-118) found and fixed a real port bug (a pointer-width
scheduler ABI bug) that had been silently halving the game to 30fps since the
port's inception; the corrected default is a genuine 60fps. Re-measure Phase 0
against the corrected baseline before resuming this plan. Also see **D250**
(M-119, open): the exposed real 60fps cost now drops to 20-40fps intermittently
on some levels — a performance pass may be a prerequisite for this plan, not
just a baseline re-measurement.
Related: `docs/dev/findings.md` §D117 (GE_DETERM), §D155 (catch-up clamp),
`port/src/libultra.c` (pacemaker), `src/game/frametiming.c`, `src/sched.c`.

## Decisions (agreed)

1. **Default preset = the original pace** — whatever the game currently does
   (measured ~30 fps; Phase 0 pins the exact rate). All testing, level
   sweeps, determinism runs (`GE_DETERM`), and playtest baselines stay on the
   default preset. Its code path must remain byte-for-byte what it is today.
2. **Unlocked FPS (60 / 120 / uncapped) is EXPERIMENTAL.** It ships behind a
   clearly-labelled option and may be less polished than the default.
3. **Hard requirement:** raising the presentation rate must never change game
   speed. Sim rate is invariant across presets. This is "the main speed-up
   issue" and it gates everything else.
4. **Follow the PD PC port where feasible** (`pd_port` checkout): wall-clock
   accumulator pacing with remainder carry, vsync + framerate-limit options,
   safety cap when vsync is off.

## Non-goals (for now)

- Motion interpolation between sim ticks (the only thing that would make 120
  fps *look* like 120 fps). Requires per-entity prev/cur state in the
  DL-building game code — rule #2 territory, parked as a separate proposal.
- Changing the sim rate itself (running logic at 120 Hz with halved constants
  is explicitly out — game logic).

## Background: why raising FPS today speeds up the game

One clock drives everything:

- `portTickThread` (`port/src/libultra.c`) posts a VI-retrace message on a
  free-running grid (60 Hz NTSC / 50 Hz PAL, `g_tickIntervalUs`).
- `src/sched.c` `__scMain` blocks on that retrace; `__scHandleRetrace`
  notifies clients → **exactly one sim tick + one render per retrace**.
- `osGetCount()` is wall-clock scaled to 46.5 MHz. `waitForNextFrame()`
  (`src/game/frametiming.c`) converts elapsed time to `deltaFrames`, which
  becomes `g_ClockTimer` (`src/game/lv.c:1008`) driving the per-tick sim loops.

The quantization is the trap: `nextFrameTime = (elapsed + Q/2) / Q` (Q = one
frame quantum). The `+Q/2` bias **rounds every interval up to at least 1
tick**, and `waitForNextFrame` spins until ≥1 tick is due. Consequence:
**sim speed tracks the retrace rate almost 1:1** — 30 Hz retrace → 30 ticks/s,
120 Hz retrace → ~120 ticks/s (each 8.3 ms interval rounds up to a full
tick). That is exactly the observed "anything faster speeds up the game".

Two useful existing properties:

- GE's game code already handles `g_ClockTimer == 0` and multi-tick deltas
  everywhere (`for (i = 0; i < g_ClockTimer; i++)`, explicit `== 0` guards in
  bondhead/bondview2/propobj/explosion). The N64 used this for dropped frames.
- D155 already clamps catch-up to 6 ticks after a real-time hitch.

## What the PD port does (reference)

- `src/game/timing.c` `frametimeCalculate()`: wall-clock accumulator with
  remainder carry (`lostframetime60t`) → `diffframe60` / `diffframe240` ticks
  due **per rendered frame**; render loop runs at display rate.
- Vsync + framerate limit options (`VIDEO_MAX_FPS` 200/240); vsync-off with
  no cap forces the safety cap.
- Feasibility mapping to GE:
  - *Pacing math (accumulator + carry)* → adoptable **port-only**. ✓
  - *Render frames carrying zero sim ticks* → blocked in GE by
    `waitForNextFrame`'s ≥1-tick enforcement (`frameDelay`, only settable
    under `LEFTOVERDEBUG`). A crafted `osGetCount()` cannot defeat it (the
    spin + `+Q/2` bias always yield ≥1). Needs a game-code change → **parked**
    (see "Parked" below), and note it buys little without interpolation.

## Phases

### Phase 0 — Baseline & ground truth (port-only)

**RESOLVED (2026-09-12, commit 8f2f9e1d, D248).** The ~30 fps default was
confirmed a **port artifact**, not original N64/GE behavior: `sched.c`'s
`__scHandleRetrace` read a per-client "every retrace vs. every other
retrace" flag via a hardcoded `*((s32*)client + 2)` byte offset (`sched.c`
~line 350) that only lands on the right field when `OSScClient` is 8 bytes
(32-bit pointers, true on N64). On this 64-bit port the struct is 16 bytes,
so the read landed inside the gfx client's own `msgQ` pointer (always
non-NULL), silently collapsing the gfx client onto the audio client's 30Hz
path even though the VI-retrace pacemaker itself always ticked at a correct
60Hz. Fix (`#ifdef PORT`, `sched.c` ~line 348): index the struct
(`client[1].next`) instead of the hand-rolled offset, so it scales with
pointer width. Verified live via the `Video.DisplayFPS` overlay: rock-stable
30 FPS pre-fix, 60 FPS post-fix, same build/scene. Same bug class as
D122/D126/D132/D209 (pointer-width struct growth), just in the scheduler.

**Decision-gate answer:** the default preset is now the console's real
60Hz(NTSC)/50Hz(PAL) rate — decision #1 above ("default preset = the
original pace") is satisfied by this fix, not by preserving the halved
30fps. All prior playtest/determinism baselines predate this fix and should
be treated as pre-D248.

- [x] Find why the default was ~30 rather than the console's 60 Hz — root
      cause above; §F **D248**.
- [x] Record a Dxxx finding with the measured rates and root cause — D248.
- [ ] Measure actual sim rate and present rate with a dedicated periodic log
      line (`sim=..Hz render=..Hz`) rather than the one-off `DisplayFPS`
      overlay reading used to verify D248. Not yet built — still open if a
      standing measurement (vs. a spot-check) is wanted before Phase 1/2
      work lands.
- [x] **Decision gate** — resolved above.

### Phase 1 — PD-style pacing hardening (port-only)

Makes the default preset stable and drift-free; no game-code changes.

**Audit (2026-09-12, post-D248):** more of this than the plan assumed
already exists. Verified by reading the code directly (not just the
delegate's first pass, which mis-read one of these — see below):

- [x] **Accumulator + remainder carry, tick thread.** `portTickThread`
      (`port/src/libultra.c` ~294-313) already schedules off `g_nextTickUs +=
      g_tickIntervalUs` — the *scheduled* time, not `now` — so it doesn't
      drift under load. It resyncs to `now + interval` only when a tick is
      already overdue (`g_nextTickUs <= now`), which is a bounded-catch-up
      behavior, not a naive reset. This already matches the PD
      `frametimeCalculate` accumulator pattern.
- [x] **Accumulator + remainder carry, present-side limiter.**
      `sync_framerate_with_timer` (`port/fast3d/gfx_sdl2.cpp` ~403-428) uses
      the same shape: `next = previous_time + interval` (schedule-based, not
      wall-clock-based), a sleep-then-busy-wait hybrid to hit the deadline
      precisely, and only snaps `previous_time` to the real wake time `t`
      when the overshoot exceeds ~1ms (`t - next < 10000` in its 100ns units)
      — i.e. it already carries the remainder in the normal case and only
      gives it up after a real hitch. (An earlier audit pass mis-read this as
      "no remainder carry, resets every frame" — it does not; re-verified by
      reading `gfx_sdl2.cpp:403-428` directly.)
- [ ] **Triple-buffered swap chain.** Not present — presentation uses a
      standard SDL/GL double-buffer swap (`SDL_GL_SwapWindow`,
      `gfx_sdl_swap_buffers_begin`, `gfx_sdl2.cpp` ~436-444). Not started.
- [ ] **PD safety rule (vsync off + no cap → force a max cap).** Not present.
      The only existing floor/ceiling logic is the D186 guard in
      `gfx_sdl_set_target_fps` / `video.c` (~289-292, ~464-467), which goes
      the *other* direction — it refuses caps below 30 (because pacing still
      runs inline on the sim thread, see Phase 2 note below) — there is no
      "vsync off AND cap==0" case that forces a ceiling. Not started.
- [x] Config: `Video.VSync` / `Video.FpsCap` already registered
      (`video.c:106-107`) and live-reapplied (`video.c` ~340-349).

**Net:** the hard part of Phase 1 (drift-free accumulator scheduling) is
already done, on both the sim-tick and present-side clocks. What's left is
narrow: a safety-cap rule and, if wanted, triple buffering — plus whatever
Phase 2 needs on top.

**Phase 2 first slice, scoped (2026-09-12).** The real gap for a 120fps
*presentation* preset isn't pacing math — it's that `gfx_run()`
(`gfx_pc.cpp:3179-3232`) composites straight into the default framebuffer
(fb 0) once per sim tick and then calls `gfx_wapi->swap_buffers_begin()`
once; there's no mechanism to show that same completed frame again on an
extra vsync between sim ticks. The existing `GfxRenderingAPI` already has
everything needed to do this **without any new GL primitives or a second
thread/context**:
- `copy_framebuffer(fb_dst, fb_src, left, top, flip_y, use_back)`
  (`gfx_opengl.cpp:1270`) already blits the real window backbuffer
  (`fb_src == 0` + `use_back = true` reads `GL_BACK`, confirmed
  `gfx_opengl.cpp:1309-1311`) into an arbitrary allocated FBO/texture, and
  can blit back the other way (`fb_dst == 0` writes straight into the real
  default framebuffer, `gfx_opengl.cpp:1214`'s `fb_id == 0 -> bind 0`
  convention).
- `gfx_pc.cpp` already keeps a couple of always-allocated internal
  framebuffers this same way (`game_framebuffer`,
  `game_framebuffer_msaa_resolved`, created at `gfx_pc.cpp:3069-3070`) — a
  new "last presented frame" capture FBO is the same pattern, one more
  entry.
- The tick clock (`portTickThread`) runs on its own pthread, independent of
  the render/sim thread that calls `gfx_run`/`swap_buffers_begin` — so
  spending extra wall-clock time *inside* `swap_buffers_begin` presenting a
  duplicate frame does not touch the tick schedule at all; it only changes
  how much idle time the render thread burns before the next tick message
  is waiting for it. Sim rate stays exactly invariant by construction, per
  decision #3.

**Smallest concrete slice:** capture the just-finished frame into a new
internal FBO right before the real `swap_buffers_begin()` call
(`gfx_pc.cpp:3230-3231`, via `gfx_rapi->copy_framebuffer(captureFb, 0, -1,
-1, false, true)`); when an experimental `Video.PresentRate` config knob
requests double-rate (2x — e.g. 120 from a 60Hz NTSC sim, 100 from 50Hz
PAL), have `gfx_sdl_swap_buffers_begin` (`gfx_sdl2.cpp`) do the normal swap,
then — using the same schedule-accumulator shape as
`sync_framerate_with_timer` — sleep to the halfway point of the tick
interval, blit the capture FBO back into fb 0
(`gfx_rapi->copy_framebuffer(0, captureFb, -1, -1, false, false)`), and
`SDL_GL_SwapWindow` again. Default (`Video.PresentRate = 0`) must take none
of these code paths, so the default preset stays byte-identical to today.
Restrict the first slice to exactly {0 = off, 2 = double}; don't generalize
to arbitrary/uncapped multipliers yet — that needs the vsync-off safety cap
from the Phase 1 list above first. No options-menu UI in this slice
(config-file-only); that's a fast follow once the mechanism is playtest-
verified not to tear/stutter.

### Phase 2 — Experimental presentation rates (port-only)

The user-visible "60 / 120 / unlocked" feature. Game speed is invariant **by
construction**: sim + render stay on the original tick clock; only *present*
runs faster by showing each rendered frame more than once.

- [ ] Frame duplication at the present stage (`video.c` / fast3d SDL swap
      path, near `gfx_pre_swap_hook`): after a fresh frame is presented,
      re-present the last finished frame on extra vsyncs until the next sim
      tick's frame is ready. Target rates: 60, 120, uncapped (safety-capped à
      la PD `VIDEO_MAX_FPS`).
- [ ] Input at present rate: sample SDL input continuously in
      `port/src/input.c`; each sim tick consumes the latest state (compare
      against PD `joy.c`). This is the real tangible win of the feature: up
      to ~half a frame of latency removed even without interpolation.
- [ ] Options menu entries, clearly labelled **experimental**
      (`port/src/optionsoverlay.c`), default preset untouched.
- [ ] Audio check: `src/audi.c` uses real-time `osGetTime()` and its own
      thread; libaudio buffer sends ride the sim/retrace clock. Verify nothing
      divides by an assumed frame rate; expect no changes.

### Phase 3 — Verification & sign-off

- [ ] `GE_DETERM` regression: default preset simulation path byte-identical to
      pre-change (existing determinism diffs keep passing).
- [ ] Preset-invariance playtest: a timed in-game event (e.g. a fixed-length
      animation or timer) takes the same real time on default / 60 / 120 /
      unlocked. This is the acceptance test for decision #3.
- [ ] Hitch recovery: force a multi-second stall (debugger pause / asset load),
      confirm D155 clamp holds and no speed spiral on any preset.
- [ ] `./build-pc.sh` + `/linkcheck` ritual; new findings in §F/§H with index
      labels.

## Timing-consumer audit (each gets a line in the finding)

| Consumer | Clock class | Action |
|---|---|---|
| `src/game/frametiming.c` (`waitForNextFrame`, `updateFrameCounters`) | sim-time | unchanged; stays tick-locked |
| `src/boss.c:403,517` (main-loop gate, `MAIN_LOOP_TICK_INTERVAL`) | sim-time | unchanged; re-verify vs new pacemaker |
| `src/usb.c:387,405` (LibDragon flash timeouts) | real-time | fine as-is; document |
| `src/speed_graph.c` | measurement | fine; source of the rate log line |
| `libultrare/audio/env.c`, `reverb.c` `lastCnt` | perf counters only | fine; document |
| `src/audi.c` `g_DeltaTime` etc. (`osGetTime`) | real-time | fine; verify in Phase 2 |

## Parked (needs explicit sign-off, separate proposals)

1. **True decoupled sim/render (PD's zero-tick render frames).** Minimal
   change class: relax `waitForNextFrame`'s ≥1-tick enforcement so rendered
   frames can carry `deltaFrames = 0` (GE already guards `g_ClockTimer == 0`
   everywhere). This is a *behavior-affecting* game-code edit gated off by
   default — outside the ABI-only D3x exception, needs its own rules decision.
   Perceptual benefit over Phase 2 duplication is small **without**
   interpolation, so do this only as a prerequisite for #2.
2. **Motion interpolation** (prev/cur entity state, lerp at render time).
   The actual "120 fps looks like 120 fps" feature. Large, invasive, in
   DL-building game code. Separate proposal; not implied by this plan.

## Open questions

- Phase 0 outcome: is the ~30 fps default a port artifact? (fix vs keep — see
  decision gate)
- PAL builds: default stays 50 Hz pace; experimental present rates identical
  option set. Confirm no PAL-specific pacing assumptions in Phase 1 math.
- Naming/caps for "unlocked": adopt PD's safety-cap value (200/240) or expose
  the cap as a slider?

**M-87 PD-legacy-survey note (candidate 8):** if this plan stalls and GE stays
fixed-tick, PD's `Game.TickRateDivisor` (0–10, slow-motion/perf lever) +
`Game.ExtraSleep` (`port/src/main.c:39,167-168`, applied `src/game/timing.c:49`)
is a cheap borrowable fallback lever — not otherwise relevant since this plan's
whole direction is decoupling sim from frame rate, the opposite of PD's
fixed-tick-with-a-divisor approach.
