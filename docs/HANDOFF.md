# Handoff brief — GoldenEye 007 PC port (Phase 1.5 done, Phase 2: first frames render)

_Paste-ready briefing for the next agent session. The authoritative detail lives in
`AGENTS.md` and `docs/PCPortResearch.md` (§H handoff, §F/D31–D44 findings); this is
the summary + the immediate tasks._

## Your job
Two tasks, in order:
1. ~~**D44 (quick win, ~15 min):** one-line fix to the crash handler~~ — **DONE,
   committed.** Every crash now auto-logs a `BACKTRACE:` section to stdout and
   `ge007.crash.log`; the D43 chain (#00 model.c:5688 ← #01
   load_object_fill_header) was confirmed without gdb. The debug loop is now
   "read ge007.crash.log".
2. **D43 (main task):** model-file loading ABI mismatch — stage object load faults
   in `modelPromoteNodeOffsetsToPointers()` (src/game/model.c:5688) because ROM model
   files are serialized with N64 layout (4-byte pointer fields) but read on PC as
   8-byte-pointer structs. **Investigation is now COMPLETE** (all format details,
   invariants, and the exact implementation plan are below + in §F/D43). What remains
   is pure implementation: a two-pass re-layout converter in `port/src/romdata.c`,
   ~6 one-line mechanical game-code edits, one fast3d seg_addr case, one hook line.
   Then keep pushing rendering forward (more asset types will fault the same way).

Work agentically — fix → build (~5 s) → verify → commit at each working milestone.
Commit message style: `PC port: <phase> — <what>`.

## Read first (authoritative, in order)
1. `AGENTS.md` — non-negotiables, critical files, build, verification ritual, phase status.
2. `docs/PCPortResearch.md` §H (handoff + plan), then **§F/D44** (the quick win),
   **D43** (the open model-file task), **§2.4** (PD-port copy-candidate audit —
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
- **D43 investigation finished (latest session):** all 512 model files decompress and
  walk cleanly; layout invariants verified with zero failures; the converter design,
  every mechanical edit, and the compaction contract are fully pinned down — see the
  task section below before writing any code. No D43 code has been written yet.
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
- **Tiling invariant verified, 0 failures in all 512 files** (`build-pc/d43_invariants.py`): `[0,D)` is exactly tiled by {switches, textures, nodes(20B), records(RSZ per opcode), vertex arrays(nv×16B), collision arrays(nc×16B), PointUsage (extends to the next owned object — size varies 8..3248B, NOT derivable from counts), GDLs (each extends to the next GDL start or D)}. Records INTERLEAVE with vertex arrays in character bodies — do not assume "all records before bulk".
- **Opcodes present: only 1,2,4,8,9,10,18,21,23,24** (HEADER, GROUP, DL, LOD, BSP, BOX, SWITCH, GROUPSIMPLE, HEADPH, DLCOLLISION). Converter handles exactly these; fatal on anything else.
- N64 record sizes: OP01=0x10, OP02=0x1C, OP04=0x14, OP08=0x10, OP09=0x24, OP10=0x1C, OP18=0x08, OP21=0x14, OP23=0x02, OP24=0x20. Field layouts per opcode are in `src/bondtypes.h` (ModelRoData_*Record) — PC record sizes differ due to pointer padding (e.g. OP04 20B→0x28). Since `port/src/romdata.c` is PC-only, read N64 fields from packed offsets and emit via the real PC structs (or explicit placement) — don't hand-compute PC offsets.
- **Vertex arrays must stay 16B-stride** (GDL gSPVertex hardcodes 16B/vertex; propobj.c copies with sizeof(Vertex); model.c:4360 allocates numVertices×4 words). bswap every s16 field BE→LE. Main Vertices array is pure data; **CollisionVertices carry LinkedTo@8 = vma of a ModelNode** — remap to the new node offset (PROMOTE promotes them after rebase).
- **GDLs:** 8B BE word pairs → 16B LE slots: `w0=bswap32(rom_w0); w1=bswap32(rom_w1)`. Set LSB of w1 on segmented commands (PD's gbi.c trick — fast3d's seg_addr requires w1&1 to take the segment path): GE values G_MTX=0x01, G_MOVEMEM=0x03, G_VTX=0x04, G_DL=0x06 + the RDP setimg family (check exact opcodes in include/PR/gbi.h before coding; PD's CMD_IS_SEGMENTED = MOVEMEM/MTX/VTX/COL/DL/SETTIMG/SETCIMG/SETZIMG). Do NOT set LSB on non-segmented commands.
- PROMOTE contract: emit every promoted pointer as zero-extended u32 (low word of the 8B slot, high word 0); `PROMOTE` (`(u32)var + diff`, vma 0x5000000) then works unmodified. Fields promoted per opcode (model.c switch): all nodes Data/Parent/Next/Prev/Child; OP01 FirstGroup; OP02 ChildGroup; OP04 Vertices; OP08 Affects; OP09 left/rightChild; OP18 Controls; OP24 Vertices/CollisionVertices/PointUsage/CollisionVertices[i].LinkedTo. **NOT promoted:** GDL Primary/Secondary (stay raw 0x05xxxxxx), BaseAddr (game overwrites with fileramaddr).

**Load path (single hook point):**
- ALL model loads go through `load_object_fill_header()` (objecthandler_2.c:97): dst≠0 → `_fileNameLoadToAddr(name,0,dst,size)` (region = caller's bytes; body/head use 0x14820 per hand — PC image P must fit); else `_fileNameLoadToBank(name,0,0x100,4)` which allocates the ENTIRE remaining MEMPOOL_ME, so expansion D→P always fits and no memp bookkeeping is needed.
- `fileIndexLoadToAddr` sets `rom_remaining = bytes` (region size) BEFORE load_resource sets `poolRemaining = D`. The compaction function reads both: **the fixup must set ONLY poolRemaining (=P), never rom_remaining.** Do NOT use fileSetSize for this — it clobbers rom_remaining too. Set `resource_lookup_data_array[fileGetIndex(name)].poolRemaining = P` directly (check where the array is declared, likely ob.h).
- **Hook location:** inside load_object_fill_header, immediately after the filedata assignment and BEFORE Switches/Textures/RootNode setup + sub_GAME_7F075A90 — PROMOTE walks with PC ModelNode stride (40B), so the file must already be re-laid-out. Signature: `u32 romdataFixupModelFile(u8 *blob, const char *name, s16 NS, s16 NT)` returning P; one `#if defined(PORT)` call site.

**Compaction contract (sub_GAME_7F0762E0, objecthandler_2.c:24-83) — the tightest constraint:**
- It mirrors the GDL region [first_gdl_off, P) into the caller-region's tail headroom, then rewrites each GDL in place via texLoadFromGdl (expands G_SETTEX/G_NOOP into RDP texture commands), writing back starting at the original first-GDL offset; final fileSetSize = end of last rewritten GDL.
- Per-GDL `count` = byte distance between CONSECUTIVELY VISITED gdls → **GDLs must be packed contiguously at the tail of the PC image, in modelIterateDisplayLists visit order.**
- `modelIterateDisplayLists` (model.c:6254) **MUTATES the tree while walking**: LOD → `node->Child = LOD.Affects`; SWITCH → `node->Child = Switch.Controls`; BSP → `modelApplyReorderRelationsByArg(node,TRUE)` splices siblings (leftChild group before rightChild). Visit order per DL node: Primary, then Secondary (if non-null and ≠ previous), then DFS Child/Next/Parent.
- `build-pc/d43_gdlorder.py` implements this exact simulation to verify visit-order adjacency — **has a known bug**: the initial node-collection pass follows only Child links, but the mutable walk reaches nodes via Next → crashes `KeyError: 400`. Fix: also push `n.next` during collection. Run it after fixing; expect all 512 files to pass (consecutive visited GDLs offset-adjacent).
- Primary/Secondary are NOT promoted (raw 0x05xxxxxx), so modelNodeReplaceGdl's `Primary == find` comparisons work unchanged; after compaction Primary = replacementgdl (raw value, arbitrary parity — see next item).

**Render-time GDL resolution:**
- The game emits `gSPDisplayList(gdl, rwdata->gdl)` with w1 = raw 0x05xxxxxx; segment 5 (SPSEGMENT_MODEL_COL1) was just set to BaseAddr (= file base) by the preceding gSPSegment. fast3d's `seg_addr` (port/fast3d/gfx_pc.cpp:2282) currently sends 0x05xxxxxx down the "direct pointer" path (invalid). **Port-layer fix:** add a case — top byte 0x05 → `segmentPointers[5] + (w1 & 0xFFFFFF)`.
- Segments 3/4 (MTX table / vertex buffer) are set by the game per-render via gSPSegment ✓. BG segments (13/14/15) belong to the background path — later task.

### Implementation checklist (in order)
1. Fix + run `build-pc/d43_gdlorder.py` → confirm visit-order adjacency for all 512 files.
2. **Mechanical game-code edits (D32 class — document in §F, each semantics-identical on N64):**
   - `src/bondtypes.h` Vertex: union members `struct Vertex *LinkedTo; void *CollisionRelatedNode;` → `u32 LinkedTo; u32 CollisionRelatedNode;` (sizeof(Vertex) stays 16 both platforms; game heap <4GiB so lossless on PC).
   - `src/game/model.c`: add `#define PROMOTE32(var) if (var) var = (u32)((u32)var + diff)` beside PROMOTE (~line 5677); line 5735 `PROMOTE(...LinkedTo)` → `PROMOTE32(...)`.
   - `src/game/chr.c:3257`: cast the now-u32 field `(ModelNode *)(uintptr_t)…`; line 3259 `!= NULL` → `!= 0`. (These are the only 3 use sites of the union.)
   - `src/game/objecthandler_2.c:103`: `&((s32*)filedata)[objheader->numSwitches]` → `((u8*)filedata + sizeof(ModelNode*) * objheader->numSwitches)` (N64: 4B stride, identical).
   - `src/game/tex.c`: texCopyGdls `arg2 = (arg2 >> 3)` → `arg2 / sizeof(Gfx)`; texLoadFromGdl `count = srcsize >> 3` → `srcsize / sizeof(Gfx)` and dispatch `switch (*(u8*)in)` → `switch ((u8)(in->words.w0 >> 24))` (N64-identical; those are the ONLY byte-level accesses in that function — everything else already uses words.w0/w1, and the default case copies whole slots).
3. **fast3d seg_addr** — add the 0x05xxxxxx → segmentPointers[5] case (port layer).
4. **`romdataFixupModelFile` in `port/src/romdata.c`** (PC-only): two-pass walk — collect all objects + sizes, then emit the PC image in place. Emission order: switches(NS×8B) → textures(NT×12B; bswap TextureID only) → nodes+records in DFS preorder (PC layout; rewrite every promoted pointer field to 0x05xxxxxx|new_offset) → bulk arrays (vertex/collision: bswap s16s, remap LinkedTo; PointUsage: bswap s16s, size = distance to next owned object in the source file) → **GDLs LAST, contiguous, in visit order** (16B LE slots, LSB-set on segmented commands).
5. **Hook** into load_object_fill_header (`#if defined(PORT)` one-liner + poolRemaining update as above).
6. **Build, run, verify:** no SIGSEGV in modelPromoteNodeOffsetsToPointers; scene shows actual geometry (not clear color); 30 s+ soak stable. Check PC image sizes vs the ToAddr region (body = 0x14820): extend `build-pc/d43_sizes.py` with a PC-size estimate (nodes 40B, records PC-sized, switches 8B, GDLs ×2) and confirm max fits; if not, D40-class pool growth.

### Helper scripts (build-pc/, written this session)
- `d43_decode.py` — decompress + decode one model's node tree + GDL command stream.
- `d43_sizes.py` — decompress all 512, report sizes (total 0x323100; largest body ~34KB).
- `d43_walk.py` — walks all trees using exe NS/NT (parses all 512 `assets/obseg/**/modelFileHeader.inc.c`); opcode histogram. Note: header names lack the C/G/P prefix and trailing Z (e.g. file `CcamguardZ` → header `camguard`).
- `d43_invariants.py` — full ownership tiling check (0 failures / 512).
- `d43_gdlorder.py` — exact visit-order simulation (**collection bug, see above**).

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
