# D530

<!-- from the old section-F/H table row -->
| D530 | **The box (X220) linux golden re-round landed — first cross-machine test of the four D117 flaky levels** — Golden gate (P7, box); D529 never-do 3/6 follow-up; full `## D530` entry at file tail | AMENDED BY D531 (2026-10-05) — the "canonical eep" provenance claim was wrong: the re-round ran under the box-LOCAL save (the `capture_p7.sh` per-level re-pin bug); the D531 re-capture under the true canonical eep supersedes this set |

## D530: The box (X220) linux golden re-round landed — first cross-machine test of the four D117 flaky levels (golden gate; D529 never-do 3/6 follow-up) — **AMENDED BY D531 (2026-10-05): the 10-05 re-round ran under the box-LOCAL save, not the canonical one (the capture_p7.sh bug); the true canonical re-capture is D531**

**2026-10-05 (local session).** The box re-round D529 owed (never-do 3: the linux set was the D525 *cleared*-eep round and no linux number could be cited until it was re-rounded under `tools_pc/golden/ge007.eep`) ran on X220 on 2026-10-05 (tree `442eb2ff`, worktree `~/gh-fullhist-wt-p7`, log `~/p7-reround.log` hit `P7 re-round DONE` 12:53 EDT). **21/21 levels rc=0** (the box self-gate is crash-detect only — the pixel probe fails closed on the box, D244). This session pulled the 63 PNGs (21 levels × 3 frames) and committed them into `tools_pc/golden/<level>/linux/`, replacing the D525 set: 51/63 differ from the D525 round; the 12/63 that are byte-identical are save-independent scenes — including **frigate's first stem (001000)**, a direct D117 stability signal. **AMENDED BY D531: that re-round ran under the box-local save (the `capture_p7.sh` bug), so the "canonical eep" provenance claim — and the first-stem table below, as canonical-save evidence — does not hold; the D531 re-capture supersedes this set.**

The `fail` rows in the box report are the expected cross-eep diffs against the *old* in-tree set, not regressions: structural tier is all `ok` (phash ≤ 3, cradle@40 the documented exception; Cuba 26.4% inside its documented 23.7–36.3% structural-only range).

**D117 flaky-four first-stem check (D529 never-do 6 — the first cross-machine test):**

| level (first stem) | worst_cell | phash |
|---|---|---|
| dam@1500 | 1.70375 | 0 |
| frigate@1000 | — (byte-identical to the D525 round) | 0 |
| streets@1000 | 1.775625 | 0 |
| jungle@900 | 5.115625 | 3 |

All four sit in the stable-levels' range (e.g. silo 0.41–0.84% over, worst_cell 0.41–0.42 is the quietest; archives 1.51–2.31 is noisier). **None went red → the never-do 6 decision closes with NO recipe change** (no first-stem cell excluded or widened on the linux side).

**Unblocked (D529 never-do 3):** the README/index v0.5.0 line "re-based on both platforms (21 levels, 63 frames each)" now stands on a canonical-eep linux set (D531's re-capture; the D530 set it first pointed at was the box-local-save round — see D531). **Fresh box sweep (crash-detect):** with the new goldens staged on the box, `verify.sh sweep` (2026-10-05) is **21/21 PASS** — but that run was crash-detect only (GE_PCDUMP reads black on the box's llvmpipe/WSLg, the D244 note), so it is a stability re-check, not a pixel datum. **AMENDED BY D531:** the "black frames" were **not** simply the no-DISPLAY llvmpipe path — the box's WSLg display is session-state-dependent: its 10-05 session was GPU-backed (the re-round's p7_report pixel table proves real frames), while today's reads back black even with `DISPLAY=:0` (software fallback; `glxinfo -B` reports no usable renderer). D531's pixel-gated attempt (21/21 crash-detect, `~/p7-verify-pixel.log`) is valid as a stability re-check; the pixel datum is owed for a GPU-display window.
