# BRIEF C1 — `chrIsNotDeadOrShot` crash on 6 levels (blocks the level sweep)

Repo: `REPO_ROOT` (git, branch `master`, Windows;
PowerShell + Bash tool).

## READ FIRST
- `docs/HANDOFF.md` (top) — state, environment, repro line.
- `docs/PORT-LEARNINGS.md` — **§A (pointer-width struct growth) and §C
  (BE rodata on LE) are the prime suspects.** Also §A D119
  (`weapons_held[]->chr` type-pun) — closest prior instance.
- `docs/PCPortResearch.md` §F entries **D119, D115, D101, D102, D56, D57**
  (jump via the index — do NOT linear-read).
- `docs/AUDIT-M6-player-offsets.md` — the raw-offset landmine class.
- `AGENTS.md` non-negotiables (#2: narrow `#ifdef PORT` ABI/layout only,
  N64 line kept under `#else`, logged in §F).

## SYMPTOM (WS4 sweep, this session — `docs/LEVEL-STATUS.md`)
`./build-pc/ge007.x86_64.exe -level_XX` (bare — pools auto-injected, D121),
crashes **early, before frame 1**, deterministically, at:

```
chrIsNotDeadOrShot  src/game/chraction.c:4483   s8 currentaction = self->actiontype;
```

`EXCEPTION 0xc0000005`, FAULT ADDR `0x8` (Dam). Crash **`self` = the same
value as backtrace frame #1 = `0x1401296a0`** — an address *inside the
compiled binary's rodata*, in the `chraidata.c` global-AI-list blob
(`m_AimAtBond[]` / `m_*[]` at `chraidata.c:61+`). So a `ChrRecord *self`
is pointing at AI-list bytecode rodata — a pointer swap / pun / wrong
struct-offset read, not mere corruption.

Affected: **Dam 33, Runway 35, Frigate 26, Statue 22, Streets 29,
Cradle 41** (6). NOT Bunker1 09 / Silo 20 / Archives 24 / Train 25 /
Caverns 39 / Egypt 32 / Cuba 54 (those 7 PASS, render ~91%).

## TASK
**One question: why is a `ChrRecord*` actually an AI-list rodata pointer
on these 6 levels, and what is the narrow fix?**

Leading hypotheses:
1. A per-level `Usetup*Z` guard record (`GuardRecord` type 9 /
   `GuardAttributeRecord` type 18, or the chr-spawn record) has a pointer
   or `s16/s16` packed field converted wrongly — or not at all — by
   `tools_pc/d88_propdefs.py` (cf. D122: the "generic" fallback arm
   word-swaps sub-word fields). Enumerate every propDef `type` byte the 6
   crashing levels emit vs the 7 passing ones
   (`tools_pc/d88_propdef_scan.py` histogram) — the delta type is the
   suspect. The 7 pass / 6 fail split is the strongest lead.
2. The AI-list bytecode pointers in `chraidata.c` `.rodata`
   (`SetChrAiList`, list-table entries) are BE and consumed raw — a chr
   gets `aiList` set to a byteswapped pointer, then a subsequent tick
   treats it as / derefs it as a `ChrRecord*`. Check `chraifunc.c` /
   `chraidata.c` list dispatch and any `AiListRecord`-style table.
3. `ChrRecord` / `GuardRecord` struct is pointer-width-grown on x86-64
   and a per-level record is read from ROM bytes at N64 offsets (D56/D57
   class) — `self->actiontype` at the wrong byte.

Determine which. Prefer an **offline converter extension**
(`d88_propdefs.py` / `d88_emit.py`, D43/D88 pattern) over a runtime fixup;
else a narrow `#ifdef PORT` ABI edit.

## FILES YOU MAY TOUCH
- `src/game/chraction.c`, `src/game/chr.c`, `src/game/chraidata.c`,
  `src/game/chraifunc.c` (narrow `#ifdef PORT` only).
- `tools_pc/d88_propdefs.py`, `tools_pc/d88_emit.py`,
  `tools_pc/d88_layoutprobe.c`, `tools_pc/d88_propdef_scan.py`.
- New probe files `tools_pc/c1_*.py` / env-gated `GE_C1` blocks.
- `docs/PORT-LEARNINGS.md` (append), `docs/PCPortResearch.md` §F (write up
  as the next Dxx after D122 — check the §F index for the current max —
  add to the index), `docs/LEVEL-STATUS.md` (flip the rows you fix).
- **NOT** `port/fast3d/*` or `src/game/propobj.c` (other tracks).

## KNOWN-GOOD / RULED OUT — do not re-investigate
- D121 per-level pool injection — works, committed.
- D122 propDef `obj` half-swap — committed `f2beae4b`; your bug is a
  *different* record type/field (BUNKER1/Silo unaffected).
- BUNKER1 + Silo + Archives + Train + Caverns + Egypt + Cuba chr/AI setup
  — works. Use them as the "good" control for the propDef-type diff.
- The intermittent BUNKER1 `loadobjectmodel.c:393` crash — D117
  nondeterminism, unrelated.

## PRE-FLIGHT ATTACHED
- `./build-pc.sh ntsc-final` green (`export PATH="/c/msys64/mingw64/bin:$PATH"` first).
- Sidecars current for `f2beae4b`. Regen after any converter change:
  `python tools_pc/d43_emit.py ntsc-final && python tools_pc/d69_emit.py ntsc-final && python tools_pc/d88_emit.py ntsc-final --regen`
- Repro: `./build-pc/ge007.x86_64.exe -level_33` (Dam). Crash log
  `ge007.crash.log` at repo root. Symbolicate:
  `addr2line -e build-pc/ge007.x86_64.exe -f -C 0x<PC>` (PATH first;
  image base 0x140000000, PC in log is absolute).
- Frame capture `GE_PCDUMP="80-260:40"` → `./ppm/`; `python tools_pc/pixcount.py ppm/<last>.ppm`.
- **`timeout` does NOT kill the game on Windows.** Launch `... &`,
  `sleep 24`, `taskkill //F //IM ge007.x86_64.exe`.
- gdb attach is fast: game running via `nohup ... &`, then
  `gdb -batch -x cmds.txt -p <winpid>` (winpid = 4th col of `ps -p <bashpid>`).
  Hardware watchpoint on the chr's `aiList` field, or on the bad `self`,
  catches the corrupting write.
- PD analogue: check `PD_PORT_CHECKOUT` chr / guard
  setup + its propdef converter before writing anything new.

## BUDGET
Structural → **~12 build→run→inspect cycles or ~90 min.** On expiry:
revert temp probes (leave only committed env-gated ones), write up spec
progress + best hypothesis in §F with an explicit confidence rating, list
every file you touched. A good half-solved write-up is a deliverable.

## CONSTRAINTS
No game-logic / control-flow changes. ABI/layout/format only, each
`#ifdef PORT` with the N64 line verbatim under `#else`, each documented in
§F. Prefer converter extension over runtime fixup. Revert your own temp
probes before reporting. Do not touch other tracks' uncommitted edits
(none expected).

## REPORT
1. Root cause — `file:line` evidence, which hypothesis, why the 6 differ
   from the 7 passing levels.
2. Fix — the diff, or why-not if unresolved.
3. Verification — `-level_33/35/26/22/29/41` each boot to ≥1
   non-degenerate frame (`pixcount.py` > a few %), AND `-level_09` +
   `-level_20` unregressed (`tools_pc/framediff.py`).
4. Probes left in the tree (env-gated, capped).
5. Confidence rating.
6. Generalisable quirk appended to `docs/PORT-LEARNINGS.md`.
