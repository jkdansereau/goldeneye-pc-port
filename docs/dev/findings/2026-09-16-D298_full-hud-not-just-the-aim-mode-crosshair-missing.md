# D298

<!-- from the old section-F/H table row -->
| D298 | **Full HUD (not just the aim-mode crosshair) missing in a v0.3.0 dev build — user suspects a debug-launch artifact and asked NOT to raise it until seen in a regular release-targeted build; parked** — full `## D298` entry at file tail | CLOSED (2026-09-26, user: HUD is fine now). Earlier: s — regression reported 2026-09-16 during v0.3.0 work (build ce8427cf); user says "don't fix, just for context"; not yet investigated** — full `## D298` entry at file tail |

## D298 — Full HUD missing in a v0.3.0 dev build (reported 2026-09-16; suspected debug-launch artifact)

**Symptom (user, 2026-09-16):** initially reported as "the aim-mode crosshair is missing as of this build" (release/v0.3.0 @ `ce8427cf`, the D243 M-154 probe build); the user then corrected it — **the full HUD is missing**, not just the crosshair. User's own suspicion: "this is the debug launch or something" — i.e. an artifact of how the dev build was launched/configured rather than a code regression. Explicit instruction: **do not raise/chase this until it reproduces in a regular release-targeted build.** Parked.

**Ruled out so far (trivially):** the M-154 probe commits (`1b3476a4`/`ce8427cf`) are `#ifdef PORT`, read-only, and only emit `osSyncPrintf` — no state writes on any path; extremely unlikely to be the cause. Also checked `4fc54293` (D295 M-149, the most recent commit touching a crosshair-referencing file, `front.c`): it only widened two menu-text stack buffers under `#ifdef PORT` and passed a 21/21 sweep — not an obvious suspect either.

**Where the crosshair lives (for the eventual investigation):** `crosshairimage` (`src/image_bank.h:7`, loaded in `image_bank.c`), draw-state fields `crosshair_angle` / `crosshair_x_pos` / `crosshair_y_pos` (`bondview.h:1114-1116`; show/hide flag documented at `bondview.h:558-559`: 0 = shown, 1 = hidden), referenced from `front.c`, `gunfire.c`, `player.c`. Most recent commit touching a referencing file besides D295: `f602b173` (v0.2.1 hotfix batch — D215 "1P weapon viewmodel never drew in-level — renderdata.flags read 0", `gunfire.c`) — same HUD/viewmodel render-data family, plausible regression source if the crosshair draw shares that path.

**Next (only if it reproduces in a release-targeted build):** first re-check the launch path (debug vs release config, `data/ge007.ini` state, any env vars left set — e.g. a stray debug render flag), then ask for the last build where the HUD was confirmed working → `git bisect` with a 30-second in-level check; static-trace the HUD draw conditions (crosshair show/hide flag at `bondview.h:558` is the cheapest single suspect).

**Status:** **OPEN — parked per user instruction: suspected debug-launch artifact; do not raise until seen in a regular release-targeted build. Not yet investigated.**

**Current status (2026-09-29 bookkeeping):** the Status line above is historical; the authoritative verdict is the §F row — CLOSED (2026-09-26, user: HUD is fine now). Earlier: s — regression reported 2026-09-16 during v0.3.0 work (build ce8427cf); user says "don't fix, just for context"; not yet investigated — full `## D298` entry at file tail
