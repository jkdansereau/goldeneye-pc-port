---
title: GoldenEye 007 PC Port
description: A native PC port of the original Nintendo 64 GoldenEye 007, built from decompiled source — and a case study in AI-agent collaboration on a large low-level codebase.
---

# GoldenEye 007 PC Port

A work-in-progress **native PC port of the original 1997 Nintendo 64
_GoldenEye 007_**, compiled from the game's
[decompiled source](https://github.com/n64decomp/007) and running the N64's
graphics coprocessor in software. It follows the architecture of the
[Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark).

It is also a **research project on AI-agent collaboration in a large,
unfamiliar, low-level codebase**: two coding agents — a local open-weight model
(Qwen 3.8) on a single RTX 5090, and Claude / Claude Code — driven by one person
part-time through ~230 translation units of unmodified big-endian MIPS game
code, handing work back and forth through shared written notes.

**Status:** Phase 2 of 4 (rendering); first alpha **v0.1.0** released. Runs the
full single-player campaign — 18 of 21 missions completable start to finish in
a full-campaign playtest (two levels crash, and the final level can't be
finished). No audio yet; AI pacing and cutscenes are rough.

- **[Project repository and README](https://github.com/jkdansereau/goldeneye-pc-port)** — build instructions, requirements, status, how it compares to the Xbox 360 recompilation projects.

## Documentation

- **[The two-agent development case study](dev/agentic-development.md)** — goal, setup, timeline, the handoff workflow, and an honest assessment of what did and didn't work.
- **[Development process](dev-process.md)** — how work was scoped, partitioned, and budgeted across agents; the finding-log discipline.
- **[Internals](internals.md)** — architecture, the software RSP-emulation approach, GoldenEye-vs-Perfect-Dark engine differences, the phased plan.
- **[Porting notes](porting-notes.md)** — the recurring Nintendo 64 → PC bug classes hit during the port, with fixes.
- **[Game-behavior reference](dev/game-behavior-reference.md)** — how the retail N64 game is meant to behave: combat/AI model, difficulty scaling, per-level objectives, timers, weapon data, and the original game's known quirks.
- **[Building](building.md)** — full build and asset-extraction guide.
