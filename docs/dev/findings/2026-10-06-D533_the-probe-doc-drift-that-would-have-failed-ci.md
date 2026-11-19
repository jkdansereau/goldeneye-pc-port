# D533

<!-- from the old section-F/H table row -->
| D533 | **The probe-doc drift that would have failed CI, plus three tracked-file accuracy/hygiene fixes (release-sweep spot-check)** — release gate (P12 U2/U6/U7 spot-checks); full `## D533` entry at file tail | FIXED 2026-10-05 (`GE-ENV-PROBES.md` missing the `GE_D526` family → `gen_env_probes.py --check` rc 1 = a CI-failing step; `build-pc.sh` local MSYS2 root in two messages; `docs/fidelity.md` og line; release-notes gate paragraph quoted a pre-D531 spread) |

## D533: release-sweep spot-check — the probe-doc drift that would have failed CI, plus three tracked-file accuracy/hygiene fixes (release gate; P12 U2/U6/U7 spot-checks)

**What was checked (P12 U2/U6/U7 spot-checks, read-only until a defect was found):**
the EEP import path (`eepimport.c`: fixed-size `save_data[96]` + `smallSave[32]`
copies, the 6-region backup rotation bounded at `.bak99`), the seat model
(`MAX_PADS = 4` with `seat < 0 || seat >= MAX_PADS` guards, per-seat arrays
`padPreset`/`padActPrev`/`padSelectPrev` all `[MAX_PADS]`), the ini/config
buffers (`configRegisterString`'s `bufSize-1` `strncpy`, `padStr[MAX_PADS][PA_COUNT][32]`
filled by `snprintf` with `sizeof`), the build's region-macro set against the
N64 Makefile's `LCDEFS` (exact match for ntsc/pal/jpn-final), and the published
docs' claims against the findings index.

**The CI-blocking one:** `tools_pc/gen_env_probes.py --check` returned **rc 1** —
three probe vars in `port/fast3d/gfx_pc.cpp` (`GE_D526`, `GE_D526BOX`,
`GE_D526MAX`, the D526 box-tolerance family) had no row in
`docs/dev/GE-ENV-PROBES.md`. The self-hosted workflow runs that check as a hard
step ("Check generated dev indexes are current"), so the pushed branch would
have carried a red CI step. Fixed by documenting the family (it was already
listed in the `docs/porting-notes.md` §F10 probe table — the drift is between
the two generated-doc sources, not a missing record).

**`build-pc.sh` carried a local machine path.** Two messages named the
maintainer's alternate MSYS2 root (`D:\<games>\msys64` form — the tree here was
built for a different drive letter than the one it runs from). AGENTS rule 1
forbids local absolute paths in tracked files; both sites now say `C:\msys64`,
which is what the surrounding "add `C:\msys64\mingw64\bin` to your PATH" advice
already says. No behaviour change (the strings are diagnostics only; the toolchain
discovery itself is path-derived via `_ge_find_mingw_bin()`).

**`docs/fidelity.md` og description.** It read "…the one `#ifdef PORT` fix…" —
misleading: `src/` carries 736 `#ifdef PORT` gates (the §A1 class), and the
finding's claim is that exactly **one** of them is a game-logic-adjacent change.
The description now reads "one game-logic-adjacent change", and the trim keeps it
under the 160-char og limit (158). The body's precise claim is unchanged.

**The release notes quoted a stale measurement.** The Reference-frame-gate
paragraph carried the D525 cross-platform spread (0.348–3.983 %, worst cell mean
4.43, phash ≤6 except cradle 40) — measured before D531 re-captured all 21 linux
sets under the canonical save (47/63 frames moved). Recomputed from the
**committed** pairs (`framediff.py` win-vs-linux at the stems, tol 2): exact
**0.380–5.120 %** on the 20 non-Cuba levels (silo's 900 stem is the outlier —
cell mean 34.3, phash 10; streets 3.223–4.566), structural **21/21 clean**, max
phash 40 (cradle, at the threshold); Cuba 1.357–12.663 % is the structural-tier-only
level. The notes now carry the recomputed band and name the two gate pins a
reader would otherwise have to look up (canonical save content — D529; hard-pinned
seed — D532). The D525 figures stay in the ROADMAP row as the historical record
for closing the two residual bands; the row now also carries the recomputed band.

**Not changed (judgement calls left alone):** the `eepimport.c` `.bak99`
overwrite-on-rotation nit (cosmetic, 99-deep rotation is unreachable in practice),
the `docs/ROADMAP.md` "Known issues" heading anchor (valid), and the site pages'
own staleness (presentation layer, per AGENTS).
