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

## Diagnostic env probes

All `GE_*` env-var probes in the tree are catalogued in
**`docs/GE-ENV-PROBES.md`** (var → file:line → what it does → live/dead).
Live tooling: `GE_PCDUMP`, `GE_INPUTSCRIPT`, `GE_STARTMENU`, `GE_UNLOCK_ALL`,
`GE_INPUTLOG`, `GE_SAVELOG`, `GE_D160`, `GE_DTEX`, `GE_TEXDUMP`. The rest are
closed-finding `GE_Dxx` scaffolding (strip candidates; all env-gated and inert,
except `bg.c`'s `GE_D63` lines which are not `#ifdef PORT`-guarded).

## Where things stand

- **BUNKER1 (`-level_09`) is playable-ish.** Loads, renders recognisably
  (textured rooms, storage racks, floor; skeletal guards render as
  humanoids), and **survives a guard firefight** — 45 s+ crash-free
  (D103–D120). **Silo (`-level_20`) also loads + renders clean.**
  Committed through `f2beae4b` (M-12: D121 WS1 boot, D122 propDef fix).
- **Input layer (Phase 3) landed and playtested** (D118, M-9/M-10).
  `port/src/input.c` is real: keyboard+mouse + SDL_GameController → N64
  pad. Core aim/move works. **M-24 mouse-look rework** (`port/src/input.c`,
  port-only): mode-aware map — aim mode (RMB) drives the analog stick past
  ±60 and emits no C-buttons; hipfire keeps digital C-up/C-down pitch.
  **D118b (mouse-Y inverted) and D118c (aim + mouse-down → crouch) FIXED**;
  **D118a residual** — hipfire pitch still digital vs analog yaw (minor).
  `config.c` INI load/save now implemented → `ge007.ini` is written on
  first run and re-read; `[Input]` `MouseEnabled` / `MouseAimSpeed` (50) /
  `MouseTurnSpeed` (100) / `MouseInvertY` (0) are live-tunable. Rebinding /
  gamepad hotplug still TODO. Weapon switch on kbd = A button
  (`Space`/`Z`/`E`) — no dedicated key.
- **Recent fixes (M-10, `d1e93b76`):** D119 guard-attack crash
  (`weapons_held[]->chr` type-pun) fixed; D120 blood-stain hang guarded
  (not fixed — `d43_emit.py` opcode-0x18 converter gap).
- **Cosmetic backlog (parked, `docs/GRAPHICS-BACKLOG.md`):** D75 front-end
  3D model transforms (Nintendo logo misplaced, gun-barrel Bond absent,
  cast models absent), D76 disclaimer screen partial, D77 no audio,
  D114/D116 HUD/text X-mirror (**DO NOT re-static-trace** — see
  PORT-LEARNINGS §D2), D74 dead wrap-block.

## Done this session (M-32b) — tactical follow-up (2026-08-31)

3 commits on `master` (`49ce620a`, `7071b110`, + this reconciliation).

- **Debug-scaffolding strip (`49ce620a`).** Removed inert env-gated
  `GE_D54`/`GE_D63`/`GE_D90` probe blocks from compiled `src/`
  (`memp.c`, `bg.c` incl. the bare non-PORT `d63bgprimarycount` static +
  entry log, `blood_animation.c`, `dyn.c`, `rsp.c`, `bondview_r.c`,
  `csplayer.c`, `load.c`, `seqplayer.c`; 161 lines). `GE_D54`/`GE_D90` now
  fully gone; `GE_D63` partially (remaining blocks all `#ifdef PORT` +
  getenv, inert — strip candidates for later). Build green; `-level_09` +
  `-level_20` render crash-free (19 frames each). `docs/GE-ENV-PROBES.md`
  updated.
- **D75 Bug 2 runtime probe (`7071b110`).** `GE_D75=1` in `title.c`
  `sub_GAME_7F007F30`. Booted bare front end through the gun-barrel:
  Bond + gun models absent; **both Model instances valid** (nMtx 21/1);
  **`render_pos` valid + fresh each frame** (== that frame's `dynAllocate`
  mtxlist base, clean double-buffer, o2p unchanged) → the "stale arena"
  hypothesis (D115 #5) is **RULED OUT** — do not land a persistent
  `render_pos` buffer. **Zero fast3d DL warnings** → not D144/D146 either.
  Failure is downstream in `drawjointlist`/`dotube` vtx/node-DL resolution
  or an off-screen `basemtx`. Next: a drawjointlist-level probe. Full
  write-up: §F "D75 Bug 2 — RUNTIME PROBE".
- **RC3 Depot eyeball — INCONCLUSIVE, default stays OFF.** Captured Depot
  (`-level_30`) with `GE_WRAPFIX=1` vs `0`. Depot is too dark in the
  boot-camera area to judge the non-PoT (65×65 / 96×48) ceiling/panel
  surfaces the fix targets; the visible corrugated-container walls look
  unchanged and un-regressed with the fix on. Not enough signal to flip
  `cfgWrapFix=1`. Needs a **live human eyeball** on Depot with
  `Video.WrapFix=1` (walk to a lit area / the ceiling), or a `GE_DTEX`
  capture on the specific surface. `port/src/video.c` `cfgWrapFix` unchanged.
- **Sidecars regenerated** (`d43_emit.py` / `d69_emit.py` / `d88_emit.py
  --regen`, ntsc-final) — ready for a campaign playtest.

### Next (M-32b)
- **RC3** — human eyeball Depot with `Video.WrapFix=1`; flip `cfgWrapFix=1`
  in `port/src/video.c` if a lit non-PoT surface is visibly better and
  `-09/-20/-34` don't regress.
- **D75 Bug 2** — drawjointlist/`dotube`-level probe (resolved vtx/nodeDl
  ptrs, whether any tris emit, composed `basemtx*render_pos` for joint 0).
- **D143** blank briefing/objective text — untouched this session.
- **D154 / D152+ / D160** — still playtest-gated.

## Done this session (M-31/M-32) — 6-agent parallel burst #2 (2026-08-31)

6 worktree subagents, files partitioned; 5 merged to `master`
(`0c918ab4`..`1753ac7a` cherry-picked, then a reconciliation commit).
Combined tree builds clean (240/240); `-level_09`/`-level_20` render
crash-free (framediff phash = documented D117 intro-pan noise, nonclear
coverage stable to 0.04pp); 6/6 texture-heavy levels (Depot/Facility/
Runway/Archives/Streets/Caverns) boot+render+no-crash.

| Item | Result | Verified |
|---|---|---|
| **D154** (`bg.c`) | Existing wall-shoot GBI-parser port was mostly right, but `vtxoff = gdl->dma.par & 0xf` was **always 0** — the PC `Gdma_le` shim maps `.par` to word0 bits 0-23 (packed length), not the N64 params byte. Fixed → `((u32)gdl->words.w0 >> 16) & 0xf`. Also ported a sibling raw parser: `bgTestBulletHitBackground` G_SETTILE back-scan (~3841). `GE_D154=1` diag added. | build 242/242; **playtest-gated** (needs firefight into a wall on an idle machine) |
| **D152+** (`snd.c`, `libultra.c`) | Audit: every compiled-audio `osSetIntMask(OS_IM_NONE)` is **balanced** — the §F "unbalanced early-return" guess was wrong. Real fixes: `sndSetSfxSlotVolume` now holds the mask across its `ALSoundState` walk (matches its twin `sndDeactivateAllSfxByFlag`) + `sndApplyVolumeAllSfxSlot` batches its loop under one recursive hold (kills the mission-failed fade lock-storm); `portThreadWrapper` → `imThreadExitRelease()` releases an orphaned lock on thread exit (kills the transient-thread-died leak + pthread-id-reuse re-wedge). Steal-lock kept as backstop. | build 240/240; `-level_09/-20` crash-free; **fade-out repro playtest-gated** |
| **RC3 / D167** (`gfx_pc.cpp`) | fast3d never stored the N64 tile `mask` — wrapped every repeating texture at GL image size, not `1<<mask` (wrong period on Depot's 65×65 / 96×48 surfaces). Fixed behind `Video.WrapFix` (**default OFF** = byte-identical to golden). `GE_WRAPFIX=0/1` override. | per-level captures clean, no regression; **needs a human eyeball on Depot with `Video.WrapFix=1` before default-on** |
| **D75** (docs) | Option (a) — D73 gu/float-endian scope gap — **ruled out** (`gu/*` + `matrixmath.c` fully endian-clean; Rareware logo exercises the whole path and renders fine). Two independent bugs: Bug 1 (logo misplaced, photo 180°) = the parked D114/D116 fast3d mirror; Bug 2 (gun-barrel/cast models **absent**) = independent, likely `model->render_pos` → transient `dynAllocate` arena (D115 item #5). Needs a runtime probe. | write-up only |
| **D160 / D148** (docs) | "propDef command-index walk desync" hypothesis **DISPROVEN** — exhaustive static trace: every record type Dam emits has `converter PC bytes == sizepropdef()×4`, stream tiles byte-exact, walk in lockstep, all 21 levels clean. Dam rappel cutscene is runtime AI-script / `CAMERAMODE_POSEND` cinematic render (D75 family). `GE_D160=1` diag already ships. | needs live Dam-to-exit playthrough |
| **Docs** | `TEXTURE-GLITCH-ANALYSIS.md` §0 status table reconciled (RC2 FIXED / RC4 RETRACTED / D159+D161 FIXED / RC3 done-behind-knob); new `docs/GE-ENV-PROBES.md` (full env-var table); GE_D63 strip-safety checklist (all inert; only `bg.c:2876/2880` is a bare non-PORT `static` + entry log worth deleting for N64 hygiene). | — |

**Worktree-harness note:** 3 of the 6 agent worktrees were created from a
**stale commit** (`0a4b3bae`, ~30 behind); a mid-flight `git reset --hard
master` per agent recovered them. Watch for this on future bursts — check
each agent's HEAD in its first report.

### Next
- **D75 Bug 2** — runtime `GE_PCDUMP` + capped probe on the front-end
  animated-model `render_pos` / `dynAllocate` path (the one in-scope lead).
- **RC3** — eyeball Depot with `Video.WrapFix=1`, flip `cfgWrapFix=1` in
  `port/src/video.c` if clean.
- **D154 / D152+ / D160** — all playtest-gated; verify during the campaign
  playthrough (wall-shoot / mission-fail / Dam exit).
- Strip the `bg.c:2876/2880` bare D63 entry log.

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

## Done this session (M-23) — WS6 Facility playthrough: D135/D137/D138/D139 FIXED, D136/D140/D118c open

First real human WS6 playtest. User speed-ran Dam OK, then hit a chain of
crashes on Facility (`-level_34`) — each one a subsystem the 21-level sweep
never exercises (no weapon fire, no stage unload, no pause menu). Fixed four,
two new open, Facility now near-complete. **Committed `ec51d9c5`** (the
older D121/D122/D126/D128/D130/D131 write-ups are still uncommitted, tangled
in the docs-restructure — fold those separately).

- **D135 (FIXED)** `src/game/propobj.c` — firefight crash. `bgTestHitOnObj`
  (bullet-ray vs object triangle geometry, via `propobjFindHit` on every
  shot that resolves on an object model) is an **unported N64 GBI parser**:
  raw 8-byte-`Gfx` byte/word indices desync on the PC 16-byte `Gfx` stream →
  walks off the DL → AV `0x709ae02a`. Ported `#ifdef PORT` to `gdl->words.w0/
  .w1` shifts (mirrors PD `pd_port/src/game/bg.c:3635`). Second fault after
  the parser fix: `objHit` `propobj.c:9717` `% impact_sounds->thing2_len` int
  div-by-0 — the ported texnum-recovery (`*(s16*)(phys(w1)-8)`) returned a
  bogus `texturenum` → OOB `g_HitTypeSounds[]`. Fixed: PORT texnum branch
  always `-1` (`isnd_default`, safe; generic impact sound/decal, park w/ D77).
- **D137 (FIXED)** `src/game/gunfire.c` — right-mouse (raise crosshair) crash.
  `gunDrawSight` `s32 sp54` holds a `Gfx*` the whole fn (`texSelect`/
  `display_image_at_position` take `Gfx**`); 4-byte slot → `*(Gfx**)&sp54`
  splices adjacent stack → wild DL write in `texSetRenderMode`. `#ifdef PORT`:
  `sp54` is `Gfx *`. §A.
- **D138 (FIXED)** `src/snd.c` — the "1 room from the end" *freeze* (not a
  crash — kernel-heartbeat watchdog). `sndCreatePostEvent` →
  `alEvtqPostEvent` (`libultra/audio/event.c:110`) walks `evtq->allocList`;
  parked audio thread never drains it, `chrobjSndCreatePostEvent` posts per
  objTick per ambient-sound object, Facility is dense with machinery → the
  list walk stalls the main thread. `#ifdef PORT`: `sndCreatePostEvent` is a
  no-op until audio lands (D77). No gameplay effect.
- **D139 (FIXED, UNVERIFIED)** `src/game/cleanup_objects.c` — stage-unload
  crash (`lvlUnloadStageTextData` → `cleanupObjects` → `objFree`, `obj->prop`
  = packed-float garbage). Root cause: `cleanupObjects` walks propDefs with
  `(u8)obj[0]` for the type, which works only because the header word
  `[u16 extrascale][u8 state][u8 type]` is **big-endian** (type = low byte).
  On LE the low byte is `extrascale` → the walk never matches `PROPDEF_END`,
  runs off the blob, dispatches the switch on garbage → `objFreePermanently`
  on non-objects → crash. `#ifdef PORT`: use `((PropDefHeaderRecord*)obj)->
  type` (offset 3) like every other consumer (`sizepropdef`, proplvreset).
  **Not confirmed** — when the user hit pause to trigger a fast teardown
  test, the pause menu itself crashed first (D140). `-level_09` unregressed
  (91.65%). §F **D139**, PORT-LEARNINGS §C (BE header byte on LE).
- **D140 (OPEN)** — **pause menu crashes.** `maybe_mp_interface` →
  `bondviewRenderWatch` (`bondview2.c:8604`) → `bondviewTransformManyPos-
  ToViewMatrix(g_CurrentPlayer->field_23C = NULL, objheader->numMatrices=9)`
  → `matrix_4x4_copy(src=0x0)` AV. The watch model's per-node view-matrix
  cache (`struct player.field_23C`) is never allocated (or `field_23C` is at
  the wrong PC offset — `struct player` layout, `docs/AUDIT-M6-player-
  offsets.md`). Blocks the WS6 pause-menu / objective-status checks and the
  watch gadgets. D75 3D-model-transform family. Files: `src/game/bondview2.c`,
  `src/bondtypes.h` (`struct player`).
- **D118c (OPEN, low pri)** — in manual-aim mode, mouse-down → **crouch**.
  Mouse Y emits C-down; `bondview2.c:5351` maps C-down in `insightaimmode`
  to `crouchDown`/zoom as well as aim pitch. No input-layer-only fix; needs
  the deferred analog-aim `#ifdef PORT` hook (§F D118). Documented, parked.
- **New harness** `tools_pc/repro_gdb.sh <XX>` — nohup-launch a level, attach
  gdb, dump full `bt full` + locals on the first SIGSEGV/SIGFPE, rolling
  frame dump to `ppm/`. Needed because `ge007.crash.log`'s own backtrace is
  `#01 = null` (native unwinder fails). Also `build-pc/d136_cmds.txt` — a
  gdb cmd file that traces every `objFree` call (`type`/`obj`/`prop`/`model`)
  then catches the fault; the "last line before FAULT" localises D136-class
  walk-off bugs. **Attach the winpid via `ps -W | grep ge007.x86_64 |
  awk '{print $4}'`** — `repro_gdb.sh`'s own `ps -p $!` derivation is wrong.
- **Follow-up (not done):** the BG room-geometry hit-test in `bg.c`
  (`~3373-3646`, walks the D85-widened `ptr_expanded_mapping_info`) is the
  identical unported GBI parser to D135 — latent, fires on shooting
  walls/floor. Same mechanical port. §F D135 + PORT-LEARNINGS §B.
- **Controls** (for the new session's own playtesting): kbd — WASD/arrows
  move, `A`/`D` sidestep, mouse X turn (analog), mouse Y look (digital
  C-button), LMB/LCtrl fire, RMB/LShift aim, `Space`/`Z`/`E` = A (action AND
  weapon-switch — GE overloads it), `X`/`R`/`F` = B (reload), `Q` = L,
  `Enter`/`Tab` = Start, `Esc` quit. No dedicated weapon key.

## Done this session (M-24) — mouse-look rework + real INI parser + playtest QoL

Pre-playtest QoL, port-layer only, no `src/` / game-logic change.
**Full review sheet: `docs/M-24-QOL-REVIEW.md`** (rationale, per-change
risk, verification, what to check by playing). Committed `f3ec5170`,
`65ed0315`, `33506aee`.

- **`port/src/input.c` mouse-look rework.** Read `bondviewProcessInput` /
  `MoveData`: GE aim is **mode-dependent** — hipfire yaw = analog stick-X,
  pitch = digital C-up/C-down (stick-Y = move, no pitch); aim mode (R held)
  yaw+pitch = analog stick past ±60, and C-up/C-down there = crouch/lean/
  zoom, *not* aim. New map keyed on our own RMB/LShift (hold-to-aim proxy):
  aim mode pushes `stick_x/y` into the 61..80 band and emits **no**
  C-buttons; hipfire keeps digital C-pitch. GE's native pitch is inverted
  (C-up→look down) — hidden so mouse-down looks down; `MouseInvertY` flips.
  - **D118b FIXED** (mouse-Y inversion), **D118c FIXED** (aim+down→crouch —
    no `src/` hook needed, aim look no longer emits C-down).
  - **D118a residual**: hipfire pitch digital vs analog yaw. Minor (precise
    vertical aim is an aim-mode activity). Full fix = the deferred analog
    `#ifdef PORT` `bondview.c` hook.
- **`port/src/config.c` — INI load/save implemented** (was a stub that only
  logged TODO). Parses `$S/ge007.ini` (`[Section]` blocks, `Key = value`,
  `#`/`;` comments), clamps ints to registered bounds, writes defaults on
  first run, rewrites on clean exit. Unblocks live tuning of the mouse
  knobs + all future video options. Note: constructor-registered options
  present at `configLoad()` time (main.c:52) are captured; anything
  registered later would miss the first save.
- **New knob** `Input.MouseTurnSpeed` (hipfire yaw %, default 100), split
  from `MouseAimSpeed` (aim-mode %, default 50) since they feed different
  game curves.
- Verified: `-level_09` boots crash-free to 900+ frames @ 91.65% coverage
  (unregressed); `-level_20` crash-free; `ge007.ini` written then re-read
  with no unknown-key warnings. Build green (`ntsc-final`).
- **`port/src/video.c` QoL:** **F12** takes a screenshot
  (`ppm/shot_NNN.ppm`, dumped on the render thread via the existing
  `gfx_opengl_dump_bound_fbo`); window **focus loss frees the mouse**
  (`inputSetMouseGrab(0)` on `SDL_WINDOWEVENT_FOCUS_LOST`, re-grab on
  gain) so alt-tab works. `inputSetMouseGrab()` also zeroes the aim delta
  and suspends mouse reads while released.
- Alt-Enter fullscreen toggle already existed (`gfx_sdl2.cpp:299`).
- **Gamepad hotplug** — `SDL_CONTROLLERDEVICEADDED/REMOVED` → `inputRescanPads()`
  (close all + re-open). Caveat: the game latches `inputConnectedMask()` at
  `osContInit` (boot), so a pad added later merges into controller 0 for
  play but won't appear as a separate channel — plug it before launch for
  multi-pad.
- **Mouse-wheel = weapon cycle** — a notch queues a 2-poll A-button press;
  GE's default scheme (`invButtons = A_BUTTON`, `bondview2.c:5162/5326`)
  cycles the weapon forward on a bare A edge. Both wheel directions cycle
  forward (no clean backward input without the A+fire combo).
- **Window title shows live FPS** (`wmAPI->set_window_title`, ~1 Hz).
- **Still TODO (not started):** key rebinding, on-screen (in-render) FPS
  overlay, in-game options menu.

## Done this session (M-25) — 3 commits, port-layer QoL

Free-roam QoL pass, `port/` only, zero `src/` / game-logic change. Full
review sheet: **`docs/M-25-QOL-REVIEW.md`**. All defaults reproduce prior
behavior exactly.

- **`9ebed821` — `[Video]` ge007.ini knobs.** `Video.VSync` / `FpsCap` /
  `MSAA` (1/2/4/8) / `TextureFilter` (0 nearest, 1 bilinear) / `Fullscreen`,
  wired in `video.c videoInit`. Plus **config auto-migration**: `config.c`
  now rewrites `ge007.ini` once when the build registers a key the file
  never had (so a new section actually appears) — an up-to-date file with a
  user comment is left untouched.
- **`5a7a1035` — `tools_pc/playtest.sh`** takes a level *name*
  (`playtest.sh bunker2`) or number, `--list` prints the 21-level table, and
  a crash now auto-runs `addr2line` on the faulting PCs.
- **`863f436b` — `[Input]` ge007.ini knobs.** `MouseYScale`,
  `MouseSmoothing` (0 = off = today), `PadDeadzone` (7000), `PadTriggerPct`
  (23), `PadLookInvertY`.
- Verified per commit: build green, `-level_09` crash-free to frame 620 @
  91.6% (unregressed), `-level_20` crash-free.
- **Silo `-level_20` capture freeze — D117/D134 load sensitivity, NOT a
  real bug and not an M-25 regression.** Full-length captures froze at
  ~frame 320 (silo→Bond camera descent) twice, but only on a machine still
  loaded from the sweep (the pre-M-25 baseline froze the same way). On an
  idle machine the user drove Silo live past ~1800 frames, VI pacemaker
  healthy, no stall. See `docs/M-25-QOL-REVIEW.md`.

## Done this session (M-26) — port-layer QoL batch (A/B/C/E)

Low-risk QoL, `port/` only, zero `src/` change. All defaults reproduce
prior behaviour. Full review: **`docs/M-26-QOL-REVIEW.md`**.

- **A — save-on-exit + `[Window]` persistence.** `atexit(portAtExit)` in
  `main.c` (`videoSaveWindowState()` + `configSave()`), fires on every
  clean quit (`exit(0)` in `videoPumpEvents`); `abort()` paths skip it.
  New `[Window]` ini section (`Width/Height/X/Y/Maximized`, sentinels =
  old behaviour) in `video.c`; `Video.Fullscreen` now round-trips.
- **B — `level_sweep.sh` STALLED verdict.** Frames rendered then froze
  with the process still alive → `STALLED (froze at N/M frames)` instead
  of a bogus PASS (the Silo case). Retried once like NO-FRAMES.
- **C — raw mouse input.** `Input.MouseRawInput=1` (default 0) sets the
  SDL relative-scale / warp hints off in `inputInit` — no OS pointer accel.
- **E — `--help` / `--version`** in `main.c` before `crashInit()`: build
  id, usage, the 21-level `-level_XX` table.
- **D — `[Debug]` ini flags.** `Debug.FrameDump` (mirrors `GE_PCDUMP`),
  `Debug.InputLog` (mirrors `GE_INPUTLOG`); env var wins, ini is the
  fallback. `config.c` `configGetFrameDump()`/`configGetInputLog()`.
- **Verified:** build green (`ntsc-final`); `-level_09` + `-level_20`
  boot crash-free 6/6 GE_PCDUMP frames (unregressed); `ge007.ini` gains a
  populated `[Window]` block on exit.

## Done this session (M-27 continued) — FULL front-end loop works end to end

**User playtested Dam → exit → post-mission report → Facility briefing →
Start, exactly like the retail game.** No crashes, no hangs. 10 commits
`bb6ac627`..`0fdb14a3` (all committed).

- **D142** (`src/bondconstants.h`) — GCC/mingw compiles an all-non-negative
  `enum` as UNSIGNED, so `for (s = SP_LEVEL_EGYPT; s >= SP_LEVEL_DAM; s--)`
  in `fileGetHighestStageDifficultyCompletedForFolder` never terminated →
  SELECT FILE screen froze the game. Fix: `#ifdef PORT` negative sentinel
  in `LEVEL_SOLO_SEQUENCE`. PORT-LEARNINGS §D3.
- **D143** (`src/game/textrelated.c`) — `langGet()` returns NULL for a
  string slot the PC menu flow hasn't loaded; `textRender/textMeasure/
  textWrap` faulted on it. NULL guards → blank text instead of crash.
  (A string bank still isn't loading — briefing/objective text is blank.
  Cosmetic, chase later.)
- **D144 / D146** (`port/fast3d/gfx_pc.cpp`) — a front-end 3D model
  (MISSION COMPLETE dossier / mode-select wallets, D75 family) emits a
  malformed compiled sub-DL (`seg5+0x9ee4`): unresolved matrix pointer,
  then garbage opcodes. D144 = bad matrix ptr → identity; D146 = unknown
  opcode → end the DL (was `sysFatalError`→`abort()`, no crash log) +
  `fast3d_ptr_ok()` guards on `gfx_sp_vertex/movemem/set_vertex_colors`.
  Model renders wrong/absent (GRAPHICS-BACKLOG D149) but the game survives.
- **D145** (`port/src/video.c`, `gfx_sdl2.cpp`, `input.c`) — bare **ESC was
  `exit(0)`** in two event handlers; on the debrief screens ESC is the
  natural "back" key so paging with it quit the game (clean exit, no log,
  looked like a crash). ESC now = N64 B (back/cancel); quit = window-X /
  Alt+F4.
- **D147** (`port/src/libultra.c`) — **the end-of-Dam hang.** `sndPlaySfx`
  → `alEvtqPostEvent` on the main thread races the `amMain` audio thread
  in `sndRemoveEvents` on the same `ALEventQueue`; both "lock" with
  `osSetIntMask(OS_IM_NONE)` which was a **no-op** → list corruption →
  infinite spin. `osSetIntMask` is now a process-wide **recursive mutex**
  (`OS_IM_NONE` acquires, `OS_IM_ALL` token releases). Thread dump
  confirmed. PORT-LEARNINGS §D4. Verified `-level_09` 1200 frames clean.

**New test tooling (committed):**
- `tools_pc/debug.ps1` — `.\tools_pc\debug.ps1 [-level_09] [-Menu 13]
  [-NoBuild]`. Builds, runs under gdb, prints FATAL line + backtrace +
  crash log on exit → `gdb.txt`. Use this for every playtest so a
  crash/hang always leaves a trace.
- `GE_INPUTSCRIPT="<frame>:<tok>,…;…"` (`port/src/input.c`) — headless
  scripted controller-0 input (buttons pulse, `SUP/SDOWN/SLEFT/SRIGHT/
  SNONE` sustain). Sole input source when set.
- `GE_STARTMENU=<id>` (+ `_PAGE`, `_DIFF`) (`src/game/lv.c`) — boot
  straight into a front-end menu (13=MISSION_COMPLETE, 10=BRIEFING,
  7=MISSION_SELECT, 12=MISSION_FAILED, 6=MODE_SELECT). Skips a save/unlock
  so it lands on Dam regardless; good for crash-testing a screen, not for
  reproducing real objective state.

### Still open (cosmetic / not blocking, parked in GRAPHICS-BACKLOG)
- **D148** — Dam level-end cutscene (Bond rappelling down the dam) doesn't
  play; cuts straight to the report. Scripted-cutscene / cinematic-camera
  + rappel anim; D75 family.
- **D149** — front-end MISSION COMPLETE / mode-select 3D models garbled
  or absent (the D144/D146 corrupt DL). D75 family.
- **D143 side effect** — briefing / objective text renders blank (a lang
  string bank not loaded in the PC menu flow). Find which bank.

### Next
- Continue the mission list: Facility → Runway → … playtest with
  `tools_pc/debug.ps1`, same loop. In-level ABI/GBI crashes are the
  expected work (D122/D135/D147 pattern).
- The D75 front-end / cutscene 3D-model family (D148/D149) is now the
  biggest *visible* gap but is cosmetic — below crashes.

## Done this session (M-30) — user batch defect list; commits + docs

### M-30b — 6-agent parallel burst (2026-08-31), all merged + verified

7 commits `84a8693e`..`1b69c8ba`. Merged tree builds clean; `-level_09/-20/-30/-34/-37/-28`
render + no crash; menu + Depot visually verified; Agent D's 21/21 with-input
(walk+fire) sweep crash-free. Three texture-pipeline fixes compose correctly.

| Dxx | Fix | Verified |
|---|---|---|
| **D158** | KF7 muzzle-flash `((f32*)stackpad2)[-8]` negative-stack write → `0xc000001d` on fire. Real `#ifdef PORT` local. | Caverns survives sustained KF7 fire; stackpad audit found no others |
| **D159** | `texSwapAltRowBytes` (odd-row byte-swap for an N64 RDP TMEM XOR fast3d doesn't emulate) scrambled every ~1:1-viewed texture = the user's "interlaced textures". `#ifdef PORT` skip. | menu wallet-photo: comb → clean portrait; `-level_09` unregressed |
| **D161** | Depot ceiling: 16×16 CI8 tile drawn with TLUT off (`G_TT_NONE`) → fast3d did a palette lookup on stale palette → blue speckle / radial rays. Decode CI+`G_TT_NONE` as intensity. | Depot ceiling: blue garbage → dark industrial roof; `-30/09/34` pass |
| **D164** | Legal/disclaimer screen drew 1 line — `legal_text_end = &legalscreen_MRD` assumed linker adjacency (false on mingw). `#ifdef PORT` bound on the array. | high-confidence static (nm-verified); needs a boot to eyeball |
| **D165** | Front-end mouse cursor: velocity² feel → P-controller 1:1 pointer. `Input.MenuPointerMode` (def 1), `Input.MenuPointerSpeed`. | build + menu smoke; user feel-check pending |
| **D166** | Hipfire vertical aim: fixed digital threshold → speed-proportional C-button duty cycle. `Input.HipfirePitchSpeed`. | build; user feel-check pending |
| **D160** | Dam rappel cutscene (D148) not root-caused — `GE_D160=1` diagnostic shipped. Hypothesis: propDef command-index walk desync (D122/D132 family). | **needs: user runs Dam to exit with `$env:GE_D160=1`, pastes `D160:` lines** |

Also this session (pre-burst): D157 (campaign unlock — user-confirmed), RC2
(`Video.FixMipTextures`), D120 (PointUsage), D74 (`Video.WrapFix` opt-in).

### M-30 state / next
**⚠ Before the next playtest: `./build-pc.sh ntsc-final` AND the full sidecar
regen** (`python tools_pc/d43_emit.py ntsc-final && python tools_pc/d69_emit.py
ntsc-final && python tools_pc/d88_emit.py ntsc-final --regen`) — D120 changed
`d43_emit.py` (PointUsage) and needs the regen; `debug.ps1` rebuilds the exe
but does NOT regen sidecars. (Done on this session's machine already.)

**Campaign progression + saving works (D157, user-confirmed).** The user can
now do the full Track A playthrough — each level boundary exercises the same
save path. `GE_UNLOCK_ALL=1` jumps to any level; `GE_SAVELOG=1` traces the
save chain if a later level misbehaves.

Fixed + committed this session (all on `master`, unpushed):
- **D165 / D166** (M-31, `port/src/input.c`, port-only) — input polish.
  D165: the front-end cursor is now a true ~1:1 pointer (P-controller
  estimating the game cursor through `front.c`'s own integrator) instead of
  the M-30 velocity² feel; `Input.MenuPointerMode` (default 1) falls back to
  the old mode, `Input.MenuPointerSpeed` scales it, `GE_INPUTLOG` traces
  `menuptr est/tgt/eff/stick`. D166: hipfire mouse pitch is speed-proportional
  C-button pulses, not a digital threshold (`Input.HipfirePitchSpeed`).
  Build green; `-level_09` + `GE_STARTMENU=6` smoke crash-free. **User
  feel-check owed** (headless can't drive the mouse) — tune MenuPointerSpeed
  up if the menu sweep feels short.
- **D157** — campaign never unlocked (objective-difficulty byte read at the
  wrong offset on LE). Root-caused + user-confirmed. §F D157.
- **RC2** (`Video.FixMipTextures`, default on) — LOD textures were uploaded
  ~1.3× too tall; clip to base-tile height. Modest visual win, no regression.
- **D120** — `d43_emit.py` now emits the opcode-0x18 `PointUsage[]` chain
  (was zero-filled → blood-decal walk cycled, guarded). Interactive verify
  pending (BUNKER1 firefight).
- **Menu #4** — stripped the per-frame D63 log flood that collapsed the
  front-end to 5fps (`d0789358`).
- **Menu #2** — front-end mouse-pointer mode (`current_menu != RUN_STAGE` →
  mouse drives the cursor both axes, LMB=A/RMB=B); Y-invert fixed.
- **RC4 RETRACTED** — the palette decode is correct RGBA5551, the analysis
  doc's "spec" was ARGB1555. Don't "fix" it.
- **21/21 level sweep PASS** (first all-green incl. Cuba) — no regressions.

Still open / next:
- **D148** (Dam exit rappel cutscene doesn't play) — user will see it right
  after Dam. D75 front-end/cutscene-model family, cosmetic, deep. Not started.
- **RC1 / wallet-Bond photo garble** — D75 family (`texLoadFromGdl` model-GDL
  expansion broken for front-end models). `TEXTURE-GLITCH-ANALYSIS.md` §6b.
- **Depot ceiling blue-speckle** — B2 light-shaft/UV bug on that surface;
  RC2 didn't touch it. Needs `GE_DTEX`/RenderDoc on the specific surface.
- **D154** (`bg.c` wall-shoot GBI parser, committed `072b5c44`) — needs a
  firefight-into-a-wall to verify, then close.
- In-level ABI/GBI crashes as the campaign playtest proceeds (D122/D135
  pattern) — the expected work.
- Latent: `fileSetDifficultyStageTime` `times[]` OOB read for 007-difficulty
  late levels (retail has it too; edge case, don't fix without growing
  `save_data` which breaks EEPROM layout).

### BLOCKER (M-30) — campaign progression not persisting — **FIXED (D157), CONFIRMED by user playtest**
User replayed Dam on Agent post-fix: `gdb.txt` shows `obj 0 objdiff=1` /
`obj 1,2 objdiff=2` (skipped on Agent) / `obj 3 objdiff=0 status=1` →
`objectiveIsAllComplete=1` → `end_of_mission_briefing` → `fileUnlockStage...
stage=0 diff=0` → `fileWriteSave slot=0 bitflags=10`. `data/ge007.eep` slot 0
now carries the Dam completion time; the Agent checkmark shows in mission-
select **and persists across a kill + restart**. Campaign progression works.
(`GE_SAVELOG` diagnostics — `#ifdef PORT`, env-gated — left in
`objective_status.c` / `boss.c` / `file.c` / `file2.c` for now; strip once a
few more level boundaries are playtested. `dump_objectives.py` per-criterion
`MinDif=` is only valid on the `type=23` lines — garbage on 2-word sub-records,
harmless.)

<details><summary>D157 root cause</summary>
`D157` (`src/bondtypes.h`, `#ifdef PORT`): the type-23 objective record's
`MinDificulty` is a BE `s32` at word 3; `d88_propdefs.py` bswap32's it, so on
LE the value moved from byte 0xF to 0xC, but `struct objective_entry.difficulty`
still read `s8`@0xF → **0 for every objective**. `get_difficulty_for_objective()`
returned Agent(0) for all → `objectiveIsAllComplete()` on Agent evaluated
objectives that should be difficulty-gated out (Dam "Neutralize all alarms" =
Secret Agent, "Install covert modem" = 00 Agent) → always incomplete →
`end_of_mission_briefing()` (the unlock write) never ran. **User was right —
you don't shoot an alarm on Agent Dam.** Fixed the struct tail order under
`#ifdef PORT`; also fixed `dump_objectives.py` (same byte-offset bug — it
showed "Agent" for everything). **Next: user replays Dam on Agent (just reach
the exit) under `GE_SAVELOG=1`; expect `MinDif=1/2/2/0` on the crit lines,
`objectiveIsAllComplete=1`, a `fileWriteSave slot=N ... TIMES` line, and
Facility unlocked in mission-select.** If obj 3 (Agent, flag 0x1000) still
shows incomplete, the level isn't setting that stage flag on exit — separate
issue. `GE_UNLOCK_ALL=1` remains the playtest workaround meanwhile.
</details>

<details><summary>Original M-30 blocker writeup (superseded by D157)</summary>
User completed Dam, hit "previous" from the debrief to mission select,
**Facility still locked**. `data/ge007.eep` has zero completion times → the
unlock save isn't landing (or is wiped on reload). Chain:
`bossReturnTitleStage` (`objectiveIsAllComplete()` gate) →
`end_of_mission_briefing` (`briefingpage`/`selected_difficulty` guard) →
`fileUnlockStageInFolderAtDifficulty` → `fileOverwriteSaveSlotWithNewSave`
(needs a slot with `SAVEFLAG_DORESET`) → `fileWriteSave` (`fileGamePakProbe`)
→ EEPROM; then `fileValidateSaves` CRC-checks every slot on menu re-entry and
`fileResetSave`s any mismatch.
**M-30 GE_SAVELOG run 1 (user's gdb.txt) — narrowed:**
`bossReturnTitleStage stage=33` (Dam) fires with **`objectiveIsAllComplete=0`**
→ `end_of_mission_briefing` never called → no unlock write. `fileValidateSaves`
/ CRC / DORESET-slot all fine (first-boot fresh-save init, expected). So the
blocker is **objective-completion detection**: `objectiveIsAllComplete()` bails
at obj 0 (`get_status_of_objective(0) != COMPLETE`). Dam obj 0 = 4×
DESTROY_OBJECT (ALARM tags). This is D126/D151 propDef-decode family — either
`objective->ObjRefID` at the wrong byte offset on LE (criteria records
`_bswap32`'d, `MissionObjectiveRecord.ObjRefID` s32@4 vs the criterion's
`u16` tag), or the `sizepropdef` walk stride is off for a criterion type.
Note `sizepropdef(PROPDEF_OBJECTIVE_START)` PC-branch returns **4** and
`d88_propdefs.py` emits **16 B** for type 23 — but `MissionObjectiveRecord`
(bondtypes.h) is header+ObjRefID+TextID+MinDificulty+`*nextentry` = 20 B N64
/ 24 B PC. If `nextentry` is real+serialized, both the stride and the
converter are 1-2 words short → whole-stream desync. (`struct objective_entry`,
the other view of type 23, is only 16 B and has no nextentry — the two
structs disagree; the "// maybe wrong..." comment flags it.)
**Diagnostic committed (`<crit dump commit>`):** `GE_SAVELOG=1` now also dumps
each criterion's `type / ObjRefID / TextID / sizepropdef stride / first 3
words / currentstatus` (gated by `g_savelogObjOnce` so it only fires on the
`bossReturnTitleStage` call, not the per-frame poll).
**Next: user does ONE genuine full Dam completion (all objectives, walk out
the exit) under `GE_SAVELOG=1`; the `SAVELOG: crit ...` lines pin the exact
field/stride that's wrong.** Also noted but not chased:
`fileSetDifficultyStageTime` `offset = ((diff*20)+levelid)*10` indexes
`times[]` up to `[98/99]` but the array is `u8 times[76]` — OOB for
high level/difficulty combos (writes into the next slot's checksum). Not the
Dam/Agent case (index 0) but a latent save-corruption bug.
</details>

## Done this session (M-30) — user batch defect list; earlier items

User playtest batch report (5 items; #1 disregarded):

- **#4 main-menu hang (file-select → main menu → back → 5fps, no input, force-close).**
  Crash log was D146 spam + `D63 [1479] from=... No stack`. **Root cause: the
  D63 debug trail in `gfx_pc.cpp` fired ~10 `sysLogPrintf` lines + a full trail
  walk on EVERY unknown GBI opcode, unconditionally** — on the D149 corrupt
  front-end model DL that ran per-frame and collapsed the menu to ~5fps with
  input starved. **FIXED `d0789358`:** stripped the D63 trail (statics,
  per-G_DL recording, default-case dump); the rate-limited D146 "ending DL"
  log (cap 20) stays. Headless: FILE_SELECT / MODE_SELECT / MISSION_SELECT all
  boot + render 900+ frames clean, 0-1 D146 lines, no hang. `-level_09` 3/3
  framediff, `-level_20` 91.6% unregressed. **User to re-verify the live
  menu-nav transition** — the exact folder→mode→back path wasn't reproducible
  headless, but the per-frame log flood (the "5fps no input") is gone.
- **#2 file-select mouse only moves side-to-side.** The front-end cursor is
  stick-driven (`frontUpdateControlStickPosition` reads `joyGetStickX/Y`); the
  port only fed mouse-X to the stick in menus (mouse-Y → C-buttons, ignored by
  menus). **FIXED `<menu-pointer commit>`:** `port/src/input.c` — when
  `current_menu != MENU_RUN_STAGE` (and `!= MENU_INVALID`, so bare `-level_XX`
  in-game aim is untouched) mouse velocity drives stick X+Y as a pointer, no
  C-buttons / aim band, LMB=A (select) RMB=B (back). Reads the game global
  `current_menu` for UI context only (enum MENU is ABI int) — no logic change.
  New knob `Input.MenuPointerSpeed` (%, default 100). **User to verify feel.**
- **#5 menu + saving for playtest.** EEPROM saves (`osEeprom*` →
  `data/ge007.eep`, write-through, M-29 `bf5e4d3d`) audited — sound; progress
  persists across restart *once the menu works* (#4). No change needed; verify
  after #4.
- **#3 "interlaced" textures** (`screenshots/interlaced texture example.jpg`).
  **RC2 (mip contamination) FIXED, default on** (`<rc2 commit>`,
  `port/fast3d/gfx_pc.cpp`). New `GE_DTEX=1` probe confirmed it: every LOD
  texture uploaded ~1.3x too tall (64x64 I4 → 64x87), the extra rows being mip
  bytes rendered as image rows. `import_texture()` now clips a full-width LOD
  block to the SETTILESIZE base height; GL builds correct mips from a correct
  base. Knob `Video.FixMipTextures` (=0 → old behaviour, byte-identical to
  golden). BUNKER1 side-wall textures visibly cleaner with it on; Silo
  unregressed. **`-level_09` framediff phash on frames 320/440 is D117 noise,
  not a regression** — M-30 verified: BUNKER1's intro camera pans continuously
  for 1000+ frames and its speed varies run-to-run, so *any* fixed capture
  frame lands on a different pan moment each run (a re-baseline attempt to a
  "settled" 700-1000 window failed the same way — there is no static window in
  the intro). Trust `nonclear` (coverage %, stays stable) + the 21-level sweep
  for `-level_09`/`-level_20`; the golden phash frames are only meaningful for
  gross breakage. Depot M-30 A/B: RC2 makes the wall panels mildly cleaner,
  does NOT touch the ceiling blue-speckle (separate B2 light-shaft/UV bug),
  makes nothing worse. **21/21 sweep PASS with the fix on.**
  - **Still open:** the wallet-Bond *photo* garble is RC1 / D75 front-end model
    family (the model GDL / `texLoadFromGdl` expansion is broken for front-end
    models — fast3d walks into `0xfafbfbfc` garbage, not a missing opcode).
    Separate larger track — see `docs/TEXTURE-GLITCH-ANALYSIS.md` §6b.
  - RC4 (palette off-by-one, `gfx_pc.cpp:835`) and RC3 (wrap period) still
    open — small, in the same doc §6.
- Regenerated `docs/LEVEL-OBJECTIVES.md` for all 21 solo levels (Track A);
  committed loose reference docs/tools.

## Done this session (M-29) — race-to-release plan; audio DEFERRED; 2 commits

**Direction (user, M-29):** ship the solo campaign start→finish, fastest.
Three tracks: A = full 21-level playtest (release gate, user batch-reports a
defect list from a solo playthrough); B = **audio DEFERRED** — ship silent,
add sound post-launch (`docs/AUDIO-PLAN.md` stays valid for later, do NOT
start it); C = solo quick wins, priority: **B2 textures** > B3 blood-stain
converter > B5/B6 menu input > C2 (D75 models) > C4 (D148 cutscene) >
C1 (mirrored HUD — user unsure it's worth it). Full plan +
`docs/SMALL-FIXES.md` mapping in the `m29-release-push` memory.

- **`0bd0ceec`** — aim sensitivity (SMALL-FIXES B4 / BACKLOG B3):
  `Input.MouseAimSpeed` default 50→25 (overshot), new `Input.AimBand` knob
  (5..40, def 20). Port-only, `ge007.ini`-tunable. Needs playtest feel check.
- **`bf5e4d3d`** — file-backed EEPROM saves (BACKLOG B5 / Phase 4):
  `osEeprom*` in `port/src/libultra.c` now back `data/ge007.eep` (2 KB /
  16Kbit, lazy-load + write-through, PD pattern). `osEepromProbe` →
  `EEPROM_TYPE_16K`. Verified: game writes the file (checksum @blk 0 +
  save_data @blk 4), `-level_09` unregressed, persists across runs.
  Campaign progress now survives quit.
- **SMALL-FIXES B1 (D74 wrap block) — DEFERRED.** The in-place one-liner
  (`G_TX_CLAMP` test + `[t]` index) activates never-run code and boot-crashes
  `-level_09` (0 frames). Left inert with a NOTE in `gfx_pc.cpp`. Needs the
  hoist-out-of-vertex-loop rework + per-level visual check.
- **B1 3-point filtering — wired up as opt-in (`a418f4d0`), NOT default.**
  The sm64ex 3-point shader path existed but was unreachable + its min-filter
  row was mip-less. Fixed the row (trilinear), aniso default 0→4, exposed
  `Video.TextureFilter=2`. **Default stays 1 (bilinear)** — 3-point softens
  textures at normal distance and (tested) did NOT fix the Depot roof.
- **B2 (Depot wrong textures) — filtering RULED OUT, still not fixed.**
  `docs/BRIEF-B2-depot-textures.md`. The roof renders as blue speckle +
  radial rays converging to a point; identical with 3-point+trilinear+aniso
  at 640×480 native, so NOT grazing aliasing. Decode path (TLUT bswap,
  `palette_to_rgba32`, `import_texture_*`) audited clean. It's a texture-data
  / UV / light-shaft-effect bug on that surface. **Next step: `GE_DTEX`
  probe** to identify what draws it (params in the brief). This is a
  RenderDoc/probe-class investigation — D114/D116 rabbit-hole family, needs
  a focused session, not inline.
- **`7d7f5fb2` D155 — Facility outro-cutscene "hang" FIXED.** User report.
  `waitForNextFrame()` passed `deltaFrames` unclamped; on the port
  `osGetCount()` is wall-clock (D117) so a real-time stall at the
  cutscene→debrief asset load (worse with a local LLM thrashing the box)
  made it balloon to hundreds → `g_ClockTimer` huge → `modelTickAnim`
  `while(numticks--)` × N chrs + dozens of `for(i<g_ClockTimer)` sim loops
  → multi-second frame → heartbeat "hang" + spiral. Fix: `#ifdef PORT` clamp
  to 6 in `frametiming.c`. **Likely fixes a whole class of post-slow-load
  transition hangs** — de-risks the Track A campaign playtest (every level
  boundary loads assets). Confidence high (stack + arithmetic agree).
- **`d656823e`+`b4a7fc2a` D156 — Facility outro hang, 2nd occurrence, the
  ACTUAL fix.** After D155 the user re-ran the playthrough → hung again at
  end of Facility, same stack but `frames=12962` **frozen constant** (true
  infinite loop, not D155's spiral). The `while(1)` at `model.c:3131`
  (`modelSetAnimFrame2WithChrStuff`) steps one anim frame at a time from
  `framea` to `frameb`; `frameb` = `modelTickAnim`'s accumulated `frame`. A
  cutscene anim transition with a near-zero blend/`timespeed`/`unkb0`
  denominator → huge/NaN `model->speed`/`playspeed` → `frameb` huge/NaN →
  `floorFloatToInt` garbage → ~2^31 iterations. `#ifdef PORT` guards: snap a
  non-finite/`|x|>=1e6` `frameb` to `framea`, and fall back `frame`/`frame2`
  to the pre-loop values in `modelTickAnim`. One-shot `stderr` diagnostic
  dumps the model speed fields when it fires. **User to re-verify the
  Facility outro.** If it still hangs OR the diagnostic prints, the NaN
  source is upstream (suspect a D100/D140-style misaligned `Model` field, or
  the cutscene data). §F D155+D156, PORT-LEARNINGS §E.
- **D153 reminder cost real time this session:** back-to-back `-level_09`
  runs during builds boot-crash under machine thrash; only regression-test on
  a settled machine (clean run = 600+ frames fine).

## Done this session (M-28) — D150 (watch page crash) + D151 (watch text blank)

- **D150** (`src/str.c`, `#ifdef PORT`, uncommitted) — user found a crash
  opening **level objectives on the watch**. AV in `strcat` (`str.c:25`,
  NULL src). The watch BRIEF/OBJECTIVES pages (`options.c:3940-4068`) build
  text with `strcat(buf, langGet(id))`; `langGet` returns NULL on PC for the
  briefing/objective string banks the in-level watch flow never loads
  (same missing-bank issue as the D143 "briefing text renders blank" note).
  Fix: NULL-tolerant `strcpy/strncpy/strcat` under `#ifdef PORT`. Note these
  are `__nonnull__` builtins to GCC so a plain `if (src==NULL)` is optimised
  away — the guard laundras the ptr through an empty `__asm__` (`GE_IS_NULL`).
  Build green; `-level_09` 600+ frames @ 91.67% unregressed. **Watch-page
  repro is interactive — needs user re-verify.** §F/§H D150, PORT-LEARNINGS §C.
- **D151** (`src/bondtypes.h`, `#ifdef PORT`, uncommitted) — the D150
  "objective/briefing text renders blank" consequence, ROOT-CAUSED and fixed.
  NOT a missing bank: the per-level lang bank IS loaded in-level
  (`langLoadToAddr`, `prop.c:1274`). The propDef records that carry the watch
  text (types 35 `WatchMenuObjectiveText`, 23 `ObjectiveStart`) store a plain
  `s32` slot id in their 3rd word; `struct watchMenuObjectiveText` /
  `struct objective_entry` decode it as `u16 reserved; u16 text;` and read
  `text` at offset `0xA` — works only because on BE the id is in the low 16
  bits. `d88_propdefs.py` `_bswap32`'s the whole word → `text` @0xA reads 0 →
  `langGet(0)` → NULL → blank. Fix: `text` is a full `u32` at the word offset
  under `#ifdef PORT` (N64 `u16 reserved; u16 text;` kept under `#else`). No
  converter change / no sidecar regen. Compiles clean (link needs the running
  game closed). **Interactive re-verify pending** — open the watch, all of
  OBJECTIVES / mission background / M / Q / Moneypenny should now show text.
  §F **D151**, PORT-LEARNINGS §C.

### D152 (OPEN) — mission-failed → permanent black screen = `osSetIntMask` lock deadlock
User killed Trevelyan in Facility (`Ctrl` = **fire**, no crouch bind) → failed
the objective → fade to black, never returns; process alive. **Live `gdb.txt`
inspected** (game left running under `debug.ps1`): NOT the front-end
mission-failed *screen* — it's a **deadlock on `s_imLock`** (the D147
recursive-mutex behind `osSetIntMask`). Rendering froze at frame 12600.
`mainThread` in `sndSetScalerApplyVolumeAllSfxSlot`→`alEvtqPostEvent`→
`osSetIntMask(OS_IM_NONE)`→`pthread_mutex_lock` (waiting); `amMain` in
`sndPlayerVoiceHandler`→`alEvtqNextEvent`→same, also waiting; **neither owns the
lock** → a leaked unbalanced `OS_IM_NONE` (libaudio early-return paths) or a
transient thread that acquired-and-exited holds it. The mission-failed **audio
fade-out** (`sndSetScalerApplyVolumeAllSfxSlot` posts `AL_SNDP_RELEASE_EVT` per
`ALSoundState` per frame) is what exercises the window. D147 family; D147's
"cannot deadlock" claim is now falsified. §F **D152**, PORT-LEARNINGS §D4.
**MITIGATED (M-28, `port/src/libultra.c`, port-only, uncommitted).**
`osSetIntMask` is now a **self-healing** logical lock: same recursive
acquire/release as the D147 mutex on the normal (microsecond) path, but a
waiter blocked **> 2 s** (`OS_IM_STUCK_NS`) declares the holder leaked it,
logs `LOG_ERROR` with the stale owner + the stealing caller's return
address, and steals the section. A leaked/unbalanced `OS_IM_NONE` (or an
acquire-and-exit transient thread) can no longer wedge the game forever —
worst case is a ~2 s audio hiccup + a log line that names the leak. Builds
clean. Still OPEN: the exact leaking call site (read it from the next
`D152: ... stealing from owner=` log) + the proper narrow fix (dedicated
`ALEventQueue` lock). Next: replay the Facility mission-fail; if the steal
fires, `addr2line` the logged caller.
Separate minor gap: no crouch keybind (BACKLOG B3/B4).

### D154 (WRITTEN, UNVERIFIED, UNCOMMITTED) — `bg.c` room hit-test GBI parser port
`bgTestRayIntersectionInRoom` (`src/game/bg.c:3331`) is the D135 sibling the
docs kept flagging: an unported N64 GBI parser (`((u8*)gdl)[k]` / `((u32*)gdl)[i]`
byte/word indexing) walking the 16-byte PC room DL as if it were 8-byte N64
`Gfx`. Fires on **shooting walls/floor** (`bgTestBulletHitBackground`), which no
level-sweep exercises. Ported under `#ifdef PORT` (N64 path verbatim under
`#else`): header fields via the `.dma` view like `bgBuildRoomVtxBounds`; G_TRI1
+ G_TRI4 vertex-index nibbles recovered from `(u32)gdl->words.w0/.w1`
(derivation table cross-checked twice); `texturenum` -> -1 like D135 (KSEG0
deref invalid for converted GDLs; only flavours the impact decal/sound, D77).
Builds clean. **NOT verified** — a no-input capture never calls this function,
so it needs a real firefight into a wall on an **idle** machine; couldn't get a
clean run this session (D153/driver-crash under machine thrash — see below).
Next session: build, `-level_09`, fire at a wall, confirm no crash + `-level_09`
framediff green, then commit. §F **D154**.

### D153 — `-level_09` frame ~900 crash = load flakiness, NOT a bug (closed)
Chased for ~30 min in M-28: `-level_09` crashed `0xc0000005` at a
nondeterministic frame 900↔1400, 6/6 runs — **but every one was while the
machine was hammered with concurrent builds + multi-run loops.** On a quiet
machine `-level_09` ran past frame 2400, 0 heartbeats, no crash. It's the
D117/D134 host-scheduling flakiness the docs warn about every session (M-25
Silo ~frame 300 is the same). Not a regression, not D150/D151/D152.
`romdataFixupMusicSeqTable: seqCount 63 exceeds blob capacity 1` is a benign
expected warning (header-only first call). **Lesson: never regression-test
while builds/other game runs are in flight.** §F **D153** (closed).

<details><summary>M-27 earlier — front-end/pause bucket: D140 + D141 FIXED</summary>

Started the parked "front-end / transitions / pause / watch" bucket.
**Pressing Start in-level no longer crashes** — the pause / watch menu
renders (weapon page verified via a temp `GE_AUTOPAUSE` input probe,
since removed). Cosmetics unchanged and still parked: mirrored watch text
(D114/D116), dark/faint weapon model (D75).

- **D140 (FIXED)** `src/game/bondview.h`, `src/game/bondview2.c` —
  pause-menu crash `bondviewRenderWatch` → `bondviewTransformManyPos-
  ToViewMatrix(field_23C=NULL)`. `something_with_watch_object_instance`
  (N64 player +0x230) is a **`struct Model` + RW-pool punned into a
  0x184-byte field-run**; `field_23C`/`watch_scale_destination`/
  `pause_watch_related_adjust` are actually `.render_pos`/`.scale`/
  `.animframe1`. PC `sizeof(struct Model)` grows → the aliases break →
  NULL `render_pos`. Fix: real inline `struct Model` +
  `u32 watchRwPool[192]` under `#ifdef PORT`; the 3 named reads redirect to
  the member via `GE_WATCH_{ANIMFRAME,SCALE,RENDERPOS}` macros (N64 `#else`
  = verbatim field). D56 branch of `sub_GAME_7F07E7CC` now uses the inline
  pool. D100/D102 pattern. §F/§H **D140**, PORT-LEARNINGS §A.
- **D118d (FIXED, feel unconfirmed)** `src/game/options.c` — user bug report:
  watch inventory list over-scrolls with keyboard W/S (one press skips several
  items). GE's "slam the stick" fast-scroll is a raw per-frame level check
  (`joyGetStickY < -0x46`); keyboard/digital pads sit at max every frame.
  `#ifdef PORT` drops the stick term → list-nav goes through the latched
  single-step path (one step per press). Build green, `-level_09` unregressed;
  couldn't verify the feel headlessly (INVENTORY watch page unreachable without
  page-cycling input) — **needs an in-game check**. Latent sibling in `front.c`
  menu nav (same `joyGetStick*InRange` level-check pattern). §F **D118d**.
- **D141 (FIXED)** `src/game/gunfire.c` — the crash D140 exposed.
  `set_enviro_fog_for_items_in_solo_watch_menu` walks `bodymodel->Switches[]`
  (a `ModelNode*` array) with raw byte offsets `+0x48`/`+0x5c`, `j += 4`
  (4-byte-pointer constants) → PC 8-byte stride reads garbage → bogus node
  → AV in `modelGetNodeRwData`. Fix: `#ifdef PORT` uses
  `Switches[18 + (j>>2)]` / `Switches[23 + (j>>2)]`. §F **D141**,
  PORT-LEARNINGS §B (cf. D128).
- **Verified:** build green (`ntsc-final`); `-level_09` framediff 3/3,
  `-level_20` crash-free (both unregressed); autopause probe → watch page
  renders 400+ frames, no `ge007.crash.log`.
- **D139** (stage-unload, M-23, still UNVERIFIED) not exercised — it needs
  a real level-exit teardown, not a pause. Next front-end item.
- **Still in the bucket:** D139 verify; unpause / watch-page navigation
  (the temp probe didn't test exiting the watch); level exit → MISSION
  COMPLETE → debrief → auto-advance; main-menu / mission-select walkthrough
  (WS2); watch objectives page + gadgets.

**Update (M-27 end): the level-exit → MISSION COMPLETE → debrief →
auto-advance → next briefing → Start loop is DONE and playtested (D142–
D147).** D139 got exercised for free. Remaining bucket items (unpause /
watch-page nav) are minor.
</details>

## Next task (M-24 — Opus 5)

**Scope call (user, M-23):** D139 (stage unload) + D140 (pause menu / watch)
are the same **not-yet-built front-end/transition layer** — level exit →
debrief → next briefing, pause, the watch. Bare `-level_XX` skips all of it
and the sweep never touches it. Do **not** build a throwaway exit path just
to verify D139 — its fix is unambiguous (LE reads the wrong header byte) and
committed; it'll be exercised for free once that layer is built. Both go in
a **parked "front-end / transitions / pause / watch" bucket** for one
dedicated session later (see `docs/PLAN-linear-level-sweep.md` WS2).

**M-24 = keep doing in-level playtesting.** In-level crashes (D135/D137/D138
type — pointer-width / ABI / GBI-parser bugs) are the real work and show up
during normal `-level_XX` play.

1. **Resume WS6** `docs/LEVEL-PLAYTEST.md`, bare `-level_XX`. Do the in-level
   ~80% (spawn, geometry, guards, doors, lifts, switches, pickups, each
   difficulty-gated objective *registers* COMPLETE via `objective_status`,
   alarms/reinforcements). **Defer** the exit-trigger / auto-advance / pause /
   watch checks — those need the parked front-end bucket. Facility first
   (it's playable start→~end now), then down the mission list.
2. Each new in-level crash: `tools_pc/repro_gdb.sh <XX>`, get the real
   `bt full`, expect another `#ifdef PORT` ABI/layout fix
   (D122/D126/D132/D135/D137/D139 pattern). Log as the next Dxx.
3. **Re-run `tools_pc/level_sweep.sh`** once — D135/D138 touch shared code
   paths; sweep is boot-only so it only proves no *boot* regression. Refresh
   `docs/LEVEL-STATUS.md`.
4. `bg.c` hit-test sibling port (§F D135 follow-up — identical unported GBI
   parser to D135, `~3373-3646`) — do it before a level where wall-shooting
   is heavy (i.e. soon).

**Parked bucket — front-end / transitions (STARTED M-27):**
- ~~D140 (pause menu → `bondviewRenderWatch` → NULL `field_23C`)~~ FIXED M-27
  (+ D141, the watch-page crash it exposed). Pause now renders.
- D139 verify (stage unload / `cleanupObjects`) — still needs a real exit
- unpause / watch-page navigation (M-27 probe only tested opening it)
- level exit → MISSION COMPLETE → debrief → auto-advance to next briefing
- main menu / mission-select / difficulty-select playtest (WS2)
- the watch (objectives page, gadgets) — needed for the WS6 exit/objective
  checks, D75 3D-model-transform family

**Verification per fix:** target level boots to ≥1 non-degenerate frame
(`tools_pc/pixcount.py`), AND `-level_09` + `-level_20` unregressed
(`tools_pc/framediff.py`). Regen sidecars after any converter change.

<details><summary>M-22 and earlier "Next task" (superseded — §H authoritative)</summary>

-1. **D134 DONE (M-22)** — the frame-2 boot hang that every session since
   M-13 wrote off as "sweep flakiness / D117 / machine load" was a **real,
   fixed bug**: the SP/DP task-done event was posted `OS_MESG_NOBLOCK` into
   the sched `interruptQ` that the 60 Hz VI pacemaker also fills, so a slow
   synchronous fast3d frame let a retrace backlog swallow the done event →
   permanent stall. Fix in `port/src/libultra.c` (`portPostEventForce` +
   2 reserved slots in `portPostVIEvent`). `-level_09` 6/6 boots to frame 600,
   0 heartbeats (pre-fix 1/3). §F/§H **D134**, PORT-LEARNINGS §E.

0. **D132 DONE (M-21, commit `fa296b17`)** — propDef union-index slots for
   types 14/19/38/44 (LINK/SWITCH/LOCK_DOOR/SAFE_ITEM) now emitted at the
   right PC offsets (`d88_propdefs.py` + `loadobjectmodel.c sizepropdef`
   PORT).

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

</details>

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
