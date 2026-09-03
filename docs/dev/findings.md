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
| D152+ | D152 addendum (M-31): static audit of every compiled-audio `osSetIntMask(OS_IM_NONE)` — **all balanced**, no unbalanced early-return. Real fixes: `sndSetSfxSlotVolume` now holds the mask across its list walk (matches its twin `sndDeactivateAllSfxByFlag`) + `sndApplyVolumeAllSfxSlot` batches the slot loop under one recursive hold (kills the fade-out lock-acquire storm); `portThreadWrapper` calls `imThreadExitRelease()` on thread exit (kills the "transient thread acquired `OS_IM_NONE` and died" leak + the pthread-id-reuse re-wedge). Steal-lock kept as backstop. | FIXED (`src/snd.c`, `port/src/libultra.c`, `#ifdef PORT`) — fade-out repro playtest-gated, not headless-verified |
| RC3 · D167 | **Non-power-of-two texture wrap period (`docs/dev/TEXTURE-GLITCH-ANALYSIS.md` §6 RC3 — "textures repeat oddly", residual Depot-ceiling noise after D161).** The N64 RDP masks the texel coordinate of a wrapping render tile at `1<<mask`, and GE sets `mask = texDimensionToMask(dim) = ceil(log2(dim))` (`src/game/tex.c:361`), so a non-PoT tile (Depot's 65×65 / 96×48 / 56×56 room surfaces) repeats at the **next power of two**, not at its image size the way GL `GL_REPEAT` does → the pattern is squashed/stretched and the seam lands in the wrong place. fast3d never stored `masks`/`maskt` at all (`gfx_dp_set_tile` dropped them) and wrapped purely at the uploaded image dimension. **Fix (`port/fast3d/gfx_pc.cpp`, behind the existing `Video.WrapFix` knob, default OFF):** store `masks`/`maskt` on the tile; in the hoisted per-texunit pre-wrap block in `gfx_sp_tri1` (D74 block — already lifted out of the vertex loop, indexed by texunit `t` not vertex `i`), when the tile is WRAP (no CLAMP/MIRROR bit) and `1<<mask != tex_width`, fold the UV at the N64 period `1<<mask` and clamp the `[dim, 1<<mask)` overflow band (which is a TMEM smear on console, no real texels) to the last texel so it reads as an edge streak instead of a bogus early image restart. `GE_WRAPFIX=0/1` env override added (env wins over the ini, matching `Debug.FrameDump`). **Per-level captures (`-level_09`/`-30`/`-34`/`-20`, WrapFix OFF vs ON, `GE_PCDUMP` 6-frame windows):** no crashes, 6/6 frames each; on settled/comparable frames Silo is ~pixel-identical (phash 0–11), Facility 180–260 pixel-identical, Depot shows small localized texel changes on ceiling/wall cells (dmean 5–9, no structural break); the large per-run deltas are all the D117 intro-camera-pan nondeterminism, not the fix. Default kept **OFF** — no regression, but a headless structural diff can't confirm the Depot ceiling actually looks *better*; needs a human eyeball with `Video.WrapFix=1`. Default-off is byte-identical to golden (all new behaviour is inside `if (g_wrap_fix)`). D74's dead in-vertex-loop wrap block was already reworked/hoisted at M-30; this only adds the mask-period trigger. Confidence: **medium** (mechanism correct; overflow-band handling is an approximation, not exact TMEM-smear emulation; visual win unconfirmed). porting-notes.md §D. | KNOB ADDED, default OFF (`port/fast3d/gfx_pc.cpp`). Needs user visual check on Depot. |
| D168 | **`GE_PCDUMP` / F12 PPM captures were vertically flipped — the entire source of the bogus D114/D116 "HUD/text X-mirror" (M-33, developer-confirmed).** `gfx_opengl_dump_bound_fbo` (`port/fast3d/gfx_opengl.cpp`) wrote `glReadPixels` output straight to a P6 PPM. GL framebuffer origin is bottom-left; PPM P6 is top-row-first — so every capture (and the `tools_pc/golden/` set, and every screenshot pasted into the finding log since M-6) was upside-down. On real hardware / an actual screen the game renders correctly (developer confirmed). The successive D114→D116 "shared fast3d mirror" investigations — each of which found *every probed stage clean yet the output "mirrored"* — were reading an inverted capture and pattern-matching upside-down asymmetric content (text, guards, the Nintendo logo) as "mirrored". Fix: emit PPM rows bottom-to-top. `tools_pc/golden/*.png` flipped in place to match (see `tools_pc/golden/README.md`); regenerate from a real run when convenient. **Not runtime-verified this session** (no ROM / toolchain in the migrated tree — see the migration note) — needs a fresh capture to confirm text reads normally. Confidence: **high** (mechanism is unambiguous; developer has hardware confirmation the screen is correct). porting-notes.md §D2. | FIXED (`port/fast3d/gfx_opengl.cpp`); D114 / D116 reclassified as capture-orientation artifacts (below). |
| D169 | **Front-end mouse pointer can't reach the outer cells of the mission/level-select grid — only an inner ~3×3 selectable (M-33, developer bug report).** The D165 pointer P-controller in `port/src/input.c` is hard-coded to a **320×240** virtual field (`MENU_CURSOR_HI_H`/`_HI_V` = 300/220, `MENU_CURSOR_MID_H`/`_MID_V` = 160/120, `input.c:145-149`), but GE's front end runs at **440×330** (`front.c:8570` `viSetViewSize(440,330)`; default cursor home 220/165 `front.c:285`). `frontUpdateControlStickPosition` clamps the real cursor to `[20,420]×[20,310]` (`front.c:1195-1217`); the mission-select hit-test grid spans x 73…352 / y 62…270 with outer split points 317 / 235.5 (`front.c:516,519,3180,3194`). The port clamps its pointer *target* and *estimate* to 300/220 (`input.c:553-564`) → the stick zeroes out once both saturate → the game's `cursor_h_pos`/`cursor_v_pos` park at ≈300/220, short of the two outer columns and the bottom row. File-select / mode-select / main-menu are unaffected because their hit targets (centred folder boxes, the `x=126` mode list, the `x=106` difficulty list) all sit inside the 320×240 sub-box. Not the D118d `joyGetStickY`-threshold class — this is a virtual-resolution constant mismatch, closer to D164 (a front-end layout constant wrong on the PC path). Also note: the "re-syncs whenever the target is held at a screen edge" comment at `input.c:141-143` describes behaviour the code doesn't implement — the only estimator reset is the activation re-home (`input.c:545-549`), so once pinned at the clamp the estimate never recovers. Confidence: **medium-high** (constants + clamp math unambiguous in source; static-only, no build/run this session; exact reachable block "~4×3" vs the reported "3×3" within tolerance). | **FIXED (M-33, `port/src/input.c`, port-only, no `#ifdef PORT`).** The `menuMode` pointer branch now derives its clamp bounds from the live virtual screen — `[screenleft+20, screenleft+screenwidth-20] × [screentop+20, screentop+screenheight-20]` via `getPlayer_c_screenwidth/height/left/top()` (`src/game/bondview.c:880-895`) — falling back to the old 320×240 constants when the front-end screen isn't set (`sw` outside 200…2000). The estimator seed on activation is now the real `cursor_h_pos`/`cursor_v_pos` (`front.c:285`, externed in `front.h`) instead of the 160/120 centre guess. `MENU_CURSOR_HI_H/_V` / `MID_H/_V` kept only as the fallback. **Verified:** builds + links clean (`getPlayer_c_screen*` and `cursor_[hv]_pos` all resolve — non-static engine symbols, as expected); `GE_STARTMENU=7` mission-select boots crash-free 600+ frames; `-level_09` unregressed (framediff 3/3 within threshold, 91.6% nonclear). The in-level path is untouched (menu-only branch). **Interactive feel-check still owed** — headless input can't drive the mouse pointer, so "every grid tile is now reachable" is inferred from the corrected clamp math, not observed. `Input.MenuPointerMode=0` (legacy velocity) and the constant fallback remain as escape hatches. |
| D178 | **Pre-mission briefing screen: objectives blank / missing, briefing pages blank (also the D143 "briefing text blank" side effect) — FIXED (M-36).** Root cause: the briefing segment (`Ubrief*Z`, source `assets/obseg/brief/*.c`) is a raw ROM image of `struct BriefStruct` = `{ u16 brief[4]; struct { u16 textid; u16 enabled_difficulty; } objective[10]; }` (48 bytes), loaded by `front.c load_briefing_text_for_stage()` via `_fileNameLoadToAddr()` — **no converter and no BE→LE fixup anywhere in the chain**. On the LE host all 24 `u16` read byte-swapped. Measured on Dam (`GE_D178=1`): raw `brief=002c,012c,022c,032c obj0=042c/0100 obj3=072c/0000` vs correct `2c00,2c01,2c02,2c03 / 2c04/0001 / 2c07/0000`. Two independent symptoms follow: (a) `textid` `0x2C04` (= `getStringID(LDAM,4)`, bank 11 slot 4) reads as `0x042C` → bank 1 slot 44, never loaded → `langGet()` NULL → **blank text** (this is exactly the D143 residual, `front.c:6732` and `brief[0..3]` at `front.c:6855-6867`); (b) `enabled_difficulty` `0x0001` (Secret Agent) reads as `0x0100 = 256`, so `selected_difficulty >= enabled_difficulty` (`front.c:6729`) is false for every difficulty-gated objective — on Agent only the one `DIFFICULTY_AGENT`(0) objective survived the filter, which is why a single bare "a." bullet printed. **Fix:** `romdataFixupBriefing()` in `port/src/romdata.c` (+ `port/include/romdata.h`), called from a `#ifdef PORT` block in `load_briefing_text_for_stage()` right after the load — the same shape as `langFixupLoadedBank()` in `language.c` (BE-serialized-struct reconciliation, semantics-preserving, no game logic touched). `GE_D178=1` prints the raw and fixed words. **Verified:** Dam briefing on Agent now shows "a. Bungee jump from platform"; on `GE_STARTMENU_DIFF=3` (00 Agent) all four (Neutralize all alarms / Install covert modem / Intercept data backup / Bungee jump from platform), correct difficulty gating. `-level_09` framediff 3/3; `GE_STARTMENU=7`/`=13` crash-free. | **FIXED (M-36)** — high confidence |
| D175 | **In-game stutter / brief hang during normal play** (user QA report): opening a door on Runway (`-level_35`, mission 3) and also observed on Surface. Self-recovered; no backtrace captured. Likely one of the known transient-hang classes (D155 catch-up spiral / D156 anim NaN loop / D134 task-done event / D147-D152 audio-lock steal) or a benign new-room texture-import frame spike on door open. See `GRAPHICS-BACKLOG.md` D175 for the gdb pattern-match sheet. | Observed, not investigated |
| D176 | **Surface exterior renders wrong (`-level_36`), two independent defects.** **(a)** sky solid black — env data is correct (`Clouds=1`, warm `CloudRGB`), the cloud-sky path runs, but `skyRenderTri`/`skyRenderFull` emit only `G_RDPHALF_*` immediates which `gfx_pc.cpp:2901` deliberately no-ops → **root-caused M-37** (see "D176(a) — M-37 UPDATE"); fix = decode the RDPHALF sky-tri stream in fast3d. **(b)** cliff/rock walls = grey diagonal static — NOT a texture decode bug (D183 disproved the shear hypothesis, 0/166 loads strided); re-scope from ROM ground truth of the wall texnum (M-37: inconclusive, leaning tiling-density). See `GRAPHICS-BACKLOG.md` D176. | (a) root-caused, fix owed / (b) open |
| D177 | **Ladders non-functional — progression blocker (user QA report) — FIXED (M-36).** Not the input path and not the ladder state machine: `MoveBond` gates the ladder-collision path on `stanGetLocusCount(&curLocus)`, which was pinned at 0 on PC. `stanCheckLinkedSpecialTile` writes the LADDER signal via raw `outFlags[1] = 1` into a `struct StandTileLocusCallbackRecord` whose first member `s32 *rooms` is pointer-widened on PC → `[1]` is the high half of `rooms`, and `count` (moved +4→+8) is never written. Compounded by `curLocus` being declared as the 8-byte placeholder `move_bond_temp_struct` (too small for the widened record) and a `(s32)coords` pointer-truncation AV waiting in `stanGetMoveBondCollisionTiles`. **Fix:** `#ifdef PORT` — write record fields by name, declare `curLocus` as the real struct, `PORT_PTRADD` for the truncating cast (`src/game/stan.c`, `src/game/bondview2.c`; no game logic). Full detail: §F "D177". | **FIXED (M-36)** — high confidence; interactive climb test owed |
| D174 | **"No blood effect" (user QA report).** Likely the unverified D120 decal fix: `d43_emit.py` now emits the opcode-0x18 `PointUsage[]` chain, but it was never interactively verified and requires a full sidecar regen to take effect (`debug.ps1` does not regen). Spray path is D172 (draws, wrong colour) — total absence would be new. See `GRAPHICS-BACKLOG.md` D174. | Observed; first step = BUNKER1 firefight with regenerated sidecars |
| D183 | **M-36 Family A ("texture line/pitch shear") is DISPROVEN for the Surface repro (`-level_36`).** The importers' pitch assumption is real but never fires there; the Surface cliff "grey static" is not a shear. Full evidence + what the walls actually are: §F "D183" below. A defensive, provably-no-op de-stride landed in `import_texture()` anyway (covers the `gfx_dp_load_tile` case the `SUPPORT_CHECK`s assert against), plus `GE_DTEX` STRIDED marker + `GE_TEXRAW` raw-source dump. | Hypothesis DISPROVEN + diagnostics shipped; D176(b)/D182(2) still OPEN |
| D173 | **Third-person Bond model spawns too high** (user QA report): the player-representing figure at level start floats well above the ground "a lot of the time" while the actual player spawn is correct; same on the Cuba end credits — JB ~6 ft in the air above Natalya. D75 animated-model family but a *position* defect, not absence (M-33: gun-barrel Bond renders fine). Suspects: intro/credits puppet chr spawn position vs model base transform; anim root-motion accumulation (`modelSetAnimFrame2WithChrStuff`, cf. D156); packed-float `coord3d` decode in intro setup. "Player spawning OK" localises it to the render-side model, not `g_CurrentPlayer`. See `GRAPHICS-BACKLOG.md` D173. | Observed, not investigated |
| D179 | **Packaged build crashes after the logos — the D43/D69 model + bg sidecars are ROM-derived and absent from any build that has no ROM (M-34, alpha-release QA).** The first `goldeneye-pc-port-*-win64.zip` from CI (bundle-win.sh) shows the Rare/Nintendo logos (compiled-in `assets/rarewarelogo.c`) then AVs at `0x6b157a88`/`0x70157a88` — symbolised: `modelPromoteNodeOffsetsToPointers` (`model.c`) ← `load_object_fill_header` (`objecthandler_2.c`) ← first prop/item model load. Root cause is **not** a code regression: the port loads PC-layout model geometry from `data/pcmodels-<region>/{pcmodels.bin,manifest.csv}` and stage bg/stan from `data/pccg-<region>/{pccg.bin,manifest.csv}`, both produced offline by `tools_pc/d43_emit.py` / `d69_emit.py` from the ROM (see `port/src/pcmodels.c` / `pccg.c`; `pcmodelsReserveSize` logs *"pcmodels.bin not found — model loads will fail"* and returns 0). CI has no ROM so it never runs the emit scripts, and the two `data/` dirs are (correctly) gitignored ROM-derived game data — **cannot be committed or shipped** (converted Nintendo/Rare geometry, DLs, collision, stan nav; = distributing assets). A local build with those dirs present runs clean (verified M-34: 1741 frames, `romdataInit ... mapped at 0x10000000`, `pcmodels: 512 sidecars`, `pccg: 73 sidecars`, no crash — so `0x10000000` is *not* the problem here). The `docs/building.md` sidecar-gen step was also missing entirely. **Fix = release-side, backlogged** (`docs/BACKLOG.md` → "Alpha release"): bundle the emit scripts + their committed inputs as a pure-stdlib-Python asset-prep tool the user runs once against their own ROM (~5 MB output, no MIPS toolchain). Secondary/latent: the fixed-address `VirtualAlloc((LPVOID)0x10000000)` in `romdata.c` has no working fallback (`"using heap copy — direct ROM reads will fail"` then limps into the same crash) — didn't bite M-34 but will on a machine where something occupies `0x10000000`; harden separately (reserve earliest in `main`, or retry low bases and derive all segment math from the base obtained). | Diagnosed, not fixed — release packaging gap + latent `romdata.c` fallback |
| D180 | **Native-PC input pass (M-XX QoL run, `qol/native-pc-input-menu`, port-only).** Three parts, all config-gated with default = prior behaviour: **(WI-1) `Input.MouseCaptureMode`** (0 = legacy always-grab; 1 = Quake-style click-to-lock) in `port/src/input.c` + `port/src/video.c` — in mode 1 the OS cursor is free until a click lands in the game window (`SDL_MOUSEBUTTONDOWN` → `inputNotifyClick`), and ESC / focus-loss / entering any front-end menu (`current_menu != MENU_RUN_STAGE`) frees it again; re-entering a stage while still "armed" re-locks so unpausing needs no click. `reconcileGrab()` runs once per controller-0 poll. Mouse buttons are suppressed from the game while the cursor is free in-stage (no phantom fire). Controller paths untouched. **(WI-2) absolute-cursor menu tracking** — when capture mode is on and the cursor is free in a menu, the D165/D169 pointer P-controller takes its target from the real cursor's absolute window position mapped onto the live virtual front-end rect (`getPlayer_c_screen*`), giving true 1:1 tracking instead of the relative-delta estimator. Relative path unchanged for legacy/grabbed. **(B3) `Input.MouseAimSpeed` default 25 → 16** (aim mode still overshot at 25 per the backlog). | LANDED, port-only, no `#ifdef PORT`. Build 241/241, `-level_09` framediff 3/3 within threshold (unregressed), `GE_STARTMENU` menu boot crash-free. **Feel-checks owed** (headless can't drive the mouse): click-to-lock ergonomics, absolute menu tracking, the new aim-speed default. |
| D181 | **`Game.ScreenShakeIntensity` — first route-(b) `src/` gameplay-cosmetic hook (M-XX QoL run).** `src/fr.c viShake()` gains a single `#ifdef PORT` line — `intensity *= portScreenShakeScale;` (extern `f32`, defined + `configRegisterFloat`'d 0.0–10.0 in `port/src/video.c`) — before the existing clamp. Default `1.0f` ⇒ exact no-op, every headless golden dump byte-unaffected; `0.0` disables explosion/effect screen shake, up to `10.0` exaggerates it. N64 build (no `-DPORT`) keeps the original line verbatim. Precedent per `docs/BACKLOG.md` "Fun features → screen-shake intensity slider" and AGENTS.md #2's documented-exception path. Same policy class as a future FOV hook. | LANDED behind config, default = original. Build green. |
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
| D117 | frame nondeterminism root-caused; `GE_DETERM` deferred; `framediff.py` structural | resolved (mode deferred) |
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
`-level_36` (Surface, has mandatory ladders) boots crash-free. **Interactive
climb test owed** — headless can't drive the climb input.

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
