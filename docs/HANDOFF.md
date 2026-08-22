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
   8-byte-pointer structs. Fix it with a D37-style re-layout in `port/src/romdata.c`,
   then keep pushing rendering forward (more asset types will fault the same way).

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

## Current state (verified live this session, 2026-08-22)
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

## Immediate task 2 — D43: model-file loading ABI mismatch
1. Reproduce: run; after a few rendered frames it SIGSEGVs in
   `modelPromoteNodeOffsetsToPointers()` (model.c:5688) during stage object load via
   `load_object_fill_header()` (src/game/objecthandler_2.c:89). With D44 fixed, the
   crash log gives you the call chain for free.
2. Understand the file format from `bondtypes.h` (`ModelFileHeader`, `ModelNode`,
   `ModelRoData` union + per-opcode records) and `bondconstants.h`
   (`MODELNODE_OPCODE_*`). N64 layout: header 24 B, node 20 B, all pointers 4 bytes.
   **Do NOT design the re-layout from scratch:** PD port's
   `port/src/preprocess/filemodel.c` (1,114 lines) is a Rare-validated near-analogue —
   same `PROMOTE` idiom, **same vma 0x5000000**. Read it first; adapt its node walk /
   placement / record handling. GE's `ModelRoData` union + opcodes differ (PD has
   stargunfire/headspot/gundl types), so validate every GE record type per-field against
   bondtypes.h + ROM ground truth.
3. Implement the two-pass re-layout in `port/src/romdata.c`: walk the node tree
   (Child/Next/Parent links), place every sub-struct once (8-aligned) in a compact image,
   rewrite each embedded pointer as a zero-extended offset from the image start, so the
   existing `PROMOTE` rebase (`diff = fileramaddr - vma`, vma = 0x5000000) works
   unmodified. Hook it between the romCopy in `load_resource()` and first use (or at the
   `load_object_fill_header` call sites — choose the narrowest correct hook).
4. Validate one small model against ROM ground truth before generalizing (opcode
   sequence, node links, a few Data records). gdb works; or write the optional
   `tools_pc/romdump.py` inspector (below) and validate statically.
5. Expect follow-on faults in other asset types (BG/stan/propobj tables) — same
   procedure; note PD's `preprocess/` module already solves most of them
   (`filebg.c`, `filetiles.c`, `filepads.c`, `filesetup.c`, …) — see the PD
   reference section below.

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
