---
title: The software RSP
description: How the GoldenEye 007 PC port emulates the N64's Reality Signal Processor (the graphics coprocessor) in software and bypasses the RDP.
---

## The software RSP

On the N64, the **Reality Signal Processor (RSP)** is the graphics
coprocessor. The R4300 CPU builds a list of graphics commands each frame
(the **GBI display list**); the RSP runs a "fast3d" microcode that turns
those commands into **RDP** (rasterizer) commands, which the RDP turns into
pixels.

The port does two things to that pipeline:

- **It emulates the RSP in software.** `port/fast3d/` (~7,000 lines of C++,
  adapted from the Perfect Dark port) *is* the RSP: it interprets the GBI
  display list the game builds and, instead of emitting RDP commands, emits
  **OpenGL** calls directly.
- **It bypasses the RDP entirely** — there is no rasterizer to emulate,
  because the software RSP talks straight to the GPU.

The game's own R4300 code runs natively and is unaware of the difference: it
builds display lists and "starts the RSP" exactly as it would on hardware. A
replacement scheduler (`pdsched.c`) catches the "start the RSP" call and runs
the software RSP on the render thread.

GoldenEye's custom commands are handled by the same interpreter: the packed
four-triangle `G_TRI4` command and the fade/blend **color-combiner** modes
(compiled on the fly from the N64's combiner equation to a GLSL shader by
`gfx_cc.cpp`). The N64's own RSP microcode (`rsp/graphics/gmain.s`) is not
run on PC but is the reference the software RSP's command decoding is checked
against.

*More: [internals](internals.md) §2 (the reference architecture) and §5
(GoldenEye-specific graphics work).*
