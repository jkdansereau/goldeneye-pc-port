# Handoff brief — GoldenEye 007 PC port (Phase 2: Session I — D69 BG/stan stage loading)

_Paste-ready briefing. Authoritative detail lives in `AGENTS.md` and
`docs/PCPortResearch.md` (§F/D59–D74 findings, §G status, §H procedure); this
is the summary + the immediate tasks._

## Your job

- **Session I — D69 (the milestone blocker):** make the first stage (BUNKER1)
  load and render. BG-file data is N64 big-endian; reverse-engineer GE's bg
  `.seg` + `Tbg_*_stanZ` formats from the decompiled consumers, then convert
  or fix them for PC. This is the only thing standing between the intro and
  gameplay (verified: clean runs reach frame ~2100 in ≈2 min wall-clock, then
  fault deterministically in `load_bg_file`, bg.c:830).
- **Secondary (low priority):** final pixel check of the intro logo. D73 +
  D74 fixed it at the data level and PPM frames ~550–560 show four gold
  letters on the dark-blue plate; compare against N64 reference footage if
  time allows (reference screenshots are at repo root: `Graphics Screenshot
  2026-08-24 121659.jpg`, `gun barrel.png`).
- Work agentically: decode → implement → build (~5 s) → verify → commit at
  each sub-milestone (document findings as D7x in §F).

## Current state (verified this session)

- **Build GREEN.** `export PATH="/c/msys64/mingw64/bin:$PATH" && ./build-pc.sh ntsc-final`.
- **D59–D68 resolved** (details in PCPortResearch.md §F): blood-RLE sentinel,
  HEADS/BODIES signed sentinels, romCopyAligned/ramrom 64-bit, image_entry
  layout, Globalimagetable BE→LE fixup (`port/src/gimgfixup.c`), etc.
- **D70–D74 (intro logo) resolved:**
  - D70: `GE_PCDUMP` PPM frame capture (kept — permanent dev tooling).
  - D71: C-array texture sources (rarewarelogo.c RGBA16 images) bswapped on
    LE PC → port-layer per-source normalization in `import_texture`.
  - D72: GE always uses authored `tc[]` UVs; `lookat_enabled` defaults false.
  - **D73 (root cause of the old D72.3):** sinf/cosf `du` double constants
    are big-endian word pairs → garbage on LE PC → guMtxF2L emitted −32768
    for every sin/cos entry → logo triangles projected off-screen. Fixed with
    the `DVAL()` macro in `src/libultra/gu/guint.h` (PORT-only; N64 build
    untouched). All scenes using guRotate/guLookAt-derived matrices were
    affected, not just the logo.
  - **D74:** texture import fallback no longer truncates valid gDPLoadBlock
    data (was dropping letter mip chains and reducing D_02005FF0 to 32×3);
    `TextureCacheKey` gained `size_bytes`; VBO path now wraps UVs by tile
    size for WRAP sub-tiles (N64 semantics; the logo's 20×3 sub-tile at
    offset (11.5, 29) was sampling row 0 instead of rows 29–31).
- **Runtime:** boots → intro music → full intro (Rareware logo with gold
  letters, gun barrel w/ Brosnan, cast screen) → **crash in `load_bg_file`
  (bg.c:830)** on first stage load. EXCEPTION 0xc0000005; the crash log at
  repo root (`ge007.crash.log`, gitignored) is from this exact fault:
  PC=0x14000627c, FAULT ADDR = Rax+0x28 (reading `pPointTableBin` through the
  mis-rebased room-fileposition pointer).
- **Committed through D74.** This session's TEMP probes are all stripped;
  previously committed TEMP diagnostics remain — strip list in Task 3.

## The blocker, precisely (D69)

`load_bg_file` (src/game/bg.c:800+) for level BUNKER1:
1. `obLoadBGFileBytesAtOffset("bg/bg_sev_all_p.seg", header, 0, 0x40)` —
   **works**. `&fileentry->hw_address[offset]` == `hw_address + offset`;
   compiled asset symbols are absolute cart addresses (e.g.
   `bg_sev_all_p_seg` @ 0x10438660) into the ROM mapped at CART_BASE, and the
   PI shim memcpys from there. GE_D69 probe confirms: idx=1, rom_size=0x10DF0,
   hw=0x10438660.
2. `((s32 *)ptr_bg_data)[1]` — **the bug**. ROM bytes at +4 are
   `0F 00 00 14` (BE value 0x0F000014). N64: BG_SEG_TO_PTR folds
   `off + 0xF1000000` → file offset 0x14. PC reads LE 0x1400000F → pointer
   ~5 MB past the stack buffer → fault reading `[1].pPointTableBin`.
3. Same class applies to: every bg_room_data record (more 0x0Fxxxxxx offsets,
   pPointTableBin at record+0x28 per the crash disasm), the rest of the .seg
   header/tables, and the whole `Tbg_sev_all_p_stanZ` geometry file
   (stanDetermineEOF/stanLoadFile in stan.c).

**Strategy options** (decide after decoding; AGENTS.md prefers offline):
- **(a) Offline sidecar conversion** (the D43/Plan-B pattern): a tools_pc
  script converts all bg/*.seg + Tbg_*_stanZ per region into one image +
  manifest, served through the existing load path via a port-layer table
  patch (cf. `port/src/pcmodels.c`). Pros: matches established pattern, no
  runtime cost, regenerable per region. Cons: must fully decode both formats.
- **(b) Runtime port-layer fixup** after each load (hook where the bytes land).
  Same format knowledge; smaller initial step if only a few fields matter for
  first-frame render — but stage geometry is deep; likely most fields end up
  needing conversion anyway.

**Caveat:** PD's `port/src/preprocess/filebg.c` describes a *different* BG
format (zipped multi-section: primary/section1/2/3 headers). GE's .seg files
are raw with segment-0x0F internal offsets. Same engine family ≠ identical
format — use PD only as an approach reference; validate every field against
GE's own consumers (bg.c, stan.c, and the N64 disasm where in doubt).

## Read first (authoritative, in order)

1. `AGENTS.md` — non-negotiables, critical files, build, verification ritual.
2. `docs/PCPortResearch.md` **§F/D69** (blocker + strategy), then D68
   (Globalimagetable fixup — the template for BE data in this segment family),
   D73–D74 (this session), §G status, and the environment reminders at the end
   of §H.
3. The consumers: `src/game/bg.c` (`load_bg_file` ~line 795+, levelinfotable
   line 183, BG_SEG_TO_PTR in bg.h:23), `src/game/stan.c` (stanLoadFile /
   stanDetermineEOF), `src/game/ob.c` (obLoadBGFileBytesAtOffset — note the
   TEMP D69 probe), `assets/obseg/file_resource_table.inc.c` (name→symbol map).
4. Reference for the offline pattern: `tools_pc/d43_emit.py` +
   `port/src/pcmodels.c` + romdata cart-extension plumbing.

## Task 1 — decode the formats

1. Dump BUNKER1's .seg header + first records from the ROM (cart 0x10438660,
   size 0x10DF0) and map each word to its consumer in bg.c:
   - word0 = 0; word1 = room-fileposition-list offset (0x14); word2/word3 =
     more table offsets (0x334 / 0x32C).
   - Follow `ptr_bgdata_room_fileposition_list[1].pPointTableBin` etc. into
     bg_room_data (bg.h) and note every offset field + its base
     (BG_SEG_TO_PTR = base + off + 0xF1000000, i.e. stored as 0x0Fxxxxxx).
2. Do the same for `Tbg_sev_all_p_stanZ` (cart 0x1087C3F0, size 0x3DD0):
   StanPrefixRecord + whatever stanLoadFile walks. Cross-check struct field
   offsets against the N64 disasm of the consumer functions where the decomp
   is ambiguous.
3. Write the format notes into PCPortResearch.md §F/D69 (or a new §I) as you
   go — they are the converter spec.

## Task 2 — implement + verify

1. Implement the chosen conversion/fixup; wire it in (sidecar table patch or
   load hook).
2. Verify: stage loads without fault, then renders frames (watch for the next
   BE field class surfacing at render time — vertex tables, DL pointers,
   texture refs inside stan data; expect a few iterations, D43 had ~10).
3. Once a stage renders: pixel-assert soak (PPM dump + tools_pc/pixcount.py),
   then move on (next stages use the same files pattern — make sure all 20+
   levels' bg/stan files are covered, not just BUNKER1).

## Task 3 — strip TEMP diagnostics before committing

This session's probes (GE_DBGUV/DBGTRI/DBGTALL/DBGMAT/D74IMP/D74DUMP/DBGLOAD)
are already stripped. Previously committed TEMP diagnostics still in the tree
(grep `TEMP D` + `GE_D`):
- gfx_pc.cpp: **TEMP D63** — the dram-branch trail (`s_d63taskcount`,
  `d63RecordBranch`/`d63DumpTrail`/`d63LogLastBranchReread` externs near the
  top of gfx_run_dl, the call in the G_DL case that also spams `[NOTE] D63
  dram-branch` every frame, the GE_D63 dump block, and the function
  definitions at the end of the file); D59 seg probes if still present.
- gfx_pc.cpp: `GE_D71LOG` one-shot normalize log (keep the normalization
  itself — it's the D71 fix).
- front.c: `d63MenuProbe`/After + call sites. blood_animation.c: D63
  g_GfxMemPos probe. ob.c: D69 probe (both obLoadBGFileBytesAtOffset
  versions) + its stdio/stdlib includes — **strip only after D69 is solved**.
- rsp.c, model.c, initanitable.c, language.c, title.c, ramromreplay.c,
  n64stubs.c, libultra.c, romdata.c: leftover TEMP blocks from D51–D66
  (decide per item; D60's DMA target validation is a permanent safety net and
  should stay).
- Scratch files at repo root: `run_*.log`, `d62mesg.log`, `all.txt`, `b3.c`,
  `btest*.c`, `buildtest.txt`, `err.txt`, `gcout.txt`, `preproc.txt`,
  `vsize.c/.exe`, `scratch-logs/` — delete (add to .gitignore if they recur).

## Standing procedure (per AGENTS.md)

- N64 build untouched; game **logic** unmodified — narrow exception for
  pointer-width/ABI-only edits under #ifdef PORT, each documented in
  PCPortResearch.md §F/D3x style.
- Verification ritual after any build-affecting change: undefined symbols,
  duplicates, syntax (`./build-pc.sh` is the final word).
- **Run the game from the repo root** (`cd REPO_ROOT &&
  ./build-pc/ge007.x86_64.exe`) — running from build-pc/ picks up the wrong
  ROM (baserom.u.z64) and misses the pcmodels.bin sidecar → spurious frame-4
  crash in modelPromoteNodeOffsetsToPointers.
- `Gfx.words.w0/w1` are both uintptr_t (16-byte Gfx on PC); ROM-copied N64
  Gfx DLs stay 8 bytes in memory — walk them at 8-byte stride when scanning.
- N64 CPU/RDP data in ROM is big-endian; any raw `*(u32*)` read of ROM-copied
  buffers needs a bswap under PORT (D68 pattern) or offline conversion.
- Heavy env-gated logging perturbs tick-driven timing (a full-triangle trace
  hung the kernel heartbeat at ~frame 107 while clean runs run fine past
  frame 2100) — use short targeted probe windows, and diagnose stability only
  with clean runs.

## Environment reminders

- MSYS2 tools: `export PATH="/c/msys64/mingw64/bin:$PATH"`. Build ~5 s.
- Crash log `ge007.crash.log` includes FAULT ADDR, STACK@RSP window, and
  module list — first stop for any fault. Symbolicate offline:
  `addr2line -e build-pc/ge007.x86_64.exe -f -C <pc>`.
- gdb is launch-mode only and far too slow for timing-dependent crashes —
  prefer env-gated probes + the crash log.
- Image base 0x140000000; DRAM V1 @ 0x70000000, V2 @ 0x80000000 (8 MB each);
  CART_BASE = 0x10000000; model sidecar extends the cart region at
  0x10C00000. `osGetCount` advances at ~46.5525 ticks/µs (D52).
- ROM: data/ge007.ntsc-final.z64 (byte-identical to the No-Intro good dump);
  sidecar: data/pcmodels-ntsc-final/ (gitignored, regenerate with
  tools_pc/d43_emit.py per region).
- Artifacts from this session: `ppm/` frames ~510–590 are the logo sequence
  (frame ~555 is the best front-facing "RARE" shot); `tools_pc/mtxtest.c` is a
  scratch matrix-convention harness (kept for reference). PPM files from
  timeout-killed runs can be truncated — validate header + pixel count before
  parsing.
