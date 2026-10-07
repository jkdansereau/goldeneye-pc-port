---
title: Framerate and pacing
description: How the GoldenEye 007 PC port holds a steady 60 fps and keeps per-frame game timing correct on a fast CPU.
date: '2026-10-05'
modified_time: '2026-10-05'
---

## Framerate and pacing

The port targets a **steady 60 fps** with VSync and an optional 30/60 frame
cap, including on low-end GPUs.

The N64's game code assumed a **60 Hz vertical-interrupt tick** — it ran a
few ticks per displayed frame. On a PC the CPU is far faster, so left alone
the game logic would race ahead. The port handles this in two places:

- **Frame pacing** happens in `port/src/video.c` on the render thread (the
  software RSP thread), which paces the frame and does the swap and FPS
  accounting.
- **Per-frame counters** in the game are gated with `portN64FrameStep()` so
  they advance at the *display* rate, not the CPU rate. Getting this wrong is
  a recurring class of subtle timing bug (a counter that ticks too fast, a
  stall that isn't real) — the [porting notes](porting-notes.md) track these
  (for example D13, D22 and D486).

On very weak integrated GPUs (Atom/Celeron class) the first launch writes
lighter values for draw distance, LOD distance and anti-aliasing into
`ge007.ini` so the game still holds 60 fps.

*More: [porting notes](porting-notes.md) — the timing-related findings (D13,
D22, D486).*
