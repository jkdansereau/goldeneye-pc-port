# D314

<!-- from the old section-F/H table row -->
| D314 | **F10 menu items flicker randomly/intermittently (reported on the main-menu context, user report, v0.3.0 playtest build, 2026-09-18).** — full `## D314` entry at file tail | FIXED (user playtest 2026-09-26) — M-200 wheel-queue race fix + D287 window-thread cursor calls. Earlier: confirmed still present post-D316 fix (2026-09-18). Diagnostic probe (`GE_D314=1`) landed in `optionsOverlayHandleInput()` if revisited. |

## D314 — F10 menu items flicker randomly/intermittently (reported on the main-menu context, user report, v0.3.0 playtest build, 2026-09-18).

**Reported:** "F10 menu glitches on main menu, the menu items flicker randomly/intermittently." **Repro detail (user, 2026-09-18):** it happens when F10 opens the settings menu while on the **file-select / level-select screen**, and also when the settings menu is open in-game and the user is then **returned to file-select/level-select** (i.e. the overlay outlives a context switch). The playtest build is the current-tip exe (includes D310/D311 fixes). So this is specifically an overlay-vs-mission-select-screen interaction, not the plain title screen.

**Not investigated.** Pointers only:
- **Prime suspect by recency: D304 (fixed TODAY) changed exactly this overlay's code** — collapsed the mouse-sensitivity rows to a single menu row (changing the total item count) and fixed `overlayUpdateScroll()`'s unconditional bottom-anchor. The in-game F10 playtest confirmed the *collapsed sensitivity section* worked, but flicker is a row-layout symptom and the main-menu context was not tested. If row positions are re-derived per frame from a list-height/anchor that assumes the old item count, intermittent flicker + mis-landing clicks (D316) would both fall out of the same bug.
- D251 (bottom-row duplication, fixed) and D304's click-jump fix are the sibling bugs in this exact code path — check `overlayUpdateScroll()` and the row hit-test/height math for any count assumption that D304's row collapse invalidated in a context with fewer/more items.
- Likely one root cause shared with D316; investigate together.

**Triage update (2026-09-18):** ruled out `viGetX()`/`viGetY()` instability (stable per-screen, set once by `front.c`) and non-determinism in the overlay display list construction; no shared fixed-buffer overflow found. The D316 mouse-mapping/hit-band bugs are fixed (see D316) but do not explain a *flicker* (those were click-accuracy bugs, not a rendering instability), so D314 likely has an independent mechanism — probably front-end-context specific (the text/font layer, or file-select's own mouse handling racing the overlay's input-suspend). Added a cheap, cached, env-gated diagnostic probe (`GE_D314=1`) in `optionsOverlayHandleInput()` (`port/src/optionsoverlay.c`) logging `viGetY()`/`s_visN`/`s_scroll`/`s_sel` once per frame while the overlay is open — diagnosis only, no behavior change when unset. A ~30s file-select repro run with it enabled should show which value oscillates (or prove none do, pointing at the text layer).

**Confirmed still present post-D316 fix (user, 2026-09-18):** the D316 mouse-mapping/hit-band fix did not resolve this — as expected, since D314 is a rendering-instability symptom (flicker), not a click-accuracy one. **User call: not worth chasing down currently** — deprioritized, left OPEN with the probe in place for whenever it's revisited.

**Status:** OPEN — deprioritized by user request (2026-09-18); probe landed, repro run not currently planned.

**2026-09-25 (v0.4.0, M-200 session): threading audit, one race fixed, headless repro clean; plausibly fixed via D287; owed a live check.**
- **Thread map (verified in source):** `optionsOverlayEmit` runs inside `gfx_run`, called from `osSpTaskStartGo` in `__scMain` (scheduler thread). `optionsOverlayHandleInput` runs from `joyPoll` → `osContGetReadData` → `inputComputePad`, *also* synchronously inside `__scMain` (`sched.c:308/321`; see the `osRecvMesg` note in `libultra.c`). So Emit and HandleInput never race. The host thread (SDL event pump in `videoPumpEvents`) calls `optionsOverlayToggle` (F10) and `optionsOverlayScroll` (wheel).
- **Race fixed:** `optionsOverlayScroll` ran `overlayUpdateVisible()` (`s_visN = 0`, then refill in place) and wrote `s_sel` on the host thread. That can race Emit's draw loop on the scheduler thread, which could draw a truncated row list for a frame or clamp the selection against a half-built `s_visN`. It now only does `SDL_AtomicAdd` of the notches; `overlayApplyWheel()` in HandleInput applies them on the scheduler thread (the D287 pattern). Behaviour is unchanged (one row per notch, clamped, headers skipped). This only mattered while the wheel was moving.
- **Pre-D287 cross-thread SDL calls on this exact path:** HandleInput calls `inputSuspendForOverlay()` on every poll while open. Before `d07ed0f2` (D287, 2026-09-25) that called `SDL_SetRelativeMouseMode`/`SDL_ShowCursor` directly from the scheduler thread while the host thread made its own cursor/focus calls. That's the D287 class, and it fits a "flicker" that was seen only with the overlay open. The D314 report (2026-09-18) predates D287, so D287 may already have fixed it.
- **Headless repro, clean:** `GE_STARTMENU=7` (mission select) and `GE_STARTMENU=5` (file select) with `GE_OPTIONSOVERLAY=1`, `GE_PCDUMP="200-800:1"` at 640x480: 600 consecutive frames each, **0 frames** with a text-level (>120 summed RGB delta, >30 px) change against the previous frame (`scratch/d314diff.py`). The post-fix file-select frame 500 is pixel-identical to pre-fix. So without input the overlay draws deterministically; any real flicker needs input (mouse/cursor) or the in-game→file-select transition.
- **User check (next playtest):** open F10 on file-select and mission-select and move the mouse, click rows, wheel-scroll; also open it in-game, then abort to file-select with it still open. If there's still flicker, note whether it's the overlay rows or the game's menu behind it, and whether it happens with the mouse still.

**Status:** OPEN, likely fixed (D287 + wheel race), pending the user check above.

**Current status (2026-09-29 bookkeeping):** the Status line above is historical; the authoritative verdict is the §F row — FIXED (user playtest 2026-09-26) — M-200 wheel-queue race fix + D287 window-thread cursor calls. Earlier: confirmed still present post-D316 fix (2026-09-18). Diagnostic probe (`GE_D314=1`) landed in `optionsOverlayHandleInput()` if revisited.


**2026-09-26 (v0.4.0, M-201): user-confirmed.** F10 on file-select/mission-select with mouse move/click/wheel, and in-level → abort with F10 open: no flicker. Closed by the M-200 wheel-queue fix plus D287 (cursor/relative-mode calls moved to the window thread).

**Status: FIXED (user playtest, 2026-09-26).**
