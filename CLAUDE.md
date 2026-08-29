# CLAUDE.md — GoldenEye 007 PC port

This file auto-loads every session. It is the operating manual. The full
rules are in `AGENTS.md` (imported below); this file adds the two
checklists that were being skipped.

@AGENTS.md

## Session preflight — do this in your FIRST few tool calls, before acting

Having a concrete "next step" from the handoff is NOT license to skip this.

1. Read `docs/HANDOFF.md` — immediate task, standing procedure, environment gotchas.
2. Read `docs/PORT-LEARNINGS.md` — recurring bug classes (you will re-derive
   bugs already catalogued here if you don't).
3. If you may dispatch a subagent this session, read `docs/AGENT-WORKFLOW.md`
   NOW — not at dispatch time.

## Dispatch preflight — EVERY Agent/Task spawn, no exceptions

A subagent brief is INVALID and must not be sent unless it contains all of:

- **FILES YOU MAY TOUCH** — disjoint from every other running agent.
- **BUDGET** — `<= N build->run->inspect cycles` or `~M min`. Tight for
  cheap-class bugs (truncations, off-by-ones); more room for structural ones
  (matrix handedness, format specs). Escalate the default as the project hardens.
- **ON EXPIRY** — stop, revert temp probes, write up what you have in
  `docs/PCPortResearch.md` §F with an explicit confidence rating. A good
  write-up of a half-solved bug is a deliverable, not a failure.
- **KNOWN-GOOD / RULED OUT**, **CONSTRAINTS** (no game-logic changes;
  ABI/layout/format only; `#ifdef PORT`; documented in §F), **REPORT** shape.
- Instruction to read `docs/PORT-LEARNINGS.md` first and append any
  generalisable quirk to it.

Full template: `docs/AGENT-WORKFLOW.md` -> "Standard investigation-subagent
brief template".

## Verification cadence (integrator / overseer)

- **Per merged patch:** single-frame `GE_PCDUMP` capture diffed against the
  committed golden baseline. Cheap. Do NOT gate every patch on a long soak.
- **Per batch / integration checkpoint:** one 60s `-level_09` crash-free run.
- Subagents on small disjoint subsystems (input, audio, saves, converters)
  don't run the game — the overseer verifies.
