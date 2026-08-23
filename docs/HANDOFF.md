# Handoff brief — GoldenEye 007 PC port (Phase 2: Plan B executed (D50) — next session diagnoses D51, the main-DL texture-upload crash)

_Paste-ready briefing. Authoritative detail lives in `AGENTS.md` and
`docs/PCPortResearch.md` (§F/D43–D51 findings); this is the summary + the
immediate tasks._

## Your job

- **Session C (next): diagnose + fix D51** — SIGSEGV in `import_texture_i8`
  via a main-DL G_TEXRECT lazy tile-0 upload (frame 5). Recipe in Task 1.
  Then build the pixel assert (Task 2), run a soak, commit at each milestone
  (`PC port: phase 2 — <what>`). The goal: get past frame 5 with actual model
  geometry on screen, provable by pixel count.

Work agentically — fix → build (~5 s) → verify → commit at each milestone.

## Read first (authoritative, in order)
1. `AGENTS.md` — non-negotiables, critical files, build, verification ritual.
2. `docs/PCPortResearch.md` **§F/D50** (what the last session did, incl. the
   texCopyGdls byte proof), **D51** (current blocker + GE opcode facts), then
   D49/D48/D47 (sidecar contract — still ground truth for regeneration).
3. The crash-chain code: `port/fast3d/gfx_pc.cpp` (`gfx_run_dl` G_TEXRECT case
   ~line 2457, `import_texture*` ~600–950, `seg_addr` ~2282,
   `gfx_sp_moveword` ~2290s), `src/game/tex.c` (texCopyGdls + texLoadFromGdl).

## Non-negotiables (the ones that bite)
- N64 build files (`Makefile`, `tools/`, `rsp/`, `ld/`) — never touch.
- **Game logic is unmodified.** Hardware deps go in `port/`. Narrow exception:
  mechanical, semantics-preserving ABI/layout fixes forced by the 32→64-bit
  transition (embedded pointers → u32 + cast at use; PC-guarded pool sizing /
  idiom branches; UB-exposure seeds), each documented as a D3x/D50 finding.
  No behavior changes. The N64 path stays verbatim under `#else`.
- Region macros in `CMakeLists.txt` must mirror the N64 Makefile exactly.

## Current state
- **Plan B is EXECUTED (D50, committed):** all 512 NTSC model files converted
  offline by `tools_pc/d43_emit.py` → `data/pcmodels-ntsc-final/pcmodels.bin`
  + `manifest.csv` (single concatenated RZ image, 16-aligned offsets, decimal
  manifest); port plumbing in `port/src/pcmodels.c` (+ `port/include/pcmodels.h`,
  romdata.c/h cart-extension); one-shot `pcmodelsPatchTable()` + poolRemaining=0
  reset hook in `load_object_fill_header` (objecthandler_2.c, #ifdef PORT).
  Boot log: `[INFO] pcmodels: table patched (512 model entries)`.
- **Runtime C fixups landed (D50.3/D50.4):** language-bank BE offset tables
  (`romdataFixupLangBank`, called from language.c after each load + lazily in
  the langGetJpnCharPixels paths) and font re-layout
  (`romdataFontPcSize`/`romdataFixupFont` in textrelated.c). These are
  load-time C fixups, NOT offline sidecars — small blobs on different load
  paths; fine to keep runtime.
- **Legal-screen UB seed (D50.5):** front.c `constructor_menu00_legalscreen`
  read an uninitialized pointer (N64 survived by luck); PORT branch seeds it
  with `legalpage_text_array`. Identical lookat.
- **First model-render crash fixed (D50.6):** `texCopyGdls` copied only w0 of
  each 16-byte Gfx slot on PC (the union's trailing `long long` is 8 bytes),
  so the compacted GDLs kept stale mempool w1s → garbage G_SETTIMG → OOB in
  `import_texture_rgba16`. Byte-proven against PlegalpageZ (sidecar w1
  0x050012C8 vs RAM garbage 0xACAAB819 at file offset 0x2488). Fixed with a
  full-slot copy under #ifdef PORT. **Model GDLs now execute to completion.**
- **Current blocker — D51:** SIGSEGV on frame 5 in `import_texture_i8`
  (gfx_pc.cpp:782) via `G_TEXRECT` (main DL offset +0x260) → lazy upload of
  RDP tile 0 with an invalid/stale source. See Task 1. Frames 1–4 render
  successfully in every run (deterministic).

## Task 1 — D51 diagnosis (the immediate task)

The failing blit: 32×48 px screen rect (236,92)–(268,140), tile 0, i8 format.
G_TEXRECT carries no source address — it uses RDP texture-memory tiles; the
fault is in `import_texture(tile=0)` reading `rdp.textures[0]`'s stored source.
Work the suspects in order:

1. **Dump live (post-mortem dumps FAIL — 0x70052e80 reads "Cannot access
   memory" after the crash).** gdb launch mode:
   ```
   gdb -batch \
     -ex "handle SIGSEGV stop" \
     -ex "break import_texture_i8" \
     -ex run \
     -ex "print rdp.textures[0]" \
     -ex "up 4" \
     -ex "print cmd" -ex "print dListStart" \
     -ex "x/256wx <dListStart>" \
     ./build-pc/ge007.x86_64.exe
   ```
   (Adjust `up` depth to land in `gfx_run_dl`; the DL start may not be
   0x70052e80 every run — it's a mempool bump allocation. Alternatively break
   at `osSpTaskStartGo` and read `t->t.data_ptr` / `data_size`.)
2. **Find the G_SETTIMG (w0 top byte 0xFD) slots in the dumped main DL** and
   decode each w1 through the seg_addr rules (raw 0x05xxxxxx → seg5+low24;
   marked LSB → segmentPointers[seg]+addr; unmarked <0x10000000 →
   segmentPointers[seg]+low24; <0x800000 → +0x80000000; else passthrough).
   Also dump `segmentPointers[]` at the breakpoint. If a SETTIMG w1 resolves
   to an OOB/invalid pointer → that's the bug (converter remap miss, or a new
   address class seg_addr doesn't handle).
3. **If no main-DL SETTIMG sets tile 0 before the G_TEXRECT:** the tile is
   stale/uninitialized — check whether the game expects a tile set by an
   earlier frame's DL (fast3d `rdp` persistence across frames) or by a sub-DL
   that never ran.
4. **If the source pointer is valid but points at wrong-format/wrong-size
   data:** the game may point it at a ROM/ramrom image needing a port fixup
   (D50.3/D50.4 class) — check what asset lives there and how N64 PD handles
   the analogue.
5. **Find who emits the G_TEXRECT** in game code: grep `src/game/` for
   `gSPTileRect\|G_TEXRECT\|gSPTextureRectangle` (likely image.c / front.c /
   title.c — legal-screen or logo 2D blit). Reading the emitter tells you
   which texture it means and where its source should come from.

Record findings as **D51.x** in §F; fix in `port/` where possible (seg_addr /
tile setup), PORT-guarded game-code only if ABI/layout-forced. Rebuild,
confirm the crash moves past frame 5, then Task 2.

## Task 2 — pixel assert + soak (build it now — dev-loop item #1)

Env-gated framebuffer PPM dump every N frames in `port/video.c`
(e.g. `GE_PCDUMP=1` → `build-pc/frame_%05d.ppm`) + `tools_pc/pixcount.py`
(counts non-clear-color pixels; diffs two captures). Milestone: pixel count
rises above a small threshold and stays up through a 30 s+ soak. Screenshot
diffs against Mupen64Plus (verified No-Intro dump) are the ground truth for
"looks right". Commit: `PC port: phase 2 — D51 …` then `… pixel assert +
soak`.

## Task 3 — next asset type (after D51 + soak)

Tbg_*_stanZ stage backgrounds (different format/path than models). SAME
offline pattern (D48): pin the format with Python over ROM ground truth, emit
+ validate offline, serve through the existing load path. PD's
`port/src/preprocess/filebg.c` is the per-field reference (§2.4) — but same
family ≠ identical format; validate every field against GE headers + ROM.

## Verified facts (sidecar spec — ground truth; do NOT re-derive)

**Sidecar serving (D50):**
- Sidecars live in `data/pcmodels-<region>/` = **one concatenated
  `pcmodels.bin` + `manifest.csv` (`name,offset,size`, DECIMAL — C parser
  uses strtol base 10; rows in file_resource_table.inc.c order)**. NOT
  per-file .bin. Regenerate: `python tools_pc/d43_emit.py [ntsc-final|
  pal-final|jpn-final|--check-only]` (needs the region ROM in data/).
- RZ format = `[0x11 0x72][raw deflate]`. Python decompress for inspection:
  `zlib.decompress(raw[2:], -15)` after checking the 2-byte header.
- pcmodels.c copies the whole blob to `[CART_BASE+romSize, …)` (cart base
  0x10000000, romdataInit reserves romSize+sidecarTotal;
  romdataCartAddrValid accepts the extension) and patches
  `file_resource_table[i].hw_address = cartBase+romSize+off` +
  `resource_lookup_data_array[i].rom_size = C_pc` per manifest row — one-shot,
  from `load_object_fill_header` (after obInit has run). The existing
  romCopy/decompressdata path then serves PC bytes unmodified and sets
  poolRemaining = D_PC.
- **Inspecting a loaded file offline:** find the name in manifest.csv →
  decompress that slice → it is exactly what lands at the file base in RAM
  (verified byte-for-byte for PlegalpageZ). GDLs are at the tail of the image
  in visit order; `x/Nwx <filebase+off>` in gdb compares directly.

**Format / layout:**
- All 512 C*/G*/P*Z model files decompress fine; each file starts directly with
  the switches array (no in-file header — NS/NT come from the exe
  `ModelFileHeader` record via `objheader->numSwitches/numtextures`).
- PC file layout: `[switches NS×8B][texconfigs NT×12B][root node @ 8NS+12NT]…`;
  all embedded pointers are vma `0x05xxxxxx` (mask 0xFFFFFF). N64 was
  `[NS×4][NT×12][root @ 4NS+12NT]`.
- **Tiling invariant verified, 0 failures in all 512 files**
  (`tools_pc/d43_invariants.py`): the image is exactly tiled by {switches,
  textures, nodes(48B PC), records(PC_REC per opcode), vertex arrays(nv×16B),
  collision arrays(nc×16B), PointUsage (numVertices s16 entries), GDLs (each
  extends to the next GDL start or D)}. Records INTERLEAVE with vertex arrays
  in character bodies — do not assume "all records before bulk".
- **ModelNode = 24B N64 / 48B PC** (u16 op + 2 pad + 5 ptrs: Data@4/8,
  Parent, Next, Prev, Child). Record sizes N64/PC: OP01 0x10/0x18, OP02
  0x1C/0x28, OP04 0x14/0x28, OP08 0x10/0x18, OP09 0x24/0x30, OP10 0x1C/0x1C,
  OP12 0x28/0x30, OP13 0x20/0x30, OP15 0x1C/0x1C, OP17 0x20/0x28, OP18
  0x08/0x10, OP21 0x14/0x14, OP22 0x10/0x20, OP23 0x02/0x02, OP24 0x20/0x40
  (compiler-verified by `tools_pc/d43_layoutprobe.c`). Opcodes present:
  1,2,4,8,9,10,12,13,15,18,21,22,23,24.
- **Endianness:** f32 fields BE in ROM → bswap; every u16/s16 record scalar
  BE → bswap16; vertex-array s16s BE → bswap16 (x,y,z,index,s,t); TextureID
  u32 → bswap32. Padding/reserved zeroed.
- **Vertex arrays stay 16B-stride** (GDL gSPVertex hardcodes it). Collision
  vertices carry LinkedTo@8 = vma of a ModelNode — remapped.
- **GDLs:** N64 8B BE word pairs → PC 16B LE slots: `w0=bswap32(rom_w0);
  w1=bswap32(rom_w1)`; **no LSB set** (fast3d's seg_addr handles unmarked +
  raw-0x05). Opcode-aware remap: only {G_VTX=0x04, G_SETTIMG=0xFD,
  G_LOADBLOCK=0xF3} carry addresses in w1; seg-5 low24 remapped via the region
  map. **0xB1 (G_TRI4) w1 is packed 8-bit vertex indices — NEVER an address.**
  G_VTX w0 is GBI1-style (`04<<24 | dst<<16 | 16·n`) — bswap only.
- **Marker format:** top byte of w0 = 0xC0; type = `w0 & 7`; texnum =
  `w1 & 0xfff`. texLoadFromGdl expands markers into RDP commands (that's the
  compaction growth); default case copies the full 16B slot as-is — **the
  sidecar must contain final correct w1 values** (no runtime remap in
  texLoadFromGdl).
- **PROMOTE contract:** promoted pointers emitted as zero-extended u64 with
  low32 = VMA `0x05xxxxxx`; `PROMOTE` (`(u32)var + diff`, vma 0x5000000) works
  unmodified. GDL Primary/Secondary NOT promoted (raw VMAs); BaseAddr=0 (game
  overwrites with fileramaddr).
- **texCopyGdls contract (D50.6):** on PC it must copy the FULL 16-byte slot
  (`*arg1 = *arg0` under #ifdef PORT) — the N64 `long long` idiom copies only
  w0 on x86-64 and poisons the compaction scratch. texLoadFromGdl then reads
  the scratch and writes back at the original GDL offsets; final fileSetSize
  shrinks the bank chunk.

**GE opcode space (include/PR/gbi.h, non-F3DEX numbering — verified D51):**
- DMA: G_SPNOOP=0, G_MTX=1, G_MOVEMEM=3, G_VTX=4, G_DL=6, G_SPRITE2D_BASE=9.
- IMM (G_IMMFIRST=−65): TRI1=0xBF, CULLDL=0xBE, POPMTX=0xBD, MOVEWORD=0xBC,
  TEXTURE=0xBB, SETOTHERMODE_H=0xBA, SETOTHERMODE_L=0xB9, ENDDL=0xB8,
  SETGEOMETRYMODE=0xB7, CLEARGEOMETRYMODE=0xB6, LINE3D=0xB5, RDPHALF_1=0xB4,
  RDPHALF_2=0xB3, TRI2=0xB2, **G_TRI4(GE)=0xB1** (gbi_extension.h).
- RDP pass-through: G_SETTIMG=0xFD, G_LOADTXC=0xFE, G_SETCIMG=0xFF,
  **G_TEXRECT=0xE4 / G_TEXRECTFLIP=0xE5** (GE-specific values — NOT libultra's
  0x46/0x45), plus VI2D=0xF5 etc.
- `gSPSegment(pkt,seg,base)` = `gMoveWd(pkt,G_MW_SEGMENT,seg*4,base)` →
  w0=(0xBC<<24)|(seg*4<<8)|6, w1=data. fast3d gfx_sp_moveword stores data as-is
  when ≥0x800000 else +0x80000000.
- Main DL is game-built per frame with gSP* macros (16B slots) — NOT from a
  sidecar. Segment table: seg0=0, seg1=0x7029F800-ish, seg3=render_pos,
  seg4=runtime vtx buffer, seg5=file base (set per object).

**Load path / compaction (unchanged by D50 — see D47/D46/D48 for full detail):**
- ALL model loads go through `load_object_fill_header()`; dst≠0 →
  `_fileNameLoadToAddr` (caller's buffer), else `_fileNameLoadToBank` (fresh
  bank when poolRemaining==0 — the hook resets it every time).
- Compaction: texCopyGdls mirrors [G,D) to tail scratch [B−D+G,B);
  texLoadFromGdl rewrites each GDL in place with marker expansion; final size
  = D + 16·Σ(K_t−1) worst case (P_est model — the emit pass's cross-checks
  encode it for every dst!=0 buffer and chain).

## Helper scripts (tools_pc/, tracked; run from repo root)
- `d43_emit.py` — **THE emit pass (D50.1):** converts all 512 files, validates
  round-trip + tiling + buffer fits, writes pcmodels.bin + manifest.csv.
  `--check-only` runs validation without writing. First stop for any
  "sidecar looks wrong" question.
- `d43_layoutprobe.c` — compiler probe that re-verifies every PC record/node/
  header size (run with the exact CMake flags; compare against d43_emit.py's
  tables after any bondtypes.h change).
- `d43_convert.py` — reference layout + validator (D47); foundation of the
  emit pass.
- `d43_invariants.py` / `d43_gdlorder.py` / `d43_sizes*.py` / `d43_chainbound.py`
  / `d43_cover.py` / `d43_lutscan.py` / `d43_seg5*.py` / `d43_gdl*.py` — the
  investigation suite (all pass on NTSC).
- Gotcha: MODELFILEHEADER's name is field a[0] of the macro args (NOT the
  filename); nsnt lookup keys by header symbol (`armourguard`), not file base
  name (`CarmourguardZ`).

## Dev-loop speedups (priority order)
1. **Frame capture + pixel assert** — NOW (Task 2): env-gated PPM dump every N
   frames in `port/video.c` + `tools_pc/pixcount.py`. Turns "the scene contains
   actual model geometry" into an assert and makes the 30 s soak scriptable.
2. **`tools_pc/sym.sh`** — tiny wrapper: PC or rel offset → add image base
   0x140000000 → `addr2line -f -C`. Re-verify the image base with `info
   address` after any link-layout change.
3. Sidecar inspection is now trivial offline (manifest + zlib, see Verified
   facts) — don't gdb the game for "geometry looks wrong" in a model file.
4. D44 automatic backtraces — DONE; `ge007.crash.log` is the first stop.

## PD port as standing reference (always available)
`PD_PORT_CHECKOUT` is the Perfect Dark PC port — same Rare
engine family, the **standing reference** for this project (noted in
AGENTS.md). Consult it whenever a remaining work item has a PD analogue:
- **Phase 2 asset conversion:** `port/src/preprocess/` (~4,100 lines) —
  per-ROM-segment N64→PC layout converters hooked from their `romdata.c`.
  `filemodel.c` = D43 near-analogue (PD converts at LOAD time — we convert
  OFFLINE for speed, same field rules apply); `filebg.c` (expected follow-on),
  `segaudio.c`, `filelang.c`, `filetiles.c`, `filepads.c`, `filesetup.c`,
  `gbi.c`, `segfonts.c`.
- **Phase 3/4 (copy-and-adapt; our stubs were scaffolded for this):**
  `port/src/mixer.c` (722 lines vs our 31), `input.c` (1,551 vs 48 —
  SDL_GameController backend; remap buttons to GE's scheme), `fs.c` (294 vs 81
  — file-backed save dir).
- **Caveats:** copy port-layer files only (their game code was modified for
  PC); same family ≠ identical format — validate every conversion per-field
  against GE headers + ROM ground truth. Full audit: §2.4.

## Standing procedure (you will hit more of these)
Every ROM-serialized struct with a pointer field faults the same way once you
reach more asset loading. The D32 fix procedure (doc §H): at the fault,
`ptype /o <Struct>`; if a pointer field's offset/size diverges from its N64
offset comment, change it to `u32` + cast at the use sites; verify the
load-time rebase yields valid V1 addresses (< 0x80000000); **also check the
ROM bytes' endianness per-field (D33 rule: u32 bswap32 / u16 bswap16 / u8
identity) and any fixup loop's pointer stride on x86-64**; for packed struct
trees use the D37 re-layout pattern; for address-arithmetic idioms over exe
symbols (D39/D42 class) guard a PC branch that reproduces the N64 32-bit value
exactly. Rebuild; confirm boot advances. Log each as D5x in §F and note it in
AGENTS.md phase status. **For asset FORMAT work, prefer the offline pattern
(D48): pin the format with Python over the ROM ground truth, emit + validate
offline, serve through the existing load path — runtime C conversion only as
fallback.** Also: any code that copies Gfx slots must use full-struct copies on
PC (D50.6) — never the `long long` union member.

## Environment (do not rediscover these)
- MSYS2/MinGW tools are in `/c/msys64/mingw64/bin/` — **not** on PATH. Prefix
  every shell: `export PATH="/c/msys64/mingw64/bin:$PATH"` (build-pc.sh needs
  it for cmake too).
- Build: `./build-pc.sh ntsc-final` (incremental; full rebuild ~5 s on the
  16-core dev box). ROM at `data/ge007.ntsc-final.z64` (= `baserom.u.z64`,
  byte-identical to the No-Intro good dump, MD5
  `70c525880240c1e838b8b1be35666c3b`).
- **Sidecars are gitignored (data/).** If `data/pcmodels-ntsc-final/` is
  missing on a fresh checkout, regenerate: `python tools_pc/d43_emit.py
  ntsc-final`. The game boots without them (warn + ROM-only mode) but model
  loads fail.
- The ROM is **mapped at cart base 0x10000000** on PC; sidecar image follows
  at [CART_BASE+romSize, …). All game RAM lives in the V1 view at 0x70xxxxxx
  (< 4 GiB) — s32 truncation of game-RAM pointers is lossless.
- gdb: **launch** mode only (`gdb -batch -ex "handle SIGSEGV stop" -ex run …`
  — attach fails with error 87). Symbolicate offline with `addr2line -e
  build-pc/ge007.x86_64.exe -f -C <0x140000000+rel>`. Image base
  0x140000000. **Post-mortem memory dumps of game RAM can fail** ("Cannot
  access memory") — dump at a live breakpoint instead (hit this with the main
  DL in D51).
- Many init functions use a fake RBP — compute stack offsets from the **entry
  RSP**, not RBP.
- Python on this box: no f-strings with nested quotes (< 3.12) — use `%`
  formatting.
- Untracked scratch at repo root (all.txt, b3.c, btest*.c, buildtest.txt,
  err.txt, gcout.txt, preproc.txt, vsize.*) + `tools_pc/__pycache__/` are
  session scratch — do NOT commit them; use explicit pathspecs with git add.

## Definition of done
- **Session C:** D51 diagnosed + fixed (finding in §F, crash moves past frame
  5), pixel assert built, 30 s+ soak clean with real geometry on screen.
  Committed + pushed (`PC port: phase 2 — <what>`). After that: Phase 2 makes
  it look right (fast3d CC/RM correctness vs `rsp/graphics/gmain.s` —
  GE-specific ground truth, no PD analogue), next asset type (Tbg_*_stanZ
  backgrounds), and Phase 3 adds sound + input (copy-and-adapt from the PD
  port).
