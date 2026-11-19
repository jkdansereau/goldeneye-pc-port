# D319

<!-- from the old section-F/H table row -->
| D319 | **Frame cap slider was mostly a dead zone; replaced with a plain 30/60 toggle (user ask, 2026-09-18).** — full `## D319` entry at file tail | FIXED — new `ROW_FPSCAP` cycle (30/60 only) in `port/src/optionsoverlay.c`; default changed to 60 in `port/src/video.c`. Root cause was D186 (sim tick ceiling + low-cap guard); this just fixes the menu's exposure of it. Build-verified; interactive confirmation owed. |

## D319 — Frame cap slider was mostly a dead zone; replaced with a plain 30/60 toggle (user ask, 2026-09-18).

**Reported:** user noticed `Video.FpsCap` "does not work below 30fps, it also does not go above 60fps" and asked whether that's a toggle-vs-refactor question.

**Not a bug — the slider just exposed a huge dead zone.** Per D186, the port's own frame-pacing wait runs inline on the game's scheduler thread, so:
- Below 30, `video.c` already force-uncaps the value (D186 guard) — the sim would otherwise be throttled, not just presentation.
- Above 60 (50 PAL), the sim's own tick pacemaker (`port/src/libultra.c`, hardcoded to the console's native VI rate) never produces a new frame faster than that anyway, so any slider value there was a silent no-op.

Only 30, 60, and 0 (uncapped: skip the port's frame-pacing wait entirely) ever did anything. Uncapped was considered as a third UI state but dropped after discussion: with `Video.VSync` on (the default) it's indistinguishable from 60, and with VSync off it just burns GPU re-presenting the same simulated frame — a confusing option with no real player benefit.

**Fix:** `port/src/optionsoverlay.c` — new `ROW_FPSCAP` row kind (mirrors the existing `ROW_MSAA` fixed-cycle pattern) cycling `Video.FpsCap` between exactly 30 and 60; the free 0-360 `ROW_SLIDER` and its fill-bar are gone for this row. `port/src/video.c` — default changed from `0` (uncapped) to `60` so a fresh install shows a real, reachable menu value instead of a state the toggle can no longer select. The `0`/uncapped value still works at the config level (old inis, hand-edits) — `valueText` still labels it "Uncapped" if seen — it's just not reachable from the menu any more.

Verified: build clean (MSYS2 MINGW64/ninja); `tools_pc/verify.sh bunker1` — PASS, `worst_cell=0`. Interactive check (toggle Frame cap in the F10 menu, confirm it reads 30/60 and only those two) still owed.

**Status:** FIXED (pending interactive confirmation). Cross-ref: D186 (root cause of both dead zones, this just fixes the UI's exposure of it).
