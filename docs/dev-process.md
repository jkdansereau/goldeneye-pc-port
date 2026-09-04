# How this port is developed

Most of this port's work is not writing code — it is **diagnosis**: figuring
out which N64 hardware or ABI behavior a piece of unmodified game code is
silently relying on, and satisfying it in the `port/` layer without touching
the game's logic. A lot of that diagnosis is done with LLM coding agents, and
the workflow below is what makes that productive rather than chaotic.

For the project context — the two-model split, the timeline, and an honest
assessment of what did and did not work — see
[`dev/agentic-development.md`](dev/agentic-development.md).

It is adapted from Chris Lewis's write-up
[*"Decompiling a Nintendo 64 Game in 84 Days"*](https://blog.chrislewis.au/decompiling-a-nintendo-64-game-in-84-days/)
(Snowboard Kids, 2026), an AI-assisted decompilation that ran several times
faster than its predecessor. That project bit-matches compiler output;
this one matches runtime behavior, so the mechanics differ — but the
project-structure lessons carry over directly.

## 1. Every investigation task gets a visible budget

An open-ended tool — for them the permuter, for us "rebuild and run the game
and stare at a frame dump" — wrecks throughput when an agent will grind it
indefinitely. Giving the agent a **deadline it can see** lets it trade
tool-time against thinking and decide when to give up.

So every investigation brief states:

- a **budget** — "≤ N build → run → inspect cycles" or "~M minutes"; and
- the **fallback on expiry**: stop, revert any temporary probes, and write up
  what was found with an explicit confidence rating. A good write-up of a
  half-solved bug is a deliverable, not a failure.

The budget scales with the project's maturity. Early bugs are cheap (one-line
truncations, off-by-ones) and get a tight cap; once those are gone the
survivors are structural (matrix handedness, serialization formats) and get
more room.

## 2. A shared, append-only learnings file

Lewis's agents recorded generalisable IDO quirks in a learnings file; later
agents read it and did better. Our equivalent is
[`porting-notes.md`](porting-notes.md) — the recurring N64→PC bug classes,
each entry a terse index into the full [`dev/findings.md`](dev/findings.md)
log. Every investigation brief links it, and every investigation ends by
appending any new generalisable quirk.

The classes it currently tracks:

- pointer-width struct growth (32→64) in ROM-serialized or pun-allocated
  structs — the dominant class;
- 16-byte host `Gfx`/`Vtx` vs 8-byte N64 — reservation and copy sizing;
- big-endian rodata read on a little-endian host: `f32` word-pairs, header
  offset tables, packed bitfields;
- N64 hardware idioms the software RSP does not emulate (fill-rect Z clear,
  LOD/detail tiles, segment-address folds);
- whole serialized formats are converted by an **offline sidecar** rather than
  patched at runtime.

## 3. Parallelise by file, consolidate often

Lewis ran multiple git worktrees and found the real cost was
*synchronisation*. For parallel investigation agents the same rule applies:

- **Partition tracks by the files they will touch**, so patches never collide.
  A task that can't be cleanly file-partitioned shouldn't be parallelised.
- Each agent reverts its own temporary probes before reporting, leaving only
  committed, env-gated, capped diagnostics — and never touches another track's
  uncommitted edits.
- One integrator merges each track with its write-up, then the next round
  starts from a clean tree. Investigation branches are not allowed to drift.

## 4. Spend the cheap automated pass before spending agent time

Before dispatching an investigation agent:

- confirm the build is green and the link is clean;
- run the existing env-gated probes for the area and attach their output;
- capture a baseline frame dump;
- grep the finding log for a prior instance of the same bug class;
- check the [Perfect Dark port](https://github.com/fgsfdsfgs/perfect_dark) for
  the analogous code — same engine family.

Then hand the agent the *findings*, not just the problem.

## 5. What does not transfer from the decomp workflow

- **The match-percentage loop.** This port doesn't bit-match. "Done" is: no
  fault, and visually correct against N64 reference footage.
- **"Understanding the function is the easy part."** True when decompiling;
  inverted here — the hard part is identifying the implicit hardware/ABI
  contract the already-readable C depends on.

## Investigation-brief template

```
TASK: <one bug, one subsystem>.
READ FIRST: docs/porting-notes.md; docs/dev/findings.md <specific Dxx entries>.
FILES YOU MAY TOUCH: <disjoint from any other in-flight work>.
KNOWN-GOOD / RULED OUT: <list — do not re-investigate>.
PRE-FLIGHT ATTACHED: <probe output, baseline capture, PD-port pointer>.
BUDGET: <N build->run cycles / ~M min>. On expiry: revert probes, write up with confidence.
CONSTRAINTS: no game-logic changes; ABI/layout/format only; #ifdef PORT; documented in the finding log.
VERIFY: <the exact tools_pc/verify.sh (or probe) invocation that proves this done — or, if runtime verification is impossible in this environment, say so and name what a human must run>.
REPORT: (a) root cause + file:line evidence  (b) fix diff, or why not
        (c) probes left in tree  (d) confidence.
        Append any generalisable quirk to docs/porting-notes.md.
```
