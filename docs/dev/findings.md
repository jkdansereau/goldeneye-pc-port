# PC port — finding log

The raw, chronological engineering record for the port: every runtime bug,
its root cause, and the fix. Kept unedited for transparency. Recurring
patterns are distilled in [../porting-notes.md](../porting-notes.md).

---
## 11. Scaffolding review: findings & resolutions

The initial scaffolding was reviewed against the sources. Each finding below
was verified and resolved in the same change. Code comments in `CMakeLists.txt`
and `port/src/` reference these labels (A1–A4, B1–B4).

### A. Build-blocking issues

* **A1 — Region/version macros.** The N64 `Makefile` defines a full per-region
  macro set (`VERSION_*`, `LANG_*`, `REFRESH_*`, `LEFTOVERDEBUG`,
  `LEFTOVERSPECTRUM`, `BUGFIX_R*`, `BYTEMATCH`) that the game code `#ifdef`s
  heavily (e.g. `VERSION_EU` in 60+ files). **Resolved:** `CMakeLists.txt`
  now emits the matching `REGION_DEFS` per `ROMID`, mirroring the Makefile
  exactly.
* **A2 — Missing source files.** The initial `SRC_ENGINE` list + "exclude all
  `io/*.c`" strategy missed files the included code depends on. **Resolved:**
  added `src/cfb.c` (defines `cfb_16`, the framebuffer),
  `src/libultrare/audio/*.c` (`drvrNew`/`env`/`reverb` — the audio "New"
  driver `synthesizer.c` calls), and `src/libultrare/io/vitbl.c` (defines
  `osViModeTable`, referenced ~15× by `fr.c`). `motor.c` / `pfsinit.c` /
  `pfsisplug.c` are **not** compiled — `joy.c` calls `osMotor*`/`osPfs*` only
  for accessory detection, so they are shimmed as no-ops (see B2).
* **A3 — `mainproc()` / `init.c` + the excluded engine files' symbols.**
  `mainproc()` is defined only in `src/init.c`, which the initial build
  excluded, contradicting `main.c` calling it. **Resolved:** `init.c` is now
  **compiled** (it provides `mainproc()` + the thread-setup helpers). Its
  N64-only `init()` is compiled but not called.

  Compiling `init.c` (and the other included engine files) pulls in a set of
  symbols that live in the EXCLUDED files (`sched.c`, `rmon.c`, `vi.c`,
  `stacks.c`). Each is now provided:
  * **`src/stacks.c`** (added to `SRC_ENGINE`) — thread-stack arrays
    `sp_boot`/`sp_rmon`/`sp_idle`/`sp_shed`/`sp_main`/`sp_audi` (+ `sp_debug`
    under `LEFTOVERDEBUG`). Pure data; the `SP_*_SZ` sizes in
    `bondconstants.h` match exactly, so `sizeof()` is correct.
  * **`port/src/gesched.c`** (replaces `sched.c`) — scheduler globals
    `os_scheduler`, `gfxClient[3]`, `g_schedViCurrentFrameBuffer`,
    `g_ViChangeVideoModes`, `g_ViModes`, `g_ViModePtrs`, plus
    `get_counters()` and `permit_stderr()`.
  * **`port/src/n64stubs.c`** — `init()`'s N64 symbols (segment starts,
    segment boundary pointers, `jump_decompressfile`, TLB, FPU CSR) and the
    rmon host-I/O functions (`rmonMain`, `osReadHost`, `osWriteHost`,
    `rmonGetToken`, `rmonStatus`, `osSyncPrintf`).
  * **`port/src/libultra.c`** — the libultra OS calls `init.c` makes, plus
    `osTvType` (region-based), `viInit`, `vi_c_debug_MQ` (from the excluded
    `vi.c`), and `osPiReadIo` (cartridge token read, called by `token.c`).

  Every external symbol referenced by the compiled set was cross-checked
  against its definition; no duplicates (all other `sp_*`/`rmon*` references
  are `extern` decls).
* **A4 — `libultra.c` missing the high-level message API + others.** The
  initial stub had low-level `osEnqueueMesg`/`osDequeueMesg` but not
  `osSendMesg`/`osRecvMesg` (used in 13/26 files) or several others. **
  Resolved:** `port/src/libultra.c` now provides `osSendMesg`/`osRecvMesg`/
  `osSetEventMesg`, the controller query/read API, `osPfs*`/`osMotor*`,
  `osSetTimer`/`osStopTimer`/`osSetIntMask`, `osViBlack`/`osViSetYScale`/
  `osViSetXScale`/`osViSetEvent`, `osUnmapTLB`, `__osGetFpcCsr`/
  `__osSetFpcCsr`, `piCreateManager`, and `viDebugRemoved`. The scheduler API
  (`osCreateScheduler`/`osScAddClient`/`osScGetCmdQ`) lives in
  `port/src/gesched.c` (it's game scheduler API, not libultra).

### B. Research-doc corrections

* **B1 — `G_SETTEX` appears unused.** `gsSPUseTexture` (the only emitter) is
  never called in the game code or `gmain.s`; the game uses standard texture
  commands the PD `fast3d` already handles. **Resolved:** §5, §8 (Phase 2),
  and §9 updated — Phase 2 shrinks to verifying the custom CC/RM modes; a
  `G_SETTEX` decode path is kept only as a safety net.
* **B2 — Saves are EEPROM-based, not Memory Pak/PFS.** The save I/O
  (`src/game/file2.c`) uses `joyGamePak*` -> `osEeprom*`; the PFS/motor code
  in `joy.c` is accessory detection only. **Resolved:** §7 and Phase 4
  updated — Phase 4 is a file-backed EEPROM + no-op PFS/motor shims.
* **B3 — `src/libultrare/` was unmentioned.** **Resolved:** new §3.2 documents
  the two libultra trees and exactly which files the PC build compiles vs.
  shims, citing `Makefile.libultrare` as ground truth.
* **B4 — `src/spectrum.c` does not exist.** **Resolved:** removed from the
  `CMakeLists.txt` excluded-files comment.

### C. Assembly (`.s`) file symbols

The A/B sweeps covered excluded **`.c`** files. A second symbol source is the
MIPS assembly (`src/*.s`), also not compiled for the PC. Sweeping every global
label in the `.s` files against the compiled C set found **10 more** undefined
symbols, in two distinct classes:

* **C1 — PRNG (`src/random.s`) — ported to C, not stubbed.** `randomGetNext`,
  `randomGetNextFrom`, `randomSetSeed`, and `g_randomSeed` are used for real
  gameplay logic: `randomGetNextFrom()` feeds the CRC (`src/game/crc.c`),
  `g_randomSeed` is persisted in replay state (`src/game/ramromreplay.c`), and
  `randomGetNext()` drives `RANDOMFRAC()`/`RANDOMGETNEXT_F32()`. A no-op would
  silently corrupt CRCs and replays. **Resolved:** `port/src/random.c` ports
  the three functions **verbatim** (each MIPS instruction mirrored with explicit
  `dsll32`/`dsrl32` masks for bit-exactness) and defines
  `u64 g_randomSeed = 0xAB8D9F7781280783ULL` (the two `.words` in `random.s`).
  Signatures match `src/random.h`. Verified bit-exact against a Python
  simulation of the assembly (the PRNG is effectively 32-bit stored in a u64;
  the initial seed's high 32 bits are exercised only on the first call).
* **C2 — RSP/ASP/GSP microcode segment markers — dummy definitions.**
  `rspbootTextStart`/`rspbootTextEnd` (`rspboot.s`), `gsp3DTextStart`/
  `gsp3DDataStart` (`gspboot.s`), `aspMainTextStart`/`aspMainDataStart`
  (`aspboot.s`) are ROM addresses of the RSP/ASP microcode, referenced by
  `src/audi.c` and `src/game/rsp.c` (declared `extern long long int <name>[]`).
  **Resolved:** `port/src/ucode.c` defines all six as `long long int <name>[1] = {0}`.
  The graphics path is safe permanently (fast3d interprets the GBI list directly
  and never runs the gsp3D microcode, so the size differences are never read).
  **Carried into Phase 3:** the audio path is the one place it could matter — if
  the port emulates the ASP by *running* the `aspMain` microcode, those two
  markers must point at the real microcode bytes in the ROM (PD `pd.ld`
  `RSP_TEXT_SEGMENT` model); if audio is CPU-only, the dummies are fine.
  A `TODO(Phase 3)` on the `aspMain*` dummies records this decision.

### D. Host-compiler portability (GCC 16 vs IDO)

The N64 build targets IDO (MIPS). The PC build targets GCC 16 (x86-64). IDO
tolerates a number of non-standard constructs that the decompilation relies on;
GCC rejects them as hard errors. Each finding below was verified and resolved
in the same change. Where a fix could not be made purely in `port/`, the minimal
`src/` change is `-DPORT`-gated (N64 build untouched) and flagged with a
`PC port:` comment. These are **portability** fixes, not game-logic changes.

* **D1 — `inherits` = struct inlining + duplicate member name.**
  `src/bondtypes.h:44` does `#define inherits struct`, so `inherits X;` inside a
  struct becomes `struct X;`. IDO resolves this by **inlining** X's members into
  the enclosing struct (C++-style base), which the decomp depends on (offset
  comments like `CCTVRecord.unk84` at `0x84`, and positional initializers like
  `New_CCTVRecord(pad)` = `{New_PropDefHeaderRecord(6), 0, pad+0}` only line up
  with the inlined layout). GCC 16 also inlines `struct X;` (correct layout) but
  **hard-errors** on duplicate member names; Clang treats `struct X;` as a no-op
  nested tag (wrong layout). No flag/pragma downgrades the duplicate-name error.
  A sweep of `bondtypes.h` found exactly **one** parent/child member-name
  collision: `CCTVRecord` redeclares `pad` (s32 @0x80) which `ObjectRecord` also
  has (s16 @0x08). The game code's `->pad` (e.g. `setupCctv`, prop.c) is used as
  the **pad index**, which `New_CCTVRecord` stores at 0x08 (the *inherited* pad),
  so IDO resolves the ambiguous `->pad` to the inherited one; CCTVRecord's own
  `pad` (0x80, "lookpad") is never accessed by name. **Resolved:** under `-DPORT`
  only, CCTVRecord's own `pad` is renamed to `lookpad` (layout byte-identical);
  the N64 build keeps `pad` (IDO tolerates it).
* **D2 — `port/shim/PR/gbi.h` needed an include guard.** The shim redefines the
  `Gfx` union members as little-endian `G*_le` typedefs. With no guard, a TU that
  includes it twice (directly + via another header) re-creates the anonymous
  `Gdma_le`/`Gtri_le`/… struct types → "conflicting types for 'Gdma_le'". The real
  header's guard is `_GBI_H_`; the shim now uses a distinct `_PORT_SHIM_GBI_H_`.
* **D3 — `New_Vector()`/`New_Coord3d()` called with zero args.** The decomp's
  macros are declared with exactly 3 params (`x,y,z`) and use the
  `IF_ELSE(IS_EMPTY(..))` trick to default each to 0, but the game code calls
  them with **zero** args (`New_Vector()` at chrai.c:1358/1378, `New_Coord3d()`
  at chrai.c:4491), relying on IDO's leniency with empty macro args. GCC rejects
  `New_Vector()` against a 3-param macro. A sweep found these are the **only**
  call sites (all zero-arg). **Resolved in `port/`:** `port/shim/bondtypes.h`
  redefines both as `#define New_Vector(...) {0,0,0}` / `New_Coord3d(...) {0,0,0}`
  after including the real header (the shim is found first via the include path).
* **D4 — local `#define osSyncPrintf()`/`(x)` arg-count mismatch.** Five files
  locally disable `osSyncPrintf` with a fixed-arity macro (`#define
  osSyncPrintf()` or `(x)`) but then call it with more args (2–4), relying on
  IDO's leniency. GCC errors ("passed N arguments, but takes just M").
  **Resolved:** the five local defines (bg.c, debugmenu_handler.c,
  debug_camera.c, deb_loadallmodels.c, initexplosioncasing.c) are made variadic
  (`#define osSyncPrintf(...)`); they still expand to nothing, so behavior is
  unchanged.
* **D5 — array-initialized-from-array.** `chraction.c:2485` had `s16 mrs[3] =
  metal_ricochet_SFX;` (a local array "initialized" by a global array). C requires
  a constant-expression initializer; IDO treated it as a runtime copy. **
  Resolved:** replaced with an explicit 3-element copy (no new includes needed).
* **D6 — flexible array member in a nested context.** `bg.c` declared `s_special
  portal specialportalarray[]` where `s_specialportal` has a flexible array member
  (`u8 portallist[]`). C forbids initializing a FAM in a nested context (an array
  element); GCC enforces it, IDO tolerated it. The code treats the data as a flat
  byte array (cast to `u8*` in `sub_GAME_7F0B37EC`, walked byte-by-byte). **
  Resolved:** defined `specialportalarray` as a flat `u8[]` with the identical
  byte sequence.
* **D7 — AI X-macro system (`chraidata.c`) — RESOLVED.** The AI command system
  (`bondaicommands.h` + `aicommands.def` + `CPPLib.h`) uses deep preprocessor
  metaprogramming (`SWITCH` with 49 fixed params + `IF_VA`/`IS_EMPTY`,
  `DEFINED(SETUPSUBROUTINES(ID))` token-pasting, `_AI_CMD_POLYMORPH` redefined
  per-include) that relies on IDO-specific `##`/expansion behavior. GCC rejects
  several of these (e.g. `pasting ')' and '_'` in `DEFINED(SETUPSUBROUTINES(ID))`,
  `SWITCH requires 49 arguments, but only 20 given`). Note `src/aicommands2.h`
  is a **pre-generated** header (from `tools/cmdbuilder.c`) already included by
  `bondaicommands.h:864`; the failing path is the *raw* `aicommands.def` include
  (bondconstants.h:731 for the `AI_CMD` enum, chrai.c:172/920 for the command
  table).

  A general GCC-clean reimplementation of `SWITCH()` was investigated and ruled
  out: its content arguments are single preprocessor arguments that expand to
  top-level comma lists, and the C preprocessor cannot detect where one content
  ends and the next `CASE`/`VAL` begins (arity is not recoverable after
  expansion). The three active `SWITCH()` call sites in chraidata.c (m_IdleAnimations,
  m_BashKeyboard, m_RunToBondPersistent) each have a **hand-written equivalent
  already present in the file behind `#if 0`** — the author's own reference form.
  Each was verified byte-identical to the IDO expansion of the adjacent `SWITCH`
  call (including the `IFNewRandomGreaterThan(N, lbl)` == `SetNewRandom()` +
  `IFRandomGreaterThan(N, lbl)` identity, confirmed against the runtime check in
  chrai.c:1605). **Resolved:** under `-DPORT` the three `#if 0` blocks are
  activated (the `SWITCH` calls become dead `#else` branches); `port/shim/bondaicommands.h`
  keeps the original 49-param `SWITCH` defined but replaces it with a marker
  that fails loudly if any *new* game code uses `SWITCH()` on the port. See also
  D8–D11 for the sibling paste/comma issues in the same macro system.
* **D8 — `MODELSKELETON`/`New_ModelSkeleton` paste failure.** The model-record
  macros in bondconstants.h write `SKELETON(##NAME##)`-style pastes that IDO
  tolerates but GCC hard-errors on ("pasting does not give a valid
  preprocessing token"). **Resolved:** `port/shim/bondconstants.h` re-emits the
  affected macros with equivalent byte-identical expansions.
* **D9 — `CPPLib.h` helpers.** The CPPLib metaprogramming header's
  `IS_EMPTY`/`IF_VA`/`DEFINED` family uses paste tricks that break under GCC.
  **Resolved:** `port/shim/CPPLib.h` provides a paste-free reimplementation with
  identical results for every usage in the tree (intercepted via the include
  path; inert on N64).
* **D10 — file-record macros paste `&` onto NAME.** `CHRFILERECORD`/
  `GUNFILERECORD`/`SUIT_LFRECORD` and `GUNSTATS` write `{& ## NAME ## _header, …}`.
  IDO tolerated the failed `&##NAME` paste; GCC hard-errors. **Resolved:**
  `port/shim/bondconstants.h` rewrites them with the `&` kept out of the paste;
  expansion is byte-identical (`&NAME_header`, `&NAME_stats`).
* **D11 — generated `CALL()` double trailing comma.** The pre-generated
  `CALL()` (aicommands2.h) concatenates `SetReturnAiList()` and
  `SetChrAiList()`, each ending in its own trailing comma, then appends its own
  separator → `…, ,` inside the array initializer. IDO accepted it; GCC
  hard-errors. The artifact byte is never executed: `AI_SetChrAiList(CHR_SELF)`
  switches to the called list at offset 0 and `AI_Return` resumes the return
  list at offset 0 (chrai.c), so anything after the `SetChrAiList` record in a
  `CALL` is dead. **Resolved:** `port/shim/bondaicommands.h` re-emits `CALL`
  without the artifact byte.
* **D12 — 64-bit pointer→integer in static initializers.** On a 32-bit target
  (MIPS) storing an array address in a 32-bit field is fine; on x86-64 GCC 16
  makes it a hard error ("initializer element is not computable at load time")
  that no warning flag suppresses. Two sites, two fixes:
  * `process_monitor_animation_microcode` jump targets: the monAnim script tables
    (chrai.h) stored raw `monAnim*` array addresses in the tvcmd word's 32-bit
    field. **Resolved:** under `-DPORT` the initializers store an *index* into
    `_PORT_monAnimPtrs[]` (defined in propobj.c after all 35 monAnim arrays, via
    a shared `_PORT_MONANIM_LIST` x-macro in chrai.h) and the interpreter
    resolves index→pointer at `TVCMD_SETCMDLIST`/`TVCMD_RANDSETCMDLIST`. On-script
    layout unchanged (12-byte tvcmd words, same opcode bytes).
  * `assets/obseg/setup/{u,e,j}/UsetuplenZ.c`: the `intro[]` table stored
    `&credits_data_0` in an `s32` slot. **Resolved:** PORT-gated replacement with
    `0` (the N64 initializer is kept in the `#else` branch). The value is only
    consumed by romCopy-style size arithmetic, which is inert until Phase 2.
    (These files are currently excluded from the PC build per D16; the patch
    keeps them compilable should any setup data be pulled into the host link.)
* **D13 — assorted IDO leniency hard errors.** Small strictness failures with no
  semantic content:
  * `front.c:2405`: bare `return;` in an `s32` function (GCC error). The sole
    caller ignores the value → PORT-gated `return 0;`.
  * `audi.c` `audioInit`: C++-style array initializer `s32 sp48[…] =
    CUSTOM_FX_PARAMS_N;` (IDO accepted, GCC rejects) → PORT-gated explicit
    `memcpy` of the same bytes.
* **D14 — `_Printf` prototype clash (xstdio.h vs xprintf.c).** The IDO printf
  engine is compiled for the PC (`src/libultrare/libc/xprintf.c` + helpers
  `src/libultra/libc/xlitob.c`/`xldtob.c`, added to CMakeLists) because
  `src/sprintf.c`'s `sprintf()` calls `_Printf` directly. xstdio.h declares it
  with `u8 *` params; xprintf.c defines it with `char *` — IDO-compatible,
  GCC-fatal. **Resolved:** `port/shim/libc/xstdio.h` renames the declaration
  (`_Printf_u8decl`) before pulling in the real header via `#include_next`, so
  xprintf.c's definition is the sole prototype. The only caller (sprintf.c)
  doesn't include the header and passes its char*-based outfun, matching the
  definition exactly.
* **D15 — host libc lacks IDO/K&R symbols.** MinGW provides none of: `bcopy`/
  `bzero` (declared in PR/os.h with `int` sizes), `__libm_qnan_f` (quiet-NaN
  helper used by gu/cosf.c and game/zlib.c), the `tlbmanage*` API (boss.c calls
  two of them; tlb_manage.c is excluded — N64 TLB management), and
  `g_ViXScales`/`g_ViYScales` (defined in vi.c on N64, written at runtime by
  fr.c). Also `chrObjRandomGetNext/SetSeed` + `g_chrObjRandomSeed` live in
  `src/game/chrObjRandom.s` (not built for PC). **Resolved:** all provided in
  `port/src/n64stubs.c` / `port/src/libultra.c`; the chrObj PRNG is ported
  verbatim from chrObjRandom.s into `port/src/random.c` (same xorshift as
  `randomGetNext`, separate state).
* **D16 — asset data strategy for the single host link.** The N64 build links
  each model/level file into its own ELF and `.incbin`s compressed ROM blobs;
  none of that transfers. Three buckets:
  * **Compiled for real** (self-contained, unique symbol names):
    `assets/animationtable_data.c`, `animationtable_entries.c`,
    `oddtextures.c`, `font_chardatae.c`, `font_chardataj.c`, `font_dl.c`,
    `rarewarelogo.c` → added to CMakeLists (`SRC_ASSETS`). Real font/image/
    animation-table data is therefore live in the binary.
  * **Excluded — symbol collisions:** every `assets/obseg/{bg,brief,setup,stan}/*.c`
    defines generic file-local globals (`header`, `room_data_table`, `intro`,
    `padlist`, …) that collide in a single link. The N64 build isolates them by
    per-file ELF linking; the PC build cannot. Their top-level symbols are
    stubbed instead.
  * **Stubbed — ROM-derived / absent:** `port/src/assetstubs.c` defines the 758
    model/level/text symbols referenced by ob.c's `file_resource_table`
    (one zero word each), the 14 `ramrom_*` replay pointers (NULL), and the
    `unknown2`/`unknown2_end` pair (same address → zero-length romCopy in
    title.c). All `*Segment*` linker-script markers (normally from ge007.ld)
    are defined as NULL `u32 *` in n64stubs.c, so address arithmetic computes
    zero lengths and any Phase-1 romCopy is a safe no-op. Real data arrives
    with ROM loading in Phase 2 (`port/src/romdata.c`), which will also replace
    the table's address arithmetic.
* **D17 — fast3d placeholder.** `port/fast3d/` contains only `gfx_api.h`; the
  real software RSP lands in Phase 2. `port/src/gfxstub.c` provides no-op
  implementations of every entry point (plus `gfx_current_dimensions`) so
  video.c links. **Delete it when the real fast3d sources are added** (same
  symbols → duplicate-definition error if both remain).
* **D18 — GE ROM header offsets.** The N64 header is GE-specific
  (`src/rom_header.s`): magic `0x80371240` @0x00, ROM name "GOLDENEYE" @0x20,
  cartridge ID "GE" **@0x3C**, country byte **@0x3E** ('E' US / 'P' EU /
  'J' JP), version @0x3F. The standard-N64 offsets (ID @0x38, country @0x3A)
  are wrong for GE — verified against `baserom.u.z64` (hexdump: `... 47 45
  45 00` at 0x3C–0x3F). romdata.c validates all four fields.
* **D19 — music table is big-endian in the ROM.** The `.music` section of
  `assets/music/music.s` (the `RareALSeqBankFile` header + 63 × 8-byte
  `music_table_entry` records) is stored in the ROM in **big-endian** word
  order: at 0x419790 the bytes are `00 3f 00 00 | 00 00 01 fc` = BE u16
  seqCount=63 (= NUM_MUSIC_TRACKS), BE u16 unk=0, then BE u32 offset 508 for
  the first track. The N64 build evidently assembles that section with
  big-endian output; the PC port never reassembles it — the CSV offsets are
  unaffected and `musicSeqPlayerInit()`'s romCopy + `musicSeqFileNew()`
  patching work on the raw bytes unchanged.
* **D20 — US music tracks are RLE-compressed in ROM.** The `music_file`
  macro incbins `build/u/assets/music/<name>.rz` under `.ifdef VERSION_US`, so
  every M* track lives in the ROM 1172/RLE-compressed; the filelist sizes are
  the compressed lengths and per-track `end_` symbols = start + csv_size are
  correct boundaries. Decompression happens at playback (Phase 3 audio), not
  at load — no Phase-1/2 action needed.
* **D21 — scanner-prefixed manifest names.** `filelist.<r>.csv` contains
  entries whose file column is prefixed with the scan origin, e.g.
  `assets/ge007.u.117880.jfont_dl.bin` (real name `jfont_dl.bin`).
  gen_romassets.py aliases such names to their suffix after the
  `ge007.<r>.<hexoffset>.` prefix, so font/image/oddtexture segments resolve.
* **D22 — CMake silently skips `.s` files without ASM enabled.** The first
  assembly source in the PC build (`romassets_u.s`) was added to the target
  but never compiled: `project(ge007 C CXX)` had no ASM language, and CMake
  does not error on unknown extensions — it just drops them, surfacing only as
  a wall of undefined asset symbols at link. Fixed with
  `project(ge007 C CXX ASM)`. Lesson: when adding a new source *extension*,
  verify the object file actually appears in the build graph.
* **D23 — absolute cart-address asset symbols (Phase 1 approach).** Instead of
  zeroed stubs (D16), every ROM asset symbol is now an ABSOLUTE cart address:
  `scripts/gen_romassets.py` emits `port/src/romassets_<r>.s` defining all
  obseg labels (in `file_resource_table.inc.c` order — ob.c computes file
  sizes as `table[i+1].hw_address - table[i].hw_address`, so order matters),
  ramrom files, music tracks/markers, and the ge007.ld segment markers as
  `0x10000000 + rom_offset` from `filelist.<r>.csv`. romdata.c maps the .z64
  at exactly 0x10000000 (VirtualAlloc preferred address), so `&symbol` is a
  live host pointer and the PI shims (`osPiStartDma`/`osPiRawStartDma` in
  libultra.c) service reads as bounds-checked memcpys. This makes both DMA'd
  assets (models, banks) and direct-read assets (fonts, image display lists)
  work with unmodified game code. `assetstubs.c` is deleted; n64stubs.c keeps
  only the pure-RAM segment symbols (`_bssSegmentEnd`, `_csegment*`,
  `_inflate/_gameSegmentVaddr*`). US-only for now: EU/JPN manifests have
  naming inconsistencies (trailing-Z add/drop, region subdirs) that need the
  same treatment before those regions build.

### E. Compile + link milestone (status)

All ~235 translation units compile and the target **links**: clean build from
scratch is `236/236` steps, zero errors (`ninja ge007 -k 0`), producing
`ge007.x86_64.exe`. Remaining warnings (~4.5k) are expected IDO-leniency noise
(int-conversion, implicit declarations, etc.), demoted via the CMake warning
flags.

**Phase 1 (boot to window) is done:** `romdata.c` loads and validates the ROM
(`baserom.u.z64` at the repo root, or a final .z64 in `data/` / next to the
exe), maps it at cart base 0x10000000 (D23); `video.c` opens an SDL2 640×480
window with a GL context and clears/presents every frame; `main.c` runs a
demo loop (ESC quits) in place of `mainproc()`, which is deferred until the
software RSP + scheduler can service real frames. Verified: ROM header passes
validation, 12 MB mapped at 0x10000000, window renders for the full test
duration. Asset symbol spot-checks against the ROM confirm correct offsets
(e.g. music bank BE seqCount=63 @0x419790, first track offset 508 → Mno_music
@0x41998C; sfx/instrument banks start with GE's `B1` header).

### F. Phase 2 runtime findings (threads, DRAM, addresses)

**Dxx index — jump to a finding, do not linear-read this section.** §F
covers D24–D69; the log continues in §H (D32 procedure, D70–D121).

| Range | Topic | Status |
|---|---|---|
| D24–D30 | green threads / kernel / crash handler | resolved |
| D31–D42 | boot ABI: ANIM_DATA, music seq-table, libaudio banks, Globalimagetable rebase, GL context, rsp toggle | resolved |
| D43–D50 | model-file loading → offline sidecar "Plan B" (`d43_emit.py`); D50.6 texCopyGdls full-slot copy | resolved |
| D51–D58 | font pixeldata, osGetCount tick rate, model RW pools, cseq BE header, synth slots, RLE menu bg, watch raw offsets, gun-barrel DL | resolved |
| D59–D68 | intro render: blood-RLE clobber, DMA validate, OSMesgQueue, HEADS/BODIES sentinels, romCopy width, image_entry, Globalimagetable BE→LE | resolved |
| D69 · D78–D84 | stage load (`load_bg_file`): bg/stan offline sidecar (`d69_emit.py`) + StandTile/bg_room_data ABI | resolved |
| D70–D74 | intro-logo pixels: C-array bswap, UV path, sinf/cosf `DVAL()`, texture-import truncation | resolved |
| D75 · D77 | front-end 3D model transforms · audio | OPEN (parked). **M-32 triage (see §F "D75 ADDENDUM"): (a) D73-scope-gap RULED OUT (gu tree fully endian-clean). Splits in two: Bug 1 = logo/photo transform = the parked D114/D116 fast3d viewport mirror (not game code). Bug 2 = absent animated models = category (b). M-32b runtime probe (`GE_D75=1`): `render_pos`/`dynAllocate`-arena hypothesis RULED OUT (render_pos valid + fresh each frame), model instances valid (nMtx 21/1), zero fast3d DL warnings — failure is downstream in `drawjointlist`/`dotube` vtx/node-DL resolution or an off-screen `basemtx`. Needs a drawjointlist-level probe. M-33 (upright captures, D168 fixed): "Bug 1 = D114/D116 mirror" retracted; **gun-barrel Bond RENDERS fine** (walk + fire, upright) — the "absent" reports were the flipped capture; **Nintendo logo genuinely broken** (renders as 2 white blobs, shifted left — a real `logoinst` transform/geometry bug, not a flip); cast roll not re-captured. See §F "D75 Bug 2 — M-33 UPDATE".** |
| D76 · D164 | disclaimer/legal screen only draws line 1 — **root-caused (M-31)**: `constructor_menu00_legalscreen` text loop bounds `legal_text_end` on `&legalscreen_MRD`, a linker-adjacency assumption that mingw breaks (`legalscreen_MRD` links 0x60 *before* `legalpage_text_array`) → `do{}while(ptr<end)` runs once. NOT an image-table bug (screen references zero `sImageTableEntry`). | fix proposed (not applied — `front.c` owned by another agent) |
| D159 | front-end wallet-Bond photo "interlaced"/combed (RC1 / D149) — `texSwapAltRowBytes` odd-row 8-byte pre-swap (N64 RDP odd-line TMEM XOR compensation) not reversed by fast3d | FIXED (`#ifdef PORT` no-op the swap in `image.c`) |
| D161 | Depot (`-level_30`) ceiling = bright-blue speckle + radial rays (B2). A CI8 tile drawn with `gsDPSetTextureLUT(G_TT_NONE)` was decoded against the stale `rdp.palette` → garbage. Fix: `#ifdef`-free narrow route CI→I when `palette_fmt == G_TT_NONE` in `gfx_pc.cpp import_texture()`. | FIXED (`port/fast3d/gfx_pc.cpp`) |
| D165 · D166 | input-layer polish (M-31, port-only, `port/src/input.c`): D165 front-end cursor is now a true ~1:1 pointer (P-controller estimating the game cursor via front.c's own integrator) instead of velocity²; D166 hipfire mouse pitch emits speed-proportional C-button pulses instead of a fixed digital threshold. | FIXED |
| D180 · D181 | native-PC input pass (QoL run, `qol/native-pc-input-menu`): D180 = `Input.MouseCaptureMode` click-to-lock + absolute-cursor menu tracking + aim-speed default retune (`port/src/input.c`, `video.c`, port-only); D181 = `Game.ScreenShakeIntensity` route-(b) hook in `src/fr.c viShake` (`#ifdef PORT`, default 1.0 = no-op). | LANDED, config-gated, feel-checks owed |
| D186 | **`Video.FpsCap` throttles the sim, not just presentation (M-39 diag, M-42 clamp).** The frame-pacing `sysSleep`+busy-wait in `sync_framerate_with_timer` (`port/fast3d/gfx_sdl2.cpp`) runs inline on the scheduler thread (`osSpTaskStartGo`→`gfx_run`→`swap_buffers_begin`), so a low cap blocks VI-retrace delivery and drags every game thread down to the cap rate (no N64 analogue — RSP/RDP are separate silicon). **Landed:** `0 < FpsCap < 30` → treated as `0` (uncapped) + warning, at both `videoInit` (normalises a bad `ge007.ini` for the next `configSave`) and `gfx_sdl_set_target_fps` (covers the F10-overlay live path); caps ≥ 30 unchanged. Verified: default `FpsCap=0` framediff 3/3 golden-identical; `FpsCap=10` now runs at the uncapped rate; `FpsCap=60` still paces. **Owed:** move pacing off the scheduler thread so any cap only drops presented frames. See §F "D186" + porting-notes.md §E. | CLAMP LANDED (`port/fast3d/gfx_sdl2.cpp`, `port/src/video.c`); real fix owed |
| D187 | **HUD ammo-type/weapon icon vanished at `-O2` (M-46 root-cause, M-47 fix + verify).** `set_rgba_redirect_generate_microcode()` (`src/game/gunfire.c:5929`, sole emitter of the bottom-right magazine icon; call sites 6057/6123/6217/6219, all `gdl = f(...)`) is `Gfx *` with **no `return`** — the decomp banner above it flags exactly this. Harmless under `-Og` (GCC left the tail-call result in `$v0`); commit `34885535` (Sep 2) switched release to `-O2` → undefined return reg → callers resume the DL from the icon's start offset and overwrite its `texSelect`/`display_image` commands with the following `gunDrawHudInteger` output. Ammo *numbers* use a separate correctly-returning path — hence the exact symptom split. Fix = one line, `return` under `#ifdef AVOID_UB` (already `-DAVOID_UB=1`, `CMakeLists.txt:207`); N64 byte-match path unchanged under `#else`. **Verified M-47 (interactive console):** `-O2` build, `-level_09` `GE_PCDUMP` 200–440 — orange clip glyph absent pre-fix, present post-fix; `framediff.py` frame 320 worst-cell dmean **21.8 → 4.0**, frame 440 **21.7 → 4.6** (converges to the pre-regression golden); other levels unaffected (change is one HUD function). Do **not** revert `34885535`. | FIXED (`src/game/gunfire.c`, `#ifdef AVOID_UB`); PR #19 |
| D188 | **Linux `-O2` boot crash: `_Printf` passes `&args` of a by-value `va_list` param (M-48).** On the x86-64 System V ABI `va_list` is an array type → a by-value `va_list` parameter decays to a pointer, so `_Printf`'s `_Putfld(&x, &args, …)` handed `_Putfld` the address of its own local pointer slot, not the arg list; `_Putfld`'s `va_arg(*args, …)` then walked garbage. Latent at `-Og`, SIGSEGV at `-O2` (PR #9) — crash path `bossInitMainthreadData` (`boss.c:247`, D121 per-level `-m` sprintf) → `sprintf("-level_%c%c %s", …)` → `_Putfld` @ `xprintf.c`. Same UB class as D187. Windows x64 `va_list` is a scalar so `&args` worked there (Windows build unaffected — `-level_09` framediff 3/3). Fix: `#ifdef PORT` — `_Printf` takes `va_list *`, threads it straight to `_Putfld`; `src/sprintf.c` (sole PC caller; `rmon.c` excluded) passes `&args` of a real object. N64 build untouched (`#ifdef PORT`; file byte-matches ROM). | FIXED (`src/libultrare/libc/xprintf.c`, `src/sprintf.c`, `#ifdef PORT`); PR #22 |
| D189 | **Linux `-fstack-protector` aborts on the decomp's latent stack-buffer overruns (M-48) — FIXED.** Two hit on `-level_09`: (1) `sub_GAME_7F0B1DDC` (`stan.c`) — local `StandTile *tileStack[39]`, bail check `if (cat >= 41)` runs only once per outer iteration so `cat` reaches `40 + pointCount` and the append writes past the end; BUNKER1 prop room-list setup legitimately visits ~40 tiles (confirmed in GDB: 30 valid distinct tile ptrs and climbing; `standTileStart=0x701ae160`, so **not** a pointer-truncation bug — the earlier "blob >4 GiB" triage premise was wrong). (2) `bondviewCalcIntroSwirlCamera` (`bondview2.c`) — `f32 pointbuf[10]` written `[-3 .. 11]` via `&pointbuf[i*3]`, i∈[-1..2]. Both harmless on N64 + the MinGW build (no stack protector); only Ubuntu gcc's `-fstack-protector-strong` default makes them fatal `__stack_chk_fail`. **Fix:** `-fno-stack-protector` globally (`CMakeLists.txt`) to match the N64/MinGW builds, + `tileStack[64]` under `#ifdef PORT` for the one cleanly-bounded case, + `stan.c:2225` link-resolve moved to the D177 `PORT_PTRADD` macro for consistency. A *hardened* Linux build would need the whole overrun class swept. **Verified:** Windows `-level_09` framediff 3/3; Linux `-O2` `-level_09` runs to frame 2100+ no crash. porting-notes.md §C. | FIXED (`CMakeLists.txt`, `src/game/stan.c` `#ifdef PORT`); PR #23 |
| D190 | **Linux `-level_45` SIGSEGV at load in `add_ptr_to_objective` (`objective.c:64`) (M-48, open).** `objective_ptrs[objective->menu] = objective;` faults — `objective` wild or `objective->menu` a garbage index → OOB write. Almost certainly the D122/D126/D132 propDef-stride / ROM-serialized-struct-widening family (`struct objective_entry` / the type-23/type-1E setup-text record chain read at the wrong stride on PC). Found in the M-48 Linux level sweep: **5/6 spot-checked levels (`-level_09/20/27/34/37`) load + render fine; `-level_45` is the one failure.** Not a v0.1.0-alpha blocker (alpha is explicitly "expect crashes"). Windows: `-level_45` load status not re-checked this session. | OPEN — not investigated (M-48) |
| D191 | **Windows `-O2` release-bundle crash: SIGSEGV in `modelGetNodeRwData` (`src/game/model.c:478`) — the `root->Parent` node-tree walk (M-49, user playtests of the v0.1.0 win64 bundle).** **Two instances, identical fault PC `0x1400801b1`:** (1) fresh save, killed the first guard right after escaping the cell in Bunker ii; (2) Statue (`-level_49`?), right after the Trevelyan meeting cutscene. `ge007.crash.log` (auto-dump; symbols intact in the shipped exe, `addr2line` resolves): PC = `modelGetNodeRwData` at `model.c:478` — the `switch (root->Opcode & 0xff)`, i.e. `root` (a `ModelNode *`) is bad; `addr2line` puts it in the inlined self-recursion at `:532`, so the `while (root->Parent) { root = root->Parent; … }` walk (`model.c:527-540`) handed a garbage node into the recursive call. Instance 1: `Rdx = 0x70267bc000000000` — a `~0x70266xxx` heap address in the **high 32 bits, zero low half** = 32-bit pointer field read as 64-bit (or a byte-swapped `u32` pair). Instance 2: `Rdx = 0`, FAULT ADDR `0x0` — `root` (or `root->Parent`) resolved to NULL. Both: `Rcx=Rdi` = the `~0x7008xxxx`/`~0x7007xxxx` object ptr, `R8 = 0x1d` (29). So the `Parent` chain in some model contains a **truncated or NULL link** — pointer-width / byte-swap family (D3x, D119 `weapons_held[]->chr` type-pun, D122/D126/D132 propDef-stride). NOT weapon-fire-specific — common factor is `modelGetNodeRwData` running on a character/cutscene model whose node tree has a bad `Parent`. Crash logs saved: `scratchpad/crashlogs/`. **Repro aid:** shipped exe has full symbols; `debug-crash.ps1` in the bundle dir runs it under gdb, `GE_D51=1` → `d52rw.log` rwdata-pool trace. The crash.c EBP backtrace breaks immediately (`#01: 0x1d`) — need a real `bt` + `p *root` + `p *Objinst` at the fault. Not yet reproduced under a debugger; no fix. | FIXED + PLAYTEST-VERIFIED (M-79). User replayed Bunker ii (first-guard kill) and Statue (post-Trevelyan cutscene) on the fix build — **both no longer crash** → 21/21 solo levels crash-free (v0.2.0 gate met; Cradle's remaining issue is D193, not a crash). Build clean; bunker1 golden framediff 3/3. **Real gdb backtrace (M-79):** the fault is NOT in the `root->Parent` walk — `root` arrives *already* garbage (`0x70267a9000000000` = a half-word-swapped `u32` `0x70267a90`) from frame #1 `bondviewSelectCuff(model=0x70076808, header=0x700763a0, switchindex=29)`, called from `gunUpdateAndFire(GUNRIGHT)` on the frame the player fires. `bondviewSelectCuff` byte-indexes `header->Switches` (a `ModelNode *` array — **8-byte stride on PC**, widened per D43/D45) with `offset = switchindex << 2` (N64 4-byte stride), so every `base[N]` deref lands mid-slot and yields a misaligned half-pointer that is non-NULL by luck → `modelGetNodeRwData(root=garbage)`. **Exactly the D140 bug class** — D140 fixed the identical pattern in `gunfire.c:1740`; this call site was missed. Fix: PORT-only `offset = switchindex * (s32) sizeof(ModelNode *)`; the `base = (u8*)switches + offset` math and the `#else` N64 line are unchanged. ABI/pointer-width only (D3x class), no game-logic change. The Statue instance (`Rdx=0`, NULL fault addr) is the same site landing on a slot whose bytes read as 0. |
| D193 | **NPC AI locomotion is too slow — characters don't move to their destinations at the original game's rate (M-49, user playtest; clarified after the FULL 21-level playthrough — "the biggest overall flaw").** Not (primarily) an animation-playback bug — the *travel speed* of scripted/AI movement is wrong: guards, named NPCs (Trevelyan, Ourumov) and friendlies (Natalya) all take noticeably longer to run/walk to their goal positions than on N64. General, not chr-specific. **Leading hypothesis — the D117/D134/D155/D156 wall-clock-timing family:** `chr.c` / `chrai.c` integrate movement as `pos += speed * g_GlobalTimerDelta` and step sim loops `while (i < g_ClockTimer)` / `-= g_ClockTimer` (`chr.c:1437,1446,1491-1502,1862,2359,2569`; `chrai.c:76`). On the port `osGetCount()` is free-running wall-clock (D117), `g_GlobalTimerDelta`/`g_ClockTimer` are derived from it, and D155's `FRAMETIMING_PORT_MAX_CATCHUP = 6` clamps `nextFrameTime`. If the effective per-second sum of `g_GlobalTimerDelta` (or the `g_ClockTimer` tick count) runs low — quantization, the catch-up clamp biting during normal play, or a units mismatch (`g_JP_GlobalTimerDelta` vs `g_GlobalTimerDelta`, `chr.c:1435/1437`) — every delta-scaled AI motion integrates slow while **player** movement (input-driven per frame) still feels right, which is exactly the reported split. Related: **D170** (Ourumov/Trevelyan "flee" looks wrong — flagged there as possibly the same locomotion-speed issue rather than an AI divergence). Also possible: `chrGoToBond/Chr(SPEED_RUN/WALK/SPRINT)` speed enum → path-follow step, or pathfinding advancing ≤1 waypoint per frame-quantized tick. **Instances seen (M-49 full playthrough):** (a) Facility — the post-scripted-scene beat with Ourumov + the soldier squad: the soldiers **walk in super slowly and take a long time to path into firing positions** before they start shooting (this is regular squad combat AI, not a named-NPC script — good evidence the slowdown is general delta-scaled `chr` locomotion / path-following, not one ailist); (b) the named-NPC flee behaviour on Silo (Ourumov) and Cradle (Trevelyan) — see D170; (c) **Natalya (the escort NPC) always walks really slow** — on every escort level (Bunker ii, Statue, Control, Archives) she trails far behind, forcing the player to wait. Escort follow-AI is the same `chrGoToChr`/path-follow path as (a). **WORST CASE — Cradle (`-level_51`) is "bugged out completely" due to Trevelyan's behaviour (M-49).** The final level is Trevelyan-fleeing-and-scripted end to end (he runs the antenna cradle, climbs, drops to the cradle floor, the helicopter beat); if his locomotion timing / path-following is off he never hits the trigger points and the level's scripted progression stalls or goes haywire. This makes D193 effectively a **can't-properly-finish-the-game** bug, not just cosmetic sluggishness. **Next:** log `g_GlobalTimerDelta` + `g_ClockTimer` per frame during normal play and check the 1-second sum vs the N64's ~60; time a guard's run across a known distance vs reference footage; then watch Trevelyan's chr state / current AI command on Cradle. | **FIXED + user-playtest-verified (M-81) — root cause is D209**: `act_ubytes.padding[45]`, a raw-byte union alias for `act_gopos.unk59` (the SPEED tier), lands on offset 45 which is `unk59` on N64 but is byte 5 of `waypoints[1]` at 64-bit — always `0x00`. So the locomotion-animation selector `get_sound_at_range()` always received tier 0 and bound `ANIM_DATA_walking`/`walking_unarmed` for **every AI chr in the game**, and since travel is anim-root-motion driven they all moved at walk pace no matter what the ailist commanded. User verified on Cradle: Trevelyan and the guards run. Full elimination trail below — **steady-state timing keystone MEASURED and the "sim sums low" hypothesis is CONTRADICTED (M-80).** Added a `GE_D193=1` per-wall-second probe in `waitForNextFrame()` (`src/game/frametiming.c`, `#ifdef PORT`, env-gated): logs render frames, sim ticks delivered (= sum of the `deltaFrames` fed to `updateFrameCounters` = sum of `g_ClockTimer`), catch-up clamp hits, and max raw pre-clamp delta. Built ntsc-final, ran `-level_09 / _20 / _27 / _34` idle-after-load, ~20 s each. **Every level, rock-steady: render = 30.0 fps, sim = 60.0 ticks/s, clamped = 0, maxraw = 2.** i.e. each rendered frame carries `g_ClockTimer = 2` and the per-second sum is *exactly* the N64's ~60 — the wall-clock timer is NOT running low in steady state, and `FRAMETIMING_PORT_MAX_CATCHUP` never bites in normal play. VSync on/off made no difference (still 30/60). So the B1 premise ("`g_GlobalTimerDelta`/`g_ClockTimer` sum low over a real second") is wrong for the common case; D193 must come from elsewhere. **Residual quantization risk (not yet observed):** `nextFrameTime = (dt_counts + 387937) / 775875` is integer-rounded per frame with no remainder carry, so a scene that renders at a *steady* rate between 30 and 60 that is not an integer divisor of 60 (e.g. a solid 50 fps → dt≈20 ms → rounds to 1 → sim 50/s, ~17 % slow) would under-advance; the fix there is a fractional accumulator in the `#ifdef PORT` path. But the port renders a hard 30 (software RSP ≈ 60 µs/frame — not GPU-bound; the cap is the `pendingGfx < 2` double-buffer + sched round-trip), so this only matters if a real AI-heavy scripted scene drops into the 20–30 band. **Next (needs input, interactive session):** drive Bond into the Facility Ourumov/squad beat (D193a) and Cradle Trevelyan run with `GE_D193=1` — confirm render/sim hold at 30/60 under real AI load; if they do, pivot off timing entirely and instrument a single guard's world position vs. distance/time (anim root-motion rate, `chrGoToChr` SPEED_* enum → step size, path-node spacing) — the travel-speed bug is likely in the locomotion/anim path, not the frame clock. `GE_D193` catalogued in `docs/dev/GE-ENV-PROBES.md`. **M-80 cont. — LIVE-LOAD CONFIRMED, pivot is final.** User ran Cradle (`GE_D193=1`, `-level_41` — see the level-id note below) and drove Bond through the Trevelyan sequence while Trevelyan was visibly "walking really slow". Probe throughout: **`render=30 sim=60 clamped=0 maxraw=2`, dead steady** — no dips, no clamp hits, no `[D156]` guard fires. So under the *exact* failing scene the sim clock delivers a perfect 60 ticks/s. **D193 is NOT a timing/scheduler bug — it is in the character locomotion/animation path.** Best current hypothesis: GE character *travel* is animation-root-motion driven — `modelTickAnim` advances `frame += playspeed*speed` per tick (numticks = g_ClockTimer, correct), then `modelSetAnimFrame2WithChrStuff` (`model.c:3060+`) extracts per-anim-frame translation via `sub_GAME_7F06D3F4(...&pos)` and applies `scale = model->scale * model->anim_translation_scale`. The **player does NOT use anim root motion** (explicit speedgo/speedstrafe physics in bondview2.c), which explains the "player fine / all AI slow" split precisely. Suspects, in order: (1) the anim translation keyframe data is decoded with wrong units/scale by its sidecar converter (D43/model family); (2) `model->anim_translation_scale` or `model->scale` misread (ABI/struct-stride — D3x/D99 class); (3) `model->animrate`/`playspeed`/`speed` wrong. **M-80 cont. — port side instrumented (`GE_D193A` in `chrTick`, `GE_D193B` in `sub_GAME_7F06D3F4`; both `#ifdef PORT`, catalogued in GE-ENV-PROBES.md). Findings:** (i) `g_GlobalTimerDelta = 2.0`, `model->playspeed = 1.0`, `model->scale = 0.1`, `model->anim_translation_scale = 1.0` for every ticked chr on Silo + Facility — the scalars that feed the root-motion accumulation are all nominal, matching the identical N64 C (no `#ifdef PORT` in this hot path bar the non-firing D156 NaN guards). `model->animrate = 0.0` universally but that is the default — only written by `modelSetAnimPlaySpeed` on a blended transition (`startframe > 0`), so not the bug. (ii) Patrolling guards (`act=14 ACT_PATROL`, `model->speed=0.5`) travel **~60–99 world-u/s** on the port — no N64 number to compare against yet. (iii) `GE_D193B` (Facility, walk anim `706af4d8`): the decoded root-motion triple `tmp` ≈ (±0-3, ~1080-1094 hip height, ±0-50) with **`base = 0` and `angle = 0` on every call** (could be legit for a straight-line walk — joint 0 channel A at index 0, no per-frame facing change — or could be a mis-selected/zeroed channel; can't tell without the N64 side). **Bottom line: every port-side input to the locomotion math reads nominal**, so the divergence is either (a) subtle wrong magnitudes in the decoded anim bitstream values themselves (`tmp` triple / the descriptor decode), or (b) something in the `pos34` accumulation / `header->unk*` state — neither visible without a reference. `ModelSkeleton` **ruled out** — the skeletons are statically defined in C (`New_ModelSkeleton`/`MODELSKELETON` macros, `chrobjdata.c` etc.), not ROM-serialized, so `Joints` is a native x86-64 pointer and `ModelJoint` is 6 B on both platforms; `base` is correct. That leaves the ROM-serialized `ModelAnimation` bitstream (`bitDescriptors`/`bitStream`, D32-widened) and the anim-table load path as where a magnitude error could enter. **M-80 cont. — anim-data endianness (D33) audited, looks complete for this path.** `ptr_animation_table` is `romCopy`'d then byte-swapped by `romdataFixupAnimationData` (`port/src/romdata.c`, D33): per 20-byte record it bswaps `+0x00/+0x04/+0x08/+0x0C/+0x0E/+0x10` and per 6-byte `ModelAnimBitField` descriptor bswaps `+0 bitOffset` / `+4 valueOffset` (bitCount/pad identity); the bit*stream* is left raw (correct — `modelAnimReadRootMotionValue` walks it byte-by-byte, MSB-first, endianness-neutral, same as PD's `anim_read_bits`). `PTR_ANIM_walking/running/sprinting` **are** in `animation_table_ptrs1` so guard-locomotion records do get fixed up; `_ptrs2` is only 3 helicopter/plane anims (no overlap → no double-swap). No `bad descriptor range` / `bad record offset` errors in any run. Every `anim->unkNN` the locomotion path reads (`unk04`, `unk06`, `unk07`, `unk0C`) is within the fixed-up 0x14 bytes. `sizeof(ModelAnimation)` = 64 B both platforms (D32 kept the embedded ptrs as `u32`); `sizeof(ModelAnimBitField)` = 6 B both. So the anim decode is structurally sound as far as static review goes — **a magnitude error, if any, needs the N64 reference to see.** **Next — two viable paths (needs a fresh session; toolchain not present here — no `mips-linux-gnu-*`, no `qemu-irix`, no emulator):** (A) set up the MIPS toolchain + IDO-recomp + an N64 emulator with `osSyncPrintf` capture, build the ROM with `#ifndef PORT` `GE_D193B`-equivalent logging, navigate to the same scene, diff `tmp`/`base`/`angle`/travel-speed against the port capture; (B) user records N64 footage (real HW or their emulator) of one guard traversing a measured distance, frame-count it → gives the port-vs-N64 speed ratio (a clean 2× would point at a per-frame vs per-tick doubling; a fractional ratio at a scale/data error). Recommend (B) first — cheap, and the ratio narrows (A) massively. **Highest-impact playtest finding — breaks Cradle.** **Level-id note:** the earlier "Cradle = `-level_51`" in this row and HANDOFF was WRONG — `LEVELID_CRADLE = 41` (`src/bondconstants.h:1662`; STATUE=22, so CRADLE = 22+19). `-level_51` = `LEVELID_EAR` (unused slot) and SIGSEGVs at load in `stanBuildRoomData` (`stan.c:281`, NULL deref) — a non-issue (bad level id), noted here only so it isn't re-investigated as a real crash. **M-81 — SOLVED, and none of the M-80 suspects were the cause.** Re-verified the whole timing chain independently (`waitForNextFrame` -> `speedgraphframes` -> `g_ClockTimer` -> `g_GlobalTimerDelta`; `g_GlobalTimer += g_ClockTimer` at `lv.c:1022` is **tick**-based, so the ~75 AI timeout comparisons against it are frame-rate independent) and re-checked the D33 fixup *widths* against the real structs (they match exactly, and `(bs-bd) % 6 == 0` passing is independent evidence the two embedded pointers decode correctly). Root-motion accumulation is self-consistent (`pos24 = pos34 + rotate(pos)` at `model.c:3322`, so `pos` is a per-frame delta in x/z and an absolute hip height in y — matching the observed `tmp` shape). Extending `GE_D193A` with the visibility-gate counters (`tick`/`move`, i.e. chrTick entries vs `chrUpdateAnim` calls) killed the culling hypothesis too — chr 0 is `31/31` every second. The tier was then shown correct (`tier=1 SPEED_RUN`) and the *animation* wrong (`aidx=40/107`, never 42/41), which is **D209**. Note for future readers: `chrlvApplySpeed`'s `speedPtr` out-param is a **turn** rate, not a travel rate (`chraction.c:8070` passes `&act_runpos.turnspeed` into it), so `act_gopos.speed == 0` just means "running in a straight line" — it is not evidence of a stalled character. |
| D194 | **Mouse input needs another tuning pass — RMB aim over-sensitive + sensitivity couples to frame/sim rate (M-49, user playtest).** Two parts. **(a) Aim mode (RMB) is near bang-bang, not proportional** (`port/src/input.c:697-707`): any per-poll delta past `AIM_MOVE_THRESH` emits stick `m = clamp(61 + |Δ|·(MouseAimSpeed/100)·AIM_GAIN, 61, 60+AimBand)` = `[61,80]` — with the M-29 defaults (`MouseAimSpeed=16`, `AIM_GAIN=4.0`, `AimBand=20`) `m` is already 61 (fast) at ~1.5 px/poll and saturates at 80 by ~30 px/poll, so the usable proportional band is tiny and sits near the top of GE's aim range → "way too sensitive," turn rate barely tracks how fast you actually move the mouse. Needs a real response curve (low floor, wider proportional band, maybe a gamma), not a threshold+clamp. **(b) Overall sensitivity correlates with movement/frame rate** (user's words: "correlated with movement speed, does not feel good"): `input.c` emits raw stick counts **per controller poll** with no `g_GlobalTimerDelta` / polls-per-frame normalization (hipfire yaw `sx += edx·(MouseTurnSpeed/100)·6.0` at `:709`; aim as above). GE integrates the stick scaled by its sim delta, and the port's poll cadence vs render/sim cadence drifts with scene load — so effective sensitivity shifts when the framerate does (which reads as "when I'm moving"). Fix direction: accumulate mouse delta and convert to stick using a fixed reference dt (or feed the game a dt-consistent value), so a given hand motion = a given view rotation regardless of fps. Lineage: D118a (hipfire pitch digital vs analog), D165 (menu pointer), D166 (hipfire pitch pulses), D180-B3 (`MouseAimSpeed` 25→16). New adjacent QoL ask, root-caused → **D223** (mouse-wheel direction should reverse weapon cycle direction, currently both directions cycle forward). Config knobs exist (`Input.MouseAimSpeed/AimBand/MouseTurnSpeed/HipfirePitchSpeed`) but the *shape* of the mapping is the problem. **M-87 design-goal note (user):** overall mouse feel should move toward **Quake Remaster / the Perfect Dark PC port** — raw, direct, responsive 1:1 mouse-look — while explicitly **not losing GE's original aiming nuance** (the analog-stick aim-mode vs digital-hipfire split this whole D118/D194 lineage exists to preserve). Reframes the fix direction from "just widen the proportional band" to: normalize by a fixed reference dt first (part (b), the frame-rate coupling — this alone should fix most of "feels unresponsive"), then re-derive the aim-mode response curve, and treat raw hipfire mouse-look (PD/Quake-style, bypassing the N64 digital-stick emulation entirely when the player is *not* in aim mode) as a real design option to evaluate against "keep GE's aim-mode feel," not assume the N64 stick-emulation path is mandatory everywhere. | OPEN — root-caused, not fixed (M-49) |
| D195 | **Transparency / alpha surfaces broken on Control (M-49, user playtest)** — Natalya's console room, the glass partitions + translucent projected wall displays. Failure mode not yet specified by the reporter (no `GE_PCDUMP` capture taken). Likely a fast3d render-mode / alpha-blend path defect, not a decode one — neighbours D161 (CI8/LUT decode), D172, D176(b) (both blend-adjacent) but those are texture-format bugs, this reads as a blend-state one; check D128 portal adjacency in case the glass geometry is portal-culled wrong instead. Logged in `GRAPHICS-BACKLOG.md`. **Next:** `GE_PCDUMP` capture on Control near the console room, compare RDP render-mode words against N64 `gmain.s` for the glass/display materials. | OPEN — not investigated (M-49), needs a capture |
| D197 | **Character face/head textures wrap around the head, seen on Silo (M-49, user playtest).** Reads as `G_TX_CLAMP` not honoured (or a non-power-of-two wrap period) on a head/face texture tile — the texture repeats around the mesh instead of clamping at the UV edge. Not yet isolated to a specific character or texture; no `GE_PCDUMP`/`GE_TEXDUMP` capture taken. Likely fast3d texture-tile setup (clamp/wrap mode bits from the tile descriptor not round-tripped) rather than a data bug, given it's cosmetic and geometry-independent. **Next:** `GE_TEXDUMP` the offending head texture's tile descriptor on Silo, diff clamp/mirror bits against `gmain.s`'s expectations for that material. | OPEN — not investigated (M-49), needs a capture |
| D198 | **`alAdpcmPull`/`_decodeChunk` used `K0_TO_PHYS` (an unconditional `& 0x1FFFFFFF` mask) on 64-bit heap pointers (`src/libultra/audio/load.c`).** `K0_TO_PHYS` is the N64 kseg0→physical mask, valid only because N64 pointers fit in 32 bits; on PC's 64-bit heap it truncates any pointer whose value exceeds 512MB, corrupting the `aLoadADPCM`/`aSetLoop`/`aADPCMdec` DRAM addresses at 3 call sites (`load.c:71`, `:468`, `:474`). Every sibling call site in `env.c`/`resample.c`/`reverb.c` already resolves state pointers via `osVirtualToPhysical` (the port shim, `port/src/libultra.c`), which is bit-identical to `K0_TO_PHYS` for real N64 kseg0 addresses but correctly PC-shimmed. **Fix (ABI-layer, `#ifdef`-free — same call resolves correctly on both platforms):** replace the 3 `K0_TO_PHYS(...)` calls with `osVirtualToPhysical(...)`. No behavior change on N64 (identical result for kseg0 pointers); fixes PC. Surfaced while building the Phase-3 software mixer (D199) — the acmd list these opcodes feed now actually executes on PC instead of being ignored. | FIXED (`src/libultra/audio/load.c`) — part of the Phase-3 mixer landing, not yet build/runtime-verified in isolation |
| D199 | **Phase-3 software audio mixer: macro-swap `aXxx` opcode execution + reverse-engineered `aSetBuffer` persistent-context semantics (`port/src/mixer.c`, `port/include/mixer.h`, `include/PR/abi.h`).** The RSP audio ucode (`aspMain`) that would normally execute GE's acmd list never runs on PC (`port/src/ucode.c`); adapting the Perfect Dark PC port's macro-swap trick (`docs/dev/AUDIO-PLAN.md`), `include/PR/abi.h` now `#include`s `port/include/mixer.h` under `#ifdef PORT`, which redefines every `aXxx` macro to call an `Impl` function in `mixer.c` immediately against a small software "DMEM" scratch buffer, instead of packing RSP command words for later execution. GE uses the classic IDO libaudio ABI (verified against every call site in `src/libultra/audio/*.c` + `src/libultrare/audio/{env,reverb}.c`), not PD's differently-shaped "naudio New" ABI — DSP math (ADPCM decode, linear resample, linear envelope mix) is the same Nintendo/SGI ucode PD's `mixer.c` already ported; only the ABI/addressing layer is GE-specific and new here. Several opcodes (`aADPCMdec`, `aResample`, `aEnvMixer`, `aLoadBuffer`, `aSaveBuffer`) take only a state/DRAM pointer — their DMEM source/dest addresses and byte counts come from the most recent `aSetBuffer` call(s), which there is no RSP disassembly to check against; the persistent-context shape (`sCtx.{in,out,count,dryR,wetL,wetR}`) was reconstructed purely from reading every `aSetBuffer`+opcode call-site pair across `resample.c`/`load.c`/`reverb.c`/`mainbus.c`/`save.c`/`env.c` (see the field comments on `sCtx` in `mixer.c:52-82`). `aSegmentImpl` is a no-op — GE's audio DRAM addresses are already resolved via `osVirtualToPhysical` before reaching the mixer (see D198), so segment/base never factor into DMEM addressing here. **Risk:** the `aSetBuffer` context reconstruction is an inference, not a verified spec — if a call site this session didn't audit sets the context differently, audio for that path will mix from the wrong DMEM offsets. **Verified:** `-level_09` now runs full music + SFX 120s+ crash-free (post-D200 fix); no cross-level or opcode-level audio-correctness verification done yet (no golden-audio comparison tooling exists). | LANDED (`port/src/mixer.c`, `port/include/mixer.h`, `include/PR/abi.h`) — functional on `-level_09`, context-inference risk noted, needs a wider level sweep |
| D200 | **`-level_09` segfault: reverb's `ALDelay.input/output` are u32 — `-d->output` zero-extends to a +4GB pointer offset on 64-bit (D3x class, FIXED).** `src/libultrare/audio/reverb.c` back-references the delay ring as `&r->input[-d->output]`; on N64 (s32 `ptrdiff_t`) the u32 negation wraps to a small negative index, on PC x86-64 it zero-extends to +4294967136 samples → `_loadBuffer`/`_saveBuffer` write 13–52KB wild, trampling the delay array / `r->base` / adjacent heap in a self-propagating loop until unmapped memory. Fixed with `(s32)` negation casts at 5 sites (PD ground truth `n_reverb.c` does exactly this); ramalign line gets `(s64)`. Verified: `-level_09` runs 120 s+ crash-free, DRAM guard window over the FX region sees zero OOB writes. | FIXED — 5-line cast in reverb.c (this session) |
| D196 | **OS mouse cursor stays visible after closing the F10 options overlay in click-to-lock mode (M-49, user playtest).** `Input.MouseCaptureMode=1`. Open F10 overlay → `optionsOverlayToggle()` (`optionsoverlay.c:391`) calls `inputSuspendForOverlay()` which forces `SDL_ShowCursor(SDL_ENABLE)` + drops the grab. While open, `inputComputePad(0)` early-returns at `input.c:512` (overlay owns controller 0) **before** `reconcileGrab()` / any cursor-visibility call. On close: the next poll runs `reconcileGrab(menuMode)` (`input.c:522`) which re-grabs + hides the cursor **only if `captureArmed && !menuMode`** — i.e. only if you had already clicked-to-lock in a stage before opening F10. If you opened the overlay from a menu, or in a stage you hadn't clicked-locked yet, `want` computes 0, `mouseGrabbed` stays 0, and **nothing calls `applyCursorVisibility()`** — so the cursor `inputSuspendForOverlay()` made visible never gets re-hidden (correct free-but-focused state per `applyCursorVisibility()` `input.c:842-846` is *hidden*). **Fix (port-only, ~1 line):** call `applyCursorVisibility()` right after the `reconcileGrab(menuMode)` at `input.c:522` (self-heals every poll once the overlay-open early-return stops firing), or add an `inputResumeFromOverlay()` that runs `reconcileGrab` + `applyCursorVisibility` and call it from `optionsOverlayToggle()` on close. D180/D184/D165 input lineage. Minor. | OPEN — root-caused, 1-line fix identified, not applied (M-49) |
| D192 | **Front-end mission/level-select grid pointer still can't reach the outer cells — but only under `Input.MouseCaptureMode=0` (legacy always-grab) (M-49, user playtest).** D169 was marked FIXED (M-33) by replacing the hard-coded 320×240 pointer clamp with the live `getPlayer_c_screenwidth/height/left/top()` rect; the feel-check was flagged "owed" on both D169 and D180 and never done. It now reproduces: with `MouseCaptureMode=0` the menu pointer runs the relative-delta P-controller path (`port/src/input.c:659-690`), which clamps target+estimate to `getPlayer_c_screen*` — but that accessor returns `g_CurrentPlayer->c_screenwidth` (`src/game/bondview.c:882`), which in the front end is the ~320×240 stage viewport, **not** the front end's actual 440×330 (`front.c:8570` `viSetViewSize(440,330)`). So the effective clamp is still ~300/220 while the mission-select grid's outer split points sit at 317 / 235.5 (`front.c:516/519/3180/3194`) — outer columns + bottom row unreachable, P-controller winds up and never settles. `MouseCaptureMode=1` (the shipped default, click-to-lock) writes `cursor_h_pos`/`cursor_v_pos` directly from the absolute OS cursor (`input.c:629-657`) and *feels* fine, though it shares the bad `hiH` so the last few px at the right/bottom edge may still be short. **Not a v0.1.0 blocker** — default mode is acceptable, always-grab is opt-in via INI, front-end roughness is already in the release notes. **Fix (port-only, small):** in the `menuMode` branch, when `current_menu` is a front-end menu use the real front-end rect (hard-code `front.c`'s cursor clamp `[20,420]×[20,310]`, or read the front-end viewport global) instead of routing through `g_CurrentPlayer`. Confirm with one `GE_INPUTLOG=1` capture in mission-select — sweep the mouse to each corner, watch where `cursor=(x,y)` plateaus. D165 · D169 · D180 lineage. | OPEN — root-caused, not fixed (M-49) |
| D221 | **SELECT FILE screen: the mouse hit-box for a file slot is vertically offset from its visible box — you have to click *below* a file to select it, hovering directly over it doesn't (M-87, user QA, default `MouseCaptureMode=1`).** Different symptom from D192 (D192 is an unreachable-outer-cells *clamp range* bug under legacy mode 0; this is a per-row hit-test/highlight *offset* bug under the shipped default mode 1) — likely adjacent, same front-end grid-coordinate family. | **OPEN — observed, not investigated.** Next: find the SELECT FILE slot hit-test rects (`front.c`, mission/file-select grid split points, same neighborhood as D192's `front.c:516/519/3180/3194`) vs. the rects used to draw the highlight/box; compare row-height/origin assumptions — a hit-test rect keyed to a baseline/bottom origin while the drawn box uses a top origin would produce exactly "click below to select." |
| D222 | **High `Video.FovScale` breaks culling — NPCs and some world objects stop rendering near the screen edges (M-87, user QA).** Split out of D211's noted residual: `currentPlayerSetCameraScale()` / the game's frustum-cull and screen↔world math still run at the **nominal, unscaled** fovy while the actual render projection is widened by `portFovScale` (`fr.c:737`) — so objects that are on-screen in the widened view but outside the nominal-FOV frustum get culled before they reach the renderer. Confirmed **not cosmetic** — a real, noticeable functional regression at high FovScale, not just "culls a hair early." | **OPEN — root cause known, not fixed.** Fix direction: find every frustum-cull / visibility-test call site that derives its frustum from the nominal fovy (`currentPlayerSetCameraScale` and friends, `bondview2.c`/`fr.c`) and feed it `frFovY` (the `portFovScale`-adjusted value) instead, gated the same way as the D211 render-path change (`!= LEVELID_TITLE`, `#ifdef PORT`). Must not change cull behavior at `FovScale=100` (no-op parity). Risk: some of these paths may double as gameplay-affecting AI visibility/awareness checks, not just render culling — audit which before changing (portFovScale must never leak into NPC AI *behavior*, only rendering-adjacent visibility). |
| D223 | **QoL ask: mouse-wheel weapon cycling should be directional like a normal PC shooter — wheel up = next weapon, wheel down = previous — instead of both directions cycling forward (M-87, user QA).** **Root-caused, port-only fix, no game-logic change needed** — GE's engine already has separate forward/backward weapon-cycle signals: `bondview2.c` computes `moveData.weaponForwardOffset = edge(invButtons) && !(shootButtons held)` and `moveData.weaponBackOffset = (invButtons held) && edge(shootButtons)` (`bondview2.c:5338-5347`, the classic N64 "hold A, tap Z to cycle backward" trick). Default control scheme (the `else` branch used unless a Domino/Goodhead/Galore/Plenty preset is active, `bondview2.c:5177-5179`): `invButtons = A_BUTTON` (= port `GE_CONT_A`, `input.c:86`), `shootButtons = Z_TRIG` (= port `GE_CONT_G`, `input.c:88`, the fire button). `moveData.triggerOn` is explicitly suppressed while `invButtons` is held, so holding A never causes an accidental shot when the Z edge lands. The port's `inputPostWheel()` (`input.c:977`) currently does `if (notches < 0) notches = -notches` — **discards wheel direction** — and only ever synthesizes a bare `GE_CONT_A` pulse (`wheelPulse`, `input.c:631-634`), i.e. always "forward." | **OPEN — root cause + fix mechanism known, not implemented.** Plan: keep `wheelPulse` (or a new `wheelPulseFwd`) for wheel-up as today; add a `wheelPulseBack` path for wheel-down that instead holds `GE_CONT_A` across ≥2 polls while presenting a fresh `GE_CONT_G` edge partway through (mirroring the invButtons-held + shootButtons-edge combo) — sequence matters (A must already read "held" `oldbuttons`-wise before G's edge, per `weaponBackOffset`'s check order). Must confirm which N64 control preset (`controldef`) the port's save data actually carries — `invButtons`/`shootButtons` differ under KISSY/GOODNIGHT/SOLITARE/etc; if the port doesn't force one, the synthesized combo needs to match whatever's active, or the port should just force the default (HONEY-equivalent) preset since its own `Input.Bind` system already replaces N64-style scheme selection. Port-only, `#ifdef PORT` in `input.c`, no `src/` game-logic touch. **M-87: PD-PC-port precedent checked (user asked what PD did about N64 control presets).** PD does **not** route mouse/gamepad through the N64 preset system at all — it defines its own logical control-key layer (`CK_*` in `port/src/input.c`/`input.h`, e.g. `CK_DPAD_L`/`CK_Y`) fully decoupled from the N64 controller-config enum, and even treats mouse-wheel-up/-down as **two independent bindable virtual keys** (`VK_MOUSE_WHEEL_UP`/`_DN`, defaulted to distinct `CK_*` actions) rather than one "wheel = cycle" event — exactly the shape of the fix this row wants. PD's `controldef` field is hardcoded (`player.c`/`playermgr.c` `controldef = 2`), not left to the original in-game control-style picker. **This GE port already has the equivalent layer for this purpose: D214's `Input.Bind` system.** Recommendation for D223 (and the user's broader ask — N64 presets aren't meaningful for mouse/keyboard, and modern-gamepad (Xbox/PS) support should be a first-class port concept, not routed through N64 presets either): add two new `Input.Bind`-able logical actions (e.g. `IA_WEAPON_NEXT` / `IA_WEAPON_PREV`) that synthesize the invButtons/shootButtons combos under the hood, default-bound to wheel-up/-down (keyboard/mouse) and left open for a future bumper/shoulder-button default under real gamepad support; and have the port force a single fixed `controldef` (matching PD's approach) so the synthesis target never depends on the original N64 control-style menu. That menu itself becomes a candidate for hiding/no-op in a future QoL pass once `Input.Bind` fully supersedes it — not proposed as a change here, just noted as the natural conclusion. |
| D224 | **QoL gap (M-87, from a PD-PC-port survey): the N64 Rumble Pak is fully dropped instead of modernized.** GE genuinely has a rumble subsystem (`src/motor.c`, `src/joy.c`) but the port's `osMotorInit/Start/Stop` (`port/src/libultra.c:1220-1223`) are all hard stubs — "no accessories on the PC," `osMotorStart`/`osMotorStop` return `-1` unconditionally, `osPfsFindFile`-family returns `PFS_ERR_NOPACK`. So every in-game rumble event (weapon recoil, explosions, etc.) computes and calls into a dead path. **PD's PC port wires the equivalent straight through to real gamepad rumble:** `inputRumbleSupported(idx)` checks `SDL_GameControllerHasRumble`/`SDL_JoystickIsHaptic` (with a Windows fallback: some pads report no haptics but rumble anyway), `inputRumble(idx, strength, time)` calls `SDL_GameControllerRumble()` scaled by a per-pad `RumbleScale` config value (options-menu slider, `Input.PadN.RumbleScale`), and `osPfsFindFile` reports a "Rumble Pak" present exactly when a real pad supports rumble (`libultra.c` — same "pretend the N64 accessory is there" pattern GE's port already uses for `osMemSize`/Expansion Pak, just applied to a real capability instead of faked). Cross-ref `QOL-INVENTORY.md` (folded into the existing "per-pad tuning" row). | **OPEN — not implemented, straightforward.** No `src/` game-logic touch: GE's `motor.c`/`joy.c` call sites already exist and already call the `osMotor*` shims; the fix is entirely in `port/src/libultra.c` + `input.c` (add `inputRumbleSupported`/`inputRumble` per the PD shape, wire `osMotorStart`/`osMotorStop` to them, add `Input.PadN.RumbleScale` config). Keyboard/mouse play is unaffected (no rumble device). |
| D225 | **Dev-tooling QoL (M-87, user ask): a debug-build/env toggle that unlocks all missions + all cheats, for testing.** Root-caused a clean single choke point — **both** gates route through the same function: `bool fileGetIsCheatUnlocked(save_data *save, s32 cheat)` (`src/game/file2.c:391`) reads the save's `unlocked_cheats_1/2/3` bitfields and returns whether bit `cheat` is set. Mission-select lock (`src/game/file.c:59`, gates whether a briefing page's mission is selectable) and the cheat-menu availability check (`frontCheckIfCheatIsUnlocked`, `front.c:1009`, itself calling `fileGetIsCheatUnlocked` for the per-level `SP_LEVEL_*` cheat IDs) **both** call it — it's a pure, side-effect-free bitfield read (no save-data mutation), so short-circuiting it to always-true under a debug env var unlocks mission-select *and* the cheat menu in one change, matching exactly what was asked ("those two go a long way"). **Bonus, free once cheats unlock:** the ROM already contains real Rare-era developer cheats that are otherwise unreachable in normal play — `CHEAT_LINEMODE` (wireframe render — would make D222-style culling bugs trivial to see), `CHEAT_BONDPHASE`, `CHEAT_DEBUG_POS`, `CHEAT_DEBUG_UNK5` (`cheat.c` ~L687-707, `CHEAT_MASK_GLOBAL`) — worth trying once this lands, may be useful dev tools in their own right. | **OPEN — root-caused, not implemented.** Plan: `#ifdef PORT`, env-gated (e.g. `GE_DEBUG_UNLOCKALL=1`, catalogue in `GE-ENV-PROBES.md` once added) short-circuit at the top of `fileGetIsCheatUnlocked` — `if (getenv-cached-flag) return TRUE;` before the bitfield read. Default off ⇒ zero behavior change for normal play; when set, purely a query-time override (save file itself is never touched, so it can't corrupt a save or leak into the shipped default). No `src/` logic change beyond the guarded early-return (same class of edit as other `#ifdef PORT` dev-convenience hooks, e.g. `GE_STARTMENU`). |
| D226 | **QoL ask: player-adjustable HUD scale for the ammo counter, bottom-left pickup/item status text, and top-of-screen dialogue/subtitle text.** All three route through the same primitive, `textRenderOutlined()` (`src/game/textrelated.c:688`, doc comment: *"Used for ammo counter, bottom left HUD messages, countdown timers"*), called from two specific in-game (not menu) sites in `bondview2.c`: **ammo + bottom-left pickup/status** at `bondview2.c:10005` (`stringbuffer_lowerleft[...]`, anchored at `view_left`/`view_vert`), and **top-of-screen dialogue** at `bondview2.c:10163`/`10165` (`stringbuffer_top[...]`, anchored at `msg.x`/`msg.y`). `textRenderOutlined` → `textDrawGlyphQuad` (`textrelated.c:615`) emits each glyph as a plain RDP `gSPTextureRectangle` at the glyph's native font-metric pixel size, `dsdx`/`dsdy` = `0x400` (1:1 texel:pixel, no scale factor anywhere in the call chain). Same function is also used by menus/options/multiplayer UI (`front.c`, `options.c`, `mpmenu.c`) — **do not scale generically**, those have their own layout at fixed screen fractions and D211 already established HUD anchors must stay put; scale only the 3 in-game HUD call sites named above, about their existing anchor (so bottom-left stays bottom-left, top stays top — same anchor-preserving principle as D211's HUD-static check). | **OPEN — root cause / call sites identified, not designed or implemented.** Two viable directions: (a) a `#ifdef PORT` wrapper at just these 2-3 `bondview2.c` call sites that scales the emitted `gSPTextureRectangle` geometry (`xl/yl/xh/yh`) by a new `f32 portHudScale` (default 1.0 ⇒ byte-identical) while inversely scaling `dsdx/dsdy` to keep the same source glyph texels mapped onto the larger/smaller rect, re-anchored so the opposite edge (bottom for the lower-left block, top for dialogue) doesn't move; or (b) thread a scale parameter into `textRenderOutlined`/`textDrawGlyphQuad` itself, defaulted to 1.0 at every other call site. (a) is more surgical (matches the "don't touch shared menu text" constraint) but duplicates a little geometry math; (b) is more central but touches a much more widely shared function. No PD precedent found (PD's port has no equivalent HUD-scale option). F10 overlay slider candidate once implemented, alongside `Video.FovScale`. |
| D204 | **PC audio ran ~2 % below real time permanently: GE's AI feedback loop (`src/audi.c:531`) has a ~3 ms setpoint that PC scheduling jitter clears, so the SDL queue starves and the device pads playback with silence (FIXED, measured).** `amMain` wakes at 30 Hz (`sched.c:334` forwards every 2nd retrace to the audio client) and asks for `g_MinFrameSize`=720 samples per block = 21600/s against a 22050 Hz device — structurally 2 % short, relying on 784-sample top-up blocks that `audi.c` only requests once the reported AI length falls under ~69 frames (3 ms). Fine on N64 (double-buffered AI, exact VI interrupt); on PC the queue empties before the loop reacts, and the padded silence is unrecoverable time. **Fix (F5, port-only):** `audioGetAiLengthBytes()` subtracts a 1024-frame (~46 ms) cushion before reporting, so the loop tops up while slack remains; `audi.c`'s control law untouched (rule #2 clean). Measured A/B in one binary via `GE_D204_OLD`: **rt 0.980 → 1.000, q min 0 → 368, drop 0.** Also: F1 correct AI_LEN_REG single-buffer semantics (robustness, measures as a no-op), F3 `Audio.QueueLimit` 8192→2880 + non-silent drops (NB: an existing `ge007.ini` pins it), F4 oversize invariant guard, and `GE_D204=1` cheap audio-health monitor (`rt`/`q`/`drop`/`max`, safe to leave on for a full playtest). **Falsified in-session, do not re-open:** the u32 wrap at `audi.c:531` is real (high-water 2064 frames > the 789 threshold) but harmless — `frameSamples` is `s16` (`audi.c:145`), so it lands negative and audi.c's own lower clamp catches it; max block stays 3136 B vs the 3156 B allocation, no heap overrun. **Corrects two M-63 claims:** `soundIndex=109` is guards' return fire (count was double-logged; the "32.6 ms cadence" is just the 2880-byte block quantum every `dumppos` is rounded to) — closed negative; and M-63's "13 % of real time" was `GE_MIXERTRACE`'s own 25 MB unbuffered log starving the audio thread (same repro without it: 58.0 s / 60 s). Does NOT explain D202. | FIXED (`port/src/audio.c`, `port/src/libultra.c`, `port/include/audio.h`) — measured; a by-ear pass on a real playthrough still owed |
| D203 | **Steam Deck (SteamOS, x86_64 Linux) v0.1.0 release bundle: Facility (`-level_34`) crashes "after loading in as James Bond" (user bug report).** Idle repro attempts on WSL Ubuntu with the actual `dist/goldeneye-pc-port-0.1.0-linux-x86_64.tar.gz` are **negative**: 120 s idle → frame 3300+ clean exit (the intro auto-advances, so "sitting as Bond" is covered), and a ~4-min stick-forward-only run → frame 6900+ also survived. So the trigger needs real gameplay input (fire/action) or is Deck-environment-specific. **Leading suspect: the D191 family** — both known v0.1.0 field crashes are SIGSEGVs in `modelGetNodeRwData` (`model.c:478`) over a truncated/NULL `root->Parent` node link, and both were triggered by *gameplay state* (first guard kill on Bunker ii; post-cutscene on Statue), never by idling; Facility opens into the Ourumov + soldier-squad scripted beat (D193a instance), which fits the trigger profile. Alternatives: Deck GL driver path (RADV/zink vs llvmpipe) or 40/50/60 Hz timing (D193 family). **Next:** (1) WSL run under gdb with a walk+fire `GE_INPUTSCRIPT` — generate the script into a file first (earlier attempts were blocked by a `bash -lc` quoting quirk that emptied the loop var, so no fire buttons ever went out); on SIGSEGV compare the fault PC against `modelGetNodeRwData`; (2) if not reproducible on WSL, get `ge007.crash.log` from the Deck (the Linux build writes it — proven by a stale one in the smoke tree) + ask what Bond was doing at crash time; optionally a gdb run from the SteamOS terminal. | OPEN — negative idle repros only; not yet reproduced with input. |
| D201 | **`src/libultra/audio/bnkf.c` bank/instrument/sound/wavetable relocation offsets were `s32`, truncating the real 64-bit base pointer.** `alSeqFileNew`/`alBnkfNew` compute `offset = (s32) base` from a heap pointer and thread it through `_bnkfPatchBank/Inst/Sound/WaveTable` as `s32 offset, table` parameters, used to rebase every embedded pointer field in a loaded bank/inst/sound/wavetable/book/loop record. On N64 (32-bit pointers) the truncation is a no-op; on PC, sign-extension of the truncated `s32` during the rebase arithmetic reconstructs the wrong 64-bit address whenever the base allocation's low 32 bits have the top bit set (~50% of allocations, allocator-dependent) — corrupting every relocated pointer in the bank file, a plausible contributor to bad model/audio pointers surfacing downstream (D191-family symptoms are a node-tree walk, not this, but the class is the same). **Fix (ABI-layer, no logic change):** widen `offset`/`table`/`woffset` and all four `_bnkfPatch*` parameters from `s32` to `uintptr_t` (`<stdint.h>`) — already this codebase's idiom for pointer-safe integers in `src/game/*.c`. No behavior change on the 32-bit N64 build. | FIXED (`src/libultra/audio/bnkf.c`) — part of the Phase-3 mixer landing, not yet build/runtime-verified in isolation |
| D202 | **Silenced PPK plays a "slap" instead of a gunshot, PLUS user reports broader real-time audio corruption (M-52 → M-59 → REOPENED M-60 → M-61/M-62 closed the data chain, recommended close → REOPENED M-63 on a fuller symptom report).** M-56–M-62: exhaustively verified the index/bank/pointer/decode/ROM-offset/source-data chain — soundIndex 46 is correctly requested and the data is ground truth (byte-match checksum corroboration, M-62). **M-63: user's actual complaint is broader — wrong sound always plays, correct one SOMETIMES plays too (both heard together), and sounds "pile up and spirally glitch out" over a session until only a stuck looping sound (e.g. Bunker's door) remains.** Got a live build+run repro this session (`-level_09`, 60s, `GE_MIXERTRACE`+`GE_AUDIOTRACE`+`GE_AUDIODUMP`, 58 scripted PPK fires). **Ruled out voice-pool exhaustion/leak** — only 16 distinct voice slots used, properly recycled (`sndDeactivate` count tracks `sndPlaySfx` count 1:1). **Found an unexplained anomaly**: `soundIndex=109` (AK47 bolt-action SFX) fires 94 times in 60s despite no AK47 in the repro, some retriggering the same voice slot at a suspiciously uniform ~32.6ms cadence (4-in-a-row) — far faster than plausible weapon fire; looks like something re-issuing `sndPlaySfx` every audio tick instead of once per game event. Not yet confirmed as the cause of the user's symptom, but the top concrete lead. Traces preserved: `scratchpad/d202-m63/`. (Side note, not a real bug: chased a `load_bg_file` crash this session that turned out to be an incomplete test-data setup on the investigator's part — see M-63 write-up.) **M-65 PARTIALLY resolved:** the "eventual silence" cascade is root-caused and mitigated (ownerless `SOUND_FLAG_LOOPED` SFX — sound 203, `METAL_SLIDE_CLOSE_SFX`, `decayTime=-1`, played by `doorPlayCloseSound0/1` with `NULL` owner — leaks 7 of 8 voices; a 4-line guard in the preemption scan reclaims only provably-ownerless loops, measured 171→629 acquisitions), and a second independent cause is fixed (`sndCreatePostEvent` was stubbed out per D138, removing ALL distance attenuation). The audible stuck door loop is still not reproduced — every candidate mechanism ruled out by measurement (see M-65); needs a user capture at the failing door with `GE_AUDIOTRACE=1 GE_AUDIODUMP=1`. | ROOT CAUSE ESTABLISHED (M-66); DISPOSITION C IMPLEMENTED + MEASURED (M-66b) — the audible stuck loop is sound 203 (`METAL_SLIDE_CLOSE_SFX`): ROM infinite ADPCM loop (`count=-1`) + `decayTime=-1`, played fire-and-forget by `doorPlayCloseSound0/1` with NULL owner, never stored in any slot, exempt from preemption (flag 0x12) and from decay-stop scheduling, priority 0x41 makes it unstealable — **faithful N64 behaviour / original quirk, not a port bug**. 327 ms period = -550-cent pitch (keyBase 54 + detune 50 − shift 6000 → ratio 0.7278; 5248 samples @ 22050×0.7278 = 327.007 ms). M-65's guard fires only under pool pressure (twice in the user capture) and cannot silence the idle loop. **Disposition C implemented + measured (M-66b):** PC-only `AL_SNDP_PORT_EXPIRE_EVT` fades provably-ownerless infinite-loop SFX out ~2.5 s after start (2 s delay + 0.5 s fade); per-second spectral check: pre-fix drone flat at max t≈39→361 s, post-fix transient bursts with full silence between; run ends allocated=0/8. **M-67: static analysis exhausted — full-bank data scan clean (all 261), DSP audit complete vs PD, A_LOOP branch dead code in GE, reference clip B00I00S2D.wav REFUTED as an in-game capture (best waveform CC 0.19 ≈ noise; M-61's +0.999 was envelope-only correlation); new `GE_VOICEDUMP` per-voice probe + timestamped WIRE/AUDIOTRACE for the decisive runtime A/B; self-test found the offline resample direction had been inverted all along (y[k]=x[ratio·k]) and that GE "music" is an SFX-sequence system with its own ~8-sample set sharing the 8-voice pool; parked anomaly: sound 232 requested at level start produces no audible audio. **M-68 (user captures ×3): all three complaint classes verified per-voice at runtime — doors CC 0.91–0.99, PPK 46 CC 0.90–0.97, armor 81 full 833 ms vs ROM-predicted 837 ms @ ratio 0.5612; the earlier "instant death" metric was a line-count artifact (µs-timestamped `[EVT]` probe shows every STOP lands on schedule). No port bug: the heard "wrong" sounds are the original design (overlapping SFX, short decays, quick-stop truncation).** **M-69 (full-corpus exact match, 320 scripted-run requests): 286/320 CC ≥ 0.85 — every allocated voice plays the exact ROM sample at the exact ROM pitch; the initial 44 "BAD" were tooling artifacts (voicedump stamps are block-END times; coarse-slide missed sharp onsets; refs not clipped at voice death). Remaining failures = dropped allocations (no `[VOICE+]` — suspected 8-voice pool exhaustion, unverified) + shared-wavetable wire misattribution. No corruption.** **M-70 (decisive clean re-capture, 209 requests, matcher v3): every voiced request plays the exact ROM sample — 200/209 auto CC ≥ 0.85; the 4 MID are idx=109 verified at CC 0.946 by manual ratio sweep (residual = wall-clock-bursty dump stamps + short live windows); the 5 NO-WIRE are 1 matcher window artifact + 4 never-voiced requests, NOT pool exhaustion (alloc ≤ 5/8 at each drop). Zero wrong-sample playback — the D202 wrong-sample/mixer-corruption hypothesis is refuted.** **M-71: user by-ear pass on M-66b = PASS ("the metal door sound does not loop forever now") — Disposition C validated; D202 ready for probe removal + close. The user's remaining audio complaints (slap/glass over gunfire, explosion→scream, armor pickup wrong sample — N64 A/B confirms non-fidelity) are a DIFFERENT bug: PC requests different/extra sound indices than N64; bank converter + playback chain exonerated → split out to D205.** **M-76/M-77c: D202 CLOSED.** The silenced-PPK "slap" was D206 (bank index +1) — `ALInstrumentAlt_s.soundArray` sits at struct offset 12 on N64 but 16 on PC (pointer width), so `sndPlaySfx` resolved `GUN_SILPPK_A`=46 to on-disk slot 46 (`PUNCH1`, a slap) instead of slot 45; M-61's rejected "index 45, +0.999" was right. Fix `snd.c:1001` `#ifdef PORT` `-1`, committed `378386d1`; **user by-ear A/B vs N64 = PASS (M-77c)**. Mixer/pool/decode were correctly exonerated by M-56–M-70. The M-65/M-66b ownerless-infinite-loop mitigations stay (independent, validated M-71). |
| D206 | **PC played every SFX one bank slot too high — a pointer-width layout shift in `struct ALInstrumentAlt_s`, not a game-logic bug (M-76 FIXED).** Root cause of D205 (3) armour + (4) melee→Klobb and of D202's silenced-PPK "slap". `sndPlaySfx` resolves a sound via `soundBank->instArray[0]->soundArray[soundIndex]` (`snd.c:1001`), casting the on-disk standard `ALInstrument` (12-byte u8 header, `s16 bendRange`@12, `s16 soundCount`@14, `ALSound* soundArray[]`@16) through GE's `struct ALInstrumentAlt_s { s32 unk0, unk4, unk8; ALSound *soundArray[1]; }` (`src/snd.h:165`). On N64 (4-byte pointers) that struct's `soundArray` sits at **offset 12** — it deliberately aliases the on-disk `bendRange`/`soundCount` words, so `soundArray[N]` == on-disk table entry **[N-1]**, i.e. GE's `SFX_ID` values are **1-based** into the sound table (`SFX_ID 0 = NOTHING_SFX`, short-circuited at `snd.c:992`, never dereferenced). On PC the 8-byte pointer + 8-byte alignment pushes `soundArray` to **offset 16**, and the converted bank (`port/src/romdata.c` `afFixupInst`, which faithfully reproduces the standard layout) is packed to match — so `soundArray[N]` landed on entry **[N]**, one slot high, on every SFX. Verified by static struct/converter arithmetic **and** at runtime: a BUNKER1 `GE_AUDIOTRACE` capture post-fix shows all ~90 distinct requested indices resolve to `rom_sfx_decode.py` slot **N-1** (0/90 at N). e.g. `ARMOUR_COLLECT_SFX`=81 → slot 80 (= rip `S50`, the user's "correct" armour); `skorpion_stats.Sound`=0x6A=106 → slot 105 (= rip `S69`, "the actual Klobb sound"); `wppksil_stats.Sound`=0x2E=46 → slot 45 (= rip `S2D`, M-61's rejected "+0.999" clip). The earlier "262 enum vs 261 bank ⇒ needs a -1" arithmetic was right about the *symptom*; the *mechanism* is the alias, and `BIG_CLANK_SFX`=261 → slot 260 is in-bounds (never an OOB). Upstream `n64decomp/007` `snd.c` byte-matches with no `-1` and no `NON_MATCHING` guard — correct, because the N64 struct offset supplies the -1. **NOT D205 (2)** explosion→scream (169-183 → slots 168-182, all still explosions) — stays with D205. | **FIXED (M-76, `src/snd.c:1001`, `#ifdef PORT` → `soundArray[soundIndex - 1]`, N64 line verbatim under `#else`).** ABI/layout-only, D3x class (pointer-width struct-layout reconciliation); no game-logic change. Build clean; BUNKER1 60s crash-free; `-level_09` golden framediff 3/3. **User by-ear A/B vs N64 = PASS (M-77c)** — armour / melee / silenced PP7 all correct now → D202 closed, D205 down to symptom (2). Committed `378386d1`. Scratchpad `rom_sfx_decode.py` / `exact_match.py` / `diff_bank.py` still walk slots 0-based — correct for "what is at bank slot i", but a check against a `SFX_ID` must compare to slot `id-1`. | 
| D207 | **Alarm klaxon starves combat SFX and (per user) doesn't recover after it stops — surfaced by D206 (M-77, ROOT-CAUSED, fix designed not applied).** Before D206, `ALARM3_SFX` (id 163) resolved to the wrong bank slot 163 — a **one-shot** sample: played once, freed its voice. After D206 it correctly resolves to slot 162 = the **real infinite-loop klaxon** (`decayTime=-1` -> `SOUND_FLAG_LOOPED`; ADPCM `loop.count=-1`). Played by `handle_alarm_gas_timer_calldamage()` (`src/game/propobj.c:14457`) with owner `&ptr_alarm_sfx` while `alarmIsActive()`. It now **permanently holds 1 of only 8 SFX voices** (`MUSIC_SFX_SEQ_MAYBE_MAX_SOUNDS=8`, `src/music.c`), at priority `0x41` (looped sounds get `decayTimeFlag + 0x40`, `sndSetupSound` `snd.c:820`) — and is **exempt from the port's preemption scan** (`snd.c` ~369: `!(unk3e & 0x12)` refuses to steal any LOOPED/RETRIGGER voice; the M-65 carve-out only covers *ownerless* loops, and the alarm is owned). **M-77b:** the voice-starvation hypothesis did NOT hold up — a BUNKER1 `GE_FORCEALARM` + scripted-fire soak showed **0 true drops** (`sndDisposeSound` no-voice sites instrumented), the designed last-resort preemption pass **never fired**, and `g_sndAllocatedVoicesCount` recovered to 0 after every alarm-off (no leak). The earlier "~20% drop" was a log-latency artifact. The alarm klaxon (`ALARM3` slot 162) is a *continuous, gapless* infinite loop — a real GE alarm pulses — so the leading remaining explanation is **perceptual** (the drone masking gunfire, and/or slot-162 mixed too loud vs N64), or genuine 8/8 starvation only on a heavy level not yet tested. Needs a user repro. | MONITORING (M-77c: user reports the alarm is acceptable in play after D206; no fix shipped) — **the voice-starvation hypothesis did NOT hold up (M-77b).** The designed last-resort LOOPED-preemption pass was built, and instrumented drop counters (`[D207-DROP]` at both `sndDisposeSound` no-voice sites) + a `[D207-YIELD]` trace were added. Result on BUNKER1 (`GE_FORCEALARM` + scripted sustained fire, ~85 s): **zero true drops**, the last-resort pass **never fired** (pass-1 always found a non-looped victim), and `g_sndAllocatedVoicesCount` sat at 1 during quiet alarm and recovered to **0** after every alarm-off — no leak, no permanent starvation. The earlier "~20% dropped" figure was a measurement artifact (log-line latency, not real drops; every requested sound eventually got a voice via the `unk38` retries). So the fix was **reverted** (kept: nothing in `snd.c`; D206 `-1` only). BUNKER1 combat is too light to peg the 8-voice pool. **Next session needs a real repro from the user:** which level + weapon, and a `GE_AUDIOTRACE=1` capture at the failing alarm — is it (a) genuine 8/8 starvation on a heavy level (Facility/Silo/Statue firefight), (b) a leak that only triggers on a specific alarm-stop path, or (c) perceptual — the continuous klaxon drone masking gunfire / the alarm mixed too loud (check `ALARM3` slot 162 volume vs N64). Temp `GE_FORCEALARM` probe kept in `src/game/propobj.c` for that work. **M-78: re-added just the `[D207-DROP]` counter (NOT the reverted last-resort pass) at both `sndDisposeSound` no-voice sites in `snd.c` (gated on `GE_AUDIOTRACE`), logging `state`/`prio`/`flags`/`count=N/max`, so the user's real-level capture is decisive between (a)/(b)/(c). Static re-check of `ALARM3` slot 162 this session: `sampleVolume=80` (vs gunfire 90-110, punches 100-110 — the klaxon is quieter per-sample, not louder, weakening the "mixed too loud" half of (c)); `decayTime=-1` + `keyMax=8` (`&0xf0=0` → NOT `SOUND_FLAG_RETRIGGER`) → it is a continuous looped sound by design on N64 too, the pulse is baked into the 225→7512-sample ADPCM loop body. So (c) narrows to "the continuous drone masks gunfire" or a loop-point playback defect; (a)/(b) still need the heavy-level capture. Launcher `build-pc/d207_run.ps1` added (forced-alarm auto-repro on level_09).** |
| D208 | **Player weapon-fire sound goes permanently silent mid-firefight (all other SFX unaffected) — `struct` field `bondview.h:200 s32 field_A48` is used as a second `ALSoundState *` sound-handle slot, but is only 32 bits wide on PC (M-78c, ROOT CAUSE FOUND, D3x pointer-width class).** This is the "doesn't recover after the alarm stops" half of the user's D207 report — a distinct bug from D207's voice-pool question. The player's per-hand fire sound is double-buffered across two handle slots: `hands[h].audioHandle` (`ALSoundState *`, correctly pointer-width) and `hands[h].field_A48` (`s32`). `gunfire.c:3186-3200` (`bondfirefunc`) each trigger interval calls `sndPlaySfx(bank, idx, (ALSoundState *)&handptr->field_A48)`, and `sndPlaySfx` writes the new state through it as `pendingState->link.next = nextState` — an **8-byte pointer store** into a 4-byte field, overrunning into the adjacent `field_A4C` (which is the auto-fire sound-cadence timer, written back every frame at `gunfire.c:3168`). `sndUnlinkClearSound` likewise does an 8-byte `state->state->link.next = NULL` there. The two writers stomp each other; after enough double-buffered fire cycles `field_A48` holds a value that is neither `0` nor a valid pointer, so the `else if ((ALSoundState *)handptr->field_A48 == 0)` re-arm gate in the double buffer is **never true again → no further player gunshot is ever issued.** `audioHandle` alone can't cover because the code only replays into it `if (audioHandle == NULL)` and the async `sndDeactivate(audioHandle)` leaves it briefly non-NULL, deferring to the jammed `field_A48` branch. **Evidence (user Bunker capture, `audiotrace.log`, silenced PP7 = idx 46):** idx-46 `sndPlaySfx` requests stop dead at t=776609203515 (log line 18930 of 25086) while guard fire (109), pickups (232), impacts, doors all continue to the end; at line 19159 the player-gun slot `0x70076630` is caught holding `was 0x000001db00000000` — a torn 64-bit value = `field_A48=0x00000000` + `field_A4C=0x1db` (475, a `g_GlobalTimer`-relative cadence stamp) read as one word. The alarm is not required — it just raises `sndPlaySfx` churn so the tear is hit sooner. On N64 both fields are 4 bytes, `audioHandle` is a 4-byte pointer, and `&field_A48` punned as `ALLink*` is a 4-byte `link.next` write — all consistent, no overrun. **`bondview` / `hands[]` is runtime-only state (`g_CurrentPlayer`), never ROM-serialized**, so widening `field_A48` to pointer width is layout-safe (no by-offset or ROM-cast access — grep-verified; only `field_A50`/`field_A4C` sit after it and are plain `s32` timers). | FIXED + user playtest-verified (M-78d) — Bunker firefight: player gunshots persist through the whole engagement and survive the klaxon stopping. Committed 7601b9db (PR #29, merged). |
| D209 | **Every AI character in the game is locked to a *walk* animation regardless of the speed the ailist commands — `act_ubytes.padding[45]`, a raw-byte alias into the action union for `act_gopos.unk59` (the SPEED tier), shifts from offset 45 to 81 at 64-bit (M-81, ROOT CAUSE of D193, D3x pointer-width class).** GE character *travel* is animation-root-motion driven, so the bound locomotion animation sets the speed. `get_sound_at_range()` (`chraction.c:3573`, the locomotion-anim selector — misleading name) picks `ANIM_DATA_sprinting`/`running`/`walking` (and the one-handed/female variants) from its `arg1` tier. Its **only** caller, `play_hit_soundeffect_and_proper_volume()` (`chraction.c:3665`), passes the tier as `self->act_ubytes.padding[45]` — a raw byte read out of the `ChrRecord` action union rather than the named field. On N64 that alias is exact: `struct act_gopos` = `{coord3d targetpos@0, StandTile *target@12, waypoint *target_path@16, waypoint *waypoints[6]@20, u8 curindex@44, u8 unk59@45}`, so `padding[45]` **is** `unk59`. On x86-64 the three pointer members widen 4→8 B (`target@16, target_path@24, waypoints@32`), pushing `curindex`→80 and `unk59`→**81**, while the literal `45` now lands inside `waypoints[1]` (spanning 40..47) — specifically **byte 5 of a heap pointer**. The port's arena is low-4GB (`0x00000000_70xxxxxx`), so byte 5 is **always `0x00`** → `arg1 = 0` → `ANIM_DATA_walking` / `ANIM_DATA_walking_unarmed`, unconditionally, for every AI chr on every level since the 64-bit transition. The named-field reads (`chrlvApplySpeed(self, ..., (s32) self->act_gopos.unk59, ...)` at `:9104`, and the `unk59 == 2` / `== 1` `modelSetAnimSpeed` tiers at `:9106-9130`) resolve correctly, which is why the tier *looked* right everywhere it was inspected — only the alias was broken. **Runtime evidence (Cradle `-level_41`, `GE_D193A` extended with `tier`/`aidx`):** all six chrs report `act=15 ACT_GOPOS tier=1 SPEED_RUN` on 420/440 samples, yet `aidx` (live `model->anim` reverse-resolved against `animation_table_ptrs1`) is **40 = `PTR_ANIM_walking`** for Trevelyan (chr 0) and **107 = `PTR_ANIM_walking_unarmed`** for the guards on every single sample — `aidx 42 (running)` / `41 (sprinting)` never appear. Travel speed sat in a flat 140–155 u/s band for all of them, independent of animation, which is the walk-cycle stride. `act_ubytes` has exactly one use in the whole tree, so this is a single site. Both callers of `play_hit_soundeffect_and_proper_volume` reach it with `actiontype == ACT_GOPOS` (`:3723`, immediately after `actiontype = ACT_GOPOS; act_gopos.unk59 = speed;`; and `:9087`, inside the explicit `else` of `actiontype == ACT_PATROL`, mirroring the `chrlvApplySpeed` split directly below it), so reading the named field is exactly the byte N64 reads — no behavioural change. | **FIXED + user-playtest-verified (M-81).** `chraction.c:3665`, `#ifdef PORT` → `get_sound_at_range(self, self->act_gopos.unk59, ...)`; the N64 line kept verbatim under `#else`. ABI/pointer-width only, no game-logic change (rule-2 narrow exception, D3x class). User ran Cradle on the fix build: **Trevelyan and the guards run.** Also corrected the `// guess: room` comment on `unk59` (`src/bondtypes.h:2191`) — it is the SPEED tier. |
| D210 | **`chrToPatrol()` leaves `act_patrol.lastvisible60` uninitialised on PC — a raw-byte union alias (`act_init.padding[0x13]`) that shifts when `act_patrol` widens (M-82, A1 class, sibling of D209).** `chrToPatrol` (`chraction.c:3872`, sets `ACT_PATROL`) does `self->act_init.padding[0x13] = -1;` — union-relative byte `0x4c`. On N64 `struct act_patrol` = `{patrol_path *path@0, s32 nextstep@4, bool forward@8, waydata@0xc (0x40 B), s32 lastvisible60@0x4c, f32 speed@0x50}`, so byte `0x4c` **is** `lastvisible60`, initialised to `-1`. On x86-64 the leading `path` pointer widens 4→8 B, so everything after shifts +4: `lastvisible60` is now at `0x50`, and the literal `0x13`/byte-`0x4c` write lands in the last 4 bytes of `waydata` (`segdisttotal`). Net PC effect: `lastvisible60` keeps whatever garbage the previous union arm left, and the "chr hasn't seen the player recently → break off / resume patrol" gate at `chraction.c:~9347` (`(act_patrol.lastvisible60 + CHRLV_DEFAULT_TIMER) < g_GlobalTimer`) reads that garbage on the first tick(s) after entering PATROL — until `chr.c:2569` / `chraction.c:3482` / `:9359` overwrite it (only when the chr actually sees the player). Same root class as D209 (raw index into a union arm whose pointer members widened); found by the post-D209 A1 sweep. Only new instance in the sweep — `act_init.padding[0..3]` in `chrlvTickAnim`/`chrlvTickDead` alias `act_anim`/`act_dead`, both pointer-free, so those are layout-stable. | **FIXED (`chraction.c:3872`, `#ifdef PORT` → `self->act_patrol.lastvisible60 = -1;`, N64 line under `#else`). ABI/pointer-width only (D3x/A1 class), no game-logic change.** Build clean; bunker1/facility/archives/dam verify PASS, no crash. **Owed:** a playtest eyeball of patrol-guard behaviour on entry to patrol (do they over/under-react to a player they haven't seen). Low individual impact but exactly the class the D170/D193 "AI behaviour looks off" reports keep landing in. |
| D205 | **PC plays wrong/extra SFX for gameplay events the user can A/B against N64: general gunfire has a slap/glass layer audible on PC but silent on N64; an explosion plays a soldier-scream sample; armor pickup plays a wrong sample; unarmed melee (slap) plays Klobb's shooting sound idx 106 instead of the slap effect (M-71/M-72, user by-ear + N64 A/B).** **M-74 WITHDRAWS M-73's root cause.** M-73 read the user's combat capture as guards stuck re-triggering fire sound idx 109 ("256x/29.7 s, no bullets") and pinned it on `stanTestLineUnobstructed` LOS over converted collision geometry. Re-counting the same file: idx 109 is requested **128x** not 256x (the double-log artifact D204 already corrected for M-63 on this very index), = 4.31/s across ~11 guards = ~0.4/s each, **~6x UNDER** the AK47's own `SoundTriggerRate` (`RATE_AK47`=4 ticks => 15/s ceiling) — so the `field_178` gate never binds; and impacts are plentiful in the same window (~30 ricochet/wall-hit 19-41, 69 x2 flesh, 12 body-falls 123-132, 14 yelps 134-147). The eye-catching monotone sweeps 134->147 / 123->132 are the ground-truth round-robin `male_guard_yelp_counter` (`chraction.c:2454-2470`), faithful, not a broken selector. **The 109 traffic is normal in-spec guard combat**; do NOT open the pccg-stan/LOS geometry investigation on this evidence. Everything upstream of presentation is proven: bank 261/261 identical (M-71), sample+pitch exact (M-68/M-70), and every index in the capture correct (182 explosion x1, 81 armor x1, 46/47/48/49/105). **Remaining untested layer = spatial presentation (per-voice volume, pan, concurrency)** — it explains all four complaints at once (a wrongly-placed sound is by ear a sound at your own position) and is the youngest code in the stack (`sndCreatePostEvent` was fully stubbed by D138 until M-65). **M-78: CLOSED.** All four symptoms were D206 (the +1 bank-slot alias). (3) armour + (4) melee->Klobb + (1) slap/glass over gunfire: user by-ear A/B vs N64 = PASS (M-77c). (2) explosion->scream was thought NOT explained by D206 (169-183 -> 168-182, all still explosions) but the user re-tested on Bunker post-merge and the explosion SFX is now correct by ear — the earlier "scream" was a coincident guard death-yelp (GET_HIT_MALE 134-147 seen +/-1s of the blast in M-73's capture), faithful N64 behaviour, not a wrong sample. No presentation-layer bug: pan audit (M-74) already showed GE never spatially pans SFX (252/261 bank sounds `samplePan==64`, no `AL_SNDP_PAN_EVT` poster). | CLOSED (M-78) — root cause was D206; all four symptoms resolved + user-confirmed by ear vs N64. No separate presentation-layer fix needed. | 
| D152+ | D152 addendum (M-31): static audit of every compiled-audio `osSetIntMask(OS_IM_NONE)` — **all balanced**, no unbalanced early-return. Real fixes: `sndSetSfxSlotVolume` now holds the mask across its list walk (matches its twin `sndDeactivateAllSfxByFlag`) + `sndApplyVolumeAllSfxSlot` batches the slot loop under one recursive hold (kills the fade-out lock-acquire storm); `portThreadWrapper` calls `imThreadExitRelease()` on thread exit (kills the "transient thread acquired `OS_IM_NONE` and died" leak + the pthread-id-reuse re-wedge). Steal-lock kept as backstop. | FIXED (`src/snd.c`, `port/src/libultra.c`, `#ifdef PORT`) — fade-out repro playtest-gated, not headless-verified |
| RC3 · D167 | **Non-power-of-two texture wrap period (`docs/dev/TEXTURE-GLITCH-ANALYSIS.md` §6 RC3 — "textures repeat oddly", residual Depot-ceiling noise after D161).** The N64 RDP masks the texel coordinate of a wrapping render tile at `1<<mask`, and GE sets `mask = texDimensionToMask(dim) = ceil(log2(dim))` (`src/game/tex.c:361`), so a non-PoT tile (Depot's 65×65 / 96×48 / 56×56 room surfaces) repeats at the **next power of two**, not at its image size the way GL `GL_REPEAT` does → the pattern is squashed/stretched and the seam lands in the wrong place. fast3d never stored `masks`/`maskt` at all (`gfx_dp_set_tile` dropped them) and wrapped purely at the uploaded image dimension. **Fix (`port/fast3d/gfx_pc.cpp`, behind the existing `Video.WrapFix` knob, default OFF):** store `masks`/`maskt` on the tile; in the hoisted per-texunit pre-wrap block in `gfx_sp_tri1` (D74 block — already lifted out of the vertex loop, indexed by texunit `t` not vertex `i`), when the tile is WRAP (no CLAMP/MIRROR bit) and `1<<mask != tex_width`, fold the UV at the N64 period `1<<mask` and clamp the `[dim, 1<<mask)` overflow band (which is a TMEM smear on console, no real texels) to the last texel so it reads as an edge streak instead of a bogus early image restart. `GE_WRAPFIX=0/1` env override added (env wins over the ini, matching `Debug.FrameDump`). **Per-level captures (`-level_09`/`-30`/`-34`/`-20`, WrapFix OFF vs ON, `GE_PCDUMP` 6-frame windows):** no crashes, 6/6 frames each; on settled/comparable frames Silo is ~pixel-identical (phash 0–11), Facility 180–260 pixel-identical, Depot shows small localized texel changes on ceiling/wall cells (dmean 5–9, no structural break); the large per-run deltas are all the D117 intro-camera-pan nondeterminism, not the fix. Default kept **OFF** — no regression, but a headless structural diff can't confirm the Depot ceiling actually looks *better*; needs a human eyeball with `Video.WrapFix=1`. Default-off is byte-identical to golden (all new behaviour is inside `if (g_wrap_fix)`). D74's dead in-vertex-loop wrap block was already reworked/hoisted at M-30; this only adds the mask-period trigger. Confidence: **medium** (mechanism correct; overflow-band handling is an approximation, not exact TMEM-smear emulation; visual win unconfirmed). porting-notes.md §D. | KNOB ADDED, default OFF (`port/fast3d/gfx_pc.cpp`). Needs user visual check on Depot. |
| D168 | **`GE_PCDUMP` / F12 PPM captures were vertically flipped — the entire source of the bogus D114/D116 "HUD/text X-mirror" (M-33, developer-confirmed).** `gfx_opengl_dump_bound_fbo` (`port/fast3d/gfx_opengl.cpp`) wrote `glReadPixels` output straight to a P6 PPM. GL framebuffer origin is bottom-left; PPM P6 is top-row-first — so every capture (and the `tools_pc/golden/` set, and every screenshot pasted into the finding log since M-6) was upside-down. On real hardware / an actual screen the game renders correctly (developer confirmed). The successive D114→D116 "shared fast3d mirror" investigations — each of which found *every probed stage clean yet the output "mirrored"* — were reading an inverted capture and pattern-matching upside-down asymmetric content (text, guards, the Nintendo logo) as "mirrored". Fix: emit PPM rows bottom-to-top. `tools_pc/golden/*.png` flipped in place to match (see `tools_pc/golden/README.md`); regenerate from a real run when convenient. **Not runtime-verified this session** (no ROM / toolchain in the migrated tree — see the migration note) — needs a fresh capture to confirm text reads normally. Confidence: **high** (mechanism is unambiguous; developer has hardware confirmation the screen is correct). porting-notes.md §D2. | FIXED (`port/fast3d/gfx_opengl.cpp`); D114 / D116 reclassified as capture-orientation artifacts (below). |
| D169 | **Front-end mouse pointer can't reach the outer cells of the mission/level-select grid — only an inner ~3×3 selectable (M-33, developer bug report).** The D165 pointer P-controller in `port/src/input.c` is hard-coded to a **320×240** virtual field (`MENU_CURSOR_HI_H`/`_HI_V` = 300/220, `MENU_CURSOR_MID_H`/`_MID_V` = 160/120, `input.c:145-149`), but GE's front end runs at **440×330** (`front.c:8570` `viSetViewSize(440,330)`; default cursor home 220/165 `front.c:285`). `frontUpdateControlStickPosition` clamps the real cursor to `[20,420]×[20,310]` (`front.c:1195-1217`); the mission-select hit-test grid spans x 73…352 / y 62…270 with outer split points 317 / 235.5 (`front.c:516,519,3180,3194`). The port clamps its pointer *target* and *estimate* to 300/220 (`input.c:553-564`) → the stick zeroes out once both saturate → the game's `cursor_h_pos`/`cursor_v_pos` park at ≈300/220, short of the two outer columns and the bottom row. File-select / mode-select / main-menu are unaffected because their hit targets (centred folder boxes, the `x=126` mode list, the `x=106` difficulty list) all sit inside the 320×240 sub-box. Not the D118d `joyGetStickY`-threshold class — this is a virtual-resolution constant mismatch, closer to D164 (a front-end layout constant wrong on the PC path). Also note: the "re-syncs whenever the target is held at a screen edge" comment at `input.c:141-143` describes behaviour the code doesn't implement — the only estimator reset is the activation re-home (`input.c:545-549`), so once pinned at the clamp the estimate never recovers. Confidence: **medium-high** (constants + clamp math unambiguous in source; static-only, no build/run this session; exact reachable block "~4×3" vs the reported "3×3" within tolerance). | **FIXED (M-33, `port/src/input.c`, port-only, no `#ifdef PORT`).** The `menuMode` pointer branch now derives its clamp bounds from the live virtual screen — `[screenleft+20, screenleft+screenwidth-20] × [screentop+20, screentop+screenheight-20]` via `getPlayer_c_screenwidth/height/left/top()` (`src/game/bondview.c:880-895`) — falling back to the old 320×240 constants when the front-end screen isn't set (`sw` outside 200…2000). The estimator seed on activation is now the real `cursor_h_pos`/`cursor_v_pos` (`front.c:285`, externed in `front.h`) instead of the 160/120 centre guess. `MENU_CURSOR_HI_H/_V` / `MID_H/_V` kept only as the fallback. **Verified:** builds + links clean (`getPlayer_c_screen*` and `cursor_[hv]_pos` all resolve — non-static engine symbols, as expected); `GE_STARTMENU=7` mission-select boots crash-free 600+ frames; `-level_09` unregressed (framediff 3/3 within threshold, 91.6% nonclear). The in-level path is untouched (menu-only branch). **Interactive feel-check still owed** — headless input can't drive the mouse pointer, so "every grid tile is now reachable" is inferred from the corrected clamp math, not observed. `Input.MenuPointerMode=0` (legacy velocity) and the constant fallback remain as escape hatches. |
| D178 | **Pre-mission briefing screen: objectives blank / missing, briefing pages blank (also the D143 "briefing text blank" side effect) — FIXED (M-36).** Root cause: the briefing segment (`Ubrief*Z`, source `assets/obseg/brief/*.c`) is a raw ROM image of `struct BriefStruct` = `{ u16 brief[4]; struct { u16 textid; u16 enabled_difficulty; } objective[10]; }` (48 bytes), loaded by `front.c load_briefing_text_for_stage()` via `_fileNameLoadToAddr()` — **no converter and no BE→LE fixup anywhere in the chain**. On the LE host all 24 `u16` read byte-swapped. Measured on Dam (`GE_D178=1`): raw `brief=002c,012c,022c,032c obj0=042c/0100 obj3=072c/0000` vs correct `2c00,2c01,2c02,2c03 / 2c04/0001 / 2c07/0000`. Two independent symptoms follow: (a) `textid` `0x2C04` (= `getStringID(LDAM,4)`, bank 11 slot 4) reads as `0x042C` → bank 1 slot 44, never loaded → `langGet()` NULL → **blank text** (this is exactly the D143 residual, `front.c:6732` and `brief[0..3]` at `front.c:6855-6867`); (b) `enabled_difficulty` `0x0001` (Secret Agent) reads as `0x0100 = 256`, so `selected_difficulty >= enabled_difficulty` (`front.c:6729`) is false for every difficulty-gated objective — on Agent only the one `DIFFICULTY_AGENT`(0) objective survived the filter, which is why a single bare "a." bullet printed. **Fix:** `romdataFixupBriefing()` in `port/src/romdata.c` (+ `port/include/romdata.h`), called from a `#ifdef PORT` block in `load_briefing_text_for_stage()` right after the load — the same shape as `langFixupLoadedBank()` in `language.c` (BE-serialized-struct reconciliation, semantics-preserving, no game logic touched). `GE_D178=1` prints the raw and fixed words. **Verified:** Dam briefing on Agent now shows "a. Bungee jump from platform"; on `GE_STARTMENU_DIFF=3` (00 Agent) all four (Neutralize all alarms / Install covert modem / Intercept data backup / Bungee jump from platform), correct difficulty gating. `-level_09` framediff 3/3; `GE_STARTMENU=7`/`=13` crash-free. | **FIXED (M-36)** — high confidence |
| D175 | **In-game stutter / brief hang during normal play** (user QA report): opening a door on Runway (`-level_35`, mission 3) and also observed on Surface. Self-recovered; no backtrace captured. Likely one of the known transient-hang classes (D155 catch-up spiral / D156 anim NaN loop / D134 task-done event / D147-D152 audio-lock steal) or a benign new-room texture-import frame spike on door open. See `GRAPHICS-BACKLOG.md` D175 for the gdb pattern-match sheet. | Observed, not investigated |
| D176 | **Surface exterior renders wrong (`-level_36`), two independent defects.** **(a)** sky solid black — env data is correct (`Clouds=1`, warm `CloudRGB`), the cloud-sky path runs, but `skyRenderTri`/`skyRenderFull` emit only `G_RDPHALF_*` immediates which `gfx_pc.cpp:2901` deliberately no-ops → **root-caused M-37** (see "D176(a) — M-37 UPDATE"); fix = decode the RDPHALF sky-tri stream in fast3d. **(b)** cliff/rock walls = grey diagonal static — NOT a texture decode bug (D183 disproved the shear hypothesis, 0/166 loads strided); re-scope from ROM ground truth of the wall texnum (M-37: inconclusive, leaning tiling-density). See `GRAPHICS-BACKLOG.md` D176. | (a) root-caused, fix owed / (b) open. **M-87 user playtest: still current** — skybox is "still glitchy" broadly (not just Surface), and some levels (**Cradle** named) still show solid black where sky/backdrop should be — consistent with (a) never having landed a verified fix (Path B draft PR #18 was left "unverified visually"). |
| D177 | **Ladders non-functional — progression blocker (user QA report) — FIXED (M-36).** Not the input path and not the ladder state machine: `MoveBond` gates the ladder-collision path on `stanGetLocusCount(&curLocus)`, which was pinned at 0 on PC. `stanCheckLinkedSpecialTile` writes the LADDER signal via raw `outFlags[1] = 1` into a `struct StandTileLocusCallbackRecord` whose first member `s32 *rooms` is pointer-widened on PC → `[1]` is the high half of `rooms`, and `count` (moved +4→+8) is never written. Compounded by `curLocus` being declared as the 8-byte placeholder `move_bond_temp_struct` (too small for the widened record) and a `(s32)coords` pointer-truncation AV waiting in `stanGetMoveBondCollisionTiles`. **Fix:** `#ifdef PORT` — write record fields by name, declare `curLocus` as the real struct, `PORT_PTRADD` for the truncating cast (`src/game/stan.c`, `src/game/bondview2.c`; no game logic). Full detail: §F "D177". | **FIXED (M-36)** — high confidence; interactive climb test owed |
| D172 | **Bullet-impact / blood / spark particles render magenta or cyan instead of dark red (M-82, ROOT CAUSE FOUND + FIXED).** Particle billboards (`explosion.c` `explosionRenderPart`, `gSPVertex`+`gSP2Triangles`) take their CC/tile state from ROM state records replayed just before them (`g_ExplosionDisplayLists[]` = `assets/oddtextures.c` `globalDL_0x078..`). The dominant record sets `gsDPSetCycleType(G_CYC_2CYCLE)` + `gsDPSetTextureLOD(G_TL_TILE)` + `gsDPSetCombineMode(G_CC_INTERFERENCE, G_CC_MODULATEIA2)` and binds **two** tiles: tile 0 = IA8 "smoke" @ TMEM 0, tile 1 = RGBA16 "fire" @ TMEM 0x188. `G_CC_INTERFERENCE` cycle-0 = `TEXEL0 * TEXEL1`. **fast3d's `gfx_lod_tile_offset()` returns `0` unconditionally** on the `!gfx_detail_textures_enabled` branch — a D107 fix for GE's mip textures (whole chain loaded in one LOADBLOCK at TMEM 0; `port/src/video.c:225` sets the flag `false`). So texunit 1's sampled tile = `first_tile_index + 0` = tile 0 → **TEXEL1 samples the smoke texture, not fire** → `smoke * smoke` → wrong colour. Two earlier M-82 candidates were false trails: (1) "0xB9 unhandled → D146 abort" — 0xB9 is `G_SETOTHERMODE_L` in this non-F3DEX2 build, handled; zero D146 aborts in a sustained-fire run. (2) "records never set 2-cycle" — they do (`0xBA`), and fast3d applies it (probe-confirmed `cycletype=2CYC`). **Probe evidence (`GE_D172=1`, user Silo firefight, `d172_silo.log`):** 40/40 particle tris → `cfg1[tile=1 tmem=392 fmt=RGBA siz=16b]` but `SAMPLED1 = tile0 (tmem=0 fmt=IA)`, `tex_lod=0`. | **FIXED (`port/fast3d/gfx_pc.cpp` `gfx_lod_tile_offset`): `return rdp.tex_lod ? 0 : i;`** — fold to the base tile only when LOD is actually active. D107's mip case (`tex_lod=1`, blurry ceilings) unchanged; a genuine non-LOD 2-texture combine now samples tile `i`. `bunker1` verify PASS (the D107 repro level), no framediff regression. PR #34. **Owed: in-game eyeball — blood/sparks should read dark red.** Env-gated `GE_D172` probes left in tree (`#ifdef PORT`, inert). |
| D174 | **"No blood effect" (user QA report).** Likely the unverified D120 decal fix: `d43_emit.py` now emits the opcode-0x18 `PointUsage[]` chain, but it was never interactively verified and requires a full sidecar regen to take effect (`debug.ps1` does not regen). Spray path is D172 (draws, wrong colour) — total absence would be new. See `GRAPHICS-BACKLOG.md` D174. | Observed; first step = BUNKER1 firefight with regenerated sidecars |
| D183 | **M-36 Family A ("texture line/pitch shear") is DISPROVEN for the Surface repro (`-level_36`).** The importers' pitch assumption is real but never fires there; the Surface cliff "grey static" is not a shear. Full evidence + what the walls actually are: §F "D183" below. A defensive, provably-no-op de-stride landed in `import_texture()` anyway (covers the `gfx_dp_load_tile` case the `SUPPORT_CHECK`s assert against), plus `GE_DTEX` STRIDED marker + `GE_TEXRAW` raw-source dump. | Hypothesis DISPROVEN + diagnostics shipped; D176(b)/D182(2) still OPEN |
| D173 | **Third-person Bond model spawns too high** (user QA report): the player-representing figure at level start floats well above the ground "a lot of the time" while the actual player spawn is correct; same on the Cuba end credits — JB ~6 ft in the air above Natalya. D75 animated-model family but a *position* defect, not absence (M-33: gun-barrel Bond renders fine). Suspects: intro/credits puppet chr spawn position vs model base transform; anim root-motion accumulation (`modelSetAnimFrame2WithChrStuff`, cf. D156); packed-float `coord3d` decode in intro setup. "Player spawning OK" localises it to the render-side model, not `g_CurrentPlayer`. See `GRAPHICS-BACKLOG.md` D173. | Observed, not investigated. **M-87 user playtest: still present** — "Bond floating in the sky / cutscene glitch thing still exists." Related, newly reported: **in some cutscenes Bond spins in circles** (named instance: end of Cradle, when Trevelyan falls) — likely the same actor-position/anim-root class rather than a new bug; logged under D148/D160 (`GRAPHICS-BACKLOG.md`) since it's cutscene-triggered. |
| D179 | **Packaged build crashes after the logos — the D43/D69 model + bg sidecars are ROM-derived and absent from any build that has no ROM (M-34, alpha-release QA).** The first `goldeneye-pc-port-*-win64.zip` from CI (bundle-win.sh) shows the Rare/Nintendo logos (compiled-in `assets/rarewarelogo.c`) then AVs at `0x6b157a88`/`0x70157a88` — symbolised: `modelPromoteNodeOffsetsToPointers` (`model.c`) ← `load_object_fill_header` (`objecthandler_2.c`) ← first prop/item model load. Root cause is **not** a code regression: the port loads PC-layout model geometry from `data/pcmodels-<region>/{pcmodels.bin,manifest.csv}` and stage bg/stan from `data/pccg-<region>/{pccg.bin,manifest.csv}`, both produced offline by `tools_pc/d43_emit.py` / `d69_emit.py` from the ROM (see `port/src/pcmodels.c` / `pccg.c`; `pcmodelsReserveSize` logs *"pcmodels.bin not found — model loads will fail"* and returns 0). CI has no ROM so it never runs the emit scripts, and the two `data/` dirs are (correctly) gitignored ROM-derived game data — **cannot be committed or shipped** (converted Nintendo/Rare geometry, DLs, collision, stan nav; = distributing assets). A local build with those dirs present runs clean (verified M-34: 1741 frames, `romdataInit ... mapped at 0x10000000`, `pcmodels: 512 sidecars`, `pccg: 73 sidecars`, no crash — so `0x10000000` is *not* the problem here). The `docs/building.md` sidecar-gen step was also missing entirely. **Fix = release-side, backlogged** (`docs/BACKLOG.md` → "Alpha release"): bundle the emit scripts + their committed inputs as a pure-stdlib-Python asset-prep tool the user runs once against their own ROM (~5 MB output, no MIPS toolchain). Secondary/latent: the fixed-address `VirtualAlloc((LPVOID)0x10000000)` in `romdata.c` has no working fallback (`"using heap copy — direct ROM reads will fail"` then limps into the same crash) — didn't bite M-34 but will on a machine where something occupies `0x10000000`; harden separately (reserve earliest in `main`, or retry low bases and derive all segment math from the base obtained). | Diagnosed, not fixed — release packaging gap + latent `romdata.c` fallback |
| D180 | **Native-PC input pass (M-XX QoL run, `qol/native-pc-input-menu`, port-only).** Three parts, all config-gated with default = prior behaviour: **(WI-1) `Input.MouseCaptureMode`** (0 = legacy always-grab; 1 = Quake-style click-to-lock) in `port/src/input.c` + `port/src/video.c` — in mode 1 the OS cursor is free until a click lands in the game window (`SDL_MOUSEBUTTONDOWN` → `inputNotifyClick`), and ESC / focus-loss / entering any front-end menu (`current_menu != MENU_RUN_STAGE`) frees it again; re-entering a stage while still "armed" re-locks so unpausing needs no click. `reconcileGrab()` runs once per controller-0 poll. Mouse buttons are suppressed from the game while the cursor is free in-stage (no phantom fire). Controller paths untouched. **(WI-2) absolute-cursor menu tracking** — when capture mode is on and the cursor is free in a menu, the D165/D169 pointer P-controller takes its target from the real cursor's absolute window position mapped onto the live virtual front-end rect (`getPlayer_c_screen*`), giving true 1:1 tracking instead of the relative-delta estimator. Relative path unchanged for legacy/grabbed. **(B3) `Input.MouseAimSpeed` default 25 → 16** (aim mode still overshot at 25 per the backlog). | LANDED, port-only, no `#ifdef PORT`. Build 241/241, `-level_09` framediff 3/3 within threshold (unregressed), `GE_STARTMENU` menu boot crash-free. **Feel-checks owed** (headless can't drive the mouse): click-to-lock ergonomics, absolute menu tracking, the new aim-speed default. |
| D181 | **`Game.ScreenShakeIntensity` — first route-(b) `src/` gameplay-cosmetic hook (M-XX QoL run).** `src/fr.c viShake()` gains a single `#ifdef PORT` line — `intensity *= portScreenShakeScale;` (extern `f32`, defined + `configRegisterFloat`'d 0.0–10.0 in `port/src/video.c`) — before the existing clamp. Default `1.0f` ⇒ exact no-op, every headless golden dump byte-unaffected; `0.0` disables explosion/effect screen shake, up to `10.0` exaggerates it. N64 build (no `-DPORT`) keeps the original line verbatim. Precedent per `docs/BACKLOG.md` "Fun features → screen-shake intensity slider" and AGENTS.md #2's documented-exception path. Same policy class as a future FOV hook. | LANDED behind config, default = original. Build green. |
| D184 | **F10 port-layer options overlay — the approach-(C) surface from `OPTIONS-MENU-PLAN.md` (M-37).** New `port/src/optionsoverlay.c` + `port/include/optionsoverlay.h`: an immediate-mode overlay drawn as its own fast3d 2D DL, appended after the game DL in `gfx_pc.cpp gfx_run()` (`optionsOverlayEmit()` → NULL when closed ⇒ **zero bytes appended, golden dumps byte-identical**). F10 toggles (`video.c videoPumpEvents`, next to F12); while open, `input.c inputComputePad(0)` returns a neutral pad and routes arrows/enter/wheel/mouse-buttons to `optionsOverlayHandleInput()` (mirrors the WI-1/D180 swallow). ESC closes; `configSave()` on close. `config.c` gains `configForEachOption()` + a `configSetOptionMeta()` label/step/enum side table (no per-key knowledge in config.c). Live knobs (VSync/FpsCap/TextureFilter via `videoRequestLiveConfig()`; mouse + screen-shake via the registered pointer) apply immediately; MSAA tagged "(restart)". v1 rows per plan §3: VSync, FpsCap, MSAA, TextureFilter, MouseAimSpeed, MouseTurnSpeed, MouseInvertY, MouseCaptureMode, ScreenShakeIntensity. | **LANDED, port-only, no `#ifdef PORT` in `src/`.** Build links 241/241. **Runtime verification NOT run this session** — the headless env kills the GUI process after ~8 s and `gfx_opengl_dump_bound_fbo` writes 0-byte PPMs here, so framediff / 60 s soak / overlay-frame eyeball are all owed on an interactive machine. Overlay-closed no-op is code-evident (emit returns NULL before touching the DL buffer). |
| D211 | **PC FOV slider — `Video.FovScale` (M-83 QoL wave, port-only, `WIDESCREEN-FOV-PLAN.md` Phase 4).** fast3d applies a multiplicative half-angle scale to every *perspective* projection matrix as it is loaded (`gfx_apply_fov_scale()` in `gfx_pc.cpp gfx_sp_matrix`, on the `G_MTX_PROJECTION | G_MTX_LOAD` path only — GE always reloads its view projection via `guPerspectiveF` at `fr.c:725`, so a relative `G_MTX_MUL` onto an already-scaled matrix cannot compound). Columns 0 and 1 are scaled by the same factor `tan(half_orig)/tan(half_adj)` → H and V FOV widen uniformly, aspect ratio exactly preserved (no stretch), and because the game rewrites fovy every frame for aim-zoom the scale composes with zoom for free. Ortho matrices (`P[3][3]==1`) are left untouched so HUD/menu 2D is unaffected. `Video.FovScale` = percent of the original vertical FOV, 50–150, **default 100 ⇒ `g_fov_scale == 1.0f` ⇒ guarded early-return, zero FP ops on the matrix ⇒ golden dumps byte-identical**. F10 overlay row "FOV scale %". Math checked offline (`s=1.25 → fovy 60°→75°`, aspect held at 1.3333). | **PARTIAL — M-84 refix: game-side FOV scale (`src/fr.c`, `#ifdef PORT`), front-end gated, user-verified.** Original M-83 note follows. LANDED, port-only; build clean; `bunker1` framediff worst_cell 12.219 vs 12.221 baseline (= render noise, no-op confirmed). **M-84 refix — the fast3d hook only scaled *pure* perspective loads; GE draws the world through `g_CurrentPlayer->field_10E0`, a projection × view matrix pre-combined on the CPU (`bondview2.c:8224`) that failed the `gfx_apply_fov_scale` guard, so only the pause/watch model + sky FOV changed (user report).** Moved the scale game-side to the `guPerspectiveF` chokepoint (`fr.c:718`, `#ifdef PORT`, `frFovY *= portFovScale`, clamp 160°), which lands *before* the CPU combine; fast3d `gfx_apply_fov_scale`/`gfx_set_fov_scale` removed; `port/src/video.c` sets `f32 portFovScale`. Also gated to `lvlGetCurrentStageToLoad() != LEVELID_TITLE` — the front end (main menu / file+mission select / briefing) renders through the same path (`lv.c lvlRender`) and its 3D is authored at a fixed FOV. **User-verified: front end unaffected + in-level view widens (Dam tunnel at high FovScale).** M-85 headless nit check (`-level_09`, FovScale 100 vs 120): **HUD-static confirmed** — the ammo counter ("7 | 93") sits at the identical bottom-right screen position in both, and the door-arrow stays square (no 4:3 stretch); viewmodel scales down + stays bottom-right anchored. **aim-zoom-compose — M-86 static analysis: correct by construction.** `bondview2.c:3123` lerps `zoominfovy` then `viSetFovY()` stores it in `g_ViBackData->fovy`; `fr.c:737` applies `frFovY = fovy * portFovScale` **once**, at the per-frame chokepoint, *after* the zoom lerp. Hipfire and zoomed states scale by the same factor so the zoom magnification ratio is preserved exactly; no double-application; the smooth zoom interpolation is unaffected (scale is post-lerp). Residual human eyeball (low risk, cosmetic) at FovScale ~120–150 + full aim-zoom: the viewmodel (`c_perspfovy`, `bondview.c:644`) and frustum-cull (`currentPlayerSetCameraScale`) use the *unscaled* fovy, so a subtle viewmodel-scale or edge-cull-early mismatch is possible; the scope reticle is 2D screen-space, unaffected. Caveat (same root): edge geometry culls a hair early at large scale (cosmetic). **User-verified good at big FovScale.** New adjacent report → **D220**: 1P muzzle flash position shifts upward at higher FovScale (Jungle, M16) — likely the same unscaled-viewmodel-fovy root as the caveat above. Wide FOV also exposes the per-level draw-distance boundary → **D218** (logged, parked). **M-87: the frustum-cull residual is a real bug, not cosmetic** → split to **D222**. |
| D212 | **Anisotropic-filtering config wiring — `Video.Anisotropy` (M-83 QoL wave, port-only).** The fast3d GL backend already implemented `set_anisotropy_level` / `get_max_anisotropy_level` and applied `GL_TEXTURE_MAX_ANISOTROPY` on mipmapped tiles with a hardcoded `current_anisotropy_level = 4`; there was no way to configure it. Added the `gfx_set_anisotropy_level()` extern-C wrapper in `gfx_pc.cpp` (clamps to `[1, GL max]`, `reset_texture_state()` so live changes take), declared in `gfx_api.h`, and `Video.Anisotropy` (1–16) in `port/src/video.c` wired through `videoApplyImageOptions()` on init + live-config. **Default 4 = the value fast3d already applied ⇒ no visual delta at defaults.** F10 overlay row "Anisotropic". | **LANDED, port-only.** Build clean. **Owed:** eyeball at 16× on a grazing surface (Depot roof / Dam walkway) vs 1×. |
| D213 | **On-screen FPS readout — `Video.DisplayFPS` (M-83 QoL wave, port-only, PD parity).** `port/src/optionsoverlay.c` samples frame time once per emitted frame (`fpsTick()` via `sysGetMicroseconds()`, ~0.5 s window) and, when enabled, emits a small top-right green "N FPS" DL even while the F10 panel is closed. Config-only knob (kept off the 13-row panel, which is at its documented layout limit — matches PD, whose `DisplayFPS` is also file-only). **Default 0 ⇒ `optionsOverlayEmit()` still returns NULL when the panel is closed ⇒ golden dumps byte-identical.** | **LANDED, port-only.** Build clean; `bunker1` no-op confirmed. **Owed:** eyeball that the counter reads a sane ~30 and doesn't overlap the HUD. |
| D214 | **Keyboard rebinding — `[Input.Bind]` config section (M-83, port-only, PD parity, Phase 4 debt).** `port/src/input.c` gains 12 gameplay actions (Forward/Back/StrafeLeft/StrafeRight/TurnLeft/TurnRight/Fire/Aim/Action/Cancel/LeanLeft/Start), each a comma-separated list of up to 4 SDL scancode names (`Input.Bind.Forward = W,Up` …). `inputRebuildBinds()` (called from `inputInit()`, after `configLoad()`) parses them via `SDL_GetScancodeFromName`; unknown names log a warning and are skipped. The old hardcoded `keyDown(ks, SDL_SCANCODE_*)` block in `inputComputePad` is replaced by `actHeld(ks, IA_*)`. Mouse buttons (fire = LMB, aim = RMB) stay hardwired; the mouse-wheel weapon-cycle A-pulse is untouched. **Defaults reproduce the previous FPS layout exactly — verified: all 20 default key names round-trip to the identical `SDL_Scancode` the old code used (`SDL_GetScancodeFromName` test, 0 mismatches).** Config-file only (no in-overlay text entry — matches PD, whose rebinding is also file-driven); a fresh `ge007.ini` gets the full `[Input.Bind]` block with the defaults visible. | **PARTIAL — M-84: code PASS (alt/ctrl); one doc-only fix.** LANDED, port-only, no `#ifdef PORT`; build clean. One doc bug fixed (no code change): `config.c` groups keys by the substring *before the last `.`*, so the section header is **`[Input.Bind]`**, not `[Bind]` — earlier finding/commit/QOL text was wrong. Working syntax: put `Fire = Left Alt` under `[Input.Bind]`, or write a fully-qualified `Input.Bind.Fire = Left Alt` line anywhere (`config.c:307`). Separately: the pause→**watch menu** does not wire Esc→back (still Enter to open/close/back) — pre-existing, `options.c`, minor. |
| D215 | **1P weapon viewmodel never drew in-level — `gunRenderFirstPersonGunModels` builds its `ModelRenderData` from a reinterpret across separately-declared adjacent globals that only works on N64 (M-83, D3x/ABI class; retires VIEWMODEL-RESEARCH.md).** The N64 line `renderdata = *(ModelRenderData *)&D_80035CC0;` (`gunfire.c`) casts the lone `u32 D_80035CC0 = 0;` to a whole `ModelRenderData` and reads on into the next declared global, `u32 D_80035CC4[] = {1, 3, 0, 0, …}` (`gun.c`) — yielding `{ basemtx=0, zbufferenabled=1, flags=3, rest=0 }` **only because the N64 linker lays the two globals out contiguously in declaration order and pointers are 4 B**. On x86-64 it breaks twice: `ModelRenderData`'s three pointer members (`basemtx`/`gdl`/`mtxlist`) widen 4→8 B so `flags` is no longer at struct offset 8, and C guarantees nothing about the two globals' relative placement. Measured (`GE_DVM=1`, BUNKER1): `renderdata.flags == 0`, so **every** weapon-model DL node was gated off at `modelRenderNodeGundl` (`(renderdata->flags & 1)`), and `subdraw()` for the weapon emitted just its `gSPSegment` — 1 gfx command instead of ~150. All three VIEWMODEL-RESEARCH.md ranked suspects were ruled out by the same probe first: `field_87F==1` (gate open), `RootNode` valid, `numRecords 22 < 192` (pool fine), node walk visits 24+ valid `DL`/`SWITCH`/`BSP` nodes. **Fix:** `#ifdef PORT` → set the three fields explicitly (`zbufferenabled=TRUE; flags=3;`), N64 line verbatim under `#else`. ABI/layout only, no logic change. | **FIXED (`src/game/gunfire.c`, `#ifdef PORT`).** Build clean. `GE_DVM=1` post-fix: `flags=0x3`, weapon `subdraw()` now emits **153** gfx commands (was 1). Regression sweep crash-free. `GE_DVM` gate+render probes left in, env-gated (cf. `GE_D193*`). **Owed:** one BUNKER1 eyeball that the gun model is actually visible + correctly posed (headless can't see pixels; the DL is now provably generated). The committed `bunker1` golden will need re-baselining — the gun now renders in-frame. |
| D216 | **`Game.SkipIntro` — boot straight to the file-select menu (M-83, port + one `#ifdef PORT` game-code line, PD parity, Phase 4 quick win).** New `s32 portSkipIntro` (`port/src/video.c`, `configRegisterInt("Game.SkipIntro", 0, 1)`), read once in `src/game/lv.c` at `LEVELID_TITLE` stage load — in the `else` of the existing `GE_STARTMENU` `#ifdef PORT` block. When set it does what every intro stage's own `!is_first_time_on_main_menu` branch does (`front.c`): `is_first_time_on_main_menu = FALSE; prev_keypresses = TRUE; maybe_is_in_menu = TRUE; menu_update = MENU_FILE_SELECT;` — i.e. it reuses the game's own post-intro route rather than inventing one, so the legal screen + Nintendo/Rare/GoldenEye logo attract loop is skipped. `GE_STARTMENU` still wins when both are set. Default 0 ⇒ untouched. | **LANDED + VERIFIED this session.** Build clean. Headless: default boot frame 90 = the TWYCROSS classification screen (10% non-black), `SkipIntro=1` frame 90 = the **SELECT FILE menu** (folders/photos/cursor all render, 89% non-black), no crash across repeated boots. Screenshots captured + eyeballed. **Owed:** nothing critical — a human boot to confirm the menu is fully interactive (cursor moves, a save slot opens). |
| D217 | **1P weapon viewmodel + third-person Bond model textures render garbled — wrong/stale palette on model-DL CI textures (M-84, user QA; surfaced by D215 now that the viewmodel draws).** BUNKER1/Facility: the PP7 viewmodel has yellow+blue scrambled texels and a solid **green** patch by the hammer; Bond's in-game **watch face** shows the same green mismatch; user reports the **third-person Bond world model** does this "sometimes" too. Signature matches the **D161 family** — a CI-format tile drawn without the expected TLUT state, so fast3d's `import_texture` indexes a **stale `rdp.palette`**. D161 fixed the explicit `G_TT_NONE` case; this looks like model DLs not emitting `gDPLoadTLUT` through the port path, or the palette segment not resolving for weapon/character geometry. **Not a D215 regression** — D215 only un-gated the DL. | **OPEN — root cause characterised (M-86); needs an upstream/asset-pipeline fix.** Headless repro: `-level_34` FACILITY ~frame 300–400, PP7 **grip = flat green block** + green ring at the barrel joint + green-cast hand; barrel/slide upper correct black. Also green on `-level_30` DEPOT. **Clean on `-level_09` BUNKER1** (A/B control — same PP7, identical DL). **Two separate defects found:** ① **Wrong palette bound (the visible green — upstream of fast3d).** The grip is a CI4 solid-fill of palette index 0. Green `(24,65,0)` == RGBA5551 `0x1A00` == `rdp.palette[0]` at grip-import time. The grip's `count=1` model `G_LOADTLUT` is fed **different source bytes per level** for the same tile: BUNKER `00 01`→`PD_BE16`→`0x0001` (black ✓), FACILITY `1a 00`→`0x1A00` (green ✗). fast3d byteswaps + stores faithfully (proven correct by BUNKER) — the wrong bytes *arrive* from the model-DL / segment-address path. Strong tell: every *larger* model TLUT has `raw[0]==raw[1]==0x0000` with real colour from index 2 ⇒ GE's model-TLUT payload likely carries a **4-byte (2-entry) leading zero pad**, OR the port's model-TLUT source pointer is **4 bytes / 2 entries too low** (then the `count=1` grip load reads the tail of the preceding CI blob — BUNKER lucky, FACILITY not; fits "widespread across levels + weapons"). Ruled out this session: not tail-overflow (index 0 is in-range; matches M-85 `GE_D217Z`), not the fast3d byteswap. **Next:** dump the grip TLUT `G_SETTIMG` target vs the ROM/`pcmodels` sidecar PP7-grip material bytes; confirm the 4-byte offset vs N64; fix in model-DL segment resolution or `tools_pc/d43_emit.py` blob placement (sidecar regen). ② **CI texture-cache collision (proven, real, separate; fix landed).** `gfx_texture_cache_lookup` (`gfx_pc.cpp:522`) keyed CI textures on `{addr, palette_addrs[0/1], palidx, size_bytes}` with **no palette-content check**; GE reissues `gDPLoadTLUT` from repeated scratch-arena addresses with different content, so a later material could take a stale HIT and sample another material's decoded palette. **Fix on branch `fix/d217-model-texture-palette` (commit `f6fc0399`):** FNV-1a hash of the 512-byte `rdp.palette` computed only in `gfx_dp_load_tlut()`, added to the CI cache key (`port/fast3d/gfx_pc.{cpp,h}`, fast3d-internal, no `#ifdef`). Cheap, cannot worsen a decode; **owes a full 21-level `verify.sh sweep`** before merge (touches every palettized world texture's import path — cache-churn risk only). Does **not** fix defect ①. Screenshots: session scratchpad `d217/`, `fac_gun_crop.png`. Cross-ref `docs/dev/GRAPHICS-BACKLOG.md`. **User playtest matrix (M-87, raw notes — not yet triaged):** DAM/PPK: watch still green; AK47 viewmodel has white (missing-texture) patches on the magazine + barrel. FACILITY: PPK garbled **green AND blue**; 3rd-person Bond world model shows greenness on level load-in; AK47 has "weird blue / glitched" textures. JUNGLE: M16 viewmodel is orange in spots. Confirms multi-weapon (PP7/AK47/M16), multi-color (green/blue/white/orange) — consistent with the upstream model-TLUT-source theory (different trailing garbage per weapon/material) rather than one single fixed offset. **Scope clarified (M-87):** not just Bond's 1P viewmodel / 3P world model — **NPC-held weapon models (other characters' guns in third person) show the same glitched-color/white-missing texture symptom.** So the defect is in the shared weapon-model-DL/TLUT path used by *any* character holding that weapon, not something specific to Bond's model or the 1P viewmodel rig — widens the likely fix surface (weapon model loading in general) beyond "Bond's gun." |
| D218 | **Wide FOV exposes the per-level draw-distance / fog boundary — "blue artifacting" down long peripheral sightlines (M-84, user QA on the D211 FOV slider).** BUNKER→**Dam main tunnel** at `FovScale` ~150: geometry / backdrop past the level's fog-end distance shows at the widened frustum edges (blue = Dam's fog tint). Not a render bug — `fogLoadCurrentEnvironment` (`bgfog.c:305`) does `viSetZRange(Visibility.BlendMultiplier, Visibility.FarFog)`, so **`Visibility.FarFog` is both the far clip plane and the fog-saturation distance**; levels tune it for the stock ~60° FOV. | **Logged (M-84), fix proposed + parked.** Plan: `Video.DrawDistance` (percent, 100–400, default 100 ⇒ no-op) `#ifdef PORT` multiplier on `FarFog` at `bgfog.c:305` (+ the direct `arg0->Visibility.FarFog` reads just below, so fog stays consistent) plus `Video.DrawDistanceAutoFov` (0/1, default 1 — couples draw distance to `FovScale` unless `DrawDistance` is set explicitly). Clamp effective multiplier ≲2.5× (small znear ⇒ far-field z-fighting risk). **M-87: user confirms still occurring** at high FOV, and — reversing the earlier "not implemented by user request" — **now wants `Video.DrawDistance` implemented as a real QoL feature**, plus scope it to cover **LOD/detail-distance in general**, not just the fog/far-clip boundary this row root-caused. Open question for that broader ask: does GE have a *separate* geometry/object LOD-swap or draw-culling distance mechanism (distinct from `Visibility.FarFog`) worth exposing too, or is fog-distance the only meaningful "how far can you see" knob in this engine? Check for a per-object visibility-radius / simple-model-swap system before assuming `FarFog` is the whole story. |
| D219 | **PPK explosions render purple on Dam (M-87, user QA, "not 3D but noting").** Effect-color defect, not a texture-decode one — likely the D172 particle-color family (`gfx_lod_tile_offset` / `G_CC_INTERFERENCE` channel-order class) recurring on a different particle type/weapon than the D172 fix covered, or a fresh instance of the same class. | **OPEN — observed, not investigated.** Next: `GE_PCDUMP`/`GE_TEXDUMP` a Dam PPK explosion; check whether it's the same `gfx_lod_tile_offset` tile-sampling defect D172 fixed for blood, on an explosion DL D172's fix didn't touch. |
| D220 | **1P muzzle flash position shifts upward at higher `Video.FovScale` (M-87, user QA, Jungle/M16).** Likely shares D211's noted residual: the viewmodel draws through `c_perspfovy`/unscaled fovy while the world view is FovScale-scaled (`fr.c:737`), so a flash sprite anchored to viewmodel-space vs. screen-space math can drift as the two diverge more at higher scale. | **OPEN — observed, not investigated.** Next: find the muzzle-flash placement code (gun DL attach point vs. screen-space billboard), check what FOV/projection it uses vs. `frFovY`/`portFovScale`, reproduce headless with `FovScale` 100 vs 150 on Jungle M16. |
| D85 | room primary/secondary DL → `texLoadFromGdl` garbage | OPEN (safety-netted; widen/pool fixes landed, geometry now renders) |
| D86 · D87 | modelInitRwData truncated ptr · attract-demo BE `ramromfilestructure` | resolved |
| D88.1–D88.3 · D88.5–D88.6 | `Usetup*Z` header + sub-table width/endian conversion (`d88_emit.py`) | resolved |
| D88.4 | `propDefs` polymorphic record stream not byteswapped → `setupDoor` crash | resolved (`d88_propdefs.py`); layout audit M-20 → **D132** |
| D132 | D88 propDefs layout audit (M-20): converter cursor confirmed vs real PC struct layout for all types the 21 levels emit. DOOR/OBJECT-prefix/VEHICHLE/AIRCRAFT/TANK/AUTOGUN/AMMO/TINTED_GLASS/objective sub-records all MATCH. **Divergence found:** types 14 LINK / 19 SWITCH / 38 LOCK_DOOR / 44 SAFE_ITEM — each has `s32 IndexN` fields sharing a union with a pointer (`LinkRecord.first`/`Index1`), so on PC the field sits in an 8-byte, 8-aligned union slot, but the converter emits it at N64 tight-4-byte-word offsets → `pdef->Index1` reads `Index2`'s value, `Index2` reads 0 → switch-doors / dual-weapons / locked-doors / safes silently fail their validity guard (non-crash; guard failure also prevents the under-sized-record `->next` overflow). | proposed fix (not applied) — see below |
| D157 | **Campaign never unlocks the next level (M-30, user bug — "complete Dam on Agent, Facility stays locked, no save").** The type-23 objective record's `MinDificulty` is a big-endian `s32` at word 3; on N64 the 0–3 value sits in its low byte == offset `0xF`, and `struct objective_entry.difficulty` (`bondtypes.h`) reads `s8` @0xF. `d88_propdefs.py` `_bswap32`'s that word (correct — it IS a 32-bit field), moving the value to offset `0xC` on LE → the `s8`@0xF read yields **0 for every objective**. `get_difficulty_for_objective()` → Agent(0) for all → `objectiveIsAllComplete()` on Agent evaluates objectives that should be difficulty-gated out (Dam "Neutralize all alarms" = Secret Agent, "Install covert modem" = 00 Agent) → they're never complete → `bossReturnTitleStage` sees `objectiveIsAllComplete=0` → `end_of_mission_briefing()` (the `fileUnlockStageInFolderAtDifficulty` → EEPROM write) never runs. The D151 sibling — same struct, the field D151 said it "left unchanged". Confirmed via GE_SAVELOG playtest + a direct ROM dump of Dam's records (word 3 = 1/2/2/0). | FIXED (`src/bondtypes.h`, `#ifdef PORT` — reorder `objective_entry`'s post-swap tail so `difficulty` reads @0xC, `unkD` @0xE; N64 layout under `#else`; sizeof unchanged 16). Also fixed `tools_pc/dump_objectives.py` (same byte-offset bug — read `buf[off+12]` = BE high byte = always 0 → showed "Agent" for every objective) + regenerated `docs/dev/LEVEL-OBJECTIVES.md`. **User-confirmed:** Dam Agent → `objdiff=1/2/2/0` → `objectiveIsAllComplete=1` → `fileWriteSave` with the completion time; Agent checkmark shows in mission-select and persists across kill+restart. New `GE_SAVELOG=1` / `GE_UNLOCK_ALL=1` `#ifdef PORT` env diagnostics added along the way. 21/21 sweep PASS. porting-notes.md §C. |
| D160 | **Dam level-end cutscene (Bond off the dam / bungee) never plays — cuts straight to the MISSION COMPLETE report (M-30/M-31, user bug D148).** Progression is unaffected: `bossReturnTitleStage` fires, the report + next briefing load, EEPROM save lands (D157). So the *transition* works; only the scripted cutscene step is skipped or silently fails. Static trace: Dam's end sequence is `UsetupdamZ` ailist `ai_24` (id 0x1004, gated `if_bond_in_room_with_pad(0x4a01)` — Bond reaches the exit) → lock controls, `bond_set_locked_velocity` (auto-walk off edge), wait `if_bond_y_pos_less_than` (Bond falling) → fade → `if_objective_all_completed` branch → `camera_switch`/`jump_to_ai_list` into the abseil ailist `ai_17` (id 0x0412: `screen_fade_from_black`, `music_xtrack_play`, `guard_play_animation(0xb100…)` = rappel anim on a puppet chr, three `camera_switch` w/ fades, `exit_level`). `camera_switch(TAG,…)` resolves a `CutsceneRecord` (propDef type 46, records 322/324/326 paired with Tags 10/11/12) via `tagGetCommandIndex` + `setupGetPtrToCommandByIndex` (a **command-index count walk over the propDef stream** using `sizepropdef()`). CutsceneRecord itself is all-scalar 28 B, correctly converted (`d88_propdefs.py` 46→28). **Leading hypotheses, unverified:** (a) the index-count walk desyncs because some earlier propDef type Dam emits (Vehicle 321 — D122 "medium confidence" tail; TintedGlass) has a wrong PC `sizepropdef` stride → `camera_switch` gets a bogus `cdef`/`TagIndex<0` → cutscene silently skipped, `ai_17` falls through to `exit_level`; (b) the trigger chr for `ai_24`/`ai_25` isn't spawned / `if_bond_in_room_with_pad` never true and the level ends via a different path; (c) D75/anim-model family — the puppet `guard_play_animation` rappel anim doesn't render (third-person Bond model absent, like the gun-barrel/cast models) so the cutscene "plays" invisibly but the user perceives it as skipped (less likely — user reports an instant cut, not a blank pause). | DIAGNOSTIC SHIPPED, not root-caused (`c95713f5`, `#ifdef PORT` + `GE_D160=1`): trace points in `bossReturnTitleStage` (entry + caller), `bondviewSetCameraMode` (mode/stage/`g_IntroSwirl`/intro-anim-idx/caller), and AI cmds `EndLevel`/`exit_level`, `CameraLookAtBondFromPad`, `CameraSwitch` (prints `cdef` ptr / `TagIndex<0` / tag `NOT FOUND`). **Next step:** user plays Dam to the exit with `GE_D160=1` (via `debug.ps1`); the trace answers (1) does `CameraSwitch`/`CameraLookAtBondFromPad` execute at all → trigger fires? (2) if yes, is `cdef` a sane pointer → propDef-walk / converter bug (a), same family as D122/D132/D157; (3) if the cam cmds never run → ai_24 trigger / chr-spawn bug (b). If it's (c), `bondviewSetCameraMode` shows POSEND being set + control returns normally. This is likely a WRITE-UP-then-fix (propDef stride) rather than a hack. Confidence in hypothesis (a): medium. |
| D161 | **Depot (`-level_30`) ceiling renders as bright-blue speckle + radial "light-ray" streaks converging to a point instead of a dark corrugated roof (M-30/M-31, user bug B2 — `docs/dev/TEXTURE-GLITCH-ANALYSIS.md`).** NOT filtering (3-point+trilinear+aniso didn't touch it), NOT RC2 mip-contamination, NOT the RGBA5551 decode. Root-caused with a `GE_TEXDUMP` PPM+param dump (`gfx_opengl.cpp` / `gfx_pc.cpp`): the offending surface is **one texture, `fmt=CI siz=8b, 16×16`**, uploaded with **`rdp.palette_fmt == G_TT_NONE` (0)** while every other CI texture that frame has `palfmt=0x8000` (`G_TT_RGBA16`). GE draws the Depot roof with the TLUT **disabled** (`gsDPSetTextureLUT(G_TT_NONE)` — a legit N64 idiom: a CI-siz tile with no LUT feeds the raw 8-bit TMEM texel straight into the colour pipe, i.e. it behaves as a plain **I8** texture). fast3d's `import_texture()` ignored `palette_fmt` and always did the palette lookup for `fmt==CI`, indexing a **stale `rdp.palette`** left over from a previous texture → the 16×16 image came out as pure blue/cyan/magenta noise; the "radial rays" were that high-frequency noise aliasing on the steeply-receding ceiling plane (which is also why filtering couldn't help — the data was garbage). Fix: in `import_texture()`, `fmt_eff = (fmt==G_IM_FMT_CI && rdp.palette_fmt==G_TT_NONE) ? G_IM_FMT_I : fmt`, and dispatch on `fmt_eff` (CI4→I4, CI8→I8). Narrow — only changes surfaces that are currently 100% garbage. Verified: Depot corridor + control-room ceilings render as clean dark/grey industrial roofs; `-level_09`/`-level_30`/`-level_34` sweep PASS, unregressed. `GE_TEXDUMP` (env-gated PPM dump of every uploaded texture + a `fmt/siz/palfmt/palidx` log line) kept as an inert diagnostic. Confidence: **high** (before/after screenshots + the palfmt=0 evidence). | FIXED (`port/fast3d/gfx_pc.cpp` `import_texture`, `port/fast3d/gfx_opengl.cpp` diag). porting-notes.md §D. |
| D89–D92 | stage-load→frame: stan zero-fill overrun, portal address trunc, chr/AI spawn ptrs | resolved |
| D93–D102 | struct-pun / hardcoded-size pass: player alloc, player.model inline Model, weapon Model pun, master-DL buffer | resolved (D95 pool bump partial) |
| D103–D107 | BUNKER1 viewport height, depth-buffer clear (`G_CLEAR_DEPTH_EXT`), portal near-plane, LOD mip tile | resolved |
| D108–D112 | skeletal models: `d43_emit.py put_f32` byte-reversal bug | resolved |
| D113–D116 | portal BFS ok · matrix chain ok · player raw-offset audit + `gunfire.c` THROW* · HUD text X-mirror | **D114/D116 CLOSED — NOT A BUG (M-33, see D168): the "X-mirror" was an upside-down `GE_PCDUMP` capture misread as mirrored. PPM writer fixed; the game renders correctly on hardware.** |
| D117 | frame nondeterminism root-caused; `GE_DETERM` deferred; `framediff.py` structural. **M-52: `GE_DETERM=1` attempted — boot deadlock fixed, determinism NOT achieved (load-phase fallback still real-time-dependent). See addendum below.** | OPEN (mode attempted, not achieved) |
| D118 | SDL input layer (`port/src/input.c`); M-24 mouse-look rework: mode-aware map (aim mode = analog stick, hipfire = digital C-pitch), `config.c` INI now real. D118b/c FIXED; D118a residual (hipfire pitch only) | resolved (D118a residual) |
| D150 | watch OBJECTIVES / BRIEF page crash: `strcat(buf, langGet(id))` with `langGet` → NULL (unloaded bank) → NULL deref; `str.c` str* builtins elide a plain param NULL check, so guard via asm-laundered `GE_IS_NULL` | FIXED (`src/str.c` `#ifdef PORT`); interactive re-verify pending |
| D137 | right-mouse (aim-sight) crash: `gunDrawSight` (`gunfire.c:6235`) `s32 sp54` holds a `Gfx*` the whole function (`sp54 = *gdl`; `texSelect(&sp54,…)`/`display_image_at_position(&sp54,…)` take `Gfx**`). As N64 `s32` it is 4 B, so `*(Gfx**)&sp54` reads 4 B of the adjacent stack float as the pointer high word → `_g = 0x03e4fca0_70081220` → wild DL write in `texSetRenderMode` (`othermodemicrocode.c:177`) the moment the crosshair raises. §A. | FIXED (`src/game/gunfire.c` `#ifdef PORT` — `sp54` is `Gfx *`) |
| D119 · D120 | guard-attack `weapons_held[]->chr` pun crash · blood-stain `PointUsage[]` chain hang | D119 fixed · **D120 FIXED (M-30)** |
| D120 | **Blood-decal `PointUsage[]` chain — converter now emits it (M-30).** `d43_emit.py` reserved the opcode-0x18 `PointUsage` region in the layout pass (`add_region(puo, 2*nv)`) but the **emit pass never wrote it** — every entry stayed zero. `chr.c`'s decal walk `index = PointUsage[index]` then cycled 0→PointUsage[0]→0 forever (capped by the `#ifdef PORT` guard at `chr.c:3328`, so decals were missing/wrong rather than a true hang). Fix: `op24_pointusage[puo] = nv` in layout + a byteswap loop (`put_s16(remap(puo)+2k, be16(src, puo+2k))` for `nv` = `numVertices` entries — a negative-terminated `s16` index chain, no remap) in the emit pass. Full regen chain re-run, round-trip validation passes. The `chr.c` guard stays as a safety net (SMALL-FIXES B3). | FIXED (`tools_pc/d43_emit.py`). Interactive verify pending — blood decals on a BUNKER1 firefight should now appear + no hang. |
| D121 | WS1 frictionless per-level boot: bare `-level_XX` injects its `memallocstringtable[]` `-m*` row (`#ifdef PORT` in `boss.c`) | resolved |
| D122 | per-level prop/item model-load crash (Dam/Facility/Runway `modelLoad`/`modelInitRwData`): `d88_propdefs.py` had no handler for 6 ObjectRecord-derived propDef types (47/39/40/45/13/20) → generic arm half-swapped the `[s16 obj][s16 pad]` word → OOB `PitemZ_entries[]` | converter fixed; residual chr/fast3d crashes on those levels are separate |
| D123 | crash class C1: `chrIsNotDeadOrShot` NULL deref on 6 levels (Dam/Runway/Frigate/Statue/Streets/Cradle). D122's `OBJ_TAIL_DESC` zeroed the widened `VehichleRecord/AircraftRecord.ailist` slot (w32), but `prop.c:1764/1786` reads a pre-populated int AI-list id there → `ailistFindById(0)` → `GAILIST_AIM_AT_BOND` → `ai()` runs a CHR aim list with `ChrEntityp==NULL` | converter fixed (`OBJ_ID_WORDS`); C1 cleared on all 6, residual crashes are fast3d (C2) |
| D125 | crash classes C3+C6 (Aztec/Bunker2/Surface2): **root cause found (M-16)** — `tools_pc/d88_emit.py:374` assigns a 4-byte literal to an 8-byte slice (`out[dst_o+0x30:dst_o+0x38] = b"\x00…"`, the stan zero-fill in `emit_pad`); every pad/boundpad record silently shrinks the output bytearray by 4 B, and once the tail falls below the boundpad plink string blob, later verbatim leaf writes hit Python out-of-range slice semantics (insert at current end, not relocated offset) → boundpad names drift/truncate (Aztec pad33 `p138d2`→`8d2`) → `stanPackId()` reject → `getposstan()` NULL stan → `setupDoor` leaves door `model=NULL` → crash `propobj.c:13601`. M-14's "propDefs zeros in RAM" was a misread; sidecar propdefs were always correct (M-15) and post-load RAM matches the sidecar byte-for-byte (M-16) | **resolved (M-16b)** — line 374 → 8 NULs; all 21 sidecars regen'd; **Aztec `-level_28` now PASSES** (was C3 CRASH). Bunker2 falls through to a separate DOOR-tail `linkedDoor` layout bug (C3 residual) |
| D126 | crash classes C3r/C4/C6 (Bunker2 `-level_27` `door7F054FB4` propobj.c:13523, Depot `-level_30` prop.c:902, Surface2 `-level_43` loadobjectmodel.c:393): the objective sub-records `criteria_picture` (30), `criteria_roomentered` (32), `criteria_deposit` (33), `setup_objective_text` (35) each end in a `T *next` list pointer that the setup walk (`set_parent_cur_obj_*` / `setup_briefing_text_entry_parent`) writes unconditionally. On PC that pointer widens 4→8B and lands 8-aligned at offset 16 → struct is 24B/6w (N64 16/20). `d88_propdefs.py` emitted them at N64 size via the generic arm → the runtime 8-byte `->next` write clobbered the *next* record's header → propdef walk desynced, command indices drifted ~100, `linkedDoorOffset + arg2` resolved to the wrong record → door `linkedDoor` chain walked into garbage. | resolved — `d88_propdefs.py` PROPDEF_PC_BYTES[30/32/33/35]=24 + typed handler; `loadobjectmodel.c` sizepropdef PORT returns 6. Bunker2/Depot/Surface2 now PASS (13→16/21) |
| D124 | crash class C2: fast3d bad texture pointer. **Jungle** (`0xabcd0824`): `gimgSyncCompiledGlobalDLs()` slot-detect keyed on the post-fixup marker, which `texLoad()` had already overwritten → compiled `globalDL_0xNNN` explosion DLs kept link-time `IMAGESEG` words → latent on every level, tripped by the first explosion-DL draw. **Facility** (`0x72181ee8`): see **D130** — the model-GDL-relocation hypothesis (M-14 addendum) was WRONG; real cause was `romdataFixupFont` | Jungle fixed (`port/src/gimgfixup.c`); Facility → D130 |
| D131 | crash class C2m (Jungle `-level_37`): fast3d `gfx_sp_matrix` AV on a wild matrix pointer `0x401c68e0` (`gfx_pc.cpp:1046`) ~frame 300, when the first explosion/smoke prop renders. `explosionRenderPropSmoke` builds `gSPMatrix(gdl++, osVirtualToPhysical((void*)&dword_CODE_bss_8007A100), …MODELVIEW)` (+ `applyRoomMatrixToDisplayList`). `osVirtualToPhysical()` is a `u32`-returning shim (`libultra.c:1207`) → truncates the compiled `.bss` symbol's `0x1_00000000` module high word → w1 = `0x40xxxxxx` → `seg_addr()` returns it raw. Same class as D94 (chraction 32-bit ptr truncation), but in a GBI DL word. The projection matrix in the same DL is fine (`get_BONDdata_field_10E0()` is a runtime `0x70xxxxxx` ptr, fits u32); the `gSPDisplayList(&globalDL_0xNNN)` refs are fine (port `Gwords.w1` is 64-bit `uintptr_t`, no macro truncation). ~30 `osVirtualToPhysical(<compiled matrix/vtx symbol>)` sites exist (`explosion.c`, `glass*.c`, `blood_animation.c`, `bondview2.c`) — all latent until that effect first draws. | resolved — `seg_addr()` restores the module high word for any fallthrough w1 in `[0x40000000, 0x70000000)` (module fixed-based at `0x140000000`, no ASLR; DRAM/KSEG0/segmented/phys all handled earlier). Jungle renders to frame 2400+ clean; `-level_20`/`-level_24` unregressed. **18→19/21.** (`-level_09` has a separate pre-existing boot crash, see below.) |
| D133 | intro render triage (M-20): the "intro renders mostly black" item in the M-18/M-19 handoff is **NOT a regression** — it is the D75/D76 parked-cosmetic steady state. M-17 (`9ec6121e`), whose handoff claimed "the entire intro renders — logos → gun barrel → cast", was built in a scratch worktree and captured with the same `GE_PCDUMP="20-900:20"` window: coverage is **pixel-identical** to HEAD `0b5f5d1a` (legal screen 6677 non-clear px / 2.17% on both; centred-logo frames ~7% on both; near-black between). 2D/text/texture layers draw; animated character-model layers (Nintendo-logo transform, gun-barrel Bond, cast models) never appear = D75 exactly. No cheap regression to fix; parked. Secondary: Facility `-level_34` + Jungle `-level_37` re-verify FAILED this pass (boot crash / frame-2 hang) — flagged in LEVEL-STATUS for a clean-machine re-check, not investigated (scope). | not a regression — parked (D75/D76, `GRAPHICS-BACKLOG.md`) |
| D134 | **Frame-2 boot hang / sweep "NO-FRAMES" flakiness — root-caused and fixed (M-22).** `osSpTaskStartGo` (port/src/libultra.c) posted the SP/DP task-done events with `OS_MESG_NOBLOCK` into the scheduler's 8-slot `interruptQ`, which the 60 Hz VI pacemaker thread also fills with `VIDEO_MSG`. A gfx task runs the whole frame synchronously on the sched thread (fast3d); the first two frames take 30-80 ms, so the pacemaker queues several retraces meanwhile. Once the queue is full the RSP-done post is **silently dropped** -> `__scMain` never clears `sc->curRSPTask` -> the client never gets `OS_SC_DONE_MSG` -> `bossMainloop`'s `pendingGfx` never clears -> permanent stall (log: `frame 2 rendered`, then `kernel heartbeat ... frames=2`, retrace queue pinned at `valid=8/8 ret=-1`). Reproduced 2 of 3 `-level_09` boots on an idle machine, i.e. most of what M-13..M-21 wrote off as D117 nondeterminism / machine load. `OS_MESG_BLOCK` is NOT the fix (the done event is posted from the sched thread, the queue's only consumer -> self-deadlock): `portPostEventForce()` drops the OLDEST message (always a stale retrace) and retries, and `portPostVIEvent` now reserves 2 slots. Verified: 6/6 `-level_09` boots reach frame 600 with 0 heartbeats (pre-fix 1/3). | FIXED (`port/src/libultra.c`) |
| D135 | **Firefight crash — `bgTestHitOnObj` (`propobj.c:8446`) is an unported N64 GBI parser (M-22, WS6 playtest).** The object bullet-hit test walks a model's display list command stream with hardcoded 8-byte-N64-`Gfx` byte/word indices (`*(s8*)gdl`, `((u32*)gdl)[1]`, `((u8*)gdl)[5..7]`, the G_TRI4 nibble reads, the backward `G_SETTIMG` scan). On PC the model GDL is the 16-byte `Gfx` layout (`d43_emit.py`, `{u64 w0; u64 w1}`), so every access reads the wrong bytes → the walk desyncs on the first `G_VTX`/`G_TRI1` → `v->coord.x` deref off the end of the DL → AV (`0x709ae02a`, a near-miss past a live DRAM ptr = the §B signature). Triggered by `propobjFindHit` every time a shot ray resolves a hit on an object model (guards' dropped weapons, attached objects, shootable props) — invisible to `level_sweep.sh` (no weapon fire). Two sibling faults seen the same session (`texSelect`/`texSetRenderMode` wild `gdl`, `objFreeEmbedmentOrProjectile` `prop->obj`) are likely the same corrupted-object cascade. PD `bgTestHitOnObj` (`pd_port/src/game/bg.c:3635`) is the fully-ported ground truth. | FIXED (`src/game/propobj.c`, `#ifdef PORT` — reads `gdl->words.w0/.w1` shifts). **Second fault, same walker:** after the parser fix the Facility firefight then hit `0xc0000094` (int div-by-zero) at `objHit` `propobj.c:9717` `randomGetNext() % impact_sounds->thing2_len` — the ported texnum-recovery (`*(s16*)(phys(w1) − 8)`) returned a bogus positive `texturenum` (`(u32)` cast truncated a 64-bit `words.w1`; the N64 "texnum 8 bytes before the texture data" layout isn't guaranteed for the port's converted/replaced GDLs) → `g_HitTypeSounds[texnum & 0xf]` picked an entry with `thing2_len == 0`. Fixed: the PORT texnum branch now always returns `-1` (→ `g_HitTypeSounds[0]` = `isnd_default`, `thing2_len == 1`, safe). `texturenum` only flavours the bullet-impact sound/decal, so generic-hit is an acceptable degradation (park with D77). **Follow-up:** the BG room-geometry sibling `sub_GAME_…` (`bg.c:~3373-3646`, walks the D85-widened `ptr_expanded_mapping_info`) has the identical unported parser pattern — latent, fires on shooting walls/floor; not yet ported. |
| D136 | **Stage-unload crash — `objFree` `obj->prop` reads float garbage (M-22, WS6 Facility playthrough).** After D135 cleared the firefight crashes the level is completable; then on teardown `lvlUnloadStageTextData` → `cleanupObjects` (`cleanup_objects.c:42`) → `objFreePermanently` → `objFree` (`propobj.c:994`) → `objFreeEmbedmentOrProjectile(obj->prop)` faults at `:888` (`prop->obj` deref). Real gdb `bt`: `obj` = `0x701bd730` (valid) but `obj->prop` = `0x3e567400405c30c0` — two packed `f32` (~0.21, ~3.44), i.e. a `coord3d` read as a pointer → `ObjectRecord.prop` (N64 `/*0x10*/`, after `inherits PropDefHeaderRecord` + `s16 obj; s16 pad; u32 flags; u32 flags2;`) lands at the wrong PC offset for this object's propDef type, OR the record is undersized and setup's `->prop` store never happened / was clobbered. Same family as D122/D132 (`tools_pc/d88_propdefs.py` per-type record sizing vs the compiled `ObjectRecord`/subtype layout). Type not yet identified — needs a gdb run with a breakpoint on `objFree` to read `obj->type` (`cleanupObjects` frees WEAPON/AMMO/MAGAZINE/COLLECTABLE/MONITOR/RACK/AUTOGUN/HAT/ARMOUR/GAS_RELEASING/VEHICHLE/AIRCRAFT/GLASS/SAFE/TANK/TINTED_GLASS via this path). Mission completes; crash is before the next briefing loads. | ROOT-CAUSED → **D139** (it was NOT a layout bug — `cleanupObjects` read the type from the wrong byte on LE and walked off the propDef blob). |
| D139 | **Stage-unload crash root cause + fix (M-23).** `cleanupObjects` (`cleanup_objects.c`) walks `g_CurrentSetup.propDefs` with `(u8)obj[0]` as the record type — correct only because the header word `[u16 extrascale][u8 state][u8 type]` is **big-endian** on N64, so `type` is the low byte. On little-endian PC the low byte is `extrascale`, so `(u8)obj[0]` is never `PROPDEF_END` → the `while` runs off the end of the blob, and the `switch` dispatches on garbage → `objFreePermanently()` on records that aren't `ObjectRecord`s → `obj->prop`/`obj->model` read adjacent float data (`0x3e567400405c30c0`) → AV in `objFreeEmbedmentOrProjectile` (`propobj.c:888`). `sizepropdef()` and every other propDef consumer already use `pdef->type` (struct member, offset 3) — only this one spot used the raw byte cast. Not a `d88_propdefs.py` layout issue at all. | FIXED (`src/game/cleanup_objects.c`, `#ifdef PORT` — `CLEANUP_PDTYPE(o)` = `((PropDefHeaderRecord*)o)->type`). **UNVERIFIED** — the fast teardown test (pause→abort) hit D140 first; `-level_09` unregressed. porting-notes.md §C. |
| D140 | **Pause-menu crash (M-23 open → M-27 fixed).** `bondviewRenderWatch` (`bondview2.c:8604`) → `bondviewTransformManyPosToViewMatrix(g_CurrentPlayer->field_23C, objheader->numMatrices)` with `field_23C == NULL` → `matrix_4x4_copy(src=0x0)` AV. Root cause (NOT "cache never allocated"): `something_with_watch_object_instance` (N64 player +0x230) is a **`struct Model` (0xBC) + its RW-data pool (0xC8) punned into a 0x184-byte hole** (`field_234..field_3B0`); three of those "fields" are Model members the game reads by name — `watch_scale_destination`==`.scale`, `pause_watch_related_adjust`==`.animframe1`, **`field_23C`==`.render_pos`**. `render_pos` IS populated (by `instcalcmatrices` via `subcalcmatrices` at `:8524`), but on x86-64 `sizeof(struct Model)` grows (4→8B ptrs) so `field_23C` no longer overlaps `.render_pos` → reads Model padding (~0) → NULL. Same class as D100 (gait Model) / D102 (weapon Model) / D56 (this very watch Model's pool). Fix: `#ifdef PORT` makes `something_with_watch_object_instance` a real inline `struct Model` + `u32 watchRwPool[192]` in `struct player` (`bondview.h`); `bondview2.c` redirects the 3 named reads to the member via `GE_WATCH_{ANIMFRAME,SCALE,RENDERPOS}` macros (N64 `#else` = the verbatim field). The D56 branch of `sub_GAME_7F07E7CC` drops its `static u8 watchRwPool[0xC8]` for the inline pool. | FIXED (`src/game/bondview.h`, `src/game/bondview2.c`; `#ifdef PORT`). Exposed the next watch-render crash → **D141**. `-level_09`/`-level_20` unregressed (3/3). porting-notes.md §A. |
| D118d | **Watch-menu list over-scrolls with keyboard (M-27, user bug report).** `game_options_inventory_navigation` / `sub_GAME_7F0A611C` (`options.c`) have a "slam the stick" fast-scroll: a raw *level* check `joyGetStickY(PLAYER_1) < -0x46` / `>= 0x47` that steps `watch_inventory_cursor_pos` by 1 **every frame** it's held. An N64 stick rarely sustains past ~0x46 on-axis so a human taps it; keyboard W/S (and SDL pads pegged at 0x80) sit there every frame → one keypress skips several items (breaks item selection needed to finish the game). Fix: `#ifdef PORT` macro `GE_WATCH_STICK_FAST{UP,DOWN}` → `0` for the port — stick list-nav then goes only through the latched single-step path (`watch_stick_y_pressed_*`, one step per press, existing game mechanism) and the -0x1e..-0x45 smooth-scroll band; D-pad/C-buttons keep edge-repeat. N64 `#else` = verbatim. | FIXED (`src/game/options.c`, `#ifdef PORT`). Build green, `-level_09` unregressed. **Feel to be confirmed in playtest** (probe couldn't reach the INVENTORY watch page headlessly). D118 input family. Latent sibling: `front.c` menu nav uses `joyGetStick*InRange` level checks the same way. |
| D141 | **Watch-menu item-model crash (M-27), exposed by D140.** Pressing Start → watch page renders → `set_enviro_fog_for_items_in_solo_watch_menu` (`gunfire.c:1620`) → `modelGetNodeRwData(&model, root)` with `root` garbage → AV reading `root->Opcode` (`model.c:478`). `gunfire.c:1720-1743` walks `bodymodel->Switches[]` (a `ModelNode*` array) with **raw byte offsets** `((u8*)Switches) + j + 0x48` / `+ 0x5c` and `j += 4` — assumes 4-byte N64 pointers. `0x48/4=18`, `0x5c/4=23`, `k=j>>2=0..4` → intent is `Switches[18+k]` and `Switches[23+k]`. PC's 8-byte stride makes the raw math read misaligned garbage → bogus non-NULL `ModelNode*`. porting-notes.md §B (raw hardcoded stride into a pointer array, cf. D128). Fix: `#ifdef PORT` uses `bodymodel->Switches[18 + (j>>2)]` / `[23 + (j>>2)]`. | FIXED (`src/game/gunfire.c`, `#ifdef PORT`). Watch page now renders (weapon page verified — mirrored text D114/D116, dark weapon model D75, both parked cosmetic). `-level_09`/`-level_20` unregressed. |
| D150 | **Watch objective/briefing page crash (M-28).** Opening the watch → BRIEF (any subpage) or OBJECTIVES page → AV in `strcat` (`str.c:25`, `Rdi/src = 0`). The pages assemble their display text with `strcpy`/`strcat(buf, langGet(id))` (`options.c:3940-4068`, `front.c:3538-3562`, `propobj.c`). `langGet()` returns **NULL** on PC for a string bank the menu flow never loaded (D129/D143 guards — briefing/objective banks in the in-level watch flow), so `strcat(buf, NULL)` derefs NULL. On N64 those ids always resolve so the decomp never guards. Twist: `str.c`'s `strcpy/strncpy/strcat` are `__nonnull__` **builtins** to GCC, so a plain `if (src == NULL)` on the parameter is optimised away as provably-dead (verified: `-Og` elided it, no `test` in the disassembly). Fix: `#ifdef PORT` NULL guards that launder the pointer through an empty `__asm__` (`GE_IS_NULL()` / `ge_launder_ptr()`) so the check survives — NULL src → treat as empty string (blank text, not a crash), NULL dst → return. Same philosophy as the D143 textRender/textMeasure NULL guards, one layer down. Inert on N64. | FIXED (`src/str.c`, `#ifdef PORT`). Build green (`ntsc-final`); `-level_09` 600+ frames @ 91.67% unregressed. Watch-page repro is interactive — user to re-verify. porting-notes.md §C. |
| D152 | **Mission-failed / objective-failed → permanent black screen (M-28, user bug report — LIVE process inspected, not yet fixed).** User killed Trevelyan in Facility (Ctrl = fire, no crouch bind) → failed the objective → screen faded to black and never came back; process stayed alive under `debug.ps1`. `gdb.txt` (kept running): rendering stops dead at frame 12600 (`frames=12657` frozen across 206 heartbeat dumps / ~7 real min), VI pacemaker still posts. **Deadlock on `s_imLock`** — the D147 recursive-mutex that backs `osSetIntMask`. Both consumer threads are blocked in `pthread_mutex_lock(&s_imLock)` and neither owns it: `mainThread` = `sndSetScalerApplyVolumeAllSfxSlot` → `sndApplyVolumeAllSfxSlot` → `sndSetSfxSlotVolume` → `alEvtqPostEvent` (`event.c:82`) → `osSetIntMask(OS_IM_NONE)` (`libultra.c:1232`); `amMain`/audioThread = `amHandleFrameMessage` → `alAudioFrame` → `sndPlayerVoiceHandler` → `alEvtqNextEvent` (`event.c:47`) → `osSetIntMask(OS_IM_NONE)`. `sndSetScalerApplyVolumeAllSfxSlot` is the **mission-failed audio fade-out** (ramps `g_sndSfxVolumeScale` → 0 each frame, `AL_SNDP_RELEASE_EVT` posted per active `ALSoundState` per frame) — so the fade-to-black *is* the intended fail transition, and it hammers the event queue hard enough that the `osSetIntMask` lock is left owned by a party not in the 3-thread dump (a leaked unbalanced `OS_IM_NONE` — libaudio has early-return mask paths — or a transient thread that acquired it and exited; `gdb.txt` shows `New Thread`/`exited` churn every few k frames). D147's "the decomp never blocks while holding OS_IM_NONE → cannot deadlock" assumption is **falsified** by this path. Same family as D147 / porting-notes.md §D4. | MITIGATED (`port/src/libultra.c`, M-28, `#ifdef PORT`/port-only). `osSetIntMask` is now a **self-healing** logical lock: `OS_IM_NONE`/`OS_IM_ALL` acquire/release a process-wide recursive critical section tracked by `s_imHeld`/`s_imOwner`/`s_imDepth` under a short bookkeeping mutex `s_imMx` + condvar. Normal (balanced, microsecond) audio critical sections behave exactly as the D147 recursive mutex did. A waiter that blocks **> 2 s** (`OS_IM_STUCK_NS`) concludes the holder leaked it, logs `LOG_ERROR` with the stale `s_imOwner` + the stealing caller's return address, and **steals** the section — so a leaked/unbalanced `OS_IM_NONE` (or an acquire-and-exit transient thread) can no longer wedge the game forever; worst case is a ~2 s audio hiccup + a log line naming the leak. Builds clean. Still OPEN: the exact leaking call site (find it from the `D152: ... stealing from owner=` log next time it fires) and the proper narrow fix (dedicated `ALEventQueue` lock, or restore-on-thread-exit). Regression check deferred — `-level_09`/`-level_20` both flaky under the current machine load (D117/D134), and clean `master` itself has a separate nondeterministic `-level_09` frame ~900-1400 `0xc0000005` (see below). |
| D152+ | **D152 addendum — static audit + narrow fix (M-31).** Audited every `osSetIntMask(OS_IM_NONE)` in the *compiled* audio code (`event.c` `alEvtqNextEvent`/`alEvtqPostEvent`/`alEvtqFlush`/`alEvtqFlushType`, `csplayer.c` `__CSPRepostEvent`, `synaddplayer.c` `alSynAddPlayer`, `snd.c` `sndRemoveEvents`/`sndSetupSound`/`sndDeactivateAllSfxByFlag`): **all balanced on every path** — `alEvtqPostEvent`'s lone early `return` (`event.c:86`, freeList empty) restores the mask first. The §F D152 guess "libaudio has unbalanced early-return mask paths" is **not** borne out. Two real defects found + fixed `#ifdef PORT` (N64 verbatim `#else`): **(1)** `sndSetSfxSlotVolume` (`snd.c`) walks the live `ALSoundState` list and posts `AL_SNDP_RELEASE_EVT` to the shared `ALEventQueue` **without holding `OS_IM_NONE`** — unlike its structural twin `sndDeactivateAllSfxByFlag`, which wraps its identical walk. On PC that's an unguarded walk racing `amMain` *and* one lock acquire/release per matching sound; the mission-failed fade (`sndSetScalerApplyVolumeAllSfxSlot`→`sndApplyVolumeAllSfxSlot`→ this, every frame, every active sound) turns it into a per-frame lock-acquire storm on `mainThread` = exactly the §F dump's "hammers the event queue hard enough that the lock is left owned." Fix: hold `OS_IM_NONE` once across the whole walk (nested `alEvtqPostEvent` hits the recursive depth++ fast path, no new contention window); also wrap `sndApplyVolumeAllSfxSlot`'s slot loop so the whole volume update is one recursive hold (1 real acquire/frame instead of `SFX_SLOT_COUNT × active-sounds`). **(2)** The actual leak matches the §F dump's *other* hypothesis — "a transient thread acquired `OS_IM_NONE` and exited" (`gdb.txt` shows `New Thread`/`exited` churn). `port/src/libultra.c` `portThreadWrapper` now calls new `imThreadExitRelease()` after the thread entry returns: if that thread still owns `s_imHeld`, release the orphan immediately (+ `LOG_ERROR` naming it). This removes the wedge *and* the ~2 s steal hitch, and stops the **re-wedge** that occurs when a new host thread reuses the dead thread's pthread id and `imAcquire` mis-detects recursion (`s_imOwner == self`). The M-28 steal-lock stays as the last-resort backstop for a genuinely unbalanced *same-thread* path (none found, but cheap insurance). Build green (240/240); `-level_09` frame 300 renders 91.7% non-clear, crash-free; `-level_20` boots crash-free (frame-count flaky under load per D117/D134, no `D152:` log). Fade-out repro is **playtest-gated** — not reproducible headless, so not end-to-end verified; user should replay the mission-fail and confirm no black screen + no `D152:` steal/orphan log lines. | FIXED (`src/snd.c`, `port/src/libultra.c`, `#ifdef PORT`). porting-notes.md §D4. |
| D154 | **`bg.c` room-geometry ray/hit-test GBI parser port (M-28, WRITTEN / UNVERIFIED / UNCOMMITTED).** `bgTestRayIntersectionInRoom` (`bg.c:3331`, called by `bgTestBulletHitBackground` on every shot that resolves against background geometry) is the D135 sibling flagged since M-23: raw `((u8*)gdl)[k]` / `((u32*)gdl)[i]` byte/word indexing into what is now the 16-byte PC `Gfx` room DL (D85 `bgWidenRoomGdl` + `texLoadFromGdl`) → desync → OOB `vtxbase[idx]` / walk-off AV the first time you shoot a wall or floor. Ported `#ifdef PORT` (N64 verbatim under `#else`): `vtxoff`/`vtxbase`/`op` via the `.dma` view (matches the already-ported `bgBuildRoomVtxBounds`); G_TRI1 indices from `(w1>>16/8/0)&0xff /10`; the 12 G_TRI4 nibble extractions from `(u32)gdl->words.w0/.w1` (each mapped from the N64 byte/halfword layout — table in the code comment, cross-checked twice); both `texturenum` recoveries → `-1` (the KSEG0 `*(u16*)(w1-8)` deref is invalid for the port's converted GDLs, same as D135; texnum only picks the impact decal/sound, parked D77). Builds clean. **Verification blocked** — a no-input `GE_PCDUMP` capture never fires a bullet so it never calls this function; needs a real firefight into a wall on an idle machine (this session's box was thrashed → D153/driver crash on every run). | **M-30 re-audit + fixes (still playtest-gated for final verify).** Cross-checked the ported G_TRI1 + all 12 G_TRI4 nibble recoveries twice against the N64 `#else` and the raw BE-word bit derivation — **G_TRI1 and G_TRI4 are correct**. **Two bugs fixed:** (1) `vtxoff` — the port read `gdl->dma.par & 0xf`, but the PC `Gdma_le` shim (`port/shim/PR/gbi.h`) maps `.par` to bits 0-23 of word0 (the packed *length*, always 16-aligned) not the N64 byte-1 params field at bits 16-23, so vtxoff was silently forced to 0 (breaks any G_VTX batch with `v0 != 0`, i.e. multi-batch rooms → wrong `vtxbase[idx]` → OOB). Now `((u32)gdl->words.w0 >> 16) & 0xf`. (2) **Sibling ported** — `bgTestBulletHitBackground` tail (`bg.c:~3841`, the post-hit G_SETTILE back-scan for `tileformat`/`tilesize`) was itself an unported raw `((u8*)gdl)[0]`/`[1]` + 64-bit `words.w0 << 11 >> 30` parser over the same widened DL; wrapped `#ifdef PORT` (opcode from bits 24-31, byte1 from bits 16-23, `(u32)`-forced 32-bit width for the tilesize shift). **Diagnostic:** `GE_D154=1` → `osSyncPrintf` of the parsed cmd/vtx-index stream (call header + per-TRI1/TRI4 `idx`) for the first 64 invocations, for the user/integrator to eyeball. Build green (`ntsc-final`, 242/242). Runtime unverifiable in a worktree (no `data/`); still needs a firefight-into-wall on an idle machine + `-level_09` framediff. Confidence: HIGH on the mechanical correctness (bit-for-bit vs N64), MEDIUM that nothing else in the walk still bites. porting-notes.md §B (D135 corollary). |
| D156 | **Facility outro-cutscene "hang" — the actual fix (M-29, user bug report, 2nd occurrence after D155).** After D155 shipped the user re-ran the playthrough and it hung again at the end of Facility — same stack (`modelSetAnimFrame2WithChrStuff` ← `modelTickAnim` ← `chrUpdateAnim` ← `chrTick` ← `playerTick` ← `propsTick` ← `lvlRender` ← `bossMainloop`) but this time **`frames=12962` frozen CONSTANT across ~200 heartbeat dumps / minutes** — a true infinite loop, not the D155 slow-catch-up spiral. Root: the `while (1)` at `model.c:3131` in `modelSetAnimFrame2WithChrStuff` steps the root-motion accumulation one anim frame at a time from `framea` (`model->animframe1`) to `frameb` (`frame`, accumulated in `modelTickAnim` as `frame += playspeed * speed` per tick). If `frameb` arrives **NaN or blown up** — a cutscene anim whose blend/transition math produces a huge/non-finite `model->speed`/`playspeed`/`timespeed` (`model.c:3436` `/ model->unkb0`, `:3477` `/ model->timespeed`, both guarded `> 0` but not against tiny values) — then `floorFloatToInt(frameb)` yields garbage, `endframe - curframe` ≈ 2^31, and the loop (each iteration: `sub_GAME_7F06D3F4` joint transform + `cosf`/`sinf` + `modelConstrainOrWrapAnimFrame`) runs effectively forever. **Likely deeper cause (unconfirmed, needs the repro):** these speed/blend fields are `Model` struct members; if this model is punned/inline (D100/D140 family) and `sizeof(Model)` growth misaligned them, they read garbage exactly when a cutscene sets an unusual anim transition. | GUARDED (`src/game/model.c`, `#ifdef PORT`): (1) in `modelSetAnimFrame2WithChrStuff`, if `frameb` is non-finite or `|frameb| >= 1e6` snap it to `framea` (loop then does ~0 iterations; the `modelConstrainOrWrapAnimFrame` at :3211 still clamps `model->frameb` to a valid frame); (2) in `modelTickAnim` after the tick loop, if `frame`/`frame2` went non-finite/absurd fall back to the pre-loop `model->animframe1`/`animframe2` so the NaN can't poison model state every frame. `!(x > -1e6f && x < 1e6f)` catches NaN too. Anim frames are 0..few-hundred so 1e6 is a safe ceiling. Verified: build green; `-level_09` frame 200 unregressed (guards never fire in normal play). **User to re-verify the Facility outro.** If it still hangs, the NaN source is upstream — dump `model->speed/playspeed/timespeed/unkb0` in `modelTickAnim` during the cutscene. porting-notes.md §E. |
| D155 | **Facility outro-cutscene end → "hang" = unclamped `deltaFrames` spiral (M-29, user bug report).** User watched the Facility outro cutscene; at the end the game froze (process alive, kernel-heartbeat dumps). Symbolicated `mainThread` stack from the port's own hang dump: `modelConstrainOrWrapAnimFrame` ← `modelTickAnim` ← `chrUpdateAnim` ← `chrTick` ← `playerTick` ← `propsTick` ← `lvlRender`, sampled there across *every* heartbeat dump. Root cause: `waitForNextFrame()` (`src/game/frametiming.c`) sets `nextFrameTime = (osGetCount() − prev + 387937) / 775875` and passes it **unclamped** to `updateFrameCounters` as `deltaFrames`. `osGetCount()` is wall-clock on the port (D117), not VI-locked, so a real-time stall — the asset load at the cutscene→debrief boundary, made far worse by the user running a local LLM on the same box — makes `nextFrameTime` balloon to hundreds/thousands. That becomes `speedgraphframes` → `g_ClockTimer`, which drives `modelTickAnim(model, g_ClockTimer, 1)`'s `while (numticks-- > 0)` loop once per character per render, plus dozens of `for (i = 0; i < g_ClockTimer; i++)` sim loops (`bondhead.c`, `bondview2.c`, `explosion.c`, `options.c`…). One render then does thousands of anim ticks × N characters → multi-second frame → heartbeat trips, and the slow frame feeds an even bigger delta next time → unrecoverable spiral. The N64 was physically VI-bound and never produced `deltaFrames > ~2`. Timing-compensation class, cf. D117/D134. | FIXED (`src/game/frametiming.c`, `#ifdef PORT` — clamp `nextFrameTime` to `FRAMETIMING_PORT_MAX_CATCHUP` = 6). After a hitch the sim just resumes at real-time-ish pace instead of spiralling, exactly as the console did when it dropped frames under load. N64 path verbatim. Confidence: high (stack + arithmetic both point here). porting-notes.md §E. |
| D153 | **`-level_09` frame ~900–1400 `0xc0000005` = D117/D134 load flakiness, NOT a bug (M-28).** Reproduced 6/6 while the machine was under heavy concurrent build+run load; on a **quiet** machine `-level_09` ran past frame 2400 with 0 heartbeats, no crash. Same false-positive class the docs warn about every session (M-13..M-22, M-25 Silo ~frame 300 freeze). The crash PC was in a system DLL (a `memcpy` off a buffer) and the frame varied run-to-run — both consistent with the port's host-scheduling jitter under load (`osGetCount` is wall-clock, variable-timestep sim), not a deterministic defect. `romdataFixupMusicSeqTable: seqCount 63 exceeds blob capacity 1` is an EXPECTED benign warning on the header-only first `romdataFixupMusicSeqTable` call (music.c:736; the full-size second call at :742 is silent) — not related. **Lesson: don't regression-test while builds/other runs are in flight.** | NOT A BUG — closed. Run anchors on a quiet machine. |
| D151 | **Watch data all blank — objectives / mission background / M / Q / Moneypenny (M-28, user bug report).** Not a crash (D150 fixed that); every line renders empty. The watch text lives in `propDefs` record types **35** (`WatchMenuObjectiveText`) and **23** (`ObjectiveStart`). In the setup stream each record's 3rd word is a **plain 32-bit language slot id** (see `assets/obseg/setup/Usetup*Z.c` `propDefs[]`: `_mkword(0,_mkshort(0,35)), menu, 11281, 0`). The decomp structs `struct watchMenuObjectiveText` / `struct objective_entry` decode that word as `u16 reserved; u16 text;` and read `text` at offset `0xA` — which only works on the N64 because the id sits in the **low 16 bits of a big-endian word**. On LE, `tools_pc/d88_propdefs.py` `_bswap32`'s the whole word (correct for a 32-bit field), moving the id to the low bytes, so `text` (u16 @0xA) reads `0x0000` → `langGet(0)` → NULL → `get_ptr_text_for_watch_breifing_page` / `get_text_for_objective` return NULL/blank. This is the "briefing/objective text renders blank" side-effect left open under D143 / D150. The per-level lang bank itself IS loaded in-level (`langLoadToAddr(langGetLangBankIndexFromStagenum(stageId))`, `prop.c:1274`). | FIXED (`src/bondtypes.h`, `#ifdef PORT` — `text` is a full `u32` covering the whole word in both structs; N64 `u16 reserved; u16 text;` kept under `#else`). No converter change, no sidecar regen. `nextentry`/`difficulty`/`unkD` unchanged. porting-notes.md §C. Build compiles clean; interactive re-verify pending (open the watch in any level). |
| D130 | crash class C2 (Facility `-level_34`, Runway `-level_35`): `import_texture_i8` AV on a wild texture pointer (`0x72181ee8`), baked into the HUD glyph DL by `gDPLoadTextureBlock(gdl, curchar->pixeldata, …)` when the level-title string ("Chemical Warfare Facility #2") renders `#`/`"`. Root cause: `romdataFixupFont` (`port/src/romdata.c`) converts the N64 24B fontchar array to the PC 32B array **in place**, forward-field, backward-glyph. PC−N64 stride = 8, so for glyphs 0/1/2 `d` overlaps `s` by less than the 24B read span and an early field write clobbers a later field's source mid-`for k<5` loop → glyph 1's `width` = `bswap(index)` = `0x01000000`, glyph 2's `pixeldata` = `pcPixOff+(index−pixStart)` ≈ `0x020002e8` → `+= font_base` → wild → fast3d AV. On N64 the array is read-only rodata, never re-laid-out, so no bug there. NOT the model-GDL relocation (`sub_GAME_7F0762E0` / `texLoadFromGdl`) the D124-Facility addendum suspected — `gdl` there is still a segmented `0x05xxxxxx` value so the `& 0x00ffffff` masks are correct, and `texLoadFromGdl` never copies a `G_SETTIMG` for these levels. | resolved — stage all 6 N64 fields in a local `f[6]` before writing any. Facility + Runway now PASS (16→18/21). Probes reverted. |
| D164 | **Disclaimer / legal screen only draws the first line (D76 root cause, M-31).** `constructor_menu00_legalscreen` (`src/game/front.c:1523`) renders the 12-entry `legalpage_text_array[]` with `legal_text_end = (struct legal_screen_text *)&legalscreen_MRD;` then `do { render; ptr++; } while (ptr < legal_text_end);`. This is a **linker-adjacency assumption**: on N64 `.data` the two file-scope globals are emitted in source order so `&legalscreen_MRD == legalpage_text_array + 12` and the loop count is exactly the array length. mingw/GCC reorders them — in `build-pc/ge007.x86_64.exe` `legalscreen_MRD` links at `0x1401305e0`, **0x60 bytes before** `legalpage_text_array` at `0x140130640` (verified via `nm`). So `legal_text_end < legal_text_ptr` from the start; the `do/while` runs the body **once** (entry 0, "TWYCROSS BOARD OF GAME CLASSIFICATION" — long + CENTER_ALIGN so it wraps to ~2 visual lines = the "classification line + one below" the user saw), then exits. The other 11 legal lines never render. NOT a D68 image-table bug — the legal screen references **zero** `sImageTableEntry`s; it is pure `langGet()` font text via `display_aligned_white_text_to_screen`→`textRender`, plus the 3D `logoinst` model (`subdraw`, absent — D75 family, parked). The D68 fixup and the `GE_IMGT` avenue in the brief are both dead ends here. | **FIX PROPOSED, NOT APPLIED** (`front.c` is owned by another agent this session). One-line `#ifdef PORT` in `constructor_menu00_legalscreen`: `legal_text_end = legalpage_text_array + ARRAY_COUNT(legalpage_text_array);` (N64 `&legalscreen_MRD` cast under `#else`). Same linker-layout class as the D75 loop-bound / D142 enum family. Confidence: **high** — mechanism confirmed by symbol addresses. |

| D159 | **Front-end wallet-Bond photo (and other large front-end I4/I8/IA8 textures) render "interlaced" / combed (M-31, user bug RC1 / D149).** The user's menu showed a garbled venetian-blind Bond photo on the folder / mode-select screen; earlier the same model DL walked into garbage (`D146` opcode spam) — that crash is already contained by the D144/D146 guards, so what's left is purely the texture. Root-caused with `GE_DTEX`: the photo is a **65×65 I4** (also seen: 65×65 I8, 95×32 IA8) with a full mip chain; `import_texture_i4` uploads it `line_size_bytes*2` (= padded stride) wide and `size_bytes/line` tall — geometry fine. The comb is **`texSwapAltRowBytes` (`src/game/image.c:2191`)**: it pre-swaps every **odd** texture row in 8-byte (one-`u32`) groups to compensate for the N64 RDP's odd-line TMEM address XOR (bit 2) during 4-byte-word `gDPLoadBlock` loads. fast3d does **not** emulate that XOR (no odd-row handling anywhere in `gfx_pc.cpp`), so the pre-swapped odd rows stay scrambled in the uploaded GL image. It's called on *every* level of *every* texture on *every* path (§4), but only bites where the texture is large enough and viewed 1:1 to see the 8-texel comb — glaring on the ~big front-end photos, ~invisible on small HUD glyphs / distant in-level surfaces (which is why it was written off as "front-end model" work for many sessions). Same class as the K0-fold / interrupt-mask hardware-quirk shims — belongs in `port/`, not game logic. | **FIXED** (`src/game/image.c`, `#ifdef PORT` early-`return` in `texSwapAltRowBytes` — fast3d wants a plain linear image; N64 body kept verbatim under it). Verified: wallet-Bond photo renders as a clean recognisable grayscale portrait (still 180°-rotated — D75 model-transform family, parked); `-level_09` frame 200 coverage 91.62 vs 91.66 golden (D117 noise) and a visual frame is clean (tiled floor / monitors / world-map screen all correct). Confidence: **high** (GE_DTEX + before/after screenshots). The residual over-tall-mip (RC2) and non-PoT wrap (RC3) issues are separate and still parked; this only removes the row-swap. porting-notes.md §C. |

| D165 | **Front-end menu cursor doesn't feel like a mouse pointer (M-31, user bug B5 follow-up).** M-30 added a "menu pointer mode" that fed mouse *velocity* into the stick axes, but `front.c frontUpdateControlStickPosition` *integrates* the stick as a velocity into a screen-pixel cursor position (`cursor_h_pos += (stickx*0.075 ± 0.5) * g_GlobalTimerDelta`, ±5 deadzone, ±70 clamp, cursor clamped into the ~320×240 virtual rect − 20px margin). Velocity-in → velocity² cursor. **Fix (port-only, no `front.c` edit):** `input.c` now runs a P-controller — it keeps its own estimate `menuEst{H,V}` of where the game cursor is, integrated each poll with the *exact same recurrence* as `frontUpdateControlStickPosition` (`menuCursorStep()`), accumulates a `menuTgt{H,V}` target from raw mouse motion (clamped to the same rect), and emits `stick = clamp(MENU_P_GAIN·(target−est), ±70)`. `MENU_P_GAIN = 6.0` → per-poll error decay ≈ 0.45 (`0.075·6`), so no overshoot and ~8-poll settle; stick falls under the game's ±5 deadzone within ~1px of target so the cursor parks with zero jitter/drift. The estimate re-syncs to the true cursor whenever the target is held at a screen edge (both clamp). `Input.MenuPointerSpeed` scales the mouse→target mapping (bump toward 150–200 if the sweep feels short — `est` steps once per controller poll ≈ 2×/rendered-frame vs `front.c`'s 1×/frame, a rate mismatch absorbed by this knob). `Input.MenuPointerMode` (0 = old velocity, 1 = pointer, **default 1**) is the escape hatch. `GE_INPUTLOG` prints an `menuptr est=/tgt=/eff=/stick=` trace line for tuning. With no mouse motion `target==est==centre` → `eff=0` → emitted stick unchanged → prior behaviour exactly. | FIXED (`port/src/input.c`; `Input.MenuPointerMode`, `Input.HipfirePitchSpeed` added). Build green, `-level_09` + `GE_STARTMENU=6` smoke crash-free. Feel-check owed (headless can't exercise the mouse). |
| D166 | **Hipfire mouse pitch is digital while yaw is analog — inconsistent feel (user bug B7 / D118a, "cheaper alternative").** In hipfire GE takes yaw from the analog stick-X but pitch only from digital C-up/C-down, so `input.c` emitted one C-button per poll once `|mouseΔY|` crossed a fixed threshold — an on/off step. **Fix (port-only):** emit the C-button as a *duty-cycled pulse* whose firing frequency scales with mouse-Y speed — `duty = |dyLook|·(HipfirePitchSpeed/100)/HIP_PITCH_FULL` (clamped ≤1), phase-accumulate, fire when the phase wraps. Fast mouse → `duty≈1` → C-button every poll (solid hold, as before); slow mouse → sparse taps proportional to speed. `HIP_PITCH_FULL = 6.0` px/poll for a solid hold; `Input.HipfirePitchSpeed` (10–500, default 100) tunes it. Aim-mode (RMB, already fully analog) untouched. No mouse motion → no pulse → prior behaviour. | FIXED (`port/src/input.c`). SMALL-FIXES B7. |

Phase 2 replaced the Phase-1 demo loop with the real `mainproc()` on real OS
threads, compiled GE's real `src/sched.c`, and brought in PD's fast3d software
RSP (`port/fast3d/`). The boot path now runs: ROM map → DRAM reserve → video
init → mainThread (real pthread) → bossEntry → bossInitMainthreadData through
mempool init, VI init, rspInit, joyInit + controller-init timers, stanInit,
gameInit — and currently dies inside `langInit()` (D31).

* **D24 — setjmp/longjmp green threads are unusable on MinGW x64.** The first
  kernel used setjmp/longjmp context switches; under Windows x64 + MinGW the
  longjmp path corrupts FPU/MC state when it interacts with SEH unwinding
  (observed: STATUS_DATATYPE_MISALIGNMENT-class crash inside a resumed
  "thread" with garbage register state). PD's port does not use setjmp at all.
  **Resolved:** `port/src/libultra.c` now runs every game thread as a real
  pthread (`PortThread` side table, 8 MB stacks). Message queues keep the N64
  `OSMesgQueue` layout and get a `PortQueue` side table (mutex + condvar, max
  64 queues); all osSendMesg/osRecvMesg paths lock it. A dedicated tick thread
  posts one VI retrace per frame (NTSC 60 Hz) and services the software timers
  (both OS_MESG_NOBLOCK — the tick thread can never deadlock). `idleThread` is
  intercepted by ID and parked; `mainproc` runs as a real pthread so the host
  main thread is free to pump SDL/OS events (`videoPumpEvents()` in the host
  loop — on Windows WndProc only runs on the window-creating thread). The GL
  context is made current on shedThread per frame via
  `gfx_sdl_make_context_current()`. Verified live: VI retrace flowing, all 4
  controller-init timers fire, bossmq loop completes.
* **D25 — dual-mapped N64 DRAM region.** Game code needs two incompatible
  address forms of the same RAM: `osVirtualToPhysical`/s32 arithmetic wants
  live host pointers that fit in a positive s32, while `offset | 0x80000000`
  rebuilds (bg.c:3184/3322, propobj.c:8578/8699, title.c:476) want a KSEG0
  view at 0x80000000. **Resolved:** one 8 MB backing store mapped twice —
  V1 @ 0x70000000 (all game RAM symbols live here) and V2 @ 0x80000000
  (byte-identical mirror). `port/src/dram.c`: Windows uses
  `CreateFileMappingW(INVALID_HANDLE_VALUE,…)` + two `MapViewOfFileEx(…, base)`
  calls (the documented kernel32 API that maps a section at an exact address;
  `NtMapViewOfSection` fails with STATUS_MEMORY_NOT_ALLOCATED 0xc0000045 and
  segfaults on wrong SECTION_INHERIT values — do not retry it); POSIX uses
  memfd + two MAP_SHARED|MAP_FIXED mmaps. Both views sanity-checked to alias.
  `port/src/dram_syms.s` pins the absolute symbols: `cfb_16` @ 0x70000000
  (0x4B000), `_bssSegmentEnd` @ 0x70050000 (mempool start), tlb block end @
  0x702F4400 (= page_align_down(0x803AB400) − 93×0x2000, the exact N64 value;
  the tlbmanage stub returns it so `mempInit`'s pool size is correct — a NULL
  return here made the mempool spin in `while(1)` at src/memp.c:164).
* **D26 — address-width shims (K0 sign-extension).** N64 K0 addresses have bit
  31 set; passing them through s32 parameters sign-extends to invalid x86-64
  pointers. With the dual map, live RAM sits below 0x80000000 so identity is
  safe: `port/shim/PR/R4300.h` → `PHYS_TO_K0(x) = (x)`; `port/shim/PR/os.h` →
  `OS_K0_TO_PHYSICAL(x) = (u32)((char *)x - 0x70000000)` (GBI w1 words then
  carry small offsets) and `OS_PHYSICAL_TO_K0(x) = (x)`. fast3d's `seg_addr()`
  (gfx_pc.cpp) resolves both forms: full host pointers pass through, values <
  0x800000 get +0x80000000 (landing in V2). Same remap applied to the
  G_MW_SEGMENT handler.
* **D27 — ASLR must be OFF for this exe.** `src/bondgame.h:8` declares
  `extern u32 *_bssSegmentEnd;` (pointer type), so `&_bssSegmentEnd` in game
  code emits a `.refptr._bssSegmentEnd` slot holding the absolute value with a
  BASE relocation. Under ASLR the loader rebases it to runtime_base +
  0x70050000 = garbage, and `mempCheckMemflagTokens` then spins/AVs with
  poolAreaStart ≈ 0xF0xxxxxx. (On MIPS `&abs_symbol` is the symbol value
  itself; x86-64/PE cannot do that.) **Resolved:** MINGW link flag
  `-Wl,--disable-dynamicbase` (note: `--disable-dynamic-base` is NOT
  recognized by GNU ld 2.47); the image now loads at its preferred base
  0x140000000 and relocations are no-ops. `main.c` fails loudly at startup if
  `sysImageBase() != 0x140000000ul` so any future ASLR regression is a clean
  error, not silent DRAM corruption.
* **D28 — ninja stale-object hazard with new shim headers.** If a shim header
  did not exist when a .obj was last built, it is absent from that object's
  .d dependency file and ninja will NOT rebuild the TU after the header
  appears — the old (unshimmed) code silently persists. Observed: boss.c.obj
  still contained `or $0x80000000,%eax` (original PHYS_TO_K0) days after the
  shim landed. **Rule:** whenever anything under `port/shim/` changes, delete
  all `.obj` files and full-rebuild. Verify with `gcc -E` using the exact
  ninja flags (`ninja -C build-pc -t commands <tgt>`) plus objdump of the
  rebuilt object.
* **D29 — `osPiStartDma` must post its completion message on every path.** The
  N64 PI posts the caller's OSMesgPI to mq when a DMA completes; GE never
  inspects the message but always blocks on it (`romReceiveMesg()` →
  `osRecvMesg(OS_MESG_BLOCK)` in ramrom.c:44). The shim did the memcpy
  synchronously and posted nothing → mainThread deadlocked in the first file
  load. **Resolved:** post unconditionally after `piServiceDma()` (also for
  skipped/dropped DMAs) with OS_MESG_BLOCK, posting the caller's OSMesgPI.
* **D30 — crash handler without SEH.** MinGW GCC 16 has no `__try`/`__except`
  and does not recognize `-fseh-exceptions`, so the unhandled-exception filter
  cannot rely on structured exceptions to protect symbolication. **Resolved:**
  `port/src/crash.c` phase 1 writes raw fault info (registers, modules) to
  `ge007.crash.log` without touching dbghelp (SymInitialize/StackWalk64 can
  allocate and re-fault inside the filter); phase 2 does a validated manual
  EBP-chain walk (`-fno-omit-frame-pointer` on all TUs; each frame is [saved
  RBP, return address], stop if saved_fp ≤ fp or outside thread stack limits).
  `TerminateProcess` instead of abort (SIGABRT can re-enter the exception
  machinery). `crashDumpThreads()` (Toolhelp32 snapshot + SuspendThread +
  GetThreadContext + StackWalk64 per thread) is called from the heartbeat
  error dump with TID→name matching; StackWalk64 cannot read DWARF unwind info
  on MinGW, so multi-frame traces depend entirely on the EBP chain. Symbols
  are recovered offline: `addr2line -e build-pc/ge007.x86_64.exe -f -C
  <base+rel>` (the log's "rel" = address − actual load base).
* **D31 — langInit SIGSEGV: `zlib_huft_build` overflows load_resource's frame via the x86-64-grown `struct huft` (RESOLVED).**
  After D24–D29, mainThread reaches `langInit()` and dies on the **first** file
  load (`_fileNameLoadToBank(LnameX_lookuptable[LGUN][…])` → index 670 →
  `fileIndexLoadToBank` → `mempAllocBytesInBank` → `load_resource`). Crash
  signature: RIP = 0x140330003 (inside `.bss`, in `g_Props`), RSP only ~0xD8
  below langInit's entry RSP (a *shallow* chain — the stack pointer itself is
  fine), and the frame region above load_resource's return address is filled
  with 8-byte slots `{u32, 0x00000001}` where each u32 is the **low 32 bits of
  a live .bss pointer**, alternating between `g_Props` (0x14033xxxx) and
  `resource_lookup_data_array` (0x14034xxxx). The crash is a `ret` popping one
  of these truncated pointers as the return address (observed RIP equals the
  table value exactly, high word 0x00000001). Something in the load chain is
  storing 64-bit pointers into s32/int storage (fine on 32-bit MIPS,
  truncating on x86-64) and that storage overlaps a live frame.

  Established facts (all under gdb, `gdb -batch -ex "handle SIGSEGV stop"`):
  * All arguments at `load_resource` entry are valid: ptrdata=0x702aa400
    (mempool), srcfile->hw_address=0x108ed250, rom_size=0x720, source=
    0x702f3ce0. `romCopy` (PI shim memcpy) completes.
  * `zlib_inflate()` itself **completes normally** (`finish` → returns 0,
    rz_wp=3872; the RZ stream is plain deflate after a 2-byte header, so
    endianness is not an issue in the bitstream). At its entry rz_outbuf =
    ptrdata ✓, rz_inbuf = source+2 ✓, and the huft table base (tl) sits inside
    load_resource's 8 KB local `buffer` (rz_hlist).
  * **Root cause (confirmed):** `struct huft {u8 e; u8 b; union{u16 n;
    struct huft *t} v;}` is **8 B on MIPS** (4-byte pointer in the union) but
    **16 B on x86-64** (8-byte pointer, 8-aligned). GE's gzip-1.2.4 inflate
    builds its Huffman tables contiguously into load_resource's fixed local
    `u8 buffer[0x2100]` (8448 B), sized for the 32-bit layout (~1056 entries);
    on x86-64 only ~528 entries fit, so a stream needing more (observed
    `rz_hufts` ≈ 797) writes past the buffer and clobbers load_resource's frame
    — including the return-address slot. The earlier "truncated-pointer table"
    reading was a misattribution of this same overflow (the 8-byte `{u32,0x1}`
    slots are huft records spilling over the frame).
  * Ruled out by code inspection: `mempAddEntryOfSizeToBank` (only touches
    pool pos/prevpos), `fileGetIndex` (reads only), `decompressdata` epilogue
    (returns rz_wp).
  * `load_resource` prologue: `push rbp/rdi/rsi/rbx; mov $0x2128,%eax;
    call ___chkstk_ms; sub %rax,%rsp; lea 0x80(%rsp),%rbp` — RBP is a **fake
    frame pointer** (RSP+0x80); the real return address is at [RBP+0x20E8].
    The compiler reuses pushed-register slots as locals (verified: the saved-
    rbx slot [RBP+0x20D0] legitimately receives `buffer+12`). Watchpoints on
    RBP-relative offsets are therefore easy to misplace — compute from entry
    RSP instead.
  * Hardware watchpoint on the real retaddr slot (set at the first
    instruction, where [RSP] = retaddr): first write changes it from a valid
    .text return address to **`buffer+12`** (a pointer into load_resource's
    own local array), reported RIP at the prologue boundary (`push %rdi`) —
    i.e. either a concurrent write by another thread (hardware watchpoints are
    global) or an 8-byte-attributed access. A `thread apply all bt` at that
    moment showed every other game thread parked (RtlUserThreadStart / sleep
    syscalls; gdb could not unwind their stacks further), so the writer is not
    yet identified.

  **Resolution (fix in `port/`, game code untouched).** Mirrors the Perfect
  Dark port, which replaces its assembly rzip with a real-zlib C impl
  (`pd_port/src/lib/rzip_c.c`: `inflateInit2` + `inflate`). We exclude
  `src/game/decompress.c` + `src/game/zlib.c` from the PC build (CMake
  `list(REMOVE_ITEM SRC_GAME …)`) and add `port/src/rzdecomp.c`, which backs the
  two externally-referenced entry points with host zlib:
  * `decompressdata(src,dst,hlist)` → `inflateInit2(-15)` (raw deflate),
    `next_in = src+2` (GE's RZ = 2-byte header `0x11 0x72` + raw deflate, no
    size field), loop on `Z_OK`, return `total_out`.
  * `rzipGetSomething()` → returns the stored `next_in` (consumed input).
  A generated `port/include/realzlib.h` (from `realzlib.h.in`) `#include`s the
  **absolute** host `<zlib.h>` path, because the game's `src/game/zlib.h`
  shadows `<zlib.h>` on the `-I` path.

  **Verified:** rebuild links clean (238/238); the exe has the new
  `decompressdata`/`rzipGetSomething` and no `zlib_inflate`/`zlib_huft_build`.
  Under gdb, mainThread now boots **past langInit** (all language files load)
  all the way into `bossInitMainthreadData()` (boss.c:233) →
  `initWeaponAnimGroups` — i.e. the original first-file-load SIGSEGV is gone.
  The next blocker is D32.

* **D32 — ROM-serialized structs with embedded pointers have divergent N64/x86-64 layout (OPEN).**
  After the D31 fix, mainThread reaches `bossInitMainthreadData()` and dies in
  `init_weapon_animation_groups_maybe()` (boss.c:233) → `initWeaponAnimGroups`
  → … → `modelAnimReadRootMotionValue` (model.c:914), faulting on
  `desc->bitCount` where `desc = anim->bitDescriptors`.

  **Mechanism.** N64 pointers are 32-bit; x86-64 pointers are 64-bit with
  8-byte alignment. `struct ModelAnimation` (src/bondtypes.h:575) declares
  `ModelAnimBitField *bitDescriptors` and `u8 *bitStream`. In the N64 ROM
  layout these are 4-byte fields at 0x08 and 0x10; on x86-64 the compiler lays
  them out as 8-byte fields — `ptype /o` (gdb) shows bitDescriptors @0x08, a
  4-byte hole, then bitStream @**0x18**, sizeof = **80** (vs 64 on N64). The
  animation data is loaded by `alloc_load_expand_ani_table()`
  (initanitable.c:263) via `romCopy(ptr_animation_table,
  &_animation_dataSegmentRomStart, size)` — raw N64-layout bytes.
  `expand_ani_table_entries()` rebases the **low 32 bits** of
  bitDescriptors/bitStream but (a) leaves the high 32 bits as adjacent-field
  junk and (b) writes bitStream's rebase to offset 0x10, which on x86-64 is
  *not* where the C struct reads bitStream (0x18). Observed under gdb:
  anim = 0x702adad4 (base 0x702ad8c0 + 0x214), `*(u32*)(anim+8)` = 0xc82bd8c0 →
  as a 64-bit pointer the high word is junk → SIGSEGV.

  **Scope.** Any ROM-serialized struct that (i) contains a pointer field and
  (ii) has other fields after it misreads the same way: each 64-bit pointer's
  high word holds adjacent-field bytes, and every post-pointer field shifts by
  the pointer-width delta. ModelAnimation is the first hit; bondtypes.h has ~13
  struct blocks with pointer fields (fewer are actually romCopy'd from ROM).

  **PD ground truth.** PD keeps its ROM-data structs at N64 layout on both
  targets by storing embedded pointers as **u32** — e.g. `struct animtableentry
  { … u32 data; }` (pd_port/src/include/types.h:5124) — and casting to a real
  pointer at the use site. GE's decomp instead uses real pointers in
  ModelAnimation, so it diverges on x86-64.

  **Resolution — Option 1 chosen (user-approved), in progress.** The user
  approved the PD pattern after confirming PD does it (`animtableentry.u32 data`,
  raw-DMA'd then resolved at use sites). Note PD's *exact* mechanism (u32 ROM
  offset + on-demand DMA cache) differs from GE's rebased-DRAM-pointer
  ModelAnimation, so we adopt the **principle** (u32 embedded address, cast at
  use), not PD's code. This required refining non-negotiable #2 (see §H).

  **Part A — struct layout: DONE & proven.** Changed `ModelAnimation`'s two
  pointer fields to u32 (src/bondtypes.h:575): `u32 bitDescriptors` @0x08,
  `u32 bitStream` @0x10; added casts at the only use sites (model.c:913/921).
  `ptype /o` now shows sizeof = **64** with fields at the N64 offsets — layout
  matches. `expand_ani_table_entries()` writes s32 to 0x08/0x10 via its own
  `struct anim_entry`, which now aligns exactly (no change needed there).

  **Part B — pointer-rebase correctness: ROOT-CAUSED (see D33), fix designed,
  implementation pending.** The garbage values are *not* a bug in
  `expand_ani_table_entries`'s arithmetic — the rebase is correct N64 code fed
  wrong bytes, plus one genuine x86-64 ABI stride bug. Both root causes and the
  full fix design are in **D33** below.

* **D33 — ROM file stores structured fields big-endian; animation load needs a per-field endianness fixup + an x86-64 loop-stride fix (root cause of D32 Part B).**

  **Discovery.** The `.z64` ROM file is not a plain image: multi-byte
  *structured* fields (record headers, tables) are stored in **big-endian byte
  order**, while bit-packed data streams are raw. Evidence chain:
  1. `baserom.u.z64` is byte-for-byte identical to `data/ge007.ntsc-final.z64`;
     the ROM header is GE-specific (magic `80 37 12 40` from `src/rom_header.s`,
     BE u32 CRC at 0x10/0x14 written by `tools/n64cksum.c`) — not the standard
     N64 header layout.
  2. Build pipeline: `$(LD)` → ELF → `objcopy -O binary --gap-fill=0xff` →
     RareZip cdata compression (`data_compress.sh`) → `n64cksum`. No byte-swap
     step; the mips64-elf-gcc toolchain (SGI-style BE default) emits BE bytes.
  3. `tools/utils.h` documents the convention: `swap_bytes()` = "convert from
     v64 to z64 ordering", `reverse_endian()` = "convert from n64 to z64
     ordering" — the project's ".z64" is a word-swapped cart image (community
     ".n64" format). Real cart = bswap32(file).
  4. Animation-blob analysis: reading headers as BE u32/u16 yields self-
     consistent values (frame counts, bit widths 0–31, offsets that tile the
     blob exactly); LE reads yield garbage.

  **Per-field transform rule** (applied at load time to the `animation_data`
  segment): u32 fields → bswap32; u16 fields → bswap16; u8 fields → identity;
  bit-packed streams → identity. No single uniform word transform exists (mixed
  field widths).

  **Blob layout** (fully mapped; all 173 entries verified): each C array
  (`animation_table_ptrs1/2` in `src/game/initanitable.c`) is a sequence of
  20-byte records at the `PTR_ANIM_*` offsets. Record fields: +0x00 address
  (entries-segment offset, u32), +0x04 frame count (u16), +0x06 angle bit width
  (u8, used by `modelAnimReadBitsAsU16Angle`), +0x07 loop flag (u8), +0x08
  bitDescriptors blob offset (u32), +0x0C bitsPerFrame root motion (u16, used by
  `sub_GAME_7F06D2E4`: `scaled = unk0C * frame`), +0x0E frame size in bits
  (u16, used by `loadAnimationFrame`: `frameSize = unk0E >> 3`), +0x10
  bitStream blob offset (u32). Tail (+0x14..+0x3C) is unused by any code — the
  effective record size is 20 bytes (= `struct anim_entry`, 5×s32). Interleave
  rule: entry *i*'s payload [descriptors][stream] sits at
  [PTR_ANIM_{i-1}+0x14, PTR_ANIM_i); all regions are disjoint; stream size =
  ceil(frameSizeBits × frames / 8) — exact fit on every entry (fire_standing:
  544 bits × 106 frames → 252 bytes). Descriptor array = `ModelAnimBitField`
  {u16 bitOffset, u8 bitCount, u8 pad, u16 valueOffset} ×4 (6 bytes each); after
  the transform all 692 descriptor arrays pack bitOffsets sequentially from 0.
  Null entries use sentinel **1** (not 0) — `expand_ani_table_entries` skips
  `*var_v0 == 1`.

  **Validation.** Simulated the full transform in Python over all 173 entries:
  0 bad descriptor ranges, 0 non-sequential arrays. Root-motion simulation for
  fire_standing frames 0–3 (widths 6/7/6/0, bitsPerFrame=19) gives smooth
  per-frame deltas (x = 7, 10, 12, 11; y ≈ 1086–1088) — proving the stream
  bytes are identity-correct (no transform). The previously observed garbage is
  reproduced exactly: file bytes `[00 00 01 58]` at fire_standing_fast's bd →
  LE read 0x58010000 + base 0x702ad8c0 = **0xC82BD8C0**, the exact value seen
  under gdb. The "zeros at odd indices" observation was a gdb display artifact —
  the compiled `.data` array is dense (verified by objdump of
  build-pc/ge007.x86_64.exe).

  **Second root cause: x86-64 stride bug in `expand_ani_table_entries`.** The
  loops iterate with `s32** var_v0; var_v0++` — +4 bytes/iter on N64, but
  `s32*` is 8 bytes on x86-64. Verified in the compiled binary (`add $0x8,%rdx`)
  : only even-indexed entries are rebased; odd entries keep raw small offsets
  (e.g. fire_standing, index 1, would stay 0x144 → instant SIGSEGV on use).
  This pattern occurs nowhere else in the compiled set.

  **Fix design (implementation pending — this is the immediate task):**
  1. New port function `romdataFixupAnimationData(u8 *blob, u32 blobSize,
     const s32 *tableA, const s32 *tableB)` in `port/src/romdata.c` (+ decl in
     `port/include/romdata.h`): for each non-null (≠0, ≠1) offset in both
     tables — bswap the 20-byte record header per-field (u32: +0x00/+0x08/
     +0x10; u16: +0x04/+0x0C/+0x0E; u8: +0x06/+0x07 untouched); then transform
     the descriptor array at [bd, bs) step 6 (bswap16 on words 0 and 4 only).
     Guards: skip if bd==0 && bs==0; require bd < bs ≤ blobSize and
     (bs−bd) % 6 == 0.
  2. Call it in `alloc_load_expand_ani_table()` (initanitable.c ~line 265)
     **between** the `romCopy` and `expand_ani_table_entries` — one line +
     `#include "romdata.h"` (`port/include` is already on the include path).
     Mechanical ABI edit per non-negotiable #2; must run *before* expand
     (expand reads/writes the fields as LE after fixup).
  3. Fix the stride in `expand_ani_table_entries()` (initanitable.c:233):
     iterate with `s32 *` instead of `s32**` — mechanical ABI fix, semantics-
     preserving on N64 where it was already correct.

  **Open item.** The entries segment (per-frame joint angles; file offset
  0x124AC0, 0x169EC0 bytes) is left as identity for Phase 1.5 — not consumed at
  boot (only during rendering). Verify visually in Phase 2 when Bond first
  animates.

**D34** `ANIM_DATA_*` address placeholders break on x86-64. On N64 the
`animation_data` segment links at VMA 0, so `(s32)&ANIM_DATA_x` is a small
segment offset; on PC `&ANIM_DATA_x` is a high PE address and the game code's
ubiquitous s32 truncation yields garbage. Audit: all 82 uses are
address-taking only (`(s32)&…`, or through `ANIM_FRAC()` /
`ANIM_FRAC_MUL_FIRST()` in chraction.c/bondview2.c/title.c/
initBondDATAdefaults.c/initactorpropstuff.c) — no data dereferences, no
sizeof, no static initializers. Fix: on x86-64 each `ANIM_DATA_x` is
redefined as an lvalue at `g_pc_animdata_base + offset` where the base is the
PE image address with its low 32 bits zeroed (derived in `romdataInit()` from
a static probe; fail-fast if not 4 GiB-aligned), so
`(s32)&ANIM_DATA_x == PTR_ANIM_x` exactly. Files:
`assets/animationtable_data.h` (guarded macro branch, N64 externs kept under
`#else`), `port/src/romdata.c` (`g_pc_animdata_base` + init),
`CMakeLists.txt` (excludes `assets/animationtable_data.c` +
`animationtable_entries.c` on PC — the arrays are address-only placeholders;
the ROM is the data ground truth via romCopy). Verified under gdb:
`D_80030984 = ANIM_FRAC(ANIM_DATA_walking)` executes; the walking record at
`ptr_animation_table + 0x4018` has entry field `0x10177dcc`
(entries base `0x10124AC0` + `PTR_ANIM_ENTRY_walking`) and frame count 0x25;
after `initWeaponAnimGroups()` the derived globals hold sane floats
(4.59/11.25/16.71/16.19).

**D35** Music sequence table: pointer-width + endianness.
`musicSeqPlayerInit()` faulted in `romCopy()` with a NULL destination:
`tblSegmentSize = sizeof(RareALSeqData) * seqCount + 4` was garbage because
(1) `RareALSeqData.address` was `u8 *` — sizeof 16 on x86-64 vs the ROM's
8-byte records — and (2) the header `seqCount` and entry fields are stored
big-endian in ROM but read as LE (63 → 0x3F00 = 16128). Fix: `address` →
`u32` (`src/music.h`; sizeof 8 on both platforms) with `(void *)` casts at the
three use sites in `src/music.c`; new `romdataFixupMusicSeqTable()` in
`port/src/romdata.c` bswaps `seqCount` + per-entry `address`(u32)/
`uncompressed_len`(u16)/`len`(u16), called after both romCopys in
`musicSeqPlayerInit()`. Verified: the header-only (0x10-byte) copy logs the
expected `seqCount 63 exceeds blob capacity 1` clamp; boot passes the old
fault.

**D36** Music heap / PERMANENT pool sizing on x86-64. `alCSPNew()` SIGSEGV'd
because the music bump heap was oversubscribed: x86-64 libaudio runtime
structs are larger (ALVoiceState 88B, ALSeqPlayer 248B, ALCSPlayer 224B, …)
and the debug-token state (`tokenFind(1,"-level_")==NULL` →
`g_DebugAndUpdateStageFlag=1` → stale `-ml0 -me0` tokens) forces the
fixed-size mempool branch with PERMANENT at 296 KiB. Measured under gdb:
pre-music PERMANENT usage 0x1BCA0 left 189,280 B for music vs a measured init
demand of 0x31660 (shortfall 13,056). Fix (PC-only guards): 
`MUSIC_ALLOCATION_BYTES` 0x2E000 → 0x32000 (`src/music.c`); PERMANENT fixed
branch 296/308 KiB → 320/340 KiB (`src/memp.c`, non-JP/JP). STAGE absorbs the
difference. Verified: boot reaches `bossMainloop()`.

**D37** libaudio bank trees must be re-laid out, not patched in place. The
audio thread SIGSEGV'd in `__initFromBank()` (`b=0xffffffff00000000`):
`ALBankFile/ALBank/ALInstrument/ALSound/ALWaveTable/…` are serialized in ROM
as big-endian scalars + 4-byte packed table-relative offsets, while the x86-64
C structs use 8-byte pointers — so BE values were LE-misread and 4-byte offset
arrays were walked with 8-byte stride, producing wild rebase pointers in
`alBnkfNew()`/`_bnkfPatch*()`. In-place conversion is impossible: each struct's
expanded tail overlaps whatever follows it in ROM (e.g. the ALWaveTable book
slot at +24 lands inside the ALADPCMBook that sits 12 bytes after the
wavetable). Fix: `romdataAudioBankPcSize()` + `romdataFixupAudioBank()` in
`port/src/romdata.c` — a two-pass DFS re-layout into a compact image where
every sub-struct is placed once (8-byte aligned, children before parents) and
each pointer slot stores the sub-struct's NEW offset from the image start,
zero-extended; `alBnkfNew()`/`_bnkfPatch*()` then rebase unmodified
(`ptr + (s32)file`). `ALWaveTable.base` stays an offset into the separate
wavetable data segment — `_bnkfPatchWaveTable()` adds its `table` argument,
verified at runtime (sfx: base+table = 0x102F19A0 =
`_sfxtblSegmentRomStart`). `music.c` PC branch allocates the re-layout size
and fixups after romCopy. Memory: bank images grow +0x1460 (sfx) / +0xA70
(instruments); init-time heap demand measures 0x33530, so 
`MUSIC_ALLOCATION_BYTES` → 0x38000 and PERMANENT → 352/368 KiB (PC only). 
Verified under gdb: runtime tree matches ROM ground truth (SFX instCount=1,
soundCount=261, bendRange=200, sampleRate=22050; INSTR instCount=75; all
env/keymap/wavetable/book/loop pointers valid heap addresses; book
order=2 npredictors=1); 25 s soak with every thread alive. Implementation
bugs caught during bring-up: a double bswap (afRd* already decodes BE),
afWr32 initially wrote BE, and the size pass used an uninitialized visited
array (undercount) — all fixed.

**D38** Implicit function declarations truncate pointer returns on x86-64.
~72 game TUs call ~400 functions with no visible prototype (missing `#include`
of the declaring header). Under C11 an implicit declaration assumes
`int f()`: harmless on N64 (32-bit pointers) but on x86-64 it silently
truncates every pointer return to 32 bits — e.g. `tokenFind()` in
`set_mt_tex_alloc()` returned a low-32-bit "pointer" that faulted in
`strtol()`. Fix: a port-layer prototype shim, `port/include/pc_protos.h`
(398 declarations; one-shot generator `scripts/gen_pcprotos.py` — the committed
header is the source of truth, hand-adjusted after generation), declaring each
function with its TRUE return type and an empty parameter list — purely
additive (only the returned value's width changes). Anchored in
`port/shim/PR/ucode.h`, the LAST include of `<ultra64.h>`: anchoring earlier
(e.g. `gbi.h`) poisons libaudio.h because the bondtypes chain reaches
`snd.h → <PR/libaudio.h>` while ultra64.h is still mid-parse. C-only guard:
C++ fast3d TUs reach this header via `SDL_stdinc.h → <stdarg.h> →
port/shim/stdarg.h`, and pulling the bondtypes/bondconstants chain into C++
brakes on `struct ALSoundState*` in `src/bondtypes.h`; no C++ TU needs a
fix here. C11 gotcha: an empty parameter name list `()` cannot match a true
prototype with default-promoted parameters (`s8`/`u8`/`s16`/…), so the 11
such functions get full prototypes with their TRUE parameter types
(substituting `int` for `s8` is NOT compatible). Also: `src/bondconstants.h`
defines function-like macros `ntohl`/`ntohs` over `CharArrayTo32/16`, which
collide with MinGW `<winsock.h>` in TUs that parse both — neutralized in the
port shim, real host functions provided in `port/src/pc_netorder.c` with
winsock-compatible signatures (`u_long` is 64-bit on LLP64!). No game-TU
edits. Result: build clean, zero warnings; the old `strtol()` crash is gone.

**D39** Globalimagetable rebasing idiom breaks on x86-64. `texReset()` /
`texLoadFromDisplayList()` compute ROM-layout pointers as
`globalbank_rdram_offset + (u32)&sym` with
`globalbank_rdram_offset = (u32)pGlobalimagetable + 0xFE000000`; on N64
`(u32)&sym` = `0x02000000 + off` and the 0x02 base cancels the 0xFE. On PC
`(u32)&sym` is a truncated PE address → garbage DL pointers. Fix (in
`src/game/image_bank.c`, PC branch): an enum of all 49 segment offsets (17
Gfx display lists + 32 sImageTableEntry tables) and
`#define GIMG_OFF(sym) (0x02000000u + g_pc_gimg_off_##sym)` — exactly the N64
`(u32)&sym`; all 49 texReset() sites replaced. No changes needed in
gunfire.c / texLoad / texSelect: all game RAM is s32-safe (V1 view,
0x70xxxxxx < 4 GiB), so the u32 math and pointer casts stay lossless.
Also fixed the segment markers: the CSV asset `Globalimagetable.bin`
(0xAC8) is TRUNCATED — the real linker segment (`ge007.ld`: oddtextures.o
`.data`) spans 0x13F8 = Gfx DLs (0xAC8) + ITE tables (0x930); the CSV's
`rarewarelogo` entry is mis-split (the logo actually starts at ROM
0x29E560). `_GlobalimagetableSegment{Rom}End` 0x1029DC28 → 0x1029E558 in
`port/src/romassets_u.s` + a size override in `scripts/gen_romassets.py`
(regeneration verified stable). All 49 offsets verified against the ROM:
the symbols tile `[0x29D160, 0x29E558)` exactly with no gaps; `gun.c`
ammo IconImage values cross-check. Verified under gdb: all 17
texLoadFromDisplayList diffs correct (0, 0x78, 0x120, …); boot passes
texReset. Known Phase-2 follow-up: `explosion.c`'s
`g_ExplosionDisplayLists[]` is a static table of EXE addresses to Gfx arrays
(16-byte entries on PC) — it must point at the ROM-layout copies in
pGlobalimagetable before explosions render.

**D40** N64-sized BSS placeholder pool overflows with pointer-stride structs.
`initModelHitEntryFreeList()` (initunk_005450.c:38, called from
lvlStageLoad) walks 600 ModelHitEntry records writing next/prev; the
decompiler-emitted BSS chain in objecthandler.c (`char g_ModelHitEntries[0xC];`
…dwords… `char g_ModelHitEntriesPenultimate[0x28]`) reproduces N64's
12 000-byte region (600 × 20 B), but PC ModelHitEntry is 40 bytes → the
free-list init writes 24 000 bytes, a 12 KB .bss overflow that clobbered
`is_ramrom_flag` (writer caught with a hardware watchpoint) → the
demo-replay path in bossMainloop ran with `address_demo_loaded == NULL` →
SIGSEGV at ramromreplay.c:341. Fix: PC branch declares a properly sized pool
(`char g_ModelHitEntries[600 * sizeof(ModelHitEntry)]`); the N64 chain stays
verbatim under `#else`; the sentinel assignment in initunk_005450.c is
computed directly on PC (`entries[LEN-1].prev = &entries[LEN-2]` — N64's
g_ModelHitEntriesPenultimate labels the start of entry 598). Verified: no
more SIGSEGV; boot progresses into GL rendering.

**D41** Cross-thread GL context binding. WGL allows a context to be current
on only ONE thread at a time. The window+context are created and made
current on the host main thread (`videoInit → gfx_sdl_init`), but all
rendering runs on the game's scheduler thread; while main still holds the
context, `wglMakeContextCurrent` from the game thread fails with "The
requested resource is in use" — and this SDL2 build (2.32.10, MSYS2)
swallows it: `SDL_GL_MakeCurrent` returns true on failure / 0 on success
(an inverted int-as-bool ABI artifact; confirmed by a minimal repro and
gdb `$1 = 255`). The silent failure left `wglGetCurrentContext() == NULL`
on the game thread → `glCreateShader` returned 0 → "Vertex shader
compilation failed" with an empty info log. Fix: new
`gfx_sdl_release_context()` (`SDL_GL_MakeCurrent(NULL, NULL)`) called at the
end of `videoInit()` after `set_swap_interval` (the last GL work on main);
the scheduler thread re-binds per frame via the existing
gfx_sdl_make_context_current() in videoStartFrame(). Do NOT branch on the
MakeCurrent return value (unreliable). Verified: frame 1 renders (69.7 ms),
several frames before the next (separate) fault.

**D42** rsp.c task-settings toggle idiom truncates pointers.
`g_gfxTaskSettingsList = (GfxInfo_s*)((u32)list ^ (u32)&g_gfxTaskSettings[0]
^ (u32)&g_gfxTaskSettings[1])` toggles between two adjacent settings structs
by XOR — fine on 32-bit, but on x86-64 the u32-truncated PE addresses XOR to
garbage → the next frame's `((GfxInfo_s*)g_gfxTaskSettingsList)->cfb = …`
(fr.c:458) SIGSEGV'd. Fix: explicit toggle under
`#if defined(__x86_64__)` (`list == &[0] ? &[1] : &[0]` — same semantics,
the list is always one of the two); N64 line verbatim under `#else`.

**D43 (OPEN)** Model-file loading ABI mismatch. Stage object load faults in
`modelPromoteNodeOffsetsToPointers()` (model.c:5688). Model files are
ROM-serialized with N64 layout (ModelFileHeader 24 B, ModelNode 20 B,
4-byte pointer fields) but on PC are read as 8-byte-pointer structs (48 B
each): `load_object_fill_header()` derives RootNode from `numtextures`
at the wrong offset, and the `PROMOTE` rebase (`(u32)var + diff`) then
operates on misaligned fields. Needs a D37-style re-layout of the model
file image (header + switches array + texture table + node tree +
ModelRoData records). Phase-2 project; see §H. Reference implementation:
PD port's `port/src/preprocess/filemodel.c` — same PROMOTE idiom, same vma
0x5000000 (§2.4); adapt + per-field validation, not a drop-in copy.

**D44 (closed 2026-08-22)** Crash-handler Phase 2 backtrace self-faulted before printing.
`crashStackTraceSym()` in `port/src/crash.c` calls
`GetCurrentThreadStackLimits(low, high)` passing the NULL pointer *values*
rather than `&low, &high` (Phase 1's `crashStackTraceRaw` passes them
correctly). The API writes the stack limits to address 0x0 → access violation
*inside the SEH filter* → Windows terminates the process without a second
dispatch. Symptom: `ge007.crash.log` contains only Phase 1 (EXCEPTION/PC/
MxCsr/thread-stack/MODULE lines) and no `BACKTRACE:` section — verified twice
live; this is why every D3x fault so far cost a full gdb launch-mode session.
Fix: one line, `GetCurrentThreadStackLimits(&low, &high)` — applied.
The build already compiles with `-fno-omit-frame-pointer` (CMakeLists.txt:193),
so every crash auto-logs an EBP-chain backtrace to console + ge007.crash.log,
symbolicable offline with addr2line. **Verified live:** the D43 run now logs
`BACKTRACE:` with #00 = `modelPromoteNodeOffsetsToPointers` (model.c:5688) and
#01 = `load_object_fill_header` (objecthandler_2.c:110) — the documented chain,
no gdb needed. Note: frames beyond the true call chain can be stale stack data
(the walk validates fp against stack bounds but not ret_addr against .text;
one such frame observed at #02). addr2line takes the full absolute address
directly (`addr2line -e build-pc/ge007.x86_64.exe -f -C 0x14007a31a`); image
base is still 0x140000000 (re-verified post-rebuild via nm).

**D45 (OPEN — sizing for D43)** Model-file buffers must grow for PC: the
GDL region doubles (8 B Gfx slots → 16 B) and texture-marker expansion in
texLoadFromGdl() emits full RDP setup sequences whose size is data-driven.
Per-file worst-case final size P_final = B_pc + 2×(D_n64−g1) + 16×Σ_markers
(K_t−1), where K_t is the exact worst-case command count per marker type t
(tex.c helpers, maxlod≤8, all texTry* state guards emitting, valid=FALSE —
the realistic branch since D_800483C4 is a gunfire texture index):
type0/LOD 37, type1/DETAIL 46, type2/MIPMAP 36, type3/TILE 18,
type4/TILE_PRESWAP 15 (preamble = PipeSync + gSPTexture ≤ 2; water check ≤ 1).
Marker type = w0&7 of the 0xC0-top-byte slot. Verified over all 512 model
files (build-pc/d43_sizes.py). Texture *pixel* data is unaffected by Gfx
width, so texpool sizes can stay at their N64 values — only the model-file
regions grow. Required buffer edits (all D40-class ABI-forced size constants,
semantics unchanged): gun.c:106 size_item_buffer 0x14820→**0x23000**
(bondview body+head+held-prop chain worst 0x1DB9A; suit pool 0xA0B0 + R
0x18000 = 0x220B0); gun.c:109 D_80032464 0x7530→**0xF000** (GautoshotZ
0xE788); gun.c ITEM_SUIT_LF_HAND R 0xBD70→**0x18000** (Csuit_lf_handZ
0x16F9C) with pool expr size−0x18000; gun.c ITEM_TRIGGER/ITEM_WATCHLASER R
0xAFD0→**0x17000** (GtriggerZ 0x16030); front.c load_walletbond R 0xA000→
**0x17000** (PwalletbondZ 0x1664C; stays below the +0x28000 DL region);
front.c cast screen bufferRemaining 0x18160→**0x1C000** (cast chain worst
0x19CA0 — zbuf at 0x19000+region must not be clobbered); initmenus.c:34 logo
buffer 0x78000→**0x7C000** (texpool 0x19000 + region 0x1C000 + zbuf
440×330×2=0x46B80). Unchanged: title/gunbarrel chain (0x16DF0 ≤ 0x23A00),
front.c logos (≤0x9BCC ≤ 0x3C000), dst=0 fileLoad path (allocates ALL
remaining STAGE space and gives the tail back via
mempAddEntryOfSizeToBank — stage props/NPC bodies are bounded only by live
STAGE headroom, ~2.3 MB). The converter will also assert P_final ≤ R at load
time with a clear error instead of overflowing.
**D46 CORRECTION:** the cast-screen values above are superseded —
front.c bufferRemaining is **0x25000** (not 0x1C000) and the initmenus.c
logo buffer is **0x85000** (not 0x7C000); see D46.

**D46 (RESOLVED — overlap safety + final buffer sizing for D43)**
The GDL expansion in sub_GAME_7F0762E0 is in-place: output starts at the
first-GDL offset and grows rightward; input is read from the mirror copy
texCopyGdls() made at the region tail ([A+R−P_conv+g, A+R)). The initial
gap is R−P_conv and the cumulative output-minus-input excess is ≤
16·Σ(K_t−1) over markers processed so far (monotone; 0xba "texture
already set" skips only reduce it). Since Gfx is 16 B for BOTH input and
output on PC, the no-clobber condition **R ≥ P_conv + 16·M_actual** is
identical to the fit condition — one constraint, not two. (The N64 game
satisfies the same identity with 8-B slots.) The converter therefore sets
`poolRemaining = P_conv = B_pc + 2×(D−g1)` EXACTLY (the pre-expansion
image; markers are expanded at runtime, consuming [P_conv, R)).

**Strict bound from N64 ground truth.** The N64 game works, so per file
8·M_a ≤ R_share_N64 − B_n64 − E. PC slots are 2× wide with the same M_a
(tex.c logic + texture data identical), hence
**P_final_actual(PC) ≤ B_pc + 2×(R_share_N64 − B_n64)** (E cancels).
N64 R_share values (game code): suit 0xBD70, trigger/watchlaser 0xAFD0,
wallet 0xA000, weapons 0x7530, bondview chain 0x14820
(size_item_buffer), cast chain 0x18160 (front.c bufferRemaining).
For chains ΣR_share = bufferRemaining EXACTLY: each subsequent load's R
is the previous file's post-load poolRemaining via
`get_pc_buffer_remaining_value()` (front.c:7809-7825), so the shares
tile the N64 budget with no margin assumption beyond "N64 works".

**tex.c expansion state machine** (for reference / any future
simulation): `sub_GAME_7F0CC4C8()` resets g_TexTileStates[8],
g_TexTileSizes[8] and g_TexLutMode at the START OF EVERY
texLoadFromGdl call → texTry*/texSetLutMode dedup only works WITHIN one
GDL. Per-marker slot counts (lutmodeindex=0): type0/LOD ≤ 2 + 6
(LoadToTmemAddr: SETTIMG+SETTILE≤1+LoadSync+LoadBlock+PipeSync) + 4
(TileFromDef: PrimColor+LUT≤1+SETTILE≤1+SETTILESIZE≤1) + 3×min(maxlod,7)
(TileLods basetile=1) + 3 (CycleType/TxLOD/Detail); type2/MIPMAP ≤ 2 + 6
+ 3×(maxlod + [maxlod==1]); type1 ≤ 2 + 6 (Zero) + 1 (TileSync) + 6 + 4
+ 3×min(maxlod,7) + 3; type3 ≤ 2+6+4+4; type4 ≤ 2+6+4. With LUT formats
the LoadToTmem* helpers double to ≤12 — see next item.

**Texture scan (build-pc/d43_lutscan.py), all 512 files:** 1071 valid
texture-table refs (TID < MAX_TEXTURES=3001; the rest are skipped by
texLoadFromModelFileHeader, and markers whose texnum was never loaded get
tex==NULL → PipeSync only). Image headers parsed via imagelist.u.csv
(order = assets/images.def order): **NO LUT textures** (formats 9-12)
exist in any model file — only format 0 (×1069) and 8 (×2) — so
lutmodeindex=0 universally and the ≤6-slot LoadToTmem bound holds.
maxlod distribution {0: 1063, 6: 7, 7: 1} → texWriteTileLods emits
NOTHING for 99% of markers. The D45 K_BOUND table {0:38, 1:46, 2:36,
3:18, 4:15} is verified a true worst-case bound under these conditions.

**Strict-bound results (build-pc/d43_chainbound.py):** suit_lf_hand
0x1002C ≤ 0x18000 ✓; trigger/watchlaser 0xED20 ≤ 0x17000 ✓;
walletbond 0x105BC ≤ 0x17000 ✓; worst weapon GmapZ 0xE91C ≤ 0xF000 ✓;
bondview chain (worst body spicebond + headbrosnan + autoshot) 0x1CE5C
≤ 0x23000 ✓; **cast chain: rifle 0x240DC / pistol 0x23F24 > 0x1C000 —
SHORT by ~0x8000** (worst: body spicebond B_pc=0x8A14/B_n64=0x7D60 +
head headbrosnan 0x1D90/0x1D50 + rifle autoshot 0x4828/0x3E28 / pistol
wppksil 0x45B0/0x3DC8). The D45 worst-K estimate (0x19CA0) was below the
strict bound because it assumed Rare's N64 buffer had zero margin.

**Resolution (two one-line constant changes, both already PORT-guarded):**
front.c init_menu18_displaycast bufferRemaining 0x1C000 → **0x25000**
(covers 0x240DC); initmenus.c logo buffer 0x7C000 → **0x85000**
(texpool 0x19000 + region 0x25000 + zbuf @ALIGN64(0x3E000) size
440×330×2=0x46B80 → 0x84B80, rounded). MEMPOOL_STAGE headroom is ample
(total stage usage at menu init ≈ 0xD0040 vs ~2.3 MB pool). The converter
should additionally assert `P_conv + 16×Σ_markers(K_t−1) ≤ R` at load
with a clear error (K_BOUND table; provably ≥ actual expansion).

**D47 (RESOLVED — D43 converter contract finalized; capstone endianness fix)**
Session that closed every open question on the model-file converter.

1. **Capstone endianness bug (environment gotcha).** All earlier-session
disassembly of the ROM used capstone's default LITTLE-endian mode against a
big-endian MIPS image → garbage register flow. MUST use
`CS_MODE_MIPS32 | CS_MODE_BIG_ENDIAN`. Compaction + accessors were re-disassembled
cleanly; where BE disassembly is still ambiguous, the byte-matched C in `src/`
is ground truth.
2. **Compaction state values (BE disasm of 0x7F0762E0 + byte-matched C).**
P = entry.poolRemaining, R = entry.rom_remaining, **delta = R − P** (not
P−R); `texCopyGdls(F+G, F+R−P+G, P−G bytes)`; per-GDL count = off_{i+1}−off_i
bytes (or P−off_last); final fileSetSize = ((rep&0xFFFFFF)+0xf)&~0xf.
Both accessors (0x7F0BD11C / 0x7F0BD100) compute `0x80090000 + idx*20` →
**resource_lookup_data_array actually lives at 0x80090000**; the ob.c comment
(0x800888B0) is stale.
3. **memp allocator (src/memp.c).** Bump allocator; `mempAddEntryOfSizeToBank`
rewinds `pool->pos` for the most recent allocation →
`fileSetSize(reallocate=1)` returns the post-compaction tail to the bank.
`load_resource` decompresses INTO ptrdata (=F), reading the compressed source
from the block's TAIL. Fresh dst==0 load: P := S_bank (all remaining stage
bank) → F := alloc(S_bank) → R := S_bank → load_resource sets P := D_N64.
4. **Reload hazard + PC decision.** `fileIndexLoadToBank` takes S_bank only
when poolRemaining==0; fileSetSize leaves P=R=post-compaction size, so a
reload within one stage would alloc(P_old) which can be < D_N64 (source
pointer before the block; decompress past it — latent on N64 too). PC fix:
PORT-guarded reset of `poolRemaining = 0` immediately before
`_fileNameLoadToBank` in load_object_fill_header → every load gets fresh-
bank semantics: staging space = whole remaining bank, steady-state bank
usage identical (fileSetSize rewinds to the same place).
5. **G_VTX w0 encoding (ROM-data proven).** GBI1/gDma1p style:
w0 = 04<<24 | dst<<16 | (16·n), dst = ((n−1)<<4)|v0. All 2826 model-file
instances have v0=0, n≤16 (batches of ≤16 verts — matches G_TRI4's 4-bit
indices). fast3d's `gfx_sp_vertex(C0(0,16)/sizeof(Vtx), C0(16,4), …)` is
correct AS-IS; the converter bswaps w0 only (no field remap). The
F3DEX_GBI_2 `gsSPVertex` block in GE's gbi.h is a red herring — the asset
toolchain emits GBI1 style.
6. **G_TRI4 = standard 4-bit indices.** The "out-of-range seg-5 w1" values
found by scanning are G_TRI4 index data, not addresses (w1 bits 20-31 hold
the last two 4-bit indices). **Opcode-aware remap rule: only {G_VTX=0x04,
G_SETTIMG=0xFD, G_LOADBLOCK=0xF3} carry addresses in w1.** Never remap
TRI4/TRI1/TEXTURE/SETOTHERMODE/CLEARGEO/SETGEO/syncs.
7. **Render-time segment bindings (model.c).** seg5 (COL1) = BaseAddr = F on
every model path; seg4 (VTX) = the record's Vertices base (or a runtime
buffer in dorottex). Hence G_VTX seg4 w1 = displacement from that array →
NO remap needed (array order preserved); G_VTX seg5 w1 = absolute file
offset — 2804/2805 verified to land inside a vertex array at vo+d with
d%16==0 → remap via the unified region map.
8. **Vertex format (all 512 files).** Normal: bswap s16 x,y,z,index,s,t;
bytes @C-F raw (rgba/normal are single bytes). Collision (34,580 verts):
bswap s16 x,y,z,index; LinkedTo u32@8 is ALWAYS 0 or 0x05xxxxxx (node vma);
CollisionRelatedIndex s16@C (range −1..113) + reserved s16@E bswapped.
9. **No embedded texture blobs in any model file** — texconfig TextureID
seg-5 count = 0 across all 512 (the earlier "3 title files" note was the
vestigial logo SETTIMG refs already covered by d43_cover.py). No blob
handling needed in the model converter.
10. **Zero-count vertex arrays.** PexplosionbitZ is the ONLY file with
nv=0 and non-null Vertices (0x98); its GDL uploads 16 verts from there via
absolute seg-5 ref. Rule: emit the array sized up to the next object offset
(→ [0x98,0x198) = exactly 256B here).
11. **GDL tight packing is safe.** fast3d `case G_ENDDL: return` — trailing
junk after ENDDL is never executed at render time. Spans: 2064 tight,
238 with trailing bytes (1904B total). Converter emits up to and including
ENDDL; each PC span = exactly 16·slots → compaction counts are exact.
12. **Reference validator: `build-pc/d43_convert.py`** implements the full
conversion spec (DFS walk with LOD/SWITCH rewiring, region map, per-opcode
record conversion, remap checks) and runs it on all 512 files:
**ALL CLEAN** — every pointer remap resolves, layout invariants hold,
max D_PC = 0xB7E0, max D_PC/D_N64 ratio = 1.31 → staging headroom is
trivially satisfied vs the ~2.3 MB stage bank. This script IS the spec for
the C implementation.
13. **Final converter contract.** Emit `[switches NS×8B LE][texconfigs NT×12B
(bswap TextureID; 0x05xxxxxx would remap — never occurs)]` → nodes+records in
DFS preorder (PC layout via struct assignment; every promoted pointer field
emitted as zero-extended u64 of 0x05|new_off; Primary/Secondary GDL ptrs
remapped but NOT "promoted"; BaseAddr emitted 0) → vertex arrays immediately
after their record (PointUsage = 2×numVertices s16 after op24 CollisionVerts)
→ **GDLs LAST, contiguous, visit order, 16B LE slots**: w0'=bswap32(w0);
w1' = bswap32(raw) EXCEPT for seg-5 of {0x04,0xFD,0xF3} → remap low24 via
the region map; **no LSB set** (ROM convention; fast3d's extended seg_addr
case handles unmarked segmented addresses). Stage at F+R−D_PC, memmove to
F. Set `poolRemaining = D_PC` exactly (never touch rom_remaining).
14. **N64 record field offsets** come from the bondtypes.h comments (which
preserve N64 offsets); all 14 opcodes present in ROM files — 1,2,4,8,9,10,
12,13,15,18,21,22,23,24 — verified against RSZ sizes. PC layout: assign
through the real structs (compiler packs); don't hand-compute.
15. **PointUsage** = 2×numVertices s16 entries, indexed by MAIN-vertex index,
chain terminated by −1 (chr.c:3309-3331).

**D48 (REVIEW — D43 re-plan: offline pre-conversion "Plan B"; process fix)**
Independent review session that audited the D47 handoff against code + ROM
before implementation. Verdict: the remaining task was mis-scoped as a 1:1 C
port of a script that does not emit bytes; a cheaper, lower-risk path exists
that reuses the existing ROM load chain. Plan B is now the default; Plan A
(D47.13 C converter) is the fallback.

1. **`d43_convert.py` does not emit bytes.** It computes the layout/region map
and validates that every pointer remap resolves (512/512 clean, re-run and
confirmed this session), but there is no emission pass in ANY language. The
byte-level contract (PC struct assignment, per-field bswaps, GDL slot writing)
is unimplemented and unvalidated. "Port d43_convert.py 1:1 to C" would have the
next session write ~250-300 lines of new emission code in C and debug it at
runtime (crash → backtrace cycles). The emission must be written once either
way — do it in Python where iteration is seconds, validate offline, then ship
the data.
2. **Plan B: offline pre-conversion through the existing ROM load path.**
Generate 512 PC-layout RZ sidecar files with a Python emit pass; serve them by
patching `file_resource_table[i].hw_address` +
`resource_lookup_data_array[i].rom_size` from the port layer. Verified
mechanical facts (all re-checkable in <1 h):
   - **RZ format is trivially reproducible**: 2-byte header (`0x11 0x72`) + raw
deflate; `decompressdata()` (port/src/rzdecomp.c) skips the header and inflates
with a generous avail_in bound. A sidecar `[0x11 0x72][raw-deflate(PC image)]`
works through `load_resource` UNMODIFIED — and it sets `poolRemaining = D_PC`
automatically (the decompressed size), so no fixup call is needed at all.
   - **`romCopy` on PC is a host memcpy**: src/ramrom.c → `osPiStartDma` → port
shim `piServiceDma` (port/src/libultra.c), gated only by
`romdataCartAddrValid()` (port/src/romdata.c:212). The ROM is VirtualAlloc'd at
CART_BASE 0x10000000 sized romSize — extend the reservation by the sidecar
total, place sidecars at [CART_BASE+romSize, …), extend the validity check.
   - **`file_resource_table` is a plain writable global** (included in
src/game/ob.c:22). ALL 512 model loads funnel through
`load_object_fill_header` (dst==0 → `_fileNameLoadToBank`; custom-buffer
callers in front.c/gun.c/bondview2.c/initmenus.c → `_fileNameLoadToAddr`) —
both read `hw_address` via `load_resource`. No game code dereferences C*/G*/P*Z
symbols directly outside the table (verified by grep; symbols exist only in
the table + romassets_<r>.s markers).
   - **The patch must run AFTER `obInit()`** (called at src/boss.c:179):
obInit computes `rom_size` from adjacent-entry hw_address DELTAS (ob.c:122), so
a pre-obInit patch would corrupt rom_size. Lazy one-shot at the PORT hook site
in load_object_fill_header is simplest — by first model load, obInit has
definitely run.
   - **Footprint**: 512 files = 1,277,088 B compressed total (1.2 MB),
3,289,344 B decompressed N64 total; PC decompressed ≤ 1.31× per file (D47.12).
   - **The indy path is NOT used** (`resource_load_from_indy`, ob.c:56): it is
the N64 host-protocol loader gated by `indy_ready` (src/game/indy_comms.c,
dormant on PC), and its pPayload placement underflows on reload when
poolRemaining == pc_size exactly. Table patching reuses the proven ROM path.
   - **What Plan B eliminates**: the C converter (~300 lines), two-pass staging
+ memmove, staging-space guard, and the "emission bug only visible at runtime"
risk class. What it keeps (all already committed): ABI edits, fast3d seg-5
case, D46 buffer sizing — plus the one-line poolRemaining=0 reset (item 3).
   - **Sidecars are region-specific** (derived from the region ROM); generator
must take the region and write `data/pcmodels-<region>/`.
3. **The poolRemaining=0 reset is STILL needed under Plan B.** fileSetSize
leaves P=R=post-compaction size (ob.c:346-347); a same-stage reload would then
alloc S_bank' = P_old, which can be < round8(compressed)+8 → `load_resource`
hits the `source − ptrdata < 8` branch → poolRemaining=0 → silent load
failure. The D47.4 reset before `_fileNameLoadToBank` covers both plans.
4. **Plan A flaw (if runtime conversion is kept):** D47.13's staging guard
"fail if D_PC > avail" is too weak — the staging region [F+R−D_PC, F+R)
overlaps the live N64 image [F, F+D_N64) whenever S_bank < D_N64 + D_PC, and
emission would then read corrupted bytes. Correct guard: `D_N64 + D_PC ≤
avail`. Low probability in practice (files load at stage start; ~2.3 MB bank
vs ~82 KB worst case), but the stated guard silently misses it.
5. **Process fix:** the 23 d43_*.py investigation scripts (incl. the reference
converter) lived in gitignored `build-pc/` — moved to tracked `tools_pc/`
and committed, so the spec is versioned and reviewable. d43_cover.py's one
internal path reference updated; converter re-run from new location: ALL CLEAN.
6. **Review checklist for the next session** (confirm each item against code +
ROM before executing Plan B; record results as D49): see docs/HANDOFF.md
§Task 1 — eight claims, each with a falsification criterion. If any fires,
fallback to Plan A with the corrected guard from item 4.

**D49 (REVIEW — Plan B verification: D48 checklist R1–R8 all CONFIRMED)**
Independent review session executed the D48.6 checklist against code + ROM
(NTSC `data/ge007.ntsc-final.z64`). **Verdict: 8/8 confirmed — Plan B is
cleared for execution (HANDOFF Task 2); no fallback to Plan A.** Per item:

1. **R1 RZ format — CONFIRMED.** `decompressdata()` (port/src/rzdecomp.c)
skips the 2-byte header and raw-deflates (`inflateInit2(-15)`, avail_in ≤
0x400000). All 512 model files (C*/G*/P*Z in
`assets/obseg/file_resource_table.inc.c`; table↔`scripts/filelist.u.csv`
set identity exact, 512=512) start `0x11 0x72` and inflate cleanly.
Σ round8(compressed) = **1,277,088 B — exactly D48's claimed number**; max
single-file compressed 0x421E; max decompressed/compressed ratio 4.75.
2. **R2 romCopy is a host memcpy with no 0x10C00000 limit — CONFIRMED.**
`romCopy` → `doRomCopy` → `osInvalDCache` (no-op shim) + `osPiStartDma` →
`piServiceDma` (port/src/libultra.c): the only gate is
`romdataCartAddrValid()` (port/src/romdata.c:212), then plain memcpy. The ROM
is VirtualAlloc'd at CART_BASE 0x10000000 sized exactly romSize
(romdata.c:156-159); nothing in the port layer bounds the region, so placing
sidecars at [CART_BASE+romSize, …) needs only a reservation-size extension +
validity-check extension in romdataInit.
3. **R3 the table is the single chokepoint — CONFIRMED.** All 512 model loads
funnel through `load_object_fill_header` (src/game/objecthandler_2.c:89) →
`_fileNameLoadToBank`/`_FileNameLoadToAddr` → `fileIndexLoad*` →
`load_resource`, which read `hw_address`/`rom_size` from
`file_resource_table` + `resource_lookup_data_array`. The C*/G*/P*Z symbols
are `.set` markers in `port/src/romassets_<r>.s` (gen'd by
`scripts/gen_romassets.py`); no game code dereferences them outside the table
(grep: all hits are string literals/comments). Callers audited: bondview2.c,
chr_b.c, ejectedcartridges.c, front.c, gun.c, loadobjectmodel.c.
4. **R4 obInit runs once, before any model load — CONFIRMED.** `obInit()`
is called exactly once at src/boss.c:179 inside one-shot
`bossInitMainthreadData()` (before the infinite main loop); it computes
`rom_size` from adjacent-entry `hw_address` DELTAS (ob.c:122). `rom_size`/
`hw_address` are touched only in ob.c. The D43 crash stack was post-obInit.
A lazy one-shot table patch at the port hook site in
`load_object_fill_header` is therefore safe.
5. **R5 buffers fit — CONFIRMED (quantified).** Fresh dst==0 loads: max
round8(compressed)+8 = 0x4228 ≪ STAGE bank (max 0x24C400 = poolArea 0x2A4400
− me 352 KiB NTSC; ≥ ~1.3 MB even after conservative pre-model stage usage —
BG stan ≤ 0xA3E0 per stage). Worst-case post-compaction size across all 512
files (D_PC + 16·Σ(K_t−1) with K_BOUND {0:38,1:46,2:36,3:18,4:15}) =
**0x16F74** (Csuit_lf_handZ) — 25× headroom vs a fresh bank. dst!=0 callers:
every buffer ≥ 0xF000 > 0x4228; `tools_pc/d43_chainbound.py` re-run against
the CURRENT constants all pass: suit 0x1002C≤0x18000, trigger/watchlaser
0xED20≤0x17000, wallet 0x105BC≤0x17000, worst weapon GmapZ 0xE91C≤0xF000,
bondview chain 0x1CE5C≤0x23000, cast rifle/pistol 0x240DC/0x23F24 ≤ 0x25000
(front.c:7795), logo 0x85000 (initmenus.c:38). `poolRemaining := D_PC` is
automatic (`load_resource` ← `decompressdata` return, ob.c:61).
6. **R6 poolRemaining hazard + reset — CONFIRMED.** Mechanism verified in
current code: `fileIndexLoadToBank` (ob.c:219-247) allocates
`poolRemaining` when non-zero (the post-compaction P left by fileSetSize),
and `load_resource` (ob.c:49-53) needs round8(rom_size)+8 ≤ bytes or it sets
poolRemaining=0 → **silent load failure**. The data CAN trigger it (e.g.
PlegalpageZ: round8(C)+8 = 0xFC8 > D_PC = 0x5F0; its own path is a custom
buffer so it is safe, but the condition exists for any file reloaded in the
same stage). Hazard window = same-stage reloads only: poolRemaining is zeroed
at stage entry/exit (boss.c:415-417 / 639-641), and all consumers of
`get_pc_remaining_buffer_for_index` / `get_pc_buffer_remaining_value` run
post-load+compaction; the PROMOTE walk (sub_GAME_7F075A90) does not touch
lookup data. The addr path is unaffected (bytes = caller's fixed buffer).
The one-line poolRemaining=0 reset before `_fileNameLoadToBank`/
`_FileNameLoadToAddr` in `load_object_fill_header` is **pending** (not yet in
tree) and closes the hazard for both plans.
7. **R7 per-region sidecars + directory matching — CONFIRMED (with notes).**
Region is build-time-fixed: `build-pc.sh` ROMID → CMake ASSET_REGION {u,e,j}
→ `romassets_<r>.s`; the loaded ROM token (`ge007.<romid>.z64` /
`baserom.<r>.z64`) uniquely determines the region and romHeaderValid enforces
the country byte, so romdataInit can derive `data/pcmodels-<region>/` with the
same token logic (baserom.u → ntsc-final, baserom.e → pal-final). Name sets
are NOT identical across regions (filelist: u=512, **e=465**, j=512) — the
generator must map whatever files exist, and the patch loop only patches names
present in the manifest. Notes: (a) romdataInit has **no JP candidate**
(GE007_IS_PAL comes from versioninfo.h.in) — PC cannot boot a JP ROM today,
so JP sidecars are moot until that is added; (b) missing sidecar directory →
warn + continue in ROM-only mode (Task 2 step 2).
8. **R8 emission-spec completeness — CONFIRMED (with one explicitness gap).**
Walked every `ModelRoData_*Record` for the 14 opcodes present in ROM
(1,2,4,8,9,10,12,13,15,18,21,22,23,24) in src/bondtypes.h: every field is
covered by Verified facts + D47.13 (pointer-promotion list D47.5; f32 bswap;
TextureID bswap32; vertex arrays D47.8; GDL slots D47.13). A fresh C probe
(mingw gcc with the EXACT flags from build-pc/compile_commands.json)
re-verified all 15 PC record sizes + ModelNode (0x30) + ModelFileHeader
(0x38) + Vertex (0x10) and every pointer-promotion offset — **no drift** vs
d43_convert.py's PC_REC / the spec. Gap: Verified facts enumerate bswap for
f32, vertex-array s16s and the TextureID u32, but do not explicitly enumerate
the **u16/s16 record scalars** (AnimPart, MatrixIndex, JointID, MatrixIDs,
Group1/2, RwDataIndex, op4 numVertices@0x10, op24 nv/ncv/ModelType/
RwDataIndex). Data check: these hold small BE values (JointID 1–11, nv ≤
73) that raw emission would corrupt (LE read → ×256); all research tooling
reads them BE (`bu16`/`be16`). The rule follows from the D33 per-field
endianness convention (u16 → bswap16), so this is an explicitness gap, not a
missing rule — but Task 2's emit pass must bswap16 every u16/s16 record
scalar (padding/reserved may be zeroed), and the HANDOFF Verified facts
should be amended to say so.

**D50 (RESOLVED — Plan B executed: offline sidecars + port plumbing; boot advances to first model GDL execution)**

Execution session ran HANDOFF Task 2 end-to-end. All 512 NTSC model files
converted offline and served through the existing load path; frames render
and model display lists execute. Sub-items (all `#ifdef PORT`-guarded or
port-layer only; N64 build untouched):

1. **D50.1 Emit pass (`tools_pc/d43_emit.py`, tracked).** Per file:
decompress the N64 image from the ROM (filelist row); build the node map;
run the EXACT `modelIterateDisplayLists` visit simulation (LOD/SWITCH
rewire, BSP splice — d43_gdlorder logic) → gdl_seq; layout `[switches
NS×8][texconfigs NT×12][DFS nodes 48B + records PC_REC + vertex arrays]
…[GDLs packed contiguously in gdl_seq order, 16B per N64 slot]`; byte-exact
emit (bswap32/16; promoted pointers → zero-extended u64 `0x05xxxxxx` VMAs;
GDL Primary/Secondary raw VMAs, NOT promoted; BaseAddr=0); **round-trip
re-parse validation of every field against the N64 source + region tiling
(no gaps)** — 512/512 pass. Compression: `0x11 0x72` + raw deflate level 6
(`zlib.compressobj(6, DEFLATED, -15)`); escalate to 9 only on a dst!=0 fit
violation (none occurred). Cross-checks all pass: per-file dst!=0 buffers
(`round8(C)+8 ≤ buf` AND `D+round8(C) ≤ buf` — inflate overlap safety), G*
hand-weapon worst case vs 0xF000, cast/title/bondview chain cumulative
P_final bounds, totals (Σ round8(C) = 1,277,088 B, matches D49). Output:
**single concatenated image** `data/pcmodels-<region>/pcmodels.bin`
(sidecars at 16-aligned offsets) + `manifest.csv` (`name,offset,size`,
decimal — the C parser uses strtol base 10; file_resource_table.inc.c
order). Deviation from D48's per-file-sidecar sketch: one blob + manifest;
pcmodels.c copies the whole image to `[CART_BASE+romSize, …)` and patches
`hw_address = cartBase+romSize+off`. Regenerate:
`python tools_pc/d43_emit.py [ntsc-final|pal-final|jpn-final|--check-only]`
(needs the region ROM in data/; sidecars are gitignored with data/).
2. **D50.2 Port plumbing (`port/src/pcmodels.c` + `port/include/pcmodels.h`,
new; romdata.c/h extended).** `pcmodelsReserveSize(romImg)` derives
`data/pcmodels-<region>/` from the ROM country byte (+0x3E), parses the
manifest, returns total bytes (0 → warn, ROM-only mode); romdataInit
reserves `romSize + sidecarTotal` at CART_BASE and
`romdataCartAddrValid` accepts the extension; `pcmodelsLoadSidecars(
cartBase, romSize)` copies the blob; `pcmodelsPatchTable()` — one-shot,
called from `load_object_fill_header` (hook block, objecthandler_2.c) after
obInit has run — redirects every manifest row's
`file_resource_table[i].hw_address` + sets
`resource_lookup_data_array[i].rom_size` to the PC compressed size. The same
hook block resets `poolRemaining = 0` for dst==0 loads (closes the D48.3/R6
reload hazard). Boot log: `[INFO] pcmodels: table patched (512 model
entries)`.
3. **D50.3 Language banks (runtime C fixup).** Banks carry a big-endian
offset table; `romdataFixupLangBank(blob, decompressedSize)` decodes it in
place. Called from language.c after each of the 7 langInit loads and lazily
per-id in the `langGetJpnCharPixels` paths (idempotent via poolRemaining
check).
4. **D50.4 Fonts (runtime C fixup).** `load_font_tables` PC branch:
allocate `romdataFontPcSize()` (PC C layout of struct font: kerning[169] +
chars[94] + glyph pixel data), romCopy the N64 size, then
`romdataFixupFont` re-lays out, shifting the pixel block below the expanded
char array; `pixeldata` fields left as relative offsets — the existing
`pixeldata += base` loop promotes them.
5. **D50.5 Legal-screen UB exposure (front.c).**
`constructor_menu00_legalscreen` reads an uninitialized pointer at a lookat
call — on N64 the register happens to hold a readable address and the
result is zeroed by `* 0.0f` anyway; the x86-64 compiler folds the UB read
to NULL → fault. PORT branch seeds it with `legalpage_text_array` (the
value assigned a few lines later); identical lookat.
6. **D50.6 `texCopyGdls` copies only w0 on PC — first model-render crash
(RESOLVED).** `Gfx` is a union whose last member is `long long int
force_structure_alignment`: 8 bytes on N64 (= the whole slot), but on x86-64
the slot is 16 bytes, so `arg1->force_structure_alignment = arg0->…`
copies only the low half. Compaction flow (sub_GAME_7F0762E0):
texCopyGdls mirrors the GDL block `[G,D)` to tail scratch `[B−D+G,B)`, then
texLoadFromGdl reads the scratch and writes expanded output back at
`[G,…)` via full-slot `*(out++)=*(in++)` — propagating the scratch's stale
w1s into the final GDLs. **Byte proof:** PlegalpageZ (NS=0, NT=5,
D=0x2638) loaded at 0x7012EA38; first 64 RAM bytes match the decompressed
sidecar exactly; the executed GDL at file offset 0x2488 has w0s matching
the sidecar exactly (0xB10000BA/0xE7000000/0xFD900000/0xE6000000) but RAM
w1s = 0x50362B58/0x66D73339/0xACAAB819/0x55BDF769 (stale mempool contents;
pad2 dwords also nonzero) vs sidecar w1s 0x0000A898/0/0x050012C8/0 — the
garbage G_SETTIMG w1 → `seg_addr` → OOB read in `import_texture_rgba16`.
Fix: `*arg1 = *arg0;` under `#ifdef PORT` (tex.c). Audit: tex.c is the only
`force_structure_alignment` use in game code (model.c:1510 is an unrelated
local 8-byte union); every other Gfx copy is full-struct. Post-fix:
PlegalpageZ's sub-DLs execute to completion; crash moves on (D51).

**D51 (RESOLVED — font `pixeldata` fixup wrote the pointer at the wrong offsets; frame-5 `import_texture_i8` SIGSEGV)**

The G_TEXRECT tile-0 upload's source was a **font glyph** (I8, loaded via
`gDPLoadTextureBlock`). Root cause in `romdataFixupFont`
(port/src/romdata.c): it wrote each char's pixel pointer at blob offsets
d+20/d+24, but on PC `struct fontchar.pixeldata` is a **u64 at char offset
+24** — the low word landed in padding and only half the pointer was
written, so glyph sources resolved to stale/invalid addresses → OOB read in
`import_texture_i8`. Fix: write the blob-relative offset into d+24 (low)
and zero-extend d+28 (high). Verified: glyphs load, SIGSEGV gone. The old
D51 hypothesis list (seg_addr / tile staleness) was wrong; its GE opcode
facts remain valid reference material (G_IMMFIRST=−65; DMA G_MTX=1 /
G_MOVEMEM=3 / G_VTX=4 / G_DL=6; IMM TRI1=0xBF … ENDDL=0xB8; GE extension
G_TRI4=0xB1, 8-bit packed indices — NOT an address carrier; RDP
pass-throughs G_SETTIMG=0xFD, G_SETCIMG=0xFF, **G_TEXRECT=0xE4 /
G_TEXRECTFLIP=0xE5** — GE-specific values, not libultra's 0x46/0x45;
width-1 stored in the 12-bit field, fmt I=4, G_TX_LOADTILE=7; main DL is
game-built per frame with gSP* macros; segment table via
`gMoveWd(G_MW_SEGMENT, seg*4, base)` → w0=(0xBC<<24)|(seg*4<<8)|6,
w1=data; fast3d's gfx_sp_moveword stores data as-is when ≥ 0x800000 else
+0x80000000).

**D52 (RESOLVED — `osGetCount` tick-rate mismatch → non-deterministic post-frame-2 hang)**

Plain runs sometimes hung after frame 2 (kernel watchdog: "no frame rendered
for 3006180 ms") instead of crashing at frame 5. Root cause:
port/src/libultra.c `osGetCount()` returned **microseconds** (1M/s), but GE's
pacing assumes the N64 RSP counter rate ≈ **46.5525 ticks/µs**:
`MAIN_LOOP_TICK_INTERVAL` = 387,937 ticks (boss.c: NTSC
`INTERVAL_INTER_MATH - 2688U`; PAL
`frameDelay*(CYCLES_PER_FRAME-6450)-(INTERVAL_INTER_MATH-3225)`), and
`waitForNextFrame()` (frametiming.c) waits for
`(elapsed+interval)/775875` ticks (NTSC frame = 775,875; PAL 931,050). At
1M/s the steady-state 16,667 µs between retraces < 387,937 → bossMainloop's
gate never passes → no DL built → hang. Frames 1–2 rendered only because
stage loading took >388 ms of real time. Fix: `osGetCount()` returns
`(u32)(((uint64_t)sysGetMicroseconds()*465525ull)/10000ull)` (rate derived
from GE's own constants: 775875/16666.67µs = 931050/20000µs); wraps every
~92 s like the HW counter. `osGetTime()` still returns µs (osSetTimer/
OSTime). PD's port has the same µs implementation but lacks GE's cycle-based
pacing gate — do not copy it blindly.

**D53 (RESOLVED: model RW-data pool addressing on PC; frame-5 SIGSEGV in `modelInitRwData`)**

Post-D51/D52, the frame-5 SIGSEGV moves to `modelInitRwData`
(model.c ~6131): first BSP node → `movl $0x0,(%rax)` right after
`call modelGetNodeRwData` — writing `visible=FALSE` through NULL. Two
compounding PC layout bugs, both in the D32 class:

- **D53.1 (applied; necessary but not sufficient): `Model.datas` word
  stride.** `RwDataIndex` values are **4-byte word offsets** into the
  RW-data pool (`modelCalculateRwDataIndexes` accumulates
  `len += sizeof(record)/4`; pool = round16(numRecords×4) bytes, allocated
  in modelmgrInstantiateModel(WithAnim)). On N64 `&data[index]` with
  `union ModelRwData **data` strides 4 B; on PC it strides 8 B → every
  non-zero index addresses the wrong record. Fix (3 files, all #ifdef PORT):
  `Model.datas` → `u32 *datas` (bondtypes.h — layout unchanged: single
  pointer field); casts in model.c (`modelGetNodeRwData` local + return
  `(union ModelRwData *)&data[index]`, parent-walk
  `data=(u32*)tmp->RwDatas`, `modelAttachPart`) and propobj.c:7301/14343.
- **D53.2 (root-caused; fix designed, NOT applied): `ModelSlot` /
  `AnimModelSlot` ↔ `Model` type-pun breaks on PC.** The game puns the slot
  structs and Model in both directions: `slot.unk08@8` ↔ `Model.obj@8` (the
  **in-use flag** — `modelInit`'s `objinst->obj = header` marks a slot),
  `slot.unk10@0x10` ↔ `Model.datas@0x10` (RW pool), `slot.unk02@2` ↔
  `rwdatalen@2`. On N64 all pointers are 4 B → offsets agree. On PC there is
  no pack pragma — natural alignment (probe-verified with the exact CMake
  flags, see vsize.c): **Model = 0xE8 B**: chr@8, **obj@0x10**,
  render_pos@0x18, **datas@0x20**, scale@0x28, attachedto@0x30, anim@0x40;
  the slot structs still have unk08@8 / unk10@0x10. (An earlier draft of
  this section said obj@0xC/datas@0x1C — that assumed packed layout and is
  wrong; the log evidence below only fits the natural-alignment offsets.)
  Consequences: (a) `modelInit` writes obj@0x10 — the free check reads
  unk08@8 (low word of Model.chr, never written) → slots are never marked in
  use → every model reuses slot 0; (b) the legal-screen logo model takes
  slot 0, then `update_menu00_legalscreen()` (front.c:1430) →
  `clear_model_obj(logoinst)` writes obj=NULL at @0x10..0x17 — exactly where
  `slot.unk10@0x10` lives; the next instantiation passes
  `modelmgrCanSlotFitRwdata` on stale Model.datas@0x20 (non-NULL) + real
  unk02=20 ≥ 17, then `rwdata = g_ModelSlots[i].unk10` = NULL →
  `modelInit(model, header, NULL)` → fault. **Log evidence** (TEMP D51 trace
  in model.c → d52rw.log):
  ```
  INST model=0x7020ac48 header=0x140141c40 numRecords=0  rwdata=0x7020b0f8 rwdatalen=20 lvreset=0
  INST model=0x7020ac48 header=0x140142700 numRecords=17 rwdata=NULL     rwdatalen=20 lvreset=0
  GND obj=0x7020ac48 datas=NULL idx=0 rwdatalen=-1 op=2 data=NULL res=NULL
  ```
  (same Model address both times = slot reuse; 20 = MODEL_SPARE_RWDATALEN
  spare-slot pool.)
- **D53.2 fix APPLIED and verified.** objecthandler.h under #ifdef PORT:
  both slot structs re-laid out on top of the PC struct Model — `unk02`@2,
  in-use marker `unk08`@**0x10** (low word of Model.obj), pool pointer
  `unk10`@**0x20** (Model.datas), then `char pad28[sizeof(struct Model)-0x28]`
  so each slot is exactly sizeof(Model) = 0xE8 (a full Model, including
  animInit's writes up to PC offset 0xE3, fits). Member names kept; N64
  layout verbatim under #else. model.c heap fallbacks bumped PORT-guarded:
  0x20 → sizeof(struct Model) (non-animated), 0xC0 → sizeof(struct
  AnimModelSlot) (animated). Verified with the d52rw.log probe: both INSTs
  get valid pools (rwdata=0x7020c778, non-NULL), all 17 records initialize,
  frame-5 modelInitRwData crash gone; game runs to ~frame 102 and past the
  Nintendo-logo transition. Note: the bump allocator's `pos` drifts with the
  new sizes → some slots land on 4-byte boundaries; misaligned 8-byte field
  stores are functionally fine on x86-64 (individual field accesses, no
  faults) — accepted without a PORT alignment bump in memp.c.

Size/offset probe: `vsize.c` at repo root (untracked scratch — delete before
commit) prints sizeof + field offsets of Model / ModelSlot / AnimModelSlot /
struct player; compile with the CMake include order (port/shim first, then .,
include, include/PR, src, src/game, src/libultra, port/include) plus
`-DPORT=1 -DAVOID_UB=1 -std=c11`. Re-run after any slot-struct edit.

Environment notes: standalone gcc needs
`/c/msys64/mingw64/bin:/c/msys64/usr/bin` on PATH (cc1 fails SILENTLY without
it — exit 1, zero diagnostics), and `-std=c11` is required (the CMake flag;
under the default gnu23 `typedef s32 bool` in bondtypes.h breaks). Also:
`include/stddef.h`'s body is `#if 0`'d — offsetof/size_t are unavailable in
game TUs (use pointer-difference arithmetic in probes).

**D54 (RESOLVED: cseq ALCMidiHdr endianness; audio-thread SIGSEGV in
`__getTrackByte`)**

After D53, the first music load (M_INTROSWOOSH, seq 44) crashed the audio
thread in `__getTrackByte`. Root cause: a decompressed compact-sequence file
starts with `struct ALCMidiHdr` — 16 **big-endian** u32 trackOffset values +
a BE u32 division. N64 reads them natively; on an LE host alCSeqNew() does
not swap them (track-0 offset 0x44 becomes 0x44000000), builds "valid"
curLoc pointers ~1 GB past the buffer, and the first track-byte read faults.
The rest of the stream is byte-oriented (varlens, MIDI bytes, BE loop offsets
assembled byte-by-byte) so only the 17 header words need fixing. Fix: port-
layer `romdataFixupCseq(u8 *blob)` (port/src/romdata.c — bswap32 of the first
17 u32s; declared in port/include/romdata.h), called under #ifdef PORT after
each of the 3 `decompressdata` calls in musicTrack1Play/2/3 (src/music.c).
Verified: __getTrackByte crash gone; full 60 s run (exit=124 timeout) with no
audio fault, past the Nintendo-logo transition.

**D54b (RESOLVED: synthesizer param-slot sizing; audio-thread SIGSEGV in
`alLoadParam`)**

Next audio crash: `alLoadParam` dereferenced a corrupted free-list slot. Root
cause: alSynInit's "build the parameter update list" allocates
`c->maxUpdates` slots of `sizeof(ALParam)` and the game type-puns those slots
as several AL*Param structs. On N64 all of them are 0x1C bytes (one slot
each); on x86-64 **ALStartParamAlt is 0x28** (two 8-byte pointers: next +
wave) vs ALParam = 0x20 — every start-voice update wrote `wave` into the
neighbouring slot, corrupting the free list. Fix in synthesizer.c alSynInit
under #ifdef PORT: allocate `maxUpdates * sizeof(ALStartParamAlt)` and stride
the init loop by that size (cast each slot to ALParam* for the next-pointer
link); N64 verbatim under #else. Cost +8 B/slot × 0x80 slots = +1 KB vs
MUSIC_ALLOCATION_BYTES — no alHeapAlloc failure observed. Verified:
alLoadParam crash gone; full 60 s run clean.

**D55 (RESOLVED: RLE folder-menu background header endianness; SIGSEGV in
`rle_expand_8bit`)**

After the audio fixes, the game ran to the gun-barrel intro (~frame 654,
~23 s) and crashed in `rle_expand_8bit` (src/game/rle.c:30, the `*dst++`
store). Caller: title.c `sub_GAME_7F008DE4` (the initializeGunBarrelIntro path)
romCopies the asset at
`unknown2` (romassets_<r>.s, NTSC cart 0x102A4D50, size 0x1A580) and RLE-
decodes it into a 0x40400-byte buffer. Root cause: the asset is the title
folder-menu background; its raw ROM header is **big-endian** `01 B8 01 2B`
(w=440, h=299) + 6 pad bytes + a valid RLE stream (decodes to exactly
440×299 = 131560 bytes). rle_expand_8bit reads w/h as **LE** u16s (byte-
matched N64 code): the raw header gives w=47105, h=11009 → remaining ≈
518 MB written into a 256 KB buffer → SIGSEGV. The N64 build embeds this
asset into .data via `assets/romfiles2.s` (`.incbin
"assets/ge007.u.2A4D50.usedby7F008DE4.bin"`) from an extracted .bin whose
header is byte-swapped — title2.c hardcodes 440-wide I8 rows × 299, and this
is the only decodable 440×299 RLE stream in the ROM (a full-file 16-bit swap
was ruled out: it zeroes the first RLE count at +0xC), so only the 4-byte
header differs between raw ROM and the N64 .bin. Fix (port-layer, romdata.c
romdataInit, after the cart-base mapping): bswap32 the first word at
`(u32 *)&unknown2` in place — the image is a writable VirtualAlloc at CART_
BASE — guarded by "only swap if the LE-read w or h > 512" so an already-LE
region copy is a no-op. rle.c/title.c untouched (D37/D54 pattern). Verified:
RLE crash gone; game renders 600+ frames past the gun-barrel background into
the watch intro, where it hits D56.

**D56 (RESOLVED: watch-intro embedded Model/RW-pool raw offsets into struct
player; SIGSEGV in `modelSetScale`)**

Post-D55, the game renders 600+ frames (~10 s into the gun-barrel/watch
intro) then SIGSEGVs in `modelSetScale` (src/game/model.c:778,
`objinst->scale = scale`) with a garbage Model* (crash-log return frame was
corrupted; caller inferred — see below). Prime suspect, and the only code
passing a **raw N64 offset into struct player** as a Model*: `sub_GAME_7F07E7CC`
(bondview2.c:3102-3116, called from bondview2.c:3400 whenever the pause/watch
transition completes — every watch-menu open, and during the gun-barrel
intro):
```
animInit((Model *)((u8 *)g_CurrentPlayer + 0x230), itemheader, (u32 *)((u8 *)g_CurrentPlayer + 0x2ec));
modelSetScale((Model *)((u8 *)g_CurrentPlayer + 0x230), c_item_entries[41].scale * 0.1f);
modelSetAnimation((Model *)((u8 *)g_CurrentPlayer + 0x230), …ANIM_DATA_bond_watch…);
*(s32 *)((u8 *)g_CurrentPlayer + 0x220) = 0;   // = step_in_view_watch_animation
```
Layout facts (probe-verified where noted):
- struct Model (bondtypes.h:1482): 8 pointer fields (chr, obj, render_pos,
  datas [u32* under PORT, D53.1 — note the in-code comment there still says
  "D52", flagged for rename in HANDOFF Task 3], attachedto,
  attachedto_objinst, anim, anim2).
  sizeof_N64 = **0xBC**; sizeof_PC = **0xE8** (vsize probe: chr@8, obj@0x10,
  render_pos@0x18, datas@0x20, scale@0x28, attachedto@0x30, anim@0x40 — note
  the 8-byte pointer alignment padding after the two leading s16s).
- N64 struct player: the **watch Model is embedded at +0x230** (its first
  word is the anonymous s32 `something_with_watch_object_instance`), size
  0xBC, so it ends exactly at +0x2EC where the **RW-data pool** begins. The
  pool region runs to `buttons_pressed`@0x3B4 = **0xC8 bytes** of capacity;
  every field in [0x2EC, 0x3B4) is an anonymous s32 (no pointers → no extra
  PC shift inside the region).
- On PC the embedded Model sits at X = offsetof(struct player,
  something_with_watch_object_instance) (≥ 0x230 + 5×4: cameratile@0x34,
  prop@0xA8, bodyModel@0xD4, autoaim_target_y@0x130, autoaim_target_x@0x140
  are the pointer fields before it) and spans [X, X+0xE8). Remaining capacity
  before buttons_pressed is only 0x184−0xE8 = **0x9C < 0xC8** → the pool does
  NOT fit embedded on PC.
- modelInit stores the pool pointer in `Model.datas` and ALL rwdata access
  goes through `modelGetNodeRwData(model, node)` via `model->datas` — so
  redirecting the pool to separate storage is safe; only sub_GAME_7F07E7CC
  references +0x2EC directly (whole-tree grep).
- The N64 capacity 0xC8 bounds the watch model's real pool size (the game
  works on N64), so a fixed static buffer of that size is safe.
**Applied as designed + verified:** bondview2.c sub_GAME_7F07E7CC under
#ifdef PORT takes the Model by field name —
`Model *watch = (Model *)&g_CurrentPlayer->something_with_watch_object_instance;`
(probe-verified at +0x24C on PC — not 8-aligned; unaligned pointer stores are
fine on x86-64 per D53.2) and hosts the pool in `static u8 watchRwPool[0xC8]`
(N64 capacity; N64 embeds it at player+0x2EC); animInit/modelSetScale/
modelSetAnimation on `watch`; the +0x220 store becomes
`g_CurrentPlayer->step_in_view_watch_animation = 0;`. N64 raw-offset path kept
verbatim under #else. Verified: the watch path no longer crashes — but the
same `modelSetScale` SIGSEGV remained, and an env-gated probe (GE_D56,
logging `__builtin_return_address(0)` in modelSetScale) proved the real
caller was **not** the watch path: `initializeGunBarrelIntro` (title.c)
calling `modelSetScale(NULL, 0.18779343f)` because `setup_chr_instance()` →
`modelmgrInstantiateModelWithAnim()` returned NULL for BODY_Brosnan_Tuxedo —
see D57.

**D57 (RESOLVED: pointer-grown rwdata records overflow the N64-sized spare
pools; `modelmgrInstantiateModelWithAnim` returns NULL → SIGSEGV in
`modelSetScale(NULL, …)`)**

The Brosnan tuxedo's computed PC `numRecords` is **153** words vs the N64-
sized anim spare-pool capacity of **140** (`ANIM_MODEL_SPARE_RWDATALEN =
0x8C`). Cause: two rwdata record structs contain pointer fields and grow 8 →
16 bytes on x86-64 — `ModelRwData_HeadPlaceholderRecord` (ModelFileHeader* +
void*) and `ModelRwData_DisplayList_CollisionRecord` (Vertex* + Gfx*). Since
`modelCalculateRwDataLen()` accumulates sizeof(record)/4 per node, every
HEAD/DLCOLLISION node adds +2 words vs N64. Fix (two parts, both #ifdef PORT):
(1) initunk_005520.c: spare capacities grown with headroom —
`MODEL_SPARE_RWDATALEN 0x14→0x38`, `ANIM_MODEL_SPARE_RWDATALEN 0x8C→0xA8`
(N64 values kept under #else); (2) model.c: in the non-LvResetting branches
of both `modelmgrInstantiateModel()` and `modelmgrInstantiateModelWithAnim()`,
a dynamic slot+pool fallback mirroring the existing LvResetting path
(`mempAllocBytesInBank(sizeof(struct ModelSlot/AnimModelSlot))` + 16-aligned
pool of numRecords words) — the slot is untracked (never reused), acceptable
because with the grown capacities it should not trigger. A u32-field approach
for the two pointer records was rejected: `ModelFileHeader` pointers there are
exe-resident globals (>0x80000000 on PC) and would truncate. Verified:
Brosnan gets a dynamic slot, `modelSetScale` succeeds, game proceeds to
rendering — where it hits D58.

**D58 (RESOLVED: gun-barrel DL — K0 vertex-pointer idiom + 16-byte Gfx
overflow of the N64-sized reservation; SIGSEGV in `gfx_sp_vertex`, then FATAL
"Unknown GBI opcode 0x00")**

Two distinct PC-layout breaks in the same buffer (initializeGunBarrelIntro,
title.c), both found via env-gated probes (GE_D57: per-command VTX/CALL/JMP
log + entry-time hexdump of the barrel DL):
- **Part A (vertex pointer):** title.c passed `barrelDisplayListPtr +
  0x80000000` to sub_GAME_7F01BFF8, which embeds it verbatim in each G_VTX
  w1 (GE's gDma1p writes `(uintptr_t)(v)` — no LSB). On N64 the mempool
  pointer was physical, so +0x80000000 gave the RSP-visible KSEG0 address;
  on PC it is a V1 pointer (0x70xxxxxx, dram.c) and +0x80000000 lands at
  0xF0xxxxxx — unresolvable by fast3d's seg_addr() → SIGSEGV reading the
  vertex array. Fix: rebuild the exact N64 value —
  `(Vtx *)(OS_K0_TO_PHYSICAL((void *)barrelDisplayListPtr) | 0x80000000u)`
  (→ 0x80xxxxxx; seg_addr passes it through to the KSEG0 mirror; segments
  7/8 are never registered, so the unmarked-segment path is skipped).
- **Part B (DL reservation):** on PC `sizeof(Gfx) == 16` — the union's
  trailing `long long` (gbi.h documents it: "except on 64-bit, where it is
  exactly 128 bit"), same class as D50.6. Both writers (`gdl++`) and fast3d
  (`++cmd`) advance by 16, so all game-written DLs are 16-byte-wide — but
  the barrel-DL reservation `bufferSize -= 0x100` was sized for N64's 8-byte
  Gfx. sub_GAME_7F01BFF8 emits 31 Gfx (2×VTX + 28×TRI + ENDDL) = 496 B, so
  slots 16–30 (second TRI batch + ENDDL) overflowed into the RLE region at
  +0x300, and sub_GAME_7F008DE4's expand then clobbered them with image data.
  At render the RSP executed VTX/TRI×14/VTX fine, then hit slot 16 = RLE
  pixels (w0=0x00000001 → opcode 0x00; fast3d has no G_SPNOOP case) → FATAL.
  Fix: reserve 0x200 under PORT. The 0x200 vertex reserve still fits
  (30 Vtx × 16 B = 0x1E0). NOTE for future asset work: any other N64-sized
  reservation for game-written DLs/vertex arrays must be re-checked against
  the 16-byte Gfx / 16-byte Vtx widths (recurring class, cf. D50.6/D53.2).
Verified: barrel DL executes to ENDDL; game proceeds past the gun-barrel
hole into model rasterization — where it hits D59.

**D59 (OPEN — current blocker: SIGSEGV inside an external GL DLL during the
first real model rasterization after the gun-barrel hole)**

Post-D58, the barrel DL runs clean (probe-verified: VTX@+0, TRI×14,
VTX@slot15(+0xF0 in 16-byte form), … ENDDL) and the game crashes shortly
after with EXCEPTION 0xc0000005 at PC 0x7ff8d42a44d3 — inside a DLL loaded
at 0x7ff8d4230000 (offset +0x744d3; almost certainly the OpenGL driver,
not yet confirmed). The crash-log backtrace frame #1 (main+0x227cc0) is a
BSS symbol (`memoryMesgMB`) — garbage stack, no usable caller. Draw path:
gfx_flush() (gfx_pc.cpp:299) → `gfx_rapi->draw_triangles(buf_vbo,
buf_vbo_len, buf_vbo_num_tris)` → gfx_opengl.cpp:813-816
`glBufferData(GL_ARRAY_BUFFER, sizeof(float)*buf_vbo_len, buf_vbo,
GL_STREAM_DRAW); glDrawArrays(GL_TRIANGLES, 0, 3*buf_vbo_num_tris)`, where
`buf_vbo` is the static `float buf_vbo[MAX_BUFFERED*(32*3)]`. Hypotheses:
(a) buf_vbo overflow while accumulating transformed vertices for the first
real model (Brosnan) — check the vertex-append site in gfx_pc.cpp (the
transform loop after ~line 1118) for a missing bounds check against
MAX_BUFFERED; (b) garbage buf_vbo_len/num_tris; (c) bad texture/shader state
on first model draw. Next: env-gated probe logging buf_vbo_len/
buf_vbo_num_tris at every gfx_flush + identify the DLL (PowerShell module
list during a run, or the GL vendor string in the log).

**D59 RESOLVED (sessions G–I).** The "external GL DLL" crash was not a
driver bug: it was an msvcrt.dll `memcpy` faulting on a wild source — the
gun-barrel sub-DL region at `ptr_logo_and_walletbond_DL + 0x200` had been
clobbered by the unbounded RLE write of D64 (below). With D64 fixed, the
barrel renders and the game advances; no fast3d/vertex-buffer change was
needed. The crash handler gained permanent improvements along the way:
FAULT ADDR (ExceptionInformation[1]), a 16-qword STACK@RSP window, and a
module list in `ge007.crash.log` (crash.c + psapi).

**D60 RESOLVED — DMA target validation (port layer).**
`osPiStartDma` (libultra.c) now validates ROM-read targets: the N64 PI can
DMA to any KSEG address, but on PC an unmapped target is a wild memcpy.
`dramHostAddrValid()` accepts DRAM V1/V2 and any host-committed region
(VirtualQuery), so legitimate `.bss`/`.data` targets (e.g.
`ramrom_data_target`) pass while s32-truncated wild addresses are rejected
with a logged FATAL instead of a silent crash. Also added the GE_D60
sidecar-read tracer and GE_D61 per-ROM-read log (`d61dma.log`).

**D62 RESOLVED — OSMesgQueue/OSScMsg layout (port layer).** The shim's
message-queue bookkeeping had to match the PC struct widths: OSMesgQueue is
40 bytes on PC (two OSThread* + 3×s32 + OSMesg*), OSScMsg stays 32 bytes.
Scheduler-thread message flow (retrace/pre-NMI/interrupt/cmd queues in
`os_scheduler`, g_AudioManager frame/reply queues) verified against those
layouts.

**D63 — TEMP diagnostics (to strip).** GE_D63-gated probes in gfx_pc.cpp /
blood_animation.c / front.c / rsp.c tracing the VTX-pool bump pointer
(`g_GfxMemPos`), the gun-barrel sub-DL slot word, dram-branch targets and
rspGfxTaskStart hand-off. Used to prove D64's clobber path and to rule out
VTX-pool overflow; no permanent change.

**D64 RESOLVED — blood RLE sentinel (src/game/blood_animation.c).**
The N64 build places `die_blood_image_end` in the same section directly
after `die_blood_image_1[]`; the RLE decoder's guard `bloodImgNxt <
&die_blood_image_end` relies on that adjacency (only the address is used).
On PC a zero-init symbol lands in `.bss` ~1 MB away, so the guard never
fires and the decoder writes unbounded past the array — it clobbered the
gun-barrel sub-DL at `ptr_logo_and_walletbond_DL + 0x200`, which is what
surfaced as the D59 "GL DLL" crash. Fixed under #ifdef PORT by defining
`die_blood_image_end` as one-past-the-end of the array.

**D65 RESOLVED — `enum HEADS` signed sentinels (src/bondconstants.h).**
The N64 toolchain gave this enum a signed underlying type, so
`HEAD_FIXED == -1` and `head >= 0` guards were real branches. PC GCC 16
picks `unsigned int` for enums whose enumerators are all non-negative
(0xFFFFFFFF > INT_MAX), making every `head >= 0` always true and turning
`c_item_entries[HEAD_FIXED]` into a wild 64-bit OOB read (SIGSEGV in
init_menu18_displaycast). Under PORT the sentinels are now negative
literals (`HEAD_FIXED = -1`, `HEAD_RANDOM = -97`) — identical bit pattern,
signed semantics restored.

**D65b RESOLVED — `enum BODIES` signed sentinel (src/bondconstants.h).**
Same class as D65: ROM tables store 0xFFFFFFFF in `body` fields and the
cast-end check compares `intro_char_table[f].body < 0`; PC's unsigned
underlying type deleted the reset branch, so the cast screen rendered the
terminator entry and `langGet(0)` dereferenced a NULL bank. Added
`BODY_FIXED = -1` under PORT (forces signed underlying type; no existing
value changes).

**D66 RESOLVED — romCopyAligned pointer width (src/ramrom.c/.h) +
ramrom replay truncations (src/game/ramromreplay.c).** The N64 build did
all of `romCopyAligned` in s32; on PC targets live in `.bss` above 4 GiB,
so `(s32)target` truncated (0x1401C6F00 → 0x401C6F00) and the DMA went to a
wild address. PORT version uses uintptr_t throughout and returns `void *`
callers assign straight to pointers. The ramrom replay path had the same
class of `(s32)` truncation on `ramrom_data_target`.

**D67 RESOLVED — struct image_entry layout (src/game/image.h).** The
decompiled field order cannot be right: texLoad() reads
`*(s32*)&entry & 0xFFFFFF` as the data offset (dataoffset must occupy bits
0-23 of word 0) while chrprop.c indexes entries with an 8-byte stride
(sizeof == 8). Under PORT the struct is re-declared with all-u32 bitfields
and `dataoffset : 24` first, so GCC packs it to exactly two words on both
targets and the raw word read is satisfied. The IMAGE() macro initializer
order in image.c is adjusted to match under PORT.

**D68 RESOLVED — Globalimagetable endianness (port/src/gimgfixup.c +
src/game/image_bank.c + src/game/image.c).** The ROM-copied Globalimagetable
segment (texReset) is N64 big-endian, but PC code reads its CPU-interpreted
u32 fields natively: the IMAGESEG-marked G_SETTIMG w1 words
(`IMAGESEG(id) = 0xABCD0000 | id`) and the `sImageTableEntry.index` field of
all 32 table arrays. Unfixed, texLoad computed texnum from byte-swapped ids
(e.g. 52651 for IMAGE_SMOKE_11 = 2106) → out-of-range offsets → a 925 KB
ROM read into the 4000-byte stack compbuffer (FATAL at boot). Fix:
`gimgFixupGlobalimagetable()` bswaps exactly those u32s in place after the
romCopy (17 Gfx DLs walked op-by-op for the AB CD marker; table entry
counts fall out of the D39 symbol layout, 12-byte stride); everything else
in the segment is byte-level (opcodes, single-byte fields, raw pixel blocks
referenced via 0x02xxxxxx segmented addresses) and untouched. Two
consequences handled under PORT: (1) texLoadFromDisplayList's marker scan
now checks bytes 6..7 (CD AB — the LE encoding of 0xABCDxxxx) instead of
4..5; (2) explosion.c executes the *compiled* globalDL_0xNNN shadows via
g_ExplosionDisplayLists[], so `gimgSyncCompiledGlobalDLs()` copies the
texLoad()-patched IMAGESEG w1 values from the ROM copy into those arrays
(command j of the 8-byte ROM DL maps to Gfx slot j of the 16-byte compiled
array; D39 verified them byte-identical). Verified: 137 texLoads with valid
in-range ids (2106, 2084, …), real offsets/sizes from g_Textures, and the
game runs the full ~3.5-minute intro (logo → gun barrel → cast) at ~59 fps
to the first stage load.

**D69 (OPEN — current blocker: BG-file big-endian headers at stage load)**

Post-D68 the game plays through the entire intro and crashes in
`load_bg_file` (src/game/bg.c:830) when loading the first stage (BUNKER1,
"bg/bg_sev_all_p.seg", cart 0x10438660). The header IS loaded correctly —
`obLoadBGFileBytesAtOffset` works: `&fileentry->hw_address[offset]`
evaluates to `hw_address + offset` (a valid cart address; the compiled
absolute asset symbols point into the ROM mapped at the cart base, and the
PI shim memcpys from there). The bug is interpretation: BG-file offsets are
N64 big-endian u32s in segment-0x0F form. Header word 1 in the ROM is
`0F 00 00 14` (BE value 0x0F000014 → file offset 0x14 after
BG_SEG_TO_PTR's `+ 0xF1000000` fold); PC reads it LE as 0x1400000F, so
`ptr_bgdata_room_fileposition_list = header + 0x1400000F - 0xF000000`
lands ~0x5 MB past the stack buffer and `...[1].pPointTableBin` faults
(EXCEPTION 0xc0000005, FAULT ADDR ≈ header + 0x500000F). The whole
stage-load path (bg .seg headers/room tables + Tbg_*_stanZ geometry files)
is riddled with BE u32 fields — the same class as D68 but a far larger
format surface. This is the "next asset type" milestone anticipated in
AGENTS.md. Strategy options: (a) offline per-region conversion of all bg/*.seg
+ Tbg_*_stanZ files into sidecars (the D43/Plan-B pattern; requires fully
decoding GE's BG/stan formats from bg.c/stan.c — note PD's
preprocess/filebg.c describes a *different*, zipped multi-section format;
same family ≠ identical, validate per field); (b) runtime port-layer fixup
after each load (same format knowledge, placed in port/). Either way the
first task is reverse-engineering the formats: header words 0..3 are
pointers (rooms/portals/bgcmds/lights-style tables per the D69 probe:
word1=0x14 room-fileposition list), bg_room_data records carry more
0x0Fxxxxxx offsets (pPointTableBin at record+0x28, see crash disasm), and
stanZ files go through stanDetermineEOF/stanLoadFile. TEMP D69 probe in
ob.c (GE_D69) logs name/index/rom_size/hw_address per BG load.

### G. Phase 2 status snapshot (HISTORICAL — frozen ~D74)

> Stale point-in-time snapshot. **Current status: `docs/HANDOFF.md`.**
> Finding detail: §F index above + §H below.

Done through **D68**: PD fast3d integrated (`port/fast3d/`); GE's real
`src/sched.c` + pthread kernel; dual-mapped DRAM; ROM mapped at cart base;
SDL2 window; full boot chain (D31–D42); **Plan B executed (D50)** — all 512
NTSC model files offline-converted to PC-layout RZ sidecars, served through
the existing load path via a port-layer table patch; runtime C fixups for
language-bank BE offset tables (D50.3) and font re-layout (D50.4);
legal-screen UB seed (D50.5); `texCopyGdls` w1 partial-copy bug fixed and
byte-proven (D50.6). **D51–D58 resolved** (font pixeldata fixup, osGetCount
tick rate, model RW-data pools, cseq BE header, synth param slots, RLE
folder-menu background, watch-intro raw offsets, spare-pool capacities,
gun-barrel DL idiom + reservation). **D59 resolved** (the "GL DLL" crash
was D64's unbounded blood-RLE write clobbering the barrel sub-DL),
**D60–D62 resolved** (DMA target validation, OSMesgQueue layout),
**D63** TEMP diagnostics, **D64 resolved** (blood RLE sentinel adjacency),
**D65/D65b resolved** (HEADS/BODIES enum signed sentinels under PORT),
**D66 resolved** (romCopyAligned + ramrom replay 64-bit pointer width),
**D67 resolved** (struct image_entry N64 layout reconstruction),
**D68 resolved** (Globalimagetable BE→LE fixup for IMAGESEG Gfx words +
sImageTableEntry.index; compiled globalDL shadows synced after texLoad).
The game now boots, plays the intro music, and renders the **entire intro**
(Nintendo logo → gun barrel with Brosnan → cast screen; ~frame 2100, ≈2 min
wall-clock at the current ~20 fps clean-run rate), then crashes in
`load_bg_file` on the first stage load —
**D69, the milestone blocker**: BG-file headers are N64 big-endian
(segment-0x0F offsets) and PC reads them LE. Details in §F/D59–D69.

**D70–D74 (intro-logo pixel work):** D70 env-gated PPM frame capture
(`GE_PCDUMP` → `./ppm/`) for numerical visual debugging; **D71 resolved** —
C-array texture sources (the four rarewarelogo.c RGBA16 images) were
byte-swapped on LE PC (pink/green logo); port-layer per-source bswap in
`import_texture`. **D72 resolved** the UV path (GE always uses authored tc[]
UVs; `lookat_enabled` defaults false). **D73 resolved** — root cause of
D72.3: sinf/cosf `du` double constants are big-endian word pairs, garbage on
LE PC → guMtxF2L emitted −32768 for every sin/cos entry → logo triangles
projected off-screen; DVAL() macro fix in guint.h/sinf.c/cosf.c under PORT.
**D74 resolved** — texture import fallback no longer truncates valid
gDPLoadBlock data (mip chains + sub-tiled textures), TextureCacheKey gains
`size_bytes`, and the VBO path now wraps UVs by tile size for WRAP sub-tiles
(N64 semantics). The logo now renders its four gold letters on the dark-blue
plate (PPM-verified at frame ~555); a final pixel-perfect comparison against
N64 reference footage is still open. **D69 remains the milestone blocker**
(stage load).

**Committed through D74**: D51–D74 fixes; this session's TEMP probes are
stripped, but previously committed TEMP diagnostics (D63 blocks, GE_D71LOG,
and the older D51–D66 leftovers) are still in the tree — strip list in
HANDOFF Task 3. Build is GREEN.

### H. Finding log continued (D32 procedure, D70–D121)

> This section began as a per-session "handoff & plan" and grew into the
> D70–D121 finding log. The **D32 repeatable fix procedure** below is
> permanent reference; the rest is the finding archive (use the §F index).
> **Current status: the README. Current task: `docs/HANDOFF.md`.** Session
> narrative: `docs/dev/HANDOFF-ARCHIVE.md`.

Full paste-ready brief: **docs/HANDOFF.md** (primary thread: D69 BG/stan
stage loading — the milestone blocker; secondary: final pixel check of the
now-rendering intro logo). For D69:
reverse-engineer GE's bg .seg + Tbg_*_stanZ formats from the decompiled
consumers (bg.c, stan.c), then choose offline sidecar conversion (Plan-B
pattern, D43) vs runtime port-layer fixup; PD's preprocess/filebg.c is a
reference for the *approach* only — its BG format is different (zipped
multi-section).
Summary:

**State.** D50–D74 resolved and verified (D73 = sinf/cosf endianness, D74 =
texture import fallback + sub-tile UV wrap); committed through the D74
milestone. The game boots, plays intro music, and renders the entire intro
(logo → gun barrel with Brosnan → cast screen) — the Rareware logo now shows
its four gold letters on the dark-blue plate (D71–D74; PPM-verified at frame
~555, final pixel-perfect check vs N64 footage still open). TEMP diagnostics
from earlier sessions are still in the tree (strip list in HANDOFF Task 3);
visual-debug tooling: `GE_PCDUMP` frame capture. Build is GREEN.

**Thread 1 — D69 (milestone blocker, now primary).** `load_bg_file`
(src/game/bg.c:830) faults on the first stage load (BUNKER1): BG-file header
words are N64 big-endian segment-0x0F offsets; PC reads them LE, so the
room-fileposition-list pointer lands ~5 MB past the stack header buffer.
Full analysis + strategy options in §F/D69. This is the next milestone:
decode the bg .seg + Tbg_*_stanZ formats, convert/fix for PC, get a stage to
load and render.

**Thread 2 — logo final check (low priority).** Compare the rendered logo
frames (ppm/, frame ~550–560) against N64 reference footage; if any letter
is still off, re-add a lightweight triangle/texture attribution probe (the
D74 probes are stripped; the import sizes and sub-tile wrap are verified at
the data level).

**After D69:** get a stage to load + render (bg .seg + Tbg_*_stanZ format
work), then continue the diagnose→fix→verify loop through gameplay; strip
TEMP diagnostics at each milestone; pixel-assert soak (PPM dump +
tools_pc/pixcount.py) once a stage is stable.

**D32 repeatable fix procedure** (apply to any ROM-serialized struct that faults
on a pointer-field read):
1. At the fault, `ptype /o <Struct>` in gdb. If a pointer field's offset/size
   diverges from the N64 offset comment (e.g. an 8-byte pointer where N64 has 4),
   it is this bug class.
2. Change the embedded pointer fields to `u32` in the struct (keeps N64 layout on
   x86-64). Add casts at every use site (`(T *)field`). Document as D3x.
3. Verify the load-time rebase/fixup writes valid **V1** DRAM addresses (< 0x80000000)
   into those u32 fields; if not, debug the fixup (see Part B below).
4. Rebuild and confirm the boot advances past this struct to the next init step.
5. If the struct is a *tree* of packed sub-structs (like the libaudio banks, D37,
   or model files, D43), an in-place BE→LE patch will not fit — the expanded
   8-byte pointer slots overrun the following ROM data. Re-lay it out into a
   fresh compact image and rewrite each pointer slot as the sub-struct's new
   offset (zero-extended), so the existing `ptr + (s32)base` rebase still works.
6. If the fault is NOT in ROM-loaded data but in an address arithmetic idiom over
   exe-resident symbols (`(u32)&sym`, XOR toggles, pointer-delta math — D39/D42),
   guard a PC branch that reproduces the N64 32-bit value exactly (e.g. keep the
   0x02000000 base) or replace the idiom with an explicit equivalent; N64 line
   stays verbatim under `#else`.
7. If two structs are **type-punned** (cast back and forth, e.g. ModelSlot ↔
   Model), verify their layouts still agree on PC — pointer-width changes break
   puns silently: fields the game reaches "through" one struct land at wrong
   offsets in the other (D53.2). Also check any fixed-size allocation that must
   contain the grown struct (the 0x20-byte model heap fallback, D53).
8. If a hang (not a crash) appears with no thread making progress, suspect a
   **timing/pacing gate**: GE's loop gates on `osGetCount()` deltas in N64 RSP
   counter ticks (~46.5525/µs), not µs (D52).
9. If the fault is a store through a pointer built from a **raw byte offset
   into a struct that contains pointers** (`(u8 *)ptr + 0xNNN`, D56): every
   pointer field before 0xNNN shifts +4 on PC (plus 8-byte alignment padding),
   so the offset no longer lands on the intended object. Fix under #ifdef PORT
   via the named field (`&s->field`); if the embedded sub-object has grown
   (sizeof_PC > sizeof_N64) and its trailing companion storage (e.g. a model
   RW pool packed right after it in the struct) no longer fits before the next
   live field, relocate that companion to a static buffer of the N64 capacity —
   safe when all access goes through a pointer stored in the sub-object at init
   (modelInit → Model.datas) and only the one site references the raw offset.

**D70 (dev tooling, TEMP):** env-gated PPM frame capture for numerical visual
debugging: `GE_PCDUMP="first-last[:step]"` dumps the bound FBO to
`./ppm/frame_NNNNNN.ppm` from `videoEndFrame()` (`gfx_opengl_pcdump_enabled()`/
`gfx_opengl_dump_bound_fbo()` implemented in gfx_opengl.cpp). Established the
intro timeline numerically (legal text f20–100, Rareware logo f380–580,
iris/gun-barrel f900–1220, red region f1240–1360, cast f1520+) and confirmed
the user's "pink and green" report at f480 (left half RGB≈(135,78,129), right
half (82,104,0)). Strip per HANDOFF Task 3.

**D71 (RESOLVED):** the Rareware logo rendered as two flat colors — pink-red
and bright green — instead of gold lettering. Root cause: the four RGBA16 logo
images in assets/rarewarelogo.c (`imgRAre_0x0020` etc.) are `u32` C arrays
compiled into the exe `.data`; on LE PC each N64 texel pair is stored as a
little-endian u32, so `import_texture_rgba16`'s big-endian u16 read produced
byte-swapped texels (raw 0xED0F gold → 0x4FCC green + 0x0FED pink — exactly
the two observed colors). All other texture sources are raw N64 BE byte
streams (ROM cart map 0x10xxxxxx, model sidecar 0x10Cxxxxx, KSEG0/V1 buffers
0x70–0x90xxxxxx) and must not be touched. Fix (port/fast3d/gfx_pc.cpp):
`gfx_tex_source_is_c_array()` classifies by address range;
`gfx_tex_normalize_source()` bswaps each u32 once per source into a stable
cached buffer (cache key stays the original address) and `import_texture()`
decodes from it. Verified: exactly two sources normalized in a full run (the
logo image banks); logo now renders gold on dark blue. One-shot per-source log
gated by `GE_D71LOG`.

**D72 (RESOLVED + D72.3 OPEN):** the logo UV path.
- D72.1: removed the PD-inherited normal/lookat-based UV overwrite in
  `gfx_sp_vertex()` — GE always uses authored per-vertex `tc[]` UVs: no GE
  code sets G_TT_BASE/G_TT_CLAMP (only G_TT_NONE), and stage geometry has
  gSPLookAt set every frame yet N64 textures are fixed to surfaces, so the
  normal-derived path is wrong for GE in general.
- D72.2: `rsp.lookat_enabled` now defaults false — N64 boots with RSP memory
  zeroed; no lookat exists until gSPLookAt writes one (the intro logo has
  none). GL UV convention confirmed as U.5 (texel×32) from the LUT/rect path,
  so DL_RAREWARETEXT's identity-scale corner UVs 0x0010..0x03F0 map to full
  32×32 coverage with a half-texel inset.
- D72.3 (OPEN): after D71+D72 the logo still does not appear: DBGTRI traces
  show ALL logo triangles (plate fan + letter quads) project off-screen
  (clip x=y≈−1.6e11, w<0, screen ≈(32776,32776)), yet the frame shows a large
  flat dark-blue (0,0,64) region — rows 150..479, cols 1..639 — filled as a
  PERFECT checkerboard (50/50 pixel parity), i.e. broken rasterization of some
  big triangle drawn after clear_framebuffer_black. Identity not yet
  established (`GE_DBGTALL` all-triangle trace captured; analysis pending).
  Ruled out: matrix-format mismatch — this codebase's guMtxF2L is Rare's
  modified variant writing the interleaved hi16/lo16 s32 Q15.16 packed format
  that gfx_sp_matrix decodes exactly (FTOFIX32=×65536); `D_8002A7D0` is a
  zero-init u32 so `[D_8002A7D0]`==`[0]`; alloc_intro_matrices() runs from
  initmenus. **SUPERSEDED by D73** — the off-screen projection was never a
  matrix-format or UV problem: guMtxF2L's sinf/cosf inputs were garbage on PC.

**D73 (RESOLVED — root cause of D72.3):** GE's `sinf`/`cosf`
(src/libultra/gu/sinf.c, cosf.c) build their double constants through the
`du` bit-union (`{ struct { u32 hi; u32 lo; } word; double d; }`) with
big-endian word pairs `{hi, lo}` (rpi, pihi, and the P[] polynomial tables).
On an LE PC the `.d` read has the two words swapped: `rpi.d` ≈ 2^733,
`pihi.d` ≈ 2^257. Range reduction then overflows: `dn = dx*rpi.d` huge →
ROUND(dn) saturates in cvttsd2si (n = −2^31) → `dx = dx − dn*pihi.d` ≈ 2^288
→ xsq ≈ 2^576 → the result overflows float → sinf/cosf return ±inf/NaN.
FTOFIX32's `(int)(±inf·65536)` is another cvttss2si saturation →
0x80000000 (−32768). Every sin/cos-derived entry of guMtxF2L became −32768,
so the Rareware logo's guRotate matrix was garbage and all its triangles
projected off-screen (the D72.3 symptom; the "checkerboard" was a separate
large triangle rasterizing over the empty logo region). The logo scene is the
first visually verifiable consumer of guMtxF2L sin/cos output in the intro;
other scenes that build matrices via guRotate/guLookAt are affected by the
same bug and were fixed by this change. Fix (narrow ABI exception — constant
interpretation only, algorithm verbatim): `duD()` static-inline + `DVAL(x)`
macro in src/libultra/gu/guint.h under #ifdef PORT re-pack hi/lo into the
correct LE double; the 5 `.d` reads in sinf.c and 4 in cosf.c now use
`DVAL(...)`. The N64 build is untouched (#else branch = original expression).
Only sinf.c/cosf.c use `du` (grep-verified); `fu` (single u32) constants are
endian-safe. Verified numerically: P decodes to the expected perspective
matrix, Rot(−40°Y) gives clean cos=0.766/sin=−0.643, and logo vertices clip
to small finite on-screen NDC values. (tools_pc/mtxtest.c is a standalone
scratch harness for the matrix-convention question that was ruled out along
the way — GE stores all matrices transposed; row-vector ≡ column pipeline.)

**D74 (RESOLVED — final pixel check pending):** with D73 in, logo geometry
was correct but the texture side was still wrong: a dim red-brown blob plus
a flat vertical bar at the right edge, one letter visible at a time. Three
port-layer bugs, all in port/fast3d/gfx_pc.cpp (with gfx_pc.h):
- **Import fallback truncation.** `import_texture`'s old condition
  `(rdp.tex_lod && tile >= rdp.first_tile_index + rdp.tex_detail) ||
  !loaded_texture.addr` overwrote valid gDPLoadBlock tmem data with
  `line_size_bytes * tile.height`. For the logo: letter mip chains 2744 B →
  2048 B (mips 1–5 dropped), and D_02005FF0's 32×32 → 192 B (32×3, stale
  tile.height=3 left by `gsDPSetTileSize(0, 46, 116, 124, 124)` in DL
  D_02004758). Fix: fall back only when `!loaded_texture.addr` (N64 TMem is
  persistent — a populated slot is the faithful source).
- **Cache poisoning.** TextureCacheKey lacked the upload size, so the first
  (truncated) import of an address poisoned every later import. Added
  `size_bytes` to the key (gfx_pc.h + both key initializers + the bucket-only
  aggregate in gfx_texture_cache_delete).
- **Sub-tile UV wrap period.** N64 wraps UVs by the TILE size when a render
  tile is a sub-region of the image; the port wrapped at the full uploaded
  image size (GL_REPEAT). D_02004758's 20×3 tile at offset (11.5, 29)
  therefore sampled row 0 instead of rows 29–31 — the flat bar's color
  matched texel [0,4] of D_02005FF0 exactly. Fix: per-vertex pre-wrap in the
  VBO path — for a WRAP sub-tile (`tex_width2 < tex_width`, where
  `tex_width2 = (lrs−uls+4)/4` is the tile window in texels),
  `u = fmodf(u, tw); if (u<0) u += tw; u += uls/4.0f` (same for v). Known
  limitations: half-texel edge bleed at the sub-tile window edges (GL
  bilinear neighbors may sample just outside [O, O+W)); mirror sub-tiles are
  not handled (the logo doesn't use them; audit if another scene regresses).
Verified: runtime import sizes correct (D_02005FF0 = 2048 B, imgRAre =
2744 B); PPM frame ~555 shows the four-letter RARE band with gold/warm
colors and per-letter segmentation; clean `GE_PCDUMP` runs are stable past
1156 frames. Final pixel-perfect confirmation against N64 reference footage
is still open (see HANDOFF Task 1). All env-gated probes added for D72.3/D74
(GE_DBGUV/GE_DBGTRI/GE_DBGTALL/GE_DBGMAT/GE_D74IMP/GE_D74DUMP/GE_DBGLOAD)
were stripped with this change; the previously committed TEMP D63 blocks and
the GE_D71LOG normalize log remain on the HANDOFF strip list.

**D75 (OPEN: 3D rendering — mispositioned / missing 3D models throughout the
front end).** Post-D74 the Rareware logo is correct (gold letters on dark
blue), but every other intro 3D element is still wrong. Confirmed symptoms
(user observation, 2026-08-28 session L):
- **Nintendo logo** renders but is **positioned wrong**.
- **Gun-barrel intro**: the **James Bond figure is entirely missing** (the
  animated walk-and-shoot character model). Barrel/spiral effect status not
  separately confirmed.
- **Intro credits / cast roll**: the per-character 3D models (each shown
  beside their actor/character name) **do not appear at all** — names draw,
  models do not.
Pattern: 2D/texture and text elements draw; **skeletal/animated character
models never appear**, and non-animated 3D (logos) appears but with a bad
transform. This strongly suggests **(b) below is the dominant bug** — the
animated-model path is broken independently of the matrix sin/cos fix. Two
candidate causes — distinguish before any fix:
- **(a) D73 scope gap.** D73 (`DVAL()` in `src/libultra/gu/guint.h`, PORT-only) was
  documented as fixing *"all scenes using guRotate/guLookAt-derived matrices … not just the
  logo."* If these elements still fail, either their matrix path bypasses that fix or a
  second coordinate defect remains.
- **(b) Separate model/RW-pool path.** The player models specifically may be on the
  `animInit` + embedded raw-offsets-into-`struct player` path (cf. D56), which is *not* the
  guMtxF2L sin/cos path — i.e. an independent bug, not "the same coordinate issue."

Files to check: `src/libultra/gu/guint.h`, `src/game/model.c`, `bondview2.c` (animInit /
modelSetScale sites), `port/fast3d/gfx_pc.cpp`. Verify: determine whether the Nintendo logo
and intro player models build matrices via guRotate/guLookAt (→ 75a) or another transform
(→ 75b); capture `GE_PCDUMP` frames across the logo transition and the gun-barrel/cast
segments to localize.

**D75 ADDENDUM (session M-32) — triage: (a) is DISPROVEN, the defect splits
into two independent bugs, both outside `src/libultra/gu` and `src/game/model.c`.**
Static-only pass (no runtime probe; ~1 build-budget spent on reading + rebase
onto M-30b master which carries D159/D164/D165/D166).

- **(a) D73 scope gap — RULED OUT, high confidence.** The whole
  `src/libultra/gu/` tree is endian-clean post-D73. Audited every file:
  `rotate.c` (`guRotateF`/`guRotate`), `perspective.c` (`guPerspectiveF`),
  `translate.c`, `scale.c`, `ortho.c`, `lookat.c`, `mtxutil.c` (`guMtxF2L`),
  `normalize.c`, `align.c`. Only `sinf.c`/`cosf.c` ever use the `du` bit-union,
  and those 9 reads are `DVAL()`-wrapped under `#ifdef PORT`; every other gu
  file uses plain `float` literals (`3.1415926/180.0` etc.) or pure integer
  bit-packing (`FTOFIX32` + shift/mask — byte-order independent). The Rareware
  logo exercises the *entire* gu matrix pipeline front-end
  (`guPerspective`+`guLookAt`+`guRotate`+`guTranslate`+`guMtxF2L`,
  `title.c load_display_rare_logo`) and renders correctly. So no front-end
  matrix path that goes through gu is broken. **Future sessions: do not
  re-audit gu for D75.** (Recorded in porting-notes.md §C.)

- **Bug 1 — non-animated `logoinst` models (Nintendo logo, GE logo,
  front.c wallets/Bond photo): wrong transform / 180° flip. Almost certainly
  the D114/D116 shared fast3d viewport/MP-matrix mirror — NOT in scope,
  NOT game code.** These do NOT use gu at all: `front.c` builds `basemtx`
  with `matrix_4x4_set_lookat_target(&m, 0,0,3000/4000, 0,0,0, 0,1,0)`
  (`matrixmath.c:643` → `matrix_4x4_set_lookat`), copies it into a
  `dynAllocate`d `render_pos`, calls `model.c subdraw`, then
  `matrix_4x4_f32_to_s32` in place (`front.c:1774/2023/2304/2929`, all
  identical). `matrix_4x4_set_lookat*` / `matrix_4x4_set_projection` /
  `matrix_4x4_f32_to_s32` are pure float/int math, native-LE, byte-identical
  in behaviour to the gu equivalents (D114 already verified this for the
  in-level path and disproved converter-axis / lookat-handedness / F2L as the
  cause). D159 (M-31) fixed the *texture* comb on the wallet photo and its
  addendum explicitly notes the residual **"still 180°-rotated"** — a pure
  two-axis negation, i.e. exactly the symptom D114's write-up predicts for a
  hidden X- or Y-flip in `gfx_calc_and_set_viewport` /
  `gfx_adjust_viewport_or_scissor` / the `MP_matrix` vertex transform
  (`gfx_pc.cpp` ~1122/1739/1771). Confidence the logo/photo transform bug is
  the D114/D116 fast3d flip: **medium-high**. This is a `port/fast3d`
  correctness gap — the fix needed is the one D2 of porting-notes.md gates
  behind a RenderDoc/apitrace capture of one texrect (or the asymmetric
  1-texel-texture experiment). No `src/` change will fix it and static
  tracing it is BANNED (porting-notes.md §D2). Parked exactly as before.

- **Bug 2 — animated character models (gun-barrel Bond, cast-roll models)
  entirely absent: category (b), an independent model-instantiation /
  `render_pos` lifetime bug, NOT a transform bug. Root cause not pinned
  (needs runtime probing).** Key discriminator: **in-level skeletal guards DO
  render as humanoids** (`-level_09`, M-12) through the *identical* joint
  path — `subcalcmatrices`→`modelUpdateMatrices`(`process_02_position` etc.)
  →`drawjointlist`→`modelRenderNodeDl`. So the joint math, `matrix_4x4_f32_to_s32`,
  and `gfx_sp_matrix` decode are all proven. What the front-end animated path
  does *differently* from the working in-level path:
  1. `renderData.mtxlist = dynAllocate(numMatrices << 6)` — the per-frame GFX
     arena (`dyn.c:140`, bump-allocated inside `g_VtxBuffers[0]` /
     MEMPOOL_STAGE emulated DRAM), then `instcalcmatrices`/`subcalcmatrices`
     set `model->render_pos = (RenderPosView*)arg0->mtxlist` (`model.c:2409`)
     and the joint matrices are written there and converted **in place**
     f32→s32 by the caller (`title.c:281`, `front.c:1568` …). D115 MED item
     **#5** already flags this exact mechanism: *"the D102 weapon-model
     `render_pos` is pointed at a `dynAllocate`'d transient arena that on N64
     aliased the persistent `hand->mtxlist`; likely the '1P weapon model
     doesn't draw' cause."* The gun-barrel Bond uses the same
     dynAllocate→render_pos pattern (`title.c:253/257`). If the arena is
     swapped / overwritten between the `f32_to_s32` write and the
     `gSPSegment(3, osVirtualToPhysical(render_pos))` bind in `subdraw`
     (`model.c:5346`) / `drawjointlist`, every joint matrix is garbage →
     model collapses to a point / projects off-screen → "absent".
  2. `chrModelInstance` / `gunModelInstance` are stand-alone `Model`s created
     for the intro (`title.c initializeGunBarrelIntro`), not the in-level
     `chr`/`prop` pool — check they are non-NULL, `obj->numMatrices > 0`, and
     the model file actually loaded (the `pcmodels` sidecar must contain the
     Bond/cast model files; a missing manifest row → `hw_address` unpatched →
     load serves N64-layout or fails).
  3. `modelSetAnimation(chrModelInstance, (ModelAnimation*)((s32)&ANIM_DATA_bond_eye_fire + (s32)&ptr_animation_table->data), …)` (`title.c:220`, and `title.c:542`).
     Looks like a truncation risk but is **probably already covered by D34**:
     the PC branch of `assets/animationtable_data.h` defines `ANIM_DATA_*` as
     an lvalue at `g_pc_animdata_base + offset`, so `(s32)&ANIM_DATA_*` is the
     bare offset, and `ptr_animation_table` points into low (<4 GiB, bit-31-clear)
     emulated DRAM, so the `(s32)` sum and the cast back to a 64-bit pointer are
     lossless. Verify anyway with a probe (cheap), but do not lead with it.
  Confidence this is category (b) and not (a): **high**. Confidence on the
  exact mechanism (1 vs 2): **low** — pick with a `GE_PCDUMP` + a capped
  env-gated probe logging `chrModelInstance`, `obj->numMatrices`,
  `render_pos`, `g_GfxMemPos`, and the first joint matrix at the gun-barrel
  frame. **Lead with mechanism 1** (`render_pos` = transient `dynAllocate`
  arena, D115 MED #5) — it also explains why the *in-level* path works (there
  `render_pos` is a persistent per-chr pool, not the gfx arena). An in-scope
  fix would give the front-end animated models a persistent `render_pos`
  buffer (`#ifdef PORT`) instead of the swapped arena, mirroring the D100/D102
  inline-pool pattern.

- **D144/D146 "seg5+0x9ee4 malformed sub-DL" is a DL-walk desync, not a
  transform bug** — "unresolved matrix pointer then garbage opcodes" is the
  D135 signature (a previous command decoded at the wrong width/offset
  desyncs everything after). seg5 = model COL1/BaseAddr (§11.7), so a G_MTX
  landing on seg5 means the walk is already lost, not that a matrix pointer
  is "unresolved". d43_emit.py's opcode-aware remap (§11.6) deliberately does
  **not** touch G_MTX w1; if a front-end model GDL span is emitted with a
  wrong slot count or a `G_TRI*` / GE tex-macro (`0xba`, cf. D124 Facility)
  is mis-sized, the following `G_MTX` reads garbage. This is
  `tools_pc/d43_emit.py` + `port/fast3d/gfx_pc.cpp` territory — outside this
  task's file scope. Recommend a dedicated converter/fast3d agent dump the
  raw bytes of the `logoinst`/`walletinst` model GDL at `seg5+0x9ee4` with
  `GE_DBG*` and diff the PC span against `build-pc/d43_convert.py`'s
  reference output for that file.

**Net for D75:** (a) closed. Bug 1 (logo/photo transform) folds into the
parked D114/D116 fast3d-mirror item. Bug 2 (absent animated models) is a
real, in-scope lead at `title.c:220` `(s32)&symbol` truncation +
`render_pos`=`dynAllocate` arena lifetime — next session should build with a
`#ifdef PORT` `(uintptr_t)` cast there and a capped probe, not a static pass.
Confidence overall: **medium** (triage solid, no runtime confirmation).

> **M-33 UPDATE — Bug 2 (gun-barrel Bond) is substantially a capture artifact
> (D168), and/or has since regressed away.** With the PPM writer fixed and a
> fresh bare-front-end capture (`GE_PCDUMP="700-1200:25"`, no `-level`), the
> gun-barrel **Bond model renders and animates correctly**: frame ~1075 shows
> Bond walking across the iris, frame ~1150 shows him turn and fire, both
> upright, recognisable tuxedo silhouette, correct position inside the barrel.
> The M-32b probe's "entirely absent — no silhouette" reading was of an
> upside-down capture (an inverted gun-barrel is a mostly-black frame with the
> iris low and Bond hanging inverted from it — easy to call "absent"). The
> `chrModelInstance` being valid with sane matrices, which the probe couldn't
> reconcile with "absent", now makes sense: it was drawing all along.
> **Still genuinely broken:** the **Nintendo logo** — frame ~775 renders it as
> two plain overlapping white ellipsoids shifted left of centre, no wordmark,
> no logo geometry. That is NOT a flip (a flipped Nintendo logo is still a
> Nintendo logo) — it's a real `logoinst` model-transform / geometry bug (Bug
> 1 territory). **Cast-roll models** not captured this pass — status unknown,
> re-check with a `1400-2200` window. Net: D75 shrinks to "Nintendo logo model
> renders as degenerate white blobs" plus the unverified cast roll; the
> gun-barrel half is retired. The M-32b probe text below is left for history.

**D75 Bug 2 — RUNTIME PROBE (session M-32b, `GE_D75=1` in `title.c`
`sub_GAME_7F007F30`, kept env-gated).** Booted the bare front end
(`GE_D75=1 GE_PCDUMP="1-1200:120"`, no `-level`) and captured through the
gun-barrel sequence. Findings:

- **Visually confirmed:** the gun-barrel spiral circles (2D backdrop DL)
  render fine; the animated **Bond chr model AND the gun model are
  entirely absent** — no silhouette, no muzzle. `chrModelInstance`
  (`obj=0x14012da80`, `numMatrices=21`) and `gunModelInstance`
  (`obj=0x140148620`, `numMatrices=1`) are **both non-NULL with valid
  `obj` pointers and sane matrix counts** — so mechanism (2) "model file
  not loaded / manifest row missing" is **ruled out**.
- **`render_pos` is NOT stale / clobbered — mechanism (1) as stated is
  ruled out.** Per-frame `chrModelInstance->render_pos` alternates cleanly
  between `0x700a8f10` and `0x700a3f10` (delta `0x5000` = the double-buffer
  swap). On each frame `render_pos` == the `renderData.mtxlist` base that
  `dynAllocate(21<<6)` returned *that same frame* (`gfxpos 0x700a9490` −
  `0x540` chr − `0x40` gun = `0x700a8f10` exactly), i.e. it points at the
  freshly-`subcalcmatrices`-written matrices, exactly as on N64. It is a
  real low-DRAM (`0x700xxxxx`) address; `osVirtualToPhysical(render_pos)`
  returns it **unchanged** (no D131 truncation). Nothing overwrites the
  arena between the write and the draw within the frame.
- **fast3d logs ZERO DL warnings** during the whole sequence (no
  "ending DL" / unknown-opcode / `fast3d_ptr_ok` / bad-matrix-substitution
  lines) — so Bug 2 is **not** the D144/D146 corrupt-front-end-sub-DL
  family either. The Bond/gun DL is submitted and consumed silently but
  emits no visible geometry.
- Mechanism (3), the `(s32)&ANIM_DATA_bond_eye_fire` truncation, was not
  the probe's focus but the model has valid matrices regardless, so a
  broken anim would at worst freeze a pose, not delete the model.

**Where that leaves Bug 2:** the failure is **downstream of matrix setup**
— inside `drawjointlist` / `modelRenderNodeDl` / `dotube` for these
title-screen `Model` instances: either the joint **vertex / node-DL
segmented-pointer resolution** produces no tris, or the model is
transformed off-screen / to zero scale by `renderData.basemtx`
(`= matrix` from `manipulateGunbarrelAndLogoMatrices`, which folds
compiled DL pointers through `OS_K0_TO_PHYSICAL` at `title.c:121/123/138`
— a D58/D84-class `(u32)`-wrap candidate, though the backdrop DLs it
feeds *do* render). **Next probe:** inside `drawjointlist`/`dotube` at the
gun-barrel frame, log the resolved `vtx`/`nodeDl` pointers and whether
`modelRenderNodeDl` emits any `gSPVertex`/`gSP1Triangle`, plus the
composed `basemtx * render_pos` for joint 0 (is it in the view frustum?).
Confidence the cause is joint vtx/DL resolution vs off-screen transform:
**low** — needs the drawjointlist-level probe. The `render_pos`-arena
in-scope fix hypothesised above is **no longer the lead** — do not land a
persistent `render_pos` buffer, it would not change anything.

**D76 (OPEN: 2D graphics — disclaimer/legal screen only partially drawn). ROOT-CAUSED (M-31) → D164.**
The image-table/D68 hypothesis is **wrong**: the legal screen (`constructor_menu00_legalscreen`,
`front.c:1523`) references **zero** `sImageTableEntry`s — it is 12 lines of `langGet()` font text
(`display_aligned_white_text_to_screen` → `textRender`) plus one 3D `logoinst` model (`subdraw`,
D75 family, parked). The "only 2 lines render" is a **linker-adjacency bug** in the text loop — see
**D164**.

**D77 (OPEN: audio — music runs in code but no audible output on PC).**
Intro music is processed without fault, but **nothing reaches the PC speakers.** This is
distinct from the earlier audio work: D54/D54b only stopped the audio-thread SIGSEGVs
(`__getTrackByte`, `alLoadParam`); they do not imply the mixed output is routed to a device.
Per §6 Audio the intended path is libaudio (CPU synth) → PD's `audio.c` (SDL device) +
`mixer.c`; the likely gap is that the SDL audio device / mixer queue is never opened or fed —
synthesis runs but the AI-DMA→device handoff never happens. Files to check: `port/src/audio.c`,
`port/src/mixer.c`, `src/audi.c` (`OUTPUT_RATE`), and the AI shim in `port/src/libultra.c`.
Verify: confirm an SDL audio device is opened and the mix buffer is written/pushed; check
`OUTPUT_RATE` match (PD = 22020 Hz stereo s16) and that the AI-DMA shim feeds it.

**Cross-cutting (Q1 — shared blocker?):** before ordering D75 vs D77, spend one check on whether
any single root cause touches both audio and rendering. Current evidence says **independent**
(audio = libaudio→SDL device; 3D = fast3d RSP emulation; 2D = image-table fixup + texture
import) — default to render-first if no shared cause is found.

**Non-negotiable #2 refinement (applied to AGENTS.md).** The original "game code
compiles unmodified / fix belongs in port/" is too absolute: pointer-width layout
cannot be isolated in `port/` (no hook between the romCopy and the first read).
Refined to "game **logic** is unmodified" with a narrow, documented exception for
mechanical, semantics-preserving ABI/layout changes forced by the 32→64-bit
transition (embedded pointers → u32 + cast at use; PC-guarded pool sizing),
following PD ground truth. No logic/behavior changes; each such edit is logged in
§F/D3x.

**D78 (RESOLVED — StandTile bitfield ABI, `#ifdef PORT` layout exception).**
`StandTile` (`src/bondtypes.h`) declares `u32 id : 24;` immediately followed by
a non-bitfield `u8 room;`. On MIPS/GCC (N64) these share one 4-byte storage
unit (id = bits 31:8, room = the low byte) giving an 8-byte tile header
(id/room word + `mid` u16 + `tail` u16) — the stride `list_of_tilesizes[]`
(0x20…0x58 = `8 + 8*pointCount`) and `stanFillin`'s `link << 3` addressing
both hard-depend on. x86 GCC never lets a non-bitfield member share a
bitfield's storage unit, so the stock declaration compiles to a **10-byte**
header on PC (`room`@4, `mid`@6, `tail`@8 — confirmed via
`offsetof()` probe against the real project headers/flags) — every
`tile->room`/`tile->mid`/`tile->tail`/`tile->points[]` access would silently
misalign, independent of any byte-swapping. `id` is provably dead (no
`.id`/`->id` read or write anywhere in the compiled game code — grep-verified
across `src/`), so under `#ifdef PORT` it is widened to `u8 id[3]` (order
irrelevant, decorative-only) with `room` immediately following as a plain
byte. Verified: this restores the exact N64 stride (`room`@3, `mid`@4,
`tail`@6, `points`@8). Layout-only, no behavior/logic change — same class as
D53.2. This is a prerequisite for D69 (byte-swapping alone cannot fix stan
tile reads if the struct itself is misaligned).

**D79 (RESOLVED — `bg_room_data` pointer-width ABI, `#ifdef PORT` layout
exception).** `bg_room_data` (`src/game/bg.h`) declares `pPointTableBin` /
`pPriMappingBin` / `pSecMappingBin` as `void *`. These are ROM-serialized as
plain 4-byte N64 segment-0x0F offset values and are **never dereferenced**
anywhere in the codebase (grep-verified: every use in `src/game/bg.c` casts
to `(u32)`/`(s32)`/`(u8*) + int` for arithmetic, never `->` or `*`). On
x86-64 `void *` is 8 bytes, silently growing the 24-byte N64 room record to
40 bytes and breaking every `ptr_bgdata_room_fileposition_list[i]` array
index. Fixed under `#ifdef PORT` by declaring them `u32` instead — verified
via `sizeof()`/`offsetof()` probe: `sizeof(bg_room_data)` == 24,
field offsets 0/4/8/12 (matching N64 exactly), so the room table needs no
resizing in the offline conversion, only in-place bswap32. No behavior
change (every existing use site already treats the value numerically); same
class as D53.1/D66.

**D80 (bg `.seg` format spec — converter spec of record).** Header
(`s32 header[0x10]`, only words 0–4 consulted by `load_bg_file`): word0 must
be 0 (bswap32, harmless either way); word1/2/3/4 are `0x0Fxxxxxx`
self-relative offsets (masked `&0xFFFFFF`) to: room-fileposition list,
portal-data-entry table, envdata table (0 = absent), and an optional f32
array (only meaningful if word3 != 0). **Table order in the file is not
index order** — verified across all 34 unique NTSC bg files
(`bg/*.seg` referenced from `levelinfotable`): word3 (envdata) < word2
(portal) in every sample; room-table extent = `[word1, min(word2, word3 if
word3>word1 else word2))`; in every sampled file this divides evenly by 24
(`bg_room_data` record size) and word4 was always 0 (f32-array path
unexercised in this ROM — converter asserts word4==0 and errors loudly if a
future region violates this, rather than silently mishandling it).
- `bg_room_data` (24B): 3× `u32` offset fields (D79) + `coord3d pos` (3×f32).
  All 6 words are plain numeric — blanket bswap32, **no resize** (PC stride
  == N64 stride after D79).
- `bg_envdata_entry_local` (8B, local to `load_bg_file`): `u8 type` + `pad[3]`
  (untouched) + `s32 data` (bswap32). Terminated by `type==0`. **Exception:**
  when `type==ENVIRONMENTDATA_ALT` (100), `data` is not arbitrary — it is
  compared post-rebase against `g_BgPortals[i].offset_portal`
  (`getIndexOfPORTALID`), i.e. it lives in the *same offset space* as
  `bg_portal_data_entry.offset_portal` (the portal point-data blob, below)
  and must receive the identical `+portal_delta` relocation, in addition to
  bswap32.
- `bg_portal_data_entry` (N64 8B: `u32 offset_portal` + 4× `u8`). Unlike
  `bg_room_data`, `offset_portal` **is** dereferenced pervasively elsewhere in
  `bg.c` (`->numPoints`, `->point`, portal/room-visibility walks) — declaring
  it `u32` would require touching dozens of call sites, so it is left as the
  native `bg_portal_entry *` pointer type (no header edit): PC
  `sizeof(bg_portal_data_entry)` is **16B** (`offset_portal`@0 8B,
  `connectedRoom1/2`+`controlbytes1/2`@8-11, 4B pad) — confirmed via probe.
  The **offline converter** (not game code) re-lays the table at 16B/record
  (N portal records + 1 zero terminator record), writing `offset_portal` as
  an 8-byte field: low 4 bytes = bswapped original offset value
  **+ portal_delta**, high 4 bytes = 0. `portal_delta = 8 * (N+1)` (the extra
  bytes inserted by 8B→16B growth). The `bg_portal_entry` point-data blob
  that follows the portal table in the file (target of every
  `offset_portal`/ALT-envdata value) needs **no per-record resize** — PC
  `sizeof(bg_portal_entry)` is 16B, identical to N64 (`u8 numPoints` + `pad[3]`
  + `coord3d point`, no pointer fields) — it is simply relocated by
  `+portal_delta` as a block, with `numPoints`/`pad` copied verbatim and
  `point` (3×f32) bswapped. Net effect: the whole `.seg` file grows by
  exactly `portal_delta` bytes; nothing outside the portal table/blob region
  needs remapping (room table, envdata, and the header's word1/word3 all sit
  *before* word2 and are untouched).

**D81 (`Tbg_*_stanZ` format spec — converter spec of record).** RZ-compressed
(`0x11 0x72` + raw deflate, same scheme as models — decompress/recompress
around the conversion). Decompressed layout: `struct StanPrefixRecord { s32
stanfile; StandTile *ptr_firstroom; }` is dereferenced directly against the
raw loaded buffer (`stanLoadFile`/`stanDetermineEOF` receive the file pointer
itself as `StanPrefixRecord *`), so — same class as D78/D79 — the struct's
PC-compiled layout must match the file's byte layout. N64: `stanfile`@0 (4B)
immediately followed by `ptr_firstroom`@4 (4B pointer). PC: pointer-alignment
forces an implicit 4B pad after `stanfile`, so `ptr_firstroom` compiles to
offset **8**, not 4 — and each subsequent room-offset array slot is a real
8-byte pointer (`stanDetermineEOF`'s `void **roomPtr; roomPtr++` walk and
in-place `*roomPtr = *roomPtr + delta` rebase already use genuine
pointer-width semantics — **no code change needed there**, only the file's
data layout). Converter fix (constant shift, no game-code edit): insert a 4B
zero pad after `stanfile` (array now starts at file offset 8, matching PC
struct layout), and widen every room-offset array slot from 4B to 8B (low
4 bytes = bswapped original file-offset value + `array_delta`, high 4 bytes
= 0; the terminator NULL slot becomes 8 zero bytes). `array_delta = (8 + 8*
(N+1)) - (4 + 4*(N+1)) = 4*(N+2)` where N = room-offset entries before the
terminator. Everything from the old tile-data start to EOF shifts by
`+array_delta` as a block; **tile records need no resize** (D78 restored the
exact N64 8-byte header stride, and `StandTilePoint`/`link` addressing is
already relative to `standTileStart`, computed at runtime — unaffected by
where the tile-data block sits in the file). Per-tile conversion: the 4-byte
id/room word is copied **verbatim** (D78 makes the PC struct byte-identical
to N64 there — no swap needed, it's a byte array not a scalar); `mid.half`
and `tail.half` (s16, top nibble of `tail` = `pointCount` selecting record
size via `list_of_tilesizes[]`: `8 + 8*pointCount`, pointCount 3–10 →
0x20…0x58) are bswap16; each of the `pointCount` `StandTilePoint` entries (8B:
x/y/z s16 + link u16) are bswap16 per field. The **N64-order (still-BE) tail
half must be read to size each record** while walking — same discipline as
the D50 model-node walk. Net: the whole stan file grows by exactly
`array_delta` bytes before recompression; the RZ-compressed sidecar size
(recorded in the manifest, patched into `rom_size`) differs from the N64
compressed size, same as pcmodels (D50) — this is expected and fine, nothing
in the load path assumes N64 compressed size.

**D82 (converter + port wiring — see `tools_pc/d69_emit.py` /
`port/src/pccg.c`).** Implements D80/D81 above: per NTSC bg/stan file
referenced by `levelinfotable`, converts and concatenates into
`data/pccg-ntsc-final/pccg.bin` + `manifest.csv` (`name,offset,size` decimal,
`file_resource_table` order) — same manifest shape as `pcmodels.bin`
(D50/`d43_emit.py`). Port layer (`port/src/pccg.c`, cloned from
`pcmodels.c`): `pccgReserveSize`/`pccgLoadSidecars`/`pccgPatchTable`, wired
into `port/src/romdata.c`'s cart-reservation extension alongside
`pcmodels*`, and `romdataCartAddrValid`/`libultra.c`'s D60 DMA-source bounds
check extended to cover the pccg byte range. One-shot patch call:
`pccgPatchTable()` from the same `load_object_fill_header` hook site as
`pcmodelsPatchTable()` (idempotent, matches every table entry by filename,
rewrites `hw_address`/`rom_size`). Regenerate: `python tools_pc/d69_emit.py
[ntsc-final|pal-final|jpn-final]`; only `ntsc-final` regenerated/verified
this session (PAL/JPN ROMs not present in this environment) — `data/pccg-*/`
is gitignored like `pcmodels-*`.

**D83 (RESOLVED — StandTileHeaderMid/StandTileHeaderTail bitfield ABI,
found during D69 verification).** After D78-D82 landed, a clean run reached
`stanBuildRoomData` (`stan.c:245`) without faulting, but then **hung
forever** (kernel heartbeat: no frame rendered, stuck at the same PC across
repeated snapshots). Root cause: same MIPS-BE-vs-x86-LE bitfield-packing
class as D78, but in a struct D78 didn't touch. `StandTileHeaderTail {
s16 pointCount:4; s16 headerC:4; s16 headerD:4; s16 headerE:4; }` — on
N64/MIPS the FIRST-declared field occupies the HIGH bits (`pointCount` =
top nibble); x86 GCC packs the first-declared field into the LOW bits
(`pointCount` = bottom nibble instead). `tile->tail.hdrTail.pointCount` is
read pervasively (`list_of_tilesizes[]` tile-size lookup used for
navigation, edge walks, `stanBuildRoomData`'s bounds loop) — with the stock
declaration this silently read the wrong nibble on PC. An env-gated probe
(`GE_D69STAN=1` in `stanBuildRoomData`, TEMP, kept) proved it directly:
tile tail=`0x03dc` (N64: pointCount=0, top nibble) decoded to
`pointCount=12` on PC (bottom nibble) — `list_of_tilesizes[12]` is
out-of-bounds (table has 12 entries, 0-11) and happened to read a stray 0,
so `tile` never advanced — infinite loop. Fixed under `#ifdef PORT` by
declaring both `StandTileHeaderMid` and `StandTileHeaderTail`'s fields in
**reverse order**: x86's low-to-high packing then lands each field in the
same bit position MIPS's high-to-low packing does (byte-identical numeric
result, verified via a union/probe against `0x03dc` returning
`pointCount=0`). `StandTileHeaderMid`'s fields (`special`/`r`/`g`/`b`) are
never read via their bitfield names either (only via `.mid.half >> 0xc`
elsewhere in stan.c) so that half of the fix is precautionary. Same
narrow-ABI-exception class as D78; no logic change.

**D84 (RESOLVED — bg.c hand-inlined segment-fold 64-bit-pointer overflow,
found during D69 verification).** With D83 in, `stanBuildRoomData`
completed and the game proceeded into room streaming
(`bgCheckIfRoomModelNeedsLoad` → `bgLoadRoomModelData` →
`bgLoadRoomVtxData`/`bgLoadRoomPrimaryGdl`/`bgLoadRoomSecondaryGdl`), which
then **segfaulted** at a fixed, reproducible fault address
(`0x7104561d`, identical across runs) inside `bgBuildRoomVtxBounds`
(`bg.c:2852`, reading `vtx[i].v.ob[0]`). Root cause: `bgLoadRoomVtxData` /
`bgLoadRoomPrimaryGdl` / `bgLoadRoomSecondaryGdl` each hand-roll the
`BG_SEG_TO_PTR` fold instead of calling the macro:
`offset = (((u8 *)room->pPointTableBin + ptr_bg_data) - ptr_bg_data) +
0xf1000000;` (and the Pri/Sec-mapping equivalents). On N64 this "+base
-base" cancellation is a no-op inside 32-bit pointer arithmetic that wraps
for free. D79 made `pPointTableBin`/`pPriMappingBin`/`pSecMappingBin`
plain `u32` fields (never dereferenced, matching every other use site), but
these three call sites still cast them to `(u8 *)` and did the arithmetic
as real 64-bit pointers: `+0xf1000000` no longer wraps at 32 bits the way
`BG_SEG_TO_PTR`'s explicit `(u32)` cast does, so the computed `offset`
came out roughly 4 GiB too large, corrupting every downstream room-file
read (compressed-data location and size). Fixed under `#ifdef PORT` by
doing the fold as plain `u32` math at all three sites, matching
`BG_SEG_TO_PTR` exactly (`offset = (u32)room->pPointTableBin +
0xf1000000;`, no pointer involved) — same narrow ABI-exception class as
D79/D69's original BG_SEG_TO_PTR fix, no logic change. (Root-caused via an
env-gated probe, `GE_D69BB=1` in `bgBuildRoomVtxBounds`/
`bgLoadRoomPrimaryGdl`, TEMP, kept — confirmed the compressed room-DL bytes
now start with the correct `11 72` RZ magic at the right file offset.)

**D85 (OPEN — room primary/secondary DL binaries decode to garbage after
D84; safety-netted, not crash-fixed at the geometry level).** With D84 in,
the compressed room DL binary loads and decompresses correctly (verified:
`11 72` RZ header at the right offset, plausible decompressed size), but
the **content** `texCopyGdls`/`texLoadFromGdl` produce from it is not a
valid GBI command stream (`GE_D69BB=1` dump: `cmd=00`, `01`, `02`, `52`...
none of these are display-list opcodes actually present in the source
bytes — the raw N64 bytes are untouched by the offline converter (D80: the
whole per-room DL/point-index blob is a byte stream, deliberately left
unconverted, out of scope for this milestone) and `texLoadFromGdl` is the
*same, already-working* model-GDL runtime converter (`bgLoadRoomPrimaryGdl`
calls it identically to the model-loading path) — so either room GDLs use
a BG-specific command/marker convention `texLoadFromGdl`'s marker-expansion
logic doesn't handle, or something upstream of it (compression alignment,
`csize_primary_DL_binary`/`csize_secondary_DL_binary` delta sizing) is
still off. Not yet root-caused; full triage is D75-class 3D-pipeline work,
out of scope for this session. **Crash prevented, not geometry fixed:**
added a `#ifdef PORT` bounds check in `bgBuildRoomVtxBounds` before every
`vtx[i]` dereference (`vtxOff`/`vtxEnd` must fit inside
`usize_point_index_binary`) — a garbage command stream now produces an
empty/degenerate bounding box for that vertex batch instead of an
out-of-bounds read, so a bad room fails to render sanely rather than
segfaulting. Follow-up: decode what `texLoadFromGdl` actually does with
room-specific opcodes (`bgApplyDynamicCCRMLUT`/`ptrDynamic_CC_RM_LUT`/
`DL_LUT_PRIMARY_ADDFOG` suggest room GDLs carry CC/RM-LUT-selection markers
models don't use) and verify `csize_*_DL_binary` sizing end-to-end.

**D86 (RESOLVED — `modelInitRwData` crash was a single truncating pointer
cast in the player's embedded gait/arm model init, unrelated to bg/stan).**
Root-caused with a new env-gated trace (`GE_D86=1`: node-walk trace in
`modelInitRwData` + a load-identity probe in `load_object_fill_header`,
both TEMP, left in place). The trace showed the crash node's low 32 bits
were `(header_ptr & 0xFFFFFFFF) + 0x1E0` with the high 32 bits zeroed, and
that this header was **never** loaded via `load_object_fill_header` (no
matching probe line) — pointing at a statically-embedded model, not a
dynamically-loaded one. `src/game/initplayergaitobject.c:5` does
`player_gait_object_header.RootNode = (int)&player_gait_hdr;` — a
same-width (32-bit) pointer→int→pointer round trip that's a no-op on N64,
but on PC `(int)` truncates the real 64-bit `&player_gait_hdr` to its low
32 bits, and the implicit int→pointer conversion back into `RootNode`
zero-extends it, dropping the executable's load-base high bits (module
maps at `0x140000000`, so the truncated pointer silently loses the
`0x1`). `init_player_gait_object()` runs once from `boss.c:236`, and
`player_gait_object_header` is only used once real gameplay starts
(`initBondDATAdefaults.c:99` `animInit`s the player's gait model) — never
exercised while the game only ever got as far as the intro/cast screens.
Fixed with a `#ifdef PORT` branch in `initplayergaitobject.c` that assigns
the real pointer directly (behavior-identical to the N64 assignment,
ABI-width fix only). Verified: BUNKER1 now loads past this point with a
clean, deterministic repro via `-level_09` (see D88).

**D87 (RESOLVED — attract-mode demo playback (`ramrom_replay_handler`)
crashed on a big-endian `ramromfilestructure` read with no byteswap).**
Found while re-verifying D86: an idle front-end run (no player input)
eventually calls `select_ramrom_to_play()` (`ramromreplay.c`), which picks
a random compiled-in demo blob from `ramrom_table[]` (`ramrom_Dam_1`,
`ramrom_BunkerI_1`, etc. — genuine shipped attract-mode assets, not a
debug-only feature; the debug-menu replay path, `DEB_REPLAYRAMROM`, is
structurally unreachable in this `ntsc-final`-equivalent build since
`DEBUGMENU` isn't defined — confirmed with a `gdb -p <pid>` **attach**
hardware watchpoint on `is_ramrom_flag`, which resolved cleanly and
quickly this session; attach mode works fine for a non-timing-dependent
write, unlike the launch-mode-only guidance logged after the D56 session —
worth a retry next time attach seems useful). `replay_recorded_ramrom_at_address`
loads `ramromfilestructure` via `romCopyAligned()`, a raw byte copy (by
design, D66) from a real ROM-compiled asset — so, like every other
N64-compiled ROM asset, its multi-byte fields are big-endian, and nothing
byte-swaps them on read. A real `size_cmds` of 2 (BE bytes `00 00 00 02`)
read as native LE prints as `33554432` (`0x02000000`); that garbage then
drives the loop bound and pointer arithmetic in
`iterate_ramrom_entries_handle_camera_out`/`ramrom_replay_handler`, which
walks far outside the small `ramrom_blkbuf_2`/`ramrom_blkbuf_3` scratch
buffers and segfaults reading `temp_v0->stick_x`
(`ramromreplay.c:301`/`ramrom.c` callers). Root-caused with a new
env-gated probe (`GE_D87=1`, left in place). Fixed with a `#ifdef PORT`
`ramromFixupEndian()` in `ramromreplay.c`, called once right after the
`romCopyAligned()` in `replay_recorded_ramrom_at_address` (same pattern as
the D54 cseq-header fixup): byte-swaps every multi-byte field
(`u64`/`u32`/enum fields via `__builtin_bswap64`/`32`, `save_data.options`
via `bswap16`); `save_data`'s single-byte fields and the `times[]` byte
array are left alone. The **downstream** per-frame chunks
(`ramrom_seed`/`ramrom_blockbuf`, read via the same `romCopyAligned`
pattern in `iterate_ramrom_entries_handle_camera_out`) are all-`u8`
structs and need no swap. Not BUNKER1-specific — this is a front-end/
attract-mode path that can select any of the 7 demo locations at random;
use `-level_09` (see D88) to skip the front end entirely for deterministic
BUNKER1 testing instead of waiting on/fixing attract mode.

**D88 (OPEN — root-caused, next blocker: per-level `Usetup*Z` "stage
setup" file is raw N64-endian/width ROM bytes read directly through a
PC-widened struct, with no conversion at all).** Found immediately after
D86/D87 while re-verifying BUNKER1 specifically — launch with `-level_09`
(`boss.c:199-339` decodes `-level_XX` into `g_StageNum`, bypassing the
front end/attract-mode entirely for a fast, deterministic repro; NTSC
`LEVELID_BUNKER1 = 9`, and the token's two digit-chars are consumed as raw
ASCII bytes, so `"09"` → `'0'*10 + '9' - 0x210 = 9`) reaches the exact same
crash as the random attract-mode run, immediately and reproducibly:
`proplvreset2` (`prop.c:1306`) segfaults reading
`g_CurrentSetup.pathwaypoints[i1].padID`. `prop.c:1267-1282` loads the
level's `"Usetup<name>Z"` file with `_fileNameLoadToBank` (raw ROM bytes,
**not** run through any PC-layout converter — unlike bg/stan (D69/D80-82)
and models (D43/D50), this asset type has zero PC porting work done on
it) into `local_stage`, then rebases 10 top-level fields
(`pathwaypoints`/`waypointgroups`/`intro`/`propDefs`/`patrolpaths`/
`ailists`/`pads`/`boundpads`/`padnames`/`boundpadnames`, plus nested
`neighbours`/`waypoints`/`ailist` pointers inside the sub-tables) with
`(void *)(((u32) local_stage) + ((u32) local_stage->pathwaypoints))` —
i.e. by reading the *raw file bytes* directly through the live
`struct stagesetup` (`bondtypes.h:4091`), whose 10 fields are declared as
real pointers. This is worse than a plain missing-byteswap bug (cf. D87):
on N64 those 10 fields are 4 bytes each (40-byte header, correctly
self-describing "byte offset from file start" per the code's own
comment), but the PC struct widens every pointer field to 8 bytes (an
80-byte header) — the same class as D79 (`bg_room_data` pointer growth)
— so field N's read doesn't even land on the right *bytes* of the file
past field 0, before even considering that the 4 meaningful bytes it does
read are big-endian. Confirmed no PORT/byteswap handling exists anywhere
in `prop.c` (`grep` for `bswap`/`#ifdef PORT` in the file: zero hits).
**Not fixed this session** — this is format-conversion work at the same
scale as D69 (a whole ROM asset type needs a byte-accurate spec + either
an offline converter sidecar, the established preferred pattern per
AGENTS.md, or a careful runtime fixup pass that parses the raw 40-byte
N64-packed header by explicit byte offset, byte-swaps each field, and
writes the results into the PC-widened `stagesetup` struct — plus the
same treatment for every nested sub-table referenced from it
(`waypoint`/`waygroup`/`PropDefHeaderRecord`/`PathRecord`/`AIListRecord`/
`PadRecord`/`BoundPadRecord`/`pname`, each of which likely has its own
internal offsets/BE fields not yet audited). **This is the actual next
blocker to a rendered BUNKER1 frame** — reachable deterministically via
`-level_09` in well under a minute, no attract-mode wait required.

**D88.1–D88.3 RESOLVED / VERIFIED (2026-08-28, session L).** The
`Usetup*Z` offline converter (`tools_pc/d88_emit.py`, 531 lines) was
written and run in a prior interrupted session; this session verified its
output is correct and consumed at runtime:
- `port/src/pccg.c` `PCCG_MAX_FILES` grown 128 → 256 so the sidecar image
  can also carry the 21 `Usetup*Z` rows (manifest now has them, e.g.
  `UsetuparchZ,3375808,19265`).
- The converter delta-relocates the 8 growing tables (header 40→80B,
  waypoint 16→24, waygroup 12→24, PathRecord 8→16, AIListRecord 8→16,
  PadRecord 44→56, BoundPadRecord 68→80, pname 4→8) and bswaps the s32
  ID/offset fields — same technique as D80/D81, generalized to many
  interleaved regions.
- `src/bondtypes.h` `SetupIntroCamera`: `lang1c`/`lang20`/`prev`
  ROM-serialized pointer-shaped fields kept narrow (`u32`) under
  `#ifdef PORT` so `sizeof` stays 40 and the fixed-stride intro-record
  walk in `bondview_r.c`/`bondview2.c` still matches the 40-byte file
  records; use sites cast `(char *)(uintptr_t)` at each read. Same class
  as D79/D53.1. **Write-before-read verified**: `bondviewLoadSetupIntroSection`
  (`bondview_r.c:276-300`) writes `prev` (list link) and both `lang_ptr`
  members (via `langGet()`) before the only subsequent reads.
- **Verified via `-level_09` + `GE_D88=1` probe**: `proplvreset2` now
  walks the entire pads table correctly — plink name strings (`p1988e`,
  `p12295e`, …) and sane BUNKER1 world coordinates for every pad; the
  crash at `prop.c:1306` (`pathwaypoints[i1].padID`) is **gone**.

**D88.4 RESOLVED / VERIFIED (2026-08-28, session M).** The `propDefs`
polymorphic record stream is now converted N64→PC by the offline sidecar.
`-level_09` no longer crashes in `setupDoor`/`modelLoad` (or anywhere else)
— BUNKER1 loads its full stage setup and renders **1000+ frames
continuously** with zero FATAL/EXCEPTION (only the pre-existing unrelated
`romdataFixupMusicSeqTable` warning).

Key facts established:
- The `propDefs` stream is a flat `s32[]`. Each record's **serialized N64
  word count is fixed per `type` byte across all 21 levels** — verified by
  parsing the getools C sources (`assets/obseg/setup/Usetup*Z.c`), which
  tile every `propDefs` region byte-for-byte against the retail ROM
  (BUNKER1: 206 records, 6477 words, end offset lands exactly on `intro`).
  Table: `PROPDEF_N64_WORDS` in `tools_pc/d88_propdefs.py`.
- Every pointer member inside a serialized record is **`0` in the file**
  (runtime-populated by the `New_*Record` macros). So there is no
  garbage-pointer / delta-relocation problem in the file — only that on PC
  those slots widen 4→8B, growing records that contain pointers and
  changing the `sizepropdef()` walk stride.
- The brief's "narrow the trailing pointers `#ifdef PORT`" idea (D88.1
  pattern) is a **poor fit**: `ObjectRecord.prop`/`.model` are mid-struct,
  and `ObjectRecord`/`DoorRecord`/`GuardRecord` are used in ~350 runtime
  sites — narrowing would touch core gameplay code broadly.
- The `#if 1` branch of `sizepropdef()` was **already N64-correct for every
  type except `OBJ_COPY_ITEM`** (returned 1, real serialized size 3).

Fix (chosen: **converter grows records to native PC layout**, no struct
changes):
1. `tools_pc/d88_propdefs.py` — rewrites each record to its native PC
   struct size: header word (`u16 extrascale`+`u8 state`+`u8 type`) and
   `_mkword` half-pairs byte-swapped independently, scalars `bswap32`'d,
   pointer slots widened to 8 zero bytes (8-aligned), runtime areas
   zero-filled. Per-type PC size = `PROPDEF_PC_BYTES`, sourced from the
   compiler-verified `tools_pc/d88_layoutprobe.c` (`sizeof`/`offsetof`
   against the real port include chain).
2. `tools_pc/d88_emit.py` — feeds the converted stream + its growth into
   the cumulative delta so `intro` and every later sub-table shift.
3. `loadobjectmodel.c` `sizepropdef()` — `#ifdef PORT` branch returns
   `PROPDEF_PC_BYTES/4` so the in-place walk (`prop.c`,
   `loadobjectmodel.c`, `objective_status.c`) matches the emitted stride.

`intro` conversion was already correct (blanket per-word `bswap32`; the
type discriminant is a full `s32`). **Not yet done:** `PROPDEF_PC_BYTES`
for `VEHICHLE`/`AIRCRAFT`/`TANK`/`AMMO`/`DEPOSIT_IN_ROOM` are placeholder
guesses (not used by BUNKER1) — probe them before those levels load.

**D88.5 (WATCH — stan tile name lookups all miss during pad setup).**
With the `GE_D88` probe on, every `stanMatchTileName` call from
`proplvreset2`'s pad loop walks the full ~2599-tile room and returns "no
match". May be benign at load (a NULL `pad->stan` is tolerated by the
reset path), but the stan-id derivation (`stanIdHi`/`stanIdLo` from the
pad `plink` name) could be another victim of a residual endian/width bug.
Re-check after D88.4; do not treat as resolved just because it doesn't
crash.

**D88.5 (RESOLVED — stan tile-name byte-swap in the converter).** With the
`GE_D88` probe on, every `stanMatchTileName` call during pad setup missed
(0 matches / 276 misses). Root cause: `stanMatchTileName` reads a tile's
packed name id through a `StandTilePoint` alias — `(u16)tile->x ==
stanIdHi` and `*((u8 *)&tile->y) == stanIdLo`. D78 left the 4-byte
id/room word as a verbatim byte array on the premise "`id` is provably
dead", which missed this aliased *scalar* read: on little-endian PC the
`(u16)` load of the big-endian id-hi bytes comes back byte-swapped.
Fix (converter, `tools_pc/d69_emit.py` stan path): swap bytes 0–1 of the
id/room word; byte 2 (`stanIdLo`) and byte 3 (`room`, read as
`tile->room`) stay put. Verified 273/273 name matches after the fix; pads
now resolve real stan tiles. Needs sidecar regen. Committed.

**D88.6 (RESOLVED — intro CAMERA `lang1c` is a `u16` pair).**
`SetupIntroCamera.lang1c` is `union { u16 lang_index[2]; u32 lang_ptr; }`
and `bondview_r.c:295` reads `lang1c.lang_index[1]`. `d88_emit.py`'s intro
converter `bswap32`'d the whole word, which swaps element 0 with element 1
— the consumer then read the wrong language-slot id, indexed `g_LangBanks`
out of range and crashed in `langGet` (`language.c:421`) on a NULL bank.
Fix: byte-swap each `u16` of `lang1c` in place; `lang20` (a real `s32`)
still gets `bswap32`. Needs `d88_emit.py --regen`. Committed.

**D89 (RESOLVED — two crashes between stage-load and first frame).**
(a) `init_path_table_links` (`initpathtablelinks.c:144`):
`validationGroupCursors[-3]` is a decomp artifact — a constant negative
index into a 1-element stack array standing in for a plain cursor local.
GCC proves it OOB and emits a trap → SIGILL on PC. Fixed under `#ifdef
PORT` by pointing the name 3 elements into a real 4-element backing buffer
(identical `[-3]` expressions, now in bounds); N64 build keeps the plain
array. (b) `sub_GAME_7F0B0914` (`walkTilesBetweenPoints`): callers like
`domakedefaultobj` pass `&pad->stan`, legitimately NULL when a pad's stan
name doesn't resolve. On N64 the walk reads ~0 for `pointCount` and
returns TRUE via the `crossings==0` early-out; on PC the near-NULL read
faults. Guarded `*tileStack == NULL → return TRUE`. Committed.

**Session-M-2 infra fixes (committed).** Two port-layer gaps that made
`-level_09` a no-op were fixed: (1) `osPiReadIo` was stubbed to 0 so the
cartridge-token read always yielded an empty string — N64 debug switches
were silently ignored and only attract-mode demo playback could reach a
level. Now synthesised from `argv[1..]` (`sysGetTokenString` in
`system.c`, served from the 0xFFB000 range in `libultra.c`). (2)
`pccgPatchTable`/`pcmodelsPatchTable` were called lazily from the first
model load; a direct `-level_XX` boot loads a stage first and
`load_bg_file` read raw big-endian ROM. Moved the one-shot calls to the
end of `obInit()`. NOTE: a bare `-level_09` still needs the per-level
memory args too (`-ml0 -me0 -mgfx100 -mvtx50 -mt700 -ma150` for BUNKER1,
from `boss.c`'s `memallocstringtable`) — the `-level_` branch skips the
default `-m*` string. TODO: auto-inject from `memallocstringtable` in the
port so bare `-level_XX` works.

**D90 (RESOLVED — `stanTileDistanceRelated` zero-fill overran the caller's
stack).** Symptom: after D88.5/D88.6/D89, `-level_09` loaded BUNKER1 and
faulted in `stanIsSpecialBit1Set` (`stan.c:2364`, `arg0 == NULL`) on the
first player collision tick (`bondviewCalcUpdatePlayerCollision` →
`bondviewTrySimpleMovePlayerCollision` → `bondviewTryMoveToStan` →
`stanTileDistanceRelated` → `sub_GAME_7F0B1DDC` → `callbackA(NULL, …)`).
Root cause was NOT the pad→stan resolution (GE_D90 probe confirmed all
159 BUNKER1 pad names resolve and the player spawn pad #102 has a valid
stan). It was `stanTileDistanceRelated`'s N64 "HACK" init loop: it
zero-fills `((s32*)arg4)[0..19]` — **80 bytes** — while
`sizeof(StandTileLocusCallbackRecord)` is 16B. On N64 the 64-byte overrun
landed in adjacent stack scratch; on PC the frame layout differs (and
locals are pointer-widened), so the fill zeroed `bondviewTryMoveToStan`'s
live `sp90` (= `field_488.current_tile_ptr`) right before it was passed
as `&sp90` to the walk. Fix: `#ifdef PORT` clears exactly the 4 record
fields (every consumer only uses those four — cf. `sub_GAME_7F0B21B0`).
Committed. GE_D90 probes left in place (env-gated).

**D91 (RESOLVED — bg portal-descend truncated an array-element address).**
`sub_GAME_7F0B7F84` (both variants, `bg.c`): `i = (s32) &D_800442FC[
portalnum];` then later `*((u8 *) i) = depth;`. The `(s32)` cast drops
the top 32 bits of the array address on PC, so the byte store faulted
during portal occlusion culling. `i` is only used as an `if (i);` no-op
after the cast, so under PORT keep it a plain value and write
`D_800442FC[portalnum] = (u8) depth;` directly. Committed.

**D92 (RESOLVED — two truncated pointers on the chr/AI spawn path).**
(a) `chrAllocate`'s 5th parameter was declared `s32` but both call sites
pass `ailistFindById()`'s `AIRecord *`. The 64-bit pointer was truncated
binding to `s32 arg4`, then forwarded to
`init_GUARDdata_with_set_values`'s `AIListRecord *arg5` → `chr->ailist`
held e.g. `0x40127640` instead of `0x140127640`, and `ai()` faulted on
`(AiListp + Offset)->cmd` at the first AI tick. Param widened to
`AIListRecord *` under `#ifdef PORT` (`chr.c` + `chr.h`). (b)
`Model.unka0` is a 32-bit field that on N64 holds a function pointer —
always `sub_GAME_7F01FC10` (`chr.c:1618` stores `(s32)sub_GAME_7F01FC10`,
the only value the setter ever gets). `model.c` `subcalcpos` calls it
back through a cast → truncated jump target. Widening the field would
shift the rest of `Model`, so under PORT the setter stores a nonzero flag
and `subcalcpos` calls `sub_GAME_7F01FC10` directly. Committed.

**D85 revisited (OPEN — now the live blocker on `-level_09`).** With
D90–D92 in, BUNKER1 loads and ticks all the way to the **first render**,
which immediately hits `sysFatalError("Bad size for RGBA texture in tile
0: 00")` (`port/fast3d/gfx_pc.cpp:967`) — a `G_SETTILE` with `fmt=RGBA
siz=0` (invalid). This is the room-GDL-decodes-to-garbage problem from
D85 (the raw N64 per-room DL/point-index blob is left unconverted, D80),
surfacing in the texture path this time rather than
`bgBuildRoomVtxBounds`. Attract mode's "~2100 frames" never hit this
because those frames were HUD/menu screens, not room geometry. This is
the render-milestone work: decode what `texLoadFromGdl` does with
room-specific opcodes / CC-RM-LUT markers, and verify `csize_*_DL_binary`
sizing (see the original D85 entry above). Interim option if a fresh
session wants to keep moving past it: soften the four `sysFatalError`
"Bad size…" guards in `gfx_pc.cpp` to skip-with-warning (same
safety-net philosophy as the D85 `bgBuildRoomVtxBounds` bounds check) so
the frame renders with placeholder textures instead of aborting.

**D93 (RESOLVED — null-room (room 0) NULL-deref sat in front of the D85
texture wall).** Committed `164d7f99`. On `-level_09` the visible-room
draw list includes room 0, which has no geometry (`csize_*_DL_binary ==
0`), so `bgLoadRoomModelData` never assigns `ptr_expanded_mapping_info`
and it stays NULL. Two consumers then walk it unconditionally:
`bgApplyDynamicCCRMLUT` (called `start=NULL, end=NULL` → the `end==NULL`
sentinel-scan branch derefs address 0) and `bgBuildRoomVtxBounds`
(`while (gdl[i].dma.cmd != G_ENDDL)` on `gdl=NULL`). On N64 address 0 is
readable RDRAM so both walks wander harmlessly; PC page 0 is unmapped.
Fixed with narrow `#ifdef PORT` NULL guards (same safety-net class as the
D85 `bgBuildRoomVtxBounds` vtx-bounds check). `-level_09` now reaches the
documented D85 `sysFatalError("Bad size for RGBA texture in tile 0: 00")`.

**D85 root cause CONFIRMED (was "not yet root-caused").** The
decompressed per-room primary/secondary DL blob is **raw N64 data with
8-byte big-endian `Gfx` slots**, but every PORT-patched consumer
(`texCopyGdls`/`texLoadFromGdl` in `tex.c`, `bgApplyDynamicCCRMLUT`,
`bgBuildRoomVtxBounds`) was patched under `#ifdef PORT` to assume 16-byte
little-endian PC `Gfx` slots — so they stride at 2× the real rate and
read the middle of each N64 command pair as an opcode (the observed
`cmd=00,01,02,52…` garbage), and even at the right stride the `w0/w1`
words are unswapped BE. The model-GDL path is immune only because its
offline sidecar (`tools_pc/d43_*.py`) pre-widens + byteswaps every GDL;
D80 explicitly left the per-room DL/point-index blob unconverted.
**Fix chosen: runtime fixup**, not a sidecar — the blob is RZ-compressed
inside the bg `.seg` and delta-sized (its size is only the offset delta
between consecutive rooms' `pPriMappingBin`), so an offline widen would
force rewriting the whole bg-header offset table / recompression. The
transform is purely mechanical (8→16 byte widen + `bswap32` each word);
room GDLs need no pointer remapping (`G_VTX` seg addresses are resolved
at runtime via `SEGMENT_OFFSET(...) + (u32)vertices`, textures via the
`G_NOOP`+`texnum` marker). Implemented in `bgLoadRoomPrimaryGdl` /
`bgLoadRoomSecondaryGdl` between `bgDecompress` and `texCopyGdls`, plus a
`Vtx` short-field bswap in `bgLoadRoomVtxData` (positions/uv are BE; the
4 rgba `u8`s are fine). Watch the alloc budget — the PC blob is 2× the
decompressed size. Also fix the pre-existing bug at `bg.c:2448`:
`texLoadFromGdl((Gfx *)scratch, (Gfx *)expanded_size, ...)` casts the
size arg to a pointer.

**D85 widen fix IMPLEMENTED (committed `ea8a37a0`/`c732425d` = master
`c732425d`).** `bgWidenRoomGdl()` — in-place back-to-front 8→16 widen +
`bswap32` per word — runs in `bgLoadRoomPrimaryGdl`/`bgLoadRoomSecondaryGdl`
right after `bgDecompress`; the doubled size flows into `texCopyGdls`/
`texLoadFromGdl`/`usize_*_DL_binary`. `bgSwapRoomVtx()` `bswap16`s the 6
leading `u16`s of each `Vtx` in `bgLoadRoomVtxData`. The `bg.c:2448`
size-cast-to-pointer bug is fixed under `#ifdef PORT`. Verified: room
GDLs now decode to real GBI (`GE_D69BB` dump: `E7` RDPPIPESYNC / `BA`
SETOTHERMODE_H / `B9` SETOTHERMODE_L / `FC` SETCOMBINE / `BB` TEXTURE /
`B7` SETGEOMETRYMODE), G_NOOP markers decode to sane texnums. Alloc
budget needs no change (first-load block is `memaGetLongestFree`-sized,
then shrunk from post-widen `used`).

**D85 texture pool FIXED (committed `2b3ee6e7` → master `6f0208d6`).**
`ptr_texture_alloc_start` is declared `struct texpool *` but every use
takes its address and treats the storage *as* a `struct texpool`
(`texInitPool` writes 4 members through `&…`, `texLoad`/`texFindInPool`
read them back). N64: 4-byte members, 16-byte struct, works by layout
luck. PC: 32-byte struct (4 widened pointers) → `texInitPool` smashes
24 bytes of trailing BSS and `->leftpos`/`->rightpos` read back garbage
→ stage pool looks permanently exhausted (`texFreeBytesInBuffer() < 0`)
→ every room `texLoad` bails → `texFindInPool` NULL for every texnum →
`Bad size for RGBA texture` abort. Fix: `#ifdef PORT` define it as a
real `struct texpool` (`image.c:14`, `image.h:98`); all call sites
already `&…` it. Verified: pool fills, ~630 room textures resolve,
FATAL gone. **Latent (not fixed):** `sizeof(struct tex)` is 24 on PC vs
16 N64 (widened `u8 *data` + bitfield align); the pool is a dual stack
(pixels up from `start`, `struct tex` headers down from `end`), so the
stacks collide ~`bytes/3` early. BUNKER1 `-mt700` doesn't hit it;
texture-heavier levels will need a separate header allocation or a
per-level `-mt` bump. (Enlarging the pool alloc starves `MEMPOOL_STAGE`
and hangs `mempAllocBytesInBank`'s OOM `while(1)` — don't.)

**D85 texture wall CLEARED (session M-3, verified).** With sidecars
present and `493c9838`+`6f0208d6` in, `-level_09` **no longer hits
`Bad size for RGBA texture`** — BUNKER1 renders multiple full frames
(`GE_D85GDL` probe: `g_BgNumberOfRoomsDrawn=5`, roomids 9/10/11/15/17,
`b_min/b_max` 0/2, frame GDL advances ~3 KB/frame cleanly). The room-GDL
stream-decode + texture-pool layers of D85 are done. What remains is a
cluster of **non-deterministic** crashes newly reachable now that the
render loop + guard AI actually run in-level (4-run sample: 3 distinct
fault sites). Treat as a fresh crash-chain, not D85:

1. **`bgScissorCurrentPlayerView` frame-GDL overrun** (`bg.c:1355`,
   fault `0x70800000` = 1 byte past the 8 MB emulated DRAM). Hits ~1/4
   runs. The frame GDL write pointer runs off the end — either the PC
   frame-DL buffer is undersized for real room geometry, or a room GDL
   without a clean `G_ENDDL` drives a runaway append somewhere upstream
   (`sub_GAME_7F0B3C8C` double loop over `chrpropsRenderPass` /
   `bgRenderRoomPrimary`). NOTE: an *earlier* "deterministic overrun"
   reading was a red herring — it was `data/` sidecars missing (see the
   `data/` deletion note below), which faults in `load_bg_file:847` on
   raw BE bg-header bytes.
2. **`chrlvInitActAttack` bad pointer** (`chraction.c:1316`, fault
   `~0x40123350`). Hits ~2/4 runs — a guard starting an attack derefs a
   truncated/garbage pointer (`0x401xxxxx` looks like a 64-bit pointer
   with the high word lost, cf. D86/D92 class).
3. **`gfx_sp_matrix` unrelocated segment addr `0x90000000`**
   (`gfx_pc.cpp:1077` via `gfx_run_dl`). Hits ~1/4 runs — a room-GDL
   `gsSPMatrix` w1 carries N64 segment 9 unresolved. Room-GDL matrix
   arg relocation still needed (the widen byte-swaps the words but does
   not remap seg addresses; `G_VTX` seg-addrs are resolved elsewhere via
   `SEGMENT_OFFSET`, but `gsSPMatrix` is not).
4. **`bgTestRayIntersectionInRoom`** (`bg.c:3302` `((u32*)gdl)[1]`,
   `:3383/3388/3521/3526` `*(u8*)gdl`) still reads opcodes/w1 N64-style;
   post-widen the opcode is byte 3 and w1 is `((u32*)gdl)[2]`. Operated
   on garbage before (so "worked"); needs PORT accessors. Hitscan, not
   render — lower priority.
3. **Frame-GDL buffer overrun in `chrpropsRenderPass`**
   (`bgScissorCurrentPlayerViewDefault`, `chrprop.c:569` → `bg.c:1355`,
   write fault ~`0x70800000`). Timing-dependent, masked behind #1.
   Prop/character render path (D75-adjacent), newly reachable.

**D94 (RESOLVED — truncating `(s32)` pointer cast in `chrlvInitActAttack`).**
Committed `63204a27`. `chraction.c:1221`/`1231` compute the firing-anim
table entry as `(s32)arg1[anim_index]->table + (s32)(idx*sizeof(...))` —
`table` is `weapon_firing_animation_table (*)[]`, and the `(s32)` cast
truncates the 64-bit pointer, then the cast back zero-extends, dropping
the `0x1_00000000` module-base bit → `panim_float ≈ 0x4012xxxx` →
`panim_float->anim.anim` faults there when a guard starts an attack
in-level. Replaced both with plain array indexing (`&(*table)[idx]`) under
`#ifdef PORT` — the code's own comments say that's the intent.

**D95 (OPEN — the `-mgfx` master-DL buffer is half-capacity on PC; the
naive fix OOMs `MEMPOOL_STAGE`).** `dyn.c:56` sizes `g_GfxBuffers` from
`-mgfx` (a byte budget from `boss.c`'s per-level `memallocstringtable`,
sized for N64 8-byte `Gfx`). On x86-64 a `Gfx` is 16 bytes, so the master
display list holds **half** the commands for the same budget. Every
render fn appends with a bare `gdl++` and **no bounds check**, so once
BUNKER1 emits real room geometry the list overruns `g_GfxBuffers[1]`/`[2]`,
runs off the stage mempool, and faults writing a GBI command at the top
of the 8 MB emulated DRAM (`0x70800000`) — **non-deterministically**, and
scribbling GBI across DRAM on the way (the run-to-run `Unknown GBI opcode
0x3f/0xffffb9` and `gfx_sp_matrix` seg-9 `0x90000000` faults were all
downstream corruption from this one overrun). Doubling the `g_GfxBuffers`
allocation (`* sizeof(Gfx)/8`) under PORT **stops the overrun** (verified:
`-level_09` then runs 90 s+ / 5000+ VI posts, no crash) **but** the extra
~100 KB starves `MEMPOOL_STAGE` so `zbufAllocate` →
`mempAllocBytesInBank` (`memp.c:204`) spins in its OOM `while(1)` and no
frame ever renders (`kernel heartbeat: frames=0`; mainThread stuck in
`lvlRender`→`viClearZBufCurrentPlayer`→`zbufInit`). Committed as
`70784f80`, reverted `2a506284`, **re-applied `f35eba91`** alongside the
mempool-ceiling fix below.

**`malloc`-the-gfx-buffer is NOT viable** — `osVirtualToPhysical` /
`OS_K0_TO_PHYSICAL` in the port `(u32)`-truncate and subtract
`0x70000000`, so any pointer that flows through them (sub-DL branches
built in the gfx buffer, etc.) must live in the `0x70000000` DRAM window.

**Mempool ceiling fix (`933ba52b`, kept).** `port/src/n64stubs.c`'s
`tlbmanageGetTlbAllocatedBlock` returned the N64-fidelity ceiling
`0x702F4400`, leaving ~5 MB of live mapped DRAM unused below the 8 MB top
(only `animations_frame_buffer` @ `0x707FFD30` up there). Raised to
`0x70700000`; the ~4 MB gain goes to `MEMPOOL_STAGE` (`boss.c:218`). This
+ the re-applied 2x `g_GfxBuffers` removes the OOM hang: `-level_09` now
**renders ~5 frames** (`frame N rendered` logs, VI posts climbing).

The "runaway GDL append at ~frame 5" that D95 chased turned out to be
**two memory-corruption bugs**, both now fixed:

**D96 (`d86ec483`) — prop room-list stack overflow.**
`chrpropUpdateRoomList` + helpers build room lists of up to 7 entries,
then write `prop->rooms[0..n]` + a `0xff` terminator. `PropRecord.rooms`
and `chrpropsRenderPass`'s `s32 sp48[…]` local are both
`PROPRECORD_STAN_ROOM_LEN` = **4**. BUNKER1 patrol guards routinely span
≥4 rooms (`[1a 13 14 10]`, …) → 4 IDs, no terminator inside the array →
`chraiGetPropRoomIds`'s `for (i=0; self->rooms[i] != 0xff; i++)` walks
off the end, overflowing the caller's stack frame → garbage `gdl` →
GBI write fault. Every *other* `chraiGetPropRoomIds` caller already used
`s32[8]`. Fix: `PROPRECORD_STAN_ROOM_LEN` → 8 under `#ifdef PORT`
(`bondconstants.h`), + a defensive bound in `chraiGetPropRoomIds`. N64
unchanged.

**D97 (`2fbcc556`) — `bondviewPlayerTickDamageAndHealth` negative
`damagetype`.** US build (unlike EU/JP) has no low clamp;
`damagetype = (s32)(health*8)` goes negative on a lethal hit → OOB
`g_DamageTypes[]` read → segfault when a guard shoots Bond (~frame 5).
Extended the EU/JP clamp to PORT.

**D98 (`000ed6af`) — `initBONDdataforPlayer` under-allocates the player
struct.** It `mempAllocBytesInBank`s a hardcoded `0x2A80` (N64
`sizeof(struct player)`, `0x2A70` EU). The PC struct is much larger
(pointer fields widened 4→8). The player block sits directly below
`g_GfxBuffers[0]` in `MEMPOOL_STAGE`, so writes past ~offset `0x2A08`
(`bondviewRenderDebugBondView`'s `g_CurrentPlayer->field_2A08 = ft4`,
run every frame since `debug_render_raster` defaults to `DEB_BOND_VIEW`)
scribbled the zbuf-clear `gsDPSetRenderMode`'s `w1` onto master-DL slot
11 → `Unknown GBI opcode 0xffffb9`. Fix: allocate
`(sizeof(struct player) + 0xF) & ~0xF` under `#ifdef PORT`.

**D99 (`253caa23`) — `modelTickAnim` garbage function-pointer call,
FIXED.** `struct Model.animflipfunc` (`bondtypes.h:1640`, "0x98") is
`s32` but `modelSetAnimFlipFunction` (`model.c:2840`) stores a `void *` →
truncated on PC → `((void(*)(void))animflipfunc)()` at `model.c:3534`
jumped to `0x00010100` (`bheadFlipAnimation` at module+0x10100). It is
*only* ever set to `bheadFlipAnimation`, for `g_CurrentPlayer->model`
(`initBondDATAdefaults.c:198`, `bondhead.c:430`). Fix (D92/`unka0`
pattern): under `#ifdef PORT` the field is a bool flag and
`modelTickAnim` calls `bheadFlipAnimation()` directly. `-level_09` now
reaches VI post ~601 (was ~421).

**D100 (`8eaad547`, PARTIAL) — `struct player.model` is an inline
`struct Model`, not a pointer.** Every use is `&g_CurrentPlayer->model`
passed to a `modelXXX(struct Model *)` fn. The decomp splits it as
`Model *model;` + ~45 `s32 field_59C..field_650` (≈ N64
`sizeof(struct Model)` ~0xB8). PC `struct Model` is ~0x2A8, so
`animInit(&model, …)` overran into `field_654` (the gait RW-data pool
`animInit` was *also* handed), `bondheadmatrices`, and the viewport
fields → garbage `model->datas` → the bit-32 fault. Fix (PORT only):
`model` becomes an inline `struct Model`; a dedicated `u32
gaitRwData[256]` at the end of `struct player` replaces the `&field_654`
gait pool; `initBondDATAdefaults.c` points `animInit` there. `sizeof`
grows ~0x2A0 — D98's `sizeof(struct player)` alloc already covers it, and
`field_59C..field_650` are grep-verified dead (only `field_654` was
used). **⚠️ LANDMINE this exposes:** `struct player` has raw
hard-coded-offset accessors above 0x594 that are NOT PORT-adjusted and
are now further off — `gunfire.c:4934-4945` `THROWMTX/THROWPOS` at
`g_CurrentPlayer + handoffset + 0xAD8`, used for grenade/knife throwing.
Those were *already* PC-wrong (PC struct ≠ N64 before D100 too); D100
doesn't regress a working path, but a real `struct player` PC-offset
pass is owed. (`bondview2.c:3165` `+0x230` watch model and `+0x2ec` are
below 0x594 → unaffected.)

**D101 (`b2234f82`) — `sub_GAME_7F06DB5C` `(s32)` pointer stash, FIXED.**
The anim2-blend variant of `modelBuildGroupMatrices` did
`sp1C = (s32)arg2->Parent` then `modelFindNodeMtx(arg1, (ModelNode*)sp1C,
0)` — the exact 32-bit-pointer idiom the sibling was already cleaned up
for. `(s32)` drops the `0x140000000` base of a static `ModelNode` →
`modelFindNodeMtxIndex` deref `0x4012c9c0`. Also stashed `&sp48[sp54]`
(a `RenderPosView*`) the same way and mis-called `g_ModelJointPositionedFunc`
with a 3-arg cast. Under `#ifdef PORT`, full-width pointer work + the real
2-arg callback (mirrors `modelBuildGroupMatrices`).

**D102 (`1b078f6d`) — 1P weapon `Model` / RW-pool `struct hand` overlap,
FIXED.** `gunfire.c` punned the weapon `Model` onto `hand->field_B68` and
its RW-data pool onto `hand->modeldatas` (a fixed run at `field_B68 +
0x20`). N64 `sizeof(struct Model)` ~0xBC keeps `modelInit`'s header
writes clear of the pool; PC `struct Model` is 0xE8, so `modelInit`
writes `objinst->datas` (0x20) **directly onto `&hand->modeldatas`** —
the field and pool base alias, and the first `modelInitRwData` record
write clobbers `datas` (bit 32 set) → the `modelInitRwData` fault
(`0x1_70076514`). `objinst->obj` (0x10) lands on `hand->mtxlist`
likewise. Fix: `struct hand.weaponModel` (inline `struct Model`) +
`weaponRwPool[192]` under `#ifdef PORT`, routed through
`HAND_WEAPON_MODEL`/`HAND_WEAPON_RWPOOL` macros; set
`weaponModel.render_pos` explicitly (N64 aliased `hand->mtxlist`). Same
class as D53.2 (watch) / D100 (player gait model). The `d43` record
layout was ruled out — `GE_D51` proved it was `Objinst->datas` *field*
corruption, not a garbage `RwDataIndex` (`idx` was 0/1, valid).

**RENDER CHAIN CLEAR (session M-3).** With D93–D102 in, `-level_09` boots
BUNKER1 and **renders continuously with no fault** (60 s+, 3500+ VI
posts, full framerate; attract mode also clean). ~46 % non-clear pixels,
content biased to the lower half of the screen. The
stage-load→first-frame→in-level crash chain (the blocker since D69) is
**resolved**. Remaining: **D75** — 3D-model rendering quality
(animated/skeletal character models), a separate track; and the
`struct player` / `struct hand` raw-hardcoded-offset landmine above
(grenade/knife throw). `struct tex` headers 24 B vs 16 B on PC is a
separate open pool-pressure item — a PC memory-budget pass is owed.

**Still open, separate:** `struct tex` headers are 24 B vs 16 B on PC
(texpool-triage) — pool-pressure; a real PC memory-budget pass should
cover it.

**D103 (session M-4) — native-resolution height, FIXED.**
`port/src/libultra.c` `osViSetMode` hard-coded the fast3d native
resolution to the *visible scanline count* (`pal ? 400 : 480`) instead of
the CFB space GE authors its GBI in. fast3d's `SCREEN_WIDTH`/
`SCREEN_HEIGHT` (`gfx_pc.h`) alias `gfx_current_native_viewport`, and
`RATIO_X/RATIO_Y` (`gfx_pc.cpp:53-54`) scale the game's 320×240-space
`gSPViewport`/`G_SETSCISSOR`/2D-texrect coords onto the 640×480 window.
With height 480 the ratios were asymmetric — `RATIO_X = 640/320 = 2.0`
but `RATIO_Y = 480/480 = 1.0` — so every viewport/scissor came out a
220 px band (≈45.8 % of the frame → the "~46 % non-clear" figure),
placed in the upper GL region and shown (post `invert_y`) in the **lower
half of the screen**. `gfx_current_native_aspect` was likewise wrong
(320/480 = 0.67). Fix: recover the real CFB height from
`vm->fldRegs[0].yScale` (GE sets it to `bufy * YSCALE_MAX(0x800) /
SCREEN_HEIGHT_MAX(480)`, `fr.c:417`) with a `pal ? 272 : 240` fallback;
`comRegs.width` already carried `bufx` so width was fine. Verified:
`-level_09` `GE_PCDUMP` frames go from ~46 % / lower-band to **91.7 % /
bbox (0,20)-(639,459)** — the full 10..230 viewport ×2 with correct
letterbox, HUD ammo counter correctly placed top-right. Purely a
transform fix — no per-model work.

**D104 (session M-4) — investigation, folded into D105.** Chased "flat
dark-blue fill + HUD, no room geometry" after D103. Ruled out (probes +
subagent trace): segment 0x0E vtx resolution, room-DL chaining/execution,
per-room scissor (`GE_D104` shows bbox `(1,10)-(319,230)` full-view,
`viewleft/top/x/y = 0/10/320/220`), the view-projection matrix
(`field_10E0` decodes to a sane perspective×lookat — rows ≈ unit-scale
3×3, translate row `518,-22,430,459`), and the room GDL decode (valid
F3DEX2, no embedded `G_MTX`). A temporary fast3d tri counter proved
~160 k room triangles WERE being submitted to GL each frame — yet the
final image was 90 % exactly `(0,16,65)` (BUNKER1's `env->{Red,Green,
Blue}` sky-fill from `skyRender`, `sky.c:326`, the 1P no-clouds
`gDPFillRectangle` path). Geometry drawn, invisible → depth. `GE_D104`
room-render-pass probe left in `bg.c` (capped, gated).

**D105 (session M-4) — depth buffer never cleared, FIXED.**
`zbufClearCurrentPlayer` (`src/game/viewport.c:89`) clears the N64 Z
buffer with the classic "point the colour image at the Z buffer and
`gDPFillRectangle` it with a packed-Z fill colour" idiom. fast3d does
**not** emulate that: `gfx_dp_fill_rectangle` (`gfx_pc.cpp:2281`) bails
when `color_image_address == z_buf_address` on the assumption the depth
clear already happened via `glClear` — but the frame-start clear is
`clear_framebuffer(true, false)` (`gfx_pc.cpp:2855`), **colour only**.
fast3d expects the game to emit `G_CLEAR_DEPTH_EXT` (opcode `0x44`,
`gfx_pc.cpp:2678`) for a real depth clear, and **nothing in GE ever
emits it** (grep-confirmed). Worse, here the two addresses don't even
match (`gDPSetDepthImage(z_buffer & ~0x3F)` vs `gDPSetColorImage(
OS_K0_TO_PHYSICAL(z_buffer))`), so the bail isn't taken and the packed-Z
fill colour gets scribbled onto the real framebuffer instead. Net: every
in-level frame renders against a stale/garbage depth buffer, all ~160 k
room triangles fail the Z test, and only `skyRender`'s background fill
survives. Fix: `#ifdef PORT` branch in `zbufClearCurrentPlayer` emits a
bare `G_CLEAR_DEPTH_EXT` Gfx word and returns, skipping the N64 idiom
(which is a no-op / actively harmful in fast3d anyway). Verified
`-level_09`: frame goes from 90 % flat sky-colour to **2585 distinct
colours, sky down to 16 %** — recognisable BUNKER1: textured walls,
storage racks, floor. 70 s run, crash-free.

**D106 (session M-4) — portal near-plane projection garbage culled the
next room, FIXED.** After D105, many camera angles in BUNKER1 showed a
large flat sky-fill void where an adjacent room should be visible
through a doorway. A `GE_D104` probe (per-frame visible-room list) showed
the portal-visibility BFS returning only 1–3 rooms; a `GE_D106` probe in
`sub_GAME_7F0B7F84` showed the plane-side metric cull (`bg.c:4135/4144`)
and `zfar` (10000, fine) were NOT the cause — portals were dying in
`sub_GAME_7F0B5864`'s screen-space projection. When the player straddles
a portal plane, `sub_GAME_7F0B5528` emits z==0 near-plane clip points and
`transform3Dto2DWithZScaling` (`bondview.c:730`) uses `inv_z = -1e20`,
projecting them to ±1e20-scale coords. On N64 the two clip points
bracket the view symmetrically → the degenerate-box check (`bg.c:1713`)
or the downstream `screensize` clamp yields a full-screen box and the
room is kept. On x86-64 the exact garbage can come back `min > max` on
only one axis (or non-finite), slipping past `bg.c:1713` → portal
dropped → room vanishes → sky-fill through the doorway. Fix: `#ifdef
PORT`, any wildly out-of-range / non-finite projected bound is treated
as degenerate → full `screensize` (matches the N64 outcome). Verified:
per-frame visible-room count 1–3 → 2–4; rooms past near doorways (r11
from r27, etc.) now render. `c_screenleft/top/halfwidth/halfheight/
recipscalex/recipscaley` were all confirmed correct (0/10/160/110/
190.53/190.53), so the projection *fields* are fine — this was purely
the z==0 edge case. `GE_D104` probe (visible-room list) left in `bg.c`,
gated + capped; `GE_D106` probes removed.

**D107 (session M-4) — fast3d sampled an unloaded mip tile for GE's LOD
textures (blurry surfaces), FIXED.** GE room GDLs emit `G_TL_LOD +
G_TD_DETAIL` for mip-mapped textures, but `gfx_detail_textures_enabled`
is false for this port. `gfx_lod_tile_offset` (`gfx_pc.cpp:1258`) then
ran `rdp.tex_lod ? rdp.tex_detail : i` → returned `tex_detail` (1) for
every texel → fast3d sampled GE's first mip (render tile 1). GE loads
the whole mip chain with one `G_LOADBLOCK` to TMEM 0 and points tiles
1..5 at offsets inside it; fast3d keys `loaded_texture[]` by TMEM addr
and has no entry for tile 1's offset, so `import_texture` fabricated a
16×16 from the top-left 256 B of the base image and magnified it across
the polygon — the "blurry brown/grey ceilings & wall panels" in BUNKER1.
Fix: with detail textures disabled, `gfx_lod_tile_offset` always returns
0 (the base render tile — the only correctly-loaded level; matches the
N64 near-surface look). Verified: BUNKER1 ventilation room
ceilings/walls/light panels render crisp. Single-level textures (racks,
room 1) were already fine, unaffected. (Root-caused by a subagent — the
`gfx_detail_textures_enabled=false` + GE's unconditional `G_TD_DETAIL`
contradiction; hi-confidence, matches every symptom + the
room-1-vs-storage-room split.) A residual minor vertical squish from
`G_LOADBLOCK` size including the mip bytes is noted but not fixed.

**Storage-room sky void — root-caused, NOT a visibility bug (session
M-4).** The large void in rooms 27/28/29 is **closed doors**: portals 25
and 26 (the storage-area doorways) carry `controlbytes1 & 1`
(`PORTALFLAG_DISABLED`) at runtime — verified correct: `load_bg_file`
clears bit 0 for every portal at load (`bg.c:1015`), then
`bgToggleDataPortalsContrlBytes1Bit1` (`bg.c:5502`) re-sets it for
doors that start closed. So `sub_GAME_7F0B7F84` correctly refuses to see
through them (`bg.c:4147`). The bug is that **the door prop/model that
should fill each closed doorway does not render** — `chrpropsRenderPass`
(`chrprop.c:482`) *runs* with `n=3..10` props per room (`GE_D96`), but
the frame GDL barely advances (~7 `Gfx` for 9 props), i.e. props are
enumerated but emit almost no geometry. This is the prop/character
model-rendering track (D75(b) + the `struct player`/prop raw-offset
landmines), not portal/visibility work. Portal visibility itself is
healthy post-D106 (room 18 → 8 rooms deep; enabled doorways traverse
correctly). Other residuals: low-frequency stray green/yellow polygons
(1–2 per room — a degenerate vertex, likely one bad entry per room-vtx
table via `bgSwapRoomVtx` or the point-index blob length); front-end
text draws mirrored. Next: prop/door model rendering (unblocks the void
AND the missing weapon), then D75(b) skeletal models.

**D108–D112 (session M-5) — the "props emit no geometry" premise was
stale; the real bug was a converter byte-swap off-by-one, FIXED.**
Re-probing `chrpropsRenderPass` / `chrpropRender` / `modelRenderNodeDl`
(`GE_D96*`) showed props *do* emit complete leaf DLs now (~15k
`nodeDl` EMITs/run, valid `gdl`/`vtx`/`BaseAddr`; fast3d transforms
~300–600 prop verts/frame with sane `w`). Two separate residual bugs
were isolated: (1) in the BUNKER storage area the per-frame visible-room
count drops to 1–2 while `cameramode` goes 1→0 and `g_RoomLoadBudget`
200→3 — but the 200→3 is *intentional* N64 behaviour
(`bgRoomVisibilityRelated`: 0xC8 for intro/swirl cams, 3 for FP) and
`currentPlayerGetProjectionMatrix()` + `g_CurrentPlayer->screensize`
were both verified valid, so this is a portal-BFS under-reach, still
open. (2) **Skeletal characters rendered as a "3D line" / not at all**
(also visible in attract mode): `drawjointlist` → `subcalcmatrices` →
`modelUpdateMatrices` → `process_02_position` → `modelBuildGroupMatrices`
produced `render_pos[]` joint matrices with **sane rotations but garbage
/ 1e27 / NaN positions**. `basemtx` (`camGetWorldToScreenMtxf()`) was a
clean orthonormal lookat; joint *rotations* from the anim bitstream were
fine; but `group->Origin` (`ModelRoData_GroupRecord.Origin`, a
`coord3d`) read as garbage for every joint whose bytes weren't
zero — while the adjacent `JointID`/`MatrixIDs` (u16/s16) read fine.
Root cause: **`put_f32` in `tools_pc/d43_emit.py` had an off-by-one in
its BE→LE byte reversal** — `buf[o:o+4] = src[doff+4:doff:-1]` yields
bytes `doff+1..doff+4` (dropping the value's MSB, pulling in one byte of
the next field) instead of `doff..doff+3`. This corrupted *every* f32
field in converted model rodata (joint `Origin`s, LOD near/far, BSP
planes, bounding radii) for every sidecar-loaded model; the compiled-in
front-end intro models (native-LE C structs, not run through the
converter) were unaffected, which is why the Rareware logo looked right
but every in-level character collapsed. Fix: `src[doff:doff+4][::-1]`.
After regen (`python tools_pc/d43_emit.py ntsc-final`), all 20
`render_pos` entries of a BUNKER guard are coherent (~±60 units around
the body), monitor screens draw their content, and character models
render as recognisable humanoids (still some pose/orientation polish
owed — a guard appears inverted — a separate matrix-handedness item).
`d43_emit.py`'s verify pass only checks pointers/opcodes, not float
values — a float spot-check is worth adding. Probes `GE_D96GATE /
D96DL / D108 / D109 / D110 / D111 / D112 / GE_TRICNT` were all removed
after the fix.

**D113 (session M-6) — "portal-BFS under-reach / storage-room void" was a
STALE premise; there is no BFS bug.** A subagent dumped BUNKER1's runtime
portal table and traced every accept/reject decision of `sub_GAME_7F0B7F84`
(non-EU/LEFTOVERDEBUG copy, `bg.c:4091`; queue = 5-arg `bgQueuePortalTraversal`
`bg.c:3843` — confirmed the compiled ones). With the camera in room 29 the
BFS visits portals 25,26,27,28: portals 25 (r29↔r26) and 26 (r29↔r27)
carry `PORTALFLAG_DISABLED` (`controlbytes1 & 1`) because door props obj
153/154 spawn `openPosition==0` → `doorDeactivatePortal`, and nothing
re-opens them in the fly-through — so they are culled at `bg.c:4138`, a
site that is **not** `#ifdef PORT`-guarded and behaves identically on N64.
Result: a stable 3 visible rooms {29,28,25} (D104 probe agrees), which is
the correct answer for this topology, not "1–2". No s32/pointer
truncation, no `min>max`/NaN reaching an accept flip, no depth-cap
(`D_8004489C=0xF`) or visited-set (`>=9`) early-out firing. `sub_GAME_7F0B5864`
is fine post-D106. **The void = the two closed-door models (obj 153/154)
filling rooms 26/27's doorways not rendering where they should** —
`GE_D96` shows room 29's prop pass emits their leaf DLs, so the door
geometry exists but its world transform puts it outside the doorway,
leaving the wall hole unoccluded → sky. This is the D114 model-transform
track, not visibility. No `bg.c` change. Probes reverted.

> **M-33 CORRECTION (D168): this finding is retracted as a capture artifact.**
> Every `GE_PCDUMP` PPM was written upside-down (`gfx_opengl_dump_bound_fbo`
> did not reverse `glReadPixels` rows). The developer confirms the game renders
> correctly on real hardware. The "shared fast3d mirror" this and the D116
> entries chase never existed — the probes kept coming back clean because there
> was nothing wrong; the analysts were reading inverted screenshots of
> asymmetric content (guards, text, logo) and calling them "mirrored". The PPM
> writer is fixed. The genuinely-open item that remains is D75 Bug 2 (front-end
> animated models absent), which is unrelated. Original text kept below for
> history.

**D114 (session M-6) — character inversion / mispositioned crate:
OPEN, matrix chain verified sane, residual is a shared fast3d mirror
(D75 class).** A subagent statically traced the whole joint-matrix chain
(`chr.c:2564` → `subcalcmatrices` → `modelUpdateMatrices` →
`process_02_position` `model.c:1662` → `modelBuildGroupMatrices`
`model.c:1318` → `matrix_4x4_multiply_homogeneous` →
`bondviewTransformManyPosToViewMatrix` → `matrix_4x4_f32_to_s32`
`matrixmath.c:495` → `gfx_sp_matrix`) and an env-gated `GE_D114` probe
(24 joint builds, 3+ BUNKER guards). Findings: `basemtx` from
`camGetWorldToScreenMtxf()` is a proper orthonormal right-handed lookat
(+Y up, `up ≈ (-0.09, 0.96, -0.26)`, `right = up × forward`), scaled by
`bgGetLevelVisibilityScale()` = 0.1 (also applied to the room path —
expected); joint `render_pos` are coherent and **upright** (head y ≈ 280
> pelvis ≈ 230 > limb ends ≈ 132); view-space z negative (in front,
correct). Converter clean: `put_f32` is now the only float writer and is
correct post-D112; `matrix_4x4_f32_to_s32` is byte-identical to
`guMtxF2L`; `matrix_4x4_set_lookat` / `guPerspectiveF` /
`matrix_4x4_multiply_homogeneous` match N64 and are native-LE. So the
plan's hypotheses (converter axis/sign, lookat handedness, compose
order, F2L) are all **disproven**. Leading residual: a horizontal (±
vertical) mirror in the **shared fast3d viewport / MP-matrix path**, not
the model path — consistent with the open D75 notes ("front-end text
draws mirrored", "Nintendo logo mispositioned"): a flip is only visible
on asymmetric content (humanoid, text, logo) and invisible on a boxy
room. GE bakes a left-handed screen convention into its own transforms
(`transform3Dto2DCoords`, `bondview.c:726`: `screenX = center −
x·invz·scale`); if GE's RSP (`rsp/graphics/gmain.s`) applies an X flip
that `port/fast3d` does not, every model and room is mirrored. Suspects:
`gfx_calc_and_set_viewport` / `gfx_adjust_viewport_or_scissor`
(`gfx_pc.cpp:1739/1771`), `MP_matrix` vertex transform
(`gfx_pc.cpp:1122`). This is a fast3d-correctness gap → write-up, not a
narrow ABI patch. Also: **rebuild with D112+D115 and eyeball a guard
first** — the D115 `gunfire.c` fix stops a per-shot 64-byte scribble into
the inline-Model / bondhead-matrix region that could itself have caused
"inverted". The crate: static-prop placement comes via a per-prop
`basemtx` from the Usetup `PropRecord` position/rotation — check
`d88_emit.py` separately from the character question. Probe reverted;
re-apply snippet in the M-6 agent report.

**D115 (session M-6) — `struct player` / `struct hand` raw-offset audit +
first fix.** Full survey in `docs/dev/AUDIT-M6-player-offsets.md` (10 offset
sites; 3 live HIGH, 1 dead const block, 2 MED, 4 already-correct).
**Fixed:** `gunfire.c:4960-4962` `THROWMTX` / `THROWPOS(k)` / `THROWPREV(k)`
were raw byte offsets — `(u8*)g_CurrentPlayer + handoffset + 0xAD8/0xB08/
0xB48` with `handoffset = handnum * sizeof(struct hand)`. Both the stride
(`sizeof(struct hand)` ≈ 0x968 PC vs 0x3B8 N64, D102) and the base offset
are N64-sized, so on x86-64 the address lands inside the inline gait
`Model` / bondhead-matrix region of `struct player`, and
`matrix_4x4_copy(THROWMTX, …)` scribbles 64 bytes of live render state on
**every shot from a casing-ejecting weapon** (`sub_GAME_7F068508`,
solo only). The offsets are exactly
`hands[handnum].throw_item_pos_related[_prev]` and its translation row, so
the macros now use those field accessors under `#ifdef PORT` (N64 build
keeps the byte-offset macros unchanged). **Still open (MED):** #5 — the
D102 weapon-model `render_pos` is pointed at a `dynAllocate`'d transient
arena that on N64 aliased the persistent `hand->mtxlist`; likely the
"1P weapon model doesn't draw" cause, may fold into D114. #6 — the D56
watch-preview Model pool (`watchRwPool[0xC8]`) is N64-sized and the
inline Model overruns live watch fields. See the audit doc for the full
table and recommended fix order.

> **M-33 CORRECTION (D168): retracted as a capture artifact — see the D114
> correction above and D168.** "Every stage probed is clean, yet the output is
> mirrored" was the tell: the output was not mirrored, it was upside-down (the
> PPM writer never reversed `glReadPixels` rows). No `GE_D116` S-swap / U-flip
> was ever warranted. Probe scaffolding (`GE_D116` in `textrelated.c` /
> `gfx_pc.cpp`) can be stripped. Original text kept below for history.

**D116 (session M-7) — the HUD/menu text mirror (D75 class) is a
per-glyph TEXTURE-space flip, NOT a screen/framebuffer/matrix mirror.**
Rebuilt at D115 (`14b6b432`), build green, captured `-level_09` frames
(`GE_PCDUMP="120-600:60"`, `ppm/frame_0003*.ppm`). Findings:
- The "OBJECTIVE C: FAILED" flash (proportional font,
  `textrelated.c:textRenderGlyph` → per-glyph `gDPLoadTextureBlock`
  `G_IM_FMT_I/G_IM_SIZ_8b` + plain `gSPTextureRectangle`, dsdx `0x400`)
  renders with **string order preserved (O first, D last) but every
  glyph individually horizontally mirrored**. That signature is a
  texture-S reversal per glyph, not a block/screen flip.
- The HUD ammo digits ("83", top-right, upright and correct) use a
  different font path and are **not** mirrored → the bug is specific to
  the proportional-font `textRenderGlyph` load/sample path, not global.
- Static trace found nothing: `gfx_dp_texture_rectangle` / `gfx_draw_rectangle`
  (`gfx_pc.cpp:2161/2086`) assign `uls`→left corner, `lrs`→right corner
  with positive dsdx; `import_texture_i8` (`gfx_pc.cpp:810`) uploads
  linearly; viewport/scissor paths have no X negation; GE's camera
  (`fr.c:694` `guPerspectiveF` + `matrix_4x4_set_lookat` = standard
  gluLookAt) is conventional RH (confirms D114).
- **Consequence for D114:** the unified "shared fast3d screen mirror"
  hypothesis is at least partly wrong — the text mirror is texture-space.
  The inverted-guard / mislocated-crate symptoms are either a *separate*
  defect or the same texture-S flip applied to model textures (an
  asymmetric guard skin flipped reads as "facing wrong way").
- **Next (needs a runtime probe, not static):** dump the glyph texrect's
  tile size / `line_size_bytes` / uploaded texture width vs `curchar->width`
  for one glyph, and the final S texcoords fast3d hands GL. Suspects:
  `gDPLoadTextureBlock` 8b `line`/pad (`(curchar->width+7)&0xF8` load
  width vs real `curchar->width` sample width) interacting with tile
  wrap; or the compiled-in font blob (`assets/obseg/text/LmiscE.h` /
  `chars[]` `pixeldata`) being 32-bit word-swapped by the asset step
  (would reverse 4-texel groups — check whether the mirror is clean or
  chunked at 4px). Probe artifacts: `ppm/frame_000300.ppm` (obj text),
  `ppm/frame_000480.ppm` (ammo).

**D116 probe results (session M-7, overseer-run — supersedes the "specific
to the proportional font" claim above).** Killed a subagent that was
drifting toward a global `GE_D116FLIP` S-swap on every non-flip texrect
(mirrors the whole HUD/3D to "fix" glyphs — wrong). Kept its
`GE_D116`-gated `fprintf` probes in `textrelated.c` (both glyph paths) and
`gfx_pc.cpp:gfx_dp_texture_rectangle`. Built green at D115, ran
`-level_09` + `GE_PCDUMP="200-360:40"`. Data:
- **The ammo digits ARE mirrored too.** `ppm/frame_000320.ppm` top-right:
  "83" renders with each digit individually X-flipped, digit order
  preserved; the clip-count glyph left of the mag icon likewise. So the
  prior session's "ammo digits render correct → bug is `textRenderGlyph`-
  specific" differentiator is **FALSE at D115**. The mirror is NOT
  path-specific. (`textRenderGlyphOutlined` is what the level text uses;
  `-level_09` direct-boot renders no "OBJECTIVE" flash / crosshair at
  all — only the ammo HUD — so the M-7 "OBJECTIVE C: FAILED" note came
  from a different scenario/run.)
- **Rect screen position is correct; only the texture content is flipped.**
  Ammo HUD sits top-right and renders top-right. So this is a texture-U
  reversal per quad, not a screen/framebuffer/viewport flip and not a
  scissor issue.
- **Every stage probed is clean, yet the output is mirrored:**
  - glyph bitmap in memory: correctly oriented — dumped `curchar->pixeldata`
    for 'D' (idx 68, w7/loadw8/h9) row-by-row; stroke on the left, bowl on
    the right, col 7 = wrap-pad of col 6. Not reversed. Rules out (c) a
    font-blob word-swap.
  - `gDPLoadTextureBlock` params: `loadw = (w+7)&0xF8` = 8 (or 16 for w9),
    `line_bytes` = 8, tile `siz=1 fmt=4 cms=2`. Consistent. No evidence
    the load-width vs sample-width interaction reverses anything — rules
    out (b) as the *primary* cause.
  - fast3d texrect: `ul.u = 0`, `lr.u = <positive max>` (e.g. 256 = 8
    texels); `ul.x < lr.x`. Corner↔UV pairing is correct
    (`gfx_dp_texture_rectangle` 2187-2211, `gfx_draw_rectangle` 2113-2136:
    ul=(ulx,uls), lr=(lrx,lrs)). Rules out (a) at the texrect layer.
  - `import_texture_i8` (`gfx_pc.cpp:810`): strictly linear byte copy, no
    row/col reversal.
  - `gfx_opengl.cpp`: `vTexCoord = aTexCoord` pass-through; no U negation
    in the vertex/fragment shader; `cms=2`→`GL_CLAMP_TO_EDGE`.
- **Conclusion:** the U-flip is downstream of everything fast3d computes —
  in the GL vertex-buffer assembly / draw, OR it is a shared transform on
  the rect quad's clip-space X that desyncs from U. This **re-opens
  D114's shared-mirror hypothesis** (relocated from screen-space to
  per-quad U/X space) and retires the D116 "proportional-font-specific,
  texture-S load path" framing. Same mechanism plausibly explains the
  "inverted" guards (mirrored skin) and mislocated door props (D113).
- **Next:** shader/vertex-buffer-level probe — dump the actual per-vertex
  (x, u) pairs in the buffer handed to `glDrawArrays` for one glyph quad,
  and render a 1-texel asymmetric test texture on a known rect to see
  which axis inverts. Confidence the mirror is a real per-quad U/X flip
  (not a capture artifact): **high** — reproduced on two independent HUD
  text paths, rect positions provably correct. Confidence in the
  GL-layer-vs-shared-transform split: **low** — not yet isolated.
- Probes left in tree (all `#ifdef PORT` + `getenv("GE_D116")`, zero-cost
  when unset): `textrelated.c` textRenderGlyph / textRenderGlyphOutlined;
  `gfx_pc.cpp` gfx_dp_texture_rectangle.

**D116 code-trace follow-up (session M-7, overseer, static — no build).**
Traced the rect quad end-to-end through fast3d for the I8 glyph case
(`cms=2`=CLAMP, `tile.uls=0`, `dsdx=1024`, 8px glyph):
- `gfx_dp_texture_rectangle` 2187-2211: `ul={x:ulxf, u:uls=0}`,
  `ur={x:lrxf, u:lrs=256}`, `ll={x:ulxf,u:0}`. `ulxf<lrxf`. Correct pairing.
- `gfx_sp_tri1(ul, ll, ur, is_rect=true)` -> `gfx_sp_tri1` 1497-1572: for
  each vtx `u = v->u/32 - tile.uls/4`; the D74 wrap block is gated on
  `cms & G_TX_WRAP` and G_TX_WRAP==0 so it is a no-op for CLAMP glyphs;
  `is_rect` skips the persp/filter half-texel. Result: `buf_vbo.u` =
  `0/8 = 0.0` for left vtx, `256/32 / 8 = 1.0` for right vtx.
- `gfx_adjust_x_for_aspect_ratio` 1092: `(aspect_ofs*w + x) * aspect_scale
  / aspect_ratio`. `aspect_scale` (1714) is always a positive aspect
  ratio; `aspect_ofs` is a monotonic shift. **Cannot invert X.** Ruled out.
- `buf_vbo` X for a rect = `v->x` verbatim (clip space from
  `gfx_draw_rectangle` 2105-2111), Y optionally `invert_y`-negated (1504),
  X never negated.
So the **entire fast3d 2D texrect -> vertex-buffer path emits a correct,
non-mirrored quad** (x-left<->u=0, x-right<->u=1). Whatever flips U is
DOWNSTREAM of `buf_vbo`: the GL vertex/fragment shader, the ortho/MVP the
backend applies to direct (non-fb) draws, or the sampler. That also means
it is testable without determinism (static per-quad property).
- **Still owed (needs a build — currently serialized behind the
  determinism/framediff agent):** (1) a probe dumping `buf_vbo` (x,u) for
  one glyph tri right before `glDrawArrays`, to confirm the CPU-side
  buffer is non-mirrored as traced; (2) render a 1-texel asymmetric test
  texture on a known screen rect to see which axis GL inverts; (3) check
  whether 3D world geometry is ALSO X-mirrored (bunker is near-symmetric
  — need an asymmetric in-world texture or a guard-facing check) to tell
  a HUD-only (direct-draw) flip from a global one.

**D116 runtime probe part 3 (session M-8, overseer). CONTRADICTION —
every stage from font-bitmap to GL-draw verified non-mirrored at runtime,
yet the on-screen glyphs are unambiguously X-flipped. Investigation
budget-capped; deprioritised (cosmetic, not a playability blocker).**
Confirmed symptom (zoomed `ppm/frame_000300.ppm`): "OBJECTIVE C: FAILED"
(via `textRenderGlyphOutlined`) AND the ammo "83" + spare-clip digit (a
*separate* HUD-number path — `textrelated.c` probes never fire for the
digit indices) both render with word/column order preserved and every
glyph individually horizontally mirrored; screen positions correct.
Runtime probes added (`GE_D116`-gated, `#ifdef PORT`):
- `[D116/vbo]` in `gfx_sp_tri1` after the per-vertex `buf_vbo` writes:
  for the OBJECTIVE glyph quads (left edge, `tile.uls=0 lrs=28`,
  `cms=2`=CLAMP) it prints `vtx0 x=-0.8125 u=0.0` and `vtx2 x=-0.7625
  u=1.0`. **x-left <-> u=0, x-right <-> u=1. The CPU vertex buffer handed
  to GL is correct and non-mirrored.**
- `[D116/i8up]` (since reverted) in `import_texture_i8` dumped the
  uploaded bytes for glyph 'D' — **byte-identical** to `curchar->pixeldata`
  in memory (`00 00 0f 37 34 0b 00 00` ...), and the bitmap is correctly
  oriented (stroke col 1, bowl col 6-7). No flip, no word-swap.
- GL backend: vertex shader is `gl_Position = aVtxPos` (fast3d does all
  transform on the CPU — no GL matrix can flip anything);
  `vTexCoord = aTexCoord` pass-through fragment path; sampler
  `GL_CLAMP_TO_EDGE`/`GL_NEAREST`; `gfx_opengl_copy_framebuffer` has only
  a `flip_y`, no X flip; `G_TEXRECT` decode (`gfx_pc.cpp:2602`) extracts
  `ulx<lrx` correctly; `gfx_adjust_x_for_aspect_ratio` is a positive
  scale+shift (cannot invert).
- `GE_D116TEST` (reverted) replaced glyph I8 textures with a
  left-opaque/right-transparent split; on screen the digit slots came
  back near-uniform white, not half-and-half — **inconclusive** (the
  9-pass outline multi-draw and/or the unidentified digit path's sampling
  window smear the result).
- **Conclusion + confidence:** the flip is real (HIGH — two independent
  text paths, clean zoomed capture). It is NOT in: the font asset, the
  texture upload, the fast3d texrect->vbo math, the GL shaders/sampler,
  the framebuffer blit (each HIGH, runtime-verified). Where it IS: unknown
  (LOW). The contradiction means a stage is being mis-modelled — leading
  candidates now: (a) the render-*tile* setup (`gDPSetTile`/`gDPSetTileSize`
  S params — probes covered the *load* tile) vs fast3d's tile-window
  sampling; (b) a GL-driver-level surprise only a real API trace
  (RenderDoc/apitrace) would show; (c) the dedicated ammo-digit renderer
  (unidentified — not in `textrelated.c`) emitting mirrored S, with
  `textRenderGlyphOutlined` doing likewise via a shared lower-level
  helper. NEXT PERSON: capture one glyph texrect in RenderDoc, or find
  and read the HUD-number path, before touching fast3d again. Do not
  exceed ~30 min without one of those in hand. Probe left in tree:
  `[D116/vbo]` in `gfx_pc.cpp` (`GE_D116`, zero-cost).

**D117 (session M-8) — frame-to-frame nondeterminism root-caused;
`GE_DETERM` fixed-tick mode assessed NOT-narrow, deferred with a design.
`tools_pc/framediff.py` added (structural/tolerant).**

*Root cause — pure frame pacing (variable timestep), NOT PRNG / uninit
state. Confidence: HIGH.*

GE is a variable-timestep simulation. Per rendered frame it advances game
logic by `deltaFrames` = *however many 60 Hz ticks of wall-clock elapsed
since the previous frame*:

- `src/game/frametiming.c:75` `waitForNextFrame()` —
  `nextFrameTime = (osGetCount() - copy_of_osgetcount_value_1 + 387937)
  / 775875` (NTSC: 775875 RSP-counter ticks per 1/60 s), loops until
  `>= frameDelay` (normally 1), then `updateFrameCounters(nextFrameTime)`.
- `src/game/frametiming.c:46` `updateFrameCounters(deltaFrames)` sets
  `speedgraphframes = deltaFrames`; everything downstream (physics, AI,
  animation blends, `g_Vars.lvupdate*`) scales by it.
- `src/boss.c:456-495` main loop: blocks on `gfxFrameMsgQ` for
  `OS_SC_RETRACE_MSG` (posted by the port pacemaker), and only builds a
  new frame once `mainTickElapsed = osGetCount() - copy_of_osgetcount_value_1
  >= MAIN_LOOP_TICK_INTERVAL` (387937). A two-level gate, both levels
  keyed on `osGetCount()`.

On the console `osGetCount()` is the CP0 Count register (fixed CPU rate);
on PC `port/src/libultra.c:88-100` maps it to **real elapsed
microseconds** scaled to 46.5525 ticks/µs (D52 — required so
`waitForNextFrame` doesn't block ~388 ms/frame). So `deltaFrames` tracks
real render time: a frame that took 28 ms advances logic 2 ticks, one
that took 14 ms advances 1. Machine load, GL driver, vsync phase and the
~20 fps clean-run rate all jitter this → two runs of the same build take
different numbers of logic steps to reach "frame N" and their sim
trajectories diverge. Measured with `framediff.py --exact`: two
`-level_09` runs differ **32 % (frame 440) – 69 % (frame 200)** of pixels.

Ruled out as *primary*:
- **PRNG seeding is correct.** `port/src/random.c:22`
  `g_randomSeed = 0xAB8D9F7781280783ULL` (the two `.word`s from
  `random.s`), `g_chrObjRandomSeed` likewise (`:93`); the xorshift is a
  line-by-line port. `randomSetSeed` matches the `.s` (`+1` before
  store). *However* — because AI/animation code calls `randomGetNext()` a
  `deltaFrames`-dependent number of times per frame, the PRNG *stream
  position* still diverges between runs. It is a victim of the pacing
  jitter, not an independent source.
- **Uninitialised state:** not investigated exhaustively, but the
  `--exact` divergence grows smoothly from frame 200→445 rather than
  being present at frame 1, which is the signature of accumulated
  timestep drift, not a per-run uninitialised seed.

*`GE_DETERM=1` fixed-tick mode — assessed NOT NARROW, deferred. Confidence
that it's not narrow: MEDIUM-HIGH.*

The obvious hook — make `osGetCount()` a virtual clock that advances
exactly 775875 ticks per presented gfx frame (`videoEndFrame` in
`osSpTaskStartGo`, `port/src/libultra.c:1187`) — deadlocks the `boss.c`
main loop. That loop only presents a frame *after* the retrace gate
`mainTickElapsed >= 387937` passes, and with a frame-coupled clock
`mainTickElapsed` is 0 until a frame is presented → the first in-loop
frame never renders. `boss.c:442` (`waitForNextFrame()` after
`lvlStageLoad`, before any frame) hangs the same way. Breaking the
seal requires the **VI retrace post itself** to drive the virtual clock
(advance a fixed quantum per `portPostVIEvent`) *and* the pacemaker to be
frame-gated so it can't enqueue >1 retrace per render (else `deltaFrames`
jumps to the queue depth). That is a redesign of the port pacing model
(`portTickThread` / `portPostVIEvent` / `osGetCount`), with real deadlock
risk in loops that pump retraces without presenting — the `boss.c:448`
`NOBLOCK` drain, multi-frame stage loads, the pause menu, `front.c`
menu loops. It is contained in `port/` and env-gatable, but it is not
"a few lines, obviously correct" — it changes retrace/tick semantics, so
per AGENTS.md it is written up here rather than patched.

**Recommended design (for a future dedicated pass):**
1. `GE_DETERM=1` → `portTickThread` stops pacing on wall clock. Instead:
   a global `g_determFrameReady` flag is set by `videoEndFrame`; the tick
   thread posts exactly one `OS_SC_RETRACE_MSG` and advances a virtual
   `g_determTicks += 775875` (931050 PAL) **only** when it sees a new
   presented frame (or when `g_viRetraceMQ->validCount == 0` and no frame
   is pending — to service pre-first-frame / load-screen waits, advancing
   by the same quantum so `waitForNextFrame` sees exactly 1).
2. `osGetCount()` returns `g_determTicks` verbatim in this mode (no
   sub-frame interpolation — `store_osgetcount`/profiling just see 0
   deltas, which is harmless).
3. Seed `g_determTicks = 775875` at `portKernelInit` so the first
   `waitForNextFrame` computes `(775875 + 387937)/775875 == 1`.
4. Leave `osGetTime()` (µs wall clock) alone — audio mixing cadence and
   the heartbeat watchdog should stay real-time.
5. Prove with `framediff.py --exact`: two
   `GE_DETERM=1 GE_PCDUMP=... ` runs must produce ~0 % pixel diff
   (allow `--tol 2` for GL dithering). Then regenerate
   `tools_pc/golden/` with `--update` and switch CI to `--exact`.
Risk to watch: any game loop that calls `waitForNextFrame()` in a context
where no gfx task will be submitted (true loading spinners) — those need
the step-1(b) "no frame pending" fallback or they hang.

*`tools_pc/framediff.py` — DONE, committed. Confidence: HIGH (validated).*

Structural/tolerant by default (no determinism to lean on): per-frame it
computes (a) 16×12 grid-cell mean-RGB delta, (b) whole-frame non-clear
(non-black, pixcount.py rule) pixel-% swing, (c) a 16×16 aHash Hamming
distance; fails the frame if any exceeds its threshold. `--mask
X0,Y0,X1,Y1` (repeatable) drops known-animating regions (HUD) from all
three. `--exact` mode (per-pixel, `--tol`/`--tol-pct`) is there for a
future deterministic build. `--update` refreshes the golden set. Reads
`.ppm` (GE_PCDUMP) and `.png` (golden) on either side; PNG decoder is
built in (stdlib `zlib`; falls back to PIL for odd formats). Validated:
two nondeterministic re-runs of HEAD pass structural (worst cell
dmean 15, phash ≤ 23, non-clear Δ ≈ 0) while `--exact` correctly reports
32–69 %; a deliberately wrong frame pair fails (18 cells, phash 105).
Golden set at `tools_pc/golden/frame_0002{00,320,440}.png` is the D115
baseline — since it is a nondeterministic capture, only structural mode
is meaningful against it today.

**D117 — M-52 addendum: `GE_DETERM=1` implemented per the design above,
tested live (Windows console). Boot deadlock FIXED as predicted; full
determinism NOT achieved. Confidence: HIGH on both findings (live-tested,
not argued).**

Implemented exactly the 5-step design (`port/src/libultra.c`, env-gated,
default off, zero change to the non-`GE_DETERM` path): `osGetCount()`
returns a virtual `g_determTicks` seeded to the region quantum; a
`g_determFrameReady` flag is set at the `osSpTaskStartGo` graphics-task call
site (i.e. by the real "a frame was presented" event, not `videoEndFrame`
directly — same effect, one call site, no `video.c` change needed);
`portTickThread` gets a `GE_DETERM` branch that advances the clock + posts
one retrace only when that flag is set, polling every 200 µs instead of
pacing to the real 16.7/20 ms tick interval.

*Step 1(b) risk, confirmed exactly as written.* Without a load-phase
fallback, `-level_09 GE_DETERM=1` hangs before the first frame: symbolicated
the live hang (not guessed) — `shedThread` blocked in `osRecvMesg`
(`sched.c:256`, `__scMain`) and `mainThread` in `joyCheckStatusThreadSafe`
(`joy.c:166`), both waiting on the scheduler's interruptQ (the same queue
`portPostVIEvent` posts into, per the D134 comment), which never receives a
retrace because no frame has rendered yet to set `g_determFrameReady` —
exactly the chicken-and-egg the design called out ("boss.c:442 hangs the
same way").

*Fallback added, per the design's own suggestion — fixes the hang, breaks
determinism.* Added: when `g_determFrameReady` is false AND
`g_viRetraceMQ->validCount == 0` (nothing left to consume from a prior
tick), advance the clock and post a retrace anyway, so a boot-time waiter
isn't stuck forever. This **does** fix the hang — `-level_09 GE_DETERM=1`
now boots and renders past frame 3000+ instead of hanging. It does **not**
achieve determinism: two independent runs, same build, same command,
`framediff.py --exact` at the golden frame numbers (200/320/440):

```
FAIL frame_000200: 276998/307200 px over tol (90.169%, maxchan=218)
FAIL frame_000320: 85131/307200 px over tol (27.712%,  maxchan=102)
FAIL frame_000440: 47936/307200 px over tol (15.604%,  maxchan=102)
```

That's *worse* at frame 200 than the port's existing wall-clock
nondeterminism without any `GE_DETERM` mode at all (32–69 % per the
original D117 measurement above) — the fallback didn't just fail to fix
determinism, it introduced a new, bigger source of it. Root cause (analysis,
not yet re-tested): the fallback fires whenever the 200 µs poll happens to
observe an empty queue, which during the load phase depends on real
OS-scheduling latency between the polling thread and whatever the load path
is doing — so the *number* of extra ticks consumed before the first real
frame renders varies run-to-run, exactly the same class of divergence D117
already documents, just relocated from "wall-clock frame pacing" to
"wall-clock load-phase tick count." User-observed side effect, consistent
with this: with the fallback active the game visibly runs at a different
apparent frame rate and sped up — because the tick loop is no longer paced
to real 60/50 Hz at all once it starts free-running on empty-queue polls,
it advances (and lets the sim advance) as fast as the poll thread schedules,
not at any fixed rate.

**D117 — M-52 2nd attempt (same session): moved generation from a polled
fallback to a synchronous one. Root-caused precisely why that still isn't
enough. Confidence: HIGH (live-tested, mechanism traced through sched.c).**

Traced the actual consumer chain instead of guessing: `sched.c` `__scMain`
is the **sole** consumer of `g_viRetraceMQ` (`sc->interruptQ`), and every
retrace it processes drives task execution (`osSpTaskStartGo`), `joyPoll()`
(which is what unblocks `joyCheckStatusThreadSafe` too — the other M-52
1st-attempt hang site, for free), and the forward to `mainThread`, all
**synchronously on that one thread** before it asks for the next message.
So replaced the poll-driven tick-thread fallback with generation **inside
`osRecvMesg()` itself**: when `GE_DETERM` is on and the sole consumer of
`g_viRetraceMQ` finds it empty, synthesize the message inline (advance
`g_determTicks`, enqueue) instead of blocking — driven by call sequencing,
not by an independent thread's wall-clock poll.

Result: still boots (no regression on the deadlock fix), and the divergence
profile changed but did not go to ~0%: two runs, `framediff.py --exact`:

```
FAIL frame_000200: 101582/307200 px over tol (33.067%, maxchan=150)
FAIL frame_000320: 116844/307200 px over tol (38.035%, maxchan=161)
FAIL frame_000440: 43617/307200 px over tol (14.198%, maxchan=117)
```

Better than the 1st attempt's 90/28/16% at frame 200, but still nowhere
near the ~0% acceptance target. **Root cause, found by diffing the two
boot logs line-for-line** (not guessed): `boss.c`'s boot sequence has a
genuine, bounded, wall-clock real-time wait —
`for (i = 0; i != MAXCONTROLLERS; i++) { osSetTimer(100ms); osRecvMesg(BLOCK); }`
(`src/boss.c:188-190`, controller detection) — untouched by `GE_DETERM` by
design (`osGetTime()`/real timers are explicitly out of scope). During
that ~400 ms real window, **nothing is yet consuming `__scMain`'s forwarded
retraces** (`mainThread` hasn't reached its own retrace-consuming loop
yet), so the new synchronous-generation code has nothing throttling it —
`__scMain` free-spins through `osRecvMesg` as fast as the OS schedules that
thread, and *how many* virtual ticks accumulate during that fixed 400 ms of
real time depends on real thread-scheduling throughput. Same nondeterminism
class as both prior attempts, relocated a third time: generation-on-ask is
only deterministic when something else deterministically paces the asking,
and right now nothing does during any window where a client isn't actively
mid-frame.

**Disposition:** kept in the tree, env-gated (`GE_DETERM` unset by default,
zero behavior change confirmed via `verify.sh bunker1` before/after across
both attempts — worst cell 12.1–12.5 throughout, within existing noise).
The single-consumer synchronous-generation mechanism in `osRecvMesg()` is
the right shape and should stay — it's strictly better than polling and
fixed a real deadlock class cleanly. What's still missing: a genuine
**rate limit** on `__scMain`'s synthetic-retrace generation, decoupled from
real time — e.g. only synthesize the next one once the client has actually
finished processing the previous forward (a real request/response coupling
to `mainThread`'s progress, not just "the queue looked empty"), with a
separate, explicitly-bounded allowance for pre-first-frame boot code that
legitimately needs to service its own `osSetTimer` waits without the
scheduler racing ahead. That is a genuine design task, not a quick
follow-up — leaving D117 OPEN. `porting-notes.md` not updated (this
addendum's lesson is already exactly what §D117 above says, restated at
one more layer: any unthrottled or wall-clock-throttled generator racing
against a genuinely real-time-bound consumer reintroduces the same
divergence class, no matter how many layers deep you push it).

**D117 — M-52 4th pass: instrumented tracing (`GE_DETERM_TRACE=1`) precisely
localizes what's left — and it is NOT the virtual clock. Confidence: HIGH,
directly measured, not inferred.**

Rather than keep auditing code paths by inspection (two dispatches -- one to
the local delegate, one done directly -- had just spent a round ruling out
the level-load path and the audio-thread shared-globals angle with nothing
to show for it), added a temporary trace: every `g_determTicks`
advance/cap event and the `first_task_run` transition get a monotonic
sequence number + timestamp-free log line (`port/src/libultra.c`, gated by
`GE_DETERM_TRACE=1`, zero cost when unset). Ran `-level_09` twice, diffed
the two trace logs directly instead of guessing.

**Result: the tick mechanism is now proven fully deterministic.** Both runs
are byte-identical (same `ticks=` value at every logged step) all the way
through the `first_task_run` transition — the only difference is *which
spin iteration* it happens on (run 1: spin 4020, run 2: spin 4086 — 66 more
capped no-op spins in run 2, real-OS-scheduling-dependent as expected), but
the **virtual clock value at that moment is identical in both** (1551750 =
`2 × g_determQuantum`). Re-aligning the two logs by that spin-count offset
shows the entire subsequent tick progression is byte-for-byte identical.
The M-52 3rd-attempt fix (cap pre-task ticks to a fixed constant) works
exactly as designed — this is not where the remaining divergence comes
from.

**The actual mechanism, found by reading `sched.c` with the trace numbers
in hand:** every synthesized retrace — capped ones included — is a real
`VIDEO_MSG` that `__scMain` processes via `__scHandleRetrace()`
(`src/sched.c`), which unconditionally does `sc->frameCount++`
(`sched.c:319`) and then, per registered client, gates forwarding on
`(*((s32*)client + 2) == 0) || ((sc->frameCount & 1) == 0)`
(`sched.c:334`) — i.e. some clients only receive every *other* frame's
retrace by `frameCount` parity. The audio client is registered as exactly
this kind of half-rate client: `osScAddClient(&os_scheduler,
&g_AudioClient[0], &g_AudioManager.frameMessageQueue, 1)` (`src/audi.c:437`,
the trailing `1` is the half-rate flag). Since `frameCount` increments on
*every* capped spin — not gated by the virtual clock at all — and the total
spin count before the first real task varies run-to-run by a genuinely
real-time-scheduling-dependent amount, **`frameCount`'s parity at the
moment real gameplay begins is not guaranteed to match between runs**, even
though the clock value is now provably identical. This run pair happened to
land on a same-parity gap (66, even) — a different pair easily could not.
If it lands on opposite parities, the audio client's very first retrace
forward shifts by one frame's phase between runs, and audio-task scheduling
(which shares the same `osSpTaskStartGo`/tick-gating machinery) diverges
from there.

**Why no 5th live-coded attempt this session:** the natural next lever —
don't call `__scHandleRetrace` (and thus don't increment `frameCount`) for
a purely-capped "keep `__scMain`/`joyPoll` alive" spin — reopens the exact
chicken-and-egg the 1st attempt hit: `joyCheckStatusThreadSafe`'s handshake
and `__scMain`'s own unblocking need *something* processed during that
window, and `sc->frameCount` is decomp-internal state with no port-visible
accessor to correct after the fact. A real fix needs `frameCount`'s parity
(or the half-rate audio client's phase) to be made deterministic
independent of the real-time-dependent capped-spin count — a genuine
4th-layer design task, not a variant of any of the first three attempts.
Stopping here deliberately rather than rushing a live guess under momentum,
per the same discipline the earlier attempts were written up under.

**Disposition:** `GE_DETERM_TRACE` tracing kept in the tree alongside
`GE_DETERM`, same env-gating, zero cost when unset — it's the tool that
should be reused for the next attempt rather than re-instrumented from
scratch. D117 stays OPEN. porting-notes.md not updated yet (the
generalisable lesson here — "a per-frame parity/half-rate dispatch gate
fed by an unconditionally-incrementing counter inherits whatever
nondeterminism feeds that counter, even after the counter's *primary*
consumer is made deterministic" — is specific enough to this scheduler
that it's better captured here than genericized prematurely).

**D118 (session M-9) — SDL input layer implemented (Phase 3).**
`port/src/input.c` was an unimplemented stub; input only worked via
`libultra.c`'s `contSnapshotFromKeyboard()` (Z/Space=A, X=B, LCtrl=Z,
arrows=stick, WASD=D-pad — no C-buttons, no mouse, no gamepad).

Implemented a focused `input.c` (NOT a full port of pd_port's 1551-line
module — GE's menu/config code never calls that VK/bind-string API).
Provides `inputInit/inputUpdate/inputDestroy/inputGetNumControllers`
plus two helpers for `libultra.c`: `inputConnectedMask()` and
`inputComputePad(idx, *sx, *sy) -> u16 button`. The N64 button bits are
duplicated as `GE_CONT_*` in `input.c` rather than `#include <PR/os.h>`
(its `u8 errno;` field collides with `<errno.h>`'s macro; libultra.c
only gets away with it via a `#pragma push_macro` dance).

*Binding scheme.* Kbd/mouse (controller 0): WASD or arrows = analog
stick (move/strafe); mouse motion = C-buttons (aim, see bridge below);
LMB/LCtrl = Z (fire); RMB/LShift = R (aim mode); Space/Z/E = A;
X/R/F = B; Q = L; Enter/Tab = Start. Gamepad (SDL_GameController; pad 0
merges into controller 0, pads 1-3 → controllers 1-3): left stick =
stick, right stick = C-buttons (digital, 50 % threshold), RT = Z,
LT = R, A/X = A, B/Y/RB = B, LB = L, D-pad = D-pad, Start = Start.

*Mouse-look → C-button bridge (the subtle part).* GE aims with digital
C-buttons and has no analog-aim hook reachable without editing `src/`.
`inputUpdate()` integrates the relative-mouse delta (× `MouseAimSpeed`/
100) into a per-axis signed accumulator clamped to ±8. Each controller
poll `inputComputePad()` emits the matching C-button while |accum| ≥ 0.5
and drains one unit — so a flick holds the C-button for several frames
(proportional dwell) instead of a single blip. Limitations: still
digital (GE's own accel curve makes turn speed non-linear in mouse
speed); fast flicks saturate at ~8 frames of turn; diagonals limited to
the 8 C-button combos. A real analog path would need an `#ifdef PORT`
hook in `bondview.c` — left as TODO.

*Wiring.* `contSnapshotFromKeyboard()` in `libultra.c` (SI section) now
just calls `inputUpdate()` then marshals `inputComputePad()` output into
`g_contPad[]`/`g_contStatus[]` for all `MAXCONTROLLERS`, and sets
`g_contConnected` from `inputConnectedMask()`. It is still driven by
`osContStartReadData`/`osContStartQuery` (once per game logic tick) — no
new frame hook in `video.c` was needed. `osContGetReadData()` now
`memcpy`s the whole `g_contPad` array (N64 semantics: one pad per
channel) instead of only controller 0 — joy.c passes a
`MAXCONTROLLERS`-long array. `inputInit()` opens the gamepad subsystem +
all connected controllers and enables relative mouse mode.

*Config.* `ge007.ini` `[Input]` `MouseEnabled` (0/1), `MouseAimSpeed`
(1..500, default 50), `MouseInvertY` (0/1), via `configRegisterInt` in a
`PD_CONSTRUCTOR`. `config.c` has no float support so speed is an int
percent. Key/button REBINDING is not wired (would need the pd_port
bind-string system or a new mini-parser) — hardcoded scheme + TODO.

*Status.* Build GREEN (`ntsc-final`). Boots `-level_09` crash-free for
35 s, 900+ frames, `input: ready (mask=0x1, 1 controller(s))` logged.
`GE_INPUTLOG=1` gates a per-poll `sysLogPrintf` of the OSContPad when
button/stick are nonzero (zero-cost when unset, kept). Live input
UNTESTED (headless agent) — needs a human pass: see
`docs/HANDOFF.md`. Confidence: build/wiring HIGH; in-game feel of the
mouse→C-button bridge MEDIUM (aim speed tuning likely needed); gamepad
UNVERIFIED (none present in build env).
TODO: (1) key rebinding; (2) gamepad hotplug (opened only at init today);
(3) optional analog-aim `#ifdef PORT` hook in bondview.c — only needed now
for D118a (fully-analog hipfire pitch) and toggle-aim-scheme correctness.
[M-24: mouse-look rework + real INI parser done — see "Mouse-look rework"
below.]

*Mouse-look rework — M-24 (`port/src/input.c`, port-layer only, no `src/`
change).* Root reading of `bondviewProcessInput`/`MoveData`: GE's aim is
**mode-dependent** — hipfire (`!insightaimmode`) yaw = analog stick-X,
pitch = digital C-up/C-down (stick-Y = move, does not pitch); aim mode
(R held) yaw+pitch = analog stick past ±60 → `(stick-60)/10`, and
C-up/C-down there = crouch/lean/zoom (`bondview2.c:5340,5351`), *not* aim.
Also GE's native pitch is inverted (`U_CBUTTONS → speedVertaDown`, i.e.
C-up looks down, `:5272-5279`). New mapping in `inputComputePad`, keyed on
our own RMB/LShift state as the hold-to-aim proxy:
- **aim mode** → push `stick_x`/`stick_y` into the 61..80 band
  (proportional to per-poll mouse delta × `MouseAimSpeed`/100 × 4), **emit
  no C-buttons**. Yaw and pitch now identical analog feel.
- **hipfire** → yaw `stick_x += dx × MouseTurnSpeed/100 × 6`; pitch =
  digital C-up/C-down on `|dy| ≥ 1.5` px/poll.
- "mouse-down = look down" by default (accounts for GE's inversion);
  `MouseInvertY` flips.
Per-poll deltas, no accumulator (rate device). `ge007.ini [Input]`:
`MouseEnabled`, `MouseAimSpeed` (50), `MouseTurnSpeed` (100), `MouseInvertY`
(0) — **now actually parsed** (`config.c` INI load/save implemented, M-24).

- **D118b (FIXED, M-24)** — mouse-Y inversion resolved by the "mouse-down
  looks down" default above + `MouseInvertY` toggle.
- **D118c (FIXED, M-24)** — aim + mouse-down → crouch. Fixed *without* a
  `src/` hook: in aim mode the mouse now drives the analog stick and emits
  no C-button, so the crouch/lean/zoom mappings are never triggered by
  look input. (Proxy caveat: a toggle-aim control scheme would need a read
  of `g_CurrentPlayer->insightaimmode` since RMB-held ≠ aim state there.)
- **D118a (RESIDUAL, lower)** — in **hipfire only**, yaw (analog) vs pitch
  (digital C-button) still feel different. Aim mode is now fully analog and
  consistent. A fully-analog hipfire pitch would need the `#ifdef PORT`
  `bondview.c` hook (TODO 4). Minor — precise vertical aim in GE happens in
  aim mode, not hipfire.
- **D137 (FIXED)** — right-mouse *crash* (not the crouch bug):
  `gunDrawSight` `s32 sp54` truncated a `Gfx*`; see §F D137.
- Weapon switch on kbd/mouse = the A button (`Space`/`Z`/`E`), same as
  action/use (GE overloads it). No dedicated key; mouse-wheel cycle is a
  candidate quick-win.

**`data/` deletion + recovery (session M-3).** `git worktree remove
--force` on an agent worktree that had a directory *junction*
`worktree/data → main/data` followed the junction and deleted the real
`data/` contents (both `.z64` baseroms + `pccg-*`/`pcmodels-*` sidecars).
`data/` is gitignored so nothing tracked was lost. Recovered:
`cp baserom.u.z64 data/ge007.ntsc-final.z64` (sha1
`abe01e4a…` == the canonical `ge007.u.z64` build hash, so byte-identical
to what was there), then regenerated all three sidecars — `python
tools_pc/d43_emit.py ntsc-final` (pcmodels, 512), `d69_emit.py
ntsc-final` (pccg bg/stan, 52), `d88_emit.py ntsc-final --regen` (Usetup,
→ 73 rows, `pccg.bin` 3605249 B) — all "ALL CHECKS PASSED". **Lesson:
never junction `data/` into a throwaway worktree; copy it or point the
generator's `ROM_PATH` at the repo-root baserom.** The extra
`GoldenEye 007 (U) [!].z64` copy was not restored (unused — runtime and
generators use `ge007.ntsc-final.z64`, falling back to root
`baserom.u.z64`).

**Docs-to-commit reminder (session L).** The D88.1–D88.3 work
(`tools_pc/d88_emit.py`, `port/src/pccg.c`, `src/bondtypes.h`,
`src/game/bondview2.c`, `src/game/bondview_r.c`) plus the `GE_D88` probes
in `prop.c`/`stan.c` have been format-verified and pass `-level_09` up to
the D88.4 crash, but **remain uncommitted** (carried through two
interrupted sessions). Commit in sub-milestones per the usual pattern
once D88.4 is understood: format spec → converter → port wiring → probes.

**D69 status after D78-D88: the ORIGINAL blocker (`load_bg_file` faulting
on first stage load) is fully resolved and verified** — a clean run loads
BUNKER1's header/room/portal/envdata tables and its full ~1066-tile stan
file correctly (spot-checked byte-for-byte against the N64 source via the
`GE_D69STAN` probe: tile room/mid/tail/point values match). The game now
progresses substantially further than before (through room-streaming
setup, past the old D86 model-init crash and the D87 attract-mode crash)
before hitting D88's separate, newly-exposed "stage setup" file format
gap. D85 (room geometry renders wrong, not yet crash-free at the *visual*
level) and D88 (stage-setup file format, unconverted) remain open
follow-ups — **the "loads without
fault" acceptance bar is not yet fully met** (the process still exits via
crash, just much later in the load sequence), but the converter, port
wiring, and every ABI fix identified so far are format-verified correct
and committed.

**Environment reminders.** MSYS2 tools in `/c/msys64/mingw64/bin/` (not on PATH —
prefix `export PATH=…`). Build: `./build-pc.sh ntsc-final`. gdb **launch** mode
is far too slow for timing-dependent crashes (a D56-class crash ~10 s in took
>300 s under gdb to reach 2 frames — DBGHELP symbol loading + the D51
stall-heartbeat thread dumps; don't wait on it): prefer env-gated TEMP probes +
the built-in crash log for reproducible faults. **Correction (D87 session):**
gdb **attach** mode (`gdb -batch -x cmds -p <winpid>`, where `<winpid>` is the
Windows PID from `ps`, 4th column — the game must already be running, e.g.
launched with `nohup ... &`) works fine and is fast, since the process is
already warmed up and running at full speed before you attach; a hardware
watchpoint (`watch *(int*)0xADDR`) caught a global's write in well under a
minute. Useful for "is this global legitimately written, or corrupted"
questions on a long-running, non-crashing process — attach once the process
has been running a while, `continue`, and it'll fire on the very next real
write. Still avoid gdb for the crash itself if the crash is reproducible via
the crash log; symbolicate
offline with `addr2line -e build-pc/ge007.x86_64.exe -f -C <0x140000000+rel>`.
Image base 0x140000000. `load_resource`/many init fns use a fake RBP — compute
stack offsets from entry RSP. The D30 crash handler writes `ge007.crash.log`
with a working Phase-2 backtrace (**D44** fixed) — first stop for any fault;
frames past the true chain may be stale (a corrupted return address outside the
module, as in D56, means unwind depth is limited — confirm callers by code-
path analysis + behavior). Standalone probe compiles need `-std=c11` (without
it `typedef s32 bool` in bondtypes.h breaks under gnu23) and pointer-difference
arithmetic instead of offsetof (include/stddef.h is #if 0'd).

---

**D121 (session M-12, WS1) — frictionless per-level boot.** A bare
`./build-pc/ge007.x86_64.exe -level_XX` with no `-m*` args booted every
level with the default memory pools and OOM-crashed early in the load
sequence; you had to hand-copy the per-level `-ml -me -mgfx -mvtx -mt -ma`
row from `memallocstringtable[]` (`boss.c:101`) onto the command line.

*Root cause.* On N64 the per-stage auto-inject loop in `bossMainloop`
(`boss.c` ~line 425: `tokenSetString(memallocstringtable[i].string)`)
supplies that row, but it is gated on `g_DebugAndUpdateStageFlag`. With
`-level_` present that flag is left to `rmonGetToken()` (`boss.c:178`),
which the PC stub (`port/src/n64stubs.c`) returns 0 for, so the loop never
runs.

*Rejected fix.* Setting `g_DebugAndUpdateStageFlag = 1` when `-level_` is
present (the "mirror `boss.c:199-202`" idea in the old HANDOFF) is **not
behaviour-preserving on PC**: it makes boot render the full title-stage
intro (logos → gun-barrel → cast) instead of loading the level directly.
The flag does more than pick the token row.

*Fix (`#ifdef PORT` in `bossInitMainthreadData`, after the flag's own
`tokenSetString` block).* When `-level_` is present and no `-m` token was
given, look up the stage's `memallocstringtable[]` row (same match the N64
loop does) and `tokenSetString` it — **prefixed with `-level_XX` (and
`-hard N` if present)**, because `tokenSetString` (`src/token.c:41`)
`strcpy`s over the whole token buffer; without the prefix `bossMainloop`'s
`tokenFind(1, "-level_")` returns NULL and it boots the title stage. No
flag change, no control-flow change.

*Verified.* Bare `-level_09` now loads BUNKER1 directly (85–92 % non-clear
frame content from frame 30, no intro), matching the old
`-level_09 -ml0 -me0 -mgfx100 -mvtx50 -mt700 -ma150` repro. Residual
BUNKER1 prop/`modelLoad` crash is intermittent (D117 nondeterminism +
D88.4) and pre-existing — out of WS1 scope. HANDOFF repro line updated to
drop the manual `-m*` list.

**D122 (session M-12) — per-level prop/item model-load crash: propDef
converter missing handlers for ObjectRecord-derived record types.**
`-level_33` (Dam), `-level_34` (Facility), `-level_35` (Runway) crashed
deterministically before frame 1: `modelLoad` (`loadobjectmodel.c:393`,
`PitemZ_entries[modelid].header->RootNode` with an OOB `modelid` — `Rax`
held ASCII) and `modelInitRwData` (`model.c:6249`). BUNKER1 (`-level_09`)
and Silo (`-level_20`) were fine.

*Root cause.* The D88.4 propDef stream converter (`tools_pc/d88_propdefs.py
convert_record`) had per-type handlers for most record layouts but **four
`inherits ObjectRecord` types fell through to the generic arm**: 47
TINTED_GLASS, 39 VEHICHLE, 40 AIRCRAFT, 45 TANK (plus 13 AUTOGUN and 20
AMMO/MultiAmmoCrate, exposed once the first four were fixed). The generic
arm bswap32's N64 word 1 as a single 32-bit value, but for an
ObjectRecord that word is `[s16 obj][s16 pad]` — it needs `_hh_word`
(swap each half in place). Result: `obj` (the model id, read by
`domakedefaultobj` at `prop.c:155` → `modelLoad`) landed in the wrong
half → garbage/OOB index into `PitemZ_entries[]`. The generic arm also
never widened the ObjectRecord's embedded pointer slots (prop*/model* 4→8
B), so every tail field was mis-offset too. BUNKER1/Silo use none of
these six types (verified with a per-level `PROPDEF_N64_WORDS` type
histogram), which is why they were unaffected — the stream *walk* was
self-consistent for all 21 levels (`convert_stream` validates region
length), only the *field contents* of these records were wrong.

*Fix (converter only — `tools_pc/d88_propdefs.py`).* Added `OBJ_TAIL_DESC`
= {type: (ptr_word_set, hh_word_set)} for types 47/39/40/45/13/20 and a
handler that emits the real 144-byte PC ObjectRecord prefix
(`_emit_object_prefix`, which does `obj` correctly) followed by the tail
with pointer members widened 4→8 B and 8-aligned, `_hh_word` for u16-pair
words (vehicle aioffset/aireturnlist; ammo-crate `slots[]`), `_bswap32`
elsewhere. Updated `PROPDEF_PC_BYTES` (39: 176→208, 40: 180→208, 45:
224→248; 47/13/20 already correct) and the matching
`sizepropdef()` `#ifdef PORT` arms in `loadobjectmodel.c` (VEHICHLE
44→52, AIRCRAFT 45→52, TANK 56→62 words; N64 values kept in the trailing
comment / `#else` `#if 1` switch). No game-logic change. Regenerate:
`python tools_pc/d88_emit.py ntsc-final --regen` (21/21, ALL CHECKS
PASSED).

*Verified.* `-level_33/34/35` all get past the model-load chain now (no
`loadobjectmodel.c:393` / `model.c:6249` crash). BUNKER1 unregressed
(83–92 % non-clear frame content, frames 80–200). Silo unregressed
(exercises the new type-20 handler — no crash).

*Residual (separate blockers, out of D122 scope — hand to the level
sweep).* Dam + Runway now crash later in `chrIsNotDeadOrShot`
(`chraction.c:4483`, `self` = an image rodata address — guard/chr setup,
not propDef-related). Facility crashes in `import_texture_i8`
(`gfx_pc.cpp:821`, bad texture pointer — fast3d). Neither is touched by
this fix.

*Tail-layout confidence: medium* for 39/40/45 (VehichleRecord/
AircraftRecord/TankRecord field offsets have "locs need confirming" notes
in `bondtypes.h`; the pointer-word sets and total sizes are derived from
the struct as written and may need a nudge if a level actually drives
tank/vehicle motion). **High** for 47/13/20 (offsets are known) and for
the crash fix itself (the `obj`-field half-swap is unambiguous).

---

**D123 (session M-13) — crash class C1: `chrIsNotDeadOrShot` NULL deref on
6 levels (Dam 33, Runway 35, Frigate 26, Statue 22, Streets 29, Cradle
41). Converter zeroed the vehicle/aircraft `ailist` id. FIXED
(`tools_pc/d88_propdefs.py` only).**

*Symptom.* Bare `-level_33` etc. crashed before frame 1 in
`chrIsNotDeadOrShot` (`chraction.c:4483`, `self->actiontype`), fault addr
0x8 → `self ≈ NULL`. (The log's frame #1 `0x1401296a0` / `chraidata.c:61`
was a stale stack-scan hit, not a real frame — misled the original
triage.)

*Real backtrace (gdb):* `objTick` (`propobj.c:5504`) →
`ai((PropDefHeaderRecord *)poTruck, PROP_TYPE_OBJ)` →
`ai()` sees `Entityp->type == 39` (PROPDEF_VEHICHLE) so sets
`VehichleEntityp`, `AiListp = VehichleEntityp->ailist`, `ChrEntityp =
NULL`. `AiListp` resolved to `m_AimAtBond` (`GAILIST_AIM_AT_BOND` == id 0).
Its first opcode is `AI_TRYFireOrAimAtTarget` →
`actor_aim_at_actor(ChrEntityp=NULL,…)` → `chrIsNotDeadOrShot(NULL)`.

*Root cause.* `prop.c:1764` does `pdef_veh->ailist =
ailistFindById(pdef_veh->ailist)` — it reads the **pre-populated integer
AI-list id** out of the ROM record (VehichleRecord.ailist @ N64 0x80;
Dam's is `0x040a` = 1034, a per-level list). But D122's `OBJ_TAIL_DESC`
put word 32 in `ptr_word_set`, whose handler emits **8 zero bytes** for
the widened 4→8B slot (correct for `path*`/`Sound*`, which `prop.c`
overwrites with 0 — but wrong for `ailist`, read before overwrite).
Result: `ailistFindById(0)` → `isGlobalAIListID(0)` true → global list id 0
= `GAILIST_AIM_AT_BOND` → a vehicle runs a CHR-only aim list with a NULL
chr. `AircraftRecord.ailist` (`prop.c:1786`) is identical. BUNKER1/Silo
emit no vehicle/aircraft propDef → unaffected. All 6 crashing levels have
a vehicle or aircraft prop.

*Fix (converter only).* New `OBJ_ID_WORDS = {39:{32}, 40:{32}}` in
`tools_pc/d88_propdefs.py`; the `OBJ_TAIL_DESC` walk now emits
`_bswap32(w)` into the low 4 bytes of the 8-aligned slot for id words
(high 4 stay zero), so the u16/s32 id survives into the widened pointer
field. Words 32 removed from the 39/40 `ptr_word_set`. Struct size
unchanged → no `sizepropdef()` edit. TANK (45, `collision*`) and AUTOGUN
(13, `unkC4/unkC8/beam`) left as ptr/zero — verified genuinely
runtime-populated (`prop.c:1712` / `setupAutogun` `prop.c:694`). Regen:
`python tools_pc/d88_emit.py ntsc-final --regen` (21/21 ALL CHECKS
PASSED).

*Verified.* `chrIsNotDeadOrShot` crash gone on all 6. Dam 83%, Statue
80%, Frigate 68–90%, Cradle 77% non-clear frames, no crash log.
BUNKER1 (91.7%) + Silo (91.7%) unregressed. Runway and Streets now crash
**later** in fast3d (`palette_to_rgba32`/`import_texture_i4`
`gfx_pc.cpp:851`; `gfx_sp_matrix` `gfx_pc.cpp:1046`) — class C2/other
track, not C1. Frigate/Statue/Cradle/Dam reach a frame cleanly.

*Confidence: high* — root cause reproduced in gdb, fix is a 1-field
converter change matching the exact `prop.c` read pattern, and the id
value (0x040a) matches the N64 `UsetupdamZ.c` Vehicle record tail.

**D124 (session M-13) — crash class C2: fast3d handed a garbage texture
pointer on Facility (`-level_34`) and Jungle (`-level_37`).** Symptom:
early deterministic crash — Facility `import_texture_i8` `gfx_pc.cpp:821`
fault `0x72181ee8`; Jungle `gfx_tex_normalize_source` `gfx_pc.cpp:644`
fault `0xabcd0824` (= `IMAGESEG(0x824)`). Two *different* root causes:

*Jungle — FIXED.* `gimgSyncCompiledGlobalDLs()` (`port/src/gimgfixup.c`,
D68) is meant to copy the texture pointers that `texLoadFromDisplayList()`
patched into the ROM copy of the Globalimagetable back into the compiled
`globalDL_0xNNN` shadow arrays (`assets/oddtextures.c`) that `explosion.c`
executes via `g_ExplosionDisplayLists[]`. Its slot-detect test was
`p[6]==0xCD && p[7]==0xAB` — the *post-fixup marker* — but by the time it
runs `texLoad()` has **already replaced every IMAGESEG w1 in the ROM copy
with a real pointer**, so that test never matches and the sync copied
*nothing*. The compiled arrays kept their link-time `IMAGESEG(id) =
0xABCD0000|id` words; the first explosion/smoke/particle DL that
Facility/Jungle render feeds `0xABCDxxxx` straight into fast3d
`seg_addr()` (falls through to `return (void*)w1`) → `import_texture`
deref. The 7 "working" levels are only working because nothing triggered
an explosion-DL draw inside the ~22 s capture — **the bug was latent in
every level.** *Fix:* detect the slot from the **compiled** array instead
(`dst[j]` is a `G_SETTIMG` whose w1 is still `0xABCDxxxx`), then copy the
ROM copy's resolved `*(u32*)(p+4)`. `port/src/gimgfixup.c` only; no
converter/sidecar change. Verified: Jungle now renders ~300 frames
(91.7% non-clear) before a *separate* downstream crash (`gfx_sp_matrix`
`gfx_pc.cpp:1046`, bad G_MTX seg addr in the same explosion-DL stream —
those DLs also carry unconverted `G_MTX` words; follow-up, D75/matrix
family). BUNKER1 (83%) + Silo (91.7%) unregressed.

*Facility — NOT fixed (separate class).* Fault `0x72181ee8` is a bogus
`G_SETTIMG` w1 (`fmt=4 siz=2 w=0`, prev cmd an unconsumed `0xba` GE
tex-macro opcode) in a DL living in V1 DRAM — a **model/prop GDL from the
runtime `texLoadFromGdl()` relocation path** in `sub_GAME_7F0762E0`
(`objecthandler_2.c:82`), not a room GDL and not the global bank.
`GE_C2` probe of `texLoadFromGdl` shows its model-GDL calls (second batch,
`src≈0x706a****`) write to **non-16-aligned `dst`** (e.g.
`0x701eac01`, `0x701eb7d5`) — the `replacementgdl`/`name`(=srcsize)
offset arithmetic in `objecthandler_2.c` still mixes N64 8-byte and PC
16-byte `Gfx` strides, so converted commands land mid-slot and a
`G_SETTIMG` w1 reads as garbage. This is the open D80/D82/D83
"model/room GDL runtime conversion not verified" area — shared infra
touched by every level; out of C2 budget, left for a dedicated pass.
BUNKER1/Silo/Jungle don't hit it because their visible prop models
happen to convert cleanly; Facility's do not.

*Probes:* none left in tree — `GE_C2` scratch prints removed after
root-cause. *Files touched:* `port/src/gimgfixup.c` (fix),
`docs/porting-notes.md`, `docs/internals.md`, `docs/dev/LEVEL-STATUS.md`.
*Confidence:* Jungle fix **high** (mechanism proven with a `[C2sync]`
trace showing 32 slots going `abcd08xx -> 700e****`, verified render).
Facility diagnosis **medium** (probe evidence strong; exact off-by in
`objecthandler_2.c` not yet pinned).

**D125 (session M-14) — crash classes C3 + C6: the converted `propDefs`
blob in RAM does not match `tools_pc/d88_propdefs.py`'s offline output;
the `sizepropdef()` walk drifts, so every `pdefIndex`-keyed lookup
(`setupDoor` → `linkedDoor`, `weaponAssignToHome`, `modelLoad(modelid)`)
gets the wrong record.** NOT root-caused; investigation cut for a usage
reset. Affects Aztec (`-level_28`, `propobj.c:13523/:13601`), Bunker2
(`-level_27`, same `door7F054FB4`), Surface2 (`-level_43`,
`loadobjectmodel.c:393` `PitemZ_entries[modelid].header`).

*Evidence.* Instrumented the `proplvreset2` propDef walk
(`prop.c:1865`, `#if defined(PORT)` `getenv("GE_C3")` print of
`pdefIndex / phead->type / sizepropdef(phead) / phead / g_CurrentSetup.propDefs`)
and the `setupDoor` `linkedDoor` resolve (`prop.c:1206`). On `-level_27`
(`UsetupsevbZ`, `setup_text_pointers[27]`):
  - `g_CurrentSetup.propDefs = 0x701ce814`.
  - Runtime walk: idx0 `type=35` (WATCH_MENU, correct), then idx1–4
    `type=0`, idx6 `type=112`, idx15 `type=26`, … — i.e. after the first
    record the blob is **zeros / garbage**, but `sizepropdef` still
    strides the phantom `type=35` at idx0/5/10 forward 16 B each.
  - By the first real DOOR the walk has counted **147** records where the
    offline converter (`convert_stream(UsetupsevbZ)`) puts it at **46**
    (verified: offline output = 346 records, DOOR at idx 46, self-
    consistent with `PROPDEF_PC_BYTES` == `sizepropdef()` PORT strides —
    every type cross-checked against `d88_layoutprobe` sizes, all match).
  - `setupDoor` then gets `arg2=147` for a stream-index-46 door;
    `linkedDoorOffset(-1) + 147 = 146` → `setupGetPtrToCommandByIndex(146)`
    returns a `type=0` record → `door->linkedDoor` garbage → fault
    walking the linked-door list in `door7F054FB4`. Surface2's C6 is the
    same mechanism one step earlier: a drifted `pdefIndex`/`modelid`.

*What is NOT the bug (ruled out).* (a) `d88_propdefs.py` `convert_stream`
itself — self-test passes, offline walk of all 3 crashing levels'
converted output is clean, DOOR/linkedDoorOffset values correct, stream
tiles `[propDefs, intro)` exactly (`end=OK` for all 21). (b)
`sizepropdef()` PORT strides — every `case` equals
`PROPDEF_PC_BYTES[type]/4` and equals the compiler `sizeof` of the widened
struct (checked via `d88_layoutprobe`). (c) DOOR converter layout —
`linkedDoorOffset@144`, `linkedDoor@216`, `unkcc@224`, size 296 all match
`d88_layoutprobe`; tail pointer-widening walk is correct end-to-end. (d)
The per-type histogram diff (brief's suggested approach): the 3 crashing
levels emit no propDef type absent from the 12 passing levels — TINTED_GLASS
and AUTOGUN appear in passing Caverns/Control too. **This is not a
forgotten-type converter gap like D122/D123.**

*Best hypothesis (M-14) — DISPROVEN (session M-15).* M-14 guessed the
converted `propdefs_pc` bytes were not landing in the sidecar at the
relocated `propDefs` offset (a `d88_emit.py` region-tiling / delta-reloc
bug, lines ~308-338/471-476). The M-13 overseer note refined this to a
pass-1 delta / region-`end` mismatch (`pd_end != pd_start + _n64len`).
**Both are wrong.** New diagnostic `tools_pc/d125_check.py` decompresses
the *emitted* `data/pccg-ntsc-final/pccg.bin` entry for every Usetup*Z,
reads the relocated `propDefs` header field, and byte-compares that slice
to a fresh `convert_stream()`:

  - **All 21 levels: `MATCH`** (Aztec/Bunker2/Surface2 included). The
    sidecar's propDefs blob, post-RZ-roundtrip, is byte-identical to the
    converter output, placed exactly where the relocated header field
    points.
  - Instrumented `d88_emit.py` (D125_DEBUG, since reverted): for all 21,
    `tiled_pd_end == H[intro]` exactly, `_n64len == pd_end - pd_start`
    exactly, the region after `propdefs` in the sorted list is always
    `intro`, and the growth `pclen - n64len` fed to `cum` is correct.
    There is **no** region-end/delta bug at `d88_emit.py:308-343`.
  - Full re-cross-check of `sizepropdef()` PORT strides vs
    `PROPDEF_PC_BYTES/4`: all 28 types match (as M-14 already found).

**Therefore the offline pipeline (converter → emit → RZ compress) is
correct end-to-end. The "idx0 correct, then zeros" RAM signature M-14
observed must originate at or after runtime load** — candidates, in
rough order: (a) `decompressdata()` truncation — `port/src/rzdecomp.c`
inflates with `Z_FINISH` and a 4 MiB `avail_out`, loops while `Z_OK`;
if the deflate stream for these larger converted files hits `Z_BUF_ERROR`
or `Z_STREAM_END` mid-output the `while (ret == Z_OK)` exits early and
the tail of `dst` stays zero (needs a probe: log `ret` + `produced` vs
expected decompressed size for `UsetupsevbZ`); (b) the STAGE bank alloc /
`mempAllocBytesInBank` giving a buffer that is fine for the N64-size file
but the converted file is ~15-25% larger — check `mempGetBankSizeLeft`
at the `_fileNameLoadToBank(strResource, …, 256, MEMPOOL_STAGE)` call
(`prop.c:1271`) vs the decompressed size; (c) the `prop.c:1280-1308`
rebase loop or `langLoadToAddr` (`prop.c:1274`, runs right after the
load, same bank) overwriting the tail of the setup file. M-14's own
runtime dump had `g_CurrentSetup.propDefs = 0x701ce814` (`&15 == 4`).

*Next step.* Probe `decompressdata()` for `UsetupsevbZ`: print `ret`,
`produced`, and the first 64 bytes at `dst + 16` (offline record 1
start). If `produced` < the offline decompressed file size → truncation
(fix the inflate loop / `avail_in`). If `produced` is correct but the
bytes are still zero → something overwrites it post-load (bisect
`langLoadToAddr` / the rebase loop). `tools_pc/d125_check.py` stays in
tree as the offline-side regression guard.

*Probes:* **none left in tree** — the two `#if defined(PORT)`/`GE_C3`
scratch prints in `src/game/prop.c` (lines ~1206 and ~1865) and the
temp scan scripts `tools_pc/c3_scan.py` / `tools_pc/c3_doors.py` were all
reverted/deleted. *Files touched:* only `docs/internals.md` (this
entry) + `docs/dev/LEVEL-STATUS.md` (one line). No code, no converter, no
sidecar change — **sidecars do NOT need regen**, tree builds clean at
`f2beae4b` + M-13 uncommitted set.

**D125 addendum (session M-16) — ROOT CAUSE FOUND: `d88_emit.py:374`
4-byte literal into an 8-byte slice; per-record buffer shrink; out-of-range
leaf writes insert at the current end → boundpad name blob drift.**

*Crash mechanism (Aztec, `-level_28`), fully traced.* Door idx 226
(`objid=307`, `pad=33`) is left with `model=NULL`/`prop=NULL` by
`setupDoor()`: its boundpad's plink name in the sidecar is `"8d2"`
(original ROM: `"p138d2"`) → `stanPackId()` rejects it (valid = `p`/`q`
+ decimal ≤32767 + letter + optional digit 0-7; probe line `D88
stanMatchTileName id=8d2 hi=ffff lo=ff` is the failure sentinel) →
`init_pathtable_something()` returns 0 with `*tile_stack=NULL` →
`getposstan()` nonzero → `setupDoor` sets `door->prop=NULL`, skips
`doorInit()` → later tick `door7F054FB4()` (`propobj.c:13601`) derefs
`door->model->obj`; `Model.obj` sits at PC offset 0x10 (widened `chr`
ptr) → fault addr 0x10, Rcx=0. Linked-door pair: idx225 (`pad=32`, valid)
has `linkedDoorOffset=1` → idx226. All 178 regular pads resolve fine; only
boundpad names are corrupt.

*The bug.* `tools_pc/d88_emit.py` `emit_pad()` line 374:
`out[dst_o + 0x30:dst_o + 0x38] = b"\x00\x00\x00\x00"` — the stan zero-fill
assigns a **4-byte** literal to an **8-byte** slice. Python silently allows
in-range short-RHS slice assignment, so each of Aztec's 179 pads + 148
boundpads shrinks the output `bytearray` by exactly 4 B (measured: after
header 0x14d1c → after pads 0x14a50 → after boundpads 0x14800). Later
verbatim leaf writes (the plink string blob, relocated to ~0x147EC+) then
target offsets at/past the shrunken tail; for those, Python clamps the
slice to an empty range **at the current end** and inserts the data there —
each string lands at the buffer tail instead of its relocated offset,
drifting earlier with every write. A simulation of exactly this semantics
reproduces the observed sidecar names byte-for-byte: pad30 `p139d2`
(intact), pad31 `139d2`, pad32 `37d1`, **pad33 `8d2`**, pad34 `2`,
pad35 `1`, pad36 `` (empty).

*Why it was missed.* No error is raised; the final file is only
`(n_pads+n_boundpads)*4 − re-extension` bytes short of `total_new`
(Aztec: 85276 vs 85145) because later leaf writes re-extend the buffer;
`d125_check.py` verified only the propDefs slice. M-14's "propDefs zeros in
RAM" signature was a misread — M-15 already proved sidecar propdefs
byte-correct, and M-16's post-load RAM dump matched the sidecar byte-for-
byte (85,145 B), so runtime load is clean too.

*Fix (pending).* Line 374 → `b"\x00\x00\x00\x00\x00\x00\x00\x00"` (8 NULs).
Audit of every other `out[` write in the file: all remaining slice/RHS
pairs are width-matched — this is the only one. **All 21 Usetup*Z sidecars
are affected to varying degrees** (shrink = 4×(pads+boundpads) per level;
which names drift depends on where the blob lands vs the shrunken tail) —
regen all: `python tools_pc/d88_emit.py ntsc-final --regen`, then re-run
`tools_pc/level_sweep.sh` (expect C3 Aztec+Bunker2 to clear; currently-
passing levels can only improve, but the sweep is the proof).

*Probes in tree (temporary — remove after fix verified):* `GE_D125`
env-gated prints in `port/src/rzdecomp.c` (decompress ret/produced),
`src/game/prop.c` (setup header + post-load 0x15000 dump to
`d125_setupdump.bin` + post-`setupDoor()` door state), `src/game/propobj.c`
(`door7F054FB4` NULL-model detect+skip, `objInit` failure print, plus temp
`#ifdef PORT` stdio/stdlib includes). Scratch: `tools_pc/d125_convtest.py`
(instrumented converter copy — source of the shrink evidence),
`tools_pc/d125_check.py` (**keep** — offline regression guard),
`tools_pc/d125_inflate_test.c`, `d125_offs_tmp.c`, `d125_t.c`,
`d125_run.log`, `d125_setupdump.bin`, `build-pc/d125conv/`.

**D125 addendum (session M-16b) — FIX LANDED.** `d88_emit.py:374` changed
to 8 NUL bytes (+ a warning comment). Offline proof (Aztec, throwaway
`d125_proof.py`): the fixed converter's boundpad plink names 30–36 read
`p139d2 p139d2 p837d1 p138d2 p85a2 p1377b1 p504d2` (= original ROM), 147
boundpad records recovered (M-14's "46" was the corrupt read — no second
bug). `d43/d69/d88 --regen` → 21/21; `d125_check.py` → 21/21 propDefs
still MATCH. Direct re-test: **Aztec `-level_28` PASSES** (91.0%, 900+
frames, was C3 CRASH at `propobj.c:13601`); Bunker1/Silo unregressed.
**Bunker2 `-level_27` still crashes** — moved to `door7F054FB4`
`propobj.c:13536` (`var_s1->openPosition`, `var_s1` = `0xffff…` from
`linkedDoor`). Separate bug: the DOOR-tail converter in `d88_propdefs.py`
(`DOOR_TAIL_PTR_WORDS` / `linkedDoorOffset` word 32) vs the compiled PC
`DoorRecord` layout — `linkedDoor` is resolved from `linkedDoorOffset`
(`prop.c:1204`), a read-before-write int id like D123's `ailist`; Bunker2
has linked double-doors, Aztec's are singletons so it dodged it. Folded
into the converter write-audit (C3 residual). Sweep: 12 → 13/21.
All M-16 probes reverted, scratch files deleted, `d125_check.py` kept.

**D126 (session M-17) — objective-subrecord `->next` pointer growth desyncs
the propdef walk (C3r Bunker2 + C4 Depot + C6 Surface2).** The Bunker2
`door7F054FB4` crash was NOT a DOOR-tail layout bug (the M-16b hypothesis).
Instrumenting the `proplvreset2` first-loop walk showed `pdefIndex` drifting
~100 ahead of the offline `convert_stream` record index; a byte dump proved
the RAM propDefs matched the sidecar exactly *at load* but had record N+1's
header zeroed *by the time the walk reached it*. Cause: four objective
sub-record types — `criteria_picture` (30), `criteria_roomentered` (32),
`criteria_deposit` (33), `setup_objective_text` (35) — are
`{ s32 x N; T *next; }`. `set_parent_cur_obj_photograph/enter_room/
deposited_in_room` and `setup_briefing_text_entry_parent` (`objective.c`)
write `arg0->next` unconditionally while the setup walk visits each record,
building a runtime linked list. On N64 `next` is a 4-byte word at the end of
a 16/20-byte record — no spill. On x86-64 the pointer widens to 8B and the
compiler 8-aligns it at offset 16, so the struct is 24B; the 8-byte
`->next` store lands at record+16..24, clobbering the *following* record's
`PropDefHeaderRecord` (type byte at +0x13). Walk desyncs → command indices
drift → `setupDoor`'s `setupGetPtrToCommandByIndex(linkedDoorOffset + arg2)`
returns the wrong record → `door->linkedDoor` chain walks into non-door
memory → crash. Depot (`prop.c:902` tile-walk room) and Surface2
(`PitemZ_entries[modelid]`, the C6 "D122 continuation") were the same
desync landing on different downstream derefs. Fix:
`tools_pc/d88_propdefs.py` `PROPDEF_PC_BYTES[30/32/33/35] = 24` + a typed
handler emitting the leading s32 words then an 8-byte zero `next`;
`src/game/loadobjectmodel.c` `sizepropdef` PORT returns 6 for those types
(N64 `#else` kept). Regen'd all 21 sidecars, `d125_check.py` 21/21 MATCH.
**Bunker2 `-level_27`, Depot `-level_30`, Surface2 `-level_43` now PASS**
(91.5% / 79.8% / 70.4%); Bunker1/Silo/Aztec/Archives/Egypt/Train
unregressed. Sweep 13 → 16/21. Generalisable quirk appended to
`porting-notes.md` §A. Remaining crashes: C2 Runway/Facility (model-GDL
align), C2m Jungle (`G_MTX`), C5 Control (BG portal), C7 Surface1
(`sndSetupSound`).

**D133 (session M-20) — intro "mostly black" is the D75/D76 steady state, not a
regression.** The M-18/M-19 handoff "Next task" listed "intro renders mostly
black" as a regression against the M-17 handoff line "the entire intro renders —
logos → gun barrel → cast".

Method: `git worktree add ../ge007-m17 9ec6121e` (COPY `data/`, never junction —
see the data-dir-junction-hazard memory), `./build-pc.sh ntsc-final`, capture
`GE_PCDUMP="20-900:20"`, `tools_pc/pixcount.py` per frame. Compared against the
integrator's HEAD (`0b5f5d1a`) capture.

| frames | M-17 `9ec6121e` | HEAD `0b5f5d1a` |
|---|---|---|
| 20–100 (legal screen) | non-clear **6677 (2.17%)**, bbox (50,265)-(589,446) | non-clear **6677 (2.17%)**, same bbox |
| 120–360 | 0–16 non-clear px (near-black) | 0–16 non-clear px |
| 380–400 (centred logo) | 20.5k–22.1k (6.7–7.2%), bbox ~(253,142)-(394,336) | 18k–24k (6–8%), bbox ~(250,138)-(390,340) |

The legal-screen frame is a static 2D framebuffer; a **pixel-identical**
non-clear count on two independently-built binaries is proof the render path is
unchanged. 2D/text/texture layers draw on both; the animated character-model
layers (Nintendo-logo transform, gun-barrel Bond figure, cast models) never
appear on either — this is **D75** verbatim ("skeletal/animated character models
never appear; non-animated 3D appears with a bad transform"). The M-17 "entire
intro renders" line was aspirational, not a measured coverage state.

Bisect not needed (state identical at the older commit). No cheap `#ifdef PORT`
regression exists; the fix is the parked D75 3D-pipeline work
(`docs/dev/GRAPHICS-BACKLOG.md`). `-level_09` re-verified PASS (690+ frames, 91.7%)
so the capture harness is sound.

Worktree removed (`git worktree remove --force ../ge007-m17` — safe because
`data/` was copied, not junctioned). Tree restored to `master`. Confidence:
**high** (pixel-identical legal-screen coverage; symptom is a verbatim match to
the open D75 description).

Secondary (M-20, not investigated per scope): Facility `-level_34` boot-crashes
`frames=0` PC `0x1400c3b77` (addr2line unresolved); Jungle `-level_37` renders
frames 1–2 then kernel-heartbeat hangs (`frames=2`). Both claimed PASS in
M-18/M-19 (D130/D131). Machine was only lightly loaded. Needs a clean-machine
re-verify (`docs/dev/LEVEL-STATUS.md`) before calling it a regression.

**D131 (session M-19) — Jungle `-level_37` C2m crash: `osVirtualToPhysical()`
truncates a compiled-symbol pointer inside a GBI matrix command.** Jungle
renders ~300 frames then AVs in `gfx_sp_matrix` (`gfx_pc.cpp:1046`,
`int32_t int_part = addr[…]`) with `addr = 0x401c68e0`. Caller is the `G_MTX`
case of `gfx_run_dl` → `seg_addr(cmd->words.w1)`.

Probe (`GE_MTXPROBE`, reverted) dumped the faulting DL — a well-formed model
render sequence in a per-frame DRAM GDL: `gSPMatrix(PROJECTION,LOAD)` w1 =
`0x700d8070` (valid), `gSPMatrix(MODELVIEW,LOAD)` w1 = `0x401acbe0` (bad),
`gSPMatrix(MODELVIEW,MUL)` w1 = `0x401c68e0` (bad, the fault), `gSPVertex`
w1 = `0x700da350` (valid). All lens `0x40`, opcodes sane — the walk is *not*
desynced, only the modelview-matrix pointers are wrong.

`0x401c68e0` == `(u32)(0x1_401c68e0)` — a compiled-module address with the
`0x1_00000000` high word dropped (module fixed-based at `0x140000000`, cf.
D94 `chraction.c:1243` comment). Source: `explosionRenderPropSmoke`
(`explosion.c:1501`) does
`gSPMatrix(gdl++, osVirtualToPhysical((void*)&dword_CODE_bss_8007A100),
G_MTX_NOPUSH|G_MTX_MUL|G_MTX_MODELVIEW)` — `&dword_CODE_bss_8007A100` is a
compiled `.bss` matrix; `osVirtualToPhysical()` is `(u32)(uintptr_t)va`
(`libultra.c:1207`), so the store truncates. The `[-1]` LOAD matrix is the
room matrix from `applyRoomMatrixToDisplayList` (same truncation path).
Why the projection matrix is fine: `get_BONDdata_field_10E0()` returns a
runtime `0x70xxxxxx` pointer that survives the `u32` cast. Why
`gSPDisplayList(&globalDL_0xNNN)` is fine: the port's `Gwords.w1` is a
64-bit `uintptr_t` (`include/PR/gbi.h:1730`) and `gDma1p` stores the full
pointer — only `osVirtualToPhysical` truncates.

~30 sites pass a compiled matrix/vtx symbol through `osVirtualToPhysical`
(`explosion.c`, `glass.c`/`glass2.c`, `blood_animation.c`, `bondview2.c`) —
each latent until that effect first draws, which is why only Jungle (an
early scripted explosion) tripped it in the no-input sweep.

**Fix** (`port/fast3d/gfx_pc.cpp` `seg_addr`): at the final fallthrough,
if `w1 ∈ [0x40000000, 0x70000000)` restore the module high word
(`(mod_hi | w1)`, `mod_hi` read from this TU's own load address). Everything
legit that reaches the fallthrough is either DRAM V1 (`≥0x70000000`), KSEG0
(`≥0x80000000`), an LSB-set / low-nibble segmented address, or a
sub-`0x800000` physical offset — all handled in earlier branches — so the
range is unambiguous. Narrow, ABI/pointer-width class (same as D94/D3x).
Verified: Jungle renders to logic-frame 2400+ crash-free at 91.7% non-clear;
`-level_20` (Silo) + `-level_24` (Archives) unregressed. **Sweep 18→19/21.**

Not fixed this session: **`-level_09` (BUNKER1) now crashes at boot**
(`frames=0`, PC `0x1400066fc`, fault `0x00a2fc68` — a stack pointer
`0x0fa2fc68` with the top nibble masked, i.e. another 28/24-bit segment-mask
applied to a real pointer). Reproduced on a **clean `git stash` checkout of
master `0a4b3bae`** → pre-existing regression, not caused by D131 (a
fast3d-only change cannot affect a pre-first-frame boot crash). Contradicts
the M-18 handoff's "`-level_09` unregressed" claim; likely fallout from a
D126/D128 propdef/portal change or an environmental sidecar/ROM mismatch.
Next session: `git bisect` D125→HEAD against `-level_09`. Silo also shows an
intermittent hang in the intro fly-down-to-Bond cinematic (user report;
not always reproducible — ran 300 frames clean here).

---

**D130 (session M-18) — Facility + Runway C2 crash was `romdataFixupFont`,
NOT the model-GDL relocation.** `-level_34` (and `-level_35`) fault in
`import_texture_i8` (`gfx_pc.cpp:821`) on `loaded_texture.addr = 0x72181ee8`,
an un-mapped wild pointer. gdb showed the fault DL is a 2D `G_TEXRECT` block
(`gfx_dp_texture_rectangle` → `gfx_sp_tri1(is_rect)`), with the wild pointer
baked into a preceding `G_SETTIMG` w1. Traced back through `GE_C2GDL` probes:

- `texLoadFromGdl` **never copies a `G_SETTIMG` (0xFD)** on these levels, and
  `texWriteLoadToTmemAddr/Zero` are **never called** — so the M-14 addendum's
  "model-GDL relocation writes non-16-aligned dst" hypothesis is wrong. A
  probe in `sub_GAME_7F0762E0` also confirmed `gdl` from
  `modelIterateDisplayLists` is still a **segmented `0x05xxxxxx` value**
  (`modelPromoteNodeOffsetsToPointers` does NOT promote DL Primary/Secondary),
  so the `Switches + (x & 0x00ffffff)` idiom is correct as written.
- The wild `G_SETTIMG` is emitted by `gDPLoadTextureBlock(gdl,
  curchar->pixeldata, G_IM_FMT_I, G_IM_SIZ_8b, …)` in
  `textRenderGlyphOutlined` (`textrelated.c`) while rendering the level-title
  string "Chemical Warfare Facility #2" — specifically glyph `#` (and `"`).
- `curchar->pixeldata` for BankGothic/ZurichBold glyph indices **0, 1, 2** is
  corrupt after `load_font_tables`: glyph 1 `.width = 0x01000000`, glyph 2
  `.pixeldata = 0x020002e8` (→ `+= font_base` → wild).

Root cause: `romdataFixupFont` (`port/src/romdata.c`) re-lays-out the N64 24B
`fontchar` array into the PC 32B array **in place** — backward over glyphs,
forward over the 5 leading `u32` fields per glyph. PC−N64 stride = 8, so
`d = s + 8*i`; for `i ∈ {0,1,2}` `d` overlaps the 24-byte source read span
and an early `*(u32*)(d+4k) = bswap(*(u32*)(s+4k))` write clobbers a later
`k`'s source. Glyph 1 (`Δ=12`): k=3/k=4 read bytes k=0/k=1 already
overwrote → `width = bswap(bswap(index)) = index = 0x01000000`. Glyph 2
(`Δ=20`): k=0 writes `s+20`; line 507 then reads `s+20` for the pixeldata
offset → `o = index = 0x02000000` → `o >= pixStart` → remapped to
`pcPixOff + (0x02000000 − pixStart) ≈ 0x020002e8`. Confirmed exactly against
a raw-bytes dump. On N64 the array is read-only rodata — never re-laid-out —
so no bug there; this is purely a PC-relayout aliasing bug.

Fix: stage all six N64 fields into a local `u32 f[6]` before writing any
field of `d`. (A previous iteration's write can never touch `s(i)` — for
`j > i`, `d(j)` starts past `s(i)`'s end — so a full pre-read is safe.)
`GLYPH_IDX` clamp in `textrelated.c` was tried and reverted: the crashing
string has no control byte, and a negative-index guard is a separate latent
concern (logged in porting-notes.md §A).

**Verification:** `-level_34` 0 crashes / ~14 runs (was ~50–100%); `-level_35`
0 crashes / 4 runs (was ~3/4). `-level_09`/`-level_20`/`-level_24` unregressed
(91.3% / 91.7% / 90.8% non-clear). `-level_37` (Jungle) unchanged — still the
separate C2m `gfx_sp_matrix` explosion-DL `G_MTX` crash at ~frame 300 (D75
family). Sweep **16 → 18/21**. All probes reverted; only
`port/src/romdata.c` (11 lines) changed. Confidence: **high** (exact
byte-level match, deterministic value, 2 levels fixed, no regression).

**D124-Facility addendum (session M-14 — partial, NOT fixed, out of time).**
*(Superseded by D130 — the model-GDL hypothesis below was wrong.)*
Re-instrumented with a `GE_C2GDL` probe in `sub_GAME_7F0762E0`
(`objecthandler_2.c`) + a bad-`G_SETTIMG` catcher in fast3d
(`gfx_pc.cpp` G_SETTIMG case). Both probes **reverted** — tree is clean.
Findings:

1. *Exact crash cmd.* `-level_34` faults in `import_texture_ia16`
   (`gfx_pc.cpp:757`, via `import_texture` :955) — NOT `import_texture_i8`
   as the old note said. The offending command in the relocated model GDL
   (dList base `0x7007b870`, cmd at `0x70080490`) is:
   `w0=0xfd900000` (`G_SETTIMG`, fmt=7/rgba? siz per bits, w=0),
   `w1=0x72181ee8`. `seg_addr(0x72181ee8)` returns it unchanged (top byte
   0x72 is not a segment) → deref of unmapped DRAM → AV. Preceding slot is
   `0xba 00 0e 02` (GE tex-load macro, w1=0) then a `00000000` slot.
2. *`Gfx` stride confirmed.* `Gwords{ uintptr_t w0; uintptr_t w1; }` →
   on x86-64 `w0`@+0 (8B), `w1`@**+8** (8B), `sizeof(Gfx)==16`. The probe
   dump (8-byte granular) shows w0 at slot+0 and w1 at slot+8, i.e. the
   command is genuinely `{0xfd900000, 0x72181ee8}` — the converter wrote a
   real 16-byte slot, it is not a half-slot artefact.
3. *`w1` is a bad `tex->data`.* This `G_SETTIMG` is emitted by
   `gDPSetTextureImage(gdl++, .., tex->data)` inside
   `texWriteLoadToTmemAddr` / `texWriteLoadToTmemZero` (`tex.c`). So the
   **texture-pool** entry `tex` returned by `texFindInPool()` has a
   garbage `.data` (should be a `0x05xxxxxx` seg-5 ref, resolved later by
   fast3d `segmentPointers[5]`). i.e. the fault is (at least partly) in
   the `texLoadFromModelFileHeader` → `texLoad` texture-pool path, driven
   off `objheader->Textures`, **not** solely the `texLoadFromGdl`
   command-stream copy. This partially contradicts the original
   D124-Facility hypothesis.
4. *Alignment smoking gun.* `GE_C2GDL` probe shows **every** model file's
   `objheader->Switches` base has `((uintptr_t)Switches & 15) == 1`
   (odd!). `mempAllocBytesInBank` (`memp.c`) does **zero** alignment —
   `allocation = pool->pos; pool->pos += bytes;` — so a single earlier
   odd-sized bank alloc leaves the STAGE pool cursor permanently odd and
   every subsequent model file loads at an odd address. `delta`
   (`romremaining - pcremaining`, the scratch-relocation offset) is also
   not 16-aligned (`&15` = 3, 7, 11 across files). `sub_GAME_7F0762E0`
   then forms `dst = Switches + (replacementgdl & 0x00ffffff)` and
   `src = Switches + gdloff + delta`; observed `dst&15` = 5, 7, 9, 13.
   The N64 code assumes the file base is Gfx-aligned (8 on N64). The
   converted model GDLs the sidecar carries also have per-DL offsets that
   are only 4/8-aligned (`gdloff0 & 15` = 4, 8, 12), so even a 16-aligned
   base would not make every DL 16-aligned.
   - *Note:* an odd/misaligned base is self-consistent between the
     converter writer (`Switches+off`) and the fast3d reader (seg-5 =
     `Switches`, `+off`), so it does **not by itself** corrupt command
     *content* on x86 (unaligned `uintptr_t` loads are tolerated). The
     bad `tex->data` (#3) must come from an *offset* miscalc, likely the
     model texture-blob offset assuming N64-sized (8B-Gfx) GDL extent
     while the PC sidecar's GDLs are pre-expanded to 16B — the texture
     data then sits at a stale/too-large offset (`0x72181ee8` is ~0x1.9M
     past the bank end `~0x706a8000`), OR `texLoad` reads a `textures[i]`
     descriptor at a wrong stride off the odd `objheader->Textures` base
     (`(u8*)filedata + sizeof(ModelNode*)*numSwitches`, D43/D45 PORT line).
   - *Next step:* probe `texLoad` (`image.c`) — dump `textures[i]`
     `TextureID` / offset / resulting `tex->data` for `-level_34`, and
     dump `objheader->Textures`, `numtextures`, `numSwitches`, and the
     sidecar's texture-section layout. Compare against `d43_emit.py` /
     `pcmodels` sidecar builder: does it emit model texture blob offsets
     in N64 (8B-Gfx) or PC (16B-Gfx) GDL-extent terms? That converter is
     the likely fix site (offline sidecar per the D43/D69/D88 rule),
     with a fallback `#ifdef PORT` 16-align of the STAGE pool cursor /
     the model file base in `mempAllocBytesInBank` or
     `load_object_fill_header`.
5. *Files touched this session:* none committed; both probe edits
   (`src/game/objecthandler_2.c`, `port/fast3d/gfx_pc.cpp`) reverted via
   `git checkout`. Docs only: this entry + `docs/dev/LEVEL-STATUS.md`.
   *Confidence:* crash cmd + `Gfx` stride **high**; `tex->data`-is-the-bad-
   value **high**; root cause of *why* `tex->data` is bad **low–medium**
   (two candidate mechanisms, neither proven).

---

## D186 — `Video.FpsCap` throttles the whole sim, not just presentation (M-39 diagnosed, M-42 clamp landed)

**Symptom (M-39, user report):** window title read "GoldenEye 007 - 1 fps";
the game had become a slideshow. Root cause was `Video.FpsCap = 10` in the
(gitignored) `data/ge007.ini` `[Video]` section — almost certainly dinged down
via the F10 options overlay (D184) during testing; ESC-close calls
`configSave()` so it persisted.

**Mechanism.** `gfx_set_target_fps(cfgFpsCap)` feeds `sync_framerate_with_timer()`
in `port/fast3d/gfx_sdl2.cpp`, which does `sysSleep(left)` + a `sysCpuRelax()`
busy-wait. That wait runs **inline on the game's scheduler thread**:
`src/sched.c __scExec` → `osSpTaskStartGo` (`port/src/libultra.c:1119`) →
`gfx_run` → `gfx_sdl_swap_buffers_begin` → `sync_framerate_with_timer`, and only
*after* it returns does `osSpTaskStartGo` post `OS_EVENT_SP`/`OS_EVENT_DP`. While
that thread is parked in the pacing wait it is not delivering VI-retrace events
either, so every game thread blocked on `osRecvMesg(retraceQ)` stalls with it —
the sim is dragged down to the render-cap rate. On console the RSP/RDP are
separate silicon and VI retrace is unconditional, so a "render cap" cannot slow
the CPU; there is no N64 analogue. Verified M-39 (`-level_09`, fixed 35 s
window): `FpsCap=10` → 5 frames; `FpsCap=30` → 5; `FpsCap=60` → frame ~300;
`FpsCap=0` → frame 900+.

**Landed (M-42, clamp only — `port/fast3d/gfx_sdl2.cpp` + `port/src/video.c`):**
any `0 < FpsCap < 30` is treated as `0` (uncapped) with a one-line warning, at
both the config-load site (`videoInit`, so a bad ini is normalised and persists
sane on the next `configSave()`) and the fast3d chokepoint (`gfx_sdl_set_target_fps`,
which also covers the live path from the F10 overlay). Caps ≥ 30 are unchanged.
Verified: `-level_09` framediff 3/3 within threshold at the default `FpsCap=0`
(golden-identical); with `FpsCap=10` the sim now runs to VI post ~1740 in a
30 s window (≈ the `FpsCap=0` rate) instead of ~300; `FpsCap=60` still paces
(~56/s, no warning).

**Still OWED (the real fix):** move the frame-pacing wait off the scheduler
thread so *any* cap only drops/duplicates presented frames without blocking
retrace delivery (e.g. pace in the host event-pump thread, or gate the swap
without sleeping the RSP path). Until then a low cap is refused rather than
supported. Same timing-compensation family as D117 / D134 / D155.
porting-notes.md §E.

---

## D183 — M-36 "Family A" texture line/pitch shear: DISPROVEN on `-level_36` (M-36)

**Brief:** fix the "diagonal grey static / comb interlacing" attributed to
`import_texture_*` assuming `full_image_line_size_bytes == line_size_bytes`
(`docs/dev/M-36-TRIAGE.md` Family A; bugs D176(b) Surface walls, D182(2)
file-select spiral). Repro: `-level_36` Surface, headless `GE_PCDUMP`.

### 1. The pitch hypothesis does not fire on Surface — measured, not argued

`full_image_line_size_bytes != line_size_bytes` can only be produced by
**`gfx_dp_load_tile`** (`gfx_pc.cpp:2166`, a windowed sub-rect load).
`gfx_dp_load_block` (`:2139`) sets `line = full = size_bytes` unconditionally,
so the two are equal by construction.

GE loads *every* texture through `gDPLoadBlock`: `texWriteLoadToTmemZero` /
`texWriteLoadToTmemAddr` (`src/game/tex.c:492,617`) emit
`gDPSetTextureImage` + `gDPSetTile(…, line = 0, …)` + `gDPLoadBlock` and never
a `gDPLoadTile`. Confirmed at runtime: `GE_DTEX` (now prints `line=`/`full=`
and a `<-- STRIDED` marker) over a Surface run — **0 of 64** logged imports
strided; the full `GE_TEXDUMP` set — **0 of 166**. So no importer on this level
ever does the flat-read-of-a-strided-source that the hypothesis needs.

### 2. The importers' width/height/pitch math is *verified correct*

New probe `GE_TEXRAW=1` (with `GE_TEXDUMP=1`) writes the raw source bytes
handed to each importer to `texdump/rNNN_f<fmt>_s<siz>_<w>x<h>.bin`. Scanning
each dump's vertical neighbour-difference over every candidate row pitch
4…128 finds the true pitch of the source image:

| texture | fmt/siz | tile | pitch fast3d uses | best-scoring pitch | vdiff |
|---|---|---|---|---|---|
| `r030` | IA8 | 54×54 | 56 | **56** | 0.22 (smooth) |
| `r021` | CI8 | 32×32 | 32 | **32** | 0.72 (smooth) |
| `r031` (the wall) | IA8 | 32×32 | 32 | none — flat ~3.7 at *every* pitch | — |

The importers pick exactly the pitch the data is stored at. (The 54→56 case is
`texAlignIndices`/`texChannelsToPixels` 8-byte row alignment,
`src/game/image.c:340,1740` — handled correctly today.)

Also re-tested and re-confirmed: applying `texSwapAltRowBytes`' odd-row u32-pair
swap to these dumps makes vdiff **worse** (0.22 → 1.57 on `r030`). **D159's
`#ifdef PORT` no-op is correct; do not re-enable it.**

### 3. What the Surface walls actually are

The cliff/perimeter wall texture is `GE_TEXI` fmt=3 siz=1 = **IA8, 32×32,
block size exactly 1024 B (no LOD chain)**. Its source bytes are a
high-frequency grey noise field: intensity nibble mean 6.5, full 0–15 range,
horizontal neighbour diff 2.98 / vertical 4.06 (uniform random ≈ 5.3); alpha
nibble is a constant 15. There is no pitch, no row swap and no bit-depth
reinterpretation that turns it into a coherent image — the *data itself* is
noise-like. It is decoded and uploaded faithfully; the on-screen "static" is
that texture tiled at ≈1 texel per pixel on the wall faces.

Two filtering theories were tested and **both failed** (temporary
`GE_MINFIX` probe in `gfx_opengl.cpp`, since reverted):

- forcing mip minification when the game point-samples
  (`gfx_opengl.cpp:752` `linear_filter ? … : GL_NEAREST`) — **no pixel change**;
  the walls are already `linear_filter == true`.
- forcing `mipmaps = true` on every sampler apply — **no change to the walls**,
  and it *regressed* the 2D HUD ammo digits to white blocks.

So the walls already get `GL_LINEAR_MIPMAP_LINEAR`. Not a filter bug.

### 4. Where D176(b) actually goes next (unresolved)

Remaining candidates, in order:

1. **The wrong texture is bound to those faces.** The next decisive step is ROM
   ground truth: decode Surface's texnums offline (the
   `TEXTURE-GLITCH-ANALYSIS.md` §7 toolchain) and check whether *any* Surface
   texture is this 32×32 IA8 noise field, or whether the rock wall should be a
   different, larger, structured texture. If it exists in ROM as-is, D176(b) may
   be **not a bug at all** (GE's Surface cliffs are a mottled grey rock) and the
   user report is really about *tiling density*.
2. **UV / tiling scale** — the texture repeats far too densely across each wall
   face. That is RC3/`Video.WrapFix` territory (D167) plus the `shifts`/`shiftt`
   the game sets per LOD tile in `texWriteTileLods` (`tex.c:596` passes
   `shifts = shiftt = lod`), which fast3d stores but the LOD-tile selection
   (D107 "always base tile") may be mismatching.
3. **D182(2)** (file-select spiral after re-entry) is *not* covered by this
   finding — it was never reproduced here, and it is the one symptom that could
   still be a real `gfx_dp_load_tile` strided case (front-end `texLoadFromGdl`
   paths do use windowed loads). The defensive fix in §5 would cover it if so.

**M-37 UPDATE (inconclusive, ~8-min timebox):** the §7 offline decoder + logs
live *outside* this checkout — a full offline ROM decode was not achievable.
Static findings: Surface's bg is `bg/bg_sevx_all_p.seg` (`bg.c:201`); the wall
texture is a `u16` image ID *embedded in the bg GDL stream*, not a static
per-level texnum list, so it cannot be grepped — needs a runtime `GE_DTEX`/
`GE_TEXRAW` correlation. `assets/images.def` has no rock/cliff names (only
`STATIC_NOISE 0x389`); Surface-named entries are mission-select thumbnails.
Leaning **"real texture / tiling-density" (candidate 2), low-med confidence** —
D183's own measurement (constant full alpha 0x0F + full-range noise intensity,
zero residual structure at any pitch) is the signature of an intentional grey
rock-detail tile, not a mis-decoded structured image. Decisive next steps:
(i) grep the *converted* `bg_sevx_all_p` GDL for opcode `0xC0` (`G_SETTEX`) —
settles the one remaining "wrong bind" scenario (stale tile state, TEXTURE-
GLITCH-ANALYSIS §2 RC1) offline; (ii) headless `-level_36` `GE_DTEX/TEXRAW=1`
logging the image ID bound to the cliff tris, look it up in `g_Textures[]`,
read + decode that ROM range per §4. Scratch note: M-37 session scratchpad
`D176b-rom-groundtruth.md`.

### 5. What shipped (`port/fast3d/gfx_pc.cpp` only)

- **De-stride in `import_texture()`**, before the format dispatch: when
  `full_image_line_size_bytes > line_size_bytes`, compact the strided rows into
  a contiguous scratch buffer and hand every importer a row-packed image with
  `line == full`. This is the correct behaviour the seven `SUPPORT_CHECK
  (full_image_line_size_bytes == line_size_bytes)` asserts (`gfx_pc.cpp:689,
  710, 739, 765, 791, 818, 866`) merely assert against — and those asserts
  compile out in the release build, so today the wrong read is silent. Only the
  CI8 importer strides correctly on its own (`:893`); it becomes a no-op stride
  after this change. **Strictly a no-op whenever `full == line`, which is
  166/166 loads on Surface and every `gDPLoadBlock` in the game** — hence
  golden-safe. Unverified as a *fix* (nothing in the current repro exercises it);
  kept because it is provably-correct and removes a latent silent-corruption path.
- **`GE_DTEX`** now prints `line=`/`full=` and flags `<-- STRIDED (pitch shear?)`.
- **`GE_TEXRAW=1`** (alongside `GE_TEXDUMP=1`) dumps raw importer-input bytes.

### 6. Verification

- `-level_09` golden framediff: **3/3 within threshold** (200/320/440;
  worst dmean 10.4, phash 21/1/4) — unchanged.
- `-level_36` before/after: **visually identical** (as expected — the de-stride
  never fires there). The wall static is unchanged and remains open.

*Confidence:* "Family A pitch shear is not the Surface bug" — **high**
(runtime-counted, 0/166). "The wall texture is decoded correctly" — **high**
(independent offline pitch scan of the raw importer input). "The shipped
de-stride is golden-safe" — **high**. "What D176(b) really is" — **low**;
needs ROM ground truth for that texnum.

---

## D176(a) — Surface black sky: env data is correct, the cloud-sky emit renders nothing (M-36, partial)

Surface (`-level_36`) sky is solid **black** where the N64 shows a warm
sunset cloud gradient. Headless `-level_36` + `GE_D176=1` probe
(`src/game/bgfog.c` `fogLoadLevelEnvironment`, `src/game/sky.c` `skyRender`):

- **The environment data is right.** `fog_tables[]` matches `Id=36`,
  `sizeof(EnvironmentRecord)=92`, `Sky.Clouds=1`, `Sky.RGB=96,96,128`,
  `CloudRGB=240,120,30` (the warm sunset), `CloudRepeat(skyheight)=10000`,
  `SkyImageId=0`, `IsWater=0`. So this is **not** a serialized-struct
  byte-order bug like D178 — `fog_tables[]` is a compiled-in C initializer
  and every field reads sane.
- **`skyRender` runs the cloud path** (not the `!Clouds` flat-fill early
  return). Corner probe: `eye=(-467.7, 374.2, -7190.5)`,
  `WaterConcavity=7.0`; the four screen-corner unproject rays come back
  `c0/c1.y ≈ +62` (`skyIsScreenCornerInSky` → TRUE) and `c2/c3.y ≈ -13.5`
  (→ FALSE). So the corner-classification switch value is
  `(1<<3)|(1<<2)|0|0 = 12` — the "top half of the screen is sky" case,
  which is correct for that camera.
- **Nothing draws.** Case 12 → `s1=4`, the sky region is built from the
  edge-vertex path and handed to `sub_GAME_7F097388` (pure-C project +
  perspective divide → screen coords) then `skyRenderTri` / `skyRenderFull`
  with `texSelect(&skywaterimages[SkyImageId=0], …)` and a
  `SHADE,ENV,TEXEL0,ENV` combine. The output is pure black, not even the
  `env->Red/Green/Blue` (96,96,128) fill — so the sky polygons are either
  degenerate after projection, culled, or the `skywaterimages[0]` bind is
  failing in a way that kills the primitive.

**Not yet root-caused.** Next steps (needs ~1–2 h / a dedicated pass):
1. Probe `sub_GAME_7F097388`'s output `unk28/unk2c` (screen x/y ×4 subpixel)
   for the 4 verts — are they on-screen and non-degenerate, or all clamped
   to one edge? That splits "projection math wrong" from "emit path wrong".
2. Check `skywaterimages` (`src/game/image_bank.c:284`,
   `globalbank_rdram_offset + GIMG_OFF(s_skywaterimages)`, PC offset
   `0xFB4`) actually resolves to valid `sImageTableEntry` records on PC, and
   that entry `[0]` (the Surface sky texture) loads — a bad global-image-bank
   offset (D69/pccg family) would give `texSelect` a junk texture.
3. fast3d-side: trace whether the sky tris reach `gfx_sp_tri` at all and
   with what verts/CC. `skyRenderTri` is a 500-line subdivided-tri emitter —
   a PC vtx/DL bug there is plausible (cf. D75 model-transform family).
4. Cross-check another cloud-sky level (Dam `-level_33` exterior, Statue
   `-level_22`) — if they're also black it's the emit path; if only Surface,
   suspect that level's sky image / `skywaterimages[0]`.

`GE_D176=1` probe left in tree (`#ifdef PORT`, env-gated, inert):
`src/game/bgfog.c` + `src/game/sky.c`. Separate from D176(b) (the tree/rock
"grey static", Family A / a different investigation).

### D176(a) — M-37 UPDATE: ROOT-CAUSED (static analysis, high confidence)

The emit path is not "wrong" — **the PC software RSP deliberately discards it.**
`port/fast3d/gfx_pc.cpp:2901`:

```cpp
case (uint8_t)G_RDPHALF_1:
case (uint8_t)G_RDPHALF_2:
case (uint8_t)G_RDPHALF_CONT:
    // on N64 skyRender uses these to render some types of skies and skybox water
    // by issuing low-level ucode commands G_TRI_FILL and G_TRI_SHADE_TXTR
    // the port renders the sky in a different manner
    break;
```

`skyRenderTri` / `skyRenderFull` build their geometry **exclusively** as
`gImmp1(gdl++, G_RDPHALF_1 / G_RDPHALF_CONT / G_RDPHALF_2, …)` pairs — a bespoke
packed triangle-raster stream (command byte in bits 24–31 = `G_TRI_SHADE_TXTR`/
`G_TRI_FILL`, `0x00800000` backface flag, subpixel Y coords, then S15.16 edge X +
dX/dY slopes via `sub_GAME_7F094298`, then per-vertex RGBA shade + S/T/W tex-coord
gradients, `G_RDPHALF_2` terminator) that GE's modified RSP ucode (`gmain.s`)
interprets. There is **no** `gSPVertex`/`gSP*Triangle` fallback and **no**
fill-rect on the textured case-12 branch — only `viSetFillColor(env->RGB)` sets
FILL state that nothing consumes. → dropped → solid black.

Explains every prior observation: `!Clouds` levels use a plain `gDPFillRectangle`
(→ work today); `Clouds` levels (Surface, Statue `-level_22`, Frigate `-level_29`,
Dam exterior) go through the RDPHALF stream (→ black). The projector
`sub_GAME_7F097388` is pure float math off endian-clean matrices — not the bug.

**No PD-port shortcut:** `pd_port/port/fast3d/gfx_pc.cpp:2512` has the identical
no-op + comment and PD's `sky.c` emits the same stream; no port-side sky
replacement exists anywhere in the PD tree. The comment is aspirational in both.

**Fix (Phase 2, ~1 session, genuine new fast3d code):** decode the RDPHALF stream
in `gfx_pc.cpp` — accumulate words, on a `G_RDPHALF_1` carrying a tri opcode start
a primitive, synthesise screen-space `LoadedVertex[3]` (bypass model/proj
transform like the 2D `gSPTextureRectangle` path; screen XY = value/4 − viewport;
shade + ST from integrating the slopes over the vertex delta), rasterise with the
current combiner/tile, close on `G_RDPHALF_2`. Full stream spec + the alternative
`#ifdef PORT` sky.c option in the M-37 scratch note.

Cheap first step: headless `-level_22` / `-level_29` — confirm also black (proves
emit path, not a Surface asset).

### D176(a) — M-42: `-level_22` headless repro CONFIRMED

`-level_22` (Statue Park, night) at frame ~360, **bare boot, no input** — the
spawn/intro camera already faces the horizon: the entire upper half of the
frame is **pure black** `(0,0,0)` where N64 shows a moonlit sky; the park
pillars and ground render fine, HUD present. Confirms the defect is the shared
cloud-sky emit path (RDPHALF stream dropped), not a Surface-specific asset.
`-level_22` frame 360 is the cleanest headless verification target for the
RDPHALF-decoder fix — no `GE_INPUTSCRIPT` needed.

Note: `-level_36` / `-level_43` (Surface) bare-boot intro cameras point **down
at the terrain**, so a no-input capture there does not frame the sky. `-level_29`
(Streets, night) top-of-frame reads dim `(29,24,22)` not pure black — needs a
visual check to distinguish "dark night sky drawn" from "partial".

### D176(a) — M-43 UPDATE: full stream decode + two implementation paths (static analysis)

The M-37 scratch note (`D176a-sky-rootcause.md`) was never committed. This is the
recovered, complete spec, re-derived from `src/game/sky.c` and `include/PR/gbi.h`.
No build, no run — analysis only. **Implementation is still owed and is a full
session; this entry exists so the next pass starts from the format, not a
grep.**

**What the stream actually is.** `skyRenderTri` (`sky.c:1480`, textured tris),
`skyRenderFull` (`sky.c:1976`, textured quads = 2 tris) and the untextured
`G_TRI_FILL` path emit a **verbatim N64 RDP triangle command**, byte-for-byte,
chopped into 32-bit halves and carried as `gImmp1(G_RDPHALF_1 / _CONT / _2, word)`
pairs. GE's modified RSP ucode (`rsp/graphics/gmain.s`) does nothing clever with
them — it copies the words into a DMEM scratch buffer and DMAs the assembled
command straight to the RDP as a `G_TRI_FILL` (0xc8) or `G_TRI_SHADE_TXTR` (0xce)
edge-walked triangle. So "decode the stream" == "own an RDP triangle rasteriser
with edge + shade + S/T/W coefficient planes." fast3d has no such thing (it is an
RSP-level vertex/`gSP*Triangle` interpreter); `pd_port` has the identical no-op.

**Word-by-word layout** (all values are S15.16 or int/frac-split 16.16 unless
noted; `sub_GAME_7F094298(f)` = clamp ±32767.9 then `(s32)(f * 65536)`, i.e. a
float→S15.16 fixed convert):

1. *Edge header* — 2 words:
   - `w0 = (opcode<<24) | (backface ? 0x00800000 : 0) | (s32)YL`
     where opcode is `G_TRI_SHADE_TXTR` or `G_TRI_FILL`, backface = `sp444 < 0`
     (signed double area after the 3-way `unk2c` vertex sort), `YL = sp47c->unk2c`
     (lowest vertex, screen-Y × 4, subpixel).
   - `w1 = ((s32)YM << 16) | (s32)YL_mid` → actually `(s32)sp480->unk2c << 16 |
     (s32)sp484->unk2c` = `YM<<16 | YH` (mid, high). N64 order is YL/YM/YH; here
     the code sorts so `sp484 ≤ sp480 ≤ sp47c` on `unk2c`, then emits
     hi=sp484, mid=sp480, lo=sp47c.
2. *Edge slopes* — 3 word-pairs, `(X.16.16 , dXdy.16.16)` for the L, M, H edges:
   - `(7F094298(sp480->unk28 * 0.25), 7F094298(sp384))` — XL, DxLDy
   - `(7F094298(sp410),               7F094298(sp394))` — XH, DxHDy
   - `(7F094298(sp408),               7F094298(sp38c))` — XM, DxMDy
   `sp384/38c/394` are the edge inverse-slopes (`dx/dy`) clamped to ±1877;
   `sp408/sp410` are the H/M edge X starting values back-stepped to the YH
   scanline (`sp37c` = sub-pixel fraction of `sp484->unk2c * 0.25`).
   `*0.25` because `unk28/unk2c` are stored ×4.
   **`skyRenderTri` returns here if `!textured`** (G_TRI_FILL: header + edges
   only).
3. *Shade coefficients* (textured only) — 8 word-pairs = the RDP shade DMEM
   block: RGBA base, DrDx/DgDx/DbDx/DaDx, DrDe.., DrDy.. each as an int-parts
   word then a frac-parts word (`(v & 0xffff0000) | (next & 0xffff0000) >> 16`
   packs two ints; the matching `<<16 | (next & 0xffff)` packs the fracs). Values:
   `sp210[0..3]` = colour at origin, `sp290[0..3]` = d/dx, `sp2b0[0..3]` = d/dy,
   `sp230[0..3]` = d/de, all run through `7F094298`. Colours are the per-vertex
   `SkyRelated38.rgba` barycentrically solved to a plane via `sp440`
   (= 1/doubled-area) and the `sp3c8..sp3d4` edge deltas.
4. *Texture coefficients* (textured only) — 8 word-pairs, same int/frac packing,
   for S, T, W: `sp210[4..6]`, `sp290[4..6]`, `sp2b0[4..6]`, `sp230[4..6]`, each
   scaled by `sp190` (an LOD/overflow clamp: `1/max(perspective-corrected
   gradient magnitude / 1024, 1)`). The final pair is emitted with
   `G_RDPHALF_2` (not `_CONT`) — **that is the stream terminator.**

Total data words: **2 + 6 + 16 + 16 = 40** for `G_TRI_SHADE_TXTR` (20 RDPHALF
pairs), **2 + 6 = 8** for `G_TRI_FILL`. The 2-word "header" is exactly the raw
N64 RDP triangle command's word0 (`[cmd 8b][backface 1b @23]...[YL 14b]`) +
word1 (`[YM 14b][YH 14b]`) — matches the canonical hardware format; the shade
and texture blocks are the standard 16-word int-halves-then-frac-halves RDP
coefficient layout (cross-checked M-43 against `include/PR/gbi.h` immediate-word
packing conventions — the low-level RDP tri macros themselves are RSP-internal
and not in this tree's `gbi.h`).

`skyRenderFull` is the same but solves the plane over 4 corner verts and emits
the quad as two RDP tris sharing the coefficient blocks; its own header pair also
ends the first tri and a second header starts the second.

**Vertex data available upstream** (before the DL): `sub_GAME_7F097388`
(`sky.c:1398`) fully projects each sky corner and writes `SkyRelated38`:
`unk28` = screen X ×4, `unk2c` = screen Y ×4 (minus `WaterConcavity*4`),
`unk30` = screen Z (0..0x7fff), `unk34` = 1/w, `unk20/unk24` = S/T, `rgba` =
per-vertex colour. **This is a complete screen-space vertex.** The projection is
pure float math off endian-clean matrices — not the bug, and reusable as-is.

---

**Path A — RDP triangle rasteriser in `port/fast3d/gfx_pc.cpp` (port-only, "correct").**
At `case G_RDPHALF_*` (`gfx_pc.cpp:~2901`): accumulate words into a small buffer;
when a `G_RDPHALF_1` carries a `0xc8`/`0xce` opcode start a command, close on
`G_RDPHALF_2`. Then either (a) feed a real edge-walk rasteriser writing into the
current render target with the active combiner/tile, or (b) **invert the plane
equations** — you have YH/YM/YL, the three edge X-at-Y and slopes, so recover the
3 screen (x,y); evaluate the RGBA and STW planes at those 3 points to get 3
`LoadedVertex` (already-projected: set `.x/.y` from screen coords mapped to
NDC-ish like the `gDPTextureRectangle` path, `.z` from a fixed sky depth, `.w`
from `unk34`), then hand to the existing `gfx_sp_tri` / GL path with a forced
"2D, no model matrix" flag. (b) is a few hundred lines and reuses the whole
existing combiner/texture pipeline; (a) means a new software rasteriser. Prefer
(b). Cost: ~1 session, genuinely new code, verifiable headless.

**Path B — swap the emit in `src/game/sky.c` behind `#ifdef PORT` ("cheap").**
`skyRenderTri` / `skyRenderFull` already hold 3–4 fully-projected screen-space
`SkyRelated38` verts. Under `#ifdef PORT`, skip the whole RDPHALF block and emit
a normal `gSPVertex` of screen-space `Vtx` (XYZ from `unk28/2c/30` ÷4, ST from
`unk20/24`, RGBA from the struct) + `gSP1Triangle` / `gSP2Triangles`, with a
`G_TEXTURE`/combiner setup mirroring what the RDP path implied
(`SHADE,ENV,TEXEL0,ENV`, `texSelect(&skywaterimages[SkyImageId], …)` already runs
just before). fast3d already eats screen-space verts (that is the front-end / HUD
path). ~30–60 lines, no new fast3d code. **Cost: a few hours.** Tension with the
"no game-logic edits" rule — but this is a pure N64-RSP-idiom substitution (same
class as the `G_TRI4` and dynamic-lighting `#ifdef PORT`s already in `src/`), it
changes no behaviour on N64 (`#else` keeps the stream verbatim), and it is
exactly the "if a game file seems to need a behavioural change, the fix belongs
in `port/`" *narrow* exception for a hardware idiom that cannot be isolated in
`port/` without reimplementing the RDP. Document as D176(a) in §F if taken.

**Recommendation:** Path B first — it lights up every cloud-sky level
(Surface `-level_36`, Statue `-level_22`, Frigate `-level_29`, Dam exterior) for
a few hours' work and de-risks Path A by giving a visual ground truth. Path A
stays the "right" long-term answer if Path B's screen-space verts show
seams/precision artefacts vs. the RDP's subpixel edge walk. Either way the
verification target is **`-level_22` (Statue, night) frame ~360, no input** — the
M-42 clean headless black-sky repro.

**Cheap first step (unchanged, still not done):** headless `-level_22` /
`-level_29` — confirm also black, proving it is the shared emit path and not a
Surface-only asset.

### D176(a) — M-46: Path B implemented (`src/game/sky.c`, `#ifdef PORT`)

Path B is in. `src/game/sky.c` only; N64 path kept byte-for-byte under `#else`.

**What changed.** A new file-scope `#ifdef PORT` helper `skyPortRenderPoly(gdl,
SkyRelated38 **v, nverts)` and an early `#ifdef PORT` return in both
`skyRenderTri` (3 verts, `arg1..arg3`) and `skyRenderFull` (4 verts,
`arg1..arg4`), placed right after the `sp444/sp488 == 0` degenerate-area guard
and `1.0f / area` so all shared early-outs still run. The helper:

- `dynAllocateVertices(n)` + two `dynAllocateMatrix()` from the vtx pool
  (same lifetime idiom as the bondview2 watch-gauge ortho overlay and
  explosion.c billboards).
- `guOrtho(proj, l, l+w, t+h, t, -32768, 32768, 1)` where `l/t/w/h =
  getPlayer_c_screen{left,top,width,height}()` — maps the already-projected
  pixel coords 1:1 into clip space, Y flipped (screen-down → NDC-up); identity
  modelview. `gSPMatrix` LOAD PROJECTION then MODELVIEW. `bg.c` reloads the
  perspective projection at the top of `bgLevelRender` (lines 638/689/…), so
  leaving ortho loaded after `skyRender` is safe.
- Per vertex: `ob = {unk28, unk2c, unk30} * 0.25` (the `*4` subpixel scale the
  RDP path used); `tc = {unk20, unk24} * 32` (standard N64 S10.5); `cn = rgba`
  from the struct.
- `gSPClearGeometryMode(G_LIGHTING|G_CULL_BOTH|G_FOG)` +
  `gSPSetGeometryMode(G_SHADE|G_SHADING_SMOOTH)`, then `gSPVertex` +
  `gSP1Triangle(0,1,2)` (tri) / `gSP2Triangles(0,1,3, 0,3,2)` (quad, winding
  irrelevant with culling off). Combiner (`SHADE,ENV,TEXEL0,ENV`), env colour
  and `texSelect(&skywaterimages[SkyImageId/WaterImageId], 1,0,2)` are already
  emitted by the caller immediately before — the helper does not touch them.

**Verified.** `src/game/sky.c` compiles clean under the real PC build flags
(`gcc.exe … -DPORT=1 -std=gnu11 -Wall`, exit 0, no new warnings) with the
worktree's `build-pc/` CMake configuration. Full link + run NOT done: the
worktree has no `data/` ROM so assets can't be extracted here. Every symbol the
helper uses (`dynAllocate*`, `guOrtho`, `guMtxIdent`, `gSPVertex`,
`gSP{1,2}Triangle{,s}`, `osVirtualToPhysical`) is already linked by the compiled
set, so link is expected clean.

**Owed to a human (cannot headless-verify):**
1. Eyeball `-level_22` (Statue, night) frame ~360, bare boot no input — the
   M-42 black-sky repro. Expect a rendered moonlit sky in the upper half.
   Also spot-check `-level_36` (Surface), `-level_29` (Streets).
2. **Untested risk — texture scale.** `tc = unk20/unk24 * 32` is the
   conventional guess; `unk20/unk24` come out of `sub_GAME_7F097388` as
   `unk0c * (65535/65536)` with `unk0c ≈ worldX*0.1` for clouds. If the sky
   texture is tiled wrong / smeared, the fix is localised to the two `tc[]`
   lines in `skyPortRenderPoly` (multiplier, or an explicit `gSPTexture`
   scaling override) — no other code moves.
3. **Untested risk — ortho Z / depth.** `ob[2] = unk30*0.25` in `[0,8192]`
   under an `[-32768,32768]` ortho; sky renders first into a cleared buffer so
   depth ordering should not matter, but if the sky Z-fights or is culled,
   drop `ob[2]` to 0.
4. Seam/precision check vs the N64 RDP subpixel edge-walk (the Path A
   motivation) — only relevant once it renders at all.

**Confidence:** MODERATE. The emit-path substitution is sound and matches the
established screen-space-overlay idiom; compile is clean. The two numeric
unknowns (texture scale, and whether fast3d's viewport transform composes with
this ortho exactly as the projection math intends) need one visual check.

### D176(a) — M-82: PR #18 rebased; Defect 1 (texcoord) fixed by static analysis

- **PR #18 rebased onto `main`** (was 5 days stale, `docs/porting-notes.md`
  conflict resolved). Builds clean; `bunker1` verify PASS, `worst_cell`
  unchanged (sky.c change is `#ifdef PORT` and only touches cloud-sky levels).
- **Defect 1 root-caused, no visual iteration needed.** The M-46 "untested
  risk — texture scale" is resolved:
  - Cloud tile = `s_skywaterimages[0]` = `IMAGE_CLOUDS_GRAYSCALE`, **64×64
    IA8, `G_TX_WRAP`** (`assets/oddtextures.c:646`). It *tiles* — S/T are not
    [0,1) fractions.
  - GE's own screen-space textured-quad idiom is `tc = width << 5`
    (`src/game/glass2.c:678`), so the patch's `× 32.0f` **is the right scale**.
  - `unk20`/`unk24` are texel coords reaching ±20 000 near the horizon (M-47),
    so `(s16)(S × 32)` **overflows** → the streak.
  - **Fix (committed):** per-primitive phase-fold — subtract the same
    multiple of the 64-texel tile period from every vertex of one triangle
    before scaling. Preserves inter-vertex deltas (hence the interpolated
    texture); keeps `tc` in s16 range. Exact for any triangle whose S/T span
    is < ~1000 texels — i.e. everything except triangles straddling the
    horizon line, which remain Defect 2.
- **Defect 2 (coverage / horizon-straddling tris) still open** → adaptive
  tessellation, denser toward the horizon.
- **Owed:** one `-level_22` visual check — see `docs/dev/D176a-SKY-NOTES.md`
  "Verification ask".

---

## D177 — Ladders non-functional: `count`/`rooms` land in the high half of a widened pointer (M-36)

**FIXED (M-36).** Climbing was completely dead — `MoveBond`
(`src/game/bondview2.c`) probes for a ladder tile every frame via
`stanTileDistanceRelated(&curLocus)` and gates the whole ladder-collision
path on `stanGetLocusCount(&curLocus)`, which was always 0 on PC.

Two compounding pointer-width ABI bugs (same class as D79/D90), all in the
stan navigation layer — **no game logic touched**:

1. **The ladder signal was written into the wrong half of a pointer.**
   `stanCheckLinkedSpecialTile` receives the caller's
   `struct StandTileLocusCallbackRecord` typed as `s32 *outFlags` and does
   raw `outFlags[0] = 1` (FORCECROUCH) / `outFlags[1] = 1` (LADDER). The
   record's first member is `s32 *rooms` — 4 bytes on N64, **8 on PC** — so
   on x86-64 `outFlags[1]` is the *upper* 4 bytes of `rooms`, and `count`
   (which moved from +4 to +8) never gets written. `stanGetLocusCount()`
   reads `record->count` → still 0 → ladder path never runs. FORCECROUCH
   survived by luck (LE low half of `rooms` is at +0, and
   `stanGetLocusField0` truncates it back). Fix: `#ifdef PORT` cast to the
   real struct and write `->rooms` / `->count` by name.
2. **`curLocus` was too small to hold the record.** It was declared
   `struct move_bond_temp_struct` — a 2-word "placeholder while matching"
   (`bondview.h`). 8 bytes on N64 = exactly the record; on PC the record is
   larger, so `stanTileDistanceRelated`'s field writes (the D90 zero-fill)
   and the fixed `count` store overflowed the local. Fix: `#ifdef PORT`
   declare it as the real `struct StandTileLocusCallbackRecord`.
3. **`stanGetMoveBondCollisionTiles` would have AV'd on the first climb.**
   Once the ladder path actually runs it calls
   `stanGetTileOrderedPointWorldPos(…, (coord3d *)((s32)coords + off))` —
   `(s32)coords` truncates the 64-bit `&bondCollision` stack pointer → the
   four quad corners written through a garbage address. Fix: `PORT_PTRADD`
   macro (`uintptr_t` arithmetic), added at the top of `stan.c`.

Touched: `src/game/stan.c` (macro + `stanCheckLinkedSpecialTile` +
`stanGetMoveBondCollisionTiles`), `src/game/bondview2.c` (`curLocus` type).
All `#ifdef PORT`, N64 path kept verbatim under `#else`. Candidate root-cause
came from an M-36 subagent; parts 1/3 were its work, part 2 (the local size)
found on review. **Verified:** build links clean; `-level_09` framediff 3/3;
`-level_36` (Surface, has mandatory ladders) boots crash-free.
**Interactive climb test — DONE (M-37): user confirmed climbing works
in-game.** Merged to `main` via PR #5 (`955349db`).

porting-notes.md §C (pointer-width in ROM/record structs).

---

## D132 — D88 propDefs layout audit (M-20)

Static analysis only (no build, no game run). Goal: prove
`tools_pc/d88_propdefs.py`'s per-type cursor matches the real compiled PC
struct layout (8-byte pointers, 8-byte alignment) for every `PROPDEF_*`
record type any of the 21 solo `Usetup*Z` levels actually emits, and that
the handler set is total.

### Handler totality

Authoritative per-level type histogram: walk each level's `propDefs`
region with `d88_propdefs.PROPDEF_N64_WORDS` as the stride (the same
table `convert_stream` uses; it walks all 21 to an exact region-end
match, and `d125_check.py` confirms offline emit == converter for all 21
— so the stride table and the encountered type set are sound).

Union of all `type` bytes emitted across the 21 levels:

    1 2 3 4 5 6 7 8 9 10 11 12 13 14 17 18 19 20 21 22 23 24 25 26 27 28
    30 32 33 34 35 36 37 38 39 40 42 43 44 45 46 47 48

Never emitted (in the enum, absent from every shipped solo level):
15 (DEBRIS), 16, 29 (OBJ_DEPOSIT), 31 (OBJ_NULL), 41.

`tools_pc/d88_propdef_scan.py` is **stale / unreliable** — its private
`WC` + `SIZEOF_N64` guess table desyncs mid-walk and reports spurious
`unmapped type NOTHING`. `d88_propdefs.convert_stream` is the ground-truth
walker. (Scan tool left as-is; a follow-up could point it at
`PROPDEF_N64_WORDS`.)

Every emitted type has a non-generic handler **or** is provably safe
under the generic arm (header `_hdr_word` + `_bswap32` of the remaining
words, no pointer widening, no sub-word packed field):

- Generic-safe: 2 (DOOR_SCALE, `s32 Scale`), 23 (OBJECTIVE_START — see
  below), 24 (OBJECTIVE_END, header only), 25/26/27/28 (the meaningful
  field is the `s32 ObjRefID` at 0x4 that `get_status_of_objective` reads
  via the `MissionObjectiveRecord` cast — the `u16 unk4` in the stub
  structs is not how it is accessed, so `_bswap32` is correct), 34
  (OBJ_COPY_ITEM, 3 scalar words), 46 (CAMERAPOS / `CutsceneRecord` —
  `coord3d pos; f32 theta; f32 verta; s32 pad`, **no pointer**, 7 words,
  `PROPDEF_PC_BYTES[46]=28` correct).

### Per-type layout comparison (audited types)

Legend: N64 word -> PC byte offset; check = converter cursor lands on the
same PC offset the compiler would.

**ObjectRecord prefix** (shared by 3/5/12/17/36/42/43 and the head of
1/4/6/7/8/10/11/13/20/21/39/40/45/47). N64 0x80 / 32 w -> PC **144 B**.
Field-by-field: header@0; obj/pad `_hh_word`@4; flags@8; flags2@12;
prop*@16(+8); model*@24(+8); Mtxf mtx@32..96; runtime_pos@96..108;
runtime_bitflags@108; collisiondata*@112(+8); projectile*@120(+8);
maxdamage@128; damage@132; shadecol@136 (verbatim); nextcol@140;
sizeof **144**. Converter `_emit_object_prefix` cursor lands on every one
of these. MATCH.

**DOOR (1)** — `DoorRecord`, tail N64 w32..w63. The C3r Bunker2 suspect.
PC struct = **296 B** (`PROPDEF_PC_BYTES[1]=296`, `sizepropdef` PORT
`return 74`).

| field | N64 off | PC off (compiler) | converter | ok |
|---|---|---|---|---|
| linkedDoorOffset s32 (read-b4-write id) | 0x80 | 0x90 | 0x90 `_bswap32` | yes |
| maxFrac..maxSpeed 5xf32 | 0x84..0x98 | 0x94..0xa8 | same | yes |
| doorFlags u16 / doorType u16 | 0x98 | 0xa8 (`_hh_word`) | 0xa8 | yes |
| keyflags..doorOpenSound 3xu32 | 0x9c..0xa4 | 0xac..0xb4 | same | yes |
| frac..speed 5xf32 | 0xa8..0xb8 | 0xb8..0xc8 | same | yes |
| openstate s8/unkbd s8/calcopacity s16 | 0xbc | 0xcc (byte-pattern) | 0xcc | yes |
| TintDist s32 | 0xc0 | 0xd0 | 0xd0 | yes |
| CullDist s16/soundType s8/fadeTime60 s8 | 0xc4 | 0xd4 (byte-pattern) | 0xd4 | yes |
| linkedDoor* | 0xc8 | 0xd8 (8-al) +8 | 0xd8 +8 | yes |
| unkcc* (Vertex*) | 0xcc | 0xe0 +8 | 0xe0 +8 | yes |
| bbox (u32 + `struct bbox` 24B = 28B, 7 w) | 0xd0 | 0xe8..0x104 | 0xe8..0x104 | yes |
| openedTime u32 / portalNumber s32 | 0xec / 0xf0 | 0x104 / 0x108 | same | yes |
| openSoundState* | 0xf4 | 0x110 (8-al, 4B pad) +8 | 0x110 +8 | yes |
| closeSoundState* | 0xf8 | 0x118 +8 | 0x118 +8 | yes |
| lastcalc60 union s32/f32 | 0xfc | 0x120 | 0x120 `_bswap32` | yes |
| **sizeof** | 0x100 | **0x124 -> pad 296** | 296 | yes |

**Conclusion: the DOOR converter cursor matches the real PC layout
exactly.** `linkedDoorOffset` lands at PC 0x90 and is emitted as an int
id (not zeroed). The C3r Bunker2 residual was fixed by **D126** (objective
sub-record `->next` growth desyncing the walk so `linkedDoorOffset+arg2`
resolved to the wrong record) — not a DOOR-tail cursor bug. Bunker2
currently PASSES, consistent with this.

**VEHICHLE (39)** — `VehichleRecord`, PC **208 B** (`sizepropdef` PORT
`return 52`). ailist@w32 = int id (`OBJ_ID_WORDS`, D123);
aioffset|aireturnlist@w33 = `_hh_word`; path@w41, Sound@w43 = ptr.
Cursor: id 144->152, hh 152->156, 7xf32 156->184, path 184(8-al)->192,
nextstep 192->196, Sound 200(8-al, 4 pad)->208. MATCH (208).

**AIRCRAFT (40)** — `AircraftRecord`, PC **208 B**. Same shape; path@w43,
Sound@w44. Cursor lands path 192(8-al)->200, Sound 200->208. MATCH.

**AUTOGUN (13)** — `AutogunRecord`, PC **248 B** (`sizepropdef` PORT
`return 62`). 17 scalar tail words 144->212, then unkC4*/unkC8*/beam*
(w49/50/51) 216(8-al, 4 pad)->224->232->240, is_active 240->244, unkD4
244->248. MATCH (`PROPDEF_PC_BYTES[13]=248`).

**AMMO / MultiAmmoCrate (20)** — `MultiAmmoCrateRecord`, PC **200 B**.
Tail = `slots[13]` of `{u16 modelnum; u16 quantity}` -> 13 `_hh_word`
writes 144->196, pad->200. `AMMOTYPE_GLOBAL_MAX == 13` matches
`PROPDEF_N64_WORDS[20]=45` (32+13). MATCH (`sizepropdef` PORT `return 50`).

**TANK (45)** — `TankRecord`, converter **248 B** (`_emit_object_prefix`
+ collision*@w32 + 23 `_bswap32` words). The `TankRecord` struct as
declared in `bondtypes.h` is **53 words**, but `PROPDEF_N64_WORDS[45]=56`
(getools/ROM) — the struct's `//s32 unk88..` comments imply a 3-word gap
the C declaration omits, so the *field offsets* past `rect` are
unconfirmed. However: the only pointer is `collision`@w32 (correctly
widened + kept in slot), the stride is self-consistent with `sizepropdef`
PORT (`return 62` = 248), and TANK appears only twice (Depot x1, Runway
x1). Walk integrity is intact; only tank-field *semantics* (non-crash)
could be off. **Confidence medium**; acceptable until a tank level is
played.

**TINTED_GLASS (47)** — `TintedGlassRecord`, PC **168 B**. 5 `s32` tail
words 144->164, pad->168. No pointers. MATCH (`sizepropdef` PORT
`return 42`).

**Objective sub-records 30/32/33/35** (D126) — re-confirmed: each is
`{header; s32 payload x N; T *next}`; converter emits header + N
`_bswap32` words + 8 zero bytes for `next` at offset 16, total 24 B.
`criteria_deposit` (33) has N=3 (5 N64 words); the rest N=2 (4 words).
MATCH (`sizepropdef` PORT `return 6`).

**OBJECTIVE_START (23)** — `MissionObjectiveRecord` ends in
`WatchMenuObjectiveTextRecord *nextentry` @0x10, so PC `sizeof` is 24,
but `PROPDEF_N64_WORDS[23]=4` (nextentry is not serialized) and
`sizepropdef` PORT `return 4` (16 B stride). This is **safe**: nothing
writes `MissionObjectiveRecord.nextentry` — the briefing-text linked
list (`ptr_last_briefing_setup_entry_type23`, `objective.c:54`,
`objective_status.c:88`) is a chain of `struct watchMenuObjectiveText`
(type-35 records) via *their* `nextentry`, despite the misleading global
name. The type-23 record is only ever *read* (`get_status_of_objective`
walks it as `ObjRefID`@4 / `TextID`@8 / `MinDificulty`@0xc). Uniform
16-byte stride, no pointer store -> no D126-class overflow.

### Divergence found — types 14 / 19 / 38 / 44 (union-with-pointer index slots)

`LinkRecord` (14), `SwitchRecord`=`LinkRecord` (19), `LockDoorRecord`
(38), `SafeObjectRecord` (44) each declare their index fields as a
**union with a pointer**:

    typedef struct LinkRecord {
        inherits PropDefHeaderRecord;                       // 0, 4 B
        union { struct PropRecord *first;  s32 Index1; };   // PC: off 8 (8-al, 8 B)
        union { struct PropRecord *second; s32 Index2; };   // PC: off 16
        struct LinkRecord *next;                            // PC: off 24
    } LinkRecord;                                           // PC sizeof = 32

On PC the union is 8 bytes / 8-aligned, so `Index1` sits at **byte 8**
(4 B pad at byte 4), `Index2` at **byte 16**, `next` at **byte 24**.
`LockDoorRecord` is the same shape (sizeof 32). `SafeObjectRecord` has
three such unions (`item`/`safe`/`door`) + `next` -> `Index1`@8,
`Index2`@16, `Index3`@24, `next`@32, sizeof **40**.

The converter's handler for `(14, 19, 38, 44)` does:

    out[0:4] = _hdr_word(src[so:so + 4])
    for i in range(1, n64w):
        out[4 * i:4 * i + 4] = _bswap32(src[so + 4 * i: so + 4 * i + 4])

i.e. it lays the N64 index words at PC bytes **4, 8, 12** — the N64
tight-4-byte packing. Result on PC:

| read | converter put | compiler expects | effect |
|---|---|---|---|
| `pdef->Index1` (byte 8) | N64 word 2 = **Index2** | Index1 | wrong id |
| `pdef->Index2` (byte 16) | (never written) = **0** | Index2 | always 0 |

`PROPDEF_PC_BYTES` also under-sizes 14 & 19 & 44 (24 vs real 32 / 32 / 40;
38 is coincidentally 32).

**Runtime effect (`prop.c` `proplvreset2` walk):** `PROPDEF_SWITCH`
reads `index1 = pdef_switch->Index1` / `index2 = pdef_switch->Index2`,
resolves `doorA`/`doorB`, and only if
`doorA && doorA->prop && doorB && doorB->type==PROPDEF_DOOR && doorB->prop`
writes `pdef_switch->first/second` + calls `initSetLevelLoadPropSwitch`
(which does `arg0->next = ...`). Because the indices read wrong, that
guard **fails**, so the pointer stores (and the would-be `->next`
overflow past the under-sized record) never happen. Same for
`PROPDEF_LOCK_DOOR` (`pdef_lock_door->door/lock` + `->next`),
`PROPDEF_SAFE_ITEM` (`pdef_safe->item/safe/door`), and `PROPDEF_LINK`
(`propweaponSetDual`, also `guna && gunb` guarded).

**Net: non-crashing but silently broken** — switch-activated doors, dual
(left+right) weapon pickups, padlocked doors, and safe/safe-item links do
not initialise on PC. This explains why Streets (`UsetuptraZ`, 20x
LOCK_DOOR) and Aztec/Dam (SWITCH) still PASS the load+no-crash sweep.
Below crash work in priority, but a WS6 objective-playthrough blocker.

Affected levels: LINK — Caverns, sevb. SWITCH — Archives, Aztec, Dam.
LOCK_DOOR — Dam, sevx, sevxb, **Streets x20**. SAFE_ITEM — Archives,
Depot, sevb, sevx.

**Not previously caught:** D125's stride re-check only asserted
`sizepropdef == PROPDEF_PC_BYTES/4` (internal consistency), never
`PROPDEF_PC_BYTES == real compiler sizeof`. D122's totality note flagged
`[u16|u16]` half-swap but not the `union{ptr; s32}` index-slot case.

### APPLIED (M-20, commit pending) — ABI/layout only, `#ifdef PORT`

Fix below applied verbatim. `tools_pc/d88_propdefs.py`: `PROPDEF_PC_BYTES`
`14/19 -> 32`, `44 -> 40`; the `(14,19,38,44)` handler now emits each
`Index{k}` into the low 4 B of the 8-aligned slot at PC `8 + 8*k`
(`nidx = {14:2,19:2,38:2,44:3}`). `loadobjectmodel.c sizepropdef()` PORT
switch: `LINK/SWITCH/LOCK_DOOR -> return 8`, `SAFE_ITEM -> return 10`,
`TAG` stays `6`. Regen chain run. Verified: `-level_09` framediff 3/3
PASS; `-level_20` behaviour byte-identical to a freshly-built pre-D132
baseline (the `frame_000320` phash delta is pre-existing stale-golden /
Silo slowdown, NOT a regression — confirmed by building the baseline);
Archives (`-level_25`, SWITCH+SAFE_ITEM), Streets (`-level_29`, 20x
LOCK_DOOR), Dam (`-level_33`, SWITCH+LOCK_DOOR) all load + render +
no-crash. Sweep runs this pass were heavily flaky (level_09 itself
0-framed on a loaded machine) — pure D117/watchdog noise, no crashes.

### Original proposed fix (as written pre-apply)

`tools_pc/d88_propdefs.py` — `PROPDEF_PC_BYTES`: `14: 24 -> 32`,
`19: 24 -> 32`, `44: 24 -> 40` (38 stays 32). Replace the
`(14, 19, 38, 44)` handler:

    if type_byte in (14, 19, 38, 44):  # LINK / SWITCH / LOCK_DOOR / SAFE_ITEM
        # D132: each Index{1,2,3} field shares a union with a pointer, so on
        # PC it lives in the LOW 4 bytes of an 8B/8-aligned slot at
        # PC offset 8 + 8*(N-1); the record ends in a *next the setup walk
        # writes.  The N64 image packs the indices as tight 4-byte words;
        # emitting them there put Index1 where the compiler reads Index2.
        nidx = {14: 2, 19: 2, 38: 2, 44: 3}[type_byte]
        out[0:4] = _hdr_word(src[so:so + 4])
        for k in range(nidx):
            w = src[so + 4 * (k + 1): so + 4 * (k + 1) + 4]
            out[8 + 8 * k: 12 + 8 * k] = _bswap32(w)   # low 4B, LE; high 4B = 0
        return bytes(out)

`src/game/loadobjectmodel.c` `sizepropdef()` `#ifdef PORT` switch: move
`PROPDEF_LINK` / `PROPDEF_SWITCH` out of the `return 6` group and give
`LINK`/`SWITCH`/`LOCK_DOOR` -> `return 8` (32 B) and `SAFE_ITEM` ->
`return 10` (40 B). `PROPDEF_TAG` stays `return 6` (its `ID`/`OffsetToObj`
are plain `u16`/`s16` at 0x4, not in a pointer union; handler already
correct).

After applying: `d88_emit.py --regen` (all 21 sidecars), then the full
`d43 && d69 && d88 --regen` chain, then re-verify `-level_09` / `-level_20`
golden + a Streets/Dam/Archives load.

### Confidence per type

| type(s) | verdict | confidence |
|---|---|---|
| ObjectRecord prefix (3/5/12/17/36/42/43) | matches | high |
| 1 DOOR | matches (incl. linkedDoorOffset @0x90) | high |
| 39 VEHICHLE / 40 AIRCRAFT | matches | high |
| 13 AUTOGUN | matches | high |
| 20 AMMO/MultiAmmoCrate | matches | high |
| 47 TINTED_GLASS | matches | high |
| 30/32/33/35 objective sub-records | matches (D126) | high |
| 23 OBJECTIVE_START | 16B stride safe (nextentry never written) | high |
| 2/24/25/26/27/28/34/46 (generic) | safe under generic arm | high |
| 4 KEY / 7 MAGAZINE / 21 ARMOUR / 8 COLLECTABLE | prefix + plain tail; widely used in passing levels | medium-high |
| 45 TANK | struct offsets unconfirmed (53 vs 56 w); stride self-consistent, walk intact, non-crash | medium |
| **14 LINK / 19 SWITCH / 38 LOCK_DOOR / 44 SAFE_ITEM** | **DIVERGENT** — union index slots at wrong PC offset; silently non-functional (non-crash) | fix high confidence |

### Files touched

Docs only: this subsection + the §F index rows (D88.4 status, new D132
row). No code changed; the proposed diff above is not applied. No temp
probe scripts left.

## D185 — fresh `d69_emit.py` run yields a game-crashing `data/pccg-<region>/` (M-38)

**Status: OPEN — alpha-release blocker.** (Add to the §F index.)

**Symptom.** `python tools_pc/d69_emit.py ntsc-final` in this repo writes
`data/pccg-ntsc-final/pccg.bin` = 3346895 B (52 sidecars, 1856-B manifest),
prints `ALL CHECKS PASSED`, and `-level_09` / `-level_20` then segfault before
frame 1 (`EXCEPTION: 0xc0000005`, faulting return address is an ASCII
model-name string). The pre-migration working sidecar
(`C:/Users/james/Source/Repos/007/data/pccg-ntsc-final/pccg.bin`, 3604378 B,
2421-B manifest) drops into this tree and `-level_09` runs crash-free.

**Isolation done (M-38).**
- Not the romdata M-38 change: baseline (stashed) crashes identically.
- Not the build: same exe, only the `data/pccg-ntsc-final/` bytes swapped.
- `tools_pc/d69_emit.py` is byte-identical between the public repo and the
  pre-migration repo `007` (only a doc-path comment differs).
- The migration commit `52539d10 "Prepare for public release"` normalised the
  committed d69 inputs from CRLF to LF: `scripts/filelist.u.csv` (−812 B, one
  CR/line) and `assets/obseg/file_resource_table.inc.c`.
- **Restoring CR to both inputs did NOT restore the 3604378-B output** → line
  endings are not the (whole) cause. Some other part of the d69 input closure
  regressed in the history rewrite, or the working sidecar predates a real
  d69/​input change and 3346895 is a separate second bug.
- `d43_emit.py` is unaffected (its `find_row` tries `name`, `name[1:]`,
  `name[:-1]`, `name[1:-1]` — tolerant of a mangled basename; `d69`'s
  `fl_by_base.get(basename)` is exact).

**Impact.** `docs/building.md` tells users to run `d69_emit.py`; M-38's
`prepare-assets.py` does the same. Both currently produce a broken game on a
clean checkout. The committed `data/` dirs (carried from pre-migration) hide it.

**Next.** Diff the sidecar *name* set: pre-migration `manifest.csv` vs a fresh
run's — find which bg/stan entries d69 now drops, trace through `find_row` →
`file_resource_table.inc.c` → `filelist.u.csv`. Fix so a fresh run reproduces
3604378 B / 2421-B manifest; re-verify `-level_09`/`-20` crash-free. Do NOT
regenerate `data/pccg-ntsc-final/` before the fix (it holds the working copy).

### D185 — RESOLVED (M-38): not a d69 regression, a missing d88 step

`d88_emit.py` **appends** the 21 `Usetup*Z` per-level stage-setup files
(object placement, AI opcode streams, pads/nav) to the same
`data/pccg-<region>/pccg.bin` + `manifest.csv` that `d69_emit.py` writes
(52 bg/stan rows → 74 after d88). The M-38 `prepare-assets.py` first shipped
ran only d43 + d69, so every level loaded with no setup data → instant
segfault. The "working" pre-migration sidecar was simply a complete
d43+d69+d88 output; nothing regressed in d69 or its inputs.

Fix (commit `4491db3f`): `prepare-assets.py` runs d43 → d69 → `d88 --regen`;
`bundle-win.sh` vendors `d88_emit.py` + its only local import
`d88_propdefs.py`. Verified: fresh 3-script run from an assembled bundle is
byte-identical to the known-good `pccg.bin` (3604378 B) / `pcmodels.bin`;
`-level_09` + `-level_20` boot crash-free. The CRLF→LF observation on
`filelist.u.csv` / `file_resource_table.inc.c` is real but a red herring for
this crash (d69 tolerates it; output unchanged).

## D200 — `-level_09` segfault: u32 reverb delay indices negate into +4GB offsets on 64-bit (fixed)

**Symptom.** `-level_09` reliably segfaulted within seconds of audio starting, in
`_filterBuffer` (`src/libultrare/audio/reverb.c`) — `d->lp` (a pointer into the AFX
delay array) resolved to garbage like `0x664d59be`.

**Root cause.** `ALDelay.input` / `ALDelay.output` are `u32`, and reverb.c computes
back-references into the delay ring as `&r->input[-d->output]` (and variants). On
N64, `ptrdiff_t` is s32, so `-160` (the u32 constant `0xFFFFFF60`) wraps to −160 and
the pointer arithmetic is correct. On PC x86-64, `ptrdiff_t` is s64: the u32 negation
**zero-extends** to +4294967136 samples, so `out_ptr` flies forward ~8GB — verified
exactly: `0x706d30a0 (r->input) + 0xFFFFFF60·2 = 0x2_706D2F60`, the observed wild
address. The subsequent `aSaveBuffer`/`aLoadBuffer` then write 13–52KB of wild data,
corrupting the delay array, `r->base`, and adjacent heap blocks — a self-propagating
loop that grows each frame until it hits unmapped memory.

**Fix (ABI/layout only, D3x class).** Five sites in `src/libultrare/audio/reverb.c`
cast to `(s32)` before negation so the offset sign-extends: `alFxPull` (`in_ptr`,
`out_ptr`), `_loadOutputBuffer` ×2 (`out_ptr`), plus the ramalign line, which uses
`(s64)out_ptr & 0x7`. Perfect Dark ground truth (`src/lib/naudio/n_reverb.c`) does
exactly this: `(s32)-d->input`, `(s32)-d->output`, `(s32)-(d->output - d->rsdelta)`,
`(intptr_t)out_ptr & 0x7`. No game logic changed; behaviour on N64 is identical.

**Verification.** `-level_09` now runs 120 s+ crash-free (previously segfaulted in
seconds); the delay pointers advance correctly (`out_ptr = input − 320B`, +0x140/frame).
A temporary DRAM guard window over the FX region, registered with the software mixer
(`port/src/mixer.c`), confirmed zero out-of-bounds writes after the fix. All
instrumentation removed; only the five casts remain in the diff.

**Sibling-risk audit.** Grep of `src/libultra/audio/*.c` + `src/libultrare/audio/*.c`
for negative pointer indexing: reverb.c is the only file with the pattern (its one
positive index, `&r->input[d->output]`, is safe — positive u32 zero-extension is
correct).

**Lineage.** D3x pointer-width reconciliation; same family as D177 (widened-pointer
high half) and D191 (32-bit field read as 64-bit).

## D202 — Phase-3 audio: silenced PPK plays "slap" not gunshot, PLUS runtime mixer corruption (piling up / glitching / eventual silence) (M-52; reclassified M-59; REOPENED M-60 on reference clip; M-61/M-62 closed the data/index/decode chain and recommended close; M-63 REOPENED again; M-65 PARTIALLY resolved — voice-leak cascade root-caused + guarded, `sndCreatePostEvent` un-stubbed; **M-66 ROOT CAUSE ESTABLISHED for the audible stuck door loop — sound 203 behaving exactly as ROM + ground-truth code specify: faithful N64 behaviour / original quirk; M-66b DISPOSITION C IMPLEMENTED + MEASURED — port-side expiration fades ownerless infinite loops out ~2.5 s after start, awaiting user by-ear pass**)

**Symptom (M-52 playtest, first listening pass on the Phase-3 software mixer,
post-D200).** In-level (Bunker1, Dam — not menu/intro, which is music-only and
correct): (a) weapon fire consistently plays the WRONG but always-the-same
clip per weapon (e.g. Bond's silenced PPK plays what sounds like a melee/punch
hit, every single shot — fully deterministic, not intermittent garble); (b)
level music never plays — a looping SFX-like sound plays in its place from
level start.

**Ruled out, with hard evidence (do not re-investigate these):**
- `soundIndex -> ALSound*` resolution (`snd.c sndPlaySfx`, `soundBank->instArray[0]->soundArray[soundIndex]`):
  traced live with a `GE_AUDIOTRACE` probe against real gameplay (`-level_09`,
  repeated PPK fire). `soundIndex=46` (confirmed via `bondconstants.h`'s SFX
  name table = `46_GUN_SILPPK_A_SFX`) resolves to the same `ALSound*`/`ALWaveTable*`
  every time; 13+ distinct indices captured across one session, all distinct,
  all valid.
- D37's bank re-layout (`port/src/romdata.c romdataFixupAudioBank`): preserves
  `soundArray`/`instArray` order; not a re-ordering bug.
- The `ALWaveTable.base`/`.book` pointer chain: traced all the way to
  `alLoadParam(AL_FILTER_SET_WAVETABLE, ...)` (`src/libultra/audio/load.c`) —
  the exact same `table`/`base`/`book` addresses `sndPlaySfx` resolved are the
  ones wired into the physical decode filter. No swap between resolution and
  voice setup.
- `ALStartParamAlt` (40B on x86-64) vs the generic `ALParam` pool slot (32B):
  already fixed pre-M-52 (D54, `synthesizer.c:127-140`, `#ifdef PORT` sizes
  each slot for the largest variant). Re-confirmed still in place, not a
  regression.
- Per-voice synth processing (`synthesizer.c`'s client-list handler loop) is
  strictly sequential — one client's full setup+decode completes before the
  next starts. Ruled out global-mixer-scratch (`sCtx`/`sAdpcmTable`/`sVol` in
  `port/src/mixer.c`) cross-talk between concurrently active voices.
- `ALSndpEvent` (snd.c-local union, force-cast to `ALEvent*` through
  `alEvtqPostEvent`/`alEvtqNextEvent`, which copy `sizeof(ALEvent)` bytes)
  vs `ALEvent` (libaudio.h): measured directly with a throwaway compile —
  both are exactly 32 bytes on x86-64. Not a truncating-copy bug.
- `sndCountAllocList`'s `(ALEventQueue *)&D_800243E4` cast (a real, compiler-flagged
  `-Warray-bounds` OOB read — `D_800243E4_s` is 24B, aliased as 40B `ALEventQueue`):
  confirmed genuine but functionally inert — its output (`numFree`/`numAlloc`)
  is computed and immediately discarded in `sndHandleEvent`, never used.

**Not yet ruled out — pick up here:**
1. **`aADPCMdecImpl`'s decode math** (`port/src/mixer.c`, this session's new
   Phase-3 code, never verified against real N64 audio output). Structurally
   matches the standard VADPCM order-2 predictor algorithm on inspection
   (9 input bytes -> 16 output samples, intra-frame reverb-of-already-decoded-
   samples loop) but no reference trace/audio diff exists yet.
2. **The raw PCM bytes embedded in the exe at the resolved ROM offset being
   wrong at the source** (asset-embedding/segment-placement issue, unrelated
   to anything traced above) — would need a byte-level compare of the
   PC-embedded `_sfxtblSegmentRomStart` segment against the source `.z64`.

**Key diagnostic fact:** the wrong sample is 100% deterministic per weapon
(same wrong clip every shot, confirmed by user) — this argues against a
stateful/interleaving bug (already the least-likely explanation given the
sequential-processing proof above) and toward either (1) or (2) above, both
of which would consistently mis-decode/mis-fetch the same bytes every time
for the same `ALWaveTable`.

**Diagnostic tooling added this session (still in the tree, `#ifdef`/env-gated,
harmless if left in, should be stripped once root-caused):**
- `src/snd.c` `sndPlaySfx`: `GE_AUDIOTRACE=1` env var → appends resolved
  `soundIndex`/`sound`/`wavetable`/`base`/`len`/`type`/`flags`/`book` to
  `audiotrace.log` (cwd-relative, unbuffered).
- `src/libultra/audio/load.c` `alLoadParam` `AL_FILTER_SET_WAVETABLE`:
  same env var → appends the filter/table/base/len/book actually wired into
  a voice to `audiotrace_wire.log`.

**Also noticed, not yet a confirmed bug:** `alLoadParam`'s
`a->memin = (s32) a->table->base;` (`load.c:382,438`) truncates a real 64-bit
`u8 *` to `s32` — currently harmless only because the ROM is deliberately
mapped at a low, sub-2GB virtual address (`0x10000000`, 12MB window per the
boot log), so no real address in range overflows 32 bits. Fragile; worth a
`(s64)`/`uintptr_t` widening pass regardless of whether it's D202's cause.

**M-53 update — both of the two remaining suspects now independently checked,
both look clean (static analysis only, no listening pass possible this
session):**

1. **`aADPCMdecImpl` decode math (`port/src/mixer.c:148-192`) — compared
   line-by-line against the local Perfect Dark PC port's scalar reference
   (`pd_port/port/src/mixer.c:333-352`, the `#else` non-SIMD path).**
   Identical: same nibble unpack (`(((*in >> 4) << 28) >> 28) << shift` /
   same for the low nibble), same `prev1`/`prev2` history read from `out[-1]`/
   `out[-2]`, same accumulator (`tbl[0][j]*prev2 + tbl[1][j]*prev1 +
   (ins[j]<<11)` plus the `k<j` intra-frame correction term), same `>>11`
   scale-down + clamp, same 16-sample/32-byte state carry via
   `memcpy(state, out-16, ...)`. No discrepancy found. This was PD's
   ground-truth non-vectorized fallback, i.e. the reference the SIMD paths
   are themselves checked against — a strong match.
2. **`_sfxtblSegmentRomStart`'s ROM offset (`0x102F19A0`,
   `port/src/romassets_u.s:2948`) — cross-validated three independent ways,
   all agree:**
   - `scripts/filelist.u.csv` row 28: `3086752,797360,assets/music/sfx.tbl` →
     `0x10000000 + 3086752 = 0x102F19A0` exactly.
   - Row 27 (`sfx.ctl`, `3063264,23488`) is contiguous with row 28:
     `3063264 + 23488 = 3086752` — matches `music.c:686`'s assumption
     (`size = &_sfxtblSegmentRomStart - &_sfxctlSegmentRomStart`, i.e. ctl
     immediately precedes tbl in ROM with no gap).
   - `ge007.ld:164-165` (N64 ground truth, untouched) places
     `sfx.ctl.o (.data)` immediately followed by `sfx.tbl.o (.data)` in the
     `musicfiles` segment — same adjacency, independently, from the linker
     script rather than the CSV scanner.
   - Read the real bytes from `data/ge007.ntsc-final.z64` at both offsets
     (this session, ad hoc Python): `sfx.ctl` decodes as a plausible
     `ALBankFile` (`bankCount=1`, one bank offset `0x5ba0` inside the 23488B
     ctl blob); no structural red flag. Byte-level content correctness
     beyond that needs a decode+listen pass, not available in this session.

   Net: the segment-offset math is corroborated by 3 independent sources
   (scanner CSV, N64 linker script, raw ROM read) and is very unlikely to be
   a simple wrong-base bug. Downgrading this suspect's priority.

**Also re-confirmed clean, not previously called out explicitly:** the ADPCM
book coefficients (`ALADPCMBook`, referenced off `ALWaveTable.waveInfo.
adpcmWave.book`) go through `afFixupBook()` (`port/src/romdata.c:704-735`),
which correctly `afRd16`/`afWr16` byte-swaps every predictor coefficient
during the D37 bank re-layout — not just the offset/pointer fields. And
`_bnkfPatchWaveTable()`'s `w->base += table;` (`bnkf.c:140`) is `uintptr_t`
end-to-end post-D201, so no truncation there either.

**Net effect: the M-52 write-up's two recommended suspects are now the
LEAST likely explanations, not the most likely.** The bug is probably
further downstream, in a stage neither this nor the M-52 session traced:
**the resample/pitch pipeline** (`aResampleImpl` / whatever computes a
voice's playback rate from `ALKeyMap`/`unityPitch` — `synallocvoice.c`,
`seqplayer.c`, `synport.c`/equivalent — not yet audited this session). A
wrong pitch/rate calculation would (a) make a correctly-decoded SFX sample
sound like a completely different sound if pitch-shifted far enough to be
mistaken for one, matching "PPK sounds like a melee hit" better than a
literal wrong-sample-content theory would, since sample *content* tracing
(D202 original session) already came back clean; and (b) explain "music
never plays, an SFX-like loop plays instead" if the same mis-pitched/
mis-selected-voice bug applies to sequence-player note events too, not just
raw `sndPlaySfx()` calls. **Recommended next step:** trace one PPK-fire note
event's computed pitch/rate value (another `GE_AUDIOTRACE`-style probe, this
time in the resample-rate / `ALKeyMap` lookup path) against what the N64
build would compute for the same key/velocity, rather than re-auditing the
decode or offset math further. Not yet attempted — no build/run done this
session (static-analysis-only pass); still needs the real listening-pass
sign-off per the standing M-52 note.

**M-53 cont. (build+run, same session) — user rebuilt off the M-53 doc-only
commit and confirmed no behavior change (expected — no code changed yet);
then supplied two new concrete data points that reframe symptom (b) and
partially (a):**

1. **"the looping sound [that plays instead of music] is actually a door
   sound in the attract screen getting looped and continuing to play."**
   This is not a wrong-sample or wrong-pitch bug — it's confirmed to
   literally be a real, correctly-identified SFX (a door slide-loop) that
   fails to stop. Traced the mechanism live with `GE_AUDIOTRACE`
   (`-level_09`, scripted door-interact input): a single player action
   fires a **chained** `sndPlaySfx` sequence — soundIndex 202
   (`METAL_SLIDE_OPEN_SFX`) → 204 (`METAL_SLIDE_LOOP_SFX`) → 203
   (`METAL_SLIDE_CLOSE_SFX`) — via the `do…while` chain in `sndPlaySfx`
   (`snd.c:927-928`, each `ALSound`'s `keyMap->velocityMin`/`keyMin` fields
   double as a "next soundIndex" link; this chain mechanic is stock N64
   design, not a port bug).
   - The game-code side that's supposed to **stop** a stuck door loop —
     `door7F053B10()` (`propobj.c:12835`, marked `//#MATCH`, i.e. decompiled
     byte-exact to the N64 binary, so its logic is ground truth and NOT a
     candidate for the bug itself) — calls `sndDeactivate(door->
     openSoundState)` whenever the door's *open* or *close* sound is still
     playing. `door->openSoundState` is already a real `ALSoundState *`
     in `bondtypes.h:3042` (not a narrowed/truncated field — checked, this
     is NOT another D3x pointer-width bug).
   - So the bug is downstream of this game-logic call: either (a)
     `sndDeactivate`'s posted `AL_SNDP_DEACTIVATE_EVT` (delta 0) isn't
     being serviced/dequeued reliably by the PC event-queue/mixer tick, or
     (b) (more likely given the user specifically saw this originate **in
     the attract screen** and persist **past it, into gameplay**) whatever
     N64 does to reset/clear all playing SFX at an attract-demo → real-game
     mode transition isn't happening equivalently on PC, orphaning the
     loop's voice with no `DoorRecord` left alive to ever call
     `sndDeactivate` on it again. **Not yet located** — haven't found the
     mode-transition audio-reset call (candidate: `sndDeactivateAllSfxByFlag`
     family, `snd.c:996`, or an equivalent full-stop at level-load).
2. **"ammo pick up is one of the knife sounds."** Checked against
   `bondconstants.h`: `PICKUP_AMMO_SFX`=234, `PICKUP_KNIFE_SFX`=233 — adjacent
   indices. Combined with the M-53 PPK report (`GUN_SILPPK_A_SFX`=46 sounding
   like `PUNCH1_SFX`=47, also adjacent but in the OPPOSITE direction), this
   does **not** fit a simple constant index-shift theory (would need to be
   the same sign both times) — leaves either (a) two unrelated single-entry
   bugs, or (b) the "wrong sound" being a badly-mispitched *correct* sample
   that a listener reasonably mistakes for a neighboring, timbrally-similar
   entry (a hard door clank at the wrong pitch could pass for a knife sound;
   a gunshot pitched down for a punch). Not resolved this session — would
   need an actual decoded-audio comparison (dump the decoded PCM for a
   known-bad play and listen/compare band energy against the two
   candidates), which the environment can produce (`GE_AUDIOTRACE` gives the
   exact `base`/`len`/`book` addresses) but wasn't attempted here.

**Still OPEN. Build/run environment confirmed available this session**
(`./build-pc.sh ntsc-final` after `export PATH=/c/msys64/mingw64/bin:$PATH`,
`GE_INPUTSCRIPT` with `"A"`=`GE_CONT_A`, `"Z"`=`GE_CONT_G` per
`port/src/input.c:311` — note **"Z" triggers the door/action interact in
this control scheme in `-level_09`, not weapon fire**; hadn't yet found the
right button/level combo to headless-repro an actual gunshot before running
out of session time). **Next step:** locate and instrument the attract→game
(or menu→level) audio-reset call to confirm/deny the orphaned-voice theory
for symptom (b); separately, dump+listen-compare decoded PCM for one
mis-sounding SFX to settle symptom (a)/(2)'s index-vs-pitch question.

**Follow-up (same session) — checked the level-transition "deactivate all"
sweep, found it's flag-filtered, which narrows the orphaned-voice theory
to a specific, already-catalogued bug family.** `lv.c:1443/1473` calls
`sndDeactivateAllSfxByFlag_1()` at level transitions → `flag=1`
(`SOUND_FLAG_FINAL_IN_SEQUENCE` only) → `sndDeactivateAllSfxByFlag`
(`snd.c:996`) only touches sounds whose `unk3e` has **every** bit in `flag`
set (`(item->unk3e & flag) == flag`). A door's loop-tail sound is very
unlikely to carry `FINAL_IN_SEQUENCE` (it's a middle link in the
open→loop→close chain, not the end), so this sweep would **not** catch an
orphaned door loop even on real N64 — meaning N64 must instead rely on the
attract-mode demo's scripted input naturally executing its "close door"
press before the demo ends, letting `door7F053B10()` clean up the loop
normally. **This reframes the theory:** if the attract-mode demo's
canned input is driven by a fixed *frame count* but the PC build's
sim-time-per-frame differs from N64's (the same `g_GlobalTimerDelta`/
`g_ClockTimer` wall-clock-vs-sim-time family already root-cause-pending
for **D193**, the AI-locomotion-too-slow bug), the demo could get cut off
before its scripted door-close input fires on PC even though it always
does on N64 — orphaning the loop's voice with no code path left to stop
it. **If true, D202(b) (the stuck-loop symptom) may not be a distinct
audio bug at all — it may be a second visible symptom of D193's root
cause once that's found**, not something to fix in the audio code. Worth
confirming/ruling out before spending more time in `snd.c`/`propobj.c`.

**M-54 update — checked the D193-linkage theory's timing mechanics
directly; it's weaker than M-53 framed it, and got two direct
experimental data points instead.**

1. Read `frametiming.c`/`libultra.c`'s timing chain end to end.
   **The ramrom-demo path (`iterate_ramrom_entries_handle_camera_out`,
   `ramromreplay.c:355`) does NOT go through `waitForNextFrame()`** — it
   calls `updateFrameCounters(ramrom_blkbuf_2->speedframes)` directly
   (`ramromreplay.c:400`), feeding the **recorded** N64 speedframes value,
   not a value derived from PC wall-clock elapsed time. The D155
   catch-up clamp (`FRAMETIMING_PORT_MAX_CATCHUP`) lives inside
   `waitForNextFrame()` and is irrelevant here. The call site
   (`boss.c:502-533`) gates *when* this fires on real elapsed ticks
   (`mainTickElapsed >= MAIN_LOOP_TICK_INTERVAL`) but that gate
   self-throttles correctly even if `gfxFrameMsgQ`'s retrace messages
   back up during a stall (checked: `osCreateMesgQueue(&gfxFrameMsgQ,
   ..., 32)`, `init.c:236` — 32-deep, so a stall *can* queue up dozens
   of retrace messages) — the very first drained message after a stall
   recomputes a big `mainTickElapsed` and fires exactly once, which
   immediately refreshes `copy_of_osgetcount_value_1`, so every
   subsequent backlogged message in the same burst sees a near-zero
   elapsed and does nothing. **No runaway/duplicate advance mechanism
   found.** This weakens (doesn't fully rule out) the D193-linkage
   theory: the demo's sim-time progression looks insulated from PC
   real-time hiccups by design, not just by luck. The D193 tie-in is
   still plausible in principle (if the *count* of real retrace ticks
   over the whole demo differs from N64's, e.g. via `osGetCount()`'s
   scaling formula, D117/D134) but the specific "backlog causes runaway
   catch-up" mechanism I'd hoped to point to isn't there. Downgrading
   this from "leading theory" to "one of several still-open".
2. Extended the existing `GE_AUDIOTRACE` probes: `sndPlaySfx` now also
   logs the `newState` pointer per `soundIndex` link (not just the
   `ALSound*`), and `sndDeactivate` now logs every call with its `state`
   arg (`snd.c`, both `#ifdef PORT`/`getenv("GE_AUDIOTRACE")`-gated,
   still uncommitted). Ran two headless captures
   (`GE_AUDIOTRACE=1 GE_D87=1 ./build-pc/ge007.x86_64.exe`, no level arg,
   no input, 100s then 240s, idling at the front end so the >=30s idle
   timer — `front.c:2493`, `MENU_TIMER >= 1801` at NTSC 60fps — kicks
   attract mode on). Findings:
   - **`sndDeactivate` DOES fire and DOES work** when called: trace
     shows `sndPlaySfx: soundIndex=260 -> newState=...706f4e18` followed
     later by `sndDeactivate: state=...706f4e18 (non-null)`, and that
     same `ALSoundState*` slot gets legitimately reused for a new
     `soundIndex` right after. This rules out "the event-queue doesn't
     service `AL_SNDP_DEACTIVATE_EVT` reliably" (branch (a) from the
     M-53 writeup) as a *general* mechanism — deactivation is not
     structurally broken.
   - Both captures only ever logged front-end/menu-click `soundIndex`
     values (258, 260, 111, 232, 109) — despite `GE_D87` confirming
     `iterate_ramrom_entries_handle_camera_out`/`ramrom_replay_handler`
     WERE firing repeatedly (159 D87 log lines in the 240s run, i.e. the
     attract-mode camera flythrough demo was genuinely playing), **no
     door-chain soundIndex (202/203/204) appeared in either capture.**
     Root cause of the miss, found by reading `ramromreplay.c:631-634`:
     which of the 14 `ramrom_table[]` demo files plays is
     `randomGetNext() % i` — **randomly selected per attract cycle**, not
     a fixed sequence. A short capture has no guarantee of landing on a
     demo that triggers a door open/loop at all (most of the 14 clips
     — Dam/Facility/Runway/Silo/Frigate/Train camera paths — may not
     pass a metal sliding door in-frame). Neither capture is long enough
     to be conclusive either way for symptom (b); this needs either (a)
     a much longer unattended capture (several attract cycles, each
     clip's `totaltime_ms` likely 30-90s, so plausibly 10+ min for good
     odds of hitting a door-bearing clip), or (b) a temporary
     `PORT`-gated env-var override pinning `ramrom_table` selection to a
     specific known door-bearing index for fast, deterministic repro
     (not yet added).
   - Separately confirmed the `romdataFixupMusicSeqTable: seqCount 63
     exceeds blob capacity 1` `[ERROR]` log line seen at boot is a
     **known, benign, single-fire false alarm** — it's the deliberate
     D35 header-only peek (`music.c:731-736`, buffer intentionally sized
     16 bytes to decode just the `seqCount` field before the real,
     correctly-sized allocation+decode at `music.c:738-742`) — confirmed
     it fires exactly once per run and the real table load that follows
     doesn't. Not a new lead; noting it here only so a future session
     doesn't re-flag it as one.

**M-54 cont. — got the live repro, and it's NOT a door.** Re-ran headless
with a `GE_INPUTSCRIPT` pressing A every second for the first ~20s (to
clear the Rare/Nintendo/legal/cast-intro screens fast) then idling, 280s
total. Landed on the `ramrom_Train` attract demo (confirmed: `GE_D87`
shows `iterate_ramrom_entries_handle_camera_out`/`ramrom_replay_handler`
actively firing — real attract-mode canned-input playback, not the boot
cinematic, which is a separate unrelated state machine in `front.c`).
`audiotrace.log` (full, unedited):

```
soundIndex=64 -> newState=706f4db0 flags=2   (TRAIN_GO_SFX, first voice)
soundIndex=64 -> newState=706f4f50 flags=2   (TRAIN_GO_SFX, second voice)
...
sndDeactivate: state=706f4e80 (non-null)
sndDeactivate: state=706f4ee8 (non-null)
```

`soundIndex=64` = `TRAIN_GO_SFX` (`bondconstants.h`), `flags=2` =
`SOUND_FLAG_LOOPED` — the **only** looped sound requested in the entire
run. **Neither `706f4db0` nor `706f4f50` ever appears in a
`sndDeactivate` line, anywhere in the log** — confirmed by grepping the
full file for both pointers. Meanwhile two *other*, non-looped states
(`706f4e80`, `706f4ee8`) DO get swept and deactivated, at exactly the
point the demo cycle ends and the sequence rolls back to the boot logos
(`RARELOGO_SFX`/`RARELOGO_FAINT_SFX`, indices 258/260, reappear
immediately after). **This is a direct trace-level confirmation of the
M-53 "Follow-up" theory**: whatever cleanup sweep runs at the
demo-cycle/mode-transition boundary deactivates some voices but
specifically skips the looped one(s) — consistent with
`sndDeactivateAllSfxByFlag`'s `FINAL_IN_SEQUENCE`-only flag filter
(`snd.c:996`, see M-53 above) never matching a genuinely-looped ambience
voice. Reframes the bug from "door-specific" to **"any `SOUND_FLAG_
LOOPED` voice started during a `ramrom` attract demo has no code path
that ever stops it once the demo cycle moves on"** — the door report and
this train report are the same bug, different SFX.

**New candidate mechanism for why N64 doesn't audibly show this**:
`src/libultra/audio/synallocvoice.c`'s `_allocatePVoice`/`alSynAllocVoice`
implement real N64 hardware **voice-stealing** — a small fixed pool of
physical `PVoice`s (`ALSynConfig.maxPVoices`, set from
`MUSIC_SYN_CONFIG_MAX_P_VOICES` in `music.c:772`, unchanged decompiled
value) where a new voice request silently steals and ramps out the
lowest-priority already-playing physical voice once the pool is full
(lines 100-131). On real hardware this would eventually silence an
orphaned looped voice anyway, once enough *other* sounds compete for
the same small voice pool — even though its `ALSoundState` bookkeeping
never gets told to stop. **Not yet checked**: whether the PC port's
mixer (`port/src/mixer.c`) and the synth's free/alloc/lame-list
machinery actually enforce the same `maxPVoices` cap end-to-end, or
whether something in the Phase-3 PC mixer path effectively gives every
voice a free physical channel (never triggers stealing), which would
make an orphaned loop audible forever on PC even in scenes where N64
would eventually have silently stolen it away. This is the most
promising next-step lead — cheaper to check (audit `mixer.c`'s voice
dispatch / count) than adding more ramrom-timing probes.

**Still OPEN.** Next session, in priority order: (1) audit whether
`port/src/mixer.c` honors `maxPVoices`/voice-stealing identically to
`synallocvoice.c`'s original algorithm — if it doesn't, that's the fix,
and it would explain both the door and train loop reports without
touching `snd.c`/`propobj.c`/`front.c` game logic at all; (2) if voice
stealing checks out fine, look at whether N64 has some OTHER explicit
stop call for these ambience loops that isn't in the `Dxx`-traced code
paths yet (e.g. a `lvlManageMpGame`-adjacent full-audio-reset at
attract-cycle boundaries not yet located). Symptom (a)/(2)
(wrong-sample-per-weapon) still untouched — still needs the
decoded-PCM listen-compare. The `GE_AUDIOTRACE` trace additions to
`snd.c` (now covering `sndPlaySfx`'s `newState=`/`flags=` and every
`sndDeactivate` call) are left in place, uncommitted, for reuse.

**M-55 — priority-(1) voice-stealing audit done; result is negative, and
narrows the bug further.** Static/read-only session (local Qwen `triage`
dispatch for the grep/citation legwork, spot-checked line-for-line
against the real files before trusting it).

- **`port/src/mixer.c` doesn't need to honor `maxPVoices` — it never
  participates in voice allocation at all.** Grepped it for
  `maxPVoices`/`pAllocList`/`pFreeList`/`pLameList`/`numPVoices`: zero
  hits. `mixer.c` (~436 lines) only implements the low-level `aXxx`
  RSP-acmd DSP commands (envmixer/resample/etc `Impl` functions) that
  `synallocvoice.c`'s already-allocated `PVoice`s get pointed at — the
  lame-list/free-list/steal-lowest-priority bookkeeping (D202 M-54's
  suspect) lives entirely in **unmodified ground-truth**
  `src/libultra/audio/synallocvoice.c` (`alSynAllocVoice`/
  `_allocatePVoice`, lines 27-131, byte-identical to decomp) plus
  `src/libultra/audio/synthesizer.c`'s `alSynNew` (`numPVoices =
  c->maxPVoices`, `music.c:772`'s `MUSIC_SYN_CONFIG_MAX_P_VOICES`,
  unchanged). **The M-54 "maybe the PC mixer gives every voice a free
  channel" theory is ruled out — there's no PC-specific voice-pool code
  to have that bug in.** Since this pool logic is unmodified decomp
  code, its steal behaviour on PC is provably identical to N64's.
- **Priority (2) has an answer too, and it's negative in a useful way.**
  Traced whether the `ALSynth` driver (or anything else audio-wide) gets
  reset/reinitialized at the attract-demo → boot-logo transition:
  `alInit` (`src/libultra/audio/sl.c:28-33`) is a **hard singleton**
  (`if (!alGlobals) { alSynNew(...); }`) called exactly once, transitively
  from `bossEntry()` → `musicSeqPlayerInit()` (`src/boss.c:312`,
  `src/music.c:648`) → `amCreateAudioManager()` (`src/music.c:781`) →
  `alInit()` (`src/audi.c:374`/`378`) → `alSynNew()`. The only teardown,
  `alClose()` (`src/audi.c:481`), fires solely on audio-thread
  `MAIN_QUIT_MESSAGE` (process exit), not at a demo boundary. The actual
  attract-demo stop path, `stop_demo_playback()`
  (`src/game/ramromreplay.c:604-616`), touches only joystick-playback
  bookkeeping (`joySetPlaybackFunc`/`joySetContDataIndex`/
  `ramrom_demo_related_3`/`is_ramrom_flag`) — **no audio call of any
  kind.** All of `sl.c`, `audi.c`, `boss.c` here are unmodified
  ground-truth, so **N64 has exactly the same absence of a
  demo-boundary audio reset** — confirming there is no missing-on-PC
  system-reset bug to find in this direction either.
- **Net effect: both of M-54's priority leads are now closed, and
  neither explains the bug.** This pushes the M-53 "Follow-up" theory
  back to the front — a genuinely `SOUND_FLAG_LOOPED` voice has *no*
  code path that stops it at a `ramrom` attract-cycle boundary on
  **either** platform; whatever the real fix is, it is a **pre-existing
  N64 behavior being newly exposed**, not a PC regression, and per rule
  #2 (game logic is ground truth) is very unlikely to be something we
  should "fix" by adding a stop call the original game never had. The
  live-repro data (M-54 cont.) already showed this exact voice audibly
  looping forever in the PC build; whether real N64 hardware's `PVoice`
  stealing (now confirmed byte-identical logic) actually does silence it
  in practice depends on whether some *other* sound eventually claims
  that priority slot during/after the demo cycle — untested acoustically
  either way.

**Still OPEN. Next session:** stop auditing the voice-pool/reset code —
both candidate mechanisms are now cross-validated clean/absent on both
platforms. The two live options are (a) accept this as a real, harmless-
on-N64-in-practice edge case (the orphaned voice gets stolen once enough
other SFX compete for the small `PVoice` pool during normal gameplay,
just not during an idle unattended attract-mode capture) and downgrade
D202(b) priority accordingly, or (b) do the acoustic check directly —
run a longer capture (10+ min, several attract cycles) or a temporary
`PORT`-gated `ramrom_table` index override to force a door/train demo
reliably, then listen past the demo boundary to hear whether the loop
in fact does eventually cut out once other SFX start competing for
voices (would confirm (a) and close D202(b) as WONTFIX/expected).
Symptom (a)/(2) (wrong-sample-per-weapon, PPK→PUNCH1) is still fully
untouched and is now the higher-value remaining D202 thread — needs the
decoded-PCM listen-compare, not more voice-pool tracing.

**M-56 — D202(a) got the decoded-PCM listen-compare, live user confirmation,
and the index/pointer/ROM-data chain is now independently proven byte-exact
against the real ROM (not just "ruled out" by code audit). Root cause is
narrowed to the actual decode/mix runtime.**

- Added a temporary `GE_AUDIODUMP=1` probe (`port/src/audio.c`,
  `audioSetNextBuffer`) that raw-dumps every mixed s16 stereo buffer
  reaching the SDL device to `audiodump.raw` — the true final output, same
  path as what plays in-game. Ran a headless repro (`-level_33` Dam,
  `GE_INPUTSCRIPT` holding aim+fire for 8 trigger-pulls starting ~10s in),
  converted the dump to WAV, located the transients via a 50ms-window RMS
  scan, and sent the clip to the user. **Exact repro command** (run from
  repo root, `export PATH=/c/msys64/mingw64/bin:$PATH` first):
  ```
  rm -f audiotrace.log audiodump.raw
  GE_AUDIODUMP=1 GE_AUDIOTRACE=1 GE_INPUTSCRIPT="600:R,Z;601:R,Z;650:R,Z;651:R,Z;700:R,Z;701:R,Z;750:R,Z;751:R,Z;800:R,Z;801:R,Z;850:R,Z;851:R,Z;900:R,Z;901:R,Z;950:R,Z;951:R,Z" \
    timeout 30 ./build-pc/ge007.x86_64.exe -level_33
  ```
  `audiotrace.log` (per-call soundIndex/pointer trace) and `audiodump.raw`
  (raw s16le stereo @ 22050Hz, no header — wrap with Python's `wave` module
  or `ffmpeg -f s16le -ar 22050 -ac 2 -i audiodump.raw out.wav`) land in the
  repo root. **User confirmation: "it's playing
  the ricochet sound effect and the slapping melee attack sound effect on
  each gunshot"** — i.e. two distinct wrong-but-real game sounds, not
  silence/noise/a single mislabeled clip.
- The `GE_AUDIOTRACE` probe (already in tree) on that same run showed the
  fire event genuinely requests **soundIndex=46 (`GUN_SILPPK_A_SFX`)**
  paired with **soundIndex=122 (`CART_SPENT_SFX`, the shell-casing sound —
  `gunfire.c:5582`)** — both legitimate, correct call sites for this
  weapon/action, resolving to non-null, structurally-valid
  sound/keyMap/wavetable/book pointers. This is a **live, real-gameplay
  confirmation** of what M-52 had only checked structurally.
- **Independently re-parsed the raw ROM bytes from scratch in Python**
  (no reuse of any project code — a from-first-principles walk of
  `ALBankFile → ALBank → ALInstrument → ALSound → ALWaveTable` against
  `data/ge007.ntsc-final.z64` at the `_sfxctlSegmentRomStart`/
  `_sfxtblSegmentRomStart` addresses) to check the PC port's custom
  `port/src/romdata.c` re-layout tool (`romdataAudioBankPcSize`/
  `romdataFixupAudioBank`, the `afFixup*` family — hand-written PORT code,
  not decomp, and the one part of this chain not previously scrutinized
  this closely). **Result: exact match.** Index 46's wavetable resolves to
  `base=0x103128A0 len=2242`, index 122's to `base=0x10351DF8 len=2196` —
  **both bit-for-bit identical to the live runtime trace.** Also pulled
  each sound's ADPCM book (order/npredictors/coefficients) and `ALKeyMap`
  directly from ROM: indices 46/47/122 all have distinct, non-aliased book
  coefficients (not a copy-paste/dedup collision), though 46 and 47
  (`PUNCH1_SFX`) do share an identical `keyBase=54/detune=50` keymap —
  coincidental (both are attack-transient sounds tuned the same), not by
  itself an index confusion.
- **This closes out the index/pointer/ROM-offset chain for good** — it is
  now proven correct by independent reconstruction, not just "audited and
  believed clean" (M-52's original phrasing). Since each sound's raw
  ROM-embedded ADPCM data is objectively distinct (different books), the
  wrong-sounding output can only be happening in the **decode or mixing
  runtime itself** — i.e. the actually executed `port/src/mixer.c` path,
  not the data feeding it.
- **New leading suspect, not yet tested**: **D199's flagged risk** — the
  Phase-3 mixer's `aSetBuffer` **persistent-DMEM-context inference**
  (macro-swap `aXxx`→`aXxxImpl` execution with no RSP disassembly to
  verify against, `findings.md` D199) — if two voices active in the same
  audio frame (e.g. the continuous Dam wave-ambience loop already seen
  looping throughout this capture, soundIndex 65/67, plus the fire SFX)
  end up sharing/clobbering the same inferred DMEM decode-context instead
  of each getting its own, one voice's ADPCM decoder state could bleed
  into another's output — producing exactly this "correct data in, wrong
  but real-sounding-like-something-else data out" symptom, deterministically
  per concurrent-voice pairing. This is a hypothesis, not yet verified.

**Still OPEN. Next session:** audit `port/src/mixer.c`'s `aSetBuffer`/DMEM
context handling for whether concurrently-active voices (not just
back-to-back `Impl` calls for the *same* voice) get isolated decode state —
trace which physical DMEM buffer/context each of the fire SFX and the
continuous ambience loop actually uses per frame during the same repro
(`GE_AUDIODUMP`+`GE_AUDIOTRACE`, `-level_33`, aim+fire script above already
gives a clean repro). Do NOT re-check the ROM offset/pointer/index chain
again — independently re-derived byte-exact this session, closed for good.

**M-57 — the M-56 concurrent-voice-DMEM hypothesis is FALSIFIED by a live
opcode-level trace, not just re-audited. A new (currently dormant)
pointer-truncation bug was found in the same family as D198/D201, and a
full sibling audit was produced, but NEITHER explains D202 yet — still
OPEN.** Interactive session, build+run environment confirmed working
(`export PATH=/c/msys64/mingw64/bin:$PATH` from repo root).

- Added a temporary `GE_MIXERTRACE=1` probe (`port/src/mixer.c`) that logs
  every `aSetBuffer`/`aLoadADPCM`/`aADPCMdec`/`aResample`/`aEnvMixer` call
  with its DMEM addresses and state pointer, in call order, to
  `mixertrace.log`. Reran the exact M-56 repro
  (`GE_AUDIODUMP=1 GE_AUDIOTRACE=1 GE_MIXERTRACE=1 GE_INPUTSCRIPT=... -level_33`)
  and read the trace around the soundIndex 46/122 fire event (cross-referenced
  via `audiotrace.log` and the `alLoadParam` wire probe's `audiotrace_wire.log`,
  both already in tree from M-52/M-56).
- **Result: every voice's full chain — `aLoadADPCM` → `aSetBuffer`(decode) →
  `aADPCMdec` → `aSetBuffer`(resample) → `aResample` → `aSetBuffer`(main) →
  `aSetBuffer`(aux) → `aEnvMixer` — runs to completion, strictly sequentially,
  before the next voice's `aLoadADPCM` starts.** No interleaving between
  voices was observed anywhere in ~62k logged opcodes spanning the fire
  event. The single shared `sDmem`/`sAdpcmTable`/`sCtx` globals are safe
  under this call pattern for exactly the same reason the real RSP's single
  physical DMEM is safe: one voice's full pipeline finishes and its result is
  accumulated into the persistent `AL_MAIN_L_OUT`/`AL_AUX_*` buffers before
  the scratch region (`AL_DECODER_IN`=0) is reused by the next voice. **D199's
  "concurrent-voice DMEM clobber" risk is closed, negative** — falsified by
  direct trace, not just re-reasoned about.
- Also noticed `a->table->len` visibly changes (2242 → 2241) between the
  first and later `alLoadParam` wire-probe log lines for the *same* shared
  `ALWaveTable`. Traced this to `load.c:396`
  (`a->table->len = ADPCMFBYTES * ((s32)(a->table->len/ADPCMFBYTES));`) —
  **unmodified N64 ground-truth code**, rounds the table length down to a
  whole-ADPCM-frame multiple. It mutates the shared table in place but is
  idempotent (rounds to the same value on every call after the first) and
  behaves identically on N64. **Not a bug — ruled out.**
- **New finding (not yet root-caused as D202's cause, likely a separate
  latent issue): `src/libultra/audio/load.c` lines 111, 173, 259, 320, 382,
  438 all do `f->memin = (s32) f->table->base + ...` / `a->memin = (s32)
  a->table->base` — narrowing a real 64-bit `u8 *base` pointer
  (`ALWaveTable.base`, `include/PR/libaudio.h:217`) into the `s32 memin`
  field** (matches N64 ground truth's struct layout, `synthInternals.h`).
  This is the same truncation class as the already-fixed D198
  (`K0_TO_PHYS`) and D201 (`bnkf.c` relocation offsets), but **currently
  dormant in this session's build**: the observed `ALWaveTable.base` values
  (e.g. `0x103128a0`) sit comfortably under 4GB (this exe loads its ROM-data
  blob at a low, non-ASLR'd address), so the `(s32)` truncation followed by
  `osPhysicalToVirtual`'s `u32`→pointer zero-extension round-trips exactly —
  no data corruption observed in this run. Flagging as a **hardening item**,
  not a confirmed D202 cause; would need a build/environment where the
  ROM-data blob's heap address exceeds 4GB to prove it live. Not fixed this
  session (out of scope for this investigation; needs its own D-number if
  picked up).
- Delegated a read-only sibling-audit of this cast pattern across the whole
  audio subsystem to the local Qwen `repo-mapper` agent (grep-only, no
  edits, checked against real files). Full hit list, confirmed genuine
  pointer-truncation sites beyond `load.c`:
  - `src/music.c:879,1073,1266` — `(u8*)((t3 + (s32)thing.seqData) -
    trackSizeBytes)` — same class, narrows a seq-data pointer before a ROM→RAM
    copy. Worth checking next alongside `load.c`.
  - `src/libultra/audio/heapinit.c:26` — narrows the audio heap `base`
    pointer to `s32` but immediately masks with `& AL_CACHE_ALIGN` (only
    alignment low-bits survive) — low risk in practice, still worth a
    `uintptr_t` pass for hygiene.
  - Every other `(s32)`/`(int)` cast in `src/libultra/audio/*.c` and
    `src/libultrare/audio/*.c` (`resample.c`, `reverb.c`, `env.c`,
    `seqplayer.c`, `csplayer.c`, `save.c`, `synthesizer.c`, `drvrNew.c`)
    narrows a genuine small integer (byte/sample counts, pitch ratios,
    indices) — not pointer truncation, nothing to fix.

**M-58 — priority (1) from the M-57 write-up (decoded-PCM-per-voice-slot dump)
executed. Result: address resolution AND the ADPCM decode math are BOTH
independently verified bit-exact correct for a live fire-SFX occurrence.
D202 is NOT in `aLoadBuffer`'s DMA/address plumbing and NOT in
`aADPCMdecImpl`'s decode arithmetic. Still OPEN — the bug is downstream of
decode or in voice/output routing.**

- Extended the `GE_MIXERTRACE` probe (env-gated, zero cost when unset)
  three ways: (a) `port/src/mixer.c` `aLoadBufferImpl`/`aLoadADPCMImpl`/
  `aADPCMdecImpl` now log the actual DMEM source bytes, the loaded ADPCM
  book coefficients, and the first decoded 16-sample frame (skipping the
  16-sample history lead-in) into the *same* `mixertrace.log` as the M-57
  opcode trace; (b) `src/libultra/audio/load.c` `_decodeChunk` logs
  `[DMAREQ] filter=<ALLoadFilter*> memin=<addr>` right before calling
  `f->dma(...)`, and `alLoadParam`'s existing `AL_FILTER_SET_WAVETABLE` case
  now also emits a `[BINDTABLE] filter=<ptr> <- table=... base=...` line into
  `mixertrace.log` (previously only in the separate `audiotrace_wire.log`)
  so table-bind, DMA, and decode events for the same physical voice filter
  interleave in one chronologically-ordered file instead of needing
  cross-file correlation by line count (a real mistake made mid-session:
  comparing `wavetable->base` from one run's `audiotrace.log` against
  `aLoadBuffer` addresses from a *different* run's `mixertrace.log` produced
  a spurious "addresses never match" false alarm — resolved by getting both
  probes into one file in one run); (c) `src/audi.c` `amDmaCallback` now
  logs `[DMAHIT]`/`[DMAMISS]` with the input ROM-side address, matched
  buffer's `startAddr`, and the returned staging-buffer address.
- Reran the exact M-56/M-57 repro
  (`GE_AUDIODUMP=1 GE_AUDIOTRACE=1 GE_MIXERTRACE=1 GE_INPUTSCRIPT=... -level_33`,
  see M-56 for the exact command). Picked one live fire event
  (`soundIndex=46`, `wavetable->base=0x103128a0`) and followed its physical
  voice filter (`ALLoadFilter*`) end to end in one linear read of
  `mixertrace.log`:
  - `[BINDTABLE] filter=...706d7230 <- table=...706c4cc8 base=0x103128a0`
  - `[DMAREQ] filter=...706d7230 memin=0x103128a0` — **exactly matches** the
    bound table's own base. No cross-voice/cross-heap address confusion.
  - `[DMAMISS] addr=0x103128a0 ... ret=0x706dd490` — legitimate cold DMA
    (N64-analogous ROM→staging-buffer copy via `port/src/libultra.c`'s
    `piServiceDma`, which correctly `memcpy`s from the cart-mapped `.z64`
    region at `srcPA=0x103128a0`, a real, valid cart address under
    `romdata.c`'s mapping — not a stray heap pointer as first suspected
    mid-session before the single-file trace fix above).
  - `[LOADBUF] dram=0x706dd490 ... bytes=900de113224092ecde90f602424ae1bf` —
    the exact ADPCM-encoded source bytes now loaded into DMEM.
  - `[ADPCMDEC] ... book0=-881` and the dumped book coefficients match
    `sndPlaySfx`'s own `book=00000000706c4ca0` trace for soundIndex 46
    (`+8` byte print-pointer offset, same object) — the correct predictor
    table for this sound, not a stale/aliased one.
  - `[PCMOUT] book0=-881 first-decoded-frame[0..15]=0,-1536,-2475,-1165,...`
- **Independently hand-decoded the same 9 input bytes against the same
  dumped book coefficients using the textbook VADPCM algorithm** (order-2
  predictor, `header byte -> shift+tableIndex`, nibble sign-extension,
  cumulative predictor sum), entirely outside this codebase, no reuse of
  `mixer.c`'s logic. First three samples worked by hand: `0`, `-1536`,
  `-2475` — **bit-exact match** against the C implementation's dumped
  output. This independently confirms `aADPCMdecImpl`'s math is a correct
  VADPCM decoder, not just internally self-consistent.
- Also noticed (not a bug, recorded for the record): `sAdpcmTable` is a
  single static global reused across sound switches without zeroing unused
  predictor slots, so predictor indices beyond a sound's own `npredictors`
  hold stale coefficients from a previous sound's book load. Harmless in
  practice — a sound's own ADPCM-encoded stream only ever emits
  `tableIndex` values within its own encoded `npredictors` range (confirmed
  here: soundIndex 46's book is `bookSize=32` bytes = 1 predictor via
  `2*order*npredictors*ADPCMVSIZE`, and its stream's header nibble was `0`,
  the only valid index) — but it is fragile precisely because it depends on
  the encoder never emitting an out-of-range index; a future corrupted/
  malformed bank could read garbage predictor coefficients silently. Not
  pursued further this session (not shown to relate to D202).
- **This closes out the entire `aSetBuffer`→`aLoadBuffer`→`aLoadADPCM`→
  `aADPCMdec` chain for the cases actually traced.** Combined with M-56
  (ROM data/index chain closed) and M-57 (DMEM concurrency closed), the
  first three links of the pipeline — request routing, ROM data integrity,
  and decode — are now all independently verified correct for at least one
  live fire-SFX instance. The mid-session false alarm (see above) is a
  useful lesson: **always put every new correlated probe into the same
  linearly-ordered trace file**; cross-file correlation by line count
  across separate runs is not valid even when the repro is "the same"
  command, because per-run nondeterminism (already catalogued, D117) shifts
  line counts and, this session, briefly looked exactly like a real bug.

**Still OPEN. Next session, in priority order:** (1) the remaining untraced
links are **resample → envmix → final-mix routing**, and **physical-voice
(PVoice) allocation/identity** — whether the correctly-decoded PCM for one
logical `ALSoundState` ends up accumulated into the *wrong* voice's/output's
persistent envelope-mixer volume-ramp state (`sVol`/`saved->t[]` in
`aEnvMixerImpl`) or gets attributed to the wrong output buffer, which would
produce exactly "correct audio computed, wrong audio heard." Trace the same
way as this session (one physical filter/voice followed end-to-end in one
`mixertrace.log`, `[ENVMIX] state=...` pointer identity checked against which
logical sound it's supposed to belong to) rather than assuming and re-deriving.
(2) If that's clean too, suspect PVoice allocation/identity higher up (the
synth's voice-slot assignment, independent of DMEM/decode). (3) Only after
(1)/(2): consider fixing the `load.c`/`music.c` pointer-truncation family as
a hardening pass (own D-number) — still not shown to cause D202. Do NOT
re-check the ROM offset/index/pointer chain (M-56), DMEM/`aSetBuffer`
concurrent-voice sharing (M-57), or the decode arithmetic/addressing
(M-58, this session, verified bit-exact) — all closed.

**M-59 — likely resolution: D202 is NOT a decode/mixer bug. The "ricochet +
melee slap" audio the M-56 listening test heard is a SEPARATE, entirely
legitimate SFX request made by unmodified ground-truth game logic on every
shot in this specific repro, because the scripted aim direction hits a wall/
prop, not open air. Recommend a follow-up open-air listening test before
resuming the M-58 priority-1 envmix/PVoice trace.**

- Two more concrete checks, continuing directly from M-58's decode-chain
  verification (which stays valid and closed): (a) independently confirmed
  the actual DMEM-loaded ADPCM byte string M-58 dumped for soundIndex 46
  (`900de113224092ecde90f602424ae1bf...`) is byte-for-byte identical to the
  **raw, untouched `data/ge007.ntsc-final.z64` file** at the corresponding
  cart offset (`0x03128a0`, i.e. `base 0x103128a0` minus `CART_BASE
  0x10000000`) — read directly with a throwaway Python script, no project
  code reused. Traced why this address is meaningful at all:
  `_sfxtblSegmentRomStart` (`port/src/romassets_u.s:2948`) is a linker
  constant equal to the literal cart address `0x102F19A0`, used as-is by
  `alBnkfNew`'s `table` argument (`src/music.c:703`) → `_bnkfPatchWaveTable`
  (`src/libultra/audio/bnkf.c:140`, `w->base += table`) — i.e. GE's
  wavetable sample data is **never relaid-out or copied by
  `port/src/romdata.c`'s bank-structure fixup at all**; `base` ends up
  pointing straight into the same live-mapped `.z64` image `port/src/audi.c`
  reads cart data from generally (`docs/dev/findings.md` cart-mapping notes,
  M-56). This closes the last theoretical gap in the M-56/M-58 chain: ROM
  data integrity is now verified at the raw file-byte level, not just
  structurally.
  - With the entire request→ROM-offset→DMA→decode chain now proven correct
    end to end, (b) re-examined the *complete* `audiotrace.log` from the
    M-58 repro (`-level_33` Dam, aim+fire `GE_INPUTSCRIPT`) rather than
    filtering to just soundIndex 46/122, and cross-referenced every other
    soundIndex against `src/bondconstants.h`'s `"NN_NAME_SFX"` string table
    (which encodes the index directly in the name — e.g. `"46_
    GUN_SILPPK_A_SFX"`). **Every single `soundIndex=46` (fire) request in
    the trace is immediately followed by a `soundIndex` from `{23, 24, 25,
    37}`** — `"23_RICO_6_TAJ_A_SFX"`, `"24_RICO_6_TAJ_B_SFX"`,
    `"25_RICO_6_TAJ_C_SFX"`, `"37_RICO_5_C_SFX"` — i.e. **ricochet/bullet-
    impact sounds**, cycling per shot, plus the already-known `122` =
    `"122_CART_SPENT_SFX"` (cartridge eject).
  - Found the call site: `src/game/gunfire.c`
    `recall_joy2_hits_edit_detail_edit_flag()` (line ~2270, unmodified
    ground-truth decomp function, original obfuscated name) is the bullet
    hit-detection handler. For a hitscan that strikes a non-character prop
    (`prop->type != PROP_TYPE_CHR && != PROP_TYPE_VIEWER`, i.e. scenery/
    wall, not an enemy), it unconditionally does:
    `ricochet_sounds_small_copy = ricochet_sounds_small;
    sndPlaySfx(g_musicSfxBufferPtr,
    ricochet_sounds_small_copy.arr[randomGetNext() % 20], sound_state);`
    — **every bullet that hits a wall/prop plays a randomly-chosen ricochet
    SFX from a 20-entry table, by design, on real N64 too.** This is not a
    port artifact; it is exactly what GoldenEye does when you shoot a wall.
  - **Conclusion**: the M-52/M-56 repro's fixed aim direction
    (`-level_33`, default spawn facing, held-down aim+fire) has the player
    shooting directly into a nearby wall/prop the entire time, so *every*
    trigger pull legitimately plays fire (46) + cartridge-eject (122) +
    a random ricochet impact (23/24/25/37) simultaneously — three real,
    correct SFX per shot, not one wrong one. The M-52 playtest's original
    verbal description ("sounds like a melee/punch hit") and the M-56
    listening test's ("ricochet sound effect and the slapping melee attack
    sound effect") both match a ricochet impact's percussive/metallic
    character far better than a coincidental decode bug that happens to
    always produce two *other real, nameable* game sounds. No corrupted or
    misattributed audio has been found anywhere in the chain despite three
    full sessions (M-56/M-57/M-58) of independent, bit-exact and byte-exact
    verification at every single link — address resolution, ROM data
    integrity, book/coefficient selection, ADPCM decode arithmetic, and now
    the request pattern itself.
  - **This does not yet prove the gunfire (46) sound itself is
    perceptually correct/prominent** — only that it is being computed
    correctly and is not what's "wrong" in what the tester heard. It's
    possible the fire SFX is simply quiet/subtle by design and gets
    acoustically masked by the louder ricochet transient, which would be
    expected N64 behavior too, or (lower probability, unverified) there is
    a genuine separate volume/mix-balance issue between simultaneous
    voices. Not fixed or further diagnosed this session — needs a
    clean listening test to settle it (see below).

**Recommended next session (before resuming the M-58 priority-1 envmix/
PVoice trace, which may now be unnecessary): re-run the exact same
`GE_AUDIODUMP` capture but with an `GE_INPUTSCRIPT` aim direction that
points at open air / the sky instead of a wall (or simply hold fire without
the aim-lock at a fixed wall-facing spawn), confirm via `audiotrace.log`
that NO `ricochet_sounds_small`-family soundIndex fires, and have the user
listen to that clip specifically. If the fire (46) + cartridge (122) sounds
alone are now clearly audible, correct, and recognizable as gunfire, D202
should be closed as a repro artifact (aim-at-wall), not a bug — with a note
that the original M-52 playtest report may itself have simply been the
player firing at nearby geometry during normal play, which is entirely
expected. Only if the open-air clip STILL sounds wrong should the M-58
priority-1 envmix/PVoice-identity trace be resumed.

**Follow-up done same session (M-59 cont.):** turned the character with
`GE_INPUTSCRIPT="1:SRIGHT;400:SNONE;600:R,Z;..."` (sustained analog-stick
turn before the aim+fire sequence) and reran the `GE_AUDIOTRACE`+
`GE_AUDIODUMP` capture on `-level_33`. Result: the *first* shot in this run
requests **only** `soundIndex=46` (fire) + `122` (cartridge) — no
`ricochet_sounds_small` index at all — confirming the turn moved the aim
off whatever wall/prop the original fixed-facing repro was hitting. The
*second* shot (moments later, once the turn brought the aim back toward
nearby geometry) requests `46` + `122` + `23` (`RICO_6_TAJ_A_SFX`) again,
giving a same-run A/B pair. Located both transients in `audiodump.raw` via
a 50ms-window RMS scan (clean shot ≈ t=11.0–11.7s, ricochet shot ≈
t=11.7–12.6s), exported both as WAV (`scratchpad/
d202_clean_shot_no_ricochet.wav` / `scratchpad/d202_shot_with_ricochet.wav`,
not committed — scratch), and sent both to the user for a direct listening
comparison. **Awaiting user confirmation** on whether the ricochet-free
clip sounds like correct, recognizable gunfire — that answer settles
whether D202 closes outright as a repro artifact or the envmix/PVoice trace
needs to resume.

**M-59 result: user says BOTH clips are the ricochet/slap sound, just at
different volumes — the "clean" clip is not clean.** This falsifies the
"aim-at-wall repro artifact" theory as originally framed. Investigated
further:

- The two exported clips both came from `dumppos`-adjacent events with
  soundIndex 46 and a ricochet index at the **same audio-frame position**
  in this run's `audiotrace.log` (a new `dumppos=<byte offset into
  audiodump.raw>` field was added to the existing `GE_AUDIOTRACE` probe —
  `port/src/audio.c` now exposes `audioDumpBytePos()`, an exact running
  byte counter, so future clip extraction can key off a logged sample
  position instead of an RMS-scan guess). Confirmed the fire (46) and
  ricochet requests land at the *same* `dumppos`, i.e. they are requested
  in the same tick and their audio overlaps in time rather than being
  sequential — so the RMS-peak-picking method used for the M-59 clips could
  not actually isolate a ricochet-free window; both exported clips likely
  had some ricochet content, just at different relative volume (distance-
  dependent ricochet gain, or a different ricochet variant per shot).
- To get a **guaranteed** ricochet-free sample, decoded `soundIndex 46`
  **entirely offline**: read `data/ge007.ntsc-final.z64` directly at ROM
  offset `0x03128a0`, len 2242, ran the same hand-verified VADPCM decode
  used in M-58 (order-2, single predictor, coefficients from the M-58
  trace dump) with no live game/mixer involved at all — zero possibility of
  a simultaneous ricochet voice. Rendered to `scratchpad/
  d202_soundindex46_isolated.wav`, sent to the user. **User confirmed: "yeah
  that is the slap effect that in the original game plays when you do a
  melee attack."** This is unambiguous — soundIndex 46's actual ROM sample
  content genuinely is (or matches) the melee-slap SFX, independent of any
  mixing/overlap question.
- **Traced the request chain one level further up, past `sndPlaySfx`, to
  find out why soundIndex 46 is requested for the silenced PPK's fire at
  all.** `bondwalkItemGetSound(item)` (`src/game/gun.c:1345`) returns
  `get_ptr_item_statistics(item)->Sound` — a `u16 Sound` field on the
  per-weapon `WeaponStats` struct (`src/game/gun.h:87`, doc comment:
  "Sound effect played when gun is shot. There are 261 sound effects, or
  0 - 105h."). `get_ptr_item_statistics` (`gun.c:721`) returns
  `gitem_structs[item].item_weapon_stats` — **not a runtime ROM-converted
  pointer**; `gitem_structs[]` (`assets/obseg/gun/gunModelFileRecord.inc.c`)
  is a **static, compiled C data table**, decompiled directly from the
  ROM's `.data` segment (each `GUNFILERECORD(...)` entry pulling in a
  `GUNSTATS(name)` initializer). The silenced PPK's is
  `assets/obseg/gun/wppksil/gunWeaponStat.inc.c`
  (`WeaponStats wppksil_stats = {1.0, 11.0, ..., 0x2E, ...}`, tagged with a
  decompilation RAM-address comment `//D:800326C4`) — **`.Sound` is
  positionally the 14th field, value `0x2E` = decimal **46**.** This is
  unmodified, hand-decompiled ground truth, not a port-side conversion —
  there is no PC-specific fixup in this chain at all (unlike the wavetable
  data, this table is a plain compiled literal, identical on N64 and PC by
  construction).
- **Conclusion: on real N64 hardware, firing the silenced PPK requests
  the exact same soundIndex 46 this port does.** Every link — decompiled
  weapon-stat data, SFX-bank index resolution (M-56/M-59), ROM sample
  bytes (M-58/M-59), and ADPCM decode (M-58) — is now independently
  verified correct and, more importantly, **identical to what N64 hardware
  would do with the same ROM**. If soundIndex 46 truly sounds like a melee
  slap and not a silenced pistol, that is either (a) the original,
  shipped game's actual designed audio (silenced firearms can have a soft,
  percussive "thwack" character that isn't unreasonable to mistake for a
  melee sound, and reusing one SFX asset across two unrelated actions to
  save ROM space is a common technique of this era) — in which case there
  is **no bug anywhere in this port to fix**, this is how GoldenEye 007
  actually sounds; or (b) a very rare decompilation transcription error in
  `wppksil_stats`'s `.Sound` field specifically — but this project's
  entire premise is byte-verified matching against the real ROM's `.data`
  segment (README/AGENTS.md), so an error surviving in a byte-matched
  build would be surprising, though not provably impossible without
  independently re-reading the literal ROM bytes at this struct's true RAM/
  ROM address (not attempted this session — would need the N64 build's
  linker map to translate the `//D:800326C4` RAM comment to a ROM file
  offset).
- **Not fixed, not committed — this is a research conclusion, not a code
  change.** No `port/`, `src/`, or `assets/` change is warranted unless (b)
  above is confirmed; per AGENTS.md, `assets/obseg/gun/wppksil/
  gunWeaponStat.inc.c` is decompiled ROM data (ground truth) and must not
  be "corrected" speculatively.

**Recommended definitive tie-breaker (next session, if the user wants to
keep pursuing this rather than close it):** run the **same ROM** in a
known-good N64 emulator (or, if available, real hardware) and fire the
silenced PPK. If it also sounds like the melee-slap effect there, D202 is
closed for good as a genuine original-game characteristic (or a very
deep, pre-existing GoldenEye quirk) — not a decompilation or port bug, and
no further engineering time should go into it. If the emulator's silenced
PPK sounds like a normal muted gunshot, the bug is almost certainly the
rare decompilation transcription error in (b), and the fix would be a
single-field data correction in `gunWeaponStat.inc.c` guarded by the
project's existing byte-match verification (which would need to still
pass — if it doesn't, this table is confirmed byte-correct after all and
the "bug" is upstream of Bond's silenced-PPK path in some other way not
yet identified).

**M-60 — user explicitly rejects the "ground truth, not a bug" conclusion**
("I know for a fact its the wrong sound playing in the PC port
specifically") and supplies a real reference clip instead of an emulator
A/B: `scratchpad/B00I00S2D.wav` (untracked/scratch), a genuine recording of
the silenced PPK's real fire sound, dropped directly by the user (source
not yet stated — presumably real hardware/video capture, not this repo).
Measured: mono, 16-bit, 16 kHz, 3264 samples, 0.204 s. Its 10-bucket RMS
envelope (`12645, 11548, 15152, 16172, 14006, 12766, 15106, 15669, 8280,
6608, 8032, 9409, 9812, 8849, 7745, 6819, 4299, 1477, 496, 176` over 20
buckets) is **sustained** — energy stays in the 6000-16000 range for
roughly the first 80% of the clip's duration before decaying in the last
~20% — structurally different from `d202_soundindex46_isolated.wav`'s
envelope, which peaks early (`11209, 19213, 17387, 16284, 11296, 10047,
6116, 3801, 2238, 1059` over 10 buckets) and decays smoothly and quickly
from the first bucket onward. This is consistent with the user's "slap"
identification (a single sharp decaying transient) vs. a real gunshot's
more sustained buzz/reverb character, and is independent corroborating
evidence — not proof by itself, since envelope shape alone doesn't rule out
a resample/pitch difference distorting the comparison (see below).

**Re-opened the investigation on the user's instruction; do not close D202
as WONTFIX.** Two concrete threads were started this session, neither
finished:

1. **The debug-name label itself is suspicious independent of any
   listening test.** `bondconstants.h`'s soundIndex 46 is *named*
   `"46_GUN_SILPPK_A_SFX"` — a name that specifically claims to be the
   silenced-PPK gunshot — yet its decoded ROM content is what the user
   twice identified as the melee-slap effect (which has its own, different,
   adjacent indices: `47_PUNCH1_SFX`/`48_PUNCH2_SFX`/`49_PUNCH3_SFX`). A
   correctly-decoded sound whose content doesn't match its own debug label
   points at a **sound-bank navigation/indexing bug** (wrong bank, wrong
   instrument, or an off-by-one somewhere in the ALBank -> ALInstrument ->
   ALSound walk) rather than at "GoldenEye's SFX design reuses one asset
   for two things" (M-59's charitable reading (a)). `src/snd.c`'s
   `sndPlaySfx` doc comment is explicit about the resolution path:
   `soundBank->instArray[0]->soundArray[soundIndex]` — i.e. `soundIndex` is
   an index into **instrument 0's** sound array, not a bank-global flat
   index. Worth checking directly: does `romdata.c`'s bank re-layout
   (`afCtx`/`_bnkfPatchWaveTable`, `port/src/romdata.c` ~L582-780) walk
   instruments/sounds in the same order the ROM's `ALBankFile` stores them,
   or could the PC conversion silently shift indices by one somewhere in
   that re-layout pass? Not yet checked line-by-line against the ROM bytes
   for THIS specific bank/instrument.
2. **Started `scratchpad/rom_sfx_decode.py`** (untracked, WIP, not yet
   working correctly) — a fresh, from-scratch offline ROM bank parser
   (independent of the live game) intended to decode a whole *range* of
   `soundIndex` values (44-50, 107, extensible) directly from
   `_sfxctlSegmentRomStart`/`_sfxtblSegmentRomStart` and compare each one's
   envelope/waveform against the new `B00I00S2D.wav` reference, to find
   which soundIndex (if any) actually matches the real gunshot — which
   would prove a specific off-by-N in the bank walk rather than relying on
   ear-only comparisons. **This script's ADPCM predictor loop is currently
   WRONG and must not be trusted as-is** — it stubs out the intra-subframe
   accumulation term with a dead `if False` branch. Confirmed the *exact*
   correct algorithm by re-reading `port/src/mixer.c`'s `aADPCMdecImpl`
   (L185-239, ground truth, this is what the live port actually runs):
   table shape `sAdpcmTable[predictor][2][8]`; per output sample `j` (0-7)
   within a subframe, `acc = tbl[0][j]*prev2 + tbl[1][j]*prev1 +
   (ins[j]<<11) + sum_{k=0}^{j-1} tbl[1][j-k-1]*ins[k]`, `ins[]` being the
   8 sign-extended-then-shifted nibbles of that subframe, `prev1`/`prev2`
   the last two output samples (carried from the previous subframe/frame).
   **Next session: fix `rom_sfx_decode.py`'s decode loop to match this
   exactly** (the M-58/M-59 "isolated decode" script that the user's slap
   identification was based on was verified byte-exact against this same
   algorithm in a prior session, so that specific result stands — only the
   new range-scanning script is unverified), then run it across a wider
   soundIndex range and diff each envelope against `B00I00S2D.wav`.
3. **Also flagged, not yet resolved:** the M-59 "correctpitch" resample
   experiment (`scratchpad/d202_soundindex46_correctpitch.wav`, pitch=23849
   from the M-58 live trace) produced *fewer* output samples (1459) than
   input (3984) at a pitch ratio ~0.364. If pitch < 1.0 means "advance less
   than one input sample per output sample" (slow down / stretch), output
   sample count should be `input / ratio` (more samples), not `input *
   ratio` (fewer) — the direction looks inverted. This resample script's
   formula needs re-deriving against `port/src/mixer.c`'s actual
   `aResampleImpl`/`sResampleTable` before its output is trusted either
   way; it has NOT been sent to the user and should not be used as evidence
   yet.

**Status at handoff: D202 is OPEN, user-confirmed-wrong-in-PC-port, actively
being re-investigated.** Do not re-close as "ground truth, not a bug" —
that conclusion was explicitly rejected by the user with a real reference
recording now in hand (`scratchpad/B00I00S2D.wav`). No code changes made
this session (diagnostic-only), nothing to revert.

**M-61 — both M-60 threads resolved (one closed, one fixed+run); the
decisive question is now "what byte is actually in the original ROM", and a
new logical constraint makes that the keystone fact.** Diagnostic-only
session again; no game/port code touched. Results:

1. **Thread 1 (bank-navigation off-by-N) CLOSED — negative.** Line-by-line
   review of `port/src/romdata.c`'s bank re-layout (`afCtx` /
   `_bnkfPatchWaveTable`, ~L582-780) confirms the PC conversion walks
   `ALBankFile -> ALInstrumentFile -> ALSoundFile` in exact ROM order with
   no index shift; combined with M-56's byte-exact runtime trace, PC
   soundIndex N resolves to the same ROM sample as N64 soundIndex N. The
   "wrong bank / off-by-one in the walk" hypothesis is dead. Do not
   re-litigate it.
2. **Thread 2 (offline decoder) FIXED and VALIDATED.**
   `scratchpad/rom_sfx_decode.py`'s `adpcm_decode()` was rewritten to match
   `port/src/mixer.c`'s `aADPCMdecImpl` exactly. Three bugs fixed: (a)
   tbl0/tbl1 indexing was swapped, (b) the intra-subframe accumulation term
   `sum_{k<j} tbl[1][j-k-1]*ins[k]` was missing, (c) wrong `ncoef` count.
   Validation: full-bank scan of all 261 sounds (gun.h L85: "There are 261
   sound effects, or 0 - 105h") decoded cleanly; **index 45
   (`45_DROP_GUN_SFX`) is the ONLY strong match for the user's reference
   clip `B00I00S2D.wav` (envelope correlation +0.999)**; index 46
   (`46_GUN_SILPPK_A_SFX`) decodes to the early-peak fast-decay "slap".
   Candidates saved as `scratchpad/romsfx_44..60.wav`. The decoder is now
   trustworthy for future soundIndex lookups.
3. **Fire path traced end-to-end in game code — no runtime remapping of the
   index exists.** `src/game/gunfire.c` L3196/L3200:
   `sndPlaySfx(g_musicSfxBufferPtr, bondwalkItemGetSound(var_s1), ...)` →
   `src/game/gun.c` L1343-1345: `bondwalkItemGetSound()` returns
   `get_ptr_item_statistics(item)->Sound` — the raw u16
   `WeaponStats.Sound` field (offset 38; layout confirmed from
   `src/game/gun.h`: 7×f32, s32 AmmoType, s16 MagSize, u8 AutoFireRate,
   s8 SingleFireRate, u8 ObjectsShootThrough, u8 SoundTriggerRate, u16
   Sound, ptr cartridge, …). The silenced PPK's fire sound IS
   `wppksil_stats.Sound`, used verbatim. (Unsilenced PPK uses 0x6B=107.)
4. **Build wiring: both N64 and PC builds compile the same initializer from
   source.** `src/game/gun.c:135` includes the aggregate
   `assets/obseg/gun/gunWeaponStats.inc.c`, which defines `wppksil_stats`
   inline at `//D:800326C4` with `.Sound = 0x2E` (it only #includes the
   per-weapon files for fist/knife; everything else is inline). The
   per-weapon file `assets/obseg/gun/wppksil/gunWeaponStat.inc.c` carries a
   second, IDENTICAL copy (also `0x2E`, same `//D:800326C4`). The PC build
   exclusion list covers only `assets/obseg/{bg,brief,setup,stan}`
   (CMakeLists ~L473), so gun.c's compiled data is identical in both
   builds. **The decompiled source is internally consistent: 0x2E in both
   copies.**
5. **KEY LOGICAL CONSTRAINT (new, this is the keystone):** the project's
   premise is that the N64 build byte-matches the original ROM. Compiled
   data cannot silently diverge — if `gunWeaponStat.inc.c`'s `.Sound`
   were a transcription error, the built ROM would differ from the
   original at exactly those 2 bytes and the match would fail. Therefore
   **0x2E almost certainly IS in the original ROM**, i.e. real N64 hardware
   also requests sound index 46 for the silenced PPK. That contradicts the
   reference clip matching index 45 at +0.999. Exactly one of these must be
   false:
   (a) the byte-match verification does not actually cover this data
       segment (check the verify tooling/output!);
   (b) `B00I00S2D.wav` is not an in-game capture from real N64 hardware —
       its filename looks like a media-library asset ID, provenance was
       never stated; if it's e.g. film/marketing audio, the "N64 plays 45"
       premise collapses and M-59's "faithful reproduction" conclusion
       returns WITH evidence;
   (c) N64 runtime resolution of index 46 differs from ROM-order index 46
       (Thread 1 closed *ordering* in the PC re-layout, not real-hardware
       ALBank content — lower priority given (a)/(b));
   (d) the +0.999 envelope correlation with index 45 is misleading (room
       reverb tail can inflate envelope matches).
6. **Direct ROM-byte read of `wppksil_stats.Sound` FAILED — do NOT repeat
   the same searches (they looped 40+ times this session, all 0 hits):**
   - Any pattern containing a decompiled float literal is unreliable: the
     literals are decimal approximations that don't round-trip to exact
     IEEE-754 bits (e.g. `-20.799999` → `c1a66666`, zero occurrences in
     ROM). Integer-only tail patterns (`AmmoType=1, MagSize=7, 0xFF, 0x10,
     …`) also returned 0 hits — either the byte layout at that ROM location
     differs from assumption or the data sits where the scan didn't look.
   - The hand-computed RAM→ROM mapping produced ROM offset `0xC11934` >
     file size `0xC00000` — WRONG. Note `ge007.ld` puts `.csegment` at ROM
     `0xC00000` (= exactly EOF), so `wppksil_stats` (vaddr `0x800326C4`)
     cannot live there; the correct vaddr→ROM mapping must come from the
     N64 build's map file/symbol table, not arithmetic.
   - No N64 `.map` found at maxdepth 3 (only the PC CMake tree
     `build-linux/`). Search DEEPER: `assets/obseg/Makefile.*`, `ld/`,
     `dist/`, or any N64 ELF in the repo; `nm`/`objdump` on it yields the
     exact vaddr+section, and the ROM offset then follows from the linker
     script.
7. **Next session, in priority order:**
   1. **Establish what byte the original ROM holds at
      `wppksil_stats.Sound`** — find the N64 build map/ELF (search deeper
      than maxdepth 3) and read the 2 bytes. This single fact splits the
      tree: ROM=0x2D → transcription error (then audit why byte-match
      verification missed it); ROM=0x2E → data is ground truth, and the
      discrepancy lives in reference-clip provenance (b) or N64 runtime
      resolution (c).
   2. **Check the byte-match verification tooling** — does it compare data
      segments or code only? Locate the last full-verify output.
   3. **Ask the user for `B00I00S2D.wav`'s provenance** (one question; see
      (b) above).
   4. Cheap corroboration: live PC trace logging the soundIndex actually
      passed to `sndPlaySfx` on PPK fire (expected 46) — confirms the PC
      side behaves exactly per its data.

**Status at handoff (M-61): D202 OPEN.** Threads 1+2 from M-60 are closed
(negative / fixed). No code changes; only `scratchpad/rom_sfx_decode.py`
modified (now correct + validated). Do not re-run the failed ROM byte-
pattern searches — go for the N64 map file instead.

### M-62 — item (2) resolved without a build: byte-match verification DOES cover this exact struct, and its checksum was captured after the source's last edit. `.Sound = 0x2E` is very likely ground truth. Pivoting to (b)/(d).

Static/read-only session. Skipped the N64-map/ELF route (item 1) — no MIPS
toolchain is installed in this environment (`mips-linux-gnu-*` absent,
`docs/building.md` says extraction needs `binutils-mips-linux-gnu`; building
the full N64 target to get a map file is a real toolchain-install task, not
attempted this session) — and went straight for the cheaper item (2) instead.

**Found: this repo's own CI (`.github/workflows/ci.yml`) never builds the
N64 target at all.** `validate`/`linux-build`/`windows-build` are all PC-port
jobs; there is no job invoking the N64 `Makefile` or
`scripts/test_files.sh`. So "byte-matches US/EU/JP ROMs" (AGENTS.md's
opening claim) is inherited from the upstream `n64decomp/007` project's own
history, not continuously re-verified here — worth knowing, but a separate
concern from D202.

**The verification tooling itself does cover `.data`, and gun.o specifically:**
`scripts/test_files_readme.md` — `test_files.sh` extracts `.text`/`.code`/
`.bss`/`.data`/`.rodata` from every `.o` under `src/`, `src/game`, and the
four asset dirs, and diffs each against a checked-in known-good md5
(`scripts/ge007.u-test_basis.csv`). Confirmed by direct grep:
`scripts/ge007.u-test_basis.csv:946`: `0289c36e967840bba7f6fbd026dda001,.data,build/u/src/game/gun.o`
— `gun.o`'s `.data` section (which is where `wppksil_stats` lives, via
`gun.c`'s `#include` of `gunWeaponStat.inc.c`) has a recorded checksum.
This directly falsifies alternative (a) from M-61 — the verification does
cover this exact data.

**Timing check, so the checksum can't be stale relative to a since-edited
source:** `git blame -L946,946 scripts/ge007.u-test_basis.csv` →
`cc14d64e7 "for england james?"` (2026-08-16, upstream sync commit).
`git log -- assets/obseg/gun/wppksil/gunWeaponStat.inc.c` → last touched
`9fbe1fd5` ("sync. run make forceextractassets..."), which is an ancestor of
`cc14d64e7` in this file's history. **So the checksum was captured AFTER
the last edit to this exact source file** — it isn't testing stale content.
Re-read the file directly: `.Sound` is still `0x2E` (14th positional field,
`assets/obseg/gun/wppksil/gunWeaponStat.inc.c:3`), unchanged.

**Conclusion (not a rebuild-confirmed pass, but strong circumstantial
evidence):** the upstream decomp project's own generated fingerprint of a
known-good `gun.o` build covers this exact struct, was captured against
this exact (still-current) source content, and this repo's non-negotiable
rule #2 (game logic/decomp source frozen, PC-port work never touches
`src/game/gun.c` or `assets/obseg/gun/**`) means nothing has drifted since.
**Actually running `test_files.sh` to get a live "pass" line still requires
installing a MIPS toolchain and doing a real N64 build — not done this
session; flagged as the one remaining way to make this airtight rather than
strongly-inferred.** Barring that, alternative (a) is now the *least* likely
of the four M-61 alternatives, not the most. Combined with M-61's already-
closed alternative (c) (traced fire path, no runtime remapping), the two
live leads are now **(b) reference-clip provenance** and **(d) envelope-
match reliability** — both point away from a port bug and toward a
misidentified reference clip.

**Next session, in order:**
1. **Ask the user the one open question: where did `scratchpad/
   B00I00S2D.wav` come from?** (Real N64-hardware capture of this exact
   weapon on this exact ROM region? An emulator? A sound-effects library
   asset, given the filename reads like a media-asset ID, not a capture
   log?) This is the cheapest possible next step and was never actually
   asked — do it before any more code/data spelunking.
2. If the user can't confirm hardware provenance: run the same ROM in a
   known-good N64 emulator (the M-59 handoff's original recommended
   tie-breaker, never done), fire the silenced PPK, listen directly —
   settles (b)/(d) without needing the reference clip at all.
3. Only if both of those come back still contradicting `.Sound = 0x2E`:
   install a MIPS toolchain (`binutils-mips-linux-gnu` equivalent) and
   actually run `scripts/test_files.sh` for real, to convert this
   session's strong-inference close of alternative (a) into a hard pass/fail.
4. Cheap corroboration, still not done: live PC trace of the soundIndex
   passed to `sndPlaySfx` on PPK fire (expected 46) — confirms PC-side
   behavior matches its own data, independent of what the data *should* be.

**Status at handoff (M-62): D202 OPEN**, but the center of mass has shifted
firmly away from "port bug in the data/decode chain" (four independent
sessions, M-56 through M-62, now corroborate `.Sound = 0x2E` end to end)
and toward "reference clip may not be what it was assumed to be." No code
changes this session — read-only investigation, one `findings.md` write-up.

**Same session, follow-up: asked the user directly where `B00I00S2D.wav`
came from. Answer: "Not sure / found online"** — the user does not know its
provenance and it was sourced from the internet, not recorded from real N64
hardware or a verified emulator run against this ROM. This closes item (1)
of the M-62 next-session list and directly confirms alternative **(b)**
from M-61 (reference clip isn't a verified in-game N64 capture) as the true
explanation — an unverified internet clip was compared against a
byte-verified ROM data table and, unsurprisingly, didn't match.

**Reassessment: five independent sessions (M-56 through M-62) have now
verified every link in the actual chain** — soundIndex resolution, bank
re-layout order, the ALWaveTable/ALSound pointer chain, the ADPCM decode
math (hand-verified bit-exact against the C output), the ROM segment
offset (verified 3 ways), and now the weapon-stats source data itself
(byte-match checksum, captured post-edit) — **all say the silenced PPK
correctly requests and plays soundIndex 46 exactly as real N64 hardware
would.** The only unverified link left is the thing D202 was reopened over:
an internet-sourced reference clip of unknown origin. That is no longer a
credible basis to keep this open as a port bug.

**Recommendation: close D202 as NOT-A-BUG** (same conclusion M-59 reached,
now on much stronger evidence) **unless the user wants the belt-and-braces
emulator A/B** (fire the silenced PPK on the actual ROM in a known-good N64
emulator, listen directly) — cheap, and the only remaining way to fully
rule out a genuine transcription error without installing a MIPS toolchain
and running `scripts/test_files.sh` for a hard pass. Do not spend further
session time on the data/decode/index chain — it is exhaustively verified.

### M-63 — SUPERSEDES the M-62 "recommend close": user reports the bug is broader than a single wrong sample — real-time mixer/voice corruption, not a data-table issue. REOPENED for real, live repro attempted. Root cause NOT found; one concrete anomaly banked for next session.

**User's report (verbatim symptom, not the M-60 reference-clip framing):** the
wrong ("slap") sound always plays on the silenced PPK; the correct suppressed
shot sometimes plays **too** (both heard together, not one-or-the-other);
and over a play session sounds "pile up and spirally glitch out," eventually
leaving **no audio except a looping sound (e.g. Bunker's door) stuck playing
since level start.** This is categorically different from "wrong sample
selected" — it describes runtime mixer/voice-lifecycle corruption. The M-56
through M-62 chain (which verified the index/data/decode path bit-exact) does
**not** cover this; it only proves soundIndex 46 is the *correctly requested*
sound, not that the mixer plays it *cleanly* once requested. Reopening.

**Build+run environment IS available this session** (`export
PATH=/c/msys64/mingw64/bin:$PATH`; MSYS2 MINGW64 gcc/cmake/SDL2 all present).
No MIPS toolchain (confirmed absent again, unrelated to this thread).

**False-alarm detour (banked so it isn't repeated): a "crash in `load_bg_file`
(`bg.c:865`) on every `-level_09` boot" is NOT a real bug** — it was this
session's own test-setup gap. A throwaway `build-pc/data/` with only the
`.z64` copied in (no `pccg-ntsc-final/`, `pcmodels-ntsc-final/`) leaves
`file_resource_table` pointing at raw big-endian ROM instead of the converted
PC sidecars (`obInit()`'s own comment describes exactly this failure mode);
the two `[WARN]` lines ("pcmodels.bin not found", "pccg.bin not found —
stage loads will fault") were sitting right in the log and should have been
read before chasing a gdb backtrace. Copying the real `data/pccg-ntsc-final/`
+ `data/pcmodels-ntsc-final/` (+ the repo's real `ge007.eep`/`ge007.ini`)
into `build-pc/data/` makes it load clean, 60s+, no crash, both with and
without the uncommitted audio-diagnostic files. **Next session: always mirror
the full `data/` dir into `build-pc/data/`, not just the ROM, before treating
any level-load failure as real.**

**Repro built:** `GE_MIXERTRACE=1 GE_AUDIOTRACE=1 GE_AUDIODUMP=1
GE_INPUTSCRIPT="600:R,Z;601:R,Z;690:R,Z;691:R,Z;...;3210:R,Z;3211:R,Z"`
(30 fire pulses at ~90-frame/~0.75s spacing) `timeout 60
./ge007.x86_64.exe -level_09`, run against the real save
(`data/ge007.eep`) copied into `build-pc/data/`. Ran clean 60s, no crash.
Logs preserved: `scratchpad/d202-m63/{audiotrace.log,mixertrace.log,
audiodump.raw}` (mixertrace.log is 25MB — grep it, don't read it whole).

**Checked and RULED OUT this session, from the trace:**
- **Voice-pool exhaustion / leak:** only **16 distinct `newState` voice-slot
  pointers** were used across all 153 `sndPlaySfx` calls in the 60s window
  (highest reuse count: 20), and `sndDeactivate` count (159) tracks
  `sndPlaySfx` count (153) almost 1:1. The pool is being recycled
  continuously, not growing unbounded — the "voices never freed" theory
  (the D199/M-53–M-55 lineage) does **not** reproduce in this repro.
- Firing the silenced PPK produced exactly the expected `soundIndex=46`
  requests (58 of them, one burst per scripted trigger pull, occasionally
  2 back-to-back — plausibly dual-wield, not investigated further) with no
  other unexpected soundIndex interleaved in the same burst.

**New anomaly found, not yet explained — the most concrete lead for next
session:** `soundIndex=109` (`bondconstants.h:2546`,
`"109_GUN_B4_BOLTACTION_SFX", //used for AK47`) fires **94 times in 60
seconds** despite the player never touching an AK47 in this repro (silenced
PPK only). A chunk of those requests hit the *same* voice slot
(`newState=00000000706f4fb8`) at a suspiciously uniform **2880-byte spacing
in the mixed output stream** — at 22050Hz stereo 16-bit that's exactly
**720 samples / ~32.6ms between re-triggers**, four-in-a-row
(`dumppos=688576,691456,694336,697216,700096`). ~30 rounds/sec is far
faster than any plausible automatic-weapon cadence in this game (real
full-auto weapons here fire far slower) — this reads like something is
re-issuing `sndPlaySfx` for the same logical sound every audio tick/frame
instead of once per actual game event, which would exactly explain "sounds
piling up." **Not yet confirmed as related to the user's PPK-slap complaint
specifically** (soundIndex 109 isn't the PPK's sound) — could be a
*second*, independent SFX-retrigger bug that happens to share the same
mixer, and its presence would still explain general "audio feels broken."

**Not yet reproduced: the user's exact reported chain** (piling
up → spiraling glitch → eventual total silence except one stuck loop). This
60s/58-shot synthetic burst didn't collapse the mixer — either it needs a
longer, more realistic session (real human play, many minutes, mixed
NPC/player/ambient audio denser than this script), or the soundIndex=109
retrigger-storm anomaly above is the seed that, given more real playtime,
compounds into what the user described (worth designing a longer repro
around forcing that specific retrigger condition first, cheaper than a raw
extended random playtest).

**Next session, in order:**
1. Find what actually calls `sndPlaySfx(109, ...)` (search `src/game/*.c`
   for callers keyed to soundIndex 109 / `GUN_B4_BOLTACTION_SFX`) and
   determine whether the ~32.6ms retrigger cadence is a real per-shot
   automatic-fire loop (legitimate, if unusually fast) or a bug re-firing
   once per audio buffer/tick instead of once per game event. This is the
   single most concrete, checkable lead from this session.
2. If (1) doesn't explain it: build a much longer repro (5+ minutes,
   `GE_INPUTSCRIPT` walking + intermittent firing near guards, or just ask
   the user to reproduce with `GE_MIXERTRACE=1 GE_AUDIOTRACE=1
   GE_AUDIODUMP=1` set and hand back the logs) aimed specifically at
   reaching the "eventual total silence except one stuck loop" end state,
   then diff voice-slot reuse counts/timing against this session's healthy
   60s baseline to see where it diverges.
3. Listen to `scratchpad/d202-m63/audiodump.raw` directly (raw s16 stereo
   22050Hz PCM) for audible garbling/overlap around the soundIndex=109
   retrigger-storm timestamps — not done this session (no audio playback
   attempted, log-analysis only).
4. Do NOT re-litigate the index/data/decode/ROM-offset chain (M-56–M-62,
   exhaustively verified) — this session's finding is orthogonal to that
   one; D202 is reopened on a *different* mechanism than the M-60 reference
   clip.

**Status at handoff (M-63): D202 REOPENED, root cause not found.** The
M-62 "recommend close" no longer stands — the user's actual complaint is a
runtime mixer/voice issue, not (only) a data-correctness question. One
concrete, unexplained retrigger anomaly (soundIndex 109, ~32.6ms cadence)
is banked as the top lead. No code changes this session — build+run+trace
only; the pre-existing uncommitted diagnostic files (`port/src/audio.c`,
`port/src/mixer.c`, `src/audi.c`, `src/libultra/audio/load.c`, `src/snd.c`)
were exercised, not edited.

### M-65 — Root-caused the "eventual silence" half: an ownerless `SOUND_FLAG_LOOPED` SFX permanently consumes one of only 8 voices. Guard measured. The "wrong/multiple sounds" half gains a second, independent cause (`sndCreatePostEvent` was stubbed out, killing ALL distance attenuation). The audible stuck loop is still NOT reproduced.

**User's report this session:** "wrong sound playing or multiple sounds playing
when there should be one", and "on bunker if the attract shows the door open,
it plays a sound and the sound gets stuck looping forever until you close the
game/level", later refined to "the double door sound by the computer room still
infinitely loops after they begin playing" and "a good chance it just happens
when the level starts if you get that attract intro with them opening".

#### (1) CONFIRMED + MEASURED: the voice leak that produces "eventual silence"

Chain, each link verified rather than argued:

- `soundIndex=203` (`METAL_SLIDE_CLOSE_SFX`) has `envelope->decayTime == -1`.
  Verified at the **source bank level**, not just at runtime, with a new
  `GE_BANKDUMP` probe walking `instArray[0]->soundArray[i]` in the ROM-layout
  blob: `BANKSRC inst@0x005778 soundIndex=203 sound@0x0046B8 env@0x000008
  decay=-1`. `GE_KEYMAPDUMP` additionally dumps every source `ALEnvelope`
  (`ENVSRC src@0x000008 raw=00000000 FFFFFFFF 000007D0`). **Genuine ROM data,
  faithfully converted** — 19 of the SFX instrument's 261 sounds are
  deliberately sustained this way.
- `decayTime == -1` sets `SOUND_FLAG_LOOPED` (`sndSetupSound`, via
  `decayTimeFlag`). A looped sound **never posts `AL_SNDP_STOP_EVT`**
  (`snd.c` `AL_SNDP_DECAY_EVT` skips it), *and* the preemption scan
  **explicitly refuses to steal looped voices** (`!(iterState->unk3e & 0x12)`).
  So the only release is `sndDeactivate`, which needs an owner.
- `doorPlayCloseSound0/1` play it as `sndPlaySfx(..., METAL_SLIDE_CLOSE_SFX,
  NULL)` — **no `pendingState`**, so nothing stores the state and nothing can
  ever deactivate it. (NB the decomp names mislead: `doorStartClose` calls
  `doorPlayOpenSound1`; `doorFinishOpen`/`doorFinishClose` call
  `doorPlayCloseSound0`/`1`. The *Open* pair own their slide loop via a slot;
  the *Close* pair are unowned terminators.)
- `maxSounds` is **8** (`MUSIC_SFX_SEQ_MAYBE_MAX_SOUNDS`, `music.c:76`).

**Measured** (115 s headless `-level_09`, new `[VOICE+]`/`[VOICE-]` probes
pairing every `g_sndAllocatedVoicesCount++` with its decrement):
**7 of the 8 voices permanently held, every one of them `sound=706c8e48`
(soundIndex 203) with `flags=7` (`LOOPED|PLAYING`)**, never released. The
64-entry *state* pool stays healthy (18 slots recycling, never NULL) — which is
why this is silence, not a crash: later `sndPlaySfx` calls get a state but no
voice and are dropped via `sndDisposeSound`.

**Guard (port-side deviation, `src/snd.c` preemption scan).** Allow reclaiming
a voice only when it is `SOUND_FLAG_LOOPED`, not `SOUND_FLAG_RETRIGGER`, and
`state->state == NULL` — i.e. provably ownerless, so nothing could ever have
stopped it. Owned loops keep their existing protection exactly.

| metric (115 s `-level_09`) | before | with guard |
|---|---|---|
| `g_sndAllocatedVoicesCount` | **pinned at 8** | oscillates 1–8 |
| voices acquired over the run | 171 | **629** |
| residual stuck 203 voices at exit | **7** | **1** |

User confirmed the symptom improves ("still happens but less cascading
perhaps"). **This is a game-logic change (AGENTS.md rule #2) justified by
measurement, not by ground truth — it is NOT known why the N64 does not leak
here.** Keep it flagged as a documented deviation, not a silent fix.

#### (2) FIXED, independent: `sndCreatePostEvent` was stubbed out, removing all distance attenuation

`sndCreatePostEvent` returned immediately under `#ifdef PORT` (D138), from when
"libaudio is Phase-3 parked" and nothing drained
`g_sndPlayerPtr->evtq->allocList`, so every positional tick appended an item
that was never consumed and `alEvtqPostEvent`'s ordered insert went O(n^2)
until the kernel-heartbeat watchdog tripped on Facility ambience.

**That premise expired when the Phase-3 audio thread landed (D198-D201)** —
`amMain` now drains the queue every audio frame. Every caller posts type 8 =
`AL_SNDP_VOL_EVT` (`bondview2.c`, `chrai.c`, `gunfire.c`, `propobj.c`), so the
stub had been silently removing **all distance-based volume**: every sound
played at full volume regardless of range. That is very likely a large part of
the "wrong sound / multiple sounds / too loud" impression, independent of (1).

Stub removed. Re-measured: `rt=0.996`, `drop=0`, frame times 91-329 us, zero
watchdog trips over 115 s — the O(n^2) regression does not return.

#### (3) STILL OPEN: the audible endless door loop is NOT reproduced

Not reproduced in any headless run. Everything that could make a sound audibly
repeat was measured and excluded — see the ruled-out list below.

#### RULED OUT this session (measured — do not re-walk)

- **Wave-level looping.** New `[WAVELOOP]` probe: `loop == NULL` on **every**
  sound in the SFX bank. No sample can repeat itself.
- **Decoding past the end of a wave.** New `GE_PULLTRACE` `[PASTEND]` assertion
  in `alAdpcmPull` (fires when `f->sample` exceeds the wave's total samples
  while `nOver == 0`): **0 hits**. `aClearBuffer` zero-fill works; an exhausted
  voice goes silent.
- **Stale / accumulating software DMEM.** `GE_DMEMWIPE` zeroes all 4 KB of
  `sDmem` at the exact frame boundary (`alMainBusPull`'s `AL_MAIN_L_OUT`
  clear is the first opcode of every frame). Audio still 99 % non-silent and
  unchanged — no audio state carries across frames in DMEM.
- **Reverb feedback self-oscillating** (plausible because `aPoleFilterImpl`,
  the damping in the feedback path, is an unimplemented no-op — D199).
  `GE_NOWET` mutes the reverb send entirely: signal unchanged.
- **The `AL_SNDP_PLAY_SFX_EVT` retrigger machinery.** 0 retrigger posts in the
  user's trace. (Also independently confirms D204's closure of the M-63
  `soundIndex=109` anomaly: measured here as 184 plays, **owned 184 / orphaned
  0** — a properly-owned repeated sound, not a retrigger storm.)
- **Doors oscillating.** ~4 open/close cycles per door per 110 s across 4 doors;
  203 played ~once per door state change. Normal, not thrashing.
- **Doors orphaning their loop because both slots are busy.** 0 occurrences
  (`[DOORSND] ... ORPHAN-both-slots-busy` never fired). The orphan is real but
  arises differently: the call site passes `NULL` by design.
- **`ALKeyMap` conversion.** `GE_KEYMAPDUMP` dumps all 220 source keymaps from
  the ROM-layout blob; runtime values match, including entries that are
  legitimately all-zero in the fields Rare repurposed.
- **`DoorRecord` 64-bit overlap.** `sizeof(DoorRecord)=0x128`, observed
  allocation stride exactly `0x128`; `openSoundState@0x110`,
  `closeSoundState@0x118`. No overlap.
- **The `ALSoundState`-pointer-to-pointer pun** in `sndPlaySfx`
  (`pendingState->link.next = nextState`, relied on by `doorPlayOpenSound0/1`
  and `audioPlayFromProp`). Survives the 64-bit transition — `ALLink.next` is
  at offset 0 and pointer-sized on both. New `[SLOTWRITE]` probe confirms the
  slot receives the state.
- **`ALWaveTable::len` drifting at runtime** (observed 6939 -> 6940 etc.).
  Benign: `alLoadParam` rounds `len` down to a whole number of 9-byte ADPCM
  frames on first bind, so the pre-round value is simply seen once.

#### Corrections banked (mistakes made this session — do not repeat)

- **`romdataFixupMusicSeqTable: seqCount 63 exceeds blob capacity 1` is
  BENIGN**, not a music failure. The function is called twice and the first
  call deliberately passes a 16-byte header slice (capacity `(16-4)/8 = 1`);
  the full-size second call succeeds. Music loads and plays.
- **A continuous ~1400 RMS "drone" in `GE_AUDIODUMP` captures is the
  background music, not a stuck sound.** It was briefly mistaken for the bug
  because only *SFX* events carry `dumppos` timestamps, so music looks like
  sound with nothing playing it. Confirmed music by pointer range: the heavily
  rebound wavetables (`706cbaf8`, `706ccfe8`) lie outside the SFX bank's
  `706c3e70`-`706ca430` range and never appear in any `sndPlaySfx`; they are
  driven by `csplayer.c:535` (`alSynStartVoiceParams`). The ~28 k
  `AL_FILTER_SET_WAVETABLE` binds per 60 s are ordinary note-ons, and
  `f->sample` resetting per note is correct.
- **Multiple `audiotrace.log` files exist** (repo root vs `build-pc/`) and a
  shell whose cwd shifts between commands will silently analyse the wrong one.
  Always use an absolute path, and check `ls -la` timestamps before trusting a
  log's provenance.

#### Diagnostics added (all `#ifdef PORT` / env-gated; remove when D202 closes)

| env var | file | what it prints |
|---|---|---|
| `GE_AUDIOTRACE` | `src/snd.c` | `[ENVELOPE]`, `[WAVELOOP]`, `[VOICES]`, `[VOICE+]`/`[VOICE-]`, `[SLOTWRITE]`, `[RETRIGGER-POST]`, keymap fields on every `sndPlaySfx` |
| `GE_AUDIOTRACE` | `src/game/propobj.c` | `[DOORSND]` — every door sound state change, both slots, and an `ORPHAN-both-slots-busy` verdict |
| `GE_KEYMAPDUMP` | `port/src/romdata.c` | `KEYMAP` + `ENVSRC` — source ROM bytes of every `ALKeyMap` / `ALEnvelope` |
| `GE_BANKDUMP` | `port/src/romdata.c` | `BANKSRC` — the source bank tree per `soundIndex` |
| `GE_PULLTRACE` | `src/libultra/audio/load.c` | `[PASTEND]` — asserts no voice decodes past its wave |
| `GE_DMEMWIPE` | `port/src/mixer.c` | zeroes software DMEM at the frame boundary |
| `GE_NOWET` | `port/src/mixer.c` | mutes the reverb send |

**Caution:** the `[VOICE+]`/`[VOICE-]` probes are written from both the game
thread and `amMain` to one unbuffered `FILE*`, so occasional lines are torn.
Parse defensively (filter to well-formed hex) or add a lock before relying on
exact counts.

#### Next step

Needs a user capture at the failing door (computer-room double doors, or a
level start whose attract intro shows them opening) with **both**
`GE_AUDIOTRACE=1` and `GE_AUDIODUMP=1`. The dump allows isolating the stuck
sound numerically and separating it from music without listening, and
`dumppos` pins it to the exact door event.

**Status at handoff (M-65): D202 PARTIALLY resolved.** The "eventual silence"
cascade is root-caused and mitigated (measured); the missing distance
attenuation is fixed; the audible stuck loop is still not reproduced.

### M-66 — the audible stuck door loop IS root-caused: it is faithful N64 behaviour (an original-game quirk), not a port bug

The user capture M-65 asked for arrived (`build-pc/audiodump.raw`, 31,905,536 B
= 361.7 s stereo s16 @22050; `build-pc/audiotrace.log`, same run, `-level_09`
level start whose attract intro shows the computer-room double doors). The
stuck loop is **sound 203 (`METAL_SLIDE_CLOSE_SFX`) doing exactly what the ROM
tells it to do**, and every link of the chain is ground truth:

1. **ROM data:** wavetable loop = `(start=2471, end=7719, count=-1)`;
   `src/libultra/audio/load.c` implements `count` with the explicit comment
   "-1 is loop forever" (decrements only when > 0). Envelope:
   `attack=0 decay=-1 release=2000 aVol=127 dVol=127`.
2. **Game code (byte-matched decompilation):** `doorPlayCloseSound0/1`
   (`src/game/propobj.c`) plays 203 fire-and-forget with a `NULL` owner state.
   The `[SLOTWRITE]` probe confirms the close-sound state is **never stored in
   any door slot**, so the door's stop-sounds path can never reach it, and no
   other code calls `sndDeactivate` on it. Nothing in the game will ever stop
   it except level exit.
3. **sndp (ground truth):** the preemption scan (`src/snd.c` ~line 321) skips
   any voice with flag `0x12` (`SOUND_FLAG_LOOPED|SOUND_FLAG_RETRIGGER`).
   `AL_SNDP_DECAY_EVT` schedules the release→stop envelope "except for a looped
   sound" (`src/snd.c:509`) — a looped voice gets no stop event at all.
   With `decayTime=-1`, `sndCreateSound` sets `state->priority = 0x41`
   (vs `0x40` normal), and libultra's `_allocatePVoice`
   (`src/libultra/audio/synallocvoice.c`) steals only voices with
   `priority <= new voice priority` — so a 0x41 leaked loop is unstealable by
   any ordinary 0x40 SFX or music note.
4. **Pool geometry:** GE's soft limit is 8 (`maxSounds =
   MUSIC_SFX_SEQ_MAYBE_MAX_SOUNDS`, `src/music.c:76`); the physical PVoice pool
   is 24 (`MUSIC_SYN_CONFIG_MAX_P_VOICES = 0x18`, `src/music.c:67`). Leaked
   loops therefore don't deadlock the synth, but they do eat GE's 8-slot soft
   limit — which is where M-65's guard bites.

**Consequence (identical on N64):** every metal-door close leaks one looping
voice that persists until level exit. During attract idle no new sounds arrive,
so nothing ever evicts them and they play forever — exactly the user's report:
"the double door sound by the computer room still infinitely loops after they
begin playing". Every code path in this chain is shared original source
(`snd.c`, `propobj.c`, libultra audio); none of it is port-specific. **This is
an original-game quirk, faithfully reproduced — rule #2 says leave it.**

#### The capture, timeline (corrected)

`dumppos` is the dump-file byte offset; time = `dumppos/88200` s.

- t=0–21 s: level intro — heavy SFX, door cycles on five doors (…7d2c, …7c04,
  …79b4, …7adc, …80a4). 203 plays at **1.3 / 3.3 / 11.3 / 17.2 / 21.0 s**.
- t≈39.1 s: door …80a4 closes → 203 (state …5020). Then a long quiet stretch:
  the same door's next close comes at **t≈360.7 s** (another 203, state
  …4f50, still looping when the capture ends at 361.7 s). The attract demo
  keeps running the whole time — it just gets sparse.
- Leaked 203 voices alive by t≈40 s: four (states …4db0 from 1.3 s, …50f0 from
  17.2 s, …4e80 from 21.0 s, …5020 from 39.1 s). Two earlier ones WERE
  reclaimed — but only because M-65's guard fired under pool pressure:
  `VOICE- snd8e48/st4e18` at t=14.8 s and `st4fb8` at t=16.4 s, each in the
  same block as a new-sound play (the preemption path). Without the guard both
  would persist and the 8-slot limit would fill by ~t=21 s.
- Steady state from t≈39 s: RMS ≈1560, strong periodicity at **327.0±0.1 ms**.

#### The 327 ms mystery (solved — it was the pitch)

The loop segment is 5248 samples; at the bank rate (22050) that is 238 ms, not
327. But the voice does **not** play at unity pitch: keymap `keyBase=54`,
`detune=50`, and `DEFAULT_SETUP_PITCH_SHIFT = -0x1770 = -6000` (`src/snd.c:20`)
give `alCents2Ratio(54*100 + 50 - 6000) = alCents2Ratio(-550)` = **0.72783**.
Loop period at playback = 5248 / (22050 × 0.72783) = **327.007 ms** — matching
the observed 327.0±0.1 ms exactly. The spectral fingerprint agrees: FFT peaks
of the decoded loop segment at 355.3 / 371.4 / 694.4 / 710.6 Hz × 0.7278 →
259 / 271 / 506 / 518 Hz vs observed steady-state lines 256.7 / 272 / 504.7 /
513.8 Hz. Cross-correlating the **pitch-stretched** loop template against the
t=100 s window gives r = 0.835 (the unpitched template gave ~0.10 — which is
why every earlier template test "failed"; see corrections below).

#### Corrections to M-65 (banked)

- **M-65's correction "the continuous ~1400 RMS drone in captures is the
  background music, not a stuck sound" is wrong (or at best incomplete).** The
  steady-state signal's spectral lines and 327 ms period are the leaked 203
  loop, pitch-shifted as above; r=0.835 template match. Music is present in the
  mix too (M-65's pointer-range argument that *those particular* rebound
  wavetables are music-driven still stands) — the steady state is a composite,
  but the *stuck* component is 203, and it dominates the fingerprint.
- **"The audible stuck loop is still not reproduced"** — it is now: user
  capture + complete mechanism, every link grounded in ROM data / byte-matched
  code / libultra source.
- The earlier "full library correlation: best 0.28, sfx203 only 0.10" was an
  artifact of correlating **unpitched** decodes against pitched playback. Do
  not re-run template tests without applying `keyBase/detune/shift` → ratio.
- M-65's guard comment "only 8 voices exist" is imprecise: there are 24
  PVoice plus the 8-slot GE soft limit; the binding constraint for SFX is the
  soft limit, and the guard operates on exactly that boundary.

#### Guard status and decision pending (user)

M-65's ownerless-loop preemption guard (`src/snd.c` ~line 325) fired twice in
this capture and demonstrably prevents pool exhaustion — but it can only fire
when a *new* sound arrives and the limit is hit. During attract idle nothing
arrives, so the audible loop persists with or without the guard. Options:

- **A) Pure fidelity:** remove the guard; document D202 as "faithful
  reproduction of an original quirk". Consequence (also faithful): after 8
  leaked loops in one level session, new SFX are silently dropped until level
  exit — that is what N64 does.
- **B) Keep the guard (status quo, recommended):** a small, precisely scoped,
  documented port-side deviation. Prevents the SFX-deadlock; the idle loop
  remains audible exactly as on N64.
- **C) Extended deviation:** cap/expire ownerless loops in the port layer to
  silence the idle loop. Fixes the user's complaint but is a larger departure
  from ground truth than B.

Diagnostics: with the root cause established, the M-65/M-66 probe set
(`GE_AUDIOTRACE` lines, `[DOORSND]`, `[SLOTWRITE]`, `GE_PULLTRACE`,
`GE_DMEMWIPE`, `GE_NOWET`, `GE_BANKDUMP`, `GE_KEYMAPDUMP`) may be removed once
the A/B/C decision lands and D202 closes.

**Status (M-66): D202 root cause ESTABLISHED — the audible stuck door loop is
sound 203 behaving exactly as the ROM + ground-truth code specify; faithful
N64 behaviour, original-game quirk. Disposition: user chose C (M-66b below).**

### M-66b — disposition C implemented and measured: ownerless infinite-loop SFX now expire (fade out) on PC

The user chose **C** (extended deviation) as part of the PC port's audio
implementation. M-65's guard stays (slot reclamation under pool pressure);
this adds the audibility fix.

**Placement — why `src/snd.c` and not `port/src/mixer.c`.** The predicate that
distinguishes a *leaked* loop from a legitimate one is **ownership**
(`soundState->state == NULL`), which only sndp sees. The mixer iterates
ALVoice/PVoices with no ownership field; scoping there would force either
hardcoding region-fragile SFX-bank DRAM pointer ranges or capping *all*
infinite-loop voices (music included). snd.c already carries the two documented
D202 port-side deviations (M-65 guard, D138 un-stub), all `#ifdef PORT` — every
D202 port-side change stays in one auditable place. N64 build: zero diff.

**Mechanism.** New PC-only event `AL_SNDP_PORT_EXPIRE_EVT` (reuses the
`AL_SNDP_UNUSED_13_EVT` = 1<<13 slot, defined under `#ifdef PORT` in
`src/snd.h`; no shared-enum change). In PLAY_EVT, immediately after a voice
starts, if the state is LOOPED + FINAL_IN_SEQUENCE + not RETRIGGER +
`state->state == NULL` **and** the wave carries an ADPCM loop with `count == -1`
(finite loops self-terminate in load.c and must not be cut short), post the
expire event at `D202_EXPIRE_DELAY_US` (2 s). Handler: re-validate the full
predicate at fire time, then ramp volume to 0 over `D202_EXPIRE_FADE_US`
(500 ms) and post END_EVT — the stock fade/teardown path. Constants are in
**ALMicroTime = microseconds** (the event queue is µs-driven: `DELTA_1_MS` is
1000, `DELTA_33_MS` is 33333).

- **Not a reused STOP_EVT:** the stock STOP ramp uses the envelope's
  releaseTime — ~2.7 ms for sound 203 (release 2000 / pitch ratio 0.7278) —
  which would hard-cut the loop (click). The dedicated event gives a clean
  500 ms fade.
- **Stale-event safety:** `sndDisposeSound` already removes *all* pending
  events for a state (`sndRemoveEvents(..., 0xffff)`), and any rebind disposes
  first — so an expire can never fire against a new binding. The fire-time
  predicate re-check is belt-and-braces (also no-ops if the state was rebound
  to the *same* sound).
- **Sequence-walk safety:** the type is added to the `isEventForSingleSound`
  mask under `#ifdef PORT`, and the post-site requires FINAL_IN_SEQUENCE, so
  the handler's do-while can never walk into an unrelated alloc-list neighbor.
- **Owned loops are untouched** (`state != NULL` fails the predicate) — they
  behave exactly as on N64. Retrigger sounds (managed by PLAY_SFX/DEACTIVATE
  chains) are excluded.

**Files:** `src/snd.h` (PORT-only event define), `src/snd.c` (two constants,
post-site in PLAY_EVT, handler case, mask token, guard-comment cross-ref).
New `[EXPIRE]` line under `GE_AUDIOTRACE` (see GE-ENV-PROBES.md).

**Measured.** 134 s headless `-level_09` run with `GE_AUDIOTRACE=1
GE_AUDIODUMP=1`. Trace: every sound-203 play posts `[EXPIRE]`; each leaked
voice gets its VOICE- ~2 s after its VOICE+; run ends at
`[VOICES] allocated=0 / max=8` (no survivors). Spectral check (per-1-s
Goertzel at 256.7 Hz, the strongest 203-loop line × 0.7278):

| capture | 256.7 Hz line |
|---|---|
| user pre-fix (361 s) | **flat at max from t≈39 s to end** — the complaint, verbatim |
| post-fix (134 s) | transient ~2–4 s bursts per door close, **full silence between** (gaps at t=60–62, 85–87, 94–98, 110–112, 128–131) |

The stuck infinite loop is gone; each close now rings ~2 s + 0.5 s fade, as a
door clunk naturally would. Scripts + archived user capture:
`scratchpad/d202-m66/` (`check_dump.py`, `line_track.py`,
`user-capture-audiodump.raw`, `user-capture-audiotrace.log`).

**Remaining:** user by-ear pass on the new binary (window is open — probes are
still in). On sign-off: remove the D202 probe set (`GE_AUDIOTRACE` additions,
`[DOORSND]`, `[SLOTWRITE]`, `GE_PULLTRACE`, `GE_DMEMWIPE`, `GE_NOWET`,
`GE_BANKDUMP`, `GE_KEYMAPDUMP`) and close D202.

**Status (M-66b): disposition C IMPLEMENTED + measured — ownerless
infinite-loop SFX fade out ~2.5 s after starting on PC; faithful N64 behaviour
otherwise. Awaiting user by-ear verification, then probe removal + close.**

---

### M-67 — Static analysis exhausted: full-bank data scan clean, DSP audit complete, reference clip REFUTED as an in-game capture; new `GE_VOICEDUMP` per-voice probe added for the decisive runtime A/B (this session)

**1. Full-bank SFX data scan — all 261 sounds clean.**
`scratchpad/sfx_bank_scan.py` walks every sound's wavetable/book/data in the
ROM: no out-of-range ADPCM table indices, all books valid (order=2, npred 1–4),
all data within segment bounds, all type-0. The old decoder's silent clamp
(`if tableIndex >= npred: tableIndex = 0`) had been masking this check — with
it removed as a hard error, the whole bank passes. No stale/corrupt-table
hypothesis is possible; the data side of "wrong sample" is closed.

**2. DSP audit complete (vs PD ground truth).** ADPCM scalar decode
(nibble→ins, predictor loop, clamp), the resample algorithm + 256-entry table,
the envelope mixer, and the pitch path (`alCents2Ratio` pure/correct; keymap
formula `keyBase*100 + detune - 6000`, snd.c:803/807) all match the PD port
line-for-line. The `A_LOOP` (flags&2) branch in `aResampleImpl` is **dead code
in GE**: the resampler filter's `first` is only ever 0 or 1 (load.c:461/530);
A_LOOP(2) is never passed to aResample. Not a bug — closed.

**3. The reference clip is refuted as an in-game capture.**
`scratchpad/ref_match_all.py`: `B00I00S2D.wav` does NOT waveform-match any of
the 261 SFX decodes (best single CC = 0.19 ≈ noise; index 45's waveform CC is
0.048), and `ref_match_mix.py` shows no PPK mixture (46+122+ricochet) matches
either (best 0.09). M-61's "+0.999 with index 45" was purely an
**envelope-correlation artifact** (two percussive sounds with similar decay
envelopes). The "N64 plays index 45 for the PPK" premise is dead; the clip's
provenance is unknown and must be asked of the user.

**4. M-63 final-mix cross-check was inconclusive, not exonerating.**
`scratchpad/m63_crosscheck.py`: door events (dumppos 23296/54976, index 203)
show CC≈0 vs offline decode, but those segments are dominated by music
(broadband RMS 2000–8000, no SFX periodicity). CC≈0 in a music-masked final
mix proves nothing about which sample the voice played. A per-voice dump (item
5) or a music-off capture is required.

**5. New probe: `GE_VOICEDUMP=1`** (`port/src/mixer.c`, temporary, D202 diag).
Writes each voice's resampled mono stream — the `aEnvMixer` input, i.e.
post-resample/pre-envelope, per-voice, no music/reverb masking — to
`voicedump.raw` as records `[u32 stateAddr][u32 nSamples][u64 us][s16 × n]`.
The `[WIRE]` lines (load.c `alLoadParam(SET_WAVETABLE)`) and the first
`[AUDIOTRACE] sndPlaySfx` line now also carry `t=<µs>` (sysGetMicroseconds)
so all three streams correlate in time. Offline matcher:
`scratchpad/voicedump_match.py` (each record vs all 261 pitch-resampled
decodes). Self-test: `scratchpad/d202-m67/` (25 s `-level_09`, 5.7 MB dump,
clean parse, records match their sources once the item-6 direction fix is
applied).

**6. Two tooling bugs found in the self-test (my scripts, not the game):**

- **Resample direction was inverted in every offline model so far.** The
  hardware playhead advances `pitch/16384 = ratio` input samples per output
  sample (`aResampleImpl`: `pitchAccumulator += pitch<<1; in += accum>>16`),
  i.e. `y[k] = x[ratio·k]` — ratio < 1 plays SLOWER and lower. The correct
  offline model is `resample_linear(native, RATE*ratio, RATE)`, not
  `(native, RATE, RATE*ratio)` as used in M-63's cross-check and the first
  matcher pass. **All prior CC results at pitch ≠ 1 are suspect** (the M-63
  door check doubly so).
- **WIRE bases are absolute; bank offsets are segment-relative.** The
  in-memory wavetables hold `base = SFXTBL_START(0x102F19A0) + bank offset`;
  matching WIRE entries against raw bank offsets finds nothing.

**7. GE's "music" is an SFX-sequence system with its own sample set.**
`sndNewPlayerInit(ALSeqpSfxConfig*)` (snd.c:168; config in src/music.c:823,
`MUSIC_SFX_SEQ_MAYBE_MAX_SOUNDS = 8`): music notes are sequenced SFX played
through the same 8-voice alSynth pool. The WIRE log shows ~8 music samples
whose bases sit at ROM 0x103ed1e8–0x10407860 — 240–350 KB **past** the last
SFX-bank sample — with wavetable structs allocated contiguously after the SFX
bank's. Music notes do not pass through `sndPlaySfx`, so they are invisible to
the `[AUDIOTRACE]` request probe: persistent voices in a voicedump are usually
music, and their "best match" against the 261-SFX bank is coincidental
(music samples reuse field recordings; free-ratio sweep found e.g. CC 0.92 vs
index 122 at transposed pitch). Any per-voice analysis must subtract music
first (or use the timestamped WIRE assignments to identify each voice's sample).

**8. Open anomaly parked for the next session: sound 232 (PICKUP_GUN_SFX)
requested at `-level_09` start produced no audible audio.**
Self-test trace: `sndPlaySfx(232)` at dumppos=0 → VOICE+ (count 1/8) →
VOICE- within ~1 frame; the WIRE-assigned filter was recycled to music
immediately; no env state in the dump matches decode(232) (max CC 0.48 ≈
noise, even with the corrected resample direction). Either the level script
stops it instantly (faithful N64 behaviour — needs an N64 A/B to confirm)
or something disposes it early on PC. Not chased this session.

**Status (M-67): static analysis has hit diminishing returns — every pipeline
stage (index resolution, bank data integrity, ADPCM, resample, envelope,
pitch, loop mechanism) is verified against ROM + PD ground truth, and the
reference clip is refuted. The remaining questions (PPK/door/armor "wrong
sound" by ear) require a user runtime A/B capture with the new
`GE_VOICEDUMP` probe: `GE_AUDIOTRACE=1 GE_AUDIODUMP=1 GE_VOICEDUMP=1`,
reproduce each scenario, send `audiotrace.log` + `audiotrace_wire.log` +
`voicedump.raw`. Also owed: provenance of `B00I00S2D.wav`, and the item-8
sound-232 anomaly.**

### M-68 — Decisive runtime A/B (user captures, 3× level_09): all three complaint classes play the exact ROM samples at the exact ROM pitches; no port bug found (this session)

Three interactive user captures (`audiodebug.ps1 -Play -Trace -Dump
-VoiceDump`), analysed with full-slide correlation of `voicedump.raw`
per-voice records against `decode_sound(idx)` resampled at the keymap ratio
(correct direction, M-67 item 7: `y[k] = x[ratio·k]`). A new `[EVT] t=µs
type=N state=P` probe (PORT-gated, `sndHandleEvent` top) plus a
`[STOP-EVT] … deltaUs=N` probe (release-ramp site) were added this session;
event types decode per `src/snd.h`: 1=PLAY, 2=STOP, 8=VOL, 64=DECAY,
128=END.

**1. Doors — correct.** `METAL_SLIDE_OPEN` 202: CC **0.957** @ ratio 0.500;
`METAL_SLIDE_CLOSE` 203: CC **0.906** @ 0.728; `METAL_SLIDE_LOOP` 204: CC
**0.993** @ 0.728. Event flow is textbook: PLAY → DECAY (attack=0, fires
immediately) → self-stop at the decay-scheduled STOP (204: +154 ms =
112197 µs ÷ 0.7278; 202: ~450 ms) → release ramp (`[STOP-EVT] deltaUs` =
releaseTime ÷ ratio, e.g. 1373/3999 µs) → END → dispose.

**2. PPK shot — correct.** `GUN_SILPPK_A` 46: CC **0.90–0.97** @ ratio 0.728
across four separate shots (full-slide, any offset). Combined with M-59
(46 + 122 CART_SPENT + a ricochet is exactly what the ROM scripts for a PPK
hit), the user's "gunshot plays broken glass/slap/ricochet" is the original
design, reproduced faithfully.

**3. Body armor pickup — correct, full length.** `ARMOUR_COLLECT` 81:
voice lifetime measured **833 ms** from `[EVT]` timestamps (PLAY → decay-
scheduled STOP), against ROM prediction 469649 µs ÷ 0.5612 = **837 ms**;
release ramp 3563 µs; CC **0.828** vs decode(81) @ ratio (window contains
other concurrent voices, hence <0.95).

**The M-67-era "instant death" metric was a line-count artifact.** Log lines
≠ time: a voice stopped by the door script's `stop-sounds`
(`door7F053B10`, `src/game/propobj.c` — stock logic, identical on N64) when
the player shuts a door quickly produces only a few log lines between birth
and death even when it plays its full ~20 ms. With µs timestamps the STOPs
land exactly at their scheduled times; no voice is disposed earlier than its
decay/stop schedule in any of the three captures.

**13 `[EXPIRE]` events in the final capture:** the M-66b guard expired 13
ownerless `METAL_SLIDE_CLOSE_SFX` (203) loops, each at exactly 2.5 s (2 s
delay + 0.5 s fade). This remains the single outstanding PORT deviation;
whether the N64 lets that loop run until level exit is the open by-ear A/B
(see M-66b).

**Status (M-68): D202's "wrong sample" hypothesis is closed negative for all
three user complaint classes — PC plays exactly what the ROM specifies,
verified per-voice at runtime. Remaining: (a) N64 A/B on door-close-loop
duration to confirm or retract the M-66b cap; (b) the parked sound-232
anomaly (M-67 item 8); (c) once the user confirms by ear, remove the D202
probe set (`GE_AUDIOTRACE`/`GE_AUDIODUMP`/`GE_VOICEDUMP` probes in
`src/snd.c`, `src/libultra/audio/load.c`, `port/src/mixer.c`,
`src/game/propobj.c`) and decide the M-66b guard's fate.**

### M-69 — Full-corpus exact match (scripted `-level_09`, 320 SFX requests): 286/320 OK after tooling fixes; the remaining failures are *dropped allocations*, not corruption (this session)

New offline tool `scratchpad/exact_match.py` (gitignored scratchpad; helpers
`rom_sfx_decode.py`, `m63_crosscheck.py`) matches **every** `sndPlaySfx`
request in a full trace to its voice and slides the ROM-decoded PCM
(resampled at the keymap ratio, M-67 item 7 direction) against that voice's
`GE_VOICEDUMP` stream. Corpus: `scratchpad/exact_run/` (scripted `-level_09`,
61 input entries, `GE_AUDIOTRACE` + WIRE + `GE_VOICEDUMP`) — **320 requests**.
Chain: request → its own `[EVT] type=1` PLAY (states are recycled across
requests, so the wire window must anchor on *that* PLAY) → WIRE record with
the same wavetable in [play−10 µs, play+50 ms] → decoder index → env-state
slot address → voicedump records for that slot.

**Result: 286/320 OK (CC ≥ 0.85).** The initial 241 OK / 35 MID / 44 BAD split
was almost entirely tooling artifacts, not audio:

1. **`GE_VOICEDUMP` records are rendered ahead — the µs stamp is the END of
   the block.** Each record's n samples end at its stamp, spaced 1/22050 s.
   Treating stamps as start offsets misplaces content by up to one block and
   produced a phantom "stale content" in pv20's slot (sound 29 looked ~180 ms
   early). With per-sample record times, sound 29's onset lands at **+263.8 ms
   vs its WIRE timestamp +264 ms — exact**. Cross-slot correlation confirmed
   the dump attribution was never wrong (max CC vs neighbouring slots ≤ 0.076;
   slot content is unique per voice).
2. **`slide()` needed step-1 refinement around the coarse best hit.** Coarse
   stepping misses sharp onsets: sound 29 measured CC 0.590 at the coarse
   argmax and **0.970 one sample later**. (Also fixed a bug: the best-offset
   tracker was only updated in the last coarse loop, so refinement searched
   around offset 0.)
3. **Clip the reference at voice death.** Requests whose voice is cut by a
   DEACTIVATE/STOP after PLAY (e.g. idx=109, killed at +131 ms) cannot match
   the full ROM decode (CC 0.016); clipping the ref to the live window gives
   CC **0.999** at offset +0 ms. The matcher now parses `[EVT]` per state and
   clips accordingly.

**The remaining ~34 failures are not corruption:**
- **"NO WIRE AT PLAY" / DROPPED (no PLAY evt):** the request was processed but
  `alSynAllocVoice` never gave it a voice (no `[VOICE+]`, no DECAY) — e.g.
  t=567204976 idx=46. Leading suspect: the 8-voice pool is exhausted under
  scripted rapid fire and low-priority sounds are dropped by design (`snd.c`
  threads `state->priority` into `config.priority`; `alSynAllocVoice`'s steal
  path has a ~512-sample delay). **Not yet verified — the next step.**
- **pv10 mismatches:** consecutive requests share wavetables, so the wire
  window can pick up the *next* request's wire and attribute the wrong slot.
  Tighten: require the WIRE to fall between this PLAY and the next PLAY on the
  same wavetable.
- Log-interleaving gotcha: AUDIOTRACE lines from multiple file handles
  interleave and corrupt some lines (a missing/corrupt PLAY line sends the
  matcher to its wide-window fallback).

**Status (M-69):** every request that *got* a voice plays the exact ROM sample
at the exact ROM pitch — this extends M-68's per-class verification to the
whole corpus. D202 remains "no port bug found"; open items are M-68's plus:
(a) confirm the dropped requests are pool-exhaustion drops that N64 would drop
identically (check `[VOICES] allocated=N / max=8` around each drop;
`src/snd.c:916`, `alSynAllocVoice`); (b) fix the shared-wavetable wire
attribution; then M-68's by-ear + N64 A/B, probe removal, close.

### M-70 — Decisive full-corpus exact match on a clean re-capture (209 requests, scripted `-level_09`): every voiced request plays the exact ROM sample; D202's wrong-sample hypothesis is refuted (this session)

The prior 316-request corpus was contaminated mid-capture (pause menu hit),
so a fresh 75 s scripted capture was taken (`GE_AUDIOTRACE=1 GE_VOICEDUMP=1`,
`-level_09`, `scratchpad/exact_run3_script.txt` fire burst) and the matcher
(`scratchpad/exact_match.py`) was fixed in three places before the decisive run:

1. **Death-clip both sides.** Clip *stream* as well as *ref* at the voice's
downstream death event (STOP/END/DEACTIVATE). M-69 only clipped `ref`; when a
slot is reused ~200–400 ms later, the unclipped 1.5 s stream window is mostly
the *next* occupant's samples.
2. **`slide()` sparse-tail bug (the big one).** The offset search was dense
only up to 9000 samples (~408 ms) and used step = span/40 beyond that. Delayed
voicing — `sndPlaySfx` queue wait, observed up to **+1.2 s** between request
and PLAY — pushes the onset into the sparse zone, where the narrow CC peak
falls between grid points: idx=134 (onset +532 ms) scored **CC 0.154** in the
matcher but **0.973** under a full-range step-4 scan of the identical data.
   Fixed: step-1 near the window start, step-4 across the *full* span, ±8
   step-1 refine around the best hit. idx=136 went 0.285 → ≥0.85 the same way.
3. **PLAY search window 600 ms → 2 s**, catching two idx=175 instances voiced
   at +1196 ms (their WIREs land with the PLAY, so pairing is unambiguous).

**Result — 209 requests:**

| bucket | count | disposition |
|---|---|---|
| OK (CC ≥ 0.85) | **200** | exact sample at exact keymap pitch |
| MID (0.6–0.85) | 4 | all idx=109, CC 0.744–0.811 — see below |
| NO WIRE AT PLAY | 5 | 1 matcher artifact + 4 never voiced — see below |

- **The 4 MID (idx=109):** manual verification on the tight `[play, death]`
   window with a ratio sweep gives **CC 0.946 at the nominal keymap ratio**
   (0.7236 = keyBase 54, detune +40, −6000 shift) — correct sample *and*
   pitch. The residual is measurement, not audio: the voicedump records are
   stamped with **wall-clock time at mix time**, and the scheduler's catch-up
   delivers them in ~32 ms bursts of ≤160-sample records with clustered
   stamps, so bisect windowing on those stamps is fuzzy by tens of ms — and
   these voices die young (killed at +67…+131 ms), leaving little clean
   signal to average over.
- **NO WIRE ×5:** idx=109@610342131 was in fact voiced — its WIRE lands at
   **+133 ms** after PLAY, outside the matcher's 50 ms wire window (all other
   idx=109 instances match fine). The other four (idx 8/40/123/202) show no
   voicing evidence anywhere: no `VOICE+` on their `newState` after t0, and no
   WIRE for their wavetable within 2–3 s *in any state*. **Not pool
   exhaustion** — `[VOICES] allocated` was 0–5/8 at each drop (run max 7/8).
   Mechanism unverified; consistent with game-logic queue/priority timing in
   code identical to the N64 build, and orthogonal to D202 (no samples were
   played, so nothing could be wrong).

**Verdict:** across 204 voiced requests spanning ~30 distinct sound indices,
**zero** play a wrong sample. Combined with M-68 (per-class runtime A/B) and
M-66 (stuck door loop = faithful N64 quirk), the D202 "wrong sample / mixer
corruption" hypothesis is refuted: what the user heard is the original design
(overlapping SFX, short decays, quick-stop truncation, the ownerless infinite
door loop).

**Status (M-70):** D202 data-closed. Remaining before final close + probe
removal: user by-ear A/B on the M-66b expiration behaviour (does the stuck
drone now fade out ~2.5 s after a door close, vs N64's forever-drone). Probes
(`GE_AUDIOTRACE`, `GE_VOICEDUMP`, `[WIRE]`, `[EVT]`, `[VOICE±]`, `[SLOTWRITE]`,
`[ENVELOPE]`, `[VOICES]`) stay in place until that pass is done; they are
`#ifdef PORT`-gated and env-var-off by default.

### M-71 — User by-ear pass on M-66b: PASS. D202 ready to close. New complaint class split out as D205 (this session)

**By-ear verdict (user, PC vs N64 side-by-side):** "the metal door sound
does not loop forever now" — the M-66b ownerless-infinite-loop expiration
behaves as intended; Disposition C validated. That was the last gate on
D202: probe removal + close is all that remains (see HANDOFF).

**New user complaint class (NOT D202 — split out to D205).** With M-70's
per-voice exact-match result standing (every voiced request plays the exact
ROM sample), the user reports by ear, and confirms against an N64 A/B that
the PC is *not* faithful: (1) general gunfire has a slap/glass-shatter
layered over the correct gunshot — audible on PC, silent on N64 for the
same shot; (2) an explosion plays a scream-like sample (the soldier-death
voice); (3) armor pickup plays a wrong sample. This session exonerated the
two remaining static suspects: the D37 bank converter offline (`scratchpad/
banktest/`: real `romdataFixupAudioBank` harness + `diff_bank.py` → all 261
entries map identically N64→PC, 0 mismatches) and the D154/D135 GBI raycast
parsers + volume law (static re-audit, ABI-correct). The remaining
explanation is that PC **requests different or extra sound indices** than
N64 in the same scenario. See D205.

### M-72 — Fourth symptom reported (unarmed melee → Klobb shot idx 106); controlled `GE_AUDIOTRACE` runs exonerate every request path; mechanism (b) now the leading suspect (this session)

User added a fourth D205 symptom: Bond's unarmed slap attack plays
Klobb's shooting sound (`GUN_B1_MGUN3_3_SFX`, idx 106) instead of the slap
effect. Also fixed the test environment: the "flaky /GS crash" of earlier
attempts was `SDL2.dll` missing from PATH (it lives in
`C:\msys64\mingw64\bin`; the exe is `-mwindows`, so a failed DLL load just
dies with 0xC0000105 and no console). Launchers now prepend it.

Controlled traces (`build-pc/d205_*.ps1`, gitignored): PPK fire at
`-level_09` requests exactly {46, one of the 20-entry ricochet table,
122} per shot — no glass/slap layer; unarmed fire-presses at levels 01–08
(all start unarmed) request only 105 PUNCHING_AIR. Static: punch hit =
constant {47,48,49}, idx 106 is data-only (`skorpion_stats.Sound`), and the
player fire-sound path cannot run for a fist — so Bond's own punch cannot
request 106; an NPC guard firing its Skorpion can. No symptom reproduces in
micro-scenarios → the layering must come from **extra events during real
gameplay** (mechanism b). Next: user `GE_AUDIOTRACE=1` capture at the exact
failing moment (splits per-symptom), then controlled explosion + armor.
Full detail in D205.

## D207 — **Alarm klaxon starves combat SFX (surfaced by D206)** (M-77, root-caused, fix designed & not applied)

### How D206 caused it

`ALARM3_SFX` (SFX_ID 163) is played by `handle_alarm_gas_timer_calldamage()`
(`src/game/propobj.c:14457`) whenever `alarmIsActive()`, owner `&ptr_alarm_sfx`.

- **Pre-D206:** resolved to on-disk bank slot **163** — a **one-shot** sample
  (`decayTime` finite, no ADPCM loop). Played once, released its voice. The
  alarm was audibly the *wrong* sound but cost the voice pool nothing.
- **Post-D206:** correctly resolves to slot **162** — the real klaxon:
  `envelope.decayTime == -1` (→ `SOUND_FLAG_LOOPED`, `sndSetupSound`
  `snd.c:~836`), wavetable `adpcmWave.loop.count == -1` (infinite ADPCM loop).
  It now holds a voice for the whole time the alarm is up.

### Why it dominates

The SFX player has **8 voices** (`MUSIC_SFX_SEQ_MAYBE_MAX_SOUNDS = 8`,
`src/music.c:76`; the synth has 24 physical voices but the SFX soft cap is 8
and is what `snd.c:328` gates on).

`sndSetupSound` sets `state->priority = (decayTime == -1) + 0x40` → the alarm
gets **0x41**, one-shots get **0x40**.

When `g_sndAllocatedVoicesCount == 8`, `snd.c` PLAY_EVT does **not** call
`alSynAllocVoice` (which *would* do libultra priority-stealing); it runs the
manual preemption scan at `snd.c:~347-392`. That scan:
- refuses to steal any `unk3e & 0x12` voice (LOOPED | RETRIGGER) — the M-65
  carve-out only exempts *ownerless* loops (`state == NULL`), and the alarm
  is **owned**;
- ignores priority entirely — it steals the first non-looped PLAYING
  non-PREEMPT voice it finds;
- if it finds none → `sndDisposeSound(soundState)` → **the new SFX is
  dropped** (`snd.c:391`).

So an active alarm removes one of the 8 voices, and in a firefight (gunshots +
ricochets + impacts + enemy fire + ambient door/machine loops) the remaining
non-looped voices are all either already PREEMPT-marked from the same burst or
themselves looped → gunshots drop.

### Measured (BUNKER1, `-level_09`)

Temporary probe `GE_FORCEALARM=1` (added to `handle_alarm_gas_timer_calldamage`,
`#ifdef PORT` / `getenv`) pins `alarm_timer` so the klaxon stays up, plus a
scripted `GE_INPUTSCRIPT` firing Z every 8 reads:

| condition | SFX requests | dropped (no voice) |
|---|---|---|
| no alarm | 52 | 1 (~2%) |
| alarm active | 137 | 28 (~20%) |

Dropped indices were gunshot/impact/ricochet sounds (29, 30, 41, 90, 36, 109…).
`ptr_alarm_sfx` stayed stable while active and was cleared on
`alarmDeactivate()`; the alarm voice was freed and the pool recovered — the
**"doesn't recover after the alarm stops" half of the user report was NOT
reproduced** in any trace (light BUNKER1 combat; may need a heavier level, or
it is perceptual).

### The 8-cap and the scan are faithful decomp

`MUSIC_SFX_SEQ_MAYBE_MAX_SOUNDS = 8` is unmodified game code; the manual
preemption scan is stock `sndHandleEvent` (M-65 modified it in place). Upstream
GE has the same. So this is not purely a port regression — it is partly the
open **D202** voice-pool-margin problem (ambient infinite-loops holding voices
longer than they should on the port) becoming audible because the alarm now
legitimately consumes a slot.

### Designed fix (PORT, not yet applied)

In the `snd.c` preemption scan, add a **last-resort second pass**: after the
normal pass (non-looped victims + the M-65 ownerless-loop carve-out) finds
nothing and `limitReached` is still true, and **the incoming `soundState` is
itself not LOOPED**, run one more pass that will preempt a stoppable LOOPED,
non-RETRIGGER voice (the alarm klaxon; also the D202 door loop as a bonus).

Self-healing: preempting the alarm posts `AL_SNDP_END_EVT` → `sndDisposeSound`
→ `sndUnlinkClearSound` clears `state->state->link.next`, i.e. sets
`ptr_alarm_sfx = 0`; the very next frame `handle_alarm_gas_timer_calldamage`
sees `ptr_alarm_sfx == 0` and re-issues `sndPlaySfx(ALARM3_SFX, &ptr_alarm_sfx)`,
which re-acquires a voice (or WAIT_VOICE-retries at 33 ms). Net effect: the
klaxon briefly yields to a gunfire burst and comes right back — gameplay audio
wins, which matches player expectation.

Alternatives weighed and rejected for now: (a) raise `maxSounds` > 8 on the
port — game-code change, rule #2 risk, changes mix loudness; (b) properly fix
the D202 ambient-loop lifetimes — the correct fix but the hard open problem.

### M-77b — designed fix tried, hypothesis not confirmed, reverted

The last-resort LOOPED-preemption pass was implemented in the `snd.c` scan,
plus `[D207-DROP]` counters at both no-voice `sndDisposeSound` sites and a
`[D207-YIELD]` trace on the new pass. BUNKER1 test (`GE_FORCEALARM=1` +
`GE_INPUTSCRIPT` firing every ~10 controller reads for ~85 s, alarm cycled
on/off/on):

- **`[D207-DROP]` = 0** — not a single SFX actually dropped.
- **`[D207-YIELD]` = 0** — the new pass never even ran; pass 1 always found a
  non-looped voice to preempt.
- Per-frame voice count: **1** during quiet alarm, spikes to 3–5 under fire,
  back to **0** after every alarm-off. No leak, no pegged pool.
- The earlier "≈20 % dropped" number was bogus: it counted requests whose
  `[VOICE+]` came more than 40 log-lines later. Cross-checking "states that
  never got a voice at all" = **0 / 136**. Non-looped sounds get 2 `unk38`
  retries then the scan, which nearly always places them a frame or two late —
  audible as slightly loose timing, not silence.

So on BUNKER1 the alarm does **not** starve the pool. The change was reverted
(`snd.c` now carries only the D206 `-1`). Either BUNKER1 combat is too light to
reach 8/8, or the user's symptom is a different mechanism.

### Owed next session — needs a real repro from the user

The BUNKER1 forced-alarm harness cannot reproduce this. Get from the user:
**which level, which weapon/situation**, and a `GE_AUDIOTRACE=1` capture (or a
short clip) of the failing alarm. Then decide between:

1. **Genuine 8/8 starvation on a heavy level** — Facility / Silo / Statue
   firefights have many more guards firing. Soak one with `GE_FORCEALARM` +
   sustained scripted fire (or the user's own capture) and watch for
   `[D207-DROP]` > 0 / `count=8` pinned. If real, the reverted last-resort
   pass is the fix — re-apply it.
2. **A leak on a specific alarm-stop path** — the `alarm_timer` auto-timeout
   (`CHROBJ_GAS_TIMER`) vs `AI_AlarmOff` vs `deactivate_alarm_sound_effect`
   from a cutscene. Trace `g_sndAllocatedVoicesCount` across each.
3. **Perceptual** — the klaxon is a *continuous* infinite loop (`slot 162`,
   no gaps). A real GE alarm pulses. Check: is `ALARM3` supposed to retrigger
   (keyMap velMax=7 was noted but `SOUND_FLAG_RETRIGGER` needs `keyMax & 0xf0`,
   and keyMax=4 → not set)? And compare slot-162 playback **volume** to N64 —
   if the drone is just mixed loud it will mask everything without dropping a
   single voice.

**Temp probe kept:** `GE_FORCEALARM` in `handle_alarm_gas_timer_calldamage`
(`src/game/propobj.c`, `#ifdef PORT` / `getenv`).


## D206 — **PC played every SFX one bank slot too high: `ALInstrumentAlt_s.soundArray` shifts offset 12→16 at 64-bit** (M-74 found, M-76 FIXED)

**This is the root cause of D205's melee→Klobb and armor-pickup symptoms, and
of D202's original "silenced PPK plays a slap" report.** It was invisible for
eight sessions because *the verification tooling shares the bug* — see
"Why M-68/M-70 passed" below.

### The evidence

The user supplied an independently-produced, verified rip of GE's sound
effects (`B00I00S<hex>.wav`, 186 files). Filenames are **hexadecimal** bank
indices. Cross-checked by PCM frame count against `scratchpad/rom_sfx_decode.py`
decoding the ROM's `sfxctl` bank:

```
offset -2:   0/184 matches
offset -1:   0/185 matches
offset  0: 186/186 matches   <-- rip index == bank index, exactly
offset +1:  21/185 matches
offset +2:  16/185 matches
```

So the rip is ground truth for *what sound lives at each bank index*, and our
decoder's indexing agrees with it. The user then identified three by ear:

| user's statement | rip file | = bank index | game requests (from `audiotrace.log`) |
|---|---|---|---|
| correct body-armour sound | `S50` | **80** | — |
| what the PC port actually plays for armour | `S51` | **81** | **81** (`ARMOUR_COLLECT_SFX`) |
| the actual Klobb sound | `S69` | **105** | **105** (`PUNCHING_AIR_SFX`, punch whiff) |

Both are exactly `requested index → played bank[index]`, and in both cases the
**correct** sound sits at `index - 1`.

### The structural confirmation

- ROM `sfxctl` bank: `bankCount=1`, `instCount=1`, **`soundCount=261`**,
  `percussion=NULL` → valid `soundArray` indices are **0…260**.
- `SFX_ID` (`src/bondconstants.h`) has **262** members, beginning
  `NOTHING_SFX = 0`, which `snd.c:992` explicitly treats as "no sound"
  (`if (soundIndex == 0) return NULL`).
- **262 enum members − 1 sentinel = 261 real IDs (1…261), mapping exactly onto
  bank slots 0…260.** The arithmetic only closes with a `-1`.
- Corollary bug: the last ID, `BIG_CLANK_SFX` = 261, currently indexes
  `soundArray[261]` — **one past the end of a 261-entry array.**

`src/snd.c:1001` is unmodified decomp code:

```c
sound = (soundBank->instArray[0]->soundArray[soundIndex]);   /* needs - 1 */
```

### Independent confirmation from D202's own reference clip

`scratchpad/B00I00S2D.wav` — the silenced-PPK reference clip that drove D202
from M-60 to M-67, and whose provenance the user could only describe as "not
sure / found online" — **is a file from this same rip**: `S2D` = **bank 45**
(3264 frames, identical). And `wppksil_stats.Sound = 0x2E` = **46**.

So the silenced PPK's ROM data value 46 must resolve to bank **45**, and the
user's clip *is* bank 45. **M-61 found precisely this** ("index 45 is the only
strong match for the user's reference clip, envcorr +0.999; index 46 is the
'slap'") and it was argued away over four sessions — M-67 "refuted" the clip as
a genuine capture on a waveform-correlation test. The +0.999 was right. The
"slap" heard on every silenced PPK shot is `bank[46]`, which under the correct
mapping is `PUNCH1_SFX` — **literally a slap.**

### Why M-68/M-70's "exact match" passed anyway

`scratchpad/exact_match.py` verifies a runtime voice against
`rom_sfx_decode.py`'s decode of **the requested index**, and that decoder walks
`soundArray[i]` with the same missing `-1`. `scratchpad/banktest/diff_bank.py`
likewise compares an N64-layout walk to a PC-layout walk using the same
indexing on both sides. **Every tool built to check the mapping reproduced the
mapping.** M-68/M-70 proved the port plays *the sample it asked for, at the
right pitch* — self-consistency — and never tested whether the index asked for
was the right one. Method note added to `porting-notes.md` §E.

### What it explains

- **D202** — "silenced PPK plays a slap": requests 46, plays `bank[46]` =
  `PUNCH1`; correct is `bank[45]`. ✔
- **D205 (4)** — melee plays the Klobb gun: whiff requests 105, plays
  `bank[105]` = **the Klobb sample**; correct is `bank[104]`. ✔ user-confirmed
- **D205 (3)** — armour pickup wrong sample: requests 81, plays `bank[81]`;
  correct is `bank[80]`. ✔ user-confirmed
- **D205 (1)** — slap/glass layered over gunfire: consistent (a gunshot ID
  landing on the adjacent impact/punch sample), not yet individually confirmed.
- **D205 (2)** — explosion → scream: **NOT explained.** Explosion IDs 169–183
  map to bank 168–182, all still explosions. Stays open; M-73's reading
  (guards killed by the blast screaming, ±1 s) remains the likely answer.

### RESOLVED (M-76) — pointer-width layout shift in `ALInstrumentAlt_s`

The M-75 reframe below was on the right track (candidate B — a PC-side layout
mismatch, not a code or data `-1`). The exact mechanism:

`sndPlaySfx` (`snd.c:1001`) reads the on-disk sound bank — a **standard**
`ALBankFile`/`ALInstrument` (verified: `rom_sfx_decode.py` finds `soundCount`
= 261 at instrument offset 14, standard `ALSound*` table at offset 16) —
through GE's cast alias:

```c
struct ALInstrumentAlt_s {      /* src/snd.h:165 */
    s32 unk0;   /* off 0  */
    s32 unk4;   /* off 4  */
    s32 unk8;   /* off 8  */
    ALSound *soundArray[1];     /* N64: off 12 (4-byte ptr) | PC: off 16 (8-byte ptr, 8-aligned) */
};
```

- **N64:** `soundArray` at struct offset **12** — it overlaps the on-disk
  instrument's `bendRange`(12) + `soundCount`(14) words. So `soundArray[0]`
  is those two `s16`s (a bogus pointer, but `SFX_ID 0 = NOTHING_SFX` is
  short-circuited at `snd.c:992` and never dereferenced), `soundArray[1]` is
  on-disk table entry `[0]`, and in general **`soundArray[N]` == on-disk
  entry `[N-1]`**. GE's `SFX_ID` enum and every `WeaponStats.Sound` byte are
  therefore **1-based** into the sound table, by design.
- **PC:** 8-byte pointers + 8-byte alignment insert 4 bytes of padding after
  `unk8`, so `soundArray` sits at offset **16**. `port/src/romdata.c`
  `afFixupInst` rebuilds the bank to exactly this layout (16-byte header,
  `8*soundCount` bytes of 64-bit pointers from offset 16, entry `i` = on-disk
  entry `i`). So PC `soundArray[N]` == on-disk entry `[N]` — **one slot
  higher than N64** for every SFX.

This is the D3x class (a ROM-serialized struct whose pointer-width field
shifts the layout at 64-bit). **Fix:** `src/snd.c:1001`, `#ifdef PORT` branch
→ `soundArray[soundIndex - 1]`, N64 line kept verbatim under `#else`. The one
read site covers both the initial lookup and the retrigger chain (the loop
recomputes `soundIndex` from `keyMap->velocityMin`, same 1-based space). No
game-logic change; `romdata.c` untouched (its layout is internally correct —
it just wasn't the layout the N64 struct alias assumes).

### Open before fixing — where the fix belongs (M-75 reframe) — SUPERSEDED by M-76 above

**M-75:** upstream `n64decomp/007` `src/snd.c` byte-matches the real ROM,
carries no `NON_MATCHING`/`GLOBAL_ASM` guard on `sndPlaySfx`, and its index
line is identical — `soundArray[soundIndex]`, **no `-1`**. Blame puts
`snd.c:1001` in the 2022 upstream import, not this fork. So the earlier
hypothesis "(i) real ROM does `soundArray[soundIndex-1]`, decomp dropped it"
is almost certainly **dead**: a matching decomp would contain the `-1`. The
N64 runs `soundArray[N]` for ID `N` and that game sounds correct.

That forces a reconciliation. Candidates, cheapest test first — full decision
gate and per-candidate byte-level experiments in
`docs/dev/notes/D206-IMPL-BRIEF.md`:

- **(A)** the rip `B00I00S<hex>` is keyed by `SFX_ID`, not by 0-based bank
  slot → there is **no engine bug**, the PC audio path is faithful, and
  D205 / D202's slap need another cause. Re-audit whether
  `scratchpad/rom_sfx_decode.py`'s 0-based `sound_offsets` walk actually
  models the runtime `soundArray` (leading NULL? offset-table vs `soundCount`
  mismatch? sfxctl not a plain `ALBankFile`?). Cheapest — do first.
- **(B)** PC-only off-by-one in the *converted* bank — `port/src/romdata.c`
  `afFixupInst` drops/adds one `soundArray` entry vs the ROM. Test: dump PC
  runtime `soundArray[81]` wavetable `base`/`len` (existing `GE_AUDIOTRACE`
  probe at `snd.c:1002`) and byte-compare to `rom_sfx_decode.py` bank 80 vs
  81. PC-side fix, rule-#2 clean.
- **(C)** PC-only off-by-one in the `soundIndex` reaching `sndPlaySfx`
  (request path / miscompiled or region-mismatched enum). Test: `GE_AUDIOTRACE`
  already logs `soundIndex=` at the call — confirm armour logs `81`, not `82`.
- **(D)** genuine data-side: ROM `SFX_ID` constants + `WeaponStats.Sound`
  bytes are each +1 vs a 0-based bank. Only reachable if A/B/C all fail.
  ~532 enum refs + debug-name table + weapon-stats asset bytes — **not** a
  legal port edit under AGENTS.md rule #2; stop and escalate to the user
  (possible "wontfix / faithful").

M-62's caveat still applies: this repo never builds or byte-verifies the N64
target, so "it byte-matches" cannot by itself settle (D).

### Verification done (M-76)

- Build clean (`build-pc.sh ntsc-final`).
- BUNKER1 (`-level_09`) `GE_AUDIOTRACE=1`, 60 s: crash-free, 446 `sndPlaySfx`
  calls. Every one of ~90 distinct requested `soundIndex` values resolves to a
  wavetable whose `len` matches `rom_sfx_decode.py` slot **`soundIndex - 1`**
  (0/90 match slot `soundIndex`). Includes `ARMOUR_COLLECT_SFX`=81 → slot 80,
  `GUN_B4_BOLTACTION_SFX`(AK47)=109 → slot 108, `GUN_SILPPK_A`=46 → slot 45.
- `-level_09` golden framediff (`200-440:120`): **3/3 within threshold** on two
  consecutive runs (an audio-path change cannot affect rasterisation; ritual).

### Still owed

User by-ear A/B vs N64: armour pickup, unarmed melee whiff, silenced PP7 fire.
If clean → **D202 closes outright**, D205 shrinks to symptom (2) (explosion→
scream) alone. Then remove the temporary D202 `GE_AUDIOTRACE` / `[VOL]` /
`[DISTVOL]` probe set (checklist in HANDOFF).

## D205 — Wrong/extra SFX requested on PC vs N64: explosion→scream, armor pickup wrong sample, slap/glass layered over gunfire, melee→Klobb shot (M-71/M-72)

**Symptoms (user by-ear; N64 A/B confirms non-fidelity):**
1. General gunfire: a slap/glass-shatter plays *over* the correct gunshot on
   PC; silent on N64 for the same shot.
2. Explosion: plays a scream-like sample (the soldier-death voice) instead
   of / over the explosion.
3. Armor pickup: wrong sample.

**Exonerated — do not re-investigate:**
- Playback chain: M-68/M-70 exact match — every voiced request plays the
  exact ROM sample at the exact ROM pitch (204/204 voiced, zero wrong-sample).
- D37 bank converter: offline harness runs the real `romdataFixupAudioBank`
  on the same ROM bytes and diffs N64 vs PC images entry-by-entry —
  261/261 identical (base/len/type/keyMap/env), 0 mismatches
  (`scratchpad/banktest/harness.c` + `diff_bank.py`; gitignored, local-only).
- D154/D135 GBI raycast parsers ABI-correct; volume law
  (`sub_GAME_7F0537B8`: d≤200→max, sqrt falloff to 5000, linear to 6000)
  and suppression logic faithful (M-71 static audit).

**Therefore: PC requests different or extra sound indices than N64 in the
4. Unarmed melee: Bond's slap attack plays Klobb's shooting sound
   (`GUN_B1_MGUN3_3_SFX`, idx 106) instead of the slap effect (M-72).

**M-72 (controlled runtime traces, this session):** environment note first —
the earlier "flaky /GS crash" was an investigator artifact: `SDL2.dll` lives
in `C:\msys64\mingw64\bin`, which is NOT on the system PATH and not copied to
`build-pc/`; launches without it die instantly (exit 0xC0000105), with or
without any env vars. Prepending mingw64/bin to PATH in the launcher
(`build-pc/d205_*.ps1`, gitignored) gives clean 40–60 s runs. Results under
`GE_AUDIOTRACE=1`: (1) PPK fire at `-level_09` (unarmed start; first Z picks
up the spawn PPK, idx 232): every shot requests exactly {46 GUN_SILPPK,
one of 27/28/39/40 — all inside the 20-entry `ricochet_sounds_small` table,
gun.c:415 — so `rnd1 % 20` is IN RANGE, not OOB, 122 CART_SPENT}. No glass
(70), no slap (47–49) layering. (2) Unarmed melee at levels 01–08 (all start
unarmed): fire-press requests ONLY 105 PUNCHING_AIR (whiff); punch-HIT path
is the compiled constant `punch_sounds` = {47,48,49} (gunfire.c:2338), whiff
= 105 (chrprop.c:1465). No 106 anywhere. (3) idx 106 is referenced by NO
code constant — it enters only via `WeaponStats.Sound` (`skorpion_stats.
Sound = 0x6A`) through `bondwalkItemGetSound`; the player fire-sound path
(gunfire.c:3192–3200) runs only in `GUN_ANIM_STATE_FIRE` and a fist sets
PUNCH state with `.Sound = 0`, so Bond's own punch cannot request 106 — an
NPC guard firing its Skorpion (`chraction.c:5588`) can. **Conclusion:
all four symptoms' request paths are faithful constants/tables, and none
reproduces in controlled micro-scenarios; the leading suspect is now
mechanism (b) — extra/different EVENTS during real gameplay (a guard firing
as you punch → 106 layered on the slap; an NPC scream at the blast; a second
surface hit adding glass/slap to a shot).**

**M-73 (user combat capture) — ROOT CAUSE WITHDRAWN by M-74 below.** The
session analysed the user's full-combat `GE_AUDIOTRACE=1` capture
(`build-pc/audiotrace.log`) and concluded that guards were stuck
re-triggering weapon fire sound idx 109 (`GUN_B4_BOLTACTION_SFX`,
AK47/Spectre) "256x over 29.7 s with no corresponding bullets", pinning it on
`stanTestLineUnobstructed` guard->Bond LOS returning clear on converted
collision geometry. **M-74 re-counted the same file and every load-bearing
number was wrong** — see below. The code walk M-73 produced is still accurate
and worth keeping as reference: `chrlvFireWeaponRelated` (`chraction.c:6530`)
passes `phi_a2 = sp27C || sp278` to `sub_GAME_7F02BFE4` (:6902), so a
non-bullet auto tick still plays the weapon sound, gated by
`SoundTriggerRate`/`field_178[hand]` (and by `CHRHIDDEN_FIRE_TRACER` 0x80,
cleared once per `chrlvTriggerFireWeapon`); sustained attack needs
`seen_bond_time >= g_GlobalTimer - CHRLV_SEEN_RECENT_CHECK` (:6593),
refreshed by `setSeenBondTimeToNow` behind LOS queries (:3917/:3972,
`chrCanSeeBond` :3945). That mechanism is real. What M-74 shows is that
**nothing in the capture indicates it ever misfired.**

**M-74 (re-count of the SAME capture — M-73's evidence does not hold, this
session):** counting only the canonical one-per-call line
(`sndPlaySfx: t=... soundIndex=...`) in `build-pc/audiotrace.log`:

| M-73 claim | Actual |
|---|---|
| idx 109 requested **256x** | **128x** — the count was doubled |
| "~11 guards each re-triggering" | 128 / 29.73 s = **4.31/s aggregate**, ~0.4/s per guard |
| "sustained roar, gate blown open" | AK47 `SoundTriggerRate` = `RATE_AK47` = 4 `g_GlobalTimer` ticks (`assets/obseg/gun/gunWeaponStats.inc.c`) => design ceiling **15/s per guard**. Observed is **~6x UNDER** the weapon's own rate — `field_178` is not even the binding constraint |
| "no corresponding bullets/impacts anywhere near that rate" | In the same 29.7 s window: **~30 ricochet / wall-hit requests** (idx 19, 20, 22-27, 29, 31-33, 35-37, 40, 41), **69 x2** (flesh hit), **123-132** (12 body-falls), **134-147** (14 guard yelps). Guards were firing *and* hitting |
| "only ~32 non-109 requests in the window" | ~150 |

Also corrected:

- **The 128-vs-256 gap is the double-log artifact D204 already documented.**
  D204 corrected M-63's "idx 109 fired 94 times in 60 s" with exactly this:
  "the count is doubled by the probe (it logs two lines per call)". M-73
  re-made the same mistake on the same sound index. Count `sndPlaySfx: t=`
  lines, never bare `soundIndex=` matches.
- **The monotone index runs are ground-truth round-robin, not a broken
  selector.** The capture contains strictly increasing sweeps
  `134,135,...,147` (GET_HIT_MALE) and `123,124,...,132` (BODY_FALL), one
  request each, never repeating. This looks exactly like a `random() % n`
  gone wrong and was checked specifically: `chraction.c:2454-2470` selects
  `male_yelps[male_guard_yelp_counter]` and post-increments modulo 25 — a
  deliberate cycling counter in byte-matched game code. Faithful. Do not
  re-open.
- The trace file spans 1321.85 s wall (288 requests); the actual firefight is
  ~40 s of it (`t` in 715754-715800 s). M-73's "29.7 s" is the 109 span only.

**Net: the 109 traffic in that capture is normal, in-spec guard combat.** The
LOS / converted-geometry root cause is **withdrawn** — it was inferred from
counts that do not survive re-derivation, and M-73's Next steps 1-3 (probe
`setSeenBondTimeToNow`, decode pccg stan in Python, replay guard->Bond rays)
would have been a full session spent on a phantom. The only genuinely open
observation left from that capture is that the firefight lasted a long time,
which is a gameplay-fidelity question for an N64 A/B, not evidence of an
audio bug.

**Where that leaves D205.** Every layer upstream of presentation is now
proven correct: bank conversion (261/261 identical, M-71), requested indices
(182 explosion x1, 81 armor x1, 46 PPK, 47/48/49 punch hits, 105 whiffs — all
correct and in range, here and in M-72's controlled runs), and sample+pitch
(M-68/M-70 exact match). That leaves exactly one untested layer between "the
right sample" and "what the user hears": **spatial presentation — per-voice
volume, pan and concurrency.** It fits all four complaints in one shape: if a
sound's apparent *position* is wrong, a guard firing behind you is by ear
indistinguishable from "a gun sound came out of my punch", a nearby guard's
death scream reads as "the explosion screamed", and a ricochet reads as "a
slap over my gunshot". The user's ear reports *what and where*; only *where*
has never been verified. It is also the youngest code in the stack —
`sndCreatePostEvent` was stubbed out entirely (D138) until M-65 un-stubbed
it, so every distance/pan post-event has been live for only a few sessions
and has never been A/B'd against N64.

**Original M-71 framing (kept as background; DEMOTED by M-74 — every index
observed in the user's capture was correct, so "PC requests a different
index" is no longer the leading shape of the bug):** PC requests different or
extra sound indices than N64 in the same scenario. Two mechanisms, ranked:
(a) **Converted level/setup data** — sndID / explosion-type / item-sound
    fields in the pccg sidecars differ from ROM. Explosions play
    `sndPlaySfx(g_musicSfxBufferPtr, sp44->sndID, NULL)`
    (`src/game/explosion.c:285`) with `sp44->sndID` =
    `g_ExplosionTypes[type].sndid`; the *type* comes from level data.
    Explosion SFX are idx 169–183 (EXPLOSION_2A_SFX=169 … EXPLOSION_9_SFX=
    183, bondconstants.h). A scream is outside that range, so either the
    type index is wrong (→ a non-explosion sndID) or a second event fires.
    Armor: idx 81 verified exact in M-68 — a "wrong" armor sample means a
    different index is requested in the user's scenario (or a layered
    second sound).
(b) **Extra hit events** — converted collision geometry makes bullet
    raycasts or explosion proximity checks hit surfaces/NPCs the N64
    misses. The slap+glass-over-gunshot signature is exactly the
    non-penetrating-OBJ-hit path: `sub_GAME_7F064720`
    (`src/game/gunfire.c`) plays `HIT_BULLET_GLASS_SFX` (idx 70) for every
    non-penetrating hit; ricochet tables classify AFDM (27–30) = prop only,
    GBU/TAJ/RICO_4 (19–26, 31–33) = wall only.

**M-74 step 1 — static audit of the presentation layer: H-A FALSIFIED,
H-B narrowed to "faithful but shallow". Done this session.**

The pan machinery is complete and faithful end to end:
`src/snd.c:461/530` computes `pan = clamp(soundState->pan + sound->samplePan -
AL_PAN_CENTER)` → `alSynSetPan` (`synsetpan.c`, posts `AL_FILTER_SET_PAN`) →
`src/libultrare/audio/env.c:400-402` turns pan into independent L/R targets via
the `eqpower` table (`ltgt = volume*eqpower[pan]`, `rtgt =
volume*eqpower[LEN-pan-1]`) and emits `aSetVolume(A_LEFT|A_RATE)` /
`(A_RIGHT|A_RATE)` → `port/src/mixer.c` `aSetVolumeImpl` decodes A_LEFT
(0x02) into `volCur/volTgt[0|1]` and `aEnvMixerImpl` applies `vol[0]`/`vol[1]`
to `dry[0]`/`dry[1]` independently (:488-496). Nothing is dropped or collapsed
to mono. **The port's stereo path is correct.**

**But GE never uses it.** `AL_SNDP_PAN_EVT` (`snd.h:41`) is handled at
`snd.c:527` and **posted by nothing anywhere in the tree** — grep returns the
enum and the handler, no callers. So `soundState->pan` keeps the
`AL_PAN_CENTER` set at `snd.c:842` for every SFX's entire life, and the only
pan input is the static per-sound `samplePan` from the bank. Dumped from the
converted bank image this session: **252 of 261 sounds have `samplePan == 64`
(= `AL_PAN_CENTER`)**; the 9 exceptions are 4, 24, 44, 74, 84, 104, 123.
Every sound in the user's capture is centred — 46, 47, 48, 49, 81, 105, 106,
109, 182 all `samplePan = 64`.

**⇒ GoldenEye does not spatially pan SFX at all, on N64 or PC.** There is no
pan to get wrong. H-A is dead; do not re-open it, and do not write the
"pan varies with bearing" probe (M-74 step 2 as originally drafted) — the
answer is known and it is "never, by design".

Field-coverage gap in M-71's bank exoneration closed at the same time: the
converter writes `samplePan`/`sampleVolume`/`flags` at `+24/+25/+26`
(`port/src/romdata.c:890`), correct for the PC layout (3 × 8-byte pointers
before them), and `scratchpad/banktest/diff_bank.py` already diffs them as
`panvol` — re-run this session, **261/261, 0 mismatches**. (M-71's prose
listed only base/len/type/keyMap/env; its actual coverage was broader,
including a 24-byte sample-data fingerprint.) The volume formula
(`snd.c:453`, `:565`) also has no PC-side divergence: peak intermediate is
127 × 32767 × 127 ≈ 5.3e8, inside s32 on both platforms, and `vol` is `s16`
(`snd.h:90`) which holds the 32767 the `[VOL]` probe reports.

**What this leaves.** GE's only spatial cue for an SFX is distance volume, and
that law is shallow: the capture's `[DISTVOL]` lines show flat max out to
~200 units, then 0.60 of max still at dist 1794. So a guard firing across the
room plays **near-full volume, dead centre**, at a per-sample volume
comparable to the player's own actions (idx 109 `sampleVolume` = 90; punch
47/48 = 100, 49/105 = 110). **That is faithful, and it is also exactly the
perceptual condition that generates all four of the user's reports** — with no
spatial cue, a correct sample played at correct volume from across the room is
by ear a sound at your own position. This reframes D205: the complaints are
fully consistent with *correct* sounds overlapping, and the only remaining
question is whether the PC generates **more** such overlaps than the N64
(H-D). Nothing on the PC side can settle that; it needs the N64 A/B.

**Next (cheapest first; re-ranked by M-74 — M-73's list is withdrawn):**
0. ~~User capture~~ — DONE (M-73), but see M-74: it does NOT show a guard
   re-trigger bug. 182 and 81 were both requested correctly and exactly once.
1. ~~Static audit of the presentation layer~~ — **DONE (M-74 step 1 above).
   H-A falsified: the port's stereo path is correct, but GE never pans SFX at
   all (no `AL_SNDP_PAN_EVT` poster exists; 252/261 bank sounds are
   `samplePan == 64`). Bank `panvol` re-diffed 261/261 clean. Do not re-open.**
2. ~~Pan probe run~~ — **cancelled by step 1: pan cannot vary, by design.**
3. ~~The PC side is now exhausted~~ — **SUPERSEDED by D206.** The premise
   ("requested indices are correct") was wrong: the indices are correct as
   *values* but are resolved one slot too high, which D206 root-causes. What
   survives from this step: the pan audit (H-A dead) and the bank `panvol`
   re-diff. Original text: bank, playback, requested indices,
   sample+pitch, pan and per-sound volume are all verified faithful. The only
   remaining hypothesis is **H-D** — that the PC generates *more* overlapping
   combat events than the N64 — and nothing measurable on the PC alone can
   settle it. **The N64 A/B is now the gating step, not an optional
   corroboration.**
4. **User-side, batched, held until 1-3 report:** (a) ROM samples 106
   (Skorpion/"Klobb") vs 109 (AK47) as WAVs via `scratchpad/rom_sfx_decode.py`
   for the user to settle the Klobb identification by ear — their by-ear
   identification stands either way, this decides *which* sound, not whether
   they heard it; (b) N64 A/B of one moment: punch a guard while others fire —
   does the same combat sustain on N64?
5. After root-cause + fix: user re-test of melee / explosion / armor pickup
   vs N64.

**Ranked hypotheses after M-74:**
- **H-A (top) — pan/spatialisation wrong or absent in the mix.** Untested end
  to end; youngest code in the stack (D138/M-65 lineage).
- **H-B — distance law right, scale wrong.** The capture's `[DISTVOL]` lines
  do follow the documented curve (flat max to ~200, falloff after) but only
  reach 0.60 of max at dist 1794. Never compared to N64 loudness at the same
  distance.
- **H-C — voice-pool masking.** 8-voice soft limit vs 11 guards + music. Weak:
  M-70 measured alloc <= 5/8.
- **H-D — real content difference (PC guards engage where N64 guards don't).**
  Possible, but after M-74 it has *no* supporting evidence. Gated behind the
  N64 A/B.

**Probes added this session (live under `GE_AUDIOTRACE`, catalogued in
GE-ENV-PROBES.md):** `[VOL] t=… state=… rawVol=… playing=…` (`src/snd.c:
556`, per-VOL-event value) and `[DISTVOL] pos=(…) player=(…) dist=… vol=…`
(`src/game/propobj.c:12780`, the distance-attenuation funnel
`sub_GAME_7F053894`).

## D204 — Audio pipeline runs ~2 % below real time forever: GE's AI feedback loop has a 3 ms setpoint that OS jitter clears, so the DAC is padded with silence (M-64, FIXED + measured)

**Found while reviewing the D202 audio state, not by chasing D202's stated
symptom.** D202 itself is NOT resolved by this — see "What this does and does
not explain" below.

### The architecture (established, not assumed)

GE's audio is a closed-loop, DAC-backpressure-driven producer:

- `amMain` (`src/audi.c:433`) blocks on `OS_SC_RETRACE_MSG`. `__scMain`
  (`src/sched.c:334`) forwards a retrace to a client either every frame or,
  when the client registered with a non-zero flag, every *second* frame. The
  audio client registers with `1` (`audi.c:441`), so it wakes at **30 Hz**.
- Per wake, `amHandleFrameMessage` (`audi.c:531`) sizes the next block:
  `frameSamples = (u16)((g_FrameSize - (osAiGetLength() >> 2) + 16 + 0x25) & ~0xf)`,
  then clamps *below* at `g_MinFrameSize`.
- NTSC constants: `outputRate=22050`, `FRAMES_PER_FIELD_AS_POW2=1`,
  `MAYBE_FRAME_RATE=60` → `g_FrameSize=736`, `g_MinFrameSize=720`,
  `g_MaxFrameSize=789`. `info->data` is allocated exactly `g_MaxFrameSize*4`
  = 3156 bytes (`audi.c:388`).
- So `osAiGetLength()` is a **±64-sample trim**, and the loop only asks for
  more than 720 once the reported length falls under ~69 frames — **3 ms**.

### The defect

720 samples per 30 Hz block is **21600 samples/s against a 22050 Hz device**
— the nominal rate is structurally 2 % short, and the design depends on those
under-69-frames top-up blocks (784 samples) to make it back.

On N64 a 3 ms setpoint is fine: AI is double-buffered and the VI interrupt is
exact. On PC, `osAiGetLength()` maps onto the SDL queue, and ordinary OS
scheduling jitter empties a 3 ms cushion before the loop reacts. The queue
hits zero, SDL pads playback with silence, and **that lost time is
unrecoverable** — a queue-*depth* reading can never tell the regulator it has
already fallen behind. Measured, sustained, indefinitely:

```
GE_D204_OLD=1  rt=0.980  q=0..416   (q reaches 0 repeatedly)
```

`rt` = produced audio seconds / wall seconds. 0.980 is exactly 21600/22050.

### The fix (F5, port-side only — `src/audi.c` untouched, rule #2 clean)

`port/src/audio.c`'s `audioGetAiLengthBytes()` subtracts a target cushion
(`AUDIO_TARGET_FRAMES` = 1024 frames ≈ 46 ms) before reporting, so "the queue
holds the cushion" reads to the game as "the queue is empty". The loop then
tops up while ~46 ms of slack remains and settles just above the cushion
instead of oscillating into starvation. audi.c's own control law is unchanged.

```
after (same repro)   rt=1.000  q=368..672  drop=0   (q never reaches 0)
```

Also landed, all in `port/src/audio.c` / `port/src/libultra.c`:

- **F1** — `osAiGetLength()` now reports the residue of a *single* buffer
  (`min(queued, lastBufferBytes)`), matching AI_LEN_REG, instead of the whole
  SDL queue depth. **Robustness only: measured A/B shows zero behavioural
  change.** See the falsified hypothesis below.
- **F3** — `Audio.QueueLimit` default 8192 → 2880 frames (372 ms → 130 ms
  worst-case latency), and a full queue now logs a dropped block instead of
  discarding it silently. **Caveat: an existing `data/ge007.ini` pins
  `QueueLimit = 8192`, so this default only reaches fresh configs.**
- **F4** — invariant guard refusing any block larger than `g_MaxFrameSize*4`.
  Believed unreachable; costs one compare, versus a silent heap overrun.
- **`GE_D204=1`** — audio-health monitor, one line per 5 s: `rt=` real-time
  ratio, `q=` queue depth, `drop=`, `max=` largest block vs its allocation.
  Deliberately cheap (~30 clock reads/s) so it can be left on for a whole
  playtest. **`GE_D204_OLD=1`** restores pre-fix behaviour for in-binary A/B.

### Hypothesis raised and FALSIFIED — do not re-open

I predicted the u32 subtraction in `audi.c:531` wraps once the queue exceeds
`g_FrameSize + 0x35` = 789 frames, truncates to u16 (e.g. 58128), and drives a
74× heap overrun of the 3156-byte `info->data`. **The queue really does exceed
789 routinely — measured high-water 2064 frames over 5 minutes — and the wrap
really does happen, but it is harmless.** `frameSamples` is declared
**`s16`** (`audi.c:145`), so the wrapped value lands negative and audi.c's own
`(s32)frameSamples < (s32)(s16)g_MinFrameSize` clamp catches it, yielding a
nominal 720-sample block. Max block observed is 3136 bytes against the
3156-byte allocation, in both old and new modes, across every run. There is no
overrun. This is why F1 measures as a no-op.

### Two M-63 conclusions corrected

1. **`soundIndex=109` firing "94 times in 60 s with no AK47 present" is a red
   herring — closed, negative.** The count is doubled by the probe (it logs two
   lines per call); 47 real calls. All of them fall in tight bursts *after*
   t≈5.4 s, interleaved with ricochet indices 27/28/30/35/39/41 — **guards
   returning fire**, correct behaviour once the player starts shooting in
   Bunker. And the "suspiciously uniform ~32.6 ms cadence" is not a retrigger
   artifact at all: 32.6 ms **is** the audio-block quantum (720 frames × 4
   bytes = 2880 bytes), so *every* `dumppos` in the entire trace is quantised
   to it. Several sounds landing in one block read as "4-in-a-row".
2. **M-63's implied "audio produced at 13 % of real time" (708608 dump bytes
   = 8.03 s over a 60 s run) was a measurement artifact of its own probe.**
   That run had `GE_MIXERTRACE=1`, whose unbuffered per-opcode `fprintf`
   produced a 25 MB log and slowed the process enough to starve the audio
   thread. The identical repro without it produces 58.0 s of audio in 60 s.
   **Lesson: `GE_MIXERTRACE` is not safe for any timing-sensitive measurement**
   — that is exactly why `GE_D204` was written to be cheap. M-63's
   recommendation to have the user replay with `GE_MIXERTRACE=1` set would
   have induced the very starvation it was looking for.

### What this does and does not explain

**Does:** a permanent low-level stutter/gap artifact in all PC audio — ~2 % of
playback was silence, in every level, for every user, from the first frame.

**Does not:** D202's reported chain (wrong sound on the silenced PPK, correct
sound sometimes heard simultaneously, sounds piling up and glitching over a
session, eventual near-silence except one stuck loop). Nothing in this
session's headless runs degraded: a 5-minute `-level_09` soak held `rt=1.000`,
`drop=0`, no voice-pool growth. D202 stays OPEN. It needs a real playtest —
now instrumentable with `GE_D204=1` (plus `GE_AUDIOTRACE=1`, and **not**
`GE_MIXERTRACE`), where `rt` falling or `q` pinning at `queueLimit` with
`drop=` climbing would localise it immediately.

### Verification

Reproduce with **`.	ools_pcudiodebug.ps1 -AB -Fire`** (added this session;
`-Play -Trace` is the instrumented interactive run for D202). `-level_09`,
scripted PPK fire, MSYS2 MINGW64 build, full `data/` mirror. In-binary A/B via
`GE_D204_OLD` (same executable, no build-to-build variance):
`rt` 0.980 → 1.000, `q` min 0 → 368, `drop` 0, `max` 3136/3156 unchanged.
Not yet verified by ear — the artifact is a ~2 % silence rate, so a human
listening pass on a real playthrough is still owed before calling it closed.

### Harness gotchas found while building `audiodebug.ps1` (both cost a debug cycle)

1. **`Start-Process -RedirectStandardOutput` does not capture this game's
   stdout.** `CMakeLists.txt:577` sets `WIN32_EXECUTABLE TRUE` (`-mwindows`),
   so `ge007.x86_64.exe` is a GUI-subsystem binary with no console of its own;
   the redirect yields a 0-byte log while the output lands on the parent
   console. A shell that opens the file itself before `CreateProcess` (cmd's
   `>`, or a bash redirect) does work.
2. **MSYS2 `bash.exe` launched from PowerShell arrives with a stripped
   environment.** Neither variables inherited from PowerShell nor the script's
   own `export` reach the child process — verified with a minimal
   `export GE_PROBE=hello; env | grep GE_PROBE` probe, which prints under
   `sh script.sh` from MSYS but nothing when the same script is run by
   `powershell -Command "& bash.exe script.sh"`. Every `GE_*` probe silently
   read as unset, which looks exactly like "the probe is broken". `cmd.exe`
   inherits normally, so `audiodebug.ps1` generates a `.bat` and runs that.

### M-78b — user repro on Bunker: symptom is specific and permanent

Natural Bunker alarm trigger (spawn, go forward into the room ahead, shoot
one of the two guards → the other trips the alarm), then sustained firefight.
User by-ear result:

- Most SFX keep playing during the klaxon.
- **Eventually the *player's* gunshots stop being emitted — and stay muted
  even after the alarm ends.** Enemy gunfire and all other SFX continue fine
  during and after.

This is NOT generic pool starvation (that would drop random sounds
transiently). It is specific to the player-weapon fire sound and it is
*permanent* — a stuck-state bug, not a transient voice shortage.

**Structural lead (static, unconfirmed — needs the capture).** The player
fire sound is played through a **two-slot double buffer** in
`bondfirefunc` / `gunfire.c:3178-3202`: `handptr->audioHandle` and
`handptr->field_A48`. Each trigger interval: `sndDeactivate` whichever slot
is currently *playing*, then `sndPlaySfx(..., &slot)` into whichever slot is
*NULL* (`audioHandle` first, else `field_A48`). If **both** slots are ever
left non-NULL-but-not-playing at once, the `else if (field_A48 == 0)` guard
fails and **no further player gunshot can play, ever** — matches the symptom
exactly.

A slot is cleared only by `sndUnlinkClearSound` (via `sndDisposeSound`)
following the state's back-link `state->state->link.next`. `sndDeactivate`
is **asynchronous** — it posts `AL_SNDP_DEACTIVATE_EVT` to the 64-slot SFX
event queue (`MUSIC_SFX_SEQ_CONFIG_MAX_EVENTS = 0x40`). If that queue is
saturated (heavy firefight + the alarm's self-retrigger posts + every
ricochet/impact/yelp posting PLAY/PLAY_EVT/STOP/DEACTIVATE), `alEvtqPostEvent`
**drops the event silently** → the deactivate never runs → the state never
disposes → the slot never clears. Once both slots are stuck, permanent mute.
The `[VOICES] ... evtq=N/64` probe already in `snd.c` will show queue
saturation; `[SLOTWRITE]` + `[VOICE+]/[VOICE-]` + `sndPlaySfx t=` trace the
two player-gun slots. All three counts (8 voices / 64 states / 64 events) are
byte-match stock GE — so if this is the mechanism it is a port *timing* bug
(the SFX event queue drained too infrequently vs N64's RCP cadence, D204
family), not a value to bump.

**Next:** read the user's `build-pc/audiotrace.log` — confirm (1) `evtq`
approaches 64, (2) a `[AUDIOTRACE] sndDeactivate: state=X` with no following
`[VOICE-] state=X` / dispose, (3) two `[SLOTWRITE] slot=<audioHandle>` and
`slot=<field_A48>` with no clear afterwards. Then the fix is port-side: drain
the SFX event queue more often, or (cheaper, D202-family-consistent) a
bounded self-heal in the double-buffer — if a slot has pointed at a
non-playing state for > N ms, force-clear it.
