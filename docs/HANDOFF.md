# Handoff brief — GoldenEye 007 PC port (Phase 1.5 done, Phase 2: first frames render)

_Paste-ready briefing for the next agent session. The authoritative detail lives in
`AGENTS.md` and `docs/PCPortResearch.md` (§H handoff, §F/D31–D43 findings); this is
the summary + the immediate task._

## Your job
Boot is complete AND **real frames render** (`[NOTE] frame N rendered in … us`). The
current blocker is **D43**: model-file loading ABI mismatch — stage object load faults
in `modelPromoteNodeOffsetsToPointers()` (src/game/model.c:5688) because ROM model files
are serialized with N64 layout (4-byte pointer fields) but read on PC as 8-byte-pointer
structs. Fix it with a D37-style re-layout in `port/src/romdata.c`, then keep pushing
rendering forward (more asset types will fault the same way). Work agentically — fix →
build → verify under gdb → commit at each working milestone.

## Read first (authoritative, in order)
1. `AGENTS.md` — non-negotiables, critical files, build, verification ritual, phase status.
2. `docs/PCPortResearch.md` §H (handoff + plan), then **§F/D43** (the open model-file
   task) and **D37** (the libaudio bank-tree re-layout — the pattern to copy), D39–D42
   for the first-render fixes, D31–D38 for background.

## Non-negotiables (the ones that bite)
- N64 build files (`Makefile`, `tools/`, `rsp/`, `ld/`) — never touch.
- **Game logic is unmodified.** Hardware deps go in `port/`. Narrow exception: mechanical,
  semantics-preserving ABI/layout fixes forced by the 32→64-bit transition (embedded
  pointers → u32 + cast at use; PC-guarded pool sizing / idiom branches), each documented
  as a D3x finding. No behavior changes.
- Region macros in `CMakeLists.txt` must mirror the N64 Makefile exactly.

## Current state (verified this session)
- **ROM dump verified:** byte-identical to the No-Intro good dump, header CRCs re-compute
  clean (`tools_pc/romverify.c`). Fingerprint + provenance in Environment below.
- **Frames render.** The chain of fixes that got us here (all documented in §F):
  - **D31–D37:** boot chain — real-zlib file decompression, animation-table ABI+endianness,
    ANIM_DATA lvalues, music seq-table fixup, music-heap sizing, libaudio bank-tree
    re-layout (`romdataFixupAudioBank`).
  - **D38:** implicit function declarations truncated pointer returns on x86-64 →
    `port/include/pc_protos.h` prototype shim (398 decls; generator `scripts/gen_pcprotos.py`;
    anchored in `port/shim/PR/ucode.h`; C-only). Build is now warning-clean.
  - **D39:** Globalimagetable rebasing — `texReset()`'s `globalbank_rdram_offset + (u32)&sym`
    idiom needs the N64 `(u32)&sym == 0x02000000 + off` value; PC branch uses a per-symbol
    offset enum + `GIMG_OFF()` in `src/game/image_bank.c`. Segment end markers extended to
    the true 0x13F8 size (the CSV's `Globalimagetable.bin` entry is truncated by 0x930).
  - **D40:** `g_ModelHitEntries` BSS pool was N64-sized (12 KB for 600×20 B entries); the
    40-byte PC stride overflowed .bss and clobbered `is_ramrom_flag`. PC branch declares a
    `600 * sizeof(ModelHitEntry)` pool.
  - **D41:** WGL contexts are current on one thread only; the host main thread created +
    held the context while the game thread renders. `videoInit()` now calls
    `gfx_sdl_release_context()` after init; the game thread re-binds per frame. NOTE: this
    SDL2 build's `SDL_GL_MakeCurrent` return value is an inverted ABI artifact (0 = success,
    -1/255 = failure) — do NOT branch on it.
  - **D42:** rsp.c's XOR toggle of `g_gfxTaskSettingsList` truncated exe pointers to u32 →
    explicit toggle under `#if defined(__x86_64__)`.

## Immediate task — D43: model-file loading ABI mismatch
1. Reproduce: run; after a few rendered frames it SIGSEGVs in
   `modelPromoteNodeOffsetsToPointers()` (model.c:5688) during stage object load via
   `load_object_fill_header()` (src/game/objecthandler_2.c:89).
2. Understand the file format from `bondtypes.h` (`ModelFileHeader`, `ModelNode`,
   `ModelRoData` union + per-opcode records) and `bondconstants.h`
   (`MODELNODE_OPCODE_*`). N64 layout: header 24 B, node 20 B, all pointers 4 bytes.
3. Implement a D37-style two-pass re-layout in `port/src/romdata.c`: walk the node tree
   (Child/Next/Parent links), place every sub-struct once (8-aligned) in a compact image,
   rewrite each embedded pointer as a zero-extended offset from the image start, so the
   existing `PROMOTE` rebase (`diff = fileramaddr - vma`, vma = 0x5000000) works
   unmodified. Hook it between the romCopy in `load_resource()` and first use (or at the
   `load_object_fill_header` call sites — choose the narrowest correct hook).
4. Validate one small model against ROM ground truth under gdb before generalizing
   (opcode sequence, node links, a few Data records).
5. Expect follow-on faults in other asset types (BG/stan/propobj tables) — same procedure.
6. D40-class audit (done this session, negative): the dangerous pattern is a raw
   `char[N]`/`u8[N]` BSS pool sized for N64 that gets cast to a pointer-containing
   struct and written with PC stride. Only 3 raw byte arrays ≥1 KiB exist in game code
   (crash/debug buffers — not pools); the 362 `dword_CODE_bss_*` placeholders are
   mostly scalars or self-sizing typed arrays (e.g. bg.c `s_bound_info[204]`). Re-audit
   per asset type as loading progresses.

## Standing procedure (you will hit more of these)
Every ROM-serialized struct with a pointer field faults the same way once you reach more
asset loading. The D32 fix procedure (doc §H): at the fault run `ptype /o <Struct>`; if a
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
  changes). ROM is at `data/ge007.ntsc-final.z64` (= `baserom.u.z64`, byte-identical).
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
- `objdump -d --disassemble=<fn>` / `objdump -s -j .data` on build-pc/ge007.x86_64.exe is a
  fast way to check what the compiler actually emitted.
- Many init functions use a fake RBP — compute stack offsets from the **entry RSP**, not RBP.
- The D30 crash handler did **not** write `ge007.crash.log` for game-thread faults;
  attribute crashes via gdb (the in-process dump does print PC/registers to stdout).
- Python on this box: no f-strings with nested quotes (< 3.12) — use `%` formatting.

## Definition of done (this milestone)
Stage objects load without faulting and the rendered scene contains actual model geometry
(not just the clear color / background), stable under a 30 s+ soak. After that, Phase 2
makes it look right (fast3d CC/RM correctness vs `gmain.s`) and Phase 3 adds sound
(libaudio → SDL). Commit + push to `origin/master` at each working checkpoint using the
message style `PC port: <phase> — <what>`.
