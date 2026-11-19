# D624

<!-- from the old section-F/H table row -->
| D624 | **B4 content scorer built + tuned: the structural RGB-NCC scorer (dither-blurred, palette-invariant) beats CLIP on this pack — rank-1 56 % vs 33 % on the 257 ground-truth pairs; auto-accept tier at margin ≥ 0.10 is 100 % precise on GT; pipeline produces the human queue (189 auto / 1341 vision-pending / 515 out-of-scope)** — ROADMAP #100 Phase B4 (`docs/dev/TEXPACK-PLAN.md`); tool `tools_pc/texpack/content_scorer.py` (rank/tune/verdicts/apply); full `## D624` entry at file tail | BUILT 2026-10-07 — 5-stage pipeline (geometric prefilter → content rank → GT-tuned auto-accept → vision judge → human queue) per the 2026-10-07 decision (commits 512e6d57, 95d38637). Scorers measured on 255 in-scope GT pairs: CLIP ViT-B/32 33 % rank-1 (ViT-L/14 no better); structural 3-channel NCC + 1/2× LANCZOS dither-blur 56 % rank-1 / 67 % rank-≤5 → adopted (CLIP kept as `--scorer clip` A/B). Margin-vs-precision on GT: 100 % down to t≈0.05 (97/255 @ 0.10, 61/255 @ 0.20); recommended t=0.10. Verdicts @ 0.10: 189 auto (47 geometric n≤3 + 142 margin), 1341 vision-pending (contact sheets + answers.csv template under `scratch/join_review/`), 515 out-of-scope; `apply` merges vision answers → `queue.csv` + `queue/` sheets (verified). Stop point honoured: queue produced, Phase C not started. Evidence: `scratch/join_ranked.csv`, `scratch/join_verdicts.csv`, `scratch/join_review/`, caches `scratch/{clip_tex,clip_pack}_cache.npz` + `scratch/ncc_van_cache.pkl` |

## D624: B4 content scorer built + tuned — the structural RGB-NCC scorer (dither-blurred, palette-invariant) beats CLIP on this pack: rank-1 56 % vs 33 % on the 257 ground-truth pairs; the GT-tuned auto-accept tier (margin ≥ 0.10) is 100 % precise on GT; the pipeline produces the human queue (189 auto / 1341 vision-pending / 515 out-of-scope)

ROADMAP #100 Phase B4 (`docs/dev/TEXPACK-PLAN.md`), the decision committed 2026-10-07 (512e6d57 + 95d38637): replace the B3 geometric-only gate with a 5-stage AI-scorer pipeline — geometric prefilter (D623's `join_ambiguity.csv`) → content rank → GT-tuned auto-accept → vision judge (agent contact sheets) → human queue for disagreements. Tool: `tools_pc/texpack/content_scorer.py` (subcommands `rank` / `tune` / `verdicts` / `apply`). Stop point honoured: the human-queue artifacts are produced; Phase C was not started.

**Scorer A/B (measured on the 255 ground-truth pairs in candidate scope — 256 known pairs, D620; 1 pair's true texnum has no geometric candidates).** The GT set is the right tuning instrument: these are *known-correct* pack-PNG → vanilla-texnum pairs, so rank-precision and the auto-accept margin are measurable without a human.

| scorer | rank-1 / 255 | rank-≤5 / 255 |
|---|---|---|
| CLIP ViT-B/32 (native pack scale) | 70 (27 %) | 136 (53 %) |
| CLIP ViT-B/32 (pack → vanilla-dims LANCZOS) | 85 (33 %) | 153 (60 %) |
| CLIP ViT-L/14 (scale-matched) | 84 (33 %) | 141 (55%) |
| grey NCC, dither-blurred | 138 (54 %) | 171 (67 %) |
| **RGB 3-ch NCC, dither-blurred (adopted)** | **143 (56 %)** | **170 (67 %)** |

The **structural scorer wins decisively** and the result is interpretable: the pack is HD *redrawn* art with regenerated palettes, while the vanilla side is 15-colour dithered N64 art — a large perceptual domain gap that hurts a learned semantic embedding (CLIP), whereas a downscale-to-vanilla-dims + mean-centred per-channel normalised cross-correlation with a 1/2× LANCZOS dither-blur on both sides is invariant to palette regeneration (the D620 hue problem) and only needs matching *structure*. CLIP is retained as `--scorer clip` for A/B, not deleted. Both sides' dither-blur is the load-bearing step (grey NCC without blur: 51 % rank-1; with blur: 54–56 %).

**Auto-accept margin (tuned on the same 255 GT pairs, structural scorer).** margin = score(true) − score(runner-up):

| threshold | auto tier | precision on GT |
|---|---|---|
| 0.00 | 163 | 89.0 % |
| 0.02 | 132 | 100 % |
| 0.10 | 97 | 100 % |
| 0.20 | 61 | 100 % |
| 0.30 | 40 | 100 % |
| 0.40 | 23 | 100 % |

(margin percentiles: p10 −0.335, p50 0.053, p90 0.369). The 100 %-precision region extends down to t≈0.02, so a 0.20 threshold (24 % of GT) is over-conservative. **Recommended t = 0.10** — 100 % GT precise, 38 % GT coverage; rows above it auto-accept, the rest route to the vision judge. Note the precision numbers are *on the 255 GT pairs* (the known-correct set); the GT pairs skew toward "easy" matches (they were CRC-joined), so the non-GT tail will be noisier — the vision-judge stage exists exactly for that, and a vision "none" on an auto row is the escape hatch (the auto tier is a *routing* decision, not a proof).

**Pipeline output (`verdicts --threshold 0.10 --vision`), 2045 pack PNGs:** 189 auto (47 geometric n_cand≤3 + 142 margin≥0.10), **1341 vision-pending** (one contact sheet each — pack + top-5 structural candidates, labelled with NCC sims — under `scratch/join_review/`, plus the `answers.csv` template), 515 out-of-scope (the D623 zero-candidate tier, dominated by the in-exe `UI` fonts). 1341 splits as 17 n_cand≤15 (top-5 ≈ the whole candidate set, most vision-resolvable) vs 527 with >40 candidates (top-5 is a weak hint there; most will answer "none" and land in the human queue). `apply` merges filled answers: an on-top-5 pick = vision verdict; a "none"/off-list pick = **human queue** (`scratch/join_review/queue.csv` + `queue/` sheets for the maintainer) — verified end-to-end on a 3-row sample.

**Tooling notes.** Vanilla renders: the B3/D616 decode path (zlib-CI + non-CI, N64 alt-row swap for lod>0, 5551 palette) verified against the pack's own CRC-joined PNGs (e.g. texnum 294). Caches: `scratch/clip_{tex,pack}_cache.npz` (CLIP, optional) and `scratch/ncc_van_cache.pkl` (vanilla NCC vectors, ~0.3 s to load; a full `rank` pass is ~90 s, ~20 s/variant of GT tuning with cached decodes). One bug caught in testing: the struct `rank` writer initially unpacked `(texnum, score)` pairs as `(score, texnum)` — every `margin` came out as a texnum-difference (~−425), silently zeroing the margin auto-tier (a wrong "960 auto" count); caught because the CSV columns were eyeballed against a known row before verdicts ran.

**Status: B4 BUILT + TUNED, stop point reached.** The human queue exists (1341 contact sheets + template); judging the 1341 vision-pending sheets (agent or maintainer, batchable) and running `apply` to finalise `queue.csv` is the next step, then B5 review / Phase C (out of scope here, per the drop-in prompt). No port C, no game-code changes.


**Addendum 2026-10-10 (auto-accept hygiene, `content_scorer.py verdicts`):**
a row is never auto-accepted when (a) its folder starts with `Hacks/` or
contains `Fan Translations` (now `out-of-scope`), (b) the pack image's
class disagrees with the top candidate's live GBI format class (`ciByRGBA`
-> CI only, `_all` -> non-CI only; unknown candidate -> disagree), (c) the
top-two margin is below the threshold (the old "<= 3 candidates" shortcut
now applies only to a single candidate), or (d) the same texnum would be
auto-assigned to more than one pack image in the same folder (all such rows
demote to `vision-pending`). Pure helpers (`auto_eligible`,
`demote_duplicates`, `class_agrees`) checked offline on a synthetic list;
the `apply` subcommand and scratch data were not touched. The 189-auto
figure above predates these rules and will drop on the next `verdicts` run.
