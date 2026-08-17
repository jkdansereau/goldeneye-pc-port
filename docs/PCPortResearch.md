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
