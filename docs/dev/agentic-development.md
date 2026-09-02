# Agentic development: two AI coding agents porting GoldenEye 007

*A case study in AI-agent collaboration on a large, low-level codebase. A
GoldenEye 007 Nintendo 64 → desktop port, taken from "builds a ROM" to
"playable front end" in about two weeks — driven mostly by two coding agents (a
local open-weight model, Qwen 3, on a single RTX 5090, and a hosted frontier
model, Claude / Claude Code), handing work back and forth through shared
written notes under one person's part-time direction. This is the goal, the
method, the timeline, and an honest read on what did and didn't work.*

## Contents

- [Why this project exists](#why-this-project-exists)
- [The setup](#the-setup)
- [Timeline](#timeline)
- [By the numbers](#by-the-numbers) — [Commit velocity](#commit-velocity) · [Who did what](#who-did-what)
- [The handoff workflow](#the-handoff-workflow)
- [Assessment](#assessment)

## Why this project exists

The playable port is real, but it is not the primary deliverable. The goal was
to **test how well coding agents hold up on a large, unfamiliar, low-level
codebase** — one with none of the properties that make web-app work easy for
an LLM:

- ~230 translation units of decompiled Nintendo 64 game C, compiled
  **unmodified**;
- big-endian, 32-bit, MIPS ABI assumptions throughout, run on a little-endian
  64-bit host;
- bugs that surface as a fault or a garbled frame hundreds of milliseconds
  after the actual cause, often in a different subsystem;
- a graphics coprocessor (the RSP) that has to be emulated in software before
  anything draws at all.

Concretely it set out to validate a **two-agent arrangement**: a
locally-hosted open-weight model doing the bulk of the work on a single
consumer GPU, a hosted frontier model brought in for a collaborative phase,
and — the part that turned out most interesting — the two **handing work back
and forth** through shared written artifacts, directed by one human.

## The setup

| | |
|---|---|
| Local agent | `unsloth/Qwen3.8-27B-GGUF:UD-Q4_K_XL` on a single **NVIDIA RTX 5090**, driven mainly through the **[pi](https://pi.dev/)** coding agent. Unsloth Desktop (Unsloth's local model runtime, which can drive agents such as Claude Code) was also trialed but not used significantly. |
| Hosted agent | **Claude**, via **Claude Code**, on a Claude Pro subscription — mostly **Sonnet 5**, with **Opus 5** used as an escalation tier for the hardest problems and whenever there was subscription budget to spend on it |
| Human | one person: direction, work partitioning, integration, and every build / playtest / frame-capture the agents could not run |
| Base | fork of the [GoldenEye 007 decompilation](https://github.com/n64decomp/007) (years of prior work by Larry Ficken ("kholdfuzion") and contributors) |
| Reused | the [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark)'s `fast3d` software RSP — same Rare engine family |

The port is the GoldenEye-specific porting work **on top of** those two
existing bodies of work; it is not a from-scratch reimplementation of either.

In practice the models formed a **three-tier escalation**: the local model
handled well-scoped work, Sonnet 5 took what it stalled on, and Opus 5 was
reserved for the bugs that needed the most reasoning held at once — the same
"escalate when stuck" move applied at every level.

## Timeline

All dates from this repository's own commit history (August 2026).

```mermaid
gantt
    dateFormat YYYY-MM-DD
    axisFormat %b %d
    title GoldenEye 007 PC port — from fork to playable front end
    section Local model - Qwen 3.8 via pi
    Fork + PC-port scaffolding            :m1, 2026-08-16, 1d
    Full compile + link (~230 TUs)        :m2, 2026-08-20, 1d
    Boot to window (ROM map + SDL2)       :m3, 2026-08-20, 1d
    OS shims, threads, fast3d integration :2026-08-21, 2d
    First rendered frames                 :m4, 2026-08-22, 1d
    Offline asset-conversion pipeline     :2026-08-22, 2d
    Entire intro renders (logos to cast)  :m5, 2026-08-24, 1d
    section Both agents - handoff workflow
    Claude joins                          :milestone, 2026-08-27, 0d
    Stage load; Bunker 1 renders + firefight :2026-08-27, 2d
    21 solo levels load + render + no-crash  :m6, 2026-08-29, 1d
    SDL input layer (kbd/mouse/gamepad)   :2026-08-29, 1d
    Front-end flow (menu to briefing to start) :m7, 2026-08-30, 1d
    File-backed EEPROM saves              :2026-08-31, 1d
```

- **Day 0** (16 Aug): repository forked, PC-port scaffolding added.
- **Day 4** (20 Aug): the entire ~230-TU game + libultra set compiles and
  links as a host binary.
- **Day 6** (22 Aug): first real frames render.
- **Day 8** (24 Aug): the whole intro sequence renders — logos, gun-barrel,
  cast roll.
- **Day 11** (27 Aug): second agent joins.
- **Day 13** (29 Aug): **all 21 solo missions load, render, and survive an
  unattended play window without crashing.**
- **Day 14** (30–31 Aug): front end playable end to end (menu → mission
  select → difficulty → briefing → start); file-backed saves.

So roughly **two weeks**, one person part-time, to take a decompilation from
"builds an N64 ROM" to "boots on desktop, renders every solo level, playable
through the front end into the early game" — with audio and a set of cosmetic
issues still outstanding.

## By the numbers

| | |
|---|---|
| Calendar time | 16 days (16 Aug – 1 Sep 2026), one person part-time |
| Commits on the port | ~223 |
| Root-caused bugs logged | 162 (`D1`–`D169` in [`findings.md`](findings.md); some later merged or withdrawn) |
| Handoff sessions | ~33 (`M-2` … `M-33`) |
| Game-source files given `#ifdef PORT` ABI edits | 63 files, 241 blocks |
| New port-layer / tooling files | 128 |
| Port layer | ~17,000 lines C/C++ (`port/`) |
| PC asset-conversion tooling | ~6,300 lines Python (`tools_pc/`) |
| Outcome | intro + all 21 solo missions render, front end playable; audio not yet done |

### Commit velocity

```mermaid
xychart-beta
    title "Commits per day"
    x-axis ["8/16", "8/17", "8/20", "8/21", "8/22", "8/23", "8/24", "8/27", "8/28", "8/29", "8/30", "8/31", "9/1"]
    y-axis "commits" 0 --> 60
    bar [1, 2, 2, 6, 9, 2, 4, 7, 57, 39, 39, 46, 9]
```

Days with no commits are omitted. The 8/25–8/26 gap is between phases; the
step up from 8/28 onward is the collaborative phase in full swing, including
the parallel multi-agent "bursts" used near the end.

### Who did what

```mermaid
pie showData title "Commits by agent (raw count)"
    "Claude" : 161
    "Local model (Qwen 3.8 / pi)" : 62
```

(Phase A — the first 26 commits, to 24 Aug — was entirely the local model,
solo; from 27 Aug on the two agents worked in parallel.)

| Milestone / workstream | Primary agent | Weight |
|---|---|---|
| PC build system, region macros, full compile + link of ~230 units | local model | large |
| Boot chain: ROM map, OS-shim layer (`libultra.c`), host threads, dual-mapped DRAM | local model | large |
| Software-RSP integration + replacement scheduler (`gesched.c`) | local model | medium |
| Offline asset-conversion architecture + first converters (`tools_pc/`) | local model | large |
| Intro rendering (logos → gun-barrel → cast) | local model | medium |
| Stage load unblocked (`D69`–`D87`) | Claude | medium |
| 21-level crash sweep — ~12 crash classes root-caused (`D88`–`D169`) | Claude | large |
| SDL input layer (keyboard / mouse / gamepad, mouse-look) | Claude | medium |
| Front-end flow (menu → briefing → start), EEPROM saves | Claude | medium |
| Parallel struct-layout / converter static audits | local model | medium |
| Continuing tasks after Claude hit a usage limit | local model | small |
| The hardest structural bugs (matrix handedness, format specs) | Claude | — |
| Every build, playtest, frame capture, integration, and direction | human | — |

**Estimated effort split.** The raw commit count above (~28% local / ~72%
Claude) undercounts the local model: Phase A landed the build system and boot
chain in relatively few, large commits, and the local model kept contributing
~15–20% of Phase B in parallel. Weighting by milestone difficulty rather than
raw commits — the foundation is a heavier third of the project than its commit
share suggests — the developer's estimate is roughly:

```mermaid
pie showData title "Agent effort, milestone-weighted"
    "Claude" : 60
    "Local model (Qwen 3.8 / pi)" : 40
```

The local model's ~40% is front-loaded and foundational (the build, the boot
chain, the RSP wiring, the converter architecture) plus continuous parallel
support; Claude's ~60% is the higher-volume Phase-B debugging, the two big
discrete features, and the hardest structural bugs. A 27B open-weight model on
one consumer GPU carrying the entire foundation of a project like this is the
result worth taking away.

## The handoff workflow

This is the part worth paying attention to.

```mermaid
flowchart LR
    H["Human: direction, integration,<br/>build + playtest verification"]
    C["Claude / Claude Code<br/>frontier, hosted"]
    Q["Qwen 3.8 via pi<br/>open-weight, local RTX 5090"]
    D[(Shared artifacts:<br/>HANDOFF.md · findings.md · porting-notes.md)]

    H -->|scopes task, budget, files| C
    H -->|scopes task, budget, files| Q
    C <-->|reads / appends| D
    Q <-->|reads / appends| D
    C -.->|usage limit reached| Q
    Q -.->|hard structural bug| C
    C -->|patch + write-up| H
    Q -->|patch + write-up| H
```

Both agents worked against the **same three written artifacts**, which is what
let them substitute for each other:

1. **`HANDOFF.md`** — the current state, the immediate next task, and the
   environment gotchas. Originally a session-to-session note for one agent, it
   became the **interface between the two agents**: when Claude reached a
   usage limit mid-problem, the local model picked the task up from the
   HANDOFF state and continued; when the local model hit a bug that needed
   deeper structural reasoning, it wrote up where it was and Claude took over.
2. **`findings.md`** — the chronological finding log. 162 numbered entries,
   each a root cause with `file:line` evidence and the fix. New agents (either
   model) are pointed at the relevant entries before they start.
3. **`porting-notes.md`** — the append-only "recurring bug classes" file. The
   single highest-leverage artifact: it stopped both models from
   re-deriving the same class of N64→PC bug over and over.

Around these, the working rules (full detail in
[`../dev-process.md`](../dev-process.md)):

- **file-partitioned tasks** — each agent's task scoped to a disjoint set of
  files so patches never collided;
- **visible budgets** — every investigation task carried an explicit
  "N build→run cycles" limit and a defined fallback (revert probes, write up
  with a confidence rating);
- **the human owns verification** — building, running the game, capturing a
  frame, and judging it against N64 reference footage was never delegated.

## Assessment

Honest notes, for anyone weighing whether this transfers.

**Worked well**

- The **local model carried the groundwork phase** — build system, boot
  chain, OS shims, and the offline-converter architecture. None of it was
  rewritten later. A 27B open model on one consumer GPU was genuinely productive
  on this.
- **The written-artifact discipline made agent output compound.** An agent
  starting cold late in the project was more effective than one early on,
  because the accumulated notes were good. This is also what made the two
  models interchangeable on a given task.
- **Handoff on limit** turned Claude's usage cap from a hard stop into a
  slowdown — the local model kept the problem moving.
- Bounded behavioural bugs (a truncated pointer, a byte-swap off by one) are a
  good fit for an agent given a tight loop and a reference to diff against.

**Worked poorly / needed the human**

- **Anything requiring the running game.** Build, playtest, capture a frame,
  decide whether it looks right — that loop was the human's job throughout,
  and it was the bottleneck.
- **Non-deterministic bugs.** Frame-timing stalls and concurrent-build
  flakiness repeatedly fooled agents into "fixing" regressions that were not
  real. A number of findings are corrections of earlier findings.
- **Long structural bugs** (matrix handedness, a whole serialized-format
  spec) often ended in a "here is what I know, confidence medium" writeup
  rather than a fix, even with the budget raised.
- **The capability gap is a gradient, not a wall.** The local model was strong
  on well-scoped work and weaker when a bug needed several interacting facts
  held at once — those went to Sonnet 5, and the few that stalled Sonnet went
  to Opus 5. Each tier earned its place on the problems the tier below it
  couldn't close.

**Still outstanding** (parked below crash/level work): audio is not
implemented; several front-end 3D transforms and some text rendering are
wrong. See [`GRAPHICS-BACKLOG.md`](GRAPHICS-BACKLOG.md).
