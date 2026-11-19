# D186

<!-- from the old section-F/H table row -->
| D186 | **`Video.FpsCap` throttles the sim, not just presentation (M-39 diag, M-42 clamp).** The frame-pacing `sysSleep`+busy-wait in `sync_framerate_with_timer` (`port/fast3d/gfx_sdl2.cpp`) runs inline on the scheduler thread (`osSpTaskStartGo`→`gfx_run`→`swap_buffers_begin`), so a low cap blocks VI-retrace delivery and drags every game thread down to the cap rate (no N64 analogue — RSP/RDP are separate silicon). **Landed:** `0 < FpsCap < 30` → treated as `0` (uncapped) + warning, at both `videoInit` (normalises a bad `ge007.ini` for the next `configSave`) and `gfx_sdl_set_target_fps` (covers the F10-overlay live path); caps ≥ 30 unchanged. Verified: default `FpsCap=0` framediff 3/3 golden-identical; `FpsCap=10` now runs at the uncapped rate; `FpsCap=60` still paces. **Owed:** move pacing off the scheduler thread so any cap only drops presented frames. See §F "D186" + porting-notes.md §E. | **CLOSED (2026-09-28 bookkeeping: the user-visible symptom is guarded — caps below 30 are forced to uncapped at load and on set, the menus expose only 30/60, and v0.4.0 holds 60 fps. Moving frame pacing off the scheduler thread and an "original timing" mode are roadmap feature work, not an open bug).** Earlier: OPEN (2026-09-26, user direction: target an "original" framerate/timing mode, with 60 fps + options on top; pacing must stop throttling the sim). Earlier: CLAMP LANDED (`port/fast3d/gfx_sdl2.cpp`, `port/src/video.c`); real fix owed |

## D186 — `Video.FpsCap` throttles the whole sim, not just presentation (M-39 diagnosed, M-42 clamp landed)

**Symptom (M-39, user report):** window title read "GoldenEye 007 - 1 fps";
the game had become a slideshow. Root cause was `Video.FpsCap = 10` in the
(gitignored) `data/ge007.ini` `[Video]` section — almost certainly dinged down
via the F10 options overlay (D184) during testing; ESC-close calls
`configSave()` so it persisted.

**Mechanism.** `gfx_set_target_fps(cfgFpsCap)` feeds `sync_framerate_with_timer()`
in `port/fast3d/gfx_sdl2.cpp`, which does `sysSleep(left)` + a `sysCpuRelax()`
busy-wait. That wait runs **inline on the game's scheduler thread**:
`src/sched.c __scExec` → `osSpTaskStartGo` (`port/src/libultra.c:1119`) →
`gfx_run` → `gfx_sdl_swap_buffers_begin` → `sync_framerate_with_timer`, and only
*after* it returns does `osSpTaskStartGo` post `OS_EVENT_SP`/`OS_EVENT_DP`. While
that thread is parked in the pacing wait it is not delivering VI-retrace events
either, so every game thread blocked on `osRecvMesg(retraceQ)` stalls with it —
the sim is dragged down to the render-cap rate. On console the RSP/RDP are
separate silicon and VI retrace is unconditional, so a "render cap" cannot slow
the CPU; there is no N64 analogue. Verified M-39 (`-level_09`, fixed 35 s
window): `FpsCap=10` → 5 frames; `FpsCap=30` → 5; `FpsCap=60` → frame ~300;
`FpsCap=0` → frame 900+.

**Landed (M-42, clamp only — `port/fast3d/gfx_sdl2.cpp` + `port/src/video.c`):**
any `0 < FpsCap < 30` is treated as `0` (uncapped) with a one-line warning, at
both the config-load site (`videoInit`, so a bad ini is normalised and persists
sane on the next `configSave()`) and the fast3d chokepoint (`gfx_sdl_set_target_fps`,
which also covers the live path from the F10 overlay). Caps ≥ 30 are unchanged.
Verified: `-level_09` framediff 3/3 within threshold at the default `FpsCap=0`
(golden-identical); with `FpsCap=10` the sim now runs to VI post ~1740 in a
30 s window (≈ the `FpsCap=0` rate) instead of ~300; `FpsCap=60` still paces
(~56/s, no warning).

**Still OWED (the real fix):** move the frame-pacing wait off the scheduler
thread so *any* cap only drops/duplicates presented frames without blocking
retrace delivery (e.g. pace in the host event-pump thread, or gate the swap
without sleeping the RSP path). Until then a low cap is refused rather than
supported. Same timing-compensation family as D117 / D134 / D155.
porting-notes.md §E.

---
