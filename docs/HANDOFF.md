# Handoff brief — GoldenEye 007 PC port (Phase 2: Plan B review PASSED (D49) — next session executes the offline pre-conversion)

_Paste-ready briefing. Authoritative detail lives in `AGENTS.md` and
`docs/PCPortResearch.md` (§F/D31–D48 findings, §H handoff); this is the summary
+ the immediate tasks._

## Your job

- ~~Session A: independent review of Plan B (D48)~~ — **DONE (D49, committed):**
  all 8 checklist items CONFIRMED against code + ROM; Plan B green-lit. One
  spec amendment recorded there: bswap16 every u16/s16 record scalar on emit
  (see Verified facts).
- **Session B (next): EXECUTE Plan B** per §Task 2 — Python emit pass +
  sidecar generation, ~60 lines of port-layer plumbing, one-line hook,
  build + pixel-assert soak. Commit at each working checkpoint
  (`PC port: phase 2 — <what>`). The goal: get stage geometry on screen.

Work agentically — fix → build (~5 s) → verify → commit at each milestone.

## Read first (authoritative, in order)
1. `AGENTS.md` — non-negotiables, critical files, build, verification ritual.
2. `docs/PCPortResearch.md` **§F/D49** (review verdict + quantified fit
   numbers), **D48** (the plan being executed — read fully), then **D47**
   (converter contract — still ground truth for the emit pass),
   **D46** (overlap safety + buffer sizing), **D45** (buffer constants),
   **D43** (the model-file task), §2.4 (PD-port copy-candidate audit).
3. The code paths D48 cites: `port/src/rzdecomp.c`, `port/src/libultra.c`
   (`piServiceDma`), `port/src/romdata.c` (`romdataInit`,
   `romdataCartAddrValid`), `src/ramrom.c`, `src/game/ob.c` (`load_resource`,
   `fileIndexLoadToBank`, `fileSetSize`, `obInit`), `src/game/objecthandler_2.c`
   (`load_object_fill_header`).

## Non-negotiables (the ones that bite)
- N64 build files (`Makefile`, `tools/`, `rsp/`, `ld/`) — never touch.
- **Game logic is unmodified.** Hardware deps go in `port/`. Narrow exception:
  mechanical, semantics-preserving ABI/layout fixes forced by the 32→64-bit
  transition (embedded pointers → u32 + cast at use; PC-guarded pool sizing /
  idiom branches), each documented as a D3x finding. No behavior changes.
  (Plan B's only game-code change is the PORT-guarded hook block in
  `load_object_fill_header` — bookkeeping reset + one-shot patch call, no logic.)
- Region macros in `CMakeLists.txt` must mirror the N64 Makefile exactly.

## Current state
- **D49 (this handoff's predecessor, committed): Plan B review PASSED** — all 8
  D48 checklist items CONFIRMED with evidence (§F/D49). Quantified: Σ round8(C)
  = 1,277,088 B; worst-case post-compaction P_final = 0x16F74 vs 0x24C400 stage
  bank (25× headroom); all dst!=0 buffers pass against current constants;
  fresh C probe re-verified every PC record size + promotion offset (no drift).
  Notes: romdataInit has no JP ROM candidate (JP sidecars moot for now);
  EU region has 465 model files vs 512 (generator maps what exists); the
  poolRemaining=0 reset is still pending (Task 2 step 3).
- **D43 status (latest review session, §F/D48):** investigation complete;
  converter spec finalized (D47) and validated 512/512 (`tools_pc/d43_convert.py`,
  max D_PC = 0xB7E0, ratio ≤ 1.31). **The remaining task was re-planned:** the
  old plan ("port d43_convert.py 1:1 to C as `romdataFixupModelFile`") is
  discarded because (a) d43_convert.py does not emit bytes — the emission code
  is unimplemented in any language, and (b) **Plan B (offline pre-conversion)**
  reuses the existing ROM load chain and eliminates the C converter entirely:
  a Python emit pass writes 512 PC-layout RZ sidecars to
  `data/pcmodels-<region>/`; the port layer serves them by patching
  `file_resource_table[i].hw_address` + `resource_lookup_data_array[i].rom_size`
  (full mechanics in D48.2). Plan A (D47.13 C converter) remains the fallback,
  with a corrected staging guard (D48.4: `D_N64 + D_PC ≤ avail`, not
  `D_PC ≤ avail`).
- DONE + committed (ecefffdb, pushed): all step-2 mechanical game-code edits
  (bondtypes.h Vertex u32 union; model.c PROMOTE32; chr.c casts;
  objecthandler_2.c Textures offset; tex.c Gfx-size/dispatch/byte-check/
  copy-count fixes), D45/D46 buffer constants (gun.c/front.c/initmenus.c),
  fast3d seg_addr seg-5 raw-VMA case (port/fast3d/gfx_pc.cpp).
- DONE this review session: **all 23 `d43_*.py` scripts moved from gitignored
  `build-pc/` to tracked `tools_pc/` and committed** — the spec is now
  versioned. Converter re-run from new location: ALL CLEAN.
- Verified environment facts (do not rediscover): frames 1–4 render then FATAL
  at `modelPromoteNodeOffsetsToPointers()` (model.c:5688) = D43; black screen
  is clear color only (no stage geometry loaded yet). ROM dump byte-identical
  to the No-Intro good dump (`tools_pc/romverify.exe`). Crash log with real
  backtraces works (D44). Build clean 237/237; full rebuild ~5 s.

## Task 1 — Plan B review checklist (Session A) — DONE (D49: 8/8 CONFIRMED)

Verify each claim. For each: state CONFIRMED or REFUTED in D49 with the
evidence you used (file:line, command output). If a falsification criterion
fires, stop and record it — do not paper over it.

**R1. RZ format is reproducible.** Claim: every model ROM region is
`[0x11 0x72][raw deflate]`; `decompressdata()` (port/src/rzdecomp.c) skips
exactly those 2 bytes and inflates raw (`inflateInit2(-15)`) with a generous
avail_in bound, so a sidecar file in that format works through
`load_resource` unmodified. Verify: read rzdecomp.c; confirm the 2-byte prefix
is constant across all 512 (one-liner over `scripts/filelist.u.csv` + ROM —
`tools_pc/d43_convert.py` already decompresses all 512 with `[2:], -15`, re-run
it). **Falsified if:** the prefix varies per file, or decompressdata depends on
header state beyond 2 bytes.

**R2. The DMA path is a plain host memcpy.** Claim: `romCopy` (src/ramrom.c) →
`osPiStartDma` → port shim `piServiceDma` (port/src/libultra.c) = `memcpy` from
`(void*)srcPA`, gated ONLY by `romdataCartAddrValid()` (port/src/romdata.c:212,
checks `addr ≥ CART_BASE && off+size ≤ romSize`). The ROM is VirtualAlloc'd at
CART_BASE 0x10000000 sized exactly romSize — so extending the reservation by
the sidecar total and extending the validity bound serves sidecars with zero
other changes. Verify: read both shims end-to-end; confirm no other gating
(alignment, hardcoded 0x10C00000 limits elsewhere, cache effects). Note
`romCopyAligned` exists but is NOT used by this load path. **Falsified if:**
any additional gate rejects non-ROM host pointers or sizes we can't satisfy.

**R3. The resource table is the single choke point and is writable.** Claim:
`file_resource_table` is a plain (non-const) global defined via include in
src/game/ob.c:22; ALL 512 model loads go through `load_object_fill_header`
(dst==0 → `_fileNameLoadToBank`, custom-buffer callers in front.c/gun.c/
bondview2.c/initmenus.c → `_fileNameLoadToAddr`), both reading `hw_address` via
`load_resource`; no game code dereferences C*/G*/P*Z asset symbols directly
outside the table. Verify: grep callers of `fileLoad` /
`load_object_fill_header`; grep every model symbol (e.g. `CcamguardZ`,
`PflagZ`) outside `assets/obseg/file_resource_table.inc.c`,
`port/src/romassets_*.s`, and N64-only files (`obseg.h` decls, `ob_seg.s`).
**Falsified if:** any compiled game code reads model bytes straight from the
cart (it would bypass the sidecar and crash on N64 layout).

**R4. Patch timing: after obInit, before first model load.** Claim: `obInit()`
(src/boss.c:179) runs before the first model load and computes `rom_size` from
adjacent-entry hw_address DELTAS (ob.c:122) — so the patch must run after it
and set `rom_size` explicitly; nothing between obInit and the first model load
consumes model entries' rom_size/hw_address. Verify: trace the init chain
(bossInitMainthreadData → … → first stage object load; the D43 crash stack
shows the order); grep `rom_size` uses (ob.c:122, 253, 291;
`obLoadBGFileBytesAtOffset` is BG-only). **Falsified if:** anything in that
window reads model entries' rom_size/hw_address, or obInit can run after a
model load.

**R5. Load-flow fit for pre-converted data.** Claim: with sidecars, a fresh
dst==0 load allocates S_bank = whole remaining stage bank; the compressed
source placement `round8(compressed)+8 ≤ S_bank` holds (max single-file
compressed ≪ ~2.3 MB bank); `load_resource` sets poolRemaining := D_PC
automatically; compaction headroom R = S_bank satisfies D46's strict bound with
P_conv = D_PC (D46 already proved the buffers against these sizes). ADDITIONALLY:
for every dst!=0 caller, the patched compressed size + 8 ≤ the caller's buffer
size (gun.c 0x18000/0xBD70/0x17000, front.c wallet 0x17000 / cast screen
0x25000, initmenus.c logo 0x85000, bondview2.c weaponbuf `size0`). Verify: have
the emit pass (or a quick Python pass) print per-file compressed sizes and
cross-check against the D45/D46 constants; re-run `tools_pc/d43_chainbound.py`.
**Falsified if:** any buffer/chain is undersized for its file.

**R6. Reload hazard + the one-line reset.** Claim: `fileSetSize` (ob.c:346-347)
leaves poolRemaining = rom_remaining = post-compaction size; without a reset, a
same-stage reload allocates S_bank' = P_old which can be < round8(compressed)+8
→ `load_resource` hits the `source − ptrdata < 8` branch → poolRemaining = 0 →
silent load failure. A PORT-guarded `poolRemaining = 0` reset immediately before
`_fileNameLoadToBank` restores fresh-bank semantics every load with identical
steady-state usage. Verify: read `fileSetSize`, `fileIndexLoadToBank`,
`load_resource`; grep consumers of `get_pc_remaining_buffer_for_index` (should
be compaction only) to confirm the reset clobbers nothing. **Falsified if:**
the reset breaks a consumer, or the hazard cannot actually occur (then drop the
reset and say so in D49).

**R7. Region handling.** Claim: sidecars are derived from a specific region ROM;
the generator takes the region, reads `data/ge007.<region>.z64`, writes
`data/pcmodels-<region>/`; romdataInit picks the directory matching the ROM it
loaded (same token logic as ROM discovery in `romdataInit`). Verify: read
`build-pc.sh` + `romdataInit`'s ROM discovery; confirm name sets are identical
across regions is NOT required (the generator maps whatever files exist).
**Falsified if:** the build flow can't regenerate sidecars per region, or
romdataInit can't tell which directory matches.

**R8. Emission spec completeness.** Claim: the "Verified facts" section below +
D47.13 cover every field of every opcode present in ROM (1,2,4,8,9,10,12,13,
15,18,21,22,23,24), and the round-trip validator (§Task 2 step 1) catches any
miss. Verify: walk each `ModelRoData_*Record` in src/bondtypes.h and confirm an
emission rule exists for every pointer/float/int field; spot-check 2–3 PC
record sizes against a fresh C probe (the Python `PC_REC` table was
probe-verified once — re-confirm it hasn't drifted). **Falsified if:** any
field has no rule, or a PC record size is wrong.

**Review output:** D49 in §F (8 items adjudicated + evidence), HANDOFF status
updated, committed + pushed. If all CONFIRMED → Session B executes §Task 2.

## Task 2 — Execution checklist (Session B; only after D49 confirms)

1. **Emit pass in Python** (`tools_pc/d43_emit.py`, importing/reusing
   `d43_convert.py`'s walk + region map). For each of the 512 files, emit the
   PC image per D47.13 + Verified facts below: switches (NS×8B LE) →
   texconfigs (NT×12B; bswap TextureID u32) → nodes+records in DFS preorder
   (PC layout mirroring bondtypes.h; every promoted pointer field =
   zero-extended u64 of `0x05|new_off`; Primary/Secondary GDL ptrs remapped but
   NOT promoted; BaseAddr emitted 0) → vertex arrays immediately after their
   record (normal: bswap s16 x,y,z,index,s,t, bytes @C–F raw; collision: bswap
   s16 x,y,z,index + remap LinkedTo u32@8 + bswap CollisionRelatedIndex s16@C /
   reserved s16@E; nv==0 with non-null ptr → size up to next object offset,
   D47.10) → PointUsage (2×numVertices s16, after op24 collision verts) →
   **GDLs LAST, contiguous, visit order, 16B LE slots**: w0' = bswap32(w0);
   w1' = bswap32(raw) EXCEPT seg-5 of {G_VTX=0x04, G_SETTIMG=0xFD,
   G_LOADBLOCK=0xF3} → remap low24 via the region map; **no LSB set**; emit up
   to and including ENDDL. Write each file as `[0x11 0x72] + raw deflate(PC
   image)` (Python: `zlib.compressobj(zlib.Z_DEFAULT_COMPRESSION,
   zlib.DEFLATED, -15)`) into `data/pcmodels-<region>/` plus a
   `manifest.csv` (`name,offset,size`) with offsets assigned contiguously in
   `file_resource_table` order (parse the quoted names from
   `assets/obseg/file_resource_table.inc.c`).
   **Round-trip validator (mandatory before trusting output):** re-parse each
   emitted image as PC layout — 48B node stride, `PC_REC` record sizes, LE Gfx
   slots; assert every promoted pointer equals the expected new offset, every
   seg-5 GDL w1 target resolves inside the image, D_PC matches the pass-1 size.
   Report per-file compressed sizes (feed R5's cross-check).
2. **Port-layer plumbing in `port/src/romdata.c`** (~60 lines):
   - `romdataInit`: if `data/pcmodels-<region>/manifest.csv` exists for the
     loaded ROM, reserve the extended region — VirtualAlloc at CART_BASE sized
     `romSize + sidecarTotal` (aligned), memcpy the ROM as today, then load the
     sidecar blob(s) at `[CART_BASE + romSize, …)` per manifest offsets. Log
     count + total bytes. Missing directory → warn and continue (ROM-only
     mode = current behavior).
   - Extend `romdataCartAddrValid` to accept up to
     `CART_BASE + romSize + sidecarTotal`.
   - New `void romdataPatchModelSidecars(void)` — one-shot (static flag): for
     each `file_resource_table[i]` whose name is in the manifest, set
     `hw_address = (u8*)(CART_BASE + romSize + off)` and
     `resource_lookup_data_array[i].rom_size = size`. `extern` both arrays from
     ob.c (same pattern as the existing `resource_lookup_data_array` extern).
3. **Hook in `load_object_fill_header`** (src/game/objecthandler_2.c, ONE
   `#if defined(PORT)` block, D48-class bookkeeping — document as D50):
   (a) call `romdataPatchModelSidecars()` (lazy one-shot; obInit has run by
   first model load — R4); (b) reset
   `resource_lookup_data_array[fileGetIndex(name)].poolRemaining = 0`
   immediately before the `_fileNameLoadToBank`/`_fileNameLoadToAddr` call
   (R6). No other game-code changes.
4. **Build, run, verify + pixel assert (build it NOW — dev-loop item #1):**
   env-gated framebuffer PPM dump every N frames in `port/video.c`
   (e.g. `GE_PCDUMP=1` → `build-pc/frame_%05d.ppm`) + `tools_pc/pixcount.py`
   (counts non-clear-color pixels; diffs two captures). Milestone: no SIGSEGV
   in `modelPromoteNodeOffsetsToPointers`; pixel count rises above a small
   threshold and stays up through a 30 s+ soak. If geometry is wrong/missing,
   the converted sidecars are inspectable OFFLINE (Python) — that's the point
   of Plan B; don't gdb the game first.
5. **Commit checkpoints** (`PC port: phase 2 — …`); record findings D49/D50+
   in §F; update AGENTS.md phase status + this file. Then move on to the next
   asset type (Tbg_*_stanZ backgrounds) with the SAME offline pattern — PD's
   `port/src/preprocess/filebg.c` is the per-field reference (§2.4).

## Task 3 — Plan A fallback (only if D49 refutes Plan B)

D47.13 C converter in `port/src/romdata.c`: two-pass walk+layout then emit,
staged at `F + R − D_PC`, memmove to F; hook after the filedata assignment and
before Switches/Textures/RootNode setup; set `poolRemaining = D_PC` exactly
(never rom_remaining); poolRemaining=0 reset before `_fileNameLoadToBank`.
**Use the corrected guard (D48.4): fail if `D_N64 + D_PC > avail`** — the
staging region overlaps the live N64 image otherwise. The Python emit pass from
Task 2 step 1 is still worth writing first: it becomes the byte-level oracle to
diff the C output against (convert one file in C, compare with Python output).

## Verified facts (converter spec — ground truth for the emit pass; do NOT re-derive)

**Format / layout:**
- All 512 C*/G*/P*Z model files (Tbg_*_stanZ backgrounds are a different format/path — later) decompress fine; each file starts directly with the switches array (no in-file header — NS/NT come from the exe `ModelFileHeader` record via `objheader->numSwitches/numtextures`).
- File layout: `[switches NS×4B][textures NT×12B][root node @ 4NS+12NT]…`; all embedded pointers are vma `0x05xxxxxx` (mask 0xFFFFFF).
- **Tiling invariant verified, 0 failures in all 512 files** (`tools_pc/d43_invariants.py`): `[0,D)` is exactly tiled by {switches, textures, nodes(24B), records(RSZ per opcode), vertex arrays(nv×16B), collision arrays(nc×16B), PointUsage (exactly numVertices s16 entries — +2 cases are pure alignment padding), GDLs (each extends to the next GDL start or D; 8-byte aligned, inter-GDL gaps copied as NOOP)}. Records INTERLEAVE with vertex arrays in character bodies — do not assume "all records before bulk".
- **ModelNode = 24B N64** (u16 op + 2 pad + 5×4B ptrs: Data@4, Parent@8, Next@0xC, Prev@0x10, Child@0x14) / **48B PC**. Older "20B/40B" notes are wrong — verified against raw hex stride 0x18.
- **Opcodes present: 1,2,4,8,9,10,12,13,15,18,21,22,23,24** (HEADER, GROUP, DL, LOD, BSP, BOX, GUNFIRE, SHADOW, INTERLINK, SWITCH, GROUPSIMPLE, DLPRIMARY, HEADPH, DLCOLLISION). op22 N64: numVertices s32@0, Vertices@4, Primary@8, BaseAddr@0xC (size 0x10); only 5 files have it.
- Record sizes N64/PC: OP01 0x10/0x18, OP02 0x1C/0x28, OP04 0x14/0x28, OP08 0x10/0x18, OP09 0x24/0x30, OP10 0x1C/0x1C, OP12 0x28/0x30, OP13 0x20/0x30, OP15 0x1C/0x1C, OP17 0x20/0x28, OP18 0x08/0x10, OP21 0x14/0x14, OP22 0x10/0x20, OP23 0x02/0x02, OP24 0x20/0x40 (compiler-verified). Field layouts in `src/bondtypes.h` (ModelRoData_*Record); read N64 fields from packed offsets, emit via the real PC layout — don't hand-compute PC offsets.
- **f32 fields are big-endian in ROM** — bswap all f32 (record origins, BoundingVolumeRadius, Scale, Min/MaxDistance) and every s16 in vertex arrays; also bswap32 the TextureID u32 in the texture table (texLoad reads `*updateword & 0xffff` as the texture number).
- **u16/s16 record scalars are big-endian too (D49 amendment)** — bswap16 every u16/s16 scalar field in every record (AnimPart, MatrixIndex, JointID, MatrixIDs, Group1/2, RwDataIndex, op4 numVertices@0x10, op24 nv/ncv/ModelType/RwDataIndex, …); padding/reserved fields may be zeroed. Data-verified: these hold small BE values (JointID 1–11, nv ≤ 73) that raw emission would corrupt ×256 on an LE read.
- **Vertex arrays must stay 16B-stride** (GDL gSPVertex hardcodes 16B/vertex; propobj.c copies with sizeof(Vertex); model.c:4360 allocates numVertices×4 words). bswap every s16 field BE→LE. Main Vertices array is pure data; **CollisionVertices carry LinkedTo@8 = vma of a ModelNode** — remap to the new node offset (PROMOTE promotes them after rebase).
- **GDLs:** 8B BE word pairs → 16B LE slots: `w0=bswap32(rom_w0); w1=bswap32(rom_w1)` — **leave w1 raw, NO LSB set** (ROM convention; fast3d's extended seg_addr case handles unmarked segmented addresses — D47.13). GE uses STANDARD libultra GBI numbering (include/PR/gbi.h); opcodes live in the TOP byte of w0. **Opcode-aware remap: only {G_VTX=0x04, G_SETTIMG=0xFD, G_LOADBLOCK=0xF3} carry addresses in w1** (seg-5 → remap low24 via region map). All other opcodes get bswap-only — G_TRI4 (GE extension 0xB1) w1 is 4-bit vertex-index data, NOT an address (D47.6); same for TRI1/TEXTURE/SET*MODE/CLEARGEO/SETGEO/syncs. **G_VTX w0 is GBI1-style** (`04<<24 | dst<<16 | 16·n`, dst=((n−1)<<4)|v0; all 2826 have v0=0, n≤16) — bswap only, no field remap (D47.5). ROM scan: all 2805 seg-5 VTX refs have even offsets → lossless; no nested G_DL in model files.
- **Marker format:** top byte of w0 = 0xC0; type = `w0 & 7` (only 0/2/3/4 exist); texnum = `w1 & 0xfff`; min = top byte of w1. texLoadFromGdl dispatches on the top byte — PC fix: `(u8)(in->words.w0 >> 24)`. Type-0/1 markers with a valid tex are SKIPPED by the game (no expansion); the rest expand per D45's K table, which D46 verified as a true worst-case bound (no LUT textures exist; 99% of textures have maxlod=0 → TileLods emits nothing; state dedup resets per GDL via sub_GAME_7F0CC4C8).
- **seg-5 VTX offsets must be remapped** — all 2805 point into op4/op22 vertex arrays that move during re-layout (PexplosionbitZ: op4 with numVertices=0 but GDL loads 16 verts from offset 0x98 — the GDL's VTX count is authoritative, not the record field).
- PROMOTE contract: emit every promoted pointer as zero-extended u32 (low word of the 8B slot, high word 0); `PROMOTE` (`(u32)var + diff`, vma 0x5000000) then works unmodified. Fields promoted per opcode (model.c switch): all nodes Data/Parent/Next/Prev/Child; OP01 FirstGroup; OP02 ChildGroup; OP04 Vertices; OP08 Affects; OP09 left/rightChild; OP18 Controls; OP24 Vertices/CollisionVertices/PointUsage/CollisionVertices[i].LinkedTo. **NOT promoted:** GDL Primary/Secondary (stay raw 0x05xxxxxx), BaseAddr (game overwrites with fileramaddr).

**Load path (single hook point):**
- ALL model loads go through `load_object_fill_header()` (objecthandler_2.c:97): dst≠0 → `_fileNameLoadToAddr(name,0,dst,size)` (region = caller's bytes — see D45 for the grown per-region sizes); else `_fileNameLoadToBank(name,0,0x100,4)`. **Fresh-load semantics (D47.2-4):** `fileIndexLoadToBank` takes ALL remaining MEMPOOL_STAGE space (`S_bank`) only when poolRemaining==0; then F := alloc(S_bank), R := S_bank, load_resource decompresses D into F and sets P := decompressed size (D_N64 from ROM; **D_PC directly for sidecars — no fixup needed**). At compaction: **delta = R − P** (positive on fresh loads — the GDL mirror moves FORWARD). `fileSetSize(reallocate=1)` → `mempAddEntryOfSizeToBank` rewinds the bank cursor to the post-compaction size, giving the tail back.
- **Reload hazard + PC fix (D47.4, needed by BOTH plans):** fileSetSize leaves P=R=post-compaction size, so a same-stage reload would alloc(P_old) which can be too small for the compressed source placement → `load_resource` sets poolRemaining=0 → silent failure. PORT-guarded reset of poolRemaining=0 before the load call forces fresh-bank semantics every load (staging space = whole remaining bank; steady-state usage identical).
- **Plan A only (fallback):** the fixup must set ONLY poolRemaining (= D_PC exactly), never rom_remaining; stage at F+R−D_PC with guard `D_N64 + D_PC ≤ avail` (D48.4); hook after filedata assignment, before Switches/Textures/RootNode setup + sub_GAME_7F075A90 (PROMOTE walks PC ModelNode stride — the image must already be re-laid-out).

**Compaction contract (sub_GAME_7F0762E0, objecthandler_2.c:24-83) — the tightest constraint (applies to both plans):**
- It mirrors the GDL region [first_gdl_off, P_conv) into the caller-region's tail headroom (texCopyGdls to [A+R−P_conv+g, A+R)), then rewrites each GDL in place via texLoadFromGdl (expands texture markers into RDP commands), writing back starting at the original first-GDL offset; final fileSetSize = end of last rewritten GDL (= P_conv + 16·M_actual). **D46: fit and no-clobber are the SAME condition — R ≥ P_conv + 16×Σ(K_t−1) worst case — and all buffers satisfy it (see §F/D46 for the strict bound + the two cast-screen corrections).**
- Per-GDL `count` = byte distance between CONSECUTIVELY VISITED gdls → **GDLs must be packed contiguously at the tail of the PC image, in modelIterateDisplayLists visit order.**
- `modelIterateDisplayLists` (model.c:6254) **MUTATES the tree while walking**: LOD → `node->Child = LOD.Affects`; SWITCH → `node->Child = Switch.Controls`; BSP → `modelApplyReorderRelationsByArg(node,TRUE)` splices siblings (leftChild group before rightChild). Visit order per DL node: Primary, then Secondary (if non-null and ≠ previous), then DFS Child/Next/Parent.
- `tools_pc/d43_gdlorder.py` implements this exact simulation to verify visit-order adjacency — all 512 files pass (total GDLs 2301).
- Primary/Secondary are NOT promoted (raw 0x05xxxxxx), so modelNodeReplaceGdl's `Primary == find` comparisons work unchanged; after compaction Primary = replacementgdl (raw value, arbitrary parity).

**Render-time GDL resolution:**
- The game emits `gSPDisplayList(gdl, rwdata->gdl)` with w1 = raw 0x05xxxxxx; segment 5 (SPSEGMENT_MODEL_COL1) is set to BaseAddr (= file base) by the preceding gSPSegment. fast3d's `seg_addr` (port/fast3d/gfx_pc.cpp:2282) resolves raw 0x05xxxxxx via `segmentPointers[5] + (w1 & 0xFFFFFF)` — **DONE, committed.**
- Segments 3/4 (MTX table / vertex buffer) are set by the game per-render via gSPSegment ✓. BG segments (13/14/15) belong to the background path — later task.

## Helper scripts (tools_pc/, tracked; run from repo root)
- `d43_convert.py` — **reference layout + validator (D47):** DFS walk with LOD/SWITCH rewiring, region map, per-opcode record/vertex/GDL analysis, remap checks; runs on all 512 ROM files (ALL CLEAN). Lays the foundation for the emit pass (Task 2 step 1) — import/reuse its walk + region map.
- `d43_invariants.py` — full ownership tiling check (0 failures / 512).
- `d43_gdlorder.py` — exact visit-order simulation; all 512 pass.
- `d43_sizes.py` / `d43_sizes2.py` — decompress all 512, report N64/PC sizes + per-file worst-case P_final.
- `d43_walk.py` / `d43_fullwalk.py` / `d43_tree_dump.py` — tree walks, opcode histogram. (Header names lack the C/G/P prefix and trailing Z: file `CcamguardZ` → header `camguard`; all header files are literally named `modelFileHeader.inc.c` in per-model dirs.)
- `d43_gdldump.py` / `d43_gdlhist.py` / `d43_gdlseq.py` — GDL command dumps/histograms/sequences.
- `d43_seg5.py` / `d43_seg5ops.py` / `d43_seg5vtx.py` / `d43_seg5vtx2.py` — seg-5 VTX reference analysis (all 2805 point into op4/op22 vertex arrays → must remap).
- `d43_cover.py` — verifies every seg-5 VTX + SETTIMG target falls inside a modeled object (2804/2805; the one exception is PexplosionbitZ's orphan array at 0x98 — handled by the zero-vtx rule).
- `d43_lutscan.py` — proves NO LUT textures (formats 9-12) and maxlod distribution across all 512 files (D46).
- `d43_chainbound.py` — computes D46's strict bound for every buffer/chain vs the PC region sizes.
- `d43_decode.py` / `d43_layout*.py` / `d43_pointusage.py` / `d43_vtxfmt.py` — earlier investigation (format pinning).
- Gotcha: MODELFILEHEADER's name is field a[0] of the macro args (NOT the filename).

## Dev-loop speedups (priority order)
1. **Frame capture + pixel assert** — NOW (Task 2 step 4): env-gated PPM dump every N frames in `port/video.c` + `tools_pc/pixcount.py` (non-clear-color pixel count / diff two captures). Turns "the scene contains actual model geometry" into an assert, makes the 30 s soak scriptable, and enables screenshot diffs against Mupen64Plus with the verified No-Intro dump as ground truth.
2. **`tools_pc/sym.sh`** — tiny wrapper: takes a PC or rel offset, adds image base `0x140000000`, runs `addr2line -e build-pc/ge007.x86_64.exe -f -C`. One command per crash; re-verify the image base with `info address` after any link-layout change.
3. **`tools_pc/romdump.py`** — Python ROM/asset inspector: parse ModelFileHeader/ModelNode trees using the bondtypes.h layout, print opcode sequences / node links / Data records for a given cart address; also re-parse SIDECAR files as PC layout (post-Plan B this is the first stop for "geometry looks wrong").
4. D44 (automatic backtraces) — DONE, committed.

## PD port as standing reference (always available)
`PD_PORT_CHECKOUT` is the Perfect Dark PC port — same Rare engine
family, and the **standing reference/guidance source** for this project (noted in
AGENTS.md). Consult it whenever a remaining work item has a PD analogue:
- **Phase 2 asset conversion:** `port/src/preprocess/` (~4,100 lines) — per-ROM-segment
  N64→PC layout converters hooked from their `romdata.c`. `filemodel.c` = D43 near-analogue
  (same vma 0x5000000; PD converts at LOAD time — we convert OFFLINE for speed, same field
  rules apply); `filebg.c` (expected follow-on), `segaudio.c` (cross-check for our D37
  bank-tree fixup), `filelang.c`, `filetiles.c`, `filepads.c`, `filesetup.c`, `gbi.c`,
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
Log each as D3x in §F and note it in AGENTS.md phase status. **For asset FORMAT work,
prefer the offline pattern (D48): pin the format with Python over the ROM ground truth,
emit + validate offline, serve through the existing load path — runtime C conversion only
as fallback.**

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
  `build-pc/` (`d37_tree.py`, `d39_*.gdb`, `d41_probe.gdb`, …); the D43 investigation suite
  is in tracked `tools_pc/`.
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

## Definition of done
- ~~**Session A:** D49 committed~~ — **DONE**: 8/8 CONFIRMED with evidence,
  HANDOFF updated, committed.
- **Session B (the milestone proper):** stage objects load without faulting and the
  rendered scene contains actual model geometry per the pixel assert (not just the clear
  color / background), stable under a 30 s+ soak. Committed + pushed to `origin/master`
  (`PC port: phase 2 — <what>`). After that, Phase 2 makes it look right (fast3d CC/RM
  correctness vs `gmain.s` — GE-specific, no PD analogue) and Phase 3 adds sound + input,
  largely copy-and-adapt from the PD port.
