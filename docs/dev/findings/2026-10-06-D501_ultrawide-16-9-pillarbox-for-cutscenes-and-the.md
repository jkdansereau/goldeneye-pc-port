# D501

<!-- from the old section-F/H table row -->
| D501 | **Ultrawide 16:9 pillarbox for cutscenes and the watch: built, then reverted (original framing, not breakage)** — full `## D501` entry at file tail | CLOSED (reverted 31ade91f) |

## D501 — Ultrawide: 16:9 pillarbox for cutscenes and the watch, REVERTED (2026-10-03)

**Report:** at > 16:9, cutscenes show past their framing (guards at the edges) and the watch raise/lower shows the end of Bond's arm model for a frame or two.
**Analysis (Qwen Q5, lead-verified):** native widescreen projects at the window aspect (`bondview2.c` ~8157 `portNativeAspect`, `fr.c:735`); scripted cameras and the watch zoom were authored for 4:3/16:9; the watch face has its own fixed-aspect projection (`bondview2.c` ~8671/8673).
**Change, then revert:** commit 6f1f49bd pillarboxed > 16:9 windows to 16:9 during scripted cameras and the watch (`Video.UltrawideCinematic`); reverted in 31ade91f. Maintainer: the switching is clunky; these are the original game's framing shown at 21:9, not breakage. A toggle can come later if wanted.
**Related:** ultrawide performance checked by the maintainer (Aztec, Cradle, 21:9 and wider): good; `GE_PERFSTAT` showed steady 60 fps at 2644x875, FOV 150.
**Status:** CLOSED (reverted).
