# D175

<!-- from the old section-F/H table row -->
| D175 | **In-game stutter / brief hang during normal play** — full `## D175` entry at file tail | CLOSED (v0.4.0 campaign sign-off 2026-09-28, user: no stutter over the full campaign -- fixed). Earlier: OPEN (user, 2026-09-26: still happens occasionally; keep). Earlier: OPEN — observed, not investigated. **Steam Deck (Linux, 2026-09-27/28): occasional frame-timing hitches at low CPU/GPU (~15–30 %) — a frame-pacing/present profile, not a throughput bottleneck (see entry).** |

## D175 — In-game stutter / brief hang during normal play

**In-game stutter / brief hang during normal play** (user QA report): opening a door on Runway (`-level_35`, mission 3) and also observed on Surface. Self-recovered; no backtrace captured. Likely one of the known transient-hang classes (D155 catch-up spiral / D156 anim NaN loop / D134 task-done event / D147-D152 audio-lock steal) or a benign new-room texture-import frame spike on door open. See `GRAPHICS-BACKLOG.md` D175 for the gdb pattern-match sheet.

**Status (from §F table):** OPEN — observed, not investigated

**Steam Deck (Linux / SteamOS, v0.4.0-pre, 2026-09-27/28 — user playtest):** the same intermittent frame-timing hitches reproduce on the Deck, but with a *different* utilisation profile than #92/D339: both CPU and GPU sit at only ~15–30 % (nowhere near a throughput bottleneck) yet the user perceives occasional "weird frame timing." Low-utilisation hitches like this are a **frame-pacing / present** problem, not a throughput problem. Candidates: the D186 `Video.FpsCap` throttle (the D186 row notes it throttles the *sim*, not just presentation), the D248/D250 pacing path (fixed for the 30-vs-60 case), or the Deck's Wayland/GBM + AMD present/vsync behaviour. Next (if pursued): a `GE_PERFSTAT=1` run plus a per-frame `dl`/`swap-pace` time histogram on the Deck, to see whether the hitches are a pacing-wait spike or a present stall. **Log-only this pass — not investigated yet.**
