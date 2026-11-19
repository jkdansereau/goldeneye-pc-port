# D951

<!-- from the old section-F/H table row -->
| D951 | **v0.6.0 release gate: Windows golden sweep flags are timing noise in the test, not rendering changes** — full `## D951` entry at file tail | OPEN (test-side; 2026-10-13) |

## D951: v0.6.0 Windows golden sweep flags are test timing, not rendering (2026-10-13)

**Context.** Release-gate runs of `tools_pc/verify.sh sweep` on the v0.6.0 release code (Windows, RTX 5090, 120 Hz display).

**Observed.**
- Default ini (`FpsCap = -1`, Auto = 120 on this display): 8 of 21 levels REGRESSION (runway, surface1, silo, frigate, streets, depot, jungle, cradle; 1.0-9.3% of pixels over tolerance). The goldens were captured at 60 fps and the run did not pin the cap, so the capture frames are not the golden frames.
- `VERIFY_INI_EXTRA='[Video]
FpsCap = 60'`: 20/21 PASS; statue REGRESSION (4.66%, frame 900).
- Statue alone at 60, same binary (exe not rebuilt), five runs: REGRESSION (worst_cell 67.1), then PASS 0.3775, 0.3775, 0.45, 0.3775.

**Reading.** Same binary and inputs give near-identical passing scores 4 of 5 times and one far outlier: the capture is timing-sensitive (D117: the intro flyby is wall-clock paced, so a slow first run lands different frames), not a rendering change. The 2026-10-12 release-candidate gate saw the same with streets (HANDOFF: "flips at tolerance with and without the changes").

**Owed (test side only).** (1) verify.sh should pin `FpsCap = 60` in its ini itself, since the goldens are 60 fps captures; (2) a flagged level should be re-run once before it counts as a regression, or the first run should be discarded as warm-up. No game-code change.

**Status.** OPEN, test-side.
