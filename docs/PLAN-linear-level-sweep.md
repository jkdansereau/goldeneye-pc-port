# PLAN — Linear pass: boot → menu → every solo level loads & renders

Paste-ready brief for a fresh session. Pivot from depth-first
(BUNKER1 + graphics rabbit-holes) to **breadth-first**: make the normal
player path work end-to-end (intro → credits → main menu → mission/level
select), then confirm **all 21 solo levels load and render** without
crashing. Turns the remaining work into a scoped list.

## Preflight (unchanged)

`CLAUDE.md` + `AGENTS.md` non-negotiables → `docs/HANDOFF.md` →
`docs/PORT-LEARNINGS.md` → `docs/AGENT-WORKFLOW.md` before any subagent.

## Scope decisions (locked with the user)

- **Intro / credits / menus:** bar is *functional, not crashing*. Menus
  navigable start to finish; the intro is skippable with any button.
  Cosmetic defects are **out of scope** here — log them in
  `docs/GRAPHICS-BACKLOG.md`: gun-barrel Bond model missing (D75),
  Nintendo-logo transform (D75), mirrored glyphs (D114/D116), no audio
  (D77), partial disclaimer screen (D76).
- **Text X-mirror (D114/D116):** stays parked. Menus are navigable with
  mirrored text.
- **Level sweep depth:** *load + render + no immediate crash* only. A
  "soak" is pointless with no input (level triggers never fire). Play-to-
  completion validation is a **separate human step** (complex input),
  driven by a per-level checklist this plan produces (WS6).
- **N64 reference captures:** deferred. Use PC golden frames +
  `tools_pc/framediff.py`; eyeball against memory/video for "matches
  original". `tools_pc/mupen64/` is available if a level looks wrong.

## Facts established (exploration, this session)

**Front end is a stage** — `LEVELID_TITLE` (0x5A). Logic in
`src/game/front.c` (~8700 lines) + `src/game/title.c` (gun-barrel/logo
3D) + `src/game/ramromreplay.c` (attract demos). State machine = `MENU_*`
enum (`src/bondconstants.h:1831`), driven per frame by `menu_init()`
(`front.c:8536`) through three switch tables (`update_/init_/interface_
menuXX_*` at `front.c:8598/8637/8666`); rendered by
`menu_jump_constructor_handler()` (`front.c:8718`). Transitions:
`frontChangeMenu(menu, reload)` (`front.c:8513`). Boot entry:
`initgamedata.c:9` sets `menu_update = MENU_LEGAL_SCREEN`.

**Intro states** (each: `init_` loads assets, `interface_` advances on
timer OR any button, `constructor_` renders):
`MENU_LEGAL_SCREEN` → `NINTENDO_LOGO` → `RAREWARE_LOGO` → `EYE_INTRO`
(gun-barrel walk, "007", blood) → `GOLDENEYE_LOGO` → `DISPLAY_CAST`
(credit roll) → idle → `select_ramrom_to_play()` (attract demo).
Any button during intro sets `prev_keypresses` → logos short-circuit to
`MENU_FILE_SELECT`, cast roll skipped. Gun-barrel sub-state machine is
`gunbarrel_mode` (u8) in `title.c` (`insert_bond_eye_intro` `title.c:297`,
`renderGunbarrelEyeIntroSequence` `title.c:608`).

**Menu → level launch:** mission select →
`selected_stage = mission_folder_setup_entries[…].stage_id`
(`front.c:3301`) → `MENU_DIFFICULTY` → `MENU_BRIEFING` /
`MENU_007_OPTIONS` → `init_menu0B_runstage()` (`front.c:6900`) →
`bossSetLoadedStage(selected_stage)` → `g_MainStageNum` → `bossMainloop`
inner loop exits → `g_StageNum = g_MainStageNum` (`boss.c:647`) →
`lvlStageLoad`. **Level → level always routes back through the front end**
(`MENU_MISSION_COMPLETE`, `interface_menu0D_missioncomplete`
`front.c:7119`; auto-advance scans `mission_folder_setup_entries` for the
next `stage_id >= 0`). No direct in-stage jump.

**21 solo levels; LEVELID value == `-level_XX` number** (`boss.c:337`
decode `d0*10 + d1 - 0x210`). Mission order + numbers:
Dam 33, Facility 34, Runway 35, Surface1 36, Bunker1 09, Silo 20,
Frigate 26, Surface2 43, Bunker2 27, Statue 22, Archives 24, Streets 29,
Depot 30, Train 25, Jungle 37, Control 23, Caverns 39, Cradle 41,
Aztec 28, Egypt 32, Cuba 54. Enum: `src/bondconstants.h:1633-1685`;
mission table `mission_folder_setup_entries[]` `front.c:434-466`.

**Per-level memory args already have a table** — `memallocstringtable[]`
(`boss.c:101-142`), one row/LEVELID (`-ml -me -mgfx -mvtx -mt -ma`). The
auto-inject loop `boss.c:369-414` already selects the right row per stage
load — but it is gated on `g_DebugAndUpdateStageFlag`, which the port
only sets when `-level_` is **absent** (`boss.c:199-202`;
`rmonGetToken()` → 0 on PC, `port/src/n64stubs.c:134`). So bare
`-level_09` today uses default pools; hence the manual
`-ml0 -me0 -mgfx100 -mvtx50 -mt700 -ma150` in the HANDOFF repro line.
Documented TODO: `PCPortResearch.md` §H (D95 area) and `docs/HANDOFF.md`
"Next task".

**D88.4 is a cross-level blocker.** Every level's `Usetup*Z` feeds the
`propDefs` polymorphic record stream, which `tools_pc/d88_emit.py`
deliberately leaves un-byteswapped. BUNKER1 crashes in `setupDoor`
(`prop.c:971`) on a garbage BE `door->obj`. Any level hits the same class
when its prop set is walked. Groundwork: `tools_pc/d88_propdefs.py`,
`tools_pc/d88_layoutprobe.c` (machine `SZ`/`PTR` lines),
`tools_pc/d88_propdef_scan.py` (per-level type histogram).
Placeholder sizes still to resolve in `d88_propdefs.py`:
`VEHICLE`/`AIRCRAFT`/`TANK`/`AMMO`/`DEPOSIT_IN_ROOM`.

**Tooling:** `GE_PCDUMP="a-b:step"` → `./ppm/` (`port/src/video.c:181`);
`tools_pc/pixcount.py` (non-black pixel count / content check),
`tools_pc/framediff.py` (structural regression vs `tools_pc/golden/` —
the port is NOT frame-deterministic, D117), `tools_pc/ppm2bmp.py`.
Crash log `ge007.crash.log`; symbolicate
`addr2line -e build-pc/ge007.x86_64.exe -f -C <pc>` (image base
0x140000000). Objectives: `propDefs` `PROPDEF_OBJECTIVE_*`
(`bondconstants.h:4332`), status `src/game/objective_status.c`
(`get_status_of_objective()` `:161`) — needed only for WS6.

## Workstreams

WS1 is a prerequisite for all. WS2 (`front.c`/`title.c`/`lv.c`/
`ramromreplay.c`/`initmenus.c`/`port/`) and WS3 (`tools_pc/d88*` +
narrow `prop.c`) are disjoint → parallelisable. WS4 depends on WS3.
WS5 iterative. WS6 is the user's.

### WS1 — Frictionless per-level boot (small, first)

Goal: `./build-pc/ge007.x86_64.exe -level_XX` with no other args boots
any level with its correct pools.

Change: in `src/boss.c` under `#ifdef PORT`, also set
`g_DebugAndUpdateStageFlag = 1` when `-level_` **is** present (mirror of
`boss.c:199-202`). The existing loop `boss.c:369-414` then supplies the
right `memallocstringtable` row per stage; the generic preload at
`boss.c:207-210` is harmless (per-loop `tokenSetString` at `:413`
overrides). Debug-harness / ABI-class `#ifdef PORT` edit (same character
as the `-level_` ASCII parse already in the file). Document as a new Dxx
in `PCPortResearch.md` §F. Alt if boss.c is judged out of bounds:
synthesize the string in the port token layer (`port/src/system.c`
`sysGetTokenString`). Boss.c is simpler and preferred.

Also update `docs/HANDOFF.md` repro line; drop the manual `-m*` list once
verified.

Verify: `-level_09` alone == today's full-arg behaviour (same frame,
`framediff.py` green vs `tools_pc/golden/`).

### WS2 — Front-end walkthrough & crash repair (human playtest loop)

Goal: a human can go boot → (watch or skip intro) → file/folder select →
mode select → mission select → difficulty → briefing → level launches,
and after a level, mission-complete → next briefing, with **no crash /
hang / dead-end**. Cosmetic wrongness acceptable and logged.

Method: same loop as the M-10 input playtest — overseer builds, human
drives, defects come back as a list, overseer root-causes each (expect
the pointer-width / BE-rodata / N64-Gfx-sizing classes in
`PORT-LEARNINGS.md`), narrow `#ifdef PORT` fix, re-test.

Files in scope: `src/game/front.c`, `src/game/lv.c`
(`interface_menu05_fileselect` is here), `src/game/title.c`,
`src/game/initmenus.c`, `src/game/ramromreplay.c`, `port/src/*`. NOT
`prop.c` / `tools_pc/d88*` (WS3).

Human checklist:
1. Boot → legal → Nintendo → Rareware → gun-barrel → GE logo → cast roll
   → (idle) attract demo. Note any crash/hang; cosmetic noted separately.
2. Press a key during intro → confirm short-circuit to file select.
3. File/folder select: create a folder, enter it; folder-delete sub-UI;
   30 s idle → back to legal screen.
4. Mode select → Mission select: page through all 20 mission entries,
   each briefing text/photo loads without fault.
5. Pick mission + Agent difficulty + briefing → Start → level loads.
6. In-level: die / abort → mission-failed → menu. Complete a short level
   (or cheat) → mission-complete → auto-advance to next briefing.
7. MP chain (lower priority): MP options → scenario/stage/char/handicap/
   teams → MP match loads.

Do NOT fix here (log/confirm in `GRAPHICS-BACKLOG.md`): D75, D76, D77,
D114/D116.

Deliverable: front-end crash list + root causes + fixes;
`GRAPHICS-BACKLOG.md` updated with confirmed cosmetic state.

### WS3 — D88.4 propDefs converter (cross-level blocker)

Goal: `Usetup*Z` `propDefs` converts correctly offline for all 21 levels
so `setupDoor` / prop instantiation stops reading BE garbage.

Method: subagent brief, same shape as D69 / D88.1-3 (offline sidecar
converter). Byte-spec every `PROPDEF_*` record type (~40, enum
`bondconstants.h`), per-type field byteswap + pointer-width audit,
extend `tools_pc/d88_emit.py`. Use `d88_propdefs.py` /
`d88_layoutprobe.c` / `d88_propdef_scan.py`. Resolve the placeholder
sizes (`VEHICLE`/`AIRCRAFT`/`TANK`/`AMMO`/`DEPOSIT_IN_ROOM`) via
`d88_layoutprobe.c`.

Files: `tools_pc/d88_emit.py`, `tools_pc/d88_propdefs.py`,
`tools_pc/d88_layoutprobe.c`, + narrow `#ifdef PORT` in `src/game/prop.c`
if a struct-width mismatch surfaces (D79/D88 class).

Budget: structural — generous. On expiry: write up spec progress in
§F/D88 with confidence rating.

Verify: `-level_09` walks past `setupDoor` to a rendered BUNKER1 frame
with props; spot-check 2-3 other levels' prop walk.

### WS4 — Per-level load + render sweep

Goal: status matrix for all 21 solo levels — (a) `lvlStageLoad`
completes without fault, (b) ≥1 non-degenerate frame renders, (c)
survives ~300 frames / ~15 s with no input (immediate-crash check only).

Prereq: WS1 + WS3 landed.

Method: agent-driven, one level at a time (`-level_XX`). Each: launch
`GE_PCDUMP="1-300:30"`, capture `ge007.crash.log`, run `pixcount.py`.
Record PASS / CRASH@<site> / RENDERS-EMPTY / HANG. Symbolicate every
crash (`addr2line`). Group failures by root-cause class.

Deliverable: `docs/LEVEL-STATUS.md` — table (level, number, load, render,
crash site, notes), refreshed as WS5 closes items.

### WS5 — Per-level crash triage (iterative)

Goal: drive the WS4 matrix to "all 21 load + render".

Method: one focused investigation per distinct failure class (subagent
brief per `AGENT-WORKFLOW.md` template; read `PORT-LEARNINGS.md` first).
Expected classes: pointer-width struct growth in per-level assets,
BE-rodata not converted, N64-Gfx/Vtx sizing, per-level pool exhaustion
(`memallocstringtable` PC bumps, cf. D95). Fix = narrow `#ifdef PORT` or
converter extension; document each in §F.

Verify per fix: target level's WS4 check → PASS *and* `-level_09`
framediff stays green.

### WS6 — Human completion-validation pass (user-run, after WS4 green)

Goal: confirm each level is playable start to finish and matches the
original — the part needing real input.

Deliverable produced for the user: `docs/LEVEL-PLAYTEST.md` — per-level
checklist template: spawn point; each difficulty-gated objective
(`objective_status.c`) reachable and registering COMPLETE; guards / props
/ doors behave; level exit / mission-complete fires clean; timing +
layout match retail. User fills it per level; findings feed a WS5-style
triage round.

## Verification (overall)

- Per merged patch: `export PATH="/c/msys64/mingw64/bin:$PATH" &&
  ./build-pc.sh ntsc-final` green, `/linkcheck` clean, single-frame
  `GE_PCDUMP` diff vs `tools_pc/golden/` via `framediff.py`.
- WS1: `-level_09` alone == old manual-arg behaviour.
- WS3: BUNKER1 renders props past `setupDoor`.
- WS4/WS5: `docs/LEVEL-STATUS.md` all 21 rows = load+render PASS.
- Regenerate sidecars after any converter change: `python
  tools_pc/d43_emit.py ntsc-final && python tools_pc/d69_emit.py
  ntsc-final && python tools_pc/d88_emit.py ntsc-final --regen`.

## Out of scope (tracked elsewhere — do not start here)

- D75 / D76 / D114 / D116 / D77 and all `docs/GRAPHICS-BACKLOG.md` items.
- D118a/b input polish, rebinding, gamepad hotplug, `ge007.ini`.
- Weapon-model-doesn't-draw (AUDIT-M6 #5), rest of the `struct player`
  offset audit.
- Audio (Phase 3), saves/EEPROM (Phase 4), `docs/BACKLOG.md` (PC
  graphics options, co-op, netplay).
