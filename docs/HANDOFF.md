# Handoff brief — GoldenEye 007 PC port (Phase 2: Session H — D69 BG/stan stage loading)

_Paste-ready briefing. Authoritative detail lives in `AGENTS.md` and
`docs/PCPortResearch.md` (§F/D59–D69 findings, §G status, §H procedure); this
is the summary + the immediate tasks._

## Your job

- **Session H:** two live threads, both verified reproducible:
  1. **D72.3 (intro logo pixels):** after D71 (logo byte order) + D72 (tc[]
     UVs), the Rareware logo still doesn't appear — all its triangles project
     off-screen, and a flat dark-blue checkerboard triangle (broken
     rasterization of something else) fills the lower screen instead. Tools
     are in place: `GE_PCDUMP` frame capture + `GE_DBGUV`/`GE_DBGTALL`
     traces in gfx_pc.cpp. Find which triangle draws the checkerboard, then
     check MP matrix + viewport at logo time (title.c
     load_display_rare_logo).
  2. **D69 (milestone):** make the first stage (BUNKER1) load and render —
     BG-file data is N64 big-endian; reverse-engineer GE's bg `.seg` +
     `Tbg_*_stanZ` formats from the decompiled consumers, then convert/fix
     for PC.
  Work agentically: decode → implement → build (~5 s) → verify → commit at
  each sub-milestone (document findings as D6x/D7x in §F).

## Current state (verified this session)

- **Build GREEN.** `export PATH="/c/msys64/mingw64/bin:$PATH" && ./build-pc.sh ntsc-final`.
- **D59–D68 all resolved** (details in PCPortResearch.md §F). Highlights:
  - **D59/D64** — the old "GL DLL" crash was an unbounded blood-RLE write
    (sentinel `die_blood_image_end` lost its N64 section adjacency on PC)
    clobbering the gun-barrel sub-DL. Sentinel now defined as one-past-the-
    end of the array under PORT.
  - **D65/D65b** — `enum HEADS`/`BODIES` sentinels: N64 semantics were signed
    (`HEAD_FIXED == -1`); PC GCC 16 picks unsigned underlying types, deleting
    the guard branches. Negative literals under PORT restore the bit pattern
    with signed semantics.
  - **D66** — `romCopyAligned` + ramrom replay did s32 pointer math; targets
    in `.bss` > 4 GiB truncated to wild addresses. 64-bit version under PORT,
    returns `void *`.
  - **D67** — `struct image_entry` re-laid (all-u32 bitfields, `dataoffset:24`
    first) so texLoad's raw word read + chrprop.c's 8-byte stride agree;
    sizeof == 8 on both targets.
  - **D68** — Globalimagetable BE→LE fixup (`port/src/gimgfixup.c`): bswap the
    IMAGESEG-marked Gfx w1 words (17 DLs) + `sImageTableEntry.index` (32
    tables, counts from D39 layout) after texReset's romCopy; marker scan in
    `texLoadFromDisplayList` moved to bytes 6..7 under PORT; compiled
    globalDL shadows synced after texLoad (`gimgSyncCompiledGlobalDLs`).
- **D70–D72 (intro logo pixels, latest session):** D70 env-gated PPM frame
  capture (`GE_PCDUMP` → `./ppm/`, gitignored); D71 RESOLVED — C-array
  texture sources (the four rarewarelogo.c RGBA16 images) were byte-swapped
  on LE PC → pink/green logo; port-layer per-source bswap normalizes them.
  D72 RESOLVED the UV path (GE always uses authored tc[] UVs; lookat defaults
  off) but **D72.3 OPEN**: all logo triangles project off-screen and a flat
  dark-blue checkerboard triangle rasterizes instead — see Your job #1.
- **Runtime:** boots → intro music → full intro (logo, gun barrel w/ Brosnan,
  cast screen) at ~59 fps → **crash in `load_bg_file` (bg.c:830)** on first
  stage load. EXCEPTION 0xc0000005, FAULT ADDR ≈ header buffer + 0x500000F.
- **Committed through D72** (see git log). TEMP diagnostics from the
  D51–D72 sessions are still in the tree — strip list in Task 3.

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
   §G status, and the environment reminders at the end of §H.
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

Env-gated probes to remove (grep `GE_D` + `TEMP D`):
- ob.c: D69 probe (both obLoadBGFileBytesAtOffset versions) + its stdio/stdlib
  includes.
- image.c / image_bank.c: D66 texLoad probe if still present.
- gfx_pc.cpp: D59 seg probes, D63 dram-branch/modelRenderNodeGundl/rspGfxTaskStart.
- gfx_pc.cpp: D70/D71/D72 TEMP — `GE_DBGUV`/`GE_DBGTALL` UV+triangle loggers
  (gfx_dbg_uv_enabled + the DBGTRI/DBGTALL blocks in gfx_sp_vertex/gfx_sp_tri1)
  and the `GE_D71LOG` normalize log. Keep the D71 normalization itself — it's
  the fix, not a probe.
- video.c + gfx_opengl.cpp: D70 `GE_PCDUMP` frame capture (videoEndFrame dump
  hook, gfx_opengl_pcdump_* implementations).
- blood_animation.c: D63 g_GfxMemPos probe.
- front.c: D63 menu probes (d63MenuProbe/After + call sites).
- rsp.c, model.c, initanitable.c, language.c, title.c, ramromreplay.c,
  n64stubs.c, libultra.c, romdata.c: leftover TEMP blocks from D51–D66
  (D51 vi-post logging, D54 music, D56 watch, D57 pool, D60/D61 DMA logging —
  decide per item whether the guard stays permanent; D60's target validation
  is a permanent safety net and should stay).
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
  ROM (baserom.u.z64) and misses the sidecar.
- `Gfx.words.w0/w1` are both uintptr_t (16-byte Gfx on PC); ROM-copied N64
  Gfx DLs stay 8 bytes in memory — walk them at 8-byte stride when scanning.
- N64 CPU/RDP data in ROM is big-endian; any raw `*(u32*)` read of ROM-copied
  buffers needs a bswap under PORT (D68 pattern) or offline conversion.

## Environment reminders

- MSYS2 tools: `export PATH="/c/msys64/mingw64/bin:$PATH"`. Build ~5 s.
- Crash log `ge007.crash.log` now includes FAULT ADDR, STACK@RSP window, and
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
