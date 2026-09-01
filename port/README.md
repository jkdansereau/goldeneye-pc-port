# GoldenEye 007 — PC port layer

This directory contains the PC port layer: the code that lets the (unmodified)
GoldenEye game sources run on a modern host instead of N64 hardware.

It mirrors the layout of the [Perfect Dark PC
port](https://github.com/fgsfdsfgs/perfect_dark), which ports the
same Rare engine family. See `docs/internals.md` (repo root) for the full
research write-up and phased plan.

## Layout

```
port/
├── fast3d/            # Software RSP: interprets GBI display lists -> OpenGL
│   ├── gfx_pc.cpp     #   RSP interpreter (matrices, lights, tris, fog)
│   ├── gfx_opengl.cpp #   OpenGL rendering backend (shaders, textures, zbuf)
│   ├── gfx_sdl2.cpp   #   SDL2 window-manager backend
│   ├── gfx_cc.cpp     #   color-combiner -> GLSL shader compiler
│   └── ...
├── include/           # port-facing headers
│   ├── audio.h  config.h  fs.h  input.h  mixer.h
│   ├── platform.h  romdata.h  system.h  video.h  utils.h
│   └── versioninfo.h.in
└── src/
    ├── main.c         # PC entry point (replaces boot.s / init())
    ├── libultra.c     # shims for the libultra OS API
    ├── video.c        # SDL2 window + GL context + frame pacing
    ├── audio.c        # SDL audio device, buffer queueing
    ├── mixer.c        # audio mixing
    ├── input.c        # SDL2 keyboard/mouse/gamepad -> N64 controller structs
    ├── fs.c           # filesystem abstraction over the ROM + data dir
    ├── romdata.c      # loads the .z64 ROM from disk, sets up segments
    ├── config.c       # INI config (ge007.ini)
    ├── system.c       # platform primitives (time, logging, paths)
    ├── crash.c        # crash handler / stack traces
    ├── gesched.c      # replacement RCP scheduler that drives the software RSP
    └── utils.c
```

## How it works (short version)

* The R4300 game code (`src/game`, `src`) is compiled **unmodified**.
* The **RSP is emulated in software** (`fast3d/`): it interprets the Gfx
  display list the game builds and emits OpenGL calls, bypassing the RDP.
* The **RCP scheduler** (`gesched.c`) replaces `src/sched.c`'s hardware task
  submission: instead of writing RSP registers it calls `gfx_run()`.
* **libultra OS** functions are shimmed in `libultra.c` (single-threaded,
  host clock, VI/PI/SI/AI mapped onto the video/audio/fs/input layers).
* **Saves** (Memory Pak / EEPROM) are backed by files in the data dir.

## GE-specific RSP work

The PD `fast3d` already handles `G_TRI4` (GE's 4-triangle command). The new
work for GE is:

* **`G_SETTEX`** — GE's custom texture-bank command (`include/gbi_extension.h`,
  `gsSPUseTexture`). Must be added to the RSP interpreter + GL backend.
* Verify GE's custom color-combiner modes (`G_CC_*FADE*`) and render mode
  (`G_RM_CUSTOM_AA_ZB_XLU_SURF`) produce correct GLSL / GL state.

Use `rsp/graphics/gmain.s` (the real GE RSP ucode) as the ground truth for the
exact command encodings.

## Status

**Scaffolding only.** The files in `src/` and `fast3d/` are stubs that define
the interfaces and mark the work to be done. The actual implementation is
brought in phase by phase (see `docs/internals.md` §8), largely by
adapting the PD port's `port/` layer.
