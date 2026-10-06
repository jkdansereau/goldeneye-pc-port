---
title: Documentation
description: The technical documentation for the GoldenEye 007 PC port; the architecture, the N64-to-PC bug catalogue, and how the project is developed.
date: '2026-10-05'
modified_time: '2026-10-05'
---

## Documentation

The technical documentation for the port: the **architecture** and how the
project is **developed**. (Building and running from source live on the
separate [Building](building.md) page.)

## Architecture

- **[Internals](internals.md)** — the architecture: the software RSP, the
  port layer, the asset pipeline, and the phased plan. The single reference
  for what the port is and how it's put together.
- **[Porting notes](porting-notes.md)** — the catalogue of N64→PC bugs: the
  recurring classes of defect and the ground-truth pattern each fix follows.

## Development

- **[Development process](dev-process.md)** — the investigation workflow:
  task budgets, file partitioning, the finding-log discipline, and the rules
  for what counts as a diagnosis versus a change.
- **[Agentic development](dev/agentic-development.md)** — the case study:
  how the two-AI-agent setup ran, its timeline and numbers, and an honest
  read on what did and didn't work.
- **[Security status](security.md)** — what the release packages install
  (and don't): no networking, no telemetry, no ROM, no system writes.
- **[Fidelity status](fidelity.md)** — how faithfully the port reproduces
  the N64 game's logic, and what was audited to back that claim.
