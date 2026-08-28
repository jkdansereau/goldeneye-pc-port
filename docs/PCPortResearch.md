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

### 2.4 PD-port copy-candidate audit (session 2026-08-22)

The PD port (`PD_PORT_CHECKOUT`) is a **standing reference**
for this project (see AGENTS.md): same Rare "Indy"/Bond engine family, and its
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

### G. Phase 2 status (current)

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

### H. Handoff & plan (current session)

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

**D76 (OPEN: 2D graphics — disclaimer/legal screen only partially drawn).**
On the opening disclaimer (legal text) screen, only the **"game classification"** line and
**one line below it** draw correctly; the rest of the 2D graphics do not render. Likely the
same image-table/endianness class as D68 but incomplete: D68 (`port/src/gimgfixup.c`) bswaps
IMAGESEG Gfx w1 words + `sImageTableEntry.index` after texReset's romCopy and syncs compiled
globalDL shadows — if a subset of the entries this screen references is missed (or its shadow
not synced), exactly this "only a couple of lines render" symptom results. Secondary
possibility: D74 sub-tile UV wrapping, if those lines use WRAP sub-tiles (was 3D-logo
specific, so lower likelihood). Files to check: `port/src/gimgfixup.c`, `src/game/tex.c`, the
image-table consumers. Verify: enumerate the image entries the disclaimer screen references and
confirm each is covered by the D68 fixup; PPM frames f20–f100 (legal-text window per the D70
timeline).

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

**PARTIAL:** D100 clears the gait-model overrun (GE_D51 trace: clean
pointers) but `-level_09` still crashes at VI post 601, now alternating
(non-det) between:
1. `modelInitRwData` (`model.c:6298`) FAULT `0x1_70076514` — same shape,
   `modelGetNodeRwData` `&data[index]` with a **garbage `index`**
   (~1.07 e9): `index = root->Data->BSP.RwDataIndex` is a RoData-record
   field — check `ModelRoData_BSPRecord` / the `Data` union PC layout vs
   `tools_pc/d43_emit.py`'s op-9 record widening + byteswap. Or the
   HEAD-node `Parent`-walk (`model.c:527-540`) escaping the model image.
2. `modelFindNodeMtxIndex` (`model.c:375`) FAULT `0x4012c9c0` — a
   truncated `ModelNode *` (low 32 of `0x1_4012c9c0`), from
   `sub_GAME_7F06DB5C` (`model.c:1479`). Another D86/D92-class node-ptr
   truncation.

Both are the player's animated/skeletal model node walk — D75 territory,
now the literal render blocker.

**Still open, separate:** `struct tex` headers are 24 B vs 16 B on PC
(texpool-triage) — pool-pressure; a real PC memory-budget pass should
cover it.

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
