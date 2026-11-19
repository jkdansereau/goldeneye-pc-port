# D473

<!-- from the old section-F/H table row -->
| D473 | **Uncached `getenv("GE_D75V")` on every G_VTX: the probe's `d75v_lo == -1` meant both "not read yet" and "disabled", so with the env unset `gfx_sp_vertex` re-ran getenv on every vertex batch (~55-59% of render-thread CPU by sampling; D302/D250 class)** — full `## D473` entry at file tail | FIXED 2026-10-01 (local); Statue display-list CPU about -55% in steady windows |

## D473 — Uncached `getenv("GE_D75V")` per G_VTX (low-end perf, #92 / D339)

**Found by** the 2026-10-01 CPU profile (in-process all-thread sampler `GE_PERFSAMP`, branch `perf/profile-hotspots`, local note `docs/dev/notes/PERF-HOTSPOTS.md`): ~55-59% of render-thread samples landed in `getenv` called from `gfx_sp_vertex`. The D75 vertex probe (`port/fast3d/gfx_pc.cpp`, `GE_D75V="lo-hi"`) cached its range in `d75v_lo`, but used `-1` both as "uncached" and as "disabled", so with the env unset (always, for players) the `if (d75v_lo < 0)` re-ran `getenv` on every G_VTX (170-520 per frame). Same class as D302/D250 (porting-notes D302); the registry wrongly described it as a cached check.

**Fix:** a separate `d75v_init` flag; the probe's behaviour when set is unchanged, so output is identical. **Measured** (`GE_PERFSTAT`, Statue `-level_22`, maintainer settings 1600x900 MSAA 8x DD 400, same scripted sweep, RTX 5090): steady windows with matching triangle counts, display-list time 0.99 -> 0.46 ms and 1.31 -> 0.55 ms per frame; total `run` 5.6 -> 1.8-2.0 ms. Profile context: whole-process CPU is only ~1.5-2 ms/frame on this machine after the fix, so on the N3060 / HD 400 (#92) the remaining bottleneck is probably GPU/driver-side; next step there is a `GE_PERFSTAT` run on that hardware (gpu= vs run= vs interval=). Further CPU proposals (texture-bind lookups, redundant state changes, debug hooks per triangle, combiner lookup) are ranked in the local note.
