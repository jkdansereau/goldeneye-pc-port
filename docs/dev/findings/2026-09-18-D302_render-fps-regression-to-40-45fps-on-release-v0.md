# D302

<!-- from the old section-F/H table row -->
| D302 | **Render FPS regression to ~40-45fps on release/v0.3.0 (user QA on the local v0.3.0 pre-release build, 2026-09-18) — a reintroduction of D250's own uncached-`getenv()`-in-the-hot-path bug class.** — full `## D302` entry at file tail | FIXED — 4 uncached `getenv()` calls added by D236 passes 10/12/14/16 directly inside `gfx_sp_tri1` (`port/fast3d/gfx_pc.cpp`, per-triangle) cached with the same `static int x = -1` pattern D250 established elsewhere in the file. Verified via `GE_D193=1 -level_33` (Dam): rock-steady `render=59-61` post-fix, matching D250's own post-fix baseline exactly (pre-fix D250 pinned this level at 45-50fps). Not yet re-confirmed by the user's own hands-on playtest. |

## D302 — Render FPS regression to ~40-45fps on release/v0.3.0 (user QA on the local v0.3.0 pre-release build, 2026-09-18).

**User reported drops to 40-45fps on a self-described high-end PC on a local v0.3.0 pre-release build**, after the port had previously held a rock-steady 60-61fps end-to-end (D250, fully resolved). Same symptom class and roughly the same fps range as D250's original regression.

**Root cause: a reintroduction of exactly D250's own bug class, this time in the still-open, uncommitted D236 (Surface1 tree-card) investigation.** Passes 10/12/14/16 of D236 added four `getenv()` calls directly inside `gfx_sp_tri1` (`port/fast3d/gfx_pc.cpp`) — `GE_D236RM`, `GE_D236ORDER`, `GE_D236ALPHA`, `GE_D236ZFIX` — every one of them called **uncached, unconditionally, on every single triangle draw**, exactly the anti-pattern D250 already root-caused and fixed everywhere else in this file (`getenv()`/`_getenv_helper_nolock` is a locked, locale-aware linear scan in the MSVC CRT; at triangle-call frequency it dominates the render thread). These four sites were added across several D236 sessions after D250 landed and were missed by D250's own sweep because they didn't exist yet at the time.

**Fix:** cached all four behind the same `static int x = -1; if (x < 0) x = getenv(...) != NULL;` pattern already used everywhere else in this file (e.g. `s_d252`, `ge_dtex`, `ge_d172_a`) — mechanical, zero behavior change when the env var is unset, matches D250's own established fix idiom exactly. `GE_D236RAW` (`src/game/bg.c`, room-load time, not per-frame) and `GE_WRAPFIX` (`gfx_pc.cpp`, a one-shot settings-setter, not per-frame) were checked and are not contributors — left as-is.

**Verified:** rebuilt clean; `GE_D193=1 -level_33` (Dam, the same level D250's own verification used) idles at a rock-steady `render=59-61 sim=60-67` with zero clamp hits, matching D250's post-fix baseline exactly (pre-fix D250 pinned this same level at 45-50fps).

**Process note for future D236 sessions:** any further temp diagnostic added to a per-triangle/per-vertex hot path in `gfx_pc.cpp` must use the cached-getenv pattern from the first line, not a raw `getenv()` call — this is now the second time an uncached hot-path `getenv()` has shipped in this file. Worth a lint/grep sweep (`grep -n 'if (getenv(' port/fast3d/*.cpp`) before any future release cut.

**Status:** FIXED, verified via `GE_D193` idle telemetry. Not yet re-confirmed by the user's own hands-on playtest of the updated build.
