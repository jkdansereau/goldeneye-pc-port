# GoldenEye 007 — PC Port: architecture &amp; plan

> This began as the pre-implementation research note and is kept as the
> architecture reference. Sections 1–9 describe the design; section 10 lists
> external references. The phased plan in section 8 is largely done through
> Phase 2 — for current status see the [README](../README.md) and
> [`dev/LEVEL-STATUS.md`](dev/LEVEL-STATUS.md); for the blow-by-blow finding
> log see [`dev/findings.md`](dev/findings.md).

This document captures the research done to plan a PC port of
GoldenEye 007, based on the decompilation this repository is forked from
([`n64decomp/007`](https://github.com/n64decomp/007)). It is modelled on the
approach used by the
[Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark),
which ports the same Rare "Indy" engine (a later revision) to modern
platforms.

---

## Contents

1. [Executive summary](#1-executive-summary)
2. [What the PD port actually does (the reference architecture)](#2-what-the-pd-port-actually-does-the-reference-architecture)
   — [2.1 Emulate the RSP, bypass the RDP](#21-the-key-architectural-insight-emulate-the-rsp-bypass-the-rdp)
   · [2.2 How the RSP is invoked](#22-how-the-rsp-is-invoked)
   · [2.3 Build system](#23-build-system)
   · [2.4 PD-port copy-candidate audit](#24-pd-port-copy-candidate-audit-session-2026-08-22)
3. [GoldenEye decomp: what we're porting](#3-goldeneye-decomp-what-were-porting)
   — [3.1 Entry / main loop](#31-entry--main-loop)
   · [3.2 The two libultra trees](#32-the-two-libultra-trees-srclibultra-and-srclibultrare)
4. [N64 hardware surfaces that must be shimmed](#4-n64-hardware-surfaces-that-must-be-shimmed)
5. [GE-specific graphics differences vs PD (the RSP work)](#5-ge-specific-graphics-differences-vs-pd-the-rsp-work)
6. [Audio](#6-audio)
7. [Input & saves](#7-input--saves)
8. [Recommended plan](#8-recommended-plan) — phases 0–4
9. [Open questions / risks](#9-open-questions--risks)
10. [References](#10-references)

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

### 2.4 PD-port copy-candidate audit (session 2026-08-22)

The [Perfect Dark port](https://github.com/fgsfdsfgs/perfect_dark) is a **standing reference**
for this project (see CONTRIBUTING.md): same Rare "Indy"/Bond engine family, and its
port layer solves the same problem classes we are currently hitting. Audited
against our remaining work items:

**Direct copy candidates (Phase 3/4 — our stubs were scaffolded to be replaced
by these):**

| PD file | Lines | Replaces our | Notes |
|---|---|---|---|
| `port/src/mixer.c` | 722 | 31-line stub | libaudio→SDL final mixer; same RareAL family. Adapt to GE's `audi.c` wiring (sample rate/buffering) |
| `port/src/input.c` | 1551 | 48-line stub | SDL_GameController backend: hotplug, rumble, keyboard fallback. Remap the button table to GE's scheme (C-buttons/Z-trig differ from PD) |
| `port/src/fs.c` | 294 | 81 lines | File-backed save dir (`--savedir`, `$S` path expansion) — foundation for Phase 4 EEPROM/PFS file backing (ours are no-op stubs today) |
| `port/src/audio.c` | 75 | 70 lines | Near-identical; diff when Phase 3 starts |

**Reference implementations (Phase 2 — the D32/D37/D43 class).** PD's port
layer has a `port/src/preprocess/` module (~4,125 lines): a per-ROM-segment
`preprocessfunc` table in their `romdata.c` that converts N64-layout asset data
to PC-native layout *at load time* — a systematic solution to the problem class
we have been fixing ad-hoc per asset type.
- `filemodel.c` (1,114) — the direct **D43** analogue: same `PROMOTE` idiom,
  **same vma 0x5000000**. NOT a drop-in copy (GE's `ModelRoData` union + opcode
  set differ — PD has stargunfire/headspot/gundl node types), but the node-walk
  order, placement/alignment choices, and record handling are Rare-validated
  against near-identical data: adapt rather than design from scratch.
- `filebg.c` (524) — BG tables (an expected D43 follow-on fault).
- `segaudio.c` (362) — audio bank-tree conversion; cross-check for our D37
  `romdataFixupAudioBank`.
- `filelang.c`, `filetiles.c`, `filepads.c`, `filesetup.c`, `gbi.c`,
  `segfonts.c` — the remaining asset types we will hit.

**Not copyable:** GE-specific `ModelRoData` record map; fast3d CC/RM
 correctness (GE's custom GBI modes vs `gmain.s` do not exist in PD); PD extras
(`config.c`, `optionsmenu.c`, `mpsetups.c`, `mod.c`) — options menu, MP setup,
MOD player; not part of 1:1 fidelity.

**Caveats:** copy **port-layer files only** — their decomp was modified for PC
(e.g. `types.h` keeps N64 offset comments while using real pointers); our
non-negotiables are unchanged. Same family ≠ identical format: validate every
copied conversion per-field against GE headers + ROM ground truth (the standing
D32 procedure).

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
  `bossMainloop()` and render frame 1. (Done — D34–D58; see §G.)
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

* PD port: https://github.com/fgsfdsfgs/perfect_dark
* PD decomp: https://github.com/n64decomp/perfect_dark
* GE decomp (this repo): https://github.com/n64decomp/007
* GE docs: https://github.com/kholdfuzion/goldeneye_docs
* N64 devkit / libultra: `include/PR/*.h` in this repo
* GE custom GBI: `include/gbi_extension.h`
* GE RSP ucode: `rsp/graphics/gmain.s`

---
