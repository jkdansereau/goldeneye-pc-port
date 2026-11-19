# D185

## D185 — fresh `d69_emit.py` run yields a game-crashing `data/pccg-<region>/` (M-38)

**Status: OPEN — alpha-release blocker.** (Add to the §F index.)

**Symptom.** `python tools_pc/d69_emit.py ntsc-final` in this repo writes
`data/pccg-ntsc-final/pccg.bin` = 3346895 B (52 sidecars, 1856-B manifest),
prints `ALL CHECKS PASSED`, and `-level_09` / `-level_20` then segfault before
frame 1 (`EXCEPTION: 0xc0000005`, faulting return address is an ASCII
model-name string). The pre-migration working sidecar
(`<repos>/007/data/pccg-ntsc-final/pccg.bin`, 3604378 B,
2421-B manifest) drops into this tree and `-level_09` runs crash-free.

**Isolation done (M-38).**
- Not the romdata M-38 change: baseline (stashed) crashes identically.
- Not the build: same exe, only the `data/pccg-ntsc-final/` bytes swapped.
- `tools_pc/d69_emit.py` is byte-identical between the public repo and the
  pre-migration repo `007` (only a doc-path comment differs).
- The migration commit `52539d10 "Prepare for public release"` normalised the
  committed d69 inputs from CRLF to LF: `scripts/filelist.u.csv` (−812 B, one
  CR/line) and `assets/obseg/file_resource_table.inc.c`.
- **Restoring CR to both inputs did NOT restore the 3604378-B output** → line
  endings are not the (whole) cause. Some other part of the d69 input closure
  regressed in the history rewrite, or the working sidecar predates a real
  d69/​input change and 3346895 is a separate second bug.
- `d43_emit.py` is unaffected (its `find_row` tries `name`, `name[1:]`,
  `name[:-1]`, `name[1:-1]` — tolerant of a mangled basename; `d69`'s
  `fl_by_base.get(basename)` is exact).

**Impact.** `docs/building.md` tells users to run `d69_emit.py`; M-38's
`prepare-assets.py` does the same. Both currently produce a broken game on a
clean checkout. The committed `data/` dirs (carried from pre-migration) hide it.

**Next.** Diff the sidecar *name* set: pre-migration `manifest.csv` vs a fresh
run's — find which bg/stan entries d69 now drops, trace through `find_row` →
`file_resource_table.inc.c` → `filelist.u.csv`. Fix so a fresh run reproduces
3604378 B / 2421-B manifest; re-verify `-level_09`/`-20` crash-free. Do NOT
regenerate `data/pccg-ntsc-final/` before the fix (it holds the working copy).

### D185 — RESOLVED (M-38): not a d69 regression, a missing d88 step

`d88_emit.py` **appends** the 21 `Usetup*Z` per-level stage-setup files
(object placement, AI opcode streams, pads/nav) to the same
`data/pccg-<region>/pccg.bin` + `manifest.csv` that `d69_emit.py` writes
(52 bg/stan rows → 74 after d88). The M-38 `prepare-assets.py` first shipped
ran only d43 + d69, so every level loaded with no setup data → instant
segfault. The "working" pre-migration sidecar was simply a complete
d43+d69+d88 output; nothing regressed in d69 or its inputs.

Fix (commit `4491db3f`): `prepare-assets.py` runs d43 → d69 → `d88 --regen`;
`bundle-win.sh` vendors `d88_emit.py` + its only local import
`d88_propdefs.py`. Verified: fresh 3-script run from an assembled bundle is
byte-identical to the known-good `pccg.bin` (3604378 B) / `pcmodels.bin`;
`-level_09` + `-level_20` boot crash-free. The CRLF→LF observation on
`filelist.u.csv` / `file_resource_table.inc.c` is real but a red herring for
this crash (d69 tolerates it; output unchanged).
