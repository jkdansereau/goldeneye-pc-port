---
title: Development Process
description: The investigation workflow behind the port; task budgets, file partitioning, and the finding-log discipline used across sessions and agents.
---

## How this port is developed

Most of this port's work is not writing code; it is **diagnosis**: figuring
out which N64 hardware or ABI behavior a piece of unmodified game code is
silently relying on, and satisfying it in the `port/` layer without touching
the game's logic. A lot of that diagnosis is done with LLM coding agents, and
the workflow below is what makes that productive rather than chaotic.

For the project context (the two-model split, the timeline, and an honest
assessment of what did and did not work), see
[`dev/agentic-development.md`](dev/agentic-development.md).

It is adapted from Chris Lewis's write-up
[*"Decompiling a Nintendo 64 Game in 84 Days"*](https://blog.chrislewis.au/decompiling-a-nintendo-64-game-in-84-days/)
(Snowboard Kids, 2026), an AI-assisted decompilation that ran several times
faster than its predecessor. That project bit-matches compiler output;
this one matches runtime behavior, so the mechanics differ; but the
project-structure lessons carry over directly.

### Diagnosis is unrestricted; only the fix is constrained

AGENTS.md non-negotiable #2 restricts *changing* `src/game` behavior. It
has never restricted *investigating* it. An agent that stops short of
naming the exact `src/game` struct, field, or logic responsible for a bug
— because touching that file "feels" off-limits — is doing worse
diagnosis than the rules require, not better compliance with them.
Several of this port's hardest, highest-impact bugs (D209, D210, D255)
were exactly this shape: a `src/game` state variable reading wrong,
root-caused only by tracing fully into the game file that owned it.

Every investigation should end able to say which of three buckets
applies:

- **(a) ABI/layout-class.** The true cause is a struct-layout or
  pointer-width misread introduced by the 32→64-bit transition — in a
  ROM-serialized record or a live runtime struct/union alike. Fixable
  under the narrow exception, entirely inside the game file under
  `#ifdef PORT`, no sign-off needed. Check `docs/porting-notes.md` §A1
  first (the pattern's tells + worked examples) — this is the most
  common shape a "`src/game` behavior looks wrong" report turns out to
  have.
- **(b) Port-layer bug that merely manifests through game state.** The
  game code is reading state that's correct for what N64 would have
  produced; something in `port/` (a shim, fast3d, frame timing, an
  uninitialized or mis-ordered port-owned value) is what actually
  differs. Fixable entirely in `port/`, no `src/game` touch, no sign-off
  needed.
- **(c) Genuine game-logic behavior difference.** The decomp's
  byte-identical control flow, given verified-correct inputs, provably
  diverges from real N64 behavior. Rare, and requires the rule-2
  sign-off procedure (§7 below) before any `src/game` edit.

Bucket (c) is rare by design — most bugs that look like (c) turn out to
be (a) or (b) in disguise — but ruling it in or out requires actually
tracing the state to its source, not stopping at the file boundary.
"This state lives in `src/game`" is a reason to keep tracing, not a
reason to stop.

## 1. Every investigation task gets a visible budget

An open-ended tool; for them the permuter, for us "rebuild and run the game
and stare at a frame dump"; wrecks throughput when an agent will grind it
indefinitely. Giving the agent a **deadline it can see** lets it trade
tool-time against thinking and decide when to give up.

So every investigation brief states:

- a **budget**; "≤ N build → run → inspect cycles" or "~M minutes"; and
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
[`porting-notes.md`](porting-notes.md); the recurring N64→PC bug classes,
each entry a terse index into the full
[`dev/findings.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/dev/findings.md)
log. Every investigation brief links it, and every investigation ends by
appending any new generalisable quirk.

The classes it currently tracks:

- pointer-width struct growth (32→64) in ROM-serialized or pun-allocated
  structs; the dominant class;
- 16-byte host `Gfx`/`Vtx` vs 8-byte N64; reservation and copy sizing;
- big-endian rodata read on a little-endian host: `f32` word-pairs, header
  offset tables, packed bitfields;
- N64 hardware idioms the software RSP does not emulate (fill-rect Z clear,
  LOD/detail tiles, segment-address folds);
- whole serialized formats are converted by an **offline sidecar** rather than
  patched at runtime.

**Two process lessons from the sibling GEVR VR port** (same decomp lineage,
reference-only survey in `scratchpad/GEVR-TRIAGE.md` item D — their corpus
hit both repeatedly enough to name them): (1) **a grep standing in for a
read** — a site list built by grepping one helper name or spelling misses
siblings spelled differently or living in an adjacent file (their own
missed-smoke-scissor-site example); treat a grep-built site list as a draft,
not a census, until it's been read against. (2) **write the falsifier before
the run** — state up front what result would kill the hypothesis ("if X
survives, hypothesis Y is dead"), not just what would confirm it; this fits
the existing budget/findings discipline directly and costs one sentence per
brief.

## 3. Parallelise by file, consolidate often

Lewis ran multiple git worktrees and found the real cost was
*synchronisation*. For parallel investigation agents the same rule applies:

- **Partition tracks by the files they will touch**, so patches never collide.
  A task that can't be cleanly file-partitioned shouldn't be parallelised.
- Each agent reverts its own temporary probes before reporting, leaving only
  committed, env-gated, capped diagnostics; and never touches another track's
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
  the analogous code; same engine family.

Then hand the agent the *findings*, not just the problem.

## 5. What does not transfer from the decomp workflow

- **The match-percentage loop.** This port doesn't bit-match. "Done" is: no
  fault, and visually correct against N64 reference footage.
- **"Understanding the function is the easy part."** True when decompiling;
  inverted here; the hard part is identifying the implicit hardware/ABI
  contract the already-readable C depends on.

## 6. Keep PR/git-history noise low

A session produces a lot of small, independently-verifiable units of work;
one Dxx row, one docs pass, one mechanical fix. Left unmanaged that turns
into a pile of small open PRs that's harder to review than the work
justifies. Rules:

- **Squash-merge single-purpose and docs-only PRs.** The repo has squash
  merge enabled; use it (`gh pr merge --squash --delete-branch`) for anything
  whose in-branch commit history (iteration, probe-add, probe-strip) isn't
  itself useful to keep; which is most PRs. Reserve a plain merge commit for
  a PR whose individual commits are each a distinct, reviewable unit someone
  might want to `git revert` independently (e.g. this session's PR #50: a
  fix commit + a separate analysis-only commit).
- **Batch same-session, same-flavor docs updates into one PR/branch**
  instead of opening a new PR per finding row. A runtime re-check that
  updates one `findings.md` row (like #48) doesn't need its own PR if
  another docs-only branch is already open in the same session; fold it in.
  Only split into separate PRs when the pieces have genuinely different
  review/merge timing (e.g. one needs a human playtest, the other doesn't).
- **A branch takes only the change it's named for.** Don't let unrelated
  housekeeping (a stray gitignore fix, a config tweak) ride along on an
  active feature/fix branch just because it's the one checked out; cut a
  separate branch, even for a one-line change.
- **Stacked PRs declare the dependency in the title/body** ("step 2 of N,
  step 1 is #N") and get rebased/merged in order promptly; don't let a
  later step sit open so long the earlier step's content drifts under it.
- **Triage the open-PR list at least once a session**, ordered by
  time-to-close (docs-only and non-draft first, human-playtest-gated last).
  A PR idle long enough that `main` has moved past the finding rows it
  touches is **stale, not just old**; check for real textual conflict
  (`git merge-tree`), not just calendar age. If `main` already has more
  current information than the PR (a bug the PR still lists `OPEN` that's
  since been fixed and verified), **close it rather than force a merge or
  spend a cycle rebasing it**; re-derive only whatever part of its content
  is still true as a fresh, small PR against current `main`. Merging stale
  content back over newer information is a regression, not a save.
- **Commit/tree hygiene is a standing rule, not a per-session judgment** —
  see AGENTS.md §Tree hygiene & release gate. In short: no local absolute
  paths or usernames in tracked files (use the placeholder convention), no
  contributor emails in prose (name + PR number), every vendored third-party
  license declared in `NOTICE`, no bulk ROM-derived captures in the tree
  (capture output dirs are gitignored — keep them on disk, untracked), and
  one batched commit per session rather than one per approved fix.

## 7. The rule-2 sign-off procedure (genuine `src/game` behavior changes)

Use this only for bucket (c) from the diagnosis framing above — never for
the ABI/layout exception (§A1-class fixes need no sign-off, only
documentation) and never for anything expressible in `port/`.

**To request sign-off, the write-up must state:**

1. The exact `src/game` file:line and the specific behavior being
   proposed for change.
2. **Proof of divergence.** Given verified-correct inputs — i.e. every
   upstream ABI/layout suspect (§A1) and every port-layer suspect has
   been checked and ruled out, with the ruling-out cited — the
   byte-identical decomp code still produces behavior that differs from
   real N64 reference behavior. Name the reference (a capture, documented
   prior N64 behavior, a cited external source). "It looks wrong" is not
   proof; "it disagrees with `<reference>`, verified by `<method>`" is.
3. Why the fix cannot be expressed in `port/`, and why it does not
   qualify under the ABI/layout exception — the two questions that must
   be answered "no" before this procedure applies at all.
4. The proposed fix, scoped as tightly as possible to the one named
   behavior, plus a same-engine precedent if one exists — the Perfect
   Dark port is the standing reference; cite the analogous PD change the
   way `docs/dev/WIDESCREEN-FOV-PLAN.md`'s "Rule #2 exception framing"
   section did.

**Granting.** The user reviews the above and replies with an explicit
go-ahead in-thread. There is no implicit or inferred sign-off: silence,
"seems reasonable," a prior similar grant, or approval of a *different*
behavior in the same file do not carry over to a new request.

**Recording.** Every sign-off gets its own `docs/dev/findings.md` entry
(next `Dxx` label), tagged **`RULE-2-SIGNOFF`** in its status line
alongside the normal finding classification, and recording: the request
as stated above, the grant (quoted or summarized, with date), and the
resulting diff. Add the label to `findings-index.csv` like any other row,
so `tools_pc/gen_findings_index.py` keeps it discoverable — chat history
is not the record of approval; the finding entry is.

**Constraints that still apply after approval:**

- Still `#ifdef PORT` — the N64 build is untouched, and the `#else` arm
  (or the file's un-PORT-guarded default) still matches the original
  decomp exactly.
- Still scoped to the one named behavior — a grant is not a standing
  license; a different behavior difference in the same file needs its own
  request, even if related.
- Still fully documented in the `RULE-2-SIGNOFF` finding entry — that
  entry is the permanent record of *why* this specific exception to
  non-negotiable #2 exists.
- If the fix would visibly change behavior at *default*
  settings/difficulty/region beyond the one bug being fixed, treat that
  as a signal the scope drifted mid-implementation — stop and go back for
  a fresh sign-off on the wider scope rather than shipping it under the
  narrower original grant.

This generalizes the ad hoc precedent in
`docs/dev/WIDESCREEN-FOV-PLAN.md`'s "Rule #2 exception framing" section (a
real, already-granted case, kept as historical record) into a reusable
process; new requests cite this section rather than re-deriving the case
for a process each time.

## Investigation-brief template

```
TASK: <one bug, one subsystem>.
READ FIRST: docs/porting-notes.md; docs/dev/findings.md <specific Dxx entries>.
FILES YOU MAY TOUCH: <disjoint from any other in-flight work>.
KNOWN-GOOD / RULED OUT: <list; do not re-investigate>.
PRE-FLIGHT ATTACHED: <probe output, baseline capture, PD-port pointer>.
BUDGET: <N build->run cycles / ~M min>. On expiry: revert probes, write up with confidence.
CONSTRAINTS: diagnosis may trace as deep into src/game state as the evidence
  needs — name the exact struct/field/logic responsible before concluding
  which bucket applies (see "Diagnosis is unrestricted" above). The FIX is
  still constrained: no src/game behavior changes unless (a) it's an
  ABI/layout-class struct-layout or pointer-width misread — check
  docs/porting-notes.md §A1 first; #ifdef PORT; documented in the finding
  log under §F/D3x — or (b) it's fixable entirely in port/ (no src/game
  touch). A genuine game-logic behavior difference (bucket c) is NOT
  authorized by this brief — stop and write up a rule-2 sign-off request
  per docs/dev-process.md §7 instead of applying it.
VERIFY: <the exact tools_pc/verify.sh (or probe) invocation that proves this done; or, if runtime verification is impossible in this environment, say so and name what a human must run>.
REPORT: (a) root cause + file:line evidence  (b) fix diff, or why not
        (c) probes left in tree  (d) confidence.
        Append any generalisable quirk to docs/porting-notes.md.
```
