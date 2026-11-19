# D296

<!-- from the old section-F/H table row -->
| D296 | **Control Center, 00 Agent: noticeable stutter, user's own read is "maybe from the drone guns" (M-146, same D295 investigation session).** — full `## D296` entry at file tail | CLOSED (2026-09-26, user-approved: stutter seen only under gdb (debug.ps1); D302, fixed two days later, removed a per-triangle uncached getenv costing ~15 fps; reopen if seen on a normal build). Earlier: OPEN — single live report, un-triaged; observed only under a gdb-attached debug-harness launch (`tools_pc/debug.ps1`), not the packaged release artifact — user flagged that harness overhead has caused false stutter before. Needs a plain-exe / real-release repro check before any perf tracing.… |

## D296 - Control Center, 00 Agent: noticeable stutter, user's own read is "maybe from the drone guns" (M-146, 2026-09-16, same D295 investigation session).

**Reported while live-playing Control Center on 00 Agent (`-level_23 -hard2`) to investigate D295** (that crash did not reproduce; this is a separate, secondary observation from the same playthrough). User: "I am getting pretty good stuttering maybe from the drone guns on 00 agent on that level."

**Caveat that must be resolved before trusting this as a real perf bug — flagged proactively by the user, not yet checked by us:** this playthrough ran under `tools_pc/debug.ps1`, i.e. **gdb-attached**, not the packaged release artifact. The user has previously seen debug-harness overhead produce stutter that doesn't exist in a genuine release build. **Not yet investigated** whether:
- (a) the stutter reproduces on a plain `build-pc/ge007.x86_64.exe` launch (no gdb attached, no `-NoBuild`/dev-tree quirks) — this is the first thing to rule out, cheap (~2 min repro attempt), before any perf tracing;
- (b) it reproduces on the actual packaged/zipped release artifact a real user would download (further rules out any local-tree-only difference, e.g. debug symbols, unstripped binary, dev `data/` sidecar state);
- (c) if it survives both of those, whether it is specifically tied to drone-gun turret enemies (their fire pattern, tracking, or projectile spawn — worth an env-gated per-frame timing probe around whatever `ai_*`/`gunfire.c` path drone turrets use) or just general Control Center scene load on 00 Agent (higher enemy density than Agent/Secret Agent difficulty).

**Status:** OPEN — single live report, un-triaged, un-reproduced outside the debug harness. **Do not start perf-tracing `port/fast3d/` or AI code over this until step (a)/(b) above rule out harness overhead** — that's the cheap, disqualifying check and it hasn't been run yet.

**Current status (2026-09-29 bookkeeping):** the Status line above is historical; the authoritative verdict is the §F row — CLOSED (2026-09-26, user-approved: stutter seen only under gdb (debug.ps1); D302, fixed two days later, removed a per-triangle uncached getenv costing ~15 fps; reopen if seen on a normal build). Earlier: OPEN — single live report, un-triaged; observed only und…
