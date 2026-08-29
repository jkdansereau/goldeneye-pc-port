# BRIEF C3+C6 — prop/door/item model-load crashes (Aztec, Bunker2, Surface2)

Dispatch AFTER C1 lands (shares `tools_pc/d88_propdefs.py` +
`src/game/prop*.c` with C1 — do not run concurrently with C1).

Repo: `REPO_ROOT` (git `master`, Windows).

## READ FIRST
- `docs/HANDOFF.md` top, `docs/PORT-LEARNINGS.md` §A + §C (esp. the D122
  "polymorphic-record converter with a forgotten type" note).
- `docs/PCPortResearch.md` §F **D122, D120, D112, D53.2, D57, D67**
  (jump via index).
- `docs/BRIEF-D122-per-level-modelload.md` — direct predecessor, same class.
- `AGENTS.md` non-negotiables.

## SYMPTOM (WS4 sweep — `docs/LEVEL-STATUS.md`)
| Level | # | Site | Note |
|---|---|---|---|
| Aztec | 28 | `propobj.c:13601` `door->model->obj->RootNode->Child->Child` | fault 0x10 |
| Bunker2 | 27 | `propobj.c:13523` door displacement `linkedDoor` list walk | fault 0x8 |
| Surface2 | 43 | `loadobjectmodel.c:393` `PitemZ_entries[modelid].header->RootNode` | fault 0x0 — direct D122 continuation |

All three: a prop/door/item model pointer or model-id from a `Usetup*Z`
propDef record is wrong. D122 fixed the `[s16 obj][s16 pad]` half-swap for
6 `inherits ObjectRecord` types; these levels emit a door subtype or
another record type whose `model` / `linkedDoor` / `modelid` field is
still converted wrong (or a placeholder-sized type:
`VEHICLE`/`AIRCRAFT`/`TANK`/`AMMO`/`DEPOSIT_IN_ROOM` per
`d88_propdefs.py`). Run `tools_pc/d88_propdef_scan.py` for the per-level
type histogram; diff Aztec/Bunker2/Surface2 against the 7 passing levels.

## TASK
Root-cause all three (likely one converter gap), extend
`tools_pc/d88_propdefs.py` (+ `sizepropdef()` `#ifdef PORT` stride in
`src/game/prop.c` to match, per the D122 pattern). Resolve any
placeholder `PROPDEF_PC_BYTES` via `tools_pc/d88_layoutprobe.c`.

## FILES YOU MAY TOUCH
`tools_pc/d88_propdefs.py`, `d88_emit.py`, `d88_layoutprobe.c`,
`d88_propdef_scan.py`; `src/game/prop.c`, `src/game/propobj.c`,
`src/game/loadobjectmodel.c` (narrow `#ifdef PORT`); new `tools_pc/c3_*`
probes; `docs/PORT-LEARNINGS.md`, `docs/PCPortResearch.md` §F,
`docs/LEVEL-STATUS.md`.

## KNOWN-GOOD / RULED OUT
D121, D122 (committed `f2beae4b`), the 7 passing levels' prop/model load,
C1's chr fix (landed first — rebase on it).

## PRE-FLIGHT / ENV / BUDGET / CONSTRAINTS / REPORT
Same as `docs/BRIEF-C1-chr-airecord.md` (repro `-level_28` / `-level_27` /
`-level_43`; `taskkill //F //IM ge007.x86_64.exe`; regen all 3 sidecars
after converter change; `#ifdef PORT` with N64 line under `#else`; verify
the 3 levels boot to a frame + `-level_09`/`-level_20` unregressed).
Budget: **~12 cycles / ~90 min.**

## STILL OPEN AFTER THIS (singletons, own briefs later)
- **C4** Depot 30 — `prop.c:902` `sp4C->room` after
  `walkTilesBetweenPoints_NoCallback` (BG tile/room table).
- **C5** Control 23 — `bg.c:5723` `portal_pts->numPoints`
  (`g_BgPortals[].offset_portal` unresolved/BE), via `chrprop.c:62`.
- **C7** Surface1 36 — `snd.c:653` `sndSetupSound` (audio subsystem is
  parked; may just need a narrow load-time guard until Phase 3).
