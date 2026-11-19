# D316

<!-- from the old section-F/H table row -->
| D316 | **Mouse-clicking some menu items glitches and lands on the item below instead (user report, v0.3.0 playtest build, 2026-09-18).** — full `## D316` entry at file tail | FIXED — root-caused to two compounding bugs (naive mouse->logical mapping ignoring the safe-area crop; asymmetric row hit-test band) in `port/src/optionsoverlay.c` + `port/fast3d/gfx_pc.cpp`. Build-verified (golden diff PASS, worst_cell=0); interactive click verification owed. |

## D316 — Mouse-clicking some menu items glitches and lands on the item below instead (user report, v0.3.0 playtest build, 2026-09-18).

**Reported:** "On some menu items when you try to click with mouse, it glitches and forces you onto the item below." **Repro detail (user, 2026-09-18):** possible on any item, but specifically noticed on the **crop overscan bars** setting; not currently reproducible on demand — intermittent.

**Root-caused and FIXED (2026-09-18), two compounding port-side bugs, both in `port/src/optionsoverlay.c`:**

1. **Mouse→logical mapping ignored the safe-area crop.** `optionsOverlayHandleInput()` mapped a click with a naive `ox = mx * viGetX() / ww` / `oy = my * viGetY() / wh` — a linear scale against the raw SDL window size. But the overlay's own 2D content is drawn into whatever on-window rect the safe-area crop (`Video.SafeAreaCrop`, default ON) currently maps the logical `viGetX() x viGetY()` canvas to, via `gfx_adjust_viewport_or_scissor` (`port/fast3d/gfx_pc.cpp`) — and that rect is inset below the full window whenever the last-set gameplay viewport was inset (NTSC "Full" removes ~8% top+bottom; this is why "the crop overscan bars setting" specifically was the noticed repro — it sits low in the list, where the growing offset between the naive scale and the real inset rect is largest). Fix: added `gfx_get_ui_screen_rect()` (`gfx_pc.cpp`, declared in `gfx_api.h`) which *reuses* `gfx_adjust_viewport_or_scissor` itself (not a hand-derived inverse) against the full VI canvas rect to get the real on-window rect, then `optionsOverlayHandleInput()` inverts the click against that rect instead of the raw window size.
2. **Asymmetric row hit-test band.** `overlayRowAtY()`'s band was `[OV_ROW_Y(p)-3, OV_ROW_Y(p)+12)` on 15px rows (`OV_LINE`) — 3 units above the row anchor but 12 below, a systematic downward bias independent of bug 1. Fixed to `[OV_ROW_Y(p) - OV_LINE/2, OV_ROW_Y(p) + OV_LINE/2)`, centered on the row anchor.

Verified: build clean (`build-pc`, MSYS2 MINGW64/ninja); `tools_pc/verify.sh bunker1` — PASS, `worst_cell=0` (pixel-identical to golden, confirming the new `gfx_get_ui_screen_rect()` accessor is a true no-op for normal 3D rendering, as intended — it only reads existing state). Interactive click-accuracy verification (varying window size/aspect, `SafeAreaCrop` on and off) still owed — headless/non-interactive here, mouse-driven UI needs a live session.

**Status:** FIXED (pending interactive click verification). Cross-ref: D304 (fixed the same file's out-of-scan-range scroll-boundary bug, a separate root cause from both bugs above — see the comment trail in `overlayRowAtY()`/`overlayUpdateScroll()`).
