# BRIEF C2-GDL — model/prop GDL runtime relocation misaligns dst (Facility, Runway)

Repo: `REPO_ROOT` (git `master`, Windows).

## READ FIRST
- `docs/HANDOFF.md` top, `docs/PORT-LEARNINGS.md` **§B (16-byte PC Gfx vs
  8-byte N64 — this is the class)** + §D.
- `docs/PCPortResearch.md` §F **D124** (the C2 split — this brief is the
  unfinished Facility half), **D80, D82, D83, D85, D95, D58, D50.6**
  (Gfx-stride / GDL history — jump via §F index).
- `AGENTS.md` non-negotiables. `rsp/graphics/gmain.s` = GBI ground truth.
- PD analogue: `PD_PORT_CHECKOUT` — its model-GDL /
  texture relocation path. Check before writing anything.

## SYMPTOM (WS4 re-sweep — `docs/LEVEL-STATUS.md`)
Facility `-level_34` (fault 0x72181ee8) and Runway `-level_35` (fault
0x721b8ee8) both crash in `import_texture_i8` (`port/fast3d/gfx_pc.cpp:821`)
— fast3d fed a garbage `G_SETTIMG` w1.

Per the D124 investigation: the bad `G_SETTIMG` (`fmt=4 siz=2 w=0`,
preceded by an unconsumed `0xba` GE tex-macro opcode) sits in a
model/prop GDL relocated at runtime by `texLoadFromGdl()` via
`sub_GAME_7F0762E0` (`objecthandler_2.c:82`) — NOT a room GDL, NOT the
global bank. A `GE_C2` probe showed `texLoadFromGdl` writing to
**non-16-byte-aligned `dst`** (`0x701eac01`, `0x701eb7d5`, …): the
`replacementgdl` / `name`(=srcsize) offset arithmetic still mixes N64
8-byte and PC 16-byte `Gfx` strides, so converted commands land mid-slot
and a subsequent `G_SETTIMG` w1 reads as garbage.

12 levels PASS — they either don't hit this model-GDL path or hit it with
a GDL whose misalignment happens to be benign.

## TASK
Pin the exact stride/offset error in the model-GDL runtime relocation and
fix it narrowly (`#ifdef PORT`, N64 line under `#else`, §F-documented).
Likely spots:
- `src/game/tex.c` `texLoadFromGdl()` — src/dst pointer advance per Gfx.
- `src/game/objecthandler_2.c` `sub_GAME_7F0762E0` — `replacementgdl` /
  `srcsize` math, GDL buffer sizing.
- `src/game/model.c` `modelNodeReplaceGdl` / rwdata GDL slot stride.
- The `0xba` (`G_GEGBIMACRO_*` / GE tex-load macro) decode — fast3d may
  need to consume its extra word (cf. gmain.s).

Use the existing `GE_D86` / model-node probes and add a capped `GE_C2GDL`
probe if needed.

## FILES YOU MAY TOUCH
`src/game/tex.c`, `src/game/objecthandler_2.c`, `src/game/model.c`
(narrow `#ifdef PORT`); `port/fast3d/gfx_pc.cpp` (only a guard / the 0xba
consume, if that's the cause); new `tools_pc/c2gdl_*` probes;
`docs/PORT-LEARNINGS.md`, `docs/PCPortResearch.md` §F (extend D124 or new
Dxx), `docs/LEVEL-STATUS.md`.
**NOT** `src/game/propobj.c` / `prop.c` / `tools_pc/d88*` (C3 agent owns
those), **NOT** `port/src/gimgfixup.c` (D124-Jungle, done).

## KNOWN-GOOD / RULED OUT
- D123 (C1), D124-Jungle (`gimgfixup.c`) — landed, work.
- 12 levels PASS — the common GDL path is fine.
- Global-bank / explosion-DL sync (D124) — separate, fixed.
- HUD/text X-mirror (D114/D116) — parked, do NOT re-trace.

## PRE-FLIGHT
- `export PATH="/c/msys64/mingw64/bin:$PATH" && ./build-pc.sh ntsc-final` — green.
- Repro `./build-pc/ge007.x86_64.exe -level_34`. `ge007.crash.log`;
  `addr2line -e build-pc/ge007.x86_64.exe -f -C 0x<PC>`.
- `GE_PCDUMP="80-260:40"` → `./ppm/`; `python tools_pc/pixcount.py`.
- `timeout` does NOT kill on Windows: `... &`, `sleep 24`,
  `taskkill //F //IM ge007.x86_64.exe`.
- gdb attach fast (`gdb -batch -x cmds.txt -p <winpid>`). HW watchpoint on
  the `dst` cursor in `texLoadFromGdl` catches the mis-advance.
- Regen sidecars after any converter change (none expected here):
  `python tools_pc/d43_emit.py ntsc-final && python tools_pc/d69_emit.py ntsc-final && python tools_pc/d88_emit.py ntsc-final --regen`.

## BUDGET
Structural / shared-infra → **~14 build→run→inspect cycles or ~2 h.** On
expiry: revert temp probes, write up the stride spec + best hypothesis in
§F with a confidence rating + files touched. A good half-solved write-up
is a deliverable.

## CONSTRAINTS
No game-logic / control-flow changes. ABI/layout/format only, `#ifdef
PORT` with N64 line under `#else`, §F-documented. Revert your own probes.
Don't touch the C3 agent's files.

## REPORT
1. Root cause — `file:line`, the exact stride/offset bug, why 12 levels
   are unaffected.
2. Fix diff or why-not.
3. Verification — `-level_34` + `-level_35` boot to ≥1 non-degenerate
   frame (`pixcount.py` > a few %); `-level_09` + `-level_20` unregressed
   (`framediff.py`); re-check Jungle `-level_37` + Streets `-level_29`
   (may improve).
4. Probes left in tree.
5. Confidence rating.
6. Generalisable quirk appended to `docs/PORT-LEARNINGS.md`.
