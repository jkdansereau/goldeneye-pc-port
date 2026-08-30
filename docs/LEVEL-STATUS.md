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
| Cuba     | 54 | **PASS** 92.5% (loads+renders 300+ frames; bare-boot end-credits path `bondviewRenderCredits` still faults intermittently — D76/D129, not a real-flow blocker) | — | — |
| Dam      | 33 | **PASS** 83.3% | — (C1 fixed, D123) | — |
| Frigate  | 26 | **PASS** 90.4% | — (C1 fixed, D123) | — |
| Statue   | 22 | **PASS** 80.1% | — (C1 fixed, D123) | — |
| Streets  | 29 | **PASS** 91.6% | — (C1 fixed, D123; no C2 crash this run — timing) | — |
| Cradle   | 41 | **PASS** 55.4% (low — partial render, no crash) | — (C1 fixed, D123) | — |
| Runway   | 35 | CRASH (intermittent, ~3/4 runs; `0x1400d1d83`) | `import_texture_i8` — same as Facility | **C2** |
| Facility | 34 | CRASH | `import_texture_i8` gfx_pc.cpp:821 (fault 0x72181ee8) — model-GDL relocation align bug, D124, NOT fixed | **C2** |
| Jungle   | 37 | CRASH | `gfx_sp_matrix` gfx_pc.cpp:1046 (fault 0x401c68e0) — C2 texture crash fixed (D124), now explosion-DL `G_MTX` (D75/matrix family) | **C2m** |
| Aztec    | 28 | **PASS** 90.8% | — (C3 fixed, D125) | — |
| Bunker2  | 27 | **PASS** 91.5% | — (C3r fixed, D126) | — |
| Depot    | 30 | **PASS** 79.8% | — (C4 fixed, D126) | — |
| Control  | 23 | **PASS** 90.8% | — (C5 fixed, D128) | — |
| Surface2 | 43 | **PASS** 70.4% | — (C6 fixed, D126) | — |
| Surface1 | 36 | **PASS** 77.5% | — (C7 guarded, D127) | — |

**18 / 21 PASS.** D123 cleared C1 on all 6. D124 cleared Jungle's texture
crash (now hits an explosion-DL matrix crash). D125 cleared C3-Aztec.
**D126** (objective sub-record `->next` pointer widens 4→8B → 8-byte store
clobbers the next propdef record's header → walk desync → wrong command
indices) cleared **C3r Bunker2 + C4 Depot + C6 Surface2** in one fix
(`d88_propdefs.py` types 30/32/33/35 → 24B/6w). **D127** guarded a bogus
ALSound* in the parked audio path → **C7 Surface1**. **D128** fixed a
hardcoded-N64-stride portal-flag write → **C5 Control**. **3 crashes
remain, all one class:** C2 Runway + Facility + C2m Jungle — the model /
prop GDL relocation misaligns on the 8→16B `Gfx` stride
(`docs/BRIEF-C2gdl-model-reloc.md`).

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

### C3/C6 update (M-14/M-15, D125) — offline pipeline RULED OUT, bug is runtime
Symptom: converted `propDefs` blob in RAM does not match the offline output
(after record 0 it is zeros/garbage), so the runtime `sizepropdef()` walk
drifts and `setupDoor`/`modelLoad` get the wrong `pdefIndex`/modelid.

**M-15: the offline side is proven correct.** `tools_pc/d125_check.py`
byte-compares the emitted `pccg.bin` propDefs slice (post-RZ-roundtrip, at
the relocated header offset) to `convert_stream()` → **MATCH for all 21
levels**. The M-13 overseer hypothesis (pass-1 delta / `pd_end` mismatch in
`d88_emit.py` ~L308–340) is **disproven**: instrumented run shows
`tiled_pd_end == H[intro]` and `_n64len == pd_end - pd_start` exactly for
all 21, next region always `intro`. `sizepropdef()` PORT strides re-checked
vs `PROPDEF_PC_BYTES/4` — all 28 match.

**→ The corruption is at/after runtime load.** Prime suspect:
`decompressdata()` truncation in `port/src/rzdecomp.c` for the larger
converted setup files (the `while (ret == Z_OK)` inflate loop), or the
STAGE bank buffer / `langLoadToAddr` (`prop.c:1274`) overwriting the tail.
Next step: probe `decompressdata()` `ret`/`produced` vs expected size for
`UsetupsevbZ`. See §F/§H D125.

### C3 — Aztec FIXED (D125); Bunker2 residual (C3r)
**Aztec** was D125 (boundpad plink-name blob drift from the
`d88_emit.py:374` slice-width bug corrupting `stanPackId` → NULL stan →
door `model=NULL`). Fixed + regen'd, Aztec PASSES.

**Bunker2 (C3r)** — `door7F054FB4` `propobj.c:13536`, `var_s1` = `0xffff..`
walked from `linkedDoor`. `door->linkedDoor` is resolved at setup from
`door->linkedDoorOffset` (`prop.c:1204-1206`, N64 struct word 32 / 0x80) —
a read-before-write int id, D123's `ailist` pattern. `d88_propdefs.py`'s
DOOR handler (`DOOR_TAIL_PTR_WORDS`, tail loop from word 32) vs the
compiled PC `DoorRecord` layout is the suspect: either `linkedDoorOffset`
lands at the wrong PC byte, or a tail pointer word's 8-align cursor
diverges from the compiler's struct layout. Bunker2 has linked
double-doors; Aztec's doors are singletons (`linkedDoorOffset==0`) so it
never exercised this path. Needs `offsetof`/`sizeof` cross-check of PC
`DoorRecord` vs the converter cursor. Files: `tools_pc/d88_propdefs.py`,
`src/bondtypes.h` (DoorRecord), `src/game/prop.c`, `src/game/propobj.c`.

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

## Next (3 crashes, 1 class)
1. **C2 / C2-GDL** Runway + Facility — model/prop GDL relocation writes
   non-16-aligned `dst` (N64 8B `Gfx` vs PC 16B stride) in the
   `texLoadFromGdl` / `sub_GAME_7F0762E0` path → garbage `G_SETTIMG` w1 →
   `import_texture_i8` fault. `docs/BRIEF-C2gdl-model-reloc.md` — its own
   focused session (D80/D82/D83 area).
2. **C2m** Jungle — renders ~300 frames then an explosion-DL `G_MTX`
   (`gfx_sp_matrix`), D75/matrix family. May be downstream of the same
   GDL-reloc infra.

Everything else loads + renders + survives the no-input window.
Hand `docs/LEVEL-PLAYTEST.md` to the user for the WS6 completion pass.

Re-run `tools_pc/level_sweep.sh` after each class fix; keep
`-level_09`/`-level_20` green (`tools_pc/framediff.py`).
