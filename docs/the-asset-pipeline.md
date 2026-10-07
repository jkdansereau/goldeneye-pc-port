---
title: The asset pipeline
description: How the GoldenEye 007 PC port turns N64 ROM assets into the model and graphics files it loads at runtime.
date: '2026-10-05'
modified_time: '2026-10-05'
---

## The asset pipeline

The N64 stored the game's assets (models, textures, fonts, level and cutscene
data) inside the ROM in on-hardware formats a PC can't use directly. The port
converts them into two "sidecar" files it loads at runtime:

- **`pcmodels`** — the 3D models (a PC layout of the N64's RZ model format).
- **`pccg`** — the graphics: textures, fonts and the rest of the ROM data.

Each is a single concatenated image plus a small `manifest.csv` (name, offset,
size) that the loader reads — `port/src/pcmodels.c` and `port/src/pccg.c`.

**Where they come from.** Two paths produce the same files:

- **A downloaded build** generates them from *your* ROM on first start, into
  `data/pcmodels-<region>/` and `data/pccg-<region>/` next to the executable.
  Delete them at any time — they are made again on the next start.
- **Building from source** runs the offline Python converters in `tools_pc/`
  to produce the same files (see the [Building](building.md) guide, §4).

This is one of the deliberate differences from the Perfect Dark port, which
fixes up the N64 asset formats at load time: GoldenEye's serialized formats
are converted **offline** instead.

*More: [building](building.md) §2 and §4; the loaders are
`port/src/pcmodels.c` and `port/src/pccg.c`.*
