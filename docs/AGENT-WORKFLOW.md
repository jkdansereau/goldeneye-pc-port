# Agent / subagent workflow for this port

Guidance distilled from Chris Lewis's *"Decompiling a Nintendo 64 Game
in 84 Days"* (`docs/reference/`, Snowboard Kids, 2026) — an AI-assisted
N64 decomp run 7× faster than its predecessor — adapted to this project,
which is a **PC port on top of a decomp**, not a byte-match decomp. The
mechanics differ (we have no permuter / m2c loop; our inner loop is
root-cause → narrow `#ifdef PORT` fix → visual verify) but the
project-structure lessons transfer directly.

## 1. Every subagent task gets an explicit budget, stated in the brief

Their key finding: an open-ended tool (the permuter) that "runs until
100% match or manually stopped" wrecks throughput; giving the agent a
**deadline it can see** let it trade tool-time against other
problem-solving and judge when to give up on a hard function.

Our open-ended tools are the same shape: rebuild-and-run-the-game cycles,
gdb, env-gated probe iteration, staring at PPM dumps. So every subagent
brief must state:

- a **wall/iteration budget** — e.g. "≤ N build→run→inspect cycles" or
  "~M minutes of investigation"; and
- the **fallback on expiry**: *stop, revert probes, write up what you
  have with a confidence rating* — a good write-up of a half-solved bug
  is a deliverable, not a failure.

Escalate the budget as the project hardens: early bugs are cheap
(one-line truncations, off-by-ones), so cap tight; once those are gone
the survivors are structural (matrix handedness, format specs) and
deserve more room. Revisit the default each session.

## 2. A shared, append-only learnings file that briefs point every agent at

They had agents record generalisable IDO quirks in
`DECOMPILATION_LEARNINGS.md`; later agents read it and got better. "Agents
helped document IDO, and the documentation made subsequent agents better."

Our equivalent already exists but is diffuse: `docs/PCPortResearch.md` §F
is 200 KB of narrative. The **recurring PC-port quirk classes** should be
distilled into a short cheat-sheet every subagent brief links and every
subagent is told to append to:

- pointer-width struct growth (32→64) in ROM-serialized / pun-allocated
  structs — the dominant bug class (D53.2, D56, D79, D98–D102)
- 16-byte PC `Gfx`/`Vtx` vs 8-byte N64 — reservation & copy sizing
  (D50.6, D58, D85, D95)
- big-endian rodata read on LE: `f32` word-pairs (D73, D112), header
  offset tables (D68, D87), packed bitfields (D78, D83)
- N64 hardware idioms fast3d doesn't emulate (fill-rect Z clear → D105;
  LOD/detail tiles → D107; K0 segment address folds → D58, D84)
- offline sidecar converter preferred over runtime fixup for whole
  formats (D43, D69, D88)

This is `docs/PORT-LEARNINGS.md` — one screen, an index into §F, not a
replacement. Every investigation-subagent brief links it; every subagent
is told to append any new generalisable quirk before reporting.

## 3. Parallel work: partition by file, consolidate often

They ran 4 git worktrees sharded by a hash of the candidate list, and
found the cost was **synchronisation** — a match in one worktree stayed
invisible to the others until merged, and merging all four took >1 h.
Their fix: make the similarity search read *all* worktrees so a new
reference propagates immediately, and merge to main periodically anyway.

For us (parallel investigation subagents, not worktrees yet):

- **Partition tracks by the files they will touch** so patches don't
  collide. This session: Track 1 = `bg.c`, Track 2 = `chr.c`/`model*.c`,
  Track 3 = `gunfire.c` + a new audit doc — disjoint, merged clean.
  A brief that can't be file-partitioned shouldn't be parallelised.
- Subagents must **revert their own probes** before reporting (leave only
  committed, env-gated, capped probes) and must **not** touch another
  track's uncommitted edits — call them out in the report instead.
- The integrator (main session) consolidates to `master` per-track with
  the §F write-up, then the next round of agents starts from a clean
  tree. Don't let investigation branches drift.
- If we move to real worktrees: `data/` must be **copied, never
  junctioned** (see the `data-dir-junction-hazard` memory —
  `git worktree remove --force` follows the junction and deletes the real
  baserom + sidecars).

## 4. Cheap automated pass before spending agent tokens

They scripted `m2c` across every unmatched function first (0.93 % hit
rate, but near-free) and told agents to exhaust known SDK
versions / compiler flags / `#if` paths before writing original code.

Our pre-flight before dispatching an investigation agent:

- `./build-pc.sh` green + `/linkcheck` clean
- run the existing env-gated probes for the area (`GE_D96`, `GE_D104`,
  `GE_D69*`, …) and attach the output to the brief
- `GE_PCDUMP` + `tools_pc/pixcount.py` baseline capture
- grep `docs/PCPortResearch.md` §F for a prior instance of the bug class
- check `PD_PORT_CHECKOUT` for the PD analogue
  (already a non-negotiable) — the brief should name the file to look at

Give the agent the results, not just the task. "Give the agent the
findings, not just the problem" is the through-line of the whole post.

## 5. What does *not* transfer

- **Model choice.** Their (unscientific) take was Codex > Claude for
  reproducing IDO codegen. Our task is behavioural correctness, not
  compiler-output matching — not comparable; ignore.
- **The permuter / match-percentage loop.** We don't bit-match. Our
  "done" bar is: no fault + visually correct vs. N64 footage
  (`docs/reference/n64-footage-*`), measured with `pixcount.py`.
- **"Understanding the function is the easy part."** True for decomp,
  inverted here — our hard part is often *what N64 hardware/ABI behaviour
  the C is silently relying on*.

## Standard investigation-subagent brief template

```
Repo: REPO_ROOT (git, master, Windows; PowerShell + Bash).
READ FIRST: docs/HANDOFF.md (top), docs/PORT-LEARNINGS.md,
            docs/PCPortResearch.md §F <specific Dxx entries>, AGENTS.md non-negotiables.
TASK: <one bug, one subsystem>.
FILES YOU MAY TOUCH: <disjoint from other running agents>.
KNOWN-GOOD / RULED OUT: <list — do not re-investigate>.
PRE-FLIGHT ATTACHED: <probe output, pixcount baseline, pd_port pointer>.
BUDGET: <N build→run cycles / ~M min>. On expiry: revert probes, write up with confidence.
CONSTRAINTS: no game-logic changes; ABI/layout/format only, #ifdef PORT, documented in §F.
             revert your own temp probes; do not touch other agents' uncommitted edits.
REPORT: (a) root cause + file:line evidence (b) fix diff or why-not
        (c) probes left in tree (d) confidence. Append any generalisable
        quirk to docs/PORT-LEARNINGS.md.
```
