# fast3d — software RSP for the GE PC port

This directory will hold the **software RSP**: a C++ interpreter that consumes
the Gfx display list the game builds and emits OpenGL calls, bypassing the N64
RDP entirely.

## Source: adapt from the Perfect Dark PC port

The PD port ships a complete, working fast3d at
`perfect-dark-pc-port/perfect_dark/port/fast3d/`:

| File               | Role                                                        | ~Lines |
|--------------------|-------------------------------------------------------------|--------|
| `gfx_pc.cpp`       | The RSP interpreter: matrices, lights, fog, tri1/tri4, vtx  | 2800   |
| `gfx_opengl.cpp`   | OpenGL backend: shaders, textures, zbuf, framebuffers       | 1300   |
| `gfx_cc.cpp`       | Color-combiner equation -> GLSL shader compiler             | 60     |
| `gfx_sdl2.cpp`     | SDL2 window-manager backend                                 | 460    |
| `gfx_api.h`        | Public API (`gfx_init/run/start_frame/end_frame/...`)       | 58     |
| `gfx_pc.h`         | Internal state (RSP struct, texture cache, combiner pool)   | 60     |
| `gfx_rendering_api.h` | Rendering-API vtable (GL backend implements this)         | 61     |
| `gfx_window_manager_api.h` | Window-manager vtable (SDL2 implements this)          | 54     |
| `glad/`            | GL loader                                                   | —      |

**The PD fast3d already handles `G_TRI4`** (GE's 4-triangle command) — the PD
game uses it too. So the bulk of the RSP work is done; we adapt, not rewrite.

## GE-specific work to add

1. **`G_SETTEX`** (opcode `0xc0`) — GE's custom texture-bank command
   (`include/gbi_extension.h`, `gsSPUseTexture`). Add a case in the `gfx_pc.cpp`
   dispatch and a corresponding texture-bank selection in the GL backend.
   Reverse the exact semantics from `rsp/graphics/gmain.s` + `src/game/tex.c` /
   `initmttex.c`.
2. **Verify custom color-combiner modes** — `G_CC_MODULATEIFADE`, `G_CC_FADE`,
   `G_CC_DECALFADE`, `G_CC_BLENDRGBFADEA`, etc. The `gfx_cc.cpp` compiler
   handles arbitrary CC equations; confirm each GE mode compiles to correct
   GLSL.
3. **Verify the custom render mode** — `G_RM_CUSTOM_AA_ZB_XLU_SURF`
   (AA_ZB_XLU with `Z_UPD`).
4. **`gDPLoadTLUT06/07`** — map onto the existing `G_LOADTLUT` path.

## Ground truth

`rsp/graphics/gmain.s` (repo root) is the real GE RSP ucode. We do **not** run
it on PC, but it is the authoritative reference for the exact command
encodings GE emits — use it to validate the software RSP's decoding.

## Status

**Empty (scaffolding).** Populate during Phase 2 by adapting the PD fast3d and
adding the GE-specific commands above.
