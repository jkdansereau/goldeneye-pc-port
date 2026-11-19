# `tools_pc/` — PC-port dev & test scripts

Index for the ~60 scripts here (speed-ups plan Step 4 / N2). None of these are
part of the build; they are converters, verification tooling, and
investigation artifacts.

**Marker:**
- **`living`** — kept and re-run; part of the verification ritual, the release
  flow, or a converter the build depends on (via the sidecars it emits).
- **`artifact`** — added to chase one finding. Safe to move to
  `tools_pc/archive/<dxx>/` once that finding is closed and the technique is
  captured in `docs/dev/findings.md` / `docs/porting-notes.md`.

## Living — converters the sidecars depend on

| Script | Role |
|---|---|
| `d43_convert.py` | Reference single-file model converter + full-512 layout/pointer validator (D43). |

## Living — verification & release

| Script | Role |
|---|---|
| `verify.sh` | Speed-ups plan Step 2. One command → one verdict: build-if-stale, headless `GE_PCDUMP`/`GE_INPUTSCRIPT` capture, crash-detect + symbolication (via `crash_brief.py`), `pixcount`, `framediff` vs golden, `--json`. `verify.sh <level>`, `verify.sh sweep [subset]`, `verify.sh parity <level> --against <dir>`. |
| `a2cdump.py` | D984: converts the `GE_A2CDUMP=1` texture dumps (`$S/a2cdump/*.tga`, the textures the alpha-to-coverage test kept on the N64 blend) to PNG, prints each one's alpha spread (0 / 255 / in-between, levels) with a cut-out / ramp verdict, and `--sheet` draws them all on one checkerboard contact sheet. `a2cdump.py <dir> [--out <dir>] [--only ramp\|cut-out] [--sheet <png>]`. Output is ROM-derived: keep it out of the tree. |
| `crash_brief.py` | Speed-ups plan Step 9 / R5. `ge007.crash.log` → symbolicated frames + a dev-process.md-shaped dispatch-ready brief: matches porting-notes.md sections / findings-index.csv entries by resolved function/file, flags known parked-crash signatures (Cuba credits, D188 va_list, D189 stack overrun) so agents stop re-investigating them. Called automatically by `verify.sh` on CRASH; also runnable standalone. |
| `framediff.py` | Visual regression: candidate `GE_PCDUMP` frames vs `tools_pc/golden/`, per-region divergence. Structural/tolerant by default; `--exact` after `GE_DETERM`. |
| `pixcount.py` | Count non-black pixels in a PPM dump — "did the scene render anything" as a number. |
| `level_sweep.sh` | Bare `-level_XX` boot of all 21 solo levels → PASS / NO-FRAMES / CRASH. Predates `verify.sh sweep`; kept as the battle-tested full-21-level runner until `verify.sh sweep` has done an equivalent soak. |
| `levelbench.sh` | Boot one solo level, run a scripted input pattern (`fight` / `turn` / `idle` / `raw:`) or a recorded demo (`--record` / `--replay`, pad input incl. dedicated use, pinned `GE_RSEED`), quit via `GE_QUITFRAME`, keep `ge007.log` + a cadence/SLOW summary. Windows, Linux, Deck. |
| `deckrunner.sh` | Steam Deck Game Mode job runner: add it to Steam as a non-Steam game and start it; it runs `levelbench.sh` jobs dropped into `~/lb/queue/` over ssh (gamescope hides ssh-launched windows). |
| `playtest.sh` | Launch a level for `docs/dev/LEVEL-PLAYTEST.md` human validation (WS6). |
| `audiodebug.ps1` | Audio counterpart to `debug.ps1` (that one is for crashes; this one is for audio behaviour). Drives the `GE_D204` health monitor + `GE_AUDIOTRACE`/`GE_AUDIODUMP` and prints a pass/fail verdict: real-time ratio, queue starvation, dropped blocks, oversized blocks, plus a soundIndex histogram. `-Play` for an instrumented interactive playtest (the D202 workflow), `-Soak` for a 5-min stability run, `-SyncData` to mirror `./data` first. |
| `debug.ps1` / `repro_gdb.sh` / `attach_animgen.sh` | Launch (or attach to) the game under gdb so a crash always leaves a backtrace. |
| `bundle-win.sh` / `bundle-linux.sh` | Package a built tree as a distributable archive (exe + licenses). |
| `romverify.c` | One-shot `.z64` integrity check against the repo's ground truths. |
| `disasm.py` | Minimal MIPS disassembler for the BE ROM (RAM-addr → file offset). |
| `ppm2bmp.py` | PPM → 24-bit BMP, no deps — eyeball `GE_PCDUMP` frames without PIL. |
| `gen_findings_index.py` | Build the untracked `docs/dev/findings-index.csv` (grep-before-you-read aid over the one-file-per-finding log in `docs/dev/findings/`). `--check` validates finding file names and unique labels (CI). |
| `docs_budget.py` | Context cost (approx tokens) of the docs every agent session loads, against per-file and tier-1 budgets; exits 1 when over. `--toc FILE` prints a heading TOC, `--json` for scripting. |
| `gen_env_probes.py` | Drift check for `docs/dev/GE-ENV-PROBES.md` — re-greps live `getenv("GE_*")` sites, reports NEW/GONE. |

## Living — compiler-verified layout probes (kept: re-run when structs change)

| Script | Role |
|---|---|
| `gen_convert_data.py` | Regenerates `port/convert/convert_data.c` (filelist CSVs, resource-table names, model NS/NT) for the C ROM converter. |
| `d43_layoutprobe.c` | Prints `sizeof`/`offsetof` for every struct `port/convert/conv_d43.c` must reproduce, built with the port toolchain. |
| `d88_layoutprobe.c` | Same, for the `PROPDEF_*` record structs `port/convert/conv_d88.c` converts. |
| `mtxtest.c` | Standalone numerical test of the fast3d matrix pipeline conventions (GE swapped-perspective, packing, MUL order). |

## Investigation artifacts — D43 model-file format (finding closed; archive candidates)

`d43_chainbound.py`, `d43_cover.py`, `d43_decode.py`, `d43_fullwalk.py`,
`d43_gdldump.py`, `d43_gdlhist.py`, `d43_gdlorder.py`, `d43_gdlseq.py`,
`d43_invariants.py`, `d43_layout.py`, `d43_layout1.py`, `d43_lutscan.py`,
`d43_pointusage.py`, `d43_seg5.py`, `d43_seg5ops.py`, `d43_seg5vtx.py`,
`d43_seg5vtx2.py`, `d43_sizes.py`, `d43_sizes2.py`, `d43_tree_dump.py`,
`d43_vtxfmt.py`, `d43_walk.py` — layout/pointer/GDL-order analysis passes that
derived the D43 converter spec. `d43_sizes.py` is still `exec()`'d by a couple
of the others.

## Investigation artifacts — other closed/parked findings

| Script | Finding |
|---|---|
| `d88_propdef_scan.py` | D88.4 — histogram of `PROPDEF_*` record types across shipped levels. |
| `d51_gdb.py` / `dump_animgen.cmd` | D51 texture-tile crash capture / Facility-outro-hang anim-state dump (gdb scripts). |

## Other

- `dist/` — staged bundle README template (`README.md.in`) + tokens.
- `golden/` — per-level golden frames for `framediff.py` (Step 3 fills this out).
- `__pycache__/` — ignored.
