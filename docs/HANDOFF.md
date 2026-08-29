# Handoff brief — GoldenEye 007 PC port (Phase 2: Session M-3 —
# STAGE-LOAD → RENDER crash chain CLEAR; BUNKER1 renders, next is D75 model quality)

## PREFLIGHT — before touching anything (a concrete next-step below is NOT a reason to skip this)

1. `CLAUDE.md` (auto-loaded) + `AGENTS.md` non-negotiables.
2. This file, top-to-bottom.
3. `docs/PORT-LEARNINGS.md` — recurring bug classes.
4. Dispatching a subagent? `docs/AGENT-WORKFLOW.md` FIRST. Every brief needs
   FILES / BUDGET / ON-EXPIRY / CONSTRAINTS / REPORT (see CLAUDE.md "Dispatch preflight").

_Paste-ready brief. Authoritative context: `AGENTS.md`,
`docs/PCPortResearch.md` §F (D69, D78-D102), `docs/BRIEF-D69-stage-load.md`._

## READ THIS FIRST — crash chain CLEAR; viewport fixed; room geometry is next

`-level_09 -ml0 -me0 -mgfx100 -mvtx50 -mt700 -ma150` **boots BUNKER1
and renders continuously with no fault** (60 s+, 5000+ VI posts at full
framerate; attract mode also clean). The stage-load → first-frame →
in-level crash chain (blocker since D69) is resolved.

**D103 (session M-4) fixed the "~46 %, lower half" symptom** — it was a
single native-resolution/viewport bug (`osViSetMode` hard-coded height
480 → `RATIO_Y` half of `RATIO_X`), not per-model. Frame is now full
(91.7 %, correct letterbox + HUD placement). See §F "D103".

**D105 (session M-4) fixed room geometry** — `zbufClearCurrentPlayer`
(`viewport.c`) used the N64 "fill the Z buffer as a colour image" idiom,
which fast3d doesn't emulate, and nothing emitted `G_CLEAR_DEPTH_EXT`, so
the depth buffer was never cleared and all ~160 k room triangles failed
the Z test (only `skyRender`'s background fill survived). PORT branch now
emits `G_CLEAR_DEPTH_EXT`. See §F "D104"/"D105". BUNKER1 now renders
recognisably — textured walls, storage racks, floor (2585 colours, sky
16 %), 70 s crash-free.

**D106 (session M-4)** — the big sky-void through doorways was the portal
BFS dropping the next room: a portal straddling the camera near-plane
projects z==0 clip points to ±1e20 screen coords, and on x86-64 that
garbage came back `min>max` on one axis, slipping past the
degenerate-box check that on N64 clamps it to full-screen. PORT guard in
`sub_GAME_7F0B5864` treats non-finite / out-of-range bounds as
full-screen. Visible rooms/frame 1–3 → 2–4. See §F "D106".

**D107 (session M-4)** — blurry ceilings/wall-panels were fast3d sampling
GE's first mip (render tile 1), whose single-`G_LOADBLOCK` TMEM slot
fast3d never registers, so `import_texture` fabricated a 16×16 crop of
the base image and magnified it. `gfx_lod_tile_offset` now returns 0
(base tile) when detail textures are off. BUNKER1 rooms render crisp.
See §F "D107".

**Skeletal models — FIXED (session M-5, D112).** The "3D line" / missing
characters were `tools_pc/d43_emit.py`'s `put_f32` reversing the wrong 4
bytes (`src[doff+4:doff:-1]` → bytes doff+1..doff+4, dropping the MSB),
corrupting every f32 in converted model rodata (joint `Origin`s, LOD
distances, BSP planes). Compiled-in front-end models were unaffected
(native LE), hence "logo fine, every guard broken". Fix: one line,
`src[doff:doff+4][::-1]`; regen with `python tools_pc/d43_emit.py
ntsc-final`. BUNKER guards now render as coherent humanoids, monitor
screens draw content. See §F "D108–D112".

**"props emit no geometry" was a STALE premise** — props/doors emit
complete leaf DLs (~15k/run, valid ptrs; fast3d transforms them). The
storage-room void is NOT closed doors failing to render.

**Real remaining work, in order (updated session M-8):**
1. **Input layer (Phase 3)** — IN PROGRESS (M-8 agent, §F D118). `port/src/
   input.c` was a stub; only a minimal keyboard path in `libultra.c`
   (`contSnapshotFromKeyboard`) worked. No C-buttons/mouse-look/gamepad/
   rebinding. This is the top playability blocker — you cannot aim or
   properly move. Reference: `pd_port/port/src/input.c`.
2. **Weapon model doesn't draw (AUDIT-M6 #5)** — NOT STARTED (M-8 agent
   hit the account session rate-limit before any work; tree untouched).
   Plan: instrument `gunfire.c` weapon setup (~600-661) + `field_87F`
   gating (~520/587-598) FIRST with a `GE_D119` printf — does setup even
   run? Then check `weaponModel.render_pos` = `dynAllocate`'d `rwmtx`
   lifetime vs the `gunRenderFirstPersonGunModels` (~1605) consumer (on
   N64 it aliased the persistent `hand->mtxlist`; D102 gave it own
   storage → arena may recycle). Also `weaponRwPool[192]` capacity vs
   `modelCalculateRwDataLen(mdlhdr)` on PC, and `flashvisptr` word-index
   assumption (~640). Narrow `#ifdef PORT` lifetime/sizing fix only — if
   it needs a behavioural arena change, STOP + write up. Direct `-level_09`
   boot may spawn Bond holstered — may need forced weapon state or the
   attract path to get the gun on screen. Gate to playability.
3. **The HUD/text X-mirror (D116)** — DEPRIORITISED (cosmetic, not a
   playability blocker). Every stage from font-bitmap to GL-draw
   runtime-verified non-mirrored, yet glyphs render flipped — a genuine
   contradiction (§F "D116 runtime probe part 3"). NEXT PERSON: RenderDoc
   trace of one glyph texrect, or find/read the dedicated ammo-digit HUD
   path (NOT in `textrelated.c`), before touching fast3d again. Do NOT
   spend >30 min without one of those in hand. This is the old D114
   "shared mirror" symptom — inverted guards / misplaced door props are
   likely the same bug on model skins.
4. **Rest of the `struct player` offset pass** — D115 fixed the HIGH
   `gunfire.c` THROW* bugs; #6 (watch-preview Model pool) + the broader
   audit remain (`docs/AUDIT-M6-player-offsets.md`).
5. Add an f32 value spot-check to `d43_emit.py`'s verify pass (it only
   checks pointers/opcodes today — the D112 bug passed "ALL CHECKS PASSED").
6. **GE_DETERM fixed-tick mode (§F D117)** — deferred as not-narrow; would
   unlock exact-diff regression testing + reproducible timing bugs. Design
   is written up. Worth doing once the above land.

**Session M-7 — see §F D116 + "D116 probe results".** Ran the glyph
probe (overseer, after killing a subagent that was drifting toward a
global texrect S-swap). Corrected finding: the HUD text mirror is a
**per-quad texture-U flip that is NOT path-specific** — the ammo digits
"83" are mirrored too (`ppm/frame_000320.ppm`), refuting M-7's earlier
"proportional-font-specific" claim. Rect *positions* are correct; only
texture content is X-flipped. Every fast3d stage probed clean (glyph
bitmap in memory correct, texrect `ul.u=0→lr.u=max`, `import_texture_i8`
linear, GL shader UV pass-through) — so the flip is in the GL
vertex-buffer/draw layer OR a shared clip-space-X vs U desync on rect
quads. **This re-opens D114's shared-mirror hypothesis** (now per-quad
U/X, not screen-space) and plausibly also explains inverted guards /
mislocated door props. NEXT: shader/vertex-buffer probe — dump per-vertex
(x,u) for one glyph quad; render a 1-texel asymmetric test texture to see
which axis inverts. `GE_D116`-gated probes left in `textrelated.c` +
`gfx_pc.cpp` (zero-cost).

**Session M-8 wrap — state for next session.** Build GREEN at
`587c6856`, tree clean. Committed: D116 part-3 writeup + `[D116/vbo]`
probe (`5ff012b4`), `CLAUDE.md`/preflight (`fd25628a`), D117
framediff+nondeterminism (`44d9ae98`), D118 input layer
(`4ac11fe7`/`587c6856`), handoff reprioritisation. `GE_D116` and
`GE_INPUTLOG` probes are in-tree, `getenv`-gated, zero-cost. Weapon-model
(#5) agent never ran (rate-limit) — task untouched, plan in item 2 above.
Pre-existing stash `stash@{0} "d59 probes WIP"` (~300 lines,
gfx_pc/gfx_opengl/crash.c) is NOT from M-8 — provenance unknown, left as
found. Next: human input playtest (tune `ge007.ini [Input] MouseAimSpeed`),
then weapon-model #5.

**Session M-8 (continued) — D116 part 3 + input layer.** Killed the
drifting D116 agent, ran the probe as overseer: `buf_vbo` (x,u) for
glyph quads and the GL texture upload are both runtime-verified
non-mirrored (x-left<->u=0), GL shader is `gl_Position=aVtxPos` +
UV-passthrough — every stage clean, yet glyphs render X-flipped
(contradiction, §F "D116 runtime probe part 3"). Deprioritised as
cosmetic. `GE_D116` `[D116/vbo]` probe kept in `gfx_pc.cpp`. Then
dispatched the **input-layer agent** (§F D118, in progress) — top
playability blocker. `framediff.py` structural mode is the interim
regression gate (`tools_pc/golden/` is a nondeterministic D115 capture).

**Session M-8 — see §F D117.** Visual-regression tooling. Root-caused the
frame-to-frame nondeterminism: pure variable-timestep frame pacing
(`osGetCount()` = wall clock on PC → `frametiming.c waitForNextFrame`
advances logic a real-time-dependent number of 60 Hz ticks per render).
PRNG seeding verified correct (`random.c`), not a source. A `GE_DETERM=1`
fixed-tick mode was assessed **not narrow** (redesigns VI retrace/tick
semantics, deadlock risk in load-screen loops) and deferred with a full
design in §F D117 — NOT implemented. Added `tools_pc/framediff.py`
(structural/tolerant: 16×12 grid mean-colour + non-clear-% + aHash,
`--mask` for HUD, `--exact` for a future deterministic build, `--update`
to refresh goldens). Validated against the D115 golden set. No C changes,
no probes left in tree.

**Session M-9 (Phase 3) — see §F D118.** SDL input layer implemented.
`port/src/input.c` is now real: keyboard+mouse and SDL_GameController,
mapped to GE's N64 pad (analog stick = move, C-buttons = aim, mouse-look
bridged to digital C-buttons via a clamped accumulator, RMB/LT = aim
mode, LMB/RT = fire). `libultra.c`'s SI section delegates to it (single
source of controller state; still driven by `osContStartReadData`, no
new frame hook). Config: `ge007.ini [Input]` MouseEnabled/MouseAimSpeed/
MouseInvertY. Build GREEN, boots `-level_09` crash-free 35 s.
**Owed: a human playtest** — live input is untested from the headless
agent. Manual checklist: (1) WASD moves Bond, mouse turns/looks (tune
`MouseAimSpeed` in ge007.ini if too fast/slow), LMB fires, RMB enters
aim mode, Enter opens the pause menu; (2) plug an Xbox pad — left stick
moves, right stick aims, triggers fire/aim; (3) `GE_INPUTLOG=1` prints
each nonzero OSContPad poll. Rebinding + gamepad hotplug are TODO (§F
D118).

**Session M-6 — see §F D113/D114/D115, `docs/PLAN-M6-playable.md`,
`docs/AUDIT-M6-player-offsets.md`, `docs/AGENT-WORKFLOW.md`,
`docs/PORT-LEARNINGS.md`.** D113: portal BFS is correct, not the void.
D114: matrix chain + converter verified clean, residual = shared fast3d
mirror (open). D115: player raw-offset audit + `gunfire.c` THROW* fix
(uncommitted). Build green.

**This session's fixes (all committed, master):**

| # | commit | one-liner |
|---|--------|-----------|
| D93 | `164d7f99` | null-room (room 0) NULL-deref guards |
| D85 | `493c9838` | `bgWidenRoomGdl` (8→16 `Gfx` + `bswap32`) + `bgSwapRoomVtx`; room DLs decode to real GBI; `bg.c:2448` size-cast bug |
| D85 | `6f0208d6` | `ptr_texture_alloc_start` → real `struct texpool` storage (pool looked exhausted → ~630 room textures now resolve; cleared `Bad size for RGBA texture`) |
| D94 | `63204a27` | `chrlvInitActAttack` `(s32)`-truncated anim-table index |
| D95 | `f35eba91` `933ba52b` | 2× `g_GfxBuffers` (16-byte PC `Gfx`) + raise PC mempool ceiling `0x702F4400→0x70700000` (reclaim ~4 MB DRAM) so it doesn't OOM `MEMPOOL_STAGE` |
| D96 | `d86ec483` | `PROPRECORD_STAN_ROOM_LEN` 4→8 (PORT) — prop room-list stack overflow (guards span ≥4 rooms → no terminator → smashed `chrpropsRenderPass` frame; *this* was the "runaway GDL append") |
| D97 | `2fbcc556` | clamp negative `damagetype` (US-only OOB `g_DamageTypes[]` read) |
| D98 | `000ed6af` | `initBONDdataforPlayer` allocate real PC `sizeof(struct player)` (hardcoded `0x2A80` under-alloc scribbled the master DL) |
| D99 | `253caa23` | `Model.animflipfunc` `s32` fn-ptr truncation (flag + direct call, D92 pattern) |
| D100 | `8eaad547` | `struct player.model` is an inline `struct Model`, not a `Model*` + 45 filler s32s (PC struct 0x2A8 ≫ 0xB8 hole → `animInit` overran); + dedicated `gaitRwData[]` |
| D101 | `b2234f82` | `sub_GAME_7F06DB5C` stashed `ModelNode*`/`RenderPosView*` through an `s32` (the idiom its sibling was already fixed for) |
| D102 | `1b078f6d` | 1P weapon `Model` + RW pool punned onto `struct hand` (PC `Model` 0xE8 ≫ N64 0xBC → `modelInit` aliased `datas` onto the pool); dedicated `weaponModel`/`weaponRwPool` |

**Recurring pattern this session:** the decomp pun-allocates `struct
Model` (and `struct player`, `PropRecord`, `struct texpool`, …) into
N64-sized holes / hardcoded byte counts. Every one is bigger on x86-64
→ overrun → corruption. Fix = give it real inline storage or
`sizeof()`-based alloc under `#ifdef PORT`. **Landmine still open**
(§F D100): `struct player` / `struct hand` have raw hardcoded-offset
accessors (`gunfire.c` `THROWMTX` at `+0xAD8`, …) that are NOT
PORT-adjusted — already PC-wrong, a real `struct player` offset pass is
owed before grenade/knife-throw code works.

- Build: `export PATH="/c/msys64/mingw64/bin:$PATH" && ./build-pc.sh ntsc-final`
- Sidecar regen: `python tools_pc/d43_emit.py ntsc-final && python
  tools_pc/d69_emit.py ntsc-final && python tools_pc/d88_emit.py
  ntsc-final --regen`. `data/` is gitignored; if missing,
  `cp baserom.u.z64 data/ge007.ntsc-final.z64` first (§F "`data/`
  deletion + recovery").
- Repro: `./build-pc/ge007.x86_64.exe -level_09 -ml0 -me0 -mgfx100
  -mvtx50 -mt700 -ma150` — `-m*` are BUNKER1's `boss.c` per-level sizes;
  a bare `-level_09` skips them and crashes early (TODO: auto-inject).

## Next: D75 — 3D model rendering quality

BUNKER1 renders but character/skeletal models are wrong (see the D75
section further down, still accurate). `GE_PCDUMP` + `tools_pc/pixcount.py`
to measure; the animated-model path (`animInit` + `struct player` raw
offsets, `drawjointlist`, `modelApplyHeadRelations` head/body splice) is
the prime suspect. `struct tex` headers are 24 B vs 16 B on PC — a
separate open pool-pressure item; a real PC memory-budget pass is owed.

## What this session (M-2) fixed — all committed

1. **`-level_09` now works.** `osPiReadIo` was stubbed to 0, so the token
   string was always empty and N64 debug switches were ignored (only
   attract mode could reach a level). Now built from `argv[1..]`.
2. **Sidecar tables patched in `obInit()`** instead of lazily at first
   model load — a direct stage boot loaded raw big-endian ROM before.
3. **D88.5** — stan tile-name byte-swap in the converter (0/276 →
   273/273 name matches during pad setup).
4. **D88.6** — intro CAMERA `lang1c` is a `u16` pair, not an `s32`
   (fixed the `langGet` NULL-bank crash).
5. **D89** — `init_path_table_links` `[-3]` OOB → SIGILL fix; NULL-tile
   guard in `sub_GAME_7F0B0914`.
6. **D90** — `stanTileDistanceRelated`'s 80-byte zero-fill was clobbering
   the caller's live stan-tile local (16B struct, 80B clear). The
   player's spawn stan was fine all along.
7. **D91** — `(s32)&D_800442FC[portalnum]` truncation in bg portal cull.
8. **D92** — `chrAllocate` `s32` param truncated the ailist pointer;
   `Model.unka0` `s32` field truncated a stored function pointer.

**Next steps for the resuming session (render milestone):**
1. **D85** — root cause confirmed (8-byte N64 `Gfx` vs 16-byte PC `Gfx`
   in the unconverted per-room DL blob). Runtime widen+bswap fixup being
   implemented; verify with `GE_D69BB=1` (real GBI opcodes, not
   00/01/02/52), then `GE_PCDUMP` + `tools_pc/pixcount.py` for
   non-degenerate BUNKER1 geometry. See §F "D85 root cause CONFIRMED".
   Fallback if the widen stalls: soften the four `sysFatalError("Bad
   size…")` guards in `gfx_pc.cpp` to skip-with-warning.
2. Then the front-end **render bugs (D75)**.

**D88.4 loose end (still open):** `PROPDEF_PC_BYTES` for `VEHICHLE`/
`AIRCRAFT`/`TANK`/`AMMO`/`DEPOSIT_IN_ROOM` in `d88_propdefs.py` are
placeholder guesses (BUNKER1 doesn't use them). Probe real sizes via
`d88_layoutprobe.c` before loading levels that use those types.

## Known rendering bugs (D75 — still open, orthogonal to the D88 crash)

Even once the crash chain is cleared, the front end has **broken 3D model
rendering** (user-confirmed this session):
- Rareware logo: correct (fixed in D73/D74).
- **Nintendo logo**: renders but **mispositioned**.
- **Gun-barrel intro**: the **James Bond character model is missing
  entirely**.
- **Intro credits / cast roll**: the per-character 3D models **do not
  appear at all** (names draw, models don't).
Pattern: textures/text draw; **animated/skeletal character models never
appear**; static 3D (logos) appears but with a bad transform. Leading
hypothesis is D75(b) — the animated-model path (`animInit` + raw offsets
into `struct player`, cf. D56) is broken independently of the D73 matrix
sin/cos fix. Full triage plan in `PCPortResearch.md` §F D75.

The rest of this document (below) is the **last known-good, committed**
status as of commit `8c9c6a2c` (D86+D87 resolved) — still accurate except
D88 is now further along (D88.1–3 done/verified, D88.4 is the live crash).

## Where things stand

**D69 (the original "stage load faults" milestone blocker) remains
RESOLVED.** `load_bg_file` (bg.c) doesn't fault on BUNKER1. Since then,
two more crashes further down the load chain were found and fixed this
session (D86, D87), and a third — the current blocker — was root-caused
but not fixed (D88).

**D86 RESOLVED.** `modelInitRwData` crash (`model.c:6174`) was a single
truncating pointer cast in the player's embedded gait/arm model:
`src/game/initplayergaitobject.c:5` did
`player_gait_object_header.RootNode = (int)&player_gait_hdr;` — a
same-width no-op on N64 that truncates+zero-extends a real 64-bit pointer
on PC. Fixed with a narrow `#ifdef PORT` branch assigning the pointer
directly. Root-caused via a new node-walk trace (`GE_D86=1`, left in
place, gated in `model.c`/`objecthandler_2.c`).

**D87 RESOLVED.** Once D86 stopped blocking progress, an idle (no-input)
run eventually triggers the front-end's genuine attract-mode demo
playback (`select_ramrom_to_play()` picks a random compiled-in demo —
this is shipped retail behavior, not a debug feature) and crashed in
`ramrom_replay_handler` (`ramromreplay.c`). Root cause: `ramromfilestructure`
is a real ROM-compiled asset (big-endian, like everything else) loaded via
`romCopyAligned()` — a raw byte copy by design (D66) — with **no
byteswap**, so every multi-byte field read back scrambled (e.g. `size_cmds`
2 → 33554432) and drove wild pointer arithmetic. Fixed with a `#ifdef PORT`
`ramromFixupEndian()` called once after the load (same pattern as the D54
cseq fixup). Not BUNKER1-specific — attract mode picks any of 7 demo
locations at random, so don't rely on it for BUNKER1-specific testing (see
`-level_09` below).

**D88 — SUPERSEDED, see "READ THIS FIRST" at top.** D88.1–D88.3 (header +
sub-table width/endian conversion) are now done and verified; D88.4
(`propDefs` byteswap) is the live blocker. The paragraph below is the
original root-cause writeup, kept for context.

**D88 (original writeup) — root-caused.** Launch with
`-level_09` (NTSC `LEVELID_BUNKER1 = 9`; `boss.c:199-339` decodes
`-level_XX` into `g_StageNum`, bypassing the front end/attract-mode
entirely — fast, deterministic BUNKER1 repro, crashes in well under a
minute instead of waiting ~2 min for attract mode to maybe pick Bunker).
Crash: `proplvreset2` (`prop.c:1306`) segfaults reading
`g_CurrentSetup.pathwaypoints[i1].padID`. Root cause: the per-level
`"Usetup<name>Z"` file (`prop.c:1267`, `struct stagesetup` in
`bondtypes.h:4091`) is loaded as raw ROM bytes and has **zero PC porting
work done on it** — unlike bg/stan (D69/D80-82) and models (D43/D50).
Two compounding problems, not just one:
1. The 10 top-level fields (`pathwaypoints`/`waypointgroups`/`intro`/
   `propDefs`/`patrolpaths`/`ailists`/`pads`/`boundpads`/`padnames`/
   `boundpadnames`) are declared as real pointers in the live C struct,
   so on PC they're 8 bytes each (an 80-byte header) instead of the
   file's real 4-byte-each (40-byte) N64 layout — same class as D79
   (`bg_room_data` pointer growth). Field 0 reads fine; everything after
   it is reading the wrong bytes entirely.
2. The 4 meaningful bytes each field *does* store are big-endian (the
   code's own comment: "stores every internal reference as a byte offset
   from the start of the file") and nothing byte-swaps them — same class
   as D87.
No PORT/byteswap handling exists anywhere in `prop.c` (confirmed by grep).
**Not fixed this session** — this is D69-scale format-conversion work: a
byte-accurate spec of the whole `Usetup*Z` format (top-level header +
every nested sub-table: `waypoint`/`waygroup`/`PropDefHeaderRecord`/
`PathRecord`/`AIListRecord`/`PadRecord`/`BoundPadRecord`/`pname`, each
likely with its own internal offsets not yet audited) plus either an
offline converter sidecar (preferred pattern per AGENTS.md, same shape as
`tools_pc/d69_emit.py`) or a careful runtime fixup pass that parses the
raw 40-byte N64-packed header by explicit byte offset, byte-swaps each
field, and writes results into the PC-widened struct.

**Net effect vs. last session:** the game now runs substantially further
— all the way through room-streaming setup, past the intro's model
pipeline, and into per-level "Usetup" data — before hitting D88. The
"loads without fault" acceptance bar is **still not met**, but the
remaining blocker is now narrowly scoped and has a fast, deterministic
repro (`-level_09`, no attract-mode wait, no depending on which random
demo attract-mode picks).

## Recommended next steps, in order

1. **D88.** Get a byte-level spec of `Usetup*Z` (start from BUNKER1's
   file; `strResource` is built as `"U" + "sev" + "Z"`-style name in
   `prop.c:1253-1265` — check `setup_text_pointers[LEVELID_BUNKER1]` for
   the exact literal). Decide offline-converter vs. runtime-fixup (D69's
   bg/stan work is the template for the former; D54's cseq fixup is the
   template for the latter — given the struct-width mismatch on top of
   the byteswap, a runtime fixup that manually walks the *raw* 40-byte
   N64 header by hand (not through the live `stagesetup` struct) into a
   freshly-populated `g_CurrentSetup` is probably simpler than a full
   sidecar here, but verify against a raw ROM hex dump either way).
2. Once BUNKER1 loads past `proplvreset2`, re-check for further crashes
   in the same vein (this session found 3 in a row — D86, D87, D88 — each
   only reachable after the previous one was fixed; expect more).
3. Once BUNKER1 reaches a rendered frame with no fault, revisit **D85**
   (room primary/secondary GDL binaries decode to garbage via
   `texLoadFromGdl`) — use `GE_PCDUMP="<range>:10"` + `tools_pc/pixcount.py`
   to confirm non-black, non-degenerate content per the original D69
   acceptance bar. (This session's `GE_PCDUMP` captures around frame
   2100-2400 during attract-mode-driven "loading" were still on a HUD/menu
   screen, not real 3D geometry — don't read too much into pixel counts
   from before D88 is fixed.)
4. Do NOT touch D75/D76/D77 (parked, lower priority, unrelated).

## Debug tooling added this session (kept, env-gated, zero cost when unset)

- `GE_D86=1` — node-walk trace in `modelInitRwData` (model.c) + a
  load-identity probe in `load_object_fill_header` (objecthandler_2.c).
  Resolved the D86 crash; left in place since the same
  load_object_fill_header/modelInitRwData pipeline could surface new
  edge cases as more of the game becomes reachable.
- `GE_D87=1` — block-setup trace in `iterate_ramrom_entries_handle_camera_out`
  and consumer trace in `ramrom_replay_handler` (ramromreplay.c). Resolved
  the D87 crash; left in place as it's a rarely-exercised path (attract
  mode) worth having visibility into if it acts up again.
- Pre-existing `GE_D69STAN=1`, `GE_D69BB=1`, `GE_D69=1` — unchanged, still
  useful for the bg/stan/D85 load path (see prior session's HANDOFF
  entries, preserved in git history, for exactly what each logs).

## New: fast, deterministic BUNKER1 repro (no attract-mode wait)

Launch with `-level_09` as a program argument
(`./build-pc/ge007.x86_64.exe -level_09`) to skip the front end/attract
mode and load BUNKER1 directly — `boss.c:199-339` decodes `-level_XX`
(the two digit-chars are consumed as raw ASCII bytes:
`g_StageNum = tokenFindLevel[0]*10 + tokenFindLevel[1] - 0x210`; NTSC
`LEVELID_BUNKER1 = 9` → `"09"` since `'0'*10 + '9' - 0x210 = 9`). This is
now the preferred way to test BUNKER1-specific load/render work — it's
faster (crashes/completes in well under a minute vs. ~2+ min waiting on
attract mode) and deterministic (not dependent on which of 7 random demo
locations attract mode happens to pick).

## Environment / build

- `export PATH="/c/msys64/mingw64/bin:$PATH" && ./build-pc.sh ntsc-final`
  (~5 s). Build is GREEN with all D86/D87 work included.
- Run from the **repo root**, not `build-pc/`.
- Regenerate sidecars if missing: `python tools_pc/d69_emit.py ntsc-final`
  (bg/stan) and the D43/D50 model sidecar generator (pcmodels) — both
  gitignored, not checked in. (Both were already present in this dev
  environment this session.)
- `GE_PCDUMP="<start>-<end>:<stride>"` + `tools_pc/pixcount.py` for frame
  captures once D88 is fixed and a frame actually renders real BUNKER1
  geometry.
- Crash log: `ge007.crash.log` (repo root); symbolicate with
  `addr2line -e build-pc/ge007.x86_64.exe -f -C <pc>` (image base
  `0x140000000`).
- gdb **launch** mode is too slow for timing-dependent bugs (unchanged
  guidance). **New this session:** gdb **attach** mode
  (`gdb -batch -x cmds.txt -p <winpid>`, `<winpid>` = 4th column of
  `ps -p <bashpid>`) works well and is fast for "watch a global for a
  legitimate vs. corrupted write" questions on an already-running,
  not-yet-crashed process — see `docs/PCPortResearch.md` §F environment
  reminders for the exact recipe used to root-cause D87.

## Non-negotiables (unchanged, see AGENTS.md)

1. N64 build files untouched.
2. Game logic unmodified except narrow, documented `#ifdef PORT`
   ABI/layout exceptions (D86/D87 this session, both logged in
   `PCPortResearch.md` §F). D88 is explicitly **not** patched with a
   quick inline hack — it needs the same disciplined
   spec-then-convert/fixup treatment as D69, logged as an open finding
   instead per AGENTS.md's "stop and write it up" guidance for anything
   beyond a narrow, obviously-correct exception.
3. Offline sidecar conversion preferred over runtime fixup for whole
   ROM-asset formats (D69/D80-82 pattern) — likely the right call for
   D88 too, though a careful runtime fixup is also plausible; decide
   after the byte-level spec work.
