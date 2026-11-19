# D600

<!-- from the old section-F/H table row -->
| D600 | **VSync-on frame pacing: the 60 Hz VI retrace tick is a free-running wall-clock pacemaker (16666 us, ~40 ppm fast) not locked to the display vblank, so its phase slides against the blocking swap (~7 min beat on a 60.000 Hz panel); when it lands in the render+swap window every frame is late, the tick gate skips, and fps stays at 40-50 until the phase slides back. Random start phase per launch = a restart re-rolls it. Port-layer fix: first-order phase lock of the tick to the swap-return time.** — full `## D600` entry at file tail | **FIXED + LIVE-CONFIRMED 2026-10-08** (macos branch; Mac M3: 11000-frame Cradle run flat 16.67 ms / swap-pace flat 13.6 ms, no creep; Windows: guard self-disables where the swap does not block, baseline unchanged; maintainer live play: the chop is gone, full campaign + 52k-frame session clean; resolves D598) |

## D600: display-locked retrace tick (fixes the D598 macOS "choppy / fps never recovers / restart re-rolls it") — FIXED 2026-10-08 (macos branch)

**Symptom.** Maintainer live playtest on the M3 MacBook Air: random, session-wide
slowdown (40-50 fps) that does not recover, sometimes survives one restart and is gone
after two; VSync off stops it. Not CPU/GPU load, memory pressure or thermals (D598
measurements 1-3).

**Mechanism.** `portTickThread` (libultra.c) posts the VI retrace on a free-running
wall clock: `g_tickIntervalUs = 1000000 / 60` = 16666 us (truncated; true 16666.67,
i.e. ~40 ppm fast). With VSync on, the render worker's swap blocks on the display
vblank. The tick phase therefore slides against the vblank by ~0.67 us/frame (live
log: `swap/pace` creeping 10.7 -> 15.1 ms over ~8700 frames = ~0.5 us/frame, ~7 min
per full beat). While the tick is inside the render+swap window every frame is late
and boss.c's tick gate skips (frame intervals 22-33 ms) until the phase slides back
out. Each launch starts at a random phase, so a restart re-rolls it. Nothing is
cached across launches. The same architecture exists on every VSync platform
(D475 "frame limiter skipping a retrace", parked Statue/Cradle hitching); macOS shows
it because its swap blocks and the beat is slow and visible.

**Fix (port layer only).** `portNoteSwapReturn()` in `port/src/libultra.c`, called
after `videoEndFrame()` in `portRenderGfxTask`: a first-order phase lock. Phase =
(next tick - swap-return time) mod period; error vs a 1500 us target; the next tick
is nudged by err/8, clamped to +-100 us/frame, consumed by the pacemaker via an
atomic exchange. Engages only when `videoVSyncOn()` (new accessor, video.c/h) and
consecutive swap returns are within 10 % of one game frame; otherwise the pacemaker
free-runs exactly as before. Runaway guard: >400 consecutive saturated nudges disables
it (a driver that does not block on vblank would make the swap return track the
render). `GE_NOTICKLOCK=1` disables it (A/B), `GE_DETERM` keeps it off. Log line
`D600: tick lock phase=... err=... nudge=...` every 300 locked frames.

**Verification.** Mac M3, windowed 1280x720 VSync 1 FpsCap 60, Cradle 11000 frames:
phase converged from 7685 us off-target to ~1.1-2.3 ms band within the first window and
held; `interval` 16.66-16.67 ms in every 300-frame block; `swap/pace` flat 13.5-13.65 ms
(previously creeping ~0.5 ms/1000 frames); no crash log. Windows x86_64: the swap
returns in <1 ms (does not block on vblank), the guard disabled the lock after the
initial slew as designed, steady 16.72 ms identical to the `GE_NOTICKLOCK=1` baseline.
**Not covered:** an unlocked-vs-locked same-session A/B over a full ~7 min beat (the
unlocked behaviour is from the maintainer's live log), real gameplay load, the
interp (D578) branch interaction (its swap-driven presents sit on top of the same
pacemaker; recheck when merging), Linux/Deck. Maintainer live playtest owed.

**Status: FIXED 2026-10-08 (macos branch; LIVE-CONFIRMED 2026-10-08 (maintainer, macOS M3: the chop is gone in live play — Cradle-class levels and the full campaign; the 52k-frame session ended at a clean `video: quit requested` exit, and `ge007.log` shows the D600 tick-lock lines active with flat 14-15 ms render/swap)).**
