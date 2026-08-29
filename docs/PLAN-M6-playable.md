# Plan — Session M-6: toward a playable, low-glitch BUNKER1 PC build

Baseline: D112 committed (`67d674da`). Build green. `-level_09` renders
BUNKER1 continuously, crash-free. Three tracks below, in priority order.
Each track: **investigate → root-cause → narrow `#ifdef PORT` fix or
write-up** per AGENTS.md non-negotiables (no game-logic changes; ABI /
layout / format exceptions only, each documented in
`docs/PCPortResearch.md` §F).

## Track 1 — Portal-BFS under-reach ("the void")  [highest visual impact]

**Symptom:** in BUNKER storage area (rooms 27–29) the per-frame visible
room count collapses to 1–2; large sky-fill void where an adjacent room
should show through an open doorway. Portal *visibility* is otherwise
healthy post-D106 (room 18 → 8 rooms deep on enabled doorways).

**Known-good / ruled out:** `currentPlayerGetProjectionMatrix()`,
`g_CurrentPlayer->screensize`, the intentional `g_RoomLoadBudget` 200→3
FP-cam drop, `zfar`, the plane-side metric cull (`bg.c:4135/4144`).

**Suspects:** `sub_GAME_7F0B5864` screen-space portal projection
(`bg.c:1619`) — sibling of the D106 z==0 near-plane case, but for
partially-off-screen (not straddling) portals; `bgQueuePortalTraversal`
(`bg.c:3814/3843`) depth/dedup; `bgDetermineVisibleRooms` (`bg.c:4615`)
seed set; `sub_GAME_7F0B5528` clip-point generation (`bg.c:1551`);
`transform3Dto2DWithZScaling` (`bondview.c:730`).

**Deliverable:** root-cause writeup in §F ("D113"); if a narrow PORT fix
is obviously correct (e.g. another degenerate-box / clamp mismatch),
apply it, else stop and document.

**RESULT (Track 1 agent):** NOT a portal-BFS bug. The BFS output is
correct for BUNKER1's topology — rooms 26/27 sit behind portals 25/26
which carry `PORTALFLAG_DISABLED` (closed doors), culled identically on
N64. Stable 3 visible rooms {29,28,25}, not "1–2". The void is the
**closed-door prop models (obj 153/154) rendering with a wrong world
transform** — their leaf DLs are emitted (`GE_D96`) but land outside the
doorway, leaving the wall hole unoccluded → sky. **Folds into Track 2**
(same model-matrix defect as the inverted guard / mislocated crate). No
`bg.c` change. Probes reverted, tree clean.

## Track 2 — Character pose / orientation

**Symptom:** post-D112 a BUNKER guard renders as a coherent humanoid but
appears **inverted**. Also a mispositioned static crate prop (top-left in
the control room).

**Known-good:** joint *rotations* from the anim bitstream; `render_pos[]`
positions now sane (~±60 units) after the D112 f32 fix; `basemtx` from
`camGetWorldToScreenMtxf()` (`bondview.c:824`) was a clean orthonormal
lookat.

**Suspects:** matrix handedness / lookat sign in the joint base matrix;
`modelBuildGroupMatrices` / `process_02_position` / `subcalcmatrices`
composition order on LE; a converter sign/axis issue in `d43_emit.py`
group `Origin` or matrix rodata that survived the D112 fix (byte order
right, axis convention wrong); guMtxF2L / Mtxf→Mtx path.

**Deliverable:** root-cause writeup in §F ("D114"); narrow fix if
obviously correct.

## Track 3 — `struct player` raw-offset audit  [gate to playability]

**Symptom:** 1P weapon model doesn't draw. Landmine (§F D100): raw
hardcoded-offset accessors into `struct player` / `struct hand` are NOT
PORT-adjusted and are already PC-wrong.

**Known instances:** `gunfire.c` `THROWMTX_OFFSET 0xAD8` / `THROWPOS`
`0xB08` / `THROWPREV` `0xB48` (all `g_CurrentPlayer + handoffset + K`);
plus D102 (weapon Model pool) already fixed.

**Task:** enumerate *every* raw byte-offset / hardcoded-size access into
`struct player`, `struct hand`, and siblings across `src/game/` (grep for
`+ 0x`, `handoffset`, `(u8 *)`, hardcoded `0x2A80`-class sizes, pointer
arithmetic on player). For each: N64 offset vs PC offset (struct is
0x2A8-class larger per D100), whether it's on a BUNKER1-reachable path,
severity. Produce a table + recommended fix order. Do NOT fix yet —
this is a survey feeding a follow-up.

**Deliverable:** `docs/AUDIT-M6-player-offsets.md` table + §F pointer.

## Execution

- Tracks 1, 2, 3 dispatched as parallel investigation subagents
  (read-mostly; each returns a writeup + optional narrow patch).
- Integrator (this session) reviews each, applies/adjusts verified narrow
  fixes, rebuilds (`export PATH="/c/msys64/mingw64/bin:$PATH" &&
  ./build-pc.sh ntsc-final`), spot-checks `-level_09`, commits per-track
  with §F docs.
- Repro: `./build-pc/ge007.x86_64.exe -level_09 -ml0 -me0 -mgfx100
  -mvtx50 -mt700 -ma150`. Frame capture: `GE_PCDUMP="<range>:<stride>"` +
  `tools_pc/pixcount.py` / `tools_pc/pixcount.py`.
- Sidecar regen (if a converter changes): `python tools_pc/d43_emit.py
  ntsc-final`.
