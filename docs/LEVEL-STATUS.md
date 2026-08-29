# LEVEL-STATUS — 21 solo-level load+render sweep (WS4)

Method: bare `./build-pc/ge007.x86_64.exe -level_XX` (per-level `-m*` pools
auto-injected, D121), `GE_PCDUMP="80-260:40"`, ~24 s watchdog then
`taskkill //F //IM ge007.x86_64.exe`, `ge007.crash.log` symbolicated with
`addr2line -e build-pc/ge007.x86_64.exe -f -C <PC>` (image base
0x140000000). "render %" = `tools_pc/pixcount.py` non-clear on the last
captured frame. Binary at `f2beae4b` + this session's build.

Last full re-sweep: 2026-08-29, after D123 (C1) + D124-Jungle (C2) landed.
Build = `f2beae4b` + `tools_pc/d88_propdefs.py` (D123) + `port/src/gimgfixup.c` (D124).

| Level | # | Status | Crash site | Class |
|---|---|---|---|---|
| Bunker1  | 09 | **PASS** 91.7% | — | — |
| Silo     | 20 | **PASS** 91.7% | — | — |
| Archives | 24 | **PASS** 91.1% | — | — |
| Train    | 25 | **PASS** 87.0% | — | — |
| Caverns  | 39 | **PASS** 91.6% | — | — |
| Egypt    | 32 | **PASS** 90.9% | — | — |
| Cuba     | 54 | **PASS** 92.5% | — | — |
| Dam      | 33 | **PASS** 83.3% | — (C1 fixed, D123) | — |
| Frigate  | 26 | **PASS** 90.4% | — (C1 fixed, D123) | — |
| Statue   | 22 | **PASS** 80.1% | — (C1 fixed, D123) | — |
| Streets  | 29 | **PASS** 91.6% | — (C1 fixed, D123; no C2 crash this run — timing) | — |
| Cradle   | 41 | **PASS** 55.4% (low — partial render, no crash) | — (C1 fixed, D123) | — |
| Runway   | 35 | CRASH | `import_texture_i8` gfx_pc.cpp:821 (fault 0x721b8ee8) — same as Facility | **C2** |
| Facility | 34 | CRASH | `import_texture_i8` gfx_pc.cpp:821 (fault 0x72181ee8) — model-GDL relocation align bug, D124, NOT fixed | **C2** |
| Jungle   | 37 | CRASH | `gfx_sp_matrix` gfx_pc.cpp:1046 (fault 0x401c68e0) — C2 texture crash fixed (D124), now explosion-DL `G_MTX` (D75/matrix family) | **C2m** |
| Aztec    | 28 | CRASH | `door7F054FB4` propobj.c:13601 (`door->model->obj->RootNode->Child->Child`) | **C3** |
| Bunker2  | 27 | CRASH | `door7F054FB4` propobj.c:13523 (door displacement list walk) | **C3** |
| Depot    | 30 | CRASH | `sub_GAME_7F00324C` prop.c:902 (`sp4C->room` after `walkTilesBetweenPoints`) | **C4** |
| Control  | 23 | CRASH | `sub_GAME_7F0BA2D4` bg.c:5723 (`portal_pts->numPoints`, via chrprop.c:62) | **C5** |
| Surface2 | 43 | CRASH | `modelLoad` loadobjectmodel.c:393 (`PitemZ_entries[modelid].header->RootNode`) | **C6** |
| Surface1 | 36 | CRASH | `sndSetupSound` snd.c:653 (fault 0x56220001…, ascii-ish) | **C7** |

**12 / 21 PASS.** D123 cleared C1 on all 6 (Dam/Frigate/Statue/Cradle/Streets
PASS; Runway falls through to C2). D124 cleared Jungle's texture crash but
it now hits an explosion-DL matrix crash. 9 crashes remain in 6 classes.

Notes: Streets passed this run but the C1 agent saw a `gfx_sp_matrix`
crash — likely the same explosion-DL `G_MTX` (C2m) as Jungle, timing-
dependent (D117 nondeterminism). Cradle renders only 55% — watch for a
partial-load issue when it gets a playtest. Frigate/Statue vary 68–90%
run to run.

## Crash classes (most-impactful first)

### C1 — chr / AI-record pointer deref — FIXED (D123)
**RESOLVED.** Root cause: `tools_pc/d88_propdefs.py` (D122's `OBJ_TAIL_DESC`)
zeroed the widened `VehichleRecord/AircraftRecord.ailist` slot instead of
carrying its pre-populated int AI-list id, so `prop.c:1764/1786` →
`ailistFindById(0)` → `GAILIST_AIM_AT_BOND` → `ai()` ran a CHR aim list
against `ChrEntityp==NULL`. Fixed via `OBJ_ID_WORDS`. See §F/§H D123.
Dam/Frigate/Statue/Cradle now PASS; Runway/Streets fall through to C2.

<details><summary>original triage notes</summary>

`chrIsNotDeadOrShot(ChrRecord *self)` faults on `self->actiontype` with
`self` bogus (Dam: Rdi=0x1401296a0 → an image address; fault addr 0x8).
Reached from `chraidata.c:61` (`m_AimAtBond[]` AI-list region — backtrace
frame is inside the global AI-list rodata blob, i.e. the return address
was corrupted OR an AI-list entry is being called as a function pointer).
Pointer-width / BE-rodata family (PORT-LEARNINGS §A / §C): a per-level
`Usetup*Z` guard record (`GuardRecord` type 9 / `GuardAttributeRecord`
type 18) or chr-spawn pointer not converted / not pointer-width-adjusted,
OR the AI-list bytecode pointers in `chraidata.c` rodata are BE and get
called raw. **Biggest single win — 6 levels.**

</details>

### C2 — fast3d bad texture pointer (D124) — SPLIT into two causes
**Jungle (`0xabcd0824`) — FIXED (D124).** `gimgSyncCompiledGlobalDLs()`
never copied the resolved texture pointers into the compiled
`globalDL_0xNNN` explosion DLs (slot-detect keyed on a marker `texLoad()`
had already erased); the arrays kept link-time `IMAGESEG` words and the
first explosion-DL draw fed `0xABCDxxxx` into fast3d. Latent on every
level. Fix: `port/src/gimgfixup.c` detects the slot from the compiled
array. Jungle now renders ~300 frames then hits a *separate* explosion-DL
`G_MTX` crash (`gfx_sp_matrix` gfx_pc.cpp:1046 — D75/matrix family).

**Facility (`0x72181ee8`) — NOT fixed, separate cause.** Bogus `G_SETTIMG`
w1 in a model/prop GDL produced by the runtime `texLoadFromGdl()`
relocation in `sub_GAME_7F0762E0` (`objecthandler_2.c:82`): its model-GDL
output `dst` pointers are non-16-aligned (N64 8B vs PC 16B `Gfx` stride
mix in the `replacementgdl`/`name` offset math). Open D80/D82/D83
"model/room GDL runtime conversion unverified" area — shared infra, needs
a dedicated pass. Files: `src/game/objecthandler_2.c`, `src/game/tex.c`
(`texLoadFromGdl`), `src/game/model.c` (`modelNodeReplaceGdl`).

**M-14 update (partial, still NOT fixed):** crash cmd pinned —
`G_SETTIMG w0=0xfd900000 w1=0x72181ee8` where `w1` is a garbage
`tex->data` from the texture-pool path (`texLoadFromModelFileHeader` →
`texLoad`), not solely the GDL command-stream copy. Every model file
loads at an odd (`&15==1`) `objheader->Switches` base
(`mempAllocBytesInBank` does no alignment) and `delta&15` != 0.
Full write-up + next-step probe plan: PCPortResearch §F D124-Facility
addendum. Likely fix site: the `d43_emit.py`/`pcmodels` sidecar's model
texture-blob offsets (N64 8B-Gfx vs PC 16B-Gfx GDL extent).

### C3/C6 update (M-14, D125) — NOT a converter type-gap
Root cause narrowed: the converted `propDefs` blob in RAM does **not** match
`tools_pc/d88_propdefs.py`'s offline output (after record 0 it is
zeros/garbage), so the runtime `sizepropdef()` walk drifts +101 records by
the first door → `setupDoor`/`modelLoad` get the wrong `pdefIndex`/modelid.
Histogram diff is clean (no type absent from passing levels). Suspect a
destination-offset / `pd_end` / header-reloc bug in `tools_pc/d88_emit.py`'s
propdefs emit path (retrofitted post-D122; docstring still says "opaque
passthrough"). Not fixed — see §F/§H D125. **d88_propdefs.py `convert_stream`
itself and `sizepropdef()` PORT strides are verified correct.**

**Overseer note (M-13, unverified — first task next session):** likely a
pass-1 delta / region-`end` mismatch in `d88_emit.py` ~L308–340. The
tiled `propdefs` region's `end` (pd_end, from the region tiling) may not
equal `pd_start + convert_propdefs()._n64len`. Pass 1 does
`cum += len(propdefs_pc) - (end - start)` with that `end`; if it is
**shorter** than the real record-stream length, the cumulative delta is
under-counted, every subsequent region (aistream / opaque) is placed too
low, and the emit loop's `out[do:...] = src[...]` for the next region
**overwrites the tail of `propdefs_pc`** — leaving record 0 intact and the
rest clobbered, exactly the observed symptom. Cheap confirm: print
`pd_start`, `pd_end`, `_n64len`, `len(propdefs_pc)`, and the next region's
`(start, do)` for `UsetupsevbZ`; check `pd_end == pd_start + _n64len`.
Fix = set the propdefs region `end` to `pd_start + _n64len` (or make
`convert_propdefs` and the tiler agree on the boundary).

### C3 — door model / ModelNode walk (2 levels: Aztec, Bunker2)
`propobj.c:13601` derefs `door->model->obj->RootNode->Child->Child`;
`:13523` walks the `linkedDoor` list. Fault addrs 0x10 / 0x8 → a null-ish
base + field offset. Door prop model not loaded, or `door->model` /
`->linkedDoor` pointer-width mis-set. Same family as D122 (propDef obj
half-swap) — Aztec/Bunker2 emit a door subtype BUNKER1/Silo don't, or the
door propDef `linkedDoor`/`model` field is pointer-width-sized wrong.
Files: `src/game/propobj.c`, `tools_pc/d88_propdefs.py` (door handler),
`src/game/prop.c`.

### C4 — prop tile-walk room deref (1: Depot)
`prop.c:902` — `*arg1 = sp4C->room` where `sp4C` came back bad from
`walkTilesBetweenPoints_NoCallback`. Likely a BG tile/room table not
converted for Depot. Files: `src/game/prop.c`, `src/game/bg.c`, BG
sidecar.

### C5 — bg portal points (1: Control)
`bg.c:5723` — `portal_pts = g_BgPortals[idx].offset_portal; portal_pts->numPoints`.
`offset_portal` unresolved / BE. Via `chrprop.c:62` (chr line-of-sight
through portals). Files: `src/game/bg.c`, BG portal sidecar.

### C6 — modelLoad PitemZ header (1: Surface2)
`loadobjectmodel.c:393` — `PitemZ_entries[modelid].header->RootNode`,
`header` NULL / `modelid` OOB. Direct D122 continuation (prop/item model
id from a propDef record). Fault addr 0x0. Files: `tools_pc/d88_propdefs.py`,
`tools_pc/d43_emit.py`, `src/game/loadobjectmodel.c`.

### C7 — sndSetupSound (1: Surface1)
`snd.c:653`. Audio is a parked subsystem (Phase 3) but this is a hard
crash on load, not silence. May be dodge-able with a narrow guard until
audio lands. Files: `src/snd.c`, `port/src/` audio stubs.

## Next (9 crashes, 6 classes — priority order)
1. **C2** Runway + Facility — model-GDL relocation align bug in
   `objecthandler_2.c`/`texLoadFromGdl` (D80/D82/D83 area). Shared infra,
   2 levels + likely helps Jungle/Streets. Biggest win.
2. **C3** Aztec + Bunker2 — door model / `linkedDoor`; brief
   `docs/BRIEF-C3-C6-prop-model.md` (dispatch after any `d88_propdefs.py`
   work settles).
3. **C2m** Jungle (+ Streets intermittent) — explosion-DL `G_MTX`
   (`gfx_sp_matrix`), D75/matrix family.
4. **C6** Surface2 — `PitemZ_entries` modelid (D122/D123 continuation).
5. **C5** Control — BG portal `offset_portal` resolve.
6. **C4** Depot — BG tile/room table.
7. **C7** Surface1 — `sndSetupSound`; parked-audio, may just need a narrow
   load guard.

Re-run `tools_pc/level_sweep.sh` after each class fix; keep
`-level_09`/`-level_20` green (`tools_pc/framediff.py`).
