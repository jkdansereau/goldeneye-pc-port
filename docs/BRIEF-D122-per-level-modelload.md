# BRIEF D122 — per-level prop/item model-load crash (blocks the level sweep)

Repo: `REPO_ROOT` (git, branch `master`, Windows;
PowerShell + Bash tool available).

## READ FIRST

- `docs/HANDOFF.md` (top) — current state, environment, repro line.
- `docs/PORT-LEARNINGS.md` — recurring bug classes. **A / C are the
  prime suspects here** (pointer-width struct growth; BE rodata on LE).
- `docs/PCPortResearch.md` §F index (top) + entries **D43, D53.2, D56,
  D57, D67, D68, D39, D108–D112, D120** — model/asset conversion +
  pointer-width history. Jump to each via the index; do NOT linear-read.
- `AGENTS.md` non-negotiables (esp. #2 game logic unmodified — narrow
  `#ifdef PORT` ABI/layout/format edits only, each logged in §F).

## CONTEXT (established this session, M-12)

- **WS1 landed** (commit `02c12068`, D121): a bare
  `./build-pc/ge007.x86_64.exe -level_XX` now auto-injects that stage's
  `memallocstringtable[]` pool row. Every solo level boots with correct
  pools now — no manual `-m*` needed.
- **D88.4 (propDefs converter) is effectively done for the 21 solo
  levels.** `tools_pc/d88_emit.py` + `d88_propdefs.py` are committed
  (`ff563812`, `4ccc0d3c`); `python tools_pc/d88_emit.py ntsc-final
  --regen` reports `21/21 converted … ALL CHECKS PASSED`. The placeholder
  `PROPDEF_PC_BYTES` for VEHICLE/AIRCRAFT/TANK/AMMO are **not exercised by
  any solo level** (`tools_pc/d88_propdef_scan.py` histogram — no
  VEHICLE/AIRCRAFT/TANK in any of the 21). So D88.4 is NOT the blocker.
- **The actual blocker: prop/item model loading crashes on most levels.**
  Smoke test (each `-level_XX`, `GE_PCDUMP="60-240:60"`, ~25 s):

  | level | # | result |
  |---|---|---|
  | Bunker1 | 09 | renders, playable (known good) |
  | Silo | 20 | renders ~91% frame content, no crash |
  | Dam | 33 | **CRASH** `modelInitRwData` (`src/game/model.c:6249`) — `node->Opcode` deref, bad `node` in the ModelNode walk |
  | Facility | 34 | **CRASH** `modelLoad` (`src/game/loadobjectmodel.c:393`) — `PitemZ_entries[modelid].header->RootNode`, bad `header` (or OOB `modelid`) |
  | Runway | 35 | **CRASH** same as Facility (`loadobjectmodel.c:393`) |

  Crashes are **early and deterministic** (before frame 1 renders), same
  PC every run — distinct from BUNKER1's rare intermittent
  `loadobjectmodel.c:393` (D117 nondeterminism). `EXCEPTION 0xc0000005`,
  `FAULT ADDR: ffffffffffffffff`. `Rax` at the Facility crash holds ASCII
  bytes ("…o ng nol…") — looks like a model/texture name string landed in
  a register, consistent with a mis-strided table read.

## TASK

**One question: why do prop/item models load fine for BUNKER1/Silo but
crash for Dam/Facility/Runway, and what is the narrow fix?**

Almost certainly one of:
1. `PitemZ_entries[341]` (`struct ItemModelFileRecord { ModelFileHeader
   *header; char *filename; float scale; }`, `src/bondtypes.h:1466`) —
   table stride or the `header`/`filename` pointer values wrong on PC.
   Find where this array is **defined** (it is `extern` everywhere I
   looked — `src/game/prop.c:40`, `chrobjdata.h:20`; the definition is
   likely a generated `.s`/rodata blob or an asset — `grep` harder,
   check `port/src/romassets_*.s`, `scripts/gen_romassets.py`,
   `scripts/gen_*`). If the `header` pointers come from BE ROM rodata
   they need rebasing/byteswap (D39 Globalimagetable / D68 class).
2. `modelid` passed to `modelLoad` is out of range / unswapped — it comes
   from a propDef `ObjectRecord`-family `modelid` field. If a record type
   used by Dam/Facility props (but not BUNKER1) has its `modelid` at a
   field offset the `d88_propdefs.py` converter byteswaps wrongly (or
   not at all), `modelid` is garbage. Cross-check the per-type field
   maps in `tools_pc/d88_propdefs.py` against the record structs.
3. The `pcmodels` sidecar (`tools_pc/d43_emit.py`, loaded at
   `0x10C00000`, "512 sidecars") is missing/mis-converting a model
   feature (ModelNode opcode, `PointUsage[]` chain — cf. D120,
   `d43_emit.py` opcode-0x18 gap) that Dam/Facility prop models use.
   `model.c:6249` already has a `GE_D86` env probe for the node walk —
   use it.

Determine which, root-cause, and apply the **narrowest** fix (converter
extension preferred over runtime fixup for a whole format — D43/D88
pattern; or a narrow `#ifdef PORT` ABI edit with the N64 line kept under
`#else`).

## FILES YOU MAY TOUCH

- `src/game/model.c`, `src/game/loadobjectmodel.c`, `src/game/prop.c`
  (narrow `#ifdef PORT` only).
- `tools_pc/d43_emit.py` (+ `tools_pc/d43_*.py` helpers, `d43_layoutprobe.c`).
- `tools_pc/d88_propdefs.py` / `tools_pc/d88_emit.py` (only if the cause
  is a propDef `modelid` field).
- `port/src/` model/asset shims if the table is patched at runtime like
  `pcmodelsPatchTable` (`grep pcmodelsPatchTable`, `port/src/romdata*.c`).
- New probe files: `tools_pc/d122_*.py` / env-gated `GE_D122` blocks.
- `docs/PORT-LEARNINGS.md` (append any new generalisable quirk),
  `docs/PCPortResearch.md` §F (write up as **D122**, add to the index).

Nothing else is running — no file collisions to worry about.

## KNOWN-GOOD / RULED OUT — do not re-investigate

- WS1 per-level pool injection (D121) — works, committed.
- D88.4 propDefs stream conversion for solo levels — works (21/21).
- BUNKER1 + Silo prop/model loading — works.
- The intermittent BUNKER1 `loadobjectmodel.c:393` crash — that is
  D117-class nondeterminism, separate; do not chase it, use the
  deterministic Dam/Facility/Runway repro.
- `d88_propdef_scan.py` "STOP … unmapped type NOTHING" — that is the
  *scan tool's* limitation, not a converter bug (`d88_emit.py` completes).

## PRE-FLIGHT (done — results above)

- `./build-pc.sh ntsc-final` green; binary at `build-pc/ge007.x86_64.exe`.
- Sidecars regenerated this session (`d43_emit.py`, `d69_emit.py`,
  `d88_emit.py --regen`) — all pass.
- Smoke-test table above.

## ENVIRONMENT

```sh
export PATH="/c/msys64/mingw64/bin:$PATH"
./build-pc.sh ntsc-final                       # ~5 s, from repo root
./build-pc/ge007.x86_64.exe -level_34           # Facility repro (bare — D121)
```

- `timeout` does NOT reliably kill the game on Windows. Launch with
  `… &`, `sleep N`, then `kill $PID; pkill -f ge007`.
- Crash log: `ge007.crash.log` (repo root). Symbolicate:
  `addr2line -e build-pc/ge007.x86_64.exe -f -C 0x<PC>` (image base
  `0x140000000`; PC in the log is already absolute).
- Frame capture: `GE_PCDUMP="a-b:step"` → `./ppm/`; content check
  `python tools_pc/pixcount.py ppm/frame_XXXX.ppm`.
- gdb: **attach** mode is fast (`gdb -batch -x cmds.txt -p <winpid>`,
  winpid = 4th col of `ps -p <bashpid>`; game must already be running).
  Launch mode is too slow. A hardware watchpoint on `PitemZ_entries` or
  the bad `node` catches the corrupting write fast.
- Standalone probe compiles: `-std=c11`.

## BUDGET

Structural bug → generous: **~12 build→run→inspect cycles or ~90 min**.
On expiry: revert all temp probes (leave only committed env-gated ones),
write up the spec progress + best hypothesis in `docs/PCPortResearch.md`
§F as **D122** with an explicit confidence rating, and list exactly which
files you changed. A good half-solved write-up is a deliverable.

## CONSTRAINTS

- No game-logic / control-flow changes. ABI/layout/format only, each
  `#ifdef PORT` with the N64 line kept verbatim under `#else`, each
  documented in §F/D122.
- Prefer an offline sidecar-converter extension over a runtime fixup for
  a whole asset format.
- Revert your own temp probes before reporting.
- Regenerate sidecars after any converter change:
  `python tools_pc/d43_emit.py ntsc-final && python tools_pc/d69_emit.py
  ntsc-final && python tools_pc/d88_emit.py ntsc-final --regen`.

## REPORT

1. Root cause — `file:line` evidence, which of the 3 hypotheses (or
   other), why Dam/Facility/Runway differ from BUNKER1/Silo.
2. Fix — the diff, or why-not if unresolved.
3. Verification — `-level_33`, `-level_34`, `-level_35` each boot to ≥1
   non-degenerate frame (`pixcount.py` > a few %), AND `-level_09`
   (BUNKER1) still renders (no regression — `tools_pc/framediff.py`).
4. Probes left in the tree (env-gated, capped).
5. Confidence rating.
6. Append any generalisable quirk to `docs/PORT-LEARNINGS.md`.
