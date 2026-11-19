# D134

<!-- from the old section-F/H table row -->
| D134 | **Frame-2 boot hang / sweep "NO-FRAMES" flakiness — root-caused and fixed (M-22).** — full `## D134` entry at file tail | FIXED (`port/src/libultra.c`) |

## D134 — Frame-2 boot hang / sweep "NO-FRAMES" flakiness — root-caused and fixed (M-22).

**Frame-2 boot hang / sweep "NO-FRAMES" flakiness — root-caused and fixed (M-22).** `osSpTaskStartGo` (port/src/libultra.c) posted the SP/DP task-done events with `OS_MESG_NOBLOCK` into the scheduler's 8-slot `interruptQ`, which the 60 Hz VI pacemaker thread also fills with `VIDEO_MSG`. A gfx task runs the whole frame synchronously on the sched thread (fast3d); the first two frames take 30-80 ms, so the pacemaker queues several retraces meanwhile. Once the queue is full the RSP-done post is **silently dropped** -> `__scMain` never clears `sc->curRSPTask` -> the client never gets `OS_SC_DONE_MSG` -> `bossMainloop`'s `pendingGfx` never clears -> permanent stall (log: `frame 2 rendered`, then `kernel heartbeat ... frames=2`, retrace queue pinned at `valid=8/8 ret=-1`). Reproduced 2 of 3 `-level_09` boots on an idle machine, i.e. most of what M-13..M-21 wrote off as D117 nondeterminism / machine load. `OS_MESG_BLOCK` is NOT the fix (the done event is posted from the sched thread, the queue's only consumer -> self-deadlock): `portPostEventForce()` drops the OLDEST message (always a stale retrace) and retries, and `portPostVIEvent` now reserves 2 slots. Verified: 6/6 `-level_09` boots reach frame 600 with 0 heartbeats (pre-fix 1/3).

**Status (from §F table):** FIXED (`port/src/libultra.c`)
