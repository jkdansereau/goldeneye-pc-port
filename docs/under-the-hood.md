---
title: Under the Hood
description: The technical documentation for the GoldenEye 007 PC port; the architecture, the N64-to-PC bug catalogue, and how the project is developed.
---

## Under the hood

The technical documentation for the port: how it works under the covers, the
catalogue of N64→PC bugs it had to fix, and how the project is developed.
(Building and running from source live on the separate
[Building](building.md) page.)

### How it works

- **[Internals](internals.md)** — the architecture: the software RSP, the
  port layer, the asset pipeline, and the phased plan. The single reference
  for what the port is and how it's put together.
- **[Porting notes](porting-notes.md)** — the catalogue of N64→PC bugs: the
  recurring classes of defect and the ground-truth pattern each fix follows.

### How it's developed

- **[Development process](dev-process.md)** — the investigation workflow:
  task budgets, file partitioning, the finding-log discipline, and the rules
  for what counts as a diagnosis versus a change.
- **[Agentic development](dev/agentic-development.md)** — the case study:
  how the two-AI-agent setup ran, its timeline and numbers, and an honest
  read on what did and didn't work.

### Topic notes

Short, single-topic notes — the quick-orientation version of the longer docs
above:

- **[The software RSP](the-software-rsp.md)** — how the N64's graphics
  coprocessor is emulated in software.
- **[The asset pipeline](the-asset-pipeline.md)** — how ROM assets become the
  files the port loads at runtime.
- **[Framerate and pacing](framerate-and-pacing.md)** — the steady 60 fps,
  frame pacing and per-frame timing.
- **[Input and aim](input-and-aim.md)** — controllers, keyboard, mouse aim
  and rebinding.
- **[Saves](saves.md)** — the file-backed N64 save.
