# GoldenEye 007 — PC Port: Preliminary Research

This document captures the preliminary research done to plan a PC port of
GoldenEye 007, based on the decompilation in this repository
(`n64decomp/007`). It is modelled on the approach used by the
[Perfect Dark PC port](https://github.com/perfect-dark-pc-port/perfect_dark),
which ports the same Rare "Indy" engine (a later revision) to modern
platforms.

---

## 1. Executive summary

* **Feasibility: HIGH.** GoldenEye and Perfect Dark share the same Rare
  engine family. The PD port proves the whole approach works end-to-end
  (singleplayer + split-screen multiplayer, mouse-look, widescreen, 60 FPS,
  mods). GE is, if anything, a *simpler* graphics pipeline than PD in some
  respects, though it has a few custom RSP extensions (see §5).
* **Recommended strategy: fork this decomp and add a `port/` layer**, exactly
  mirroring the PD port's layout. The N64 build (Makefile + IDO) stays
  untouched; the PC build is a parallel CMake build that compiles the same
  game C sources against a set of PC shims.
* **The single hardest component is the RSP (Reality Signal Processor)
  emulator.** The PD port ships a complete, working "fast3d" software RSP
  (`port/fast3d/`, ~7k lines of C++) that interprets GBI display lists and
  emits OpenGL calls. It **already handles `G_TRI4`**, which is the main
  custom triangle command GE uses. GE's other custom command, `G_SETTEX`,
  appears **unused** (its emitter `gsSPUseTexture` is never called), so the
  remaining GE-specific work is mostly **verifying the custom
  render/color-combiner modes** against the RSP ucode.
* **Audio is straightforward.** GE uses the standard libaudio (AL) engine on
  the R4300, the same as PD. The PD port's SDL audio + mixer approach carries
  over directly.
* **Saves** are **EEPROM-based** (controller EEPROM via `osEeprom*`), not
  Memory Pak. The PFS/motor code in `joy.c` is only for accessory (Rumble Pak
  / Memory Pak) detection and can be stubbed. Phase 4 is therefore a
  file-backed EEPROM plus no-op PFS/motor shims — simpler than a full Memory
  Pak emulation.

---

## 2. What the PD port actually does (the reference architecture)

The PD port is a **fork of the PD decomp** with one added top-level
directory, `port/`. The original game code (`src/game/*.c`, `src/lib/*.c`)
is compiled **unmodified** for the PC. All N64-hardware dependencies are
satisfied by the `port/` layer:

```
port/
├── fast3d/            # Software RSP: interprets GBI display lists -> OpenGL
│   ├── gfx_pc.cpp     #   the RSP interpreter (matrices, lights, tris, fog)
│   ├── gfx_opengl.cpp #   OpenGL rendering backend (shaders, textures, zbuf)
│   ├── gfx_sdl2.cpp   #   SDL2 window-manager backend
│   ├── gfx_cc.cpp     #   color-combiner -> GLSL shader compiler
│   └── ...
├── include/           # port-facing headers
│   ├── audio.h  config.h  fs.h  input.h  mixer.h  mod.h
│   ├── romdata.h  system.h  video.h  utils.h  platform.h
└── src/
    ├── main.c         # PC entry point (replaces boot.s / _start.s)
    ├── libultra.c     # shims for the libultra OS API (threads, time, VI, PI, SI, SP, AI)
    ├── video.c        # SDL2 window + GL context + frame pacing
    ├── audio.c        # SDL audio device, buffer queueing
    ├── mixer.c        # audio mixing / RSP-audio-buffer processing
    ├── mod.c          # MOD music playback
    ├── input.c        # SDL2 keyboard/mouse/gamepad -> N64 controller structs
    ├── fs.c           # filesystem abstraction over the ROM + data dir
    ├── romdata.c      # loads the .z64 ROM from disk, sets up segments
    ├── config.c       # INI config (pd.ini)
    ├── system.c       # platform primitives (time, logging, paths)
    ├── crash.c        # crash handler / stack traces
    ├── pdsched.c      # replacement RCP scheduler that drives the software RSP
    └── utils.c
```

### 2.1 The key architectural insight: emulate the RSP, bypass the RDP

On real hardware the render path is:

```
R4300 builds a Gfx display list
        |
        v
   RSP (coproc 0) runs "fast3d" ucode, turns GBI cmds into RDP cmds
        |
        v
   RDP (coproc 1) rasterizes into the framebuffer
```

The PD port **emulates the RSP in software** (`fast3d/gfx_pc.cpp`) and
**bypasses the RDP entirely**: the software RSP translates GBI commands
directly into OpenGL calls. The R4300 game code is run natively and is
unaware of the difference — it just builds display lists and "starts the
RSP" as usual.

The frame flow (from `port/src/pdsched.c` + `port/src/video.c`):

```
videoStartFrame()  -> gfx_start_frame()
   game builds display list (Gfx*)
videoSubmitCommands(Gfx* cmds) -> gfx_run(cmds)   # <- the software RSP runs here
videoEndFrame()    -> gfx_end_frame()             # swap buffers, FPS accounting
```

### 2.2 How the RSP is invoked

The game's RCP scheduler (`src/lib/sched.c` in PD, `src/sched.c` in GE) calls
`osSpTaskLoad()` + `osSpTaskStartGo()` to hand a task (gfx or audio) to the
RSP. The port:

* **Excludes** the original `libultra/io/sptask.c` (and the other `io/*.c`
  hardware drivers) from the PC build.
* Provides its own scheduler (`pdsched.c`) that, instead of writing to RSP
  registers, calls `gfx_run()` on the display list for gfx tasks and feeds the
  audio buffer for audio tasks.
* Shims the rest of the libultra OS API in `libultra.c` (threads become
  no-ops / single-threaded, `osGetTime` maps to a host clock, VI/PI/SI/AI
  functions map to the port's video/audio/fs/input layers).

### 2.3 Build system

The PC build is **CMake** (the N64 build stays Make + IDO). Dependencies:
**SDL2, zlib, OpenGL** (plus `stdc++`, `m`, `dl`). It compiles:

* `src/game/*.c` (all of it)
* a curated list of `src/lib/*.c` (the engine helpers that are
  platform-independent: mema, memp, model, music, snd, vi, rdp, etc.)
* `src/lib/ultra/audio/*.c` (libaudio — runs fine on the CPU)
* `src/lib/ultra/gu/*.c` (gu matrix helpers)
* `port/src/*.c` + `port/src/*.cpp` + `port/fast3d/*.cpp`

It deliberately **excludes** the N64 I/O drivers (`src/lib/ultra/io/*.c`),
the boot/TLB assembly, and the RSP microcode (`.s` under `rsp/`).

---

## 3. GoldenEye decomp: what we're porting

* **Repo:** `n64decomp/007` (this repo). WIP decomp that byte-matches the
  US/EU/JP ROMs. Game code lives in `src/game/` (~242 files) plus engine
  files in `src/`.
* **Engine layout** (same family as PD):
  * `src/sched.c` — RCP scheduler (`osCreateScheduler`, `__scExec`,
    `__scYield`). Structurally near-identical to PD's `src/lib/sched.c`.
  * `src/game/rsp.c` — builds the Gfx display list, wraps it in an `OSScTask`,
    and starts it via the scheduler (`rspGfxTaskStart`).
  * `src/fr.c` — frame/VI management, video modes (`osViSetMode`).
  * `src/audi.c`, `src/snd.c`, `src/music.c` — libaudio-based sound + music.
  * `src/joy.c` — controllers, **EEPROM** save handling (via `joyGamePak*`
    -> `osEeprom*`), plus PFS/motor code for accessory (Rumble Pak / Memory
    Pak) detection only.
  * `src/init.c` — `init()` -> `mainproc()` -> `bossEntry()` (game entry).
    **Compiled for the PC** (provides `mainproc()`); its N64-only `init()` is
    stubbed (see A3).
* **Custom RSP microcode:** `rsp/graphics/gmain.s` (1545 lines) — a modified
  fast3d. This is the RSP-side code that consumes the display list. For the PC
  port we do **not** run this; we replace it with the software RSP. But it is
  the authoritative reference for which GBI commands GE actually emits.

### 3.1 Entry / main loop

```
boot.s -> init()  [src/init.c]
  -> decompress data segment
  -> osInitialize(), TLB setup
  -> osCreateThread(mainThread, mainproc)
mainproc()
  -> idleCreateThread(), piCreateManager(), rmonCreateThread()
  -> schedulerInitThread()   # osCreateScheduler + osScAddClient(gfxClient)
  -> bossEntry()             # [src/boss.c] real game start
```

The port's `main.c` replaces `boot.s`/`init()` and drives `mainproc()` (or
`bossEntry()`) directly after setting up video/audio/input/ROM.

### 3.2 The two libultra trees: `src/libultra/` and `src/libultrare/`

GE ships **two** libultra trees. `src/libultra/` is the standard Nintendo
libultra; `src/libultrare/` holds Rare's modified/replacement files. The N64
build selects which files to compile via `src/libultrare/Makefile.libultrare`,
which explicitly lists each file as *original* (from `libultra/`) or *Rare*
(from `libultrare/`). The Rare files **override** the originals where they
overlap.

What the PC build must do with each:

* **`src/libultra/audio/*.c`** — standard libaudio (AL). Runs on the CPU;
  **compile** (the game's `audi.c`/`snd.c`/`music.c` depend on it).
* **`src/libultrare/audio/*.c`** (`drvrNew.c`, `env.c`, `reverb.c`) — Rare's
  audio "New" driver. `synthesizer.c` calls `alSaveNew`/`alAuxBusNew`/
  `alMainBusNew`/`alLoadNew`/`alResampleNew`/`alEnvmixerNew`/`alEnvmixerParam`/
  `alFxParam`, all defined here. Runs on the CPU; **compile**.
* **`src/libultra/gu/*.c`** — matrix helpers (`guMtxF`, `guOrtho`, ...).
  **Compile**.
* **`src/libultrare/io/vitbl.c`** — defines `osViModeTable` (a data table),
  referenced ~15× by `fr.c`. **Compile** (it's data, not a driver). This is the
  *only* `io/` file we compile.
* **All other `src/libultra/io/*.c` and `src/libultrare/io/*.c`** — hardware
  drivers (PI, SI, SP, DP, VI, controller, PFS, motor, EEPROM, ...). **Exclude
  and shim** in `port/src/libultra.c`. Notably `pfsinit.c`/`pfsisplug.c`
  (`osPfsInit`/`osPfsIsPlug`) and `motor.c` (`osMotor*`) are called by
  `joy.c` only for accessory detection, so they are shimmed as no-ops (see §7).
* **`src/libultra/os/*.c` + `src/libultrare/os/*.c`** — OS core (threads,
  message queues, timers, cache, TLB). **Exclude and shim** in
  `port/src/libultra.c` (+ `n64stubs.c` for the boot/TLB/FPU symbols).

> The exact original-vs-Rare file lists are in
> `src/libultrare/Makefile.libultrare` (`LIBULTRA_*_C_FILES` vs
> `LIBULTRARE_*_C_FILES`). Use it as the ground truth when curating the PC
> build's source list.

---

## 4. N64 hardware surfaces that must be shimmed

| Subsystem | GE API used | Port strategy (from PD) |
|-----------|-------------|--------------------------|
| RSP (coproc 0) | `osSpTaskLoad/StartGo/Yield`, display lists | Software RSP (`fast3d`) — **main work** |
| RDP (coproc 1) | `osDpSetStatus`, `osDpSetNextBuffer`, render modes | Bypassed; OpenGL backend |
| VI (video) | `osViSetMode`, `osViVSyncCallback`, `osViWaitVSync` | SDL2 window + GL context + frame pacing |
| AI (audio) | `osAiSetFrequency/SetNextBuffer/GetLength` | SDL audio device + queue |
| PI (peripheral) | `osPiRawStartDma`, `osPiCreateManager`, cart reads | Read from ROM file on disk |
| SI (controller) | `osContInit/StartReadData`, `osEeprom*`, `osPfs*`/`osMotor*` | SDL input + file-backed EEPROM; PFS/motor stubbed (accessory detection only) |
| OS core | threads, msg queues, timers, `osGetTime`, cache | Single-threaded shims + host clock |
| R4300 specifics | TLB, cache, FPU csr, K0/K1 segments | No-ops / identity (native x86-64) |

---

## 5. GE-specific graphics differences vs PD (the RSP work)

GE's `include/gbi_extension.h` defines the custom GBI surface. Compared to the
PD port's `fast3d`, the deltas are:

1. **`G_TRI4`** — draws up to 4 triangles in one command using 4-bit vertex
   indices (0–15). A memory-saving optimization (Rare packed 4 tris into the
   slot normally used for `G_TRI2`).
   * **Status: already supported** by the PD `fast3d` (`gfx_sp_tri4`, and the
     PD game itself uses it). ✅
2. **`G_SETTEX`** (`0xc0`) — "use texture from bank": selects a texture by
   bank/tile with detail/mipmap type, min-level, detail-id and texture-id.
   Emitted only by `gsSPUseTexture` (`include/gbi_extension.h:193`).
   * **Status: appears UNUSED.** `gsSPUseTexture` is defined but never called
     anywhere in the game code or the RSP ucode (`rsp/graphics/gmain.s`), and
     `G_SETTEX` appears nowhere outside its `#define`. The game uses the
     standard texture commands (`gSPTexture`, `gSPTextureL`,
     `gSPTextureRectangle`, `gSPTextureRectangleFlip`) — all already handled
     by the PD `fast3d`. **If this holds (verify the decomp is complete),
     Phase 2 shrinks to verifying the custom CC/RM modes and the PD fast3d
     may work largely as-is.** Keep a `G_SETTEX` decode path as a safety net,
     but it is no longer the headline task.
3. **Custom color-combiner modes** — `G_CC_MODULATEIFADE`, `G_CC_FADE`,
   `G_CC_DECALFADE`, `G_CC_BLENDRGBFADEA`, etc. (fade/blend variants).
   * **Status: handled generically** by the fast3d color-combiner -> GLSL
     shader compiler (`gfx_cc.cpp`), which compiles arbitrary CC equations.
     Needs verification that each GE mode produces correct GLSL.
4. **Custom render mode** — `G_RM_CUSTOM_AA_ZB_XLU_SURF` (AA_ZB_XLU with
   `Z_UPD`).
   * **Status: handled** by the render-mode -> GL state mapping; verify.
5. **`gDPLoadTLUT06/07`** — custom TLUT load variants.
   * **Status: minor**, map onto the existing `G_LOADTLUT` path.

**Conclusion:** the PD `fast3d` is a strong starting point. `G_SETTEX` appears
unused (see item 2), so the concrete new RSP work is **verification of the
custom CC/RM modes** against `gmain.s`, with `G_SETTEX` as a low-priority
safety net. Everything else (matrices, lights, fog, tri1/tri4, texture load,
sync/flush) is shared.

> Note: GE's on-hardware RSP ucode is `rsp/graphics/gmain.s`. We do not run it
> on PC, but it is the ground truth for the exact command encodings GE emits —
> use it to validate the software RSP's command decoding.

---

## 6. Audio

* GE uses **libaudio (AL)** on the R4300 (`src/audi.c`, `src/snd.c`,
  `src/music.c`) — identical architecture to PD. Sound synthesis (ADPCM
  decode, mixing, FX) runs on the CPU; the RSP audio ucode is a thin
  pass-through to the AI DMA.
* The PD port runs libaudio natively and routes the mixed output through
  `audio.c` (SDL device) + `mixer.c`. **Carries over directly.**
* Music: GE uses the same MOD-style music system; PD's `mod.c` is the model.
* Output rate: PD uses 22020 Hz stereo s16. GE's `OUTPUT_RATE` in `audi.c`
  should be matched.

---

## 7. Input & saves

* **Controllers:** `src/joy.c` uses `osContInit` / `osContStartReadData`.
  The port maps SDL2 keyboard/mouse/gamepad into the N64 `OSError`/controller
  structs. PD implements 1964GEPD-style and Xbox-style bindings — a good
  default for GE too (fire=Z, aim=R, etc.).
* **Saves:** GE saves are **EEPROM-based**, not Memory Pak. The actual save
  I/O (`src/game/file2.c`) uses `joyGamePakProbe/LongRead/LongWrite`, which
  map to `osEepromProbe/LongRead/LongWrite` (controller EEPROM). The PFS/motor
  code in `joy.c` (`osPfsInit`, `osMotorInit/Start/Stop`) is only for
  accessory (Rumble Pak / Memory Pak) detection. The port therefore:
  * backs the EEPROM with a file in the data dir (e.g. `eeprom.bin`), and
  * stubs PFS/motor to report "no accessory" so the game proceeds without
    rumble/mempak.
  This is simpler than a full Memory Pak/PFS emulation.

---

## 8. Recommended plan

### Phase 0 — Scaffolding (this change)
* Add `port/` directory structure + headers + stub sources.
* Add a top-level `CMakeLists.txt` (+ `cmake/` modules) for the PC build,
  parallel to the N64 `Makefile`.
* This document + `port/README.md`.

### Phase 1 — Boot to a window
* Port `system.c`, `fs.c`, `romdata.c`, `config.c`, `main.c`.
* Port `video.c` + a minimal `fast3d` (clear color, present).
* Goal: load `ge007.u.z64`, open a window, draw a clear color.

### Phase 1.5 — Boot to first frame (ABI reconciliation)
* Get mainThread through all of `bossInitMainthreadData()` → `bossEntry()` →
  `bossMainloop()` and render frame 1. This is the current frontier (post-D31).
* Recurring mechanism: **ROM-data struct ABI/layout fixes** (the D32 pattern) —
  a 32-bit-pointer struct read as a 64-bit-pointer struct misaligns; convert
  embedded pointer fields to `u32` + cast at use sites (PD ground truth), then
  verify the load-time rebase yields valid DRAM addresses. See §H for the
  step-by-step procedure and the standing "non-negotiable #2 ABI exception."
* Goal: title screen / first level actually drawn on the GL surface.

### Phase 2 — RSP / rendering
* Bring in the PD `fast3d` (gfx_pc / gfx_opengl / gfx_cc / gfx_sdl2).
* Verify the custom CC/RM modes against `gmain.s`; add a `G_SETTEX` decode
  path only if it turns out to be used (see §5 — it appears unused).
* Port `pdsched.c` (GE variant) to drive the software RSP.
* Goal: render the title screen / first level.

### Phase 3 — Audio + input
* Port `audio.c`, `mixer.c`, `mod.c`; wire libaudio output to SDL.
* **Decide the ASP strategy** (see §11 C2): if audio runs the `aspMain`
  microcode, wire `aspMainTextStart`/`aspMainDataStart` to the real microcode
  bytes in the ROM; if audio is CPU-only, the `port/src/ucode.c` dummies stand.
* Port `input.c` (keyboard/mouse/gamepad).
* Goal: playable with sound.

### Phase 4 — Saves + polish
* File-backed EEPROM (saves); PFS/motor already stubbed as no-ops.
* Widescreen, mouse-look, FPS options, config file, crash handler.
* Goal: feature-parity with the original + QoL extras.

---

## 9. Open questions / risks

1. **`G_SETTEX` (low priority)** — appears unused (`gsSPUseTexture` is never
   called). If a future decomp revision or a missed call site shows it is used,
   reverse the texture-bank selection (bank -> image, tile, detail/mipmap type)
   from `gmain.s` + `tex.c` / `initmttex.c`. Not currently the main unknown.
2. **Custom CC correctness** — the GLSL compiler must handle every fade/blend
   mode GE uses; validate per-mode against the original. This is now the main
   rendering unknown.
3. **Scheduler fidelity** — GE's `sched.c` has speed-graph / profiling hooks
   (`speedgraphMarkerHandler`) and a slightly different task model than PD;
   the port scheduler must reproduce the gfx/audio interleaving and VSync
   pacing.
4. **Decomp completeness** — the decomp is WIP; some functions may still be
   stubs/`unk_`. The PC port inherits whatever state the decomp is in. Track
   the upstream `n64decomp/007` and rebase.
5. **Endianness / alignment** — GE, like PD, is big-endian N64 code with
   unaligned accesses; the PD port handles this with careful casts and
   `-fno-strict-aliasing`. Reuse those flags.

---

## 10. References

* PD port: https://github.com/perfect-dark-pc-port/perfect_dark
* PD decomp: https://github.com/n64decomp/perfect_dark
* GE decomp (this repo): https://github.com/n64decomp/007
* GE docs: https://github.com/kholdfuzion/goldeneye_docs
* N64 devkit / libultra: `include/PR/*.h` in this repo
* GE custom GBI: `include/gbi_extension.h`
* GE RSP ucode: `rsp/graphics/gmain.s`

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

  **Part B — pointer-rebase correctness: OPEN (next target).** After Part A the
  *values* are still invalid for the crashing weapon-anim entry (gdb, break at
  boss.c:233 after `alloc_load_expand_ani_table`): anim = 0x702adad4 = base
  0x702ad8c0 + 0x214; `bitDescriptors` = 0xf714b067, `bitStream` = 0x000c9100 —
  neither a V1 address. Two concrete anomalies to debug in
  `expand_ani_table_entries()` (initanitable.c:233):
  * `animation_table_ptrs1[]` is a **dense** array of `PTR_ANIM_*` offsets in
    source, but in memory reads `[0x702ad8dc, 0, 0x702adad4, 0, …]` (zeros at
    odd indices). The fixup loop `while (*var_v0 != 0)` would stop at the first
    zero — yet even-index entries show rebased values. Reconcile the array's true
    post-fixup shape and confirm every entry's unk08/unk10 is actually rebased
    (the crashing anim is `animation_table_ptrs1[2]`).
  * Even entry[0]'s `bitStream` comes out wrong (0x882ad8c0 — low 24 bits match
    base, high byte 0x88≠0x70) while its `bitDescriptors` is correct
    (0x702ad8c0). Inspect the second fixup loop (`**var_v0 +=
    &_animation_entriesSegmentRomStart`) for a 32/64-bit or overlap bug.

  The crash site is unchanged (still model.c:914) — Part A is a necessary
  prerequisite, not a regression. Next session: gdb `expand_ani_table_entries`
  step-by-step until every entry's bitDescriptors/bitStream is a valid V1 address.

### G. Phase 2 status (current)

Done: PD fast3d integrated (`port/fast3d/`, replaces the deleted gfxstub.c);
GE's real `src/sched.c` compiled (shedThread runs `__scMain`); real pthread
kernel (D24); dual-mapped DRAM + address shims (D25/D26); ASLR off with startup
sanity check (D27); PI completion messages (D29); SEH-free crash handler with
thread dumps (D30). The process now boots, maps the ROM, opens the window,
runs `mainproc()` → `bossInitMainthreadData` through controller init / timers /
mempool / VI / rsp / stan / game init. The D31 langInit file-load SIGSEGV is
**fixed** (real-zlib decompression, §F/D31): mainThread now loads all language
files and proceeds into `bossInitMainthreadData()` → `alloc_load_expand_ani_table`
→ `init_weapon_animation_groups_maybe` (boss.c:233), where it SIGSEGVs on the
**D32** ROM-struct pointer-width issue. D32 is decomposed: **Part A** (struct
layout — u32 pointer fields) is DONE & proven; **Part B** (the load-time pointer
rebase in `expand_ani_table_entries`) is OPEN and is the current blocker. No
frames rendered yet. See §H for the handoff + plan.

Reference — the file-load chain (for D31 debugging):
`langInit` (language.c:230) loads 7 text banks via
`_fileNameLoadToBank(LnameX_lookuptable[bank][j_text_trigger], …, 0x100,
MEMPOOL_PERMANENT)` → `fileGetIndex` (strcmp over file_resource_table) →
`fileIndexLoadToBank(index, …)` (ob.c:216): if poolRemaining==0 take
`mempGetBankSizeLeft(bank)`; `ptrdata = mempAllocBytesInBank(…)`; hw_address
nonzero → `load_resource(ptrdata, poolRemaining, &file_resource_table[index],
&resource_lookup_data_array[index])` (ob.c:37): `source = (ptrdata + bytes) −
((rom_size+7)&−8)`; `romCopy(source, hw_address, rom_size)`;
`decompressdata(source, ptrdata, buffer)` with `u8 buffer[0x2100]` on the
stack → sets rz_inbuf=source+2 / rz_outbuf=ptrdata / rz_hlist=buffer →
`zlib_inflate()` (src/game/zlib.c:668). rom_size for index 670 = 0x720,
decompressed size = 3872 (0xF20). `file_resource_table` is compiled
(`assets/obseg/file_resource_table.inc.c`) with hw_address = cart addresses;
rom_size = adjacent-marker differences computed in obInit.

Debugging techniques that worked: gdb **launch** mode (`gdb -batch -ex
"handle SIGSEGV stop" -ex run …` — attach fails with error 87); breakpoint
chains through init functions to bracket the crash; `finish` to let a suspect
function complete and inspect its return; hardware watchpoints on exact stack
slots (compute offsets from entry RSP, not RBP — see D31); `addr2line` for
offline symbolication; objdump + `gcc -E` with exact ninja flags to verify
shim expansion in compiled code.

### H. Handoff & plan (current session)

**State.** D31 (langInit file-load SIGSEGV) is fixed & verified — mainThread
boots past all language-file loading into `bossInitMainthreadData()`. The boot
now dies in `init_weapon_animation_groups_maybe()` on the **D32** ROM-struct
pointer-width issue. D32 Part A (struct layout) is done; Part B (load-time
rebase correctness) is the next target. No frames rendered yet.

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

**Next concrete target — D32 Part B.** Make `expand_ani_table_entries()`
(initanitable.c:233) rebase *every* entry's `bitDescriptors`/`bitStream` to a
valid V1 address. Gdb plan: break at boss.c:233 (after `alloc_load_expand_ani_table`),
step through the two fixup loops, and for each `animation_table_ptrs1[i]` /
`animation_table_ptrs2[i]` entry verify `*(u32*)(entry+8)` and `*(u32*)(entry+16)`
are in [0x70xxxxxx]. Resolve these two anomalies first:
* In-memory `animation_table_ptrs1[]` reads `[ptr, 0, ptr, 0, …]` (zeros at odd
  indices) though the source is a dense `PTR_ANIM_*` list — reconcile the array's
  true shape vs the `while (*var_v0 != 0)` loop.
* entry[0]'s `bitStream` = 0x882ad8c0 (high byte 0x88≠0x70) while its
  `bitDescriptors` = 0x702ad8c0 is correct — inspect the second fixup loop
  (`**var_v0 += &_animation_entriesSegmentRomStart`) for a 32/64-bit or overlap bug.

**Non-negotiable #2 refinement (applied to AGENTS.md).** The original "game code
compiles unmodified / fix belongs in port/" is too absolute: pointer-width layout
cannot be isolated in `port/` (no hook between the romCopy and the first read).
Refined to "game **logic** is unmodified" with a narrow, documented exception for
mechanical, semantics-preserving ABI/layout changes forced by the 32→64-bit
transition (embedded pointers → u32 + cast at use), following PD ground truth. No
logic/behavior changes; each such edit is logged in §F/D3x.

**Reassessed plan.** The frontier is now **Phase 1.5 — boot to first frame**:
drive mainThread through the rest of `bossInitMainthreadData` + `bossEntry` to
`bossMainloop()` and render frame 1, using the D32 procedure each time a
ROM-data struct faults. Phase 2 (fast3d/CC-RM/scheduler) then makes those frames
correct. Expect several more D3x ABI fixes (models, textures, other tables) as
more asset loading is reached — this is now a standing sub-task, not a one-off.

**Environment reminders.** MSYS2 tools in `/c/msys64/mingw64/bin/` (not on PATH —
prefix `export PATH=…`). Build: `./build-pc.sh ntsc-final`. gdb **launch** mode
only (attach fails, error 87); symbolicate offline with `addr2line -e
build-pc/ge007.x86_64.exe -f -C <0x140000000+rel>`. Image base 0x140000000.
`load_resource`/many init fns use a fake RBP — compute stack offsets from entry
RSP. The D30 crash handler did **not** write `ge007.crash.log` for the game-thread
SIGSEGV this session (attribute via gdb instead; worth a separate look).
