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
| `d43_emit.py` | Offline N64→PC model-file converter → RZ sidecar + `manifest.csv` (D50 / Plan B). Regen after any model-format change. |
| `d43_convert.py` | Reference single-file model converter + full-512 layout/pointer validator (D43). |
| `d69_emit.py` | Offline converter for stage `bg/*.seg` + `Tbg_*_stanZ` → concatenated sidecar (D69/D78–D82). |
| `d88_emit.py` | Offline converter for per-level `Usetup*Z` stage-setup files → appended to the `pccg.bin` sidecar (D88). |
| `d88_propdefs.py` | The `propDefs` polymorphic-record stream N64→PC converter used by `d88_emit.py` (D88.4). |
| `d69_emit.py` / `d88_emit.py` / `d43_emit.py` | Run all three (+ `d88_propdefs`) to rebuild `data/` sidecars from the ROM. |

## Living — verification & release

| Script | Role |
|---|---|
| `framediff.py` | Visual regression: candidate `GE_PCDUMP` frames vs `tools_pc/golden/`, per-region divergence. Structural/tolerant by default; `--exact` after `GE_DETERM`. |
| `pixcount.py` | Count non-black pixels in a PPM dump — "did the scene render anything" as a number. |
| `level_sweep.sh` | Bare `-level_XX` boot of all 21 solo levels → PASS / NO-FRAMES / CRASH. (Step 2 folds this into `verify.sh sweep`.) |
| `playtest.sh` | Launch a level for `docs/dev/LEVEL-PLAYTEST.md` human validation (WS6). |
| `debug.ps1` / `repro_gdb.sh` / `attach_animgen.sh` | Launch (or attach to) the game under gdb so a crash always leaves a backtrace. |
| `bundle-win.sh` / `bundle-linux.sh` | Package a built tree as a distributable archive (exe + licenses + `prepare-assets/`). |
| `romverify.c` | One-shot `.z64` integrity check against the repo's ground truths. |
| `dump_objectives.py` | Dump per-level objectives + win/fail criteria from the ROM (playtest validation; Step 10 / N8 acceptance oracle). |
| `disasm.py` | Minimal MIPS disassembler for the BE ROM (RAM-addr → file offset). |
| `ppm2bmp.py` | PPM → 24-bit BMP, no deps — eyeball `GE_PCDUMP` frames without PIL. |
| `gen_findings_index.py` | Regenerate `docs/dev/findings-index.csv` (grep-before-you-read aid for the 200 KB finding log). `--check` in CI-style use. |
| `gen_env_probes.py` | Drift check for `docs/dev/GE-ENV-PROBES.md` — re-greps live `getenv("GE_*")` sites, reports NEW/GONE. |

## Living — compiler-verified layout probes (kept: re-run when structs change)

| Script | Role |
|---|---|
| `d43_layoutprobe.c` | Prints `sizeof`/`offsetof` for every struct `d43_emit.py` must reproduce, built with the port toolchain. |
| `d88_layoutprobe.c` | Same, for the `PROPDEF_*` record structs `d88_emit.py` converts. |
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
| `d125_check.py` | D125 — is the emitted `propDefs` blob byte-identical to `d88_propdefs.convert_stream()`? |
| `d88_propdef_scan.py` | D88.4 — histogram of `PROPDEF_*` record types across shipped levels. |
| `d51_gdb.py` / `dump_animgen.cmd` | D51 texture-tile crash capture / Facility-outro-hang anim-state dump (gdb scripts). |

## Other

- `dist/` — staged bundle README template (`README.md.in`) + tokens.
- `golden/` — per-level golden frames for `framediff.py` (Step 3 fills this out).
- `__pycache__/` — ignored.
