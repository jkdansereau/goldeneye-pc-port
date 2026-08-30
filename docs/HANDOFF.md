# HANDOFF — GoldenEye 007 PC port (Phase 2, breadth-first level sweep)

Tier 1 doc. Current state + next task + environment. Finding history is
`docs/PCPortResearch.md` §F/§H (indexed at top of §F); session narrative
is `docs/HANDOFF-ARCHIVE.md`.

## Preflight (do not skip because you have a "next step")

1. `CLAUDE.md` (auto) + `AGENTS.md` non-negotiables.
2. This file, top to bottom.
3. `docs/PORT-LEARNINGS.md` — recurring bug classes (you WILL re-derive
   catalogued bugs otherwise).
4. `docs/PLAN-linear-level-sweep.md` — the plan of record.
5. Dispatching a subagent? `docs/AGENT-WORKFLOW.md` first — every brief
   needs FILES / BUDGET / ON-EXPIRY / CONSTRAINTS / REPORT.

## Direction (2026-08-29)

**Breadth-first pivot.** Stop the depth-first BUNKER1 + graphics
rabbit-holes. Instead:

1. Make boot → intro → menu → level-select **functional (not crashing)** —
   cosmetics parked in `docs/GRAPHICS-BACKLOG.md`.
2. Sweep all **21 solo levels** for load + render + no-crash →
   `docs/LEVEL-STATUS.md`.

Full workstream breakdown (WS1–WS6) in `docs/PLAN-linear-level-sweep.md`.

## Where things stand

- **BUNKER1 (`-level_09`) is playable-ish.** Loads, renders recognisably
  (textured rooms, storage racks, floor; skeletal guards render as
  humanoids), and **survives a guard firefight** — 45 s+ crash-free
  (D103–D120). **Silo (`-level_20`) also loads + renders clean.**
  Committed through `f2beae4b` (M-12: D121 WS1 boot, D122 propDef fix).
- **Input layer (Phase 3) landed and playtested** (D118, M-9/M-10).
  `port/src/input.c` is real: keyboard+mouse + SDL_GameController → N64
  pad. Core aim/move works. **Open polish bugs (deferred):** D118a mouse
  yaw slower than pitch; D118b mouse-Y inverted; rebinding / gamepad
  hotplug / `ge007.ini` generation still TODO.
- **Recent fixes (M-10, `d1e93b76`):** D119 guard-attack crash
  (`weapons_held[]->chr` type-pun) fixed; D120 blood-stain hang guarded
  (not fixed — `d43_emit.py` opcode-0x18 converter gap).
- **Cosmetic backlog (parked, `docs/GRAPHICS-BACKLOG.md`):** D75 front-end
  3D model transforms (Nintendo logo misplaced, gun-barrel Bond absent,
  cast models absent), D76 disclaimer screen partial, D77 no audio,
  D114/D116 HUD/text X-mirror (**DO NOT re-static-trace** — see
  PORT-LEARNINGS §D2), D74 dead wrap-block.

## Done this session (M-12) — 2 commits

- **WS1 / D121 (`02c12068`)** — bare `./build-pc/ge007.x86_64.exe
  -level_XX` auto-injects that stage's `memallocstringtable[]` `-m*` pool
  row (`#ifdef PORT` in `boss.c bossInitMainthreadData`). No manual `-m*`
  args any more. Gotchas recorded: setting `g_DebugAndUpdateStageFlag=1`
  is the WRONG fix (routes boot through the title-stage intro), and
  `tokenSetString` *replaces* the whole token buffer so the injected
  string must carry `-level_XX` forward. See §H D121.
- **D88.4 was already resolved** (committed `ff563812`/`4ccc0d3c` weeks
  ago). The "OPEN — cross-level blocker / `setupDoor` crash" note in the
  old §F index / plan was **stale**. `d88_emit.py --regen` = 21/21 pass.
- **D122 (`f2beae4b`)** — the real cross-level blocker was prop/item
  **model loading**. `tools_pc/d88_propdefs.py` had a per-type handler
  table + a generic fallback; 6 `inherits ObjectRecord` propDef types
  (47 TINTED_GLASS, 39 VEHICHLE, 40 AIRCRAFT, 45 TANK, 13 AUTOGUN, 20
  AMMO/MultiAmmoCrate) had **no handler** → fell to the generic
  word-granular `bswap32`, which swapped the `[s16 obj][s16 pad]` word as
  one u32 → model id `obj` in the wrong half → OOB `PitemZ_entries[]`
  deref → crash in `modelLoad` (`loadobjectmodel.c:393`) /
  `modelInitRwData` (`model.c:6249`). Fixed in the converter
  (`OBJ_TAIL_DESC` handler) + matching `sizepropdef()` `#ifdef PORT`
  stride. BUNKER1/Silo were never affected (they don't emit those types).
  Full write-up: §F/§H **D122**. Confidence: crash fix high; types
  39/40/45 tail layout medium (structs have "locs unconfirmed" notes —
  nudge if a level actually drives a tank/vehicle).

**Uncommitted docs** (tangled with the in-progress big docs restructure —
`HANDOFF.md`, `AGENTS.md`, `CLAUDE.md`): the D121 + D122 entries in
`docs/PCPortResearch.md` §F/§H and this file's edits. Commit or fold in.

## Done this session (M-17) — 4 commits, 13→18/21 PASS

- **D126** (`e851e2ab`) — objective sub-records `criteria_picture` (30),
  `criteria_roomentered` (32), `criteria_deposit` (33),
  `setup_objective_text` (35) each end in a `T *next` list pointer that
  `set_parent_cur_obj_*` / `setup_briefing_text_entry_parent` write while
  walking the setup stream. Widens 4→8B and 8-aligns at offset 16 on PC →
  struct is 24B/6w (N64 16/20); the old N64-sized emit meant the runtime
  8-byte `->next` store clobbered the *next* propdef record's header →
  walk desync → command indices drifted ~100 → `setupDoor` `linkedDoor`
  resolution landed on the wrong record. Fixed `d88_propdefs.py`
  (`PROPDEF_PC_BYTES[30/32/33/35]=24` + typed handler) +
  `loadobjectmodel.c` `sizepropdef` PORT returns 6. **Cleared C3r Bunker2
  + C4 Depot + C6 Surface2** in one change.
- **D127** (`1129f63d`) — `sndPlaySfx` guards a bogus `ALSound*` (the
  converted libaudio bank has fewer/rearranged `soundArray` slots than
  N64; audio is Phase-3 parked). **Cleared C7 Surface1.**
- **D128** (`749feb9f`) — `sub_GAME_7F0B37EC` special-portal marker used
  the hardcoded N64 8-byte `bg_portal_data_entry` stride / `controlbytes1@6`;
  on PC the struct is 16B with `controlbytes1@10`, so `|= 2` scribbled
  into the middle of a portal's `offset_portal` pointer → `bg.c:5723`
  crash on any level with a `specialportalarray` entry. `#ifdef PORT`
  uses the struct accessor. **Cleared C5 Control.**
- **D129** (`<this commit>`) — `langGet` bounds-checks the slot id and
  resolved bank pointer. Bare `-level_XX` boots that reach the
  cast/credits text path (Cuba, `bondviewRenderCredits`, D76 area)
  reference banks the menu flow never loaded. Reduces the Cuba bare-boot
  end-credits crash; **Cuba itself loads + renders 300+ frames fine** and
  the credits work through the real front-end flow.

## Done this session (M-18) — 1 fix, 18→20/21 PASS

- **D130** (`port/src/romdata.c`, uncommitted) — Facility `-level_34` +
  Runway `-level_35` C2 crash (`import_texture_i8` AV on wild ptr
  `0x72181ee8`). Root cause was NOT the model-GDL relocation the
  D124-Facility addendum / `BRIEF-C2gdl-model-reloc.md` suspected —
  disproved: `texLoadFromGdl` never copies a `G_SETTIMG` on these levels,
  `texWriteLoadToTmem*` is never called, and `gdl` in `sub_GAME_7F0762E0`
  is still a segmented `0x05xxxxxx` value so its `& 0x00ffffff` masks are
  correct. Real cause: **`romdataFixupFont` corrupts BankGothic/ZurichBold
  glyph indices 0/1/2** — the in-place N64 24B → PC 32B `fontchar` relayout
  aliases `dst`/`src` for low glyph indices (stride grew 8B < the 24B field
  read span), so a field write clobbers a later field's source mid-loop
  (glyph 1 `width` = `bswap(index)`, glyph 2 `pixeldata` ≈ `0x020002e8` →
  `+= font_base` → wild). Facility's title "Chemical Warfare Facility #2"
  renders `#` → `gDPLoadTextureBlock(gdl, curchar->pixeldata=wild, …)` →
  fast3d AV. Fix: stage all 6 N64 fields in a local `f[6]` before writing.
  §F/§H **D130**; PORT-LEARNINGS §A. Verified: -level_34 0/~14, -level_35
  0/4; -level_09/20/24 unregressed.

## Done this session (M-19) — D131 (Jungle), D63 scaffolding removal

- **D131** (`port/fast3d/gfx_pc.cpp`) — Jungle `-level_37` C2m crash.
  `explosionRenderPropSmoke` passes a compiled `.bss` matrix symbol
  (`&dword_CODE_bss_8007A100`) through `osVirtualToPhysical()` — a
  `u32`-returning shim — into `gSPMatrix`, truncating the `0x1_00000000`
  module high word → w1 = `0x401c68e0` → wild deref in `gfx_sp_matrix`
  when the first explosion draws (~frame 300). NOT `gimgfixup` / the
  model-GDL path (that premise was already dead per D130). Fix: `seg_addr`
  restores the module high word for a fallthrough w1 in
  `[0x40000000, 0x70000000)`. Covers ~30 latent `osVirtualToPhysical(<compiled
  matrix/vtx symbol>)` sites (explosion/glass/blood/bondview2). Verified:
  Jungle renders to frame 1500–2400+ crash-free; `-level_20`/`-level_24`
  unregressed. §F/§H **D131**, PORT-LEARNINGS §A.
- **D63 scaffolding removed** — dead TEMP D63 probe calls stripped from
  `port/src/libultra.c` (watchdog thread + activity ring + slot watchers)
  and the `#ifdef PORT` probe blocks in `blood_animation.c`, `dyn.c`,
  `front.c`, `rsp.c`, `title.c`. All inert (env-gated / no side effects);
  build links clean. `port/fast3d/gfx_pc.cpp` D63 trail code left for a
  later pass. Bug class D24-implications + `osYieldThread` watch item added
  to PORT-LEARNINGS §E.

## Next task

1. **`-level_09` (BUNKER1) boot crash — DISPROVEN as a regression** (M-20,
   coordinator: 3 runs / 1800 frames crash-free on `1fc3cff6`; re-confirmed
   M-20 this pass: 690+ frames at 91.7% coverage). Was sweep flakiness /
   D117 nondeterminism under machine load. Do not investigate.
2. **Intro "renders mostly black" is NOT a regression — it is the known
   D75/D76 parked-cosmetic steady state** (M-20 / **D133**). Verified by
   building M-17 (`9ec6121e`, whose handoff claimed "the entire intro
   renders") in a scratch worktree and capturing the same `GE_PCDUMP`
   window: coverage is **pixel-identical** to HEAD (legal screen 6677
   non-clear px = 2.17% both builds; logo-ish frames ~7%; black gaps in
   between). The 2D/text layers draw; the animated character-model layers
   (Nintendo-logo transform, gun-barrel Bond, cast models) never appear —
   exactly D75. The M-17 "entire intro renders" handoff line was
   aspirational, not a measured state. **Parked below level/crash work
   (`docs/GRAPHICS-BACKLOG.md`).** Silo (#3, fly-down hang) is the same
   class or D117 nondeterminism — not a fresh regression.
3. **Facility `-level_34` + Jungle `-level_37` re-verify FAILED this pass**
   (M-20, machine lightly loaded). Facility: boot crash `frames=0`, PC
   `0x1400c3b77` (addr2line unresolved). Jungle: renders frames 1–2 then
   kernel-heartbeat hang ("no frame rendered", `frames=2`). Both were
   claimed PASS in M-18/M-19 (D130/D131). **Needs a clean-machine
   re-verify** before deciding regression vs. flakiness — see
   `docs/LEVEL-STATUS.md`. `-level_09`/`-level_20` still PASS, so the
   capture harness is sound.

After that: all 21 load+render → hand `docs/LEVEL-PLAYTEST.md` to the
user for the WS6 completion pass (real input, per-level objective
checklist).

**Regen gotcha:** `d69_emit.py` rewrites `data/pccg-<r>/pccg.bin` from
scratch with only the 52 bg/stan rows — you MUST follow it with
`d88_emit.py --regen` to re-add the 21 `Usetup*Z` rows, or every level
falls back to the raw N64 setup and crashes identically in
`proplvreset2`. Always run the full `d43 && d69 && d88 --regen` chain.

Sweep note: `level_sweep.sh` was flaky this session (spurious NO-FRAMES on
known-good levels under machine load / the 24 s watchdog + D117
nondeterminism). Verify individual levels with a 35–45 s window and
`GE_PCDUMP="60-500:40"` when a sweep row looks wrong.

Sweep runner: `tools_pc/level_sweep.sh` (bare `-level_XX`, `GE_PCDUMP="80-260:40"`,
24 s watchdog, `taskkill //F //IM ge007.x86_64.exe`; **`export PATH=".../mingw64/bin:$PATH"`
before `addr2line`** — the script's own symbolication no-op'd without it).

The M-13 matrix below is partially superseded by M-14 (see §H D125 for the current
12/21 status) — treat §H as authoritative.

| Class | Levels | Site |
|---|---|---|
| **C1** | Dam 33, Runway 35, Frigate 26, Statue 22, Streets 29, Cradle 41 | `chrIsNotDeadOrShot` chraction.c:4483 — `self` = AI-list rodata ptr |
| **C2** | Facility 34, Jungle 37 | `import_texture_i8`/`gfx_tex_normalize_source` — bad tex ptr |
| **C3** | Aztec 28, Bunker2 27 | `propobj.c` door model / `linkedDoor` walk |
| **C4** | Depot 30 | `prop.c:902` `sp4C->room` after `walkTilesBetweenPoints` |
| **C5** | Control 23 | `bg.c:5723` `portal_pts->numPoints` |
| **C6** | Surface2 43 | `loadobjectmodel.c:393` `PitemZ_entries[modelid].header` (D122 cont.) |
| **C7** | Surface1 36 | `snd.c:653` `sndSetupSound` (audio — parked subsystem) |

**Done M-13:** C1 fixed (**D123**, `tools_pc/d88_propdefs.py` — the C1
crash was D122 fallout: widened `Vehichle/AircraftRecord.ailist` slot
zeroed instead of carrying its read-before-write int id). C2 split:
Jungle's texture crash fixed (**D124**, `port/src/gimgfixup.c` — compiled
explosion-DL sync keyed on an already-erased marker); Facility+Runway
diagnosed (model-GDL relocation misaligns `dst`, `objecthandler_2.c` /
`texLoadFromGdl`, D80/D82/D83 area) — NOT fixed.

Full re-sweep after both: **12 / 21 PASS** (`docs/LEVEL-STATUS.md`).
9 crashes remain in 6 classes: **C2** Runway+Facility (model-GDL align —
biggest), **C3** Aztec+Bunker2 (`docs/BRIEF-C3-C6-prop-model.md`),
**C2m** Jungle (explosion-DL `G_MTX`), **C6** Surface2 (`PitemZ` modelid),
**C5** Control (BG portal), **C4** Depot (BG tile/room), **C7** Surface1
(`sndSetupSound`). Priority order + files in `docs/LEVEL-STATUS.md` "Next".

Uncommitted M-13 edits: `tools_pc/d88_propdefs.py`, `port/src/gimgfixup.c`,
§F/§H D123+D124, `docs/LEVEL-STATUS.md`, `docs/LEVEL-PLAYTEST.md`,
`docs/PORT-LEARNINGS.md`, `tools_pc/level_sweep.sh`, the BRIEF-C* files,
this file. Fold into the docs-restructure commit set.

Re-run `tools_pc/level_sweep.sh` after each fix; keep `-level_09` +
`-level_20` green (`framediff.py`). All-21-PASS → hand the user
`docs/LEVEL-PLAYTEST.md` for WS6.

**Verification per fix:** target level boots to ≥1 non-degenerate frame
(`tools_pc/pixcount.py` > a few %), AND `-level_09` + `-level_20`
unregressed (`tools_pc/framediff.py`). Regen sidecars after any converter
change: `python tools_pc/d43_emit.py ntsc-final && python
tools_pc/d69_emit.py ntsc-final && python tools_pc/d88_emit.py ntsc-final
--regen`.

**Parked (do NOT start here):** WS2 front-end menu playtest (needs a
human driving; cosmetics D75/D76/D77/D114/D116 all out of scope —
`GRAPHICS-BACKLOG.md`). Audio (Phase 3), saves (Phase 4).

## Environment / build

```sh
export PATH="/c/msys64/mingw64/bin:$PATH" && ./build-pc.sh ntsc-final   # ~5 s
```

Run from the **repo root**, not `build-pc/`.

- **Repro (BUNKER1, skips attract mode):**
  `./build-pc/ge007.x86_64.exe -level_09`
  (per-level `-m*` pool sizes are now auto-injected — D121/WS1; pass them
  by hand only to override). `boss.c:337` decodes `-level_XX` as
  `d0*10 + d1 - 0x210`; LEVELID == the `-level_XX` number.
- **Sidecar regen** (after any converter change; `data/` is gitignored):
  ```sh
  python tools_pc/d43_emit.py ntsc-final && \
  python tools_pc/d69_emit.py ntsc-final && \
  python tools_pc/d88_emit.py ntsc-final --regen
  ```
  If `data/` is missing: `cp baserom.u.z64 data/ge007.ntsc-final.z64`
  first (see §F "`data/` deletion + recovery").
- **Frame capture:** `GE_PCDUMP="<start>-<end>:<stride>"` → `./ppm/`
  (gitignored). Analyse with `tools_pc/pixcount.py` (non-black content
  check) and `tools_pc/framediff.py <ppmdir>` (structural regression vs
  `tools_pc/golden/` — the port is **NOT frame-deterministic**, D117; use
  `--mask` for the HUD, not an exact compare). `tools_pc/ppm2bmp.py` to
  view.
- **Crashes:** `ge007.crash.log` (repo root). Symbolicate
  `addr2line -e build-pc/ge007.x86_64.exe -f -C <pc>` (image base
  `0x140000000`). Frames past the true chain may be stale (D56).
- **gdb:** launch mode is too slow for timing-dependent faults. **Attach**
  mode is fast: `gdb -batch -x cmds.txt -p <winpid>` (`<winpid>` = 4th
  column of `ps -p <bashpid>`; game must already be running, e.g. `nohup
  … &`). A hardware watchpoint on a global catches a bad write in < 1 min.
- Standalone probe compiles need `-std=c11`.

## Non-negotiables (full list in AGENTS.md)

1. N64 build files (`Makefile`, `tools/`, `rsp/`, `ld/`) untouched.
2. Game logic unmodified except narrow, documented `#ifdef PORT`
   ABI/layout/format exceptions — each logged as a Dxx in §F/§H with the
   N64 line kept verbatim under `#else`. Anything beyond a narrow,
   obviously-correct exception: **stop and write it up** with a confidence
   rating, don't hack it in.
3. Prefer an **offline sidecar converter** (`tools_pc/d*_emit.py`,
   D43/D69/D88 pattern) over a runtime fixup for a whole ROM-asset format.
