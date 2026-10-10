---
title: Framerate and pacing
description: How the GoldenEye 007 PC port holds a steady 60 fps, draws frames above 60 on fast displays, and keeps per-frame game timing correct on a fast CPU.
date: '2026-10-05'
modified_time: '2026-10-10'
---

## Framerate and pacing

The port targets a **steady 60 fps** with VSync and a frame cap, including on
low-end GPUs. Since v0.6.0 the cap can also go above 60 on a faster display
(see [Above 60 fps](#above-60-fps)).

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

### Above 60 fps

`Video.FpsCap` (F10, *Frame rate cap*) accepts Auto, 30, 60, 90, 120 and 144.
**Auto** is the default on fresh installs (an existing `ge007.ini` keeps its
value) and matches the display's refresh rate, up to 144; on a 60 Hz display it
is plain 60.

- **The game still simulates at its original 60 Hz (NTSC) tick.** The extra
  frames are drawn between two game ticks by blending the previous and current
  frame's matrices (camera and objects), so no game state changes.
- **Cost:** one game tick of extra latency.
- **Inert on 60 Hz displays.** The present rate is the lower of the cap and the
  display's refresh rate. The *Original N64* display mode caps at 60.
- **Fallbacks:** a fast camera turn or a large change in the visible room set
  draws a full (non-blended) frame instead.
- The design comes from f1zz1ec0ke's PR #137; the port reworked it onto the
  render worker thread and from a fixed 2x to any present rate, so a 90 Hz
  Steam Deck OLED works (D578, D583, D584).

Checked by the maintainer: 120 Hz with VSync on a G-Sync display on PC, and 90
fps on the Steam Deck. Known issue: on the Deck at 90, centre firefights in
Bunker 1/2 can drop several fps, and Jungle can feel slightly less smooth.

*More: [porting notes](porting-notes.md) — the timing-related findings (D13,
D22, D486).*
