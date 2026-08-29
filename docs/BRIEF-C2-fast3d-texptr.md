# BRIEF C2 — fast3d bad texture pointer on Facility + Jungle

Repo: `REPO_ROOT` (git, `master`, Windows;
PowerShell + Bash).

## READ FIRST
- `docs/HANDOFF.md` (top), `docs/PORT-LEARNINGS.md` (§C BE rodata, §D N64
  hardware idioms fast3d doesn't emulate).
- `docs/PCPortResearch.md` §F entries **D69, D68, D39, D67, D107, D84,
  D58** (texture / segment-address history — jump via §F index).
- `AGENTS.md` non-negotiables. `rsp/graphics/gmain.s` = GBI ground truth.
- PD analogue: `PD_PORT_CHECKOUT` `port/fast3d/`
  texture import path — check first.

## SYMPTOM (WS4 sweep — `docs/LEVEL-STATUS.md`)
Bare `./build-pc/ge007.x86_64.exe -level_XX`, early deterministic crash:

| Level | # | Site | Fault addr |
|---|---|---|---|
| Facility | 34 | `import_texture_i8` `port/fast3d/gfx_pc.cpp:821` | `0x72181ee8` (looks like an unresolved N64 segment addr) |
| Jungle | 37 | `gfx_tex_normalize_source` `port/fast3d/gfx_pc.cpp:644` | `0xabcd0824` (sentinel / uninit value) |

fast3d is handed a garbage texture address. 7 levels render fine
(Bunker1/Silo/Archives/Train/Caverns/Egypt/Cuba) so the common path is
sound — Facility/Jungle pull in a texture asset or a GBI texture-image
segment address the others don't.

## TASK
Root-cause both (likely the same class) and apply the narrowest fix.
Hypotheses:
1. A per-level texture asset mis-converted / missing —
   `tools_pc/d69_emit.py` (pccg path) or `port/src/pccg.c`. `0xabcd....`
   is a classic uninit-slot sentinel — a texture slot that was never
   filled because the converter skipped a format/opcode.
2. A GBI `gsDPSetTextureImage` segment address not resolved to a real
   pointer before `import_texture_*` (segment-fold class — D58/D84;
   `(u32)` 32-bit wrap on `BG_SEG_TO_PTR` / K0 folds). `0x72181ee8` is in
   the N64 segment-mapped range.
3. A texture LOD / detail-tile path (D107) hit only by these levels.

## FILES YOU MAY TOUCH
- `port/fast3d/gfx_pc.cpp` (narrow — a resolve/guard at the import site).
- `tools_pc/d69_emit.py` (+ `tools_pc/d69_*` helpers), `port/src/pccg.c`,
  `port/src/romdata*.c` (segment resolution).
- New probe files `tools_pc/c2_*` / env-gated `GE_C2` blocks.
- `docs/PORT-LEARNINGS.md` (append), `docs/PCPortResearch.md` §F (next Dxx
  after the current §F-index max; add to index), `docs/LEVEL-STATUS.md`.
- **NOT** `src/game/chr*.c` or `src/game/propobj.c` (other tracks).

## KNOWN-GOOD / RULED OUT
- D121 pool injection, D122 propDef fix — committed, work.
- 7 levels render clean — the base texture path is fine.
- HUD/text X-mirror (D114/D116) — parked, do NOT re-trace (PORT-LEARNINGS §D2).

## PRE-FLIGHT
- `export PATH="/c/msys64/mingw64/bin:$PATH" && ./build-pc.sh ntsc-final` — green.
- Repro `./build-pc/ge007.x86_64.exe -level_34`. Crash log
  `ge007.crash.log`. `addr2line -e build-pc/ge007.x86_64.exe -f -C 0x<PC>`.
- `GE_PCDUMP="80-260:40"` → `./ppm/`; `python tools_pc/pixcount.py`.
- `timeout` does NOT kill on Windows: `... &`, `sleep 24`,
  `taskkill //F //IM ge007.x86_64.exe`.
- Regen sidecars after converter change:
  `python tools_pc/d43_emit.py ntsc-final && python tools_pc/d69_emit.py ntsc-final && python tools_pc/d88_emit.py ntsc-final --regen`.

## BUDGET
**~10 build→run→inspect cycles or ~75 min.** On expiry: revert probes,
write up in §F with confidence rating + files touched.

## CONSTRAINTS
No game-logic changes; ABI/layout/format / port-layer only. Prefer
converter fix over runtime. Revert temp probes. Don't touch other tracks'
uncommitted edits.

## REPORT
1. Root cause + `file:line` evidence, why Facility/Jungle differ.
2. Fix diff or why-not.
3. Verification: `-level_34` + `-level_37` boot to ≥1 non-degenerate
   frame; `-level_09` + `-level_20` unregressed (`framediff.py`).
4. Probes left in tree.
5. Confidence.
6. Quirk appended to `docs/PORT-LEARNINGS.md`.
