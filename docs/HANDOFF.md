# Handoff brief — GoldenEye 007 PC port (Phase 1.5 done, Phase 2: first frames render)

_Paste-ready briefing for the next agent session. The authoritative detail lives in
`AGENTS.md` and `docs/PCPortResearch.md` (§H handoff, §F/D31–D46 findings); this is
the summary + the immediate tasks._

## Your job
One task left on D43 (everything else is done and validated):
1. ~~**D44:** one-line fix to the crash handler~~ — **DONE, committed.** Every
   crash now auto-logs a `BACKTRACE:` section to stdout and `ge007.crash.log`.
2. **D43 (main task):** model-file loading ABI mismatch — stage object load faults
   in `modelPromoteNodeOffsetsToPointers()` (src/game/model.c:5688) because ROM model
   files are serialized with N64 layout (4-byte pointer fields) but read on PC as
   8-byte-pointer structs. **Investigation COMPLETE; the converter spec is fully
   pinned down and validated 512/512 by `build-pc/d43_convert.py`** (see §F/D47).
   What remains: **the C implementation of `romdataFixupModelFile()` in
   `port/src/romdata.c`, the hook in `load_object_fill_header`, build + soak.**
   After that keep pushing rendering forward (more asset types will fault the
   same way — Tbg_*_stanZ backgrounds next).

Work agentically — fix → build (~5 s) → verify → commit at each working milestone.
Commit message style: `PC port: <phase> — <what>`.

## Read first (authoritative, in order)
1. `AGENTS.md` — non-negotiables, critical files, build, verification ritual, phase status.
2. `docs/PCPortResearch.md` §H (handoff + plan), then **§F/D47** (converter
   contract finalized — read before writing the C converter), **D46** (overlap
   safety + final buffer sizing), **D45** (buffer constants), **D43** (the open
   model-file task), **§2.4** (PD-port copy-candidate audit —
   read before starting D43 or Phase 3/4), **D37** (the libaudio bank-tree
   re-layout pattern), D39–D42 for the first-render fixes, D31–D38 for background.

## Non-negotiables (the ones that bite)
- N64 build files (`Makefile`, `tools/`, `rsp/`, `ld/`) — never touch.
- **Game logic is unmodified.** Hardware deps go in `port/`. Narrow exception: mechanical,
  semantics-preserving ABI/layout fixes forced by the 32→64-bit transition (embedded
  pointers → u32 + cast at use; PC-guarded pool sizing / idiom branches), each documented
  as a D3x finding. No behavior changes.
- Region macros in `CMakeLists.txt` must mirror the N64 Makefile exactly.

## Current state
- **D43 status (latest session, §F/D47):**
  - DONE + APPLIED (uncommitted): all step-2 mechanical game-code edits
    (bondtypes.h Vertex u32 union; model.c PROMOTE32; chr.c casts;
    objecthandler_2.c Textures offset; tex.c Gfx-size/dispatch/byte-check/
    copy-count fixes; gun.c/front.c/initmenus.c D45 buffer constants).
  - DONE (this session): **D46 buffer corrections applied** (front.c
    bufferRemaining 0x1C000→0x25000, initmenus.c logo 0x7C000→0x85000) and the
    **fast3d seg_addr seg-5 raw-VMA case** (port/fast3d/gfx_pc.cpp).
  - DONE (this session): every open converter question resolved — compaction
    state values (delta = R−P; accessors at 0x80090000), G_VTX w0 encoding
    (GBI1 style; fast3d already correct), G_TRI4 false-positive eliminated
    (opcode-aware remap rule), vertex formats verified, no embedded blobs,
    zero-vtx-array rule, reload hazard + poolRemaining-reset decision.
  - **VALIDATED:** `build-pc/d43_convert.py` implements the full conversion spec
    and runs it on all 512 ROM files: ALL CLEAN (every pointer remap resolves;
    max D_PC = 0xB7E0; max ratio D_PC/D_N64 = 1.31).
  - **REMAINING:** port that validated logic to C as
    `romdataFixupModelFile()` in port/src/romdata.c (two-pass: walk+layout,
    then emit PC image staged at the tail of the reserved block and memmove to
    F), hook it into load_object_fill_header (+ poolRemaining=0 reset before
    _fileNameLoadToBank, per D47.4), set poolRemaining = D_PC after conversion,
    build + run + 30 s soak.
- (Earlier session, verified live 2026-08-22)
- **Re-ran the committed build:** frames 1–4 render (`[NOTE] frame N rendered in … us`,
  frame 1 ~21 ms), then FATAL `0xc0000005` at PC `0x14007a31a` =
  `modelPromoteNodeOffsetsToPointers()` model.c:5688 — exactly D43. The black screen you
  see is the clear color only: display lists execute but no stage geometry has loaded yet.
- **Dev loop measured (16-core box):** full clean rebuild ≈ **5 s** wall; launch → crash
  ≈ **3.7 s**. Neither is a bottleneck — *diagnosis* is (gdb launch-only mode + manual
  addr2line arithmetic per fault). ccache and boot fast-forward are NOT worth it.
- **D44 fixed + committed:** `crashStackTraceSym()` in
  `port/src/crash.c` calls `GetCurrentThreadStackLimits(low, high)` — passing NULL pointer
  *values* instead of `&low, &high`. The API writes to address 0x0 *inside the SEH filter*
  → the process dies before printing the backtrace. That's why `ge007.crash.log` contains
  only Phase 1 (EXCEPTION/PC/MODULE lines) and no `BACKTRACE:` section, and why every D3x
  fault so far cost a gdb session. The build already uses `-fno-omit-frame-pointer`
  (CMakeLists.txt:193), so the EBP-chain walk will work once the call is fixed.
- **ROM dump verified:** byte-identical to the No-Intro good dump, header CRCs re-compute
  clean (`tools_pc/romverify.c`). Fingerprint + provenance in Environment below.
- Frames render because of D31–D42 (all documented in §F): boot chain (real-zlib
  decompression, animation-table ABI+endianness, ANIM_DATA lvalues, music seq-table fixup,
  music-heap sizing, libaudio bank-tree re-layout), D38 prototype shim (warning-clean
  build), D39 Globalimagetable rebasing (`GIMG_OFF()` idiom), D40 ModelHitEntry pool
  sizing, D41 WGL single-thread context currency (`gfx_sdl_release_context()`), D42
  rsp.c pointer-toggle truncation.

## ~~Immediate task 1 — D44~~ (DONE — committed)
The one-line fix (`GetCurrentThreadStackLimits(&low, &high)` in
`crashStackTraceSym()`) is in. Verified: `BACKTRACE:` with #00 at model.c:5688
and #01 `load_object_fill_header` appears on stdout + in `ge007.crash.log`.
Caveats learned: (a) frames past the true chain can be stale stack data —
validate a frame's address is inside `.text` before trusting it; (b) addr2line
wants the full absolute hex address directly (`addr2line -e
build-pc/ge007.x86_64.exe -f -C 0x14007a31a`).

## Immediate task 2 — D43: model-file loading ABI mismatch (INVESTIGATION DONE — implement)

### Verified facts (all checked against ROM ground truth, all 512 files / 1661 nodes — do NOT re-derive)

**Format / layout:**
- All 512 C*/G*/P*Z model files (Tbg_*_stanZ backgrounds are a different format/path — later) decompress fine; each file starts directly with the switches array (no in-file header — NS/NT come from the exe `ModelFileHeader` record via `objheader->numSwitches/numtextures`).
- File layout: `[switches NS×4B][textures NT×12B][root node @ 4NS+12NT]…`; all embedded pointers are vma `0x05xxxxxx` (mask 0xFFFFFF).
- **Tiling invariant verified, 0 failures in all 512 files** (`build-pc/d43_invariants.py`): `[0,D)` is exactly tiled by {switches, textures, nodes(24B), records(RSZ per opcode), vertex arrays(nv×16B), collision arrays(nc×16B), PointUsage (exactly numVertices s16 entries — +2 cases are pure alignment padding), GDLs (each extends to the next GDL start or D; 8-byte aligned, inter-GDL gaps copied as NOOP)}. Records INTERLEAVE with vertex arrays in character bodies — do not assume "all records before bulk".
- **ModelNode = 24B N64** (u16 op + 2 pad + 5×4B ptrs: Data@4, Parent@8, Next@0xC, Prev@0x10, Child@0x14) / **48B PC**. Older "20B/40B" notes are wrong — verified against raw hex stride 0x18.
- **Opcodes present: 1,2,4,8,9,10,12,13,15,18,21,22,23,24** (HEADER, GROUP, DL, LOD, BSP, BOX, GUNFIRE, SHADOW, INTERLINK, SWITCH, GROUPSIMPLE, DLPRIMARY, HEADPH, DLCOLLISION). op22 N64: numVertices s32@0, Vertices@4, Primary@8, BaseAddr@0xC (size 0x10); only 5 files have it.
- Record sizes N64/PC: OP01 0x10/0x18, OP02 0x1C/0x28, OP04 0x14/0x28, OP08 0x10/0x18, OP09 0x24/0x30, OP10 0x1C/0x1C, OP12 0x28/0x30, OP13 0x20/0x30, OP15 0x1C/0x1C, OP17 0x20/0x28, OP18 0x08/0x10, OP21 0x14/0x14, OP22 0x10/0x20, OP23 0x02/0x02, OP24 0x20/0x40 (compiler-verified). Field layouts in `src/bondtypes.h` (ModelRoData_*Record); read N64 fields from packed offsets, emit via real PC structs — don't hand-compute PC offsets.
- **f32 fields are big-endian in ROM** — bswap all f32 (record origins, BoundingVolumeRadius, Scale, Min/MaxDistance) and every s16 in vertex arrays; also bswap32 the TextureID u32 in the texture table (texLoad reads `*updateword & 0xffff` as the texture number).
- **Vertex arrays must stay 16B-stride** (GDL gSPVertex hardcodes 16B/vertex; propobj.c copies with sizeof(Vertex); model.c:4360 allocates numVertices×4 words). bswap every s16 field BE→LE. Main Vertices array is pure data; **CollisionVertices carry LinkedTo@8 = vma of a ModelNode** — remap to the new node offset (PROMOTE promotes them after rebase).
- **GDLs:** 8B BE word pairs → 16B LE slots: `w0=bswap32(rom_w0); w1=bswap32(rom_w1)` — **leave w1 raw, NO LSB set** (ROM convention; fast3d's extended seg_addr case handles unmarked segmented addresses — D47.13). GE uses STANDARD libultra GBI numbering (include/PR/gbi.h); opcodes live in the TOP byte of w0. **Opcode-aware remap: only {G_VTX=0x04, G_SETTIMG=0xFD, G_LOADBLOCK=0xF3} carry addresses in w1** (seg-5 → remap low24 via region map). All other opcodes get bswap-only — G_TRI4 (GE extension 0xB1) w1 is 4-bit vertex-index data, NOT an address (D47.6); same for TRI1/TEXTURE/SET*MODE/CLEARGEO/SETGEO/syncs. **G_VTX w0 is GBI1-style** (`04<<24 | dst<<16 | 16·n`, dst=((n−1)<<4)|v0; all 2826 have v0=0, n≤16) — bswap only, no field remap (D47.5). ROM scan: all 2805 seg-5 VTX refs have even offsets → lossless; no nested G_DL in model files.
- **Marker format:** top byte of w0 = 0xC0; type = `w0 & 7` (only 0/2/3/4 exist); texnum = `w1 & 0xfff`; min = top byte of w1. texLoadFromGdl dispatches on the top byte — PC fix: `(u8)(in->words.w0 >> 24)`. Type-0/1 markers with a valid tex are SKIPPED by the game (no expansion); the rest expand per D45's K table, which D46 verified as a true worst-case bound (no LUT textures exist; 99% of textures have maxlod=0 → TileLods emits nothing; state dedup resets per GDL via sub_GAME_7F0CC4C8).
- **seg-5 VTX offsets must be remapped** — all 2805 point into op4/op22 vertex arrays that move during re-layout (PexplosionbitZ: op4 with numVertices=0 but GDL loads 16 verts from offset 0x98 — the GDL's VTX count is authoritative, not the record field).
- PROMOTE contract: emit every promoted pointer as zero-extended u32 (low word of the 8B slot, high word 0); `PROMOTE` (`(u32)var + diff`, vma 0x5000000) then works unmodified. Fields promoted per opcode (model.c switch): all nodes Data/Parent/Next/Prev/Child; OP01 FirstGroup; OP02 ChildGroup; OP04 Vertices; OP08 Affects; OP09 left/rightChild; OP18 Controls; OP24 Vertices/CollisionVertices/PointUsage/CollisionVertices[i].LinkedTo. **NOT promoted:** GDL Primary/Secondary (stay raw 0x05xxxxxx), BaseAddr (game overwrites with fileramaddr).

**Load path (single hook point):**
- ALL model loads go through `load_object_fill_header()` (objecthandler_2.c:97): dst≠0 → `_fileNameLoadToAddr(name,0,dst,size)` (region = caller's bytes — see D45 for the grown per-region sizes); else `_fileNameLoadToBank(name,0,0x100,4)`. **Fresh-load semantics (D47.2-4):** `fileIndexLoadToBank` takes ALL remaining MEMPOOL_STAGE space (`S_bank`) only when poolRemaining==0; then F := alloc(S_bank), R := S_bank, load_resource decompresses D into F and sets P := D_N64. At compaction: **delta = R − P** (positive on fresh loads — the GDL mirror moves FORWARD). `fileSetSize(reallocate=1)` → `mempAddEntryOfSizeToBank` rewinds the bank cursor to the post-compaction size, giving the tail back.
- **Reload hazard + PC fix (D47.4):** fileSetSize leaves P=R=post-compaction size, so a same-stage reload would alloc(P_old) which can be < D_N64. PORT-guarded reset of poolRemaining=0 before _fileNameLoadToBank forces fresh-bank semantics every load (staging space = whole remaining bank; steady-state usage identical).
- **The fixup must set ONLY poolRemaining (= D_PC exactly — D46/D47), never rom_remaining.** Do NOT use fileSetSize for this — it clobbers rom_remaining too. Set `resource_lookup_data_array[fileGetIndex(name)].poolRemaining` directly (array defined in ob.c:14, not extern'd — declare `extern` in romdata.c; note the accessors actually index **0x80090000 + idx*20** — D47.2).
- **Hook location:** inside load_object_fill_header — (a) poolRemaining=0 reset immediately BEFORE `_fileNameLoadToBank`; (b) `u32 romdataFixupModelFile(u8 *blob, const char *name, s16 NS, s16 NT)` (returns D_PC, 0 on failure; reads entry.rom_remaining itself for the staging-space guard) called immediately after the filedata assignment and BEFORE Switches/Textures/RootNode setup + sub_GAME_7F075A90 — PROMOTE walks with PC ModelNode stride (48B), so the file must already be re-laid-out. One `#if defined(PORT)` call site.

**Compaction contract (sub_GAME_7F0762E0, objecthandler_2.c:24-83) — the tightest constraint:**
- It mirrors the GDL region [first_gdl_off, P_conv) into the caller-region's tail headroom (texCopyGdls to [A+R−P_conv+g, A+R)), then rewrites each GDL in place via texLoadFromGdl (expands texture markers into RDP commands), writing back starting at the original first-GDL offset; final fileSetSize = end of last rewritten GDL (= P_conv + 16·M_actual). **D46: fit and no-clobber are the SAME condition — R ≥ P_conv + 16×Σ(K_t−1) worst case — and all buffers satisfy it (see §F/D46 for the strict bound + the two cast-screen corrections).**
- Per-GDL `count` = byte distance between CONSECUTIVELY VISITED gdls → **GDLs must be packed contiguously at the tail of the PC image, in modelIterateDisplayLists visit order.**
- `modelIterateDisplayLists` (model.c:6254) **MUTATES the tree while walking**: LOD → `node->Child = LOD.Affects`; SWITCH → `node->Child = Switch.Controls`; BSP → `modelApplyReorderRelationsByArg(node,TRUE)` splices siblings (leftChild group before rightChild). Visit order per DL node: Primary, then Secondary (if non-null and ≠ previous), then DFS Child/Next/Parent.
- `build-pc/d43_gdlorder.py` implements this exact simulation to verify visit-order adjacency — **bug fixed + verified**: node collection now pushes child+next plus LOD.Affects/SWITCH.Controls targets; all 512 files pass (total GDLs 2301).
- Primary/Secondary are NOT promoted (raw 0x05xxxxxx), so modelNodeReplaceGdl's `Primary == find` comparisons work unchanged; after compaction Primary = replacementgdl (raw value, arbitrary parity — see next item).

**Render-time GDL resolution:**
- The game emits `gSPDisplayList(gdl, rwdata->gdl)` with w1 = raw 0x05xxxxxx; segment 5 (SPSEGMENT_MODEL_COL1) was just set to BaseAddr (= file base) by the preceding gSPSegment. fast3d's `seg_addr` (port/fast3d/gfx_pc.cpp:2282) currently sends 0x05xxxxxx down the "direct pointer" path (invalid). **Port-layer fix:** add a case — top byte 0x05 → `segmentPointers[5] + (w1 & 0xFFFFFF)`.
- Segments 3/4 (MTX table / vertex buffer) are set by the game per-render via gSPSegment ✓. BG segments (13/14/15) belong to the background path — later task.

### Implementation checklist (in order)
0. ~~**D46 buffer corrections (2 lines)**~~ — **DONE this session:** front.c
   `bufferRemaining = 0x25000`, initmenus.c logo buffer `0x85000` (comments cite D46).
1. ~~Fix + run `build-pc/d43_gdlorder.py`~~ — DONE: all 512 files pass, op22 added.
2. ~~**Mechanical game-code edits (D32 class)**~~ — **APPLIED (uncommitted):**
   bondtypes.h Vertex LinkedTo/CollisionRelatedNode → u32 under `#ifdef PORT`;
   model.c PROMOTE32 macro + use at line ~5735; chr.c:3257-3259 cast + !=0 check;
   objecthandler_2.c Textures offset via `sizeof(struct ModelNode*) * numSwitches`;
   tex.c (4 fixes: texCopyGdls count, dispatch `(u8)(in->words.w0>>24)`,
   0xba byte check `(s8)((w0>>8)&0xff)`, texLoadFromGdl count `srcsize/sizeof(Gfx)`);
   gun.c size_item_buffer {0x23000,0x23000}, D_80032464 {0xF000,0xF000}, SUIT R
   0x18000 (pool = size−0x18000), TRIGGER/WATCHLASER R 0x17000; front.c wallet R
   0x17000 + cast bufferRemaining (now corrected to 0x25000 per item 0);
   initmenus.c logo buffer (now 0x85000 per item 0). All PORT-guarded with D45/D46
   comments.
   - `src/bondtypes.h` Vertex: union members `struct Vertex *LinkedTo; void *CollisionRelatedNode;` → `u32 LinkedTo; u32 CollisionRelatedNode;` (sizeof(Vertex) stays 16 both platforms; game heap <4GiB so lossless on PC).
   - `src/game/model.c`: add `#define PROMOTE32(var) if (var) var = (u32)((u32)var + diff)` beside PROMOTE (~line 5677); line 5735 `PROMOTE(...LinkedTo)` → `PROMOTE32(...)`.
   - `src/game/chr.c:3257`: cast the now-u32 field `(ModelNode *)(uintptr_t)…`; line 3259 `!= NULL` → `!= 0`. (These are the only 3 use sites of the union.)
   - `src/game/objecthandler_2.c:103`: `&((s32*)filedata)[objheader->numSwitches]` → `((u8*)filedata + sizeof(ModelNode*) * objheader->numSwitches)` (N64: 4B stride, identical).
   - `src/game/tex.c`: texCopyGdls `arg2 = (arg2 >> 3)` → `arg2 / sizeof(Gfx)`; texLoadFromGdl `count = srcsize >> 3` → `srcsize / sizeof(Gfx)` and dispatch `switch (*(u8*)in)` → `switch ((u8)(in->words.w0 >> 24))` (N64-identical; those are the ONLY byte-level accesses in that function — everything else already uses words.w0/w1, and the default case copies whole slots).
3. ~~**fast3d seg_addr**~~ — **DONE this session:** port/fast3d/gfx_pc.cpp now
   resolves raw 0x05xxxxxx (no LSB) via segmentPointers[5] + low24 BEFORE the LSB
   path; also handles unmarked segmented addresses generally (nibble 24-27 = seg,
   w1 < 0x10000000). Segment 5 is set per-render by the game's gSPSegment
   (BaseAddr = live host file base).
4. **`romdataFixupModelFile` in `port/src/romdata.c`** (PC-only; bswap helpers +
   extern `resource_lookup_data_array` from ob.c are already there). **Port the
   validated logic of `build-pc/d43_convert.py` 1:1 — it IS the spec** (512/512
   clean; D47.12). Two-pass: (a) DFS walk collecting nodes/records/GDLs + build
   the region map (old_off, old_size → new_off); (b) emit the PC image into a
   STAGING area at `blob + avail − D_PC` (avail = entry.rom_remaining; guard:
   fail with an error if D_PC > avail), then memmove to blob. Emission order:
   switches(NS×8B LE) → texconfigs(NT×12B; bswap TextureID only — no seg-5 TIDs
   exist in any model file, D47.9) → nodes+records in DFS preorder (PC layout via
   struct assignment; every promoted pointer field = zero-extended u64 of
   0x05|new_off — full per-opcode field map in the facts above + D47.13;
   Primary/Secondary GDL ptrs remapped but NOT promoted; BaseAddr emitted 0) →
   vertex arrays immediately after their record (normal: bswap s16 x,y,z,index,s,t,
   bytes @C-F raw; collision: bswap s16 x,y,z,index + remap LinkedTo u32@8 +
   bswap CollisionRelatedIndex s16@C/reserved@E; **nv==0 with non-null ptr → size
   up to next object offset**, D47.10) → PointUsage (2×numVertices s16, after
   op24 CollisionVerts) → **GDLs LAST, contiguous, visit order, 16B LE slots**:
   w0'=bswap32(w0); w1' = bswap32(raw) EXCEPT for seg-5 of {G_VTX=0x04,
   G_SETTIMG=0xFD, G_LOADBLOCK=0xF3} → remap low24 via the region map (sorted
   regions + binary search); **no LSB set** (ROM convention; D47.6/13). Emit each
   GDL up to and including its ENDDL (drop trailing junk — fast3d returns at
   ENDDL, D47.11). Set `resource_lookup_data_array[fileGetIndex(name)].poolRemaining`
   = D_PC EXACTLY after conversion (load_resource left it at D_N64). NEVER touch
   rom_remaining. Assert `D_PC + 16×Σ_markers(K_t−1) ≤ avail` (K_BOUND
   {0:38,1:46,2:36,3:18,4:15}) with a clear error.
5. **Hook** into load_object_fill_header (`#if defined(PORT)`): (a) reset
   `resource_lookup_data_array[fileGetIndex(name)].poolRemaining = 0` immediately
   BEFORE `_fileNameLoadToBank` (D47.4 — forces fresh-bank allocation so staging
   space = whole remaining bank; avoids the reload-under-alloc hazard);
   (b) call romdataFixupModelFile after the filedata assignment and BEFORE
   Switches/Textures/RootNode setup + sub_GAME_7F075A90 (PROMOTE walks PC
   ModelNode stride so the image must be re-laid-out first).
6. **Build, run, verify:** no SIGSEGV in modelPromoteNodeOffsetsToPointers; scene
   shows actual geometry (not clear color); 30 s+ soak stable. Sizing is already
   proven by D46's strict bound + D47.12's max-ratio 1.31 — if anything still
   overflows, the converter has a bug, not the buffers.

### Helper scripts (build-pc/, written across D43 sessions)
- `d43_decode.py` — decompress + decode one model's node tree + GDL command stream.
- `d43_sizes.py` / `d43_sizes2.py` — decompress all 512, report N64/PC sizes +
  per-file worst-case P_final (B_pc + 2E + 16×Σ(K_t−1)).
- `d43_walk.py` — walks all trees using exe NS/NT; opcode histogram. Note: header
  names lack the C/G/P prefix and trailing Z (file `CcamguardZ` → header `camguard`).
- `d43_invariants.py` — full ownership tiling check (0 failures / 512).
- `d43_gdlorder.py` — exact visit-order simulation; **bug fixed, all 512 pass**.
- `d43_gdldump.py` / `d43_gdlhist.py` / `d43_gdlseq.py` / `d43_seg5.py` — GDL
  command dumps/histograms/sequences; seg-5 VTX reference analysis (all 2805 point
  into op4/op22 vertex arrays → must remap).
- `d43_cover.py` — verifies every seg-5 VTX + SETTIMG target falls inside a modeled
  object (2804/2805 covered; the one exception is PexplosionbitZ's orphan array at
  offset 0x98 — handled by the converter's implicit-object rule). SETTIMG targets in
  logo files are vestigial (in gaps, never dereferenced).
- `d43_lutscan.py` — scans all model texture tables → image headers (via
  imagelist.u.csv, order = assets/images.def): proves NO LUT textures (formats 9-12)
  and maxlod distribution {0:1063, 6:7, 7:1} across all 512 files (D46).
- `d43_convert.py` — **THE reference converter + validator (D47):** implements
  the full N64→PC conversion spec (DFS walk with LOD/SWITCH rewiring, region map,
  per-opcode record/vertex/GDL conversion, remap checks) and runs it on all 512
  ROM files — ALL CLEAN. This IS the spec to port to C in romdata.c.
- `d43_chainbound.py` — computes the D46 strict bound B_pc + 2×(R_share_N64 − B_n64)
  for every buffer/chain and compares against the PC region sizes.
- Gotcha: MODELFILEHEADER's name is field a[0] of the macro args (NOT the filename —
  all header files are literally named `modelFileHeader.inc.c` in per-model dirs).

### Follow-on
Expect later faults in other asset types (BG/stan/propobj tables) — same D32/D37 procedure; PD's `preprocess/` module already solves most of them (`filebg.c`, `filetiles.c`, `filepads.c`, `filesetup.c`, …) — see the PD reference section below.

## Dev-loop speedups (build these when they pay off, not before)
Priority order from the last session's measurements:
1. **D44** (above) — automatic backtraces; biggest win per diagnosis.
2. **`tools_pc/sym.sh`** — tiny wrapper: takes a PC or rel offset, adds image base
   `0x140000000`, runs `addr2line -e build-pc/ge007.x86_64.exe -f -C`. Kills the manual
   arithmetic; pairs with the crash log (one command per crash). Re-verify the image base
   with `info address` after any link-layout change.
3. **`tools_pc/romdump.py`** — Python ROM/asset inspector: load `data/ge007.ntsc-final.z64`,
   parse ModelFileHeader/ModelNode trees using the bondtypes.h layout, print opcode
   sequences / node links / Data records for a given cart address. Accelerates D43 and
   every follow-on asset type; validates re-layout output against ROM ground truth without
   running the game. (Python here: no f-strings with nested quotes — use `%` formatting.)
4. **Frame capture + pixel assert** — env-gated framebuffer PPM dump every N frames in
   `port/`, plus a script that counts non-clear-color pixels / diffs two captures. Turns
   "the scene contains actual model geometry" into an assert, makes the 30 s soak
   scriptable, and enables screenshot diffs against an emulator (Mupen64Plus with the
   verified No-Intro dump as ground truth).

## PD port as standing reference (always available)
`PD_PORT_CHECKOUT` is the Perfect Dark PC port — same Rare engine
family, and the **standing reference/guidance source** for this project (noted in
AGENTS.md). Consult it whenever a remaining work item has a PD analogue:
- **Phase 2 asset conversion:** `port/src/preprocess/` (~4,100 lines) — per-ROM-segment
  N64→PC layout converters hooked from their `romdata.c`. `filemodel.c` = D43 near-analogue
  (same vma 0x5000000); `filebg.c` (expected follow-on), `segaudio.c` (cross-check for our
  D37 bank-tree fixup), `filelang.c`, `filetiles.c`, `filepads.c`, `filesetup.c`, `gbi.c`,
  `segfonts.c`.
- **Phase 3/4 (copy-and-adapt; our stubs were scaffolded for this):** `port/src/mixer.c`
  (722 lines vs our 31 — libaudio→SDL mixer), `input.c` (1,551 vs 48 — SDL_GameController
  backend with hotplug/rumble/keyboard; remap buttons to GE's scheme), `fs.c` (294 vs 81 —
  file-backed save dir for Phase 4 EEPROM/PFS).
- **Caveats:** copy port-layer files only (their game code was modified for PC); same
  family ≠ identical format — validate every conversion per-field against GE headers + ROM
  ground truth. Full audit: `docs/PCPortResearch.md` §2.4.

## Standing procedure (you will hit more of these)
Every ROM-serialized struct with a pointer field faults the same way once you reach more
asset loading. The D32 fix procedure (doc §H): at the fault, `ptype /o <Struct>`; if a
pointer field's offset/size diverges from its N64 offset comment, change it to `u32` + cast
at the use sites; verify the load-time rebase yields valid V1 addresses (< 0x80000000);
**also check the ROM bytes' endianness per-field (D33 rule: u32 bswap32 / u16 bswap16 / u8
identity) and any fixup loop's pointer stride on x86-64**; for packed struct trees use the
D37 re-layout pattern; for address-arithmetic idioms over exe symbols (D39/D42 class) guard
a PC branch that reproduces the N64 32-bit value exactly. Rebuild; confirm boot advances.
Log each as D3x in §F and note it in AGENTS.md phase status.

## Environment (do not rediscover these)
- MSYS2/MinGW tools are in `/c/msys64/mingw64/bin/` — **not** on PATH. Prefix every shell:
  `export PATH="/c/msys64/mingw64/bin:$PATH"`.
- Build: `./build-pc.sh ntsc-final` (incremental; full rebuild only when `port/shim/`
  changes; a *clean* full build is ~5 s on the 16-core dev box). ROM is at
  `data/ge007.ntsc-final.z64` (= `baserom.u.z64`, byte-identical).
- The ROM is **mapped at cart base 0x10000000** on PC (`romdataInit`), so `&<segment>Symbol`
  values (e.g. `_sfxctlSegmentRomStart = 0x102EBDE0`) are directly dereferenceable host
  pointers into the ROM image; `romCopy()` copies from those cart addresses. All game RAM
  lives in the V1 view at 0x70xxxxxx (< 4 GiB) — s32 truncation of game-RAM pointers is
  lossless (this is why D39 needed no changes outside image_bank.c).
- ROM provenance (verified 2026-08): byte-identical to the No-Intro good dump
  `GoldenEye 007 (U) [!].z64` (kept in `data/`). MD5
  `70c525880240c1e838b8b1be35666c3b`, SHA-256
  `2cdcec8a9f0cb6e36337f3ee39d8ad105dc8afa6ba1c02d466e8f5b771f9a162`, 0xC00000 bytes,
  country 'E' (US). Header CRCs DCBC50D1/09FD1AA3 re-verify with `tools_pc/romverify.exe`.
- gdb: **launch** mode only (`gdb -batch -ex "handle SIGSEGV stop" -ex run …`). Symbolicate
  offline with `addr2line -e build-pc/ge007.x86_64.exe -f -C <0x140000000+rel>`. Image base
  is `0x140000000` (re-verify with `info address` after a rebuild). Debug scripts live in
  `build-pc/` (`d37_tree.py`, `d39_*.gdb`, `d41_probe.gdb`, …).
- **Capstone gotcha (D47.1):** the ROM is BIG-ENDIAN MIPS — always disassemble with
  `CS_MODE_MIPS32 | CS_MODE_BIG_ENDIAN`. Default (LE) mode produces garbage that looks
  plausible; an earlier session's disassembly was all LE and had to be redone.
- Crash log: `ge007.crash.log` (repo root) now holds Phase 1 + a `BACKTRACE:`
  section (**D44** fixed). First stop for any fault; symbolicate with
  `addr2line -e build-pc/ge007.x86_64.exe -f -C <abs-addr>`. Frames past the
  true chain may be stale — sanity-check they fall inside `.text`.
- `objdump -d --disassemble=<fn>` / `objdump -s -j .data` on build-pc/ge007.x86_64.exe is a
  fast way to check what the compiler actually emitted.
- Many init functions use a fake RBP — compute stack offsets from the **entry RSP**, not RBP.
- Python on this box: no f-strings with nested quotes (< 3.12) — use `%` formatting.

## Definition of done (this milestone)
Checkpoint A: D44 committed — crash log shows a real backtrace for the D43 fault.
Then the milestone proper: stage objects load without faulting and the rendered scene
contains actual model geometry (not just the clear color / background), stable under a
30 s+ soak. After that, Phase 2 makes it look right (fast3d CC/RM correctness vs
`gmain.s` — GE-specific, no PD analogue) and Phase 3 adds sound + input, largely
copy-and-adapt from the PD port (see "PD port as standing reference" above). Commit +
push to `origin/master` at each working checkpoint using the message style
`PC port: <phase> — <what>`.
