# Unlocked FPS Plan

Status: **proposal, parked** — documented while audio work (project Phase 3) is
active. Do not start Phase 0 until audio is done; first action when resumed is
the Phase 0 measurement pass.
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

Goal: know exactly what the default preset does before touching anything.

- [ ] Measure actual sim rate and present rate of the current default build
      (speedgraph + a `video.c` counter; add a periodic log line
      `sim=..Hz render=..Hz`). Confirm NTSC vs PAL build.
- [ ] Find why the default is ~30 rather than the console's 60 Hz. Candidates:
      - tick-thread 4 ms chunked-sleep drift under host load;
      - `__scTaskReady` double-buffer check (`sched.c`) stalling a task every
        other retrace;
      - `__scHandleRetrace`'s client flag read `*((s32*)client + 2)` is
        **out-of-bounds of `OSScClient`** (struct is only `next`+`msgQ`; the
        decomp even says "im wrong size"). For `gfxClient[0]` it happens to
        read `gfxClient[1].next` (zero) — but this is layout luck, worth a
        documented finding either way.
- [ ] Record a Dxxx finding with the measured rates and root cause.
- [ ] **Decision gate:** if 30 fps turns out to be a port artifact rather than
      original behavior, document it and decide with data whether the default
      preset keeps the current pace (test continuity) or is fixed. Default
      assumption: keep current behavior as the "original" preset either way;
      a fix becomes its own item.

### Phase 1 — PD-style pacing hardening (port-only)

Makes the default preset stable and drift-free; no game-code changes.

- [ ] Replace the free-running tick grid in `portTickThread` with a
      wall-clock accumulator + remainder carry (PD `frametimeCalculate` math):
      post ticks when real time says they're due, carry the remainder, bounded
      catch-up after hitches (D155 philosophy). No drift under load, no burst
      of back-to-back ticks.
- [ ] `video.c`: proper frame limiter for fractional caps (spin+sleep hybrid),
      triple-buffered swap chain, and the PD safety rule (vsync off + no cap →
      force a max cap so "unlocked" can't spin).
- [ ] Config: keep `Video.VSync` / `Video.FpsCap`; add whatever Phase 2 needs.

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
