# CLAUDE.md — GoldenEye 007 PC port

This file auto-loads every session. It is the operating manual. The full
rules are in `AGENTS.md` (imported below); this file adds the two
checklists that were being skipped.

@AGENTS.md

## Doc load order — read deliberately, not everything

Context is scarce. Load by tier; do not blind-read whole files.

- **Tier 0 — always (auto-loaded):** `CLAUDE.md`, `AGENTS.md`. Nothing
  else auto-loads.
- **Tier 1 — every session start:**
  `docs/HANDOFF.md` (current state + next task + environment — a rolling
  local working file; may be absent in a fresh clone, in which case read
  the README "Status" section instead) — read fully;
  `docs/porting-notes.md` (recurring bug classes) — skim the section
  headers, read the classes relevant to the task.
- **Tier 2 — on demand only, do NOT read start-to-finish:**
  - `docs/internals.md` — architecture / RSP deltas / phased plan (§1–§10).
    Read the section you need.
  - `docs/dev/findings.md` — the `Dxx` finding log (§F + §H).
    **Jump to a specific `Dxx` via the index at the top of §F.** Never
    linear-read.
  - `docs/dev/HANDOFF-ARCHIVE.md` — prior-session narrative (M-2…). Only
    when tracing the history of one fix.
  - `docs/dev-process.md` — before dispatching a subagent (also Tier 1
    if you may dispatch this session).

## Session preflight — do this in your FIRST few tool calls, before acting

Having a concrete "next step" from the handoff is NOT license to skip this.

1. Read the Tier 1 docs: `docs/HANDOFF.md` (immediate task, standing
   procedure, environment gotchas — or the README "Status" section if it is
   absent) and `docs/porting-notes.md` (recurring bug classes — you will
   re-derive catalogued bugs if you don't).
2. Skim the README "Status" section for where the project stands.
3. If you may dispatch a subagent this session, read `docs/dev-process.md`
   NOW — not at dispatch time.

## Dispatch preflight — EVERY Agent/Task spawn, no exceptions

A subagent brief is INVALID and must not be sent unless it contains all of:

- **FILES YOU MAY TOUCH** — disjoint from every other running agent.
- **BUDGET** — `<= N build->run->inspect cycles` or `~M min`. Tight for
  cheap-class bugs (truncations, off-by-ones); more room for structural ones
  (matrix handedness, format specs). Escalate the default as the project hardens.
- **ON EXPIRY** — stop, revert temp probes, write up what you have in
  `docs/dev/findings.md` §F with an explicit confidence rating. A good
  write-up of a half-solved bug is a deliverable, not a failure.
- **KNOWN-GOOD / RULED OUT**, **CONSTRAINTS** (no game-logic changes;
  ABI/layout/format only; `#ifdef PORT`; documented in §F), **REPORT** shape.
- Instruction to read `docs/porting-notes.md` first and append any
  generalisable quirk to it.

Full template: `docs/dev-process.md` -> "Investigation-brief template".

## Verification cadence (integrator / overseer)

- **Per merged patch:** single-frame `GE_PCDUMP` capture diffed against the
  committed golden baseline. Cheap. Do NOT gate every patch on a long soak.
- **Per batch / integration checkpoint:** one 60s `-level_09` crash-free run.
- Subagents on small disjoint subsystems (input, audio, saves, converters)
  don't run the game — the overseer verifies.

## Local Qwen dispatch (cost-free, data-local worker)

A local Qwen3.8-27B is wired in via the `delegate-local` MCP server (see
`C:\Users\james\Source\Repos\20260902-qwen38claudepair`). Use it as a
*subagent whose compute runs on-machine* for scoped, checkable work — the lead
still owns judgment, planning, and the verification gate.

- **Tools:** `local_backend_status()` (run once before first dispatch),
  `list_local_agents()`, `delegate_to_local_agent(agent_name, task, workdir, max_turns?, max_tokens?)`.
- **Workers** (`~/.claude/agents/`): `repo-mapper` (read-only ingest/map),
  `mechanical-coder`, `test-writer`, `triage` (read-only root-cause + patch),
  `refactor-bot`.
- **Dispatch when ALL hold:** narrow blast radius, clear spec, output checkable
  by a build/test/diff. Keep on Claude: ABI/handedness/format reasoning, novel
  debugging, anything you can't cheaply verify.
- **Backend reality:** ~1 inference slot, slow per token — dispatch
  **sequentially**, `max_tokens` ~4–8K, treat as batch work. Needs the LiteLLM
  proxy (`litellm/run.ps1`) + Unsloth Studio both up.
- **Windows quirk:** the worker's shell is `cmd.exe`, not bash — its agent
  prompt tells it to use `read_file`/`findstr`, not `cat`/`grep`. Always
  spot-check the returned artifact against the real files before integrating;
  the 27B will confidently fabricate if a read fails.
- Fits the **dispatch preflight** rules above (FILES / BUDGET / ON EXPIRY /
  CONSTRAINTS / REPORT) — write the `task` string to that shape.
