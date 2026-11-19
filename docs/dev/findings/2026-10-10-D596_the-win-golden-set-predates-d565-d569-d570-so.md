# D596

<!-- from the old section-F/H table row -->
| D596 | **the `win/` golden set predates D565/D569/D570, so `verify.sh` on the `macos` branch (and the main tip) reported a `REGRESSION worst_cell=3.106875` that is a stale reference, not a branch regression — the branch's Windows identity gate was unmeasurable. Recipe-side fix: regenerate the `win/` set from `e0a2d4b9` (pre-D609 tree; `scratch/wt-goldens`), `capture_p7.sh -p win` with LOWERCASE level names (the `golden_dump_for` case table is lowercase — `Dam` silently falls to the default window); `verify.sh sweep` 21/21 PASS.** — full `## D596` entry at file tail | **FIXED 2026-10-08** (macos branch; win goldens regenerated from `e0a2d4b9`, 21/21 PASS; recipe-side only, no game-code change; the linux/deck sets need their own re-capture round before any new cross-platform number is read) |

## D596: the `win/` golden set predates D565/D569/D570 — the `macos` branch's Windows identity gate was unmeasurable; regenerated from `e0a2d4b9` (pre-D609 tree), 21/21 PASS — FIXED 2026-10-08 (macos branch; recipe-side only, no game-code change)

**Symptom.** `tools_pc/golden/<level>/win` was last regenerated at `4b710728`
(the v0.5.0 fold-in), i.e. before the D565 fog-distance floor and the
D569/D570 crosshair changes. `verify.sh` against the stale set reports
`REGRESSION worst_cell=3.106875` (limit 3.0) **from both the `macos` branch
and the main checkout's tip** — a stale reference, not a branch regression
(HANDOFF-MAC-TRACK-C §4). The consequence: the `macos` branch (whose
D607-D612 edits are all identity at `PORT_ADDR_BASE == 0`) had *no working
Windows pixel gate* — a Windows-side regression on that branch would be
invisible.

**Fix (recipe-side only):** regenerate the `win/` set from `e0a2d4b9` (the
D608 commit — the top of the chain *before* the branch's own Apple/guard
edits D609/D610/D612; D607/D608 are identity on Windows at base 0, so the
reference keeps the current release-line look — D565/D569/D570 included —
while staying independent of the branch's edits). Worktree `scratch/wt-goldens`
(`git worktree add` at `e0a2d4b9`, `data/` junction to the main checkout),
`./build-pc.sh ntsc-final`, then `tools_pc/capture_p7.sh -b build-pc -p win
-j 2` with **lowercase level names**. 63 frames, every level rc=0
(dam 1500-1700:100, frigate/surface2/streets/depot/cradle 1000-1400:200,
cuba 300-900:300 `SNONE`, the rest 900-1500:300 — per `golden_dump_for`).

**Gotcha (cost one re-capture):** `capture_p7.sh`'s windows come out of
`verify.sh`'s `golden_dump_for`, whose case table is **lowercase**
(`dam`, `frigate|...`, `cuba`). Passing `Dam:33` / `Cradle:41` ... silently
fell through to the default `900-1500:300` window for every level — the
capture log's per-level `window=` line is the only tell. Lowercase names
are also what the golden folder names are.

**Verification (2026-10-08, Windows, `macos` tip `43542455`):**
`./tools_pc/verify.sh sweep -j 2` over all 21 levels: **21/21 PASS** —
worst structural cell 8.44 (streets, cell-mean units; every per-pixel tier
within its per-level limit; run-to-run residual). The gate is measurable
again on the branch.

**Caveat (cross-platform spread):** the `linux/` and `deck/` sets (captured
2026-10-05 from a pre-D565/D569 tree) now differ from the new `win/` set by
exactly that visual delta. Per the D521 rule the cross-platform spread is
informational only (never a parity gate; each platform's gate is its own
captures against its own goldens) — but a *fresh* linux/deck capture round
on those boxes is owed before reading any new cross-platform number.

**Status: FIXED 2026-10-08 (macos branch; win goldens regenerated from
`e0a2d4b9`, 21/21 PASS, recipe-side only).**
