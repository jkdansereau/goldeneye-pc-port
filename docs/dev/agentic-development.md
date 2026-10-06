---
title: Agentic Development
description: A case study in two AI coding agents porting a Nintendo 64 game to PC; the setup, timeline, handoff workflow, and an honest assessment of what worked.
---

## Agentic development: two AI coding agents porting GoldenEye 007

*How this port was actually built: a local open-weight model (Qwen 3.8 on a
single RTX 5090) and a hosted frontier model (Claude / Claude Code) handing
work back and forth through shared written notes, under one person's
part-time direction. The goal, the setup, the timeline, and an honest read on
what did and didn't work.*

> *Snapshot as of the v0.3.0 milestone (20 Sep 2026; findings through D321).
> v0.4.0 (28 Sep) extended the timeline with the options/input work -- the
> README's Background section carries the final project numbers.*

## Contents

- [Why this project exists](#why-this-project-exists)
- [The setup](#the-setup)
- [Timeline](#timeline)
- [By the numbers](#by-the-numbers); [Commit velocity](#commit-velocity) · [Who did what](#who-did-what)
- [The handoff workflow](#the-handoff-workflow)
- [Assessment](#assessment)

## Why this project exists

The playable port is real, but it is not the primary deliverable. The goal was
to **test how well coding agents hold up on a large, unfamiliar, low-level
codebase**; one with none of the properties that make web-app work easy for
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
and, the part that turned out most interesting, the two **handing work back
and forth** through shared written artifacts, directed by one human.

## The setup

| | |
|---|---|
| Local agent | `unsloth/Qwen3.8-27B-GGUF:UD-Q4_K_XL` on a single **NVIDIA RTX 5090**, driven mainly through the **[pi](https://github.com/earendil-works/pi)** coding agent. Unsloth Desktop (Unsloth's local model runtime, which can drive agents such as Claude Code) was also trialed but not used significantly. |
| Hosted agent | **Claude**, via **Claude Code**, on a Claude Pro subscription; mostly **Sonnet 5**, with **Opus 5** used as an escalation tier for the hardest problems and whenever there was subscription budget to spend on it |
| Human | one person: direction, work partitioning, integration, and every build / playtest / frame-capture the agents could not run |
| Base | fork of the [GoldenEye 007 decompilation](https://github.com/n64decomp/007) (years of prior work by Larry Ficken ("kholdfuzion") and contributors) |
| Reused | the [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark)'s `fast3d` software RSP; same Rare engine family |

The port is the GoldenEye-specific porting work **on top of** those two
existing bodies of work; it is not a from-scratch reimplementation of either.

In practice the models formed a **three-tier escalation** — the local model
handled well-scoped work, Sonnet 5 took what it stalled on, and Opus 5 the
rest; the same "escalate when stuck" move applied at every level.

## Timeline

All dates from this repository's own commit history (August–September 2026).

```text
# phase timeline (from the port's own commit dates)
Aug 16  day 0   fork + PC-port scaffolding
Aug 20  day 4   full compile + link (~230 TUs); boot to window
Aug 22  day 6   first rendered frames; offline asset-conversion pipeline
Aug 24  day 8   entire intro renders (logos -> gun-barrel -> cast)
Aug 27  day 11  Claude joins (handoff workflow begins)
Aug 29  day 13  21 solo levels load + render + unattended no-crash; SDL input layer
Aug 30  day 14  front-end flow (menu -> briefing -> start)
Aug 31  day 14  file-backed EEPROM saves
Sep 02        audio mixer (music + SFX)
Sep 14        Steam Deck hardening + F10 overlay QoL
Sep 18        final fix sweep (D309-D321) + release review
Sep 20  day 35  v0.3.0 released: full campaign at Agent difficulty, audio
                complete, bundles for Windows, Linux and Steam Deck
```

Roughly **two weeks**, one person part-time, took the decompilation from
"builds an N64 ROM" to "boots on desktop, renders every solo level, playable
through the front end into the early game". The rest of month one (29 Aug –
20 Sep) took it from there to a public release: the 21-level sweep to
full-campaign completion, the audio mixer, input and Steam Deck polish,
widescreen FOV scaling, and the v0.3.0 bundles — with a set of cosmetic
issues documented rather than outstanding.

## By the numbers

| | |
|---|---|
| Calendar time | 36 days (16 Aug – 20 Sep 2026), one person part-time |
| Commits on the port | ~835 |
| Root-caused bugs logged | `D1`–`D321` in [`findings.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/dev/findings.md), 199 tracked entries in the index (some labels later merged or withdrawn) |
| Handoff sessions | 190+ (`M-2` … `M-192`, and counting) |
| Game-source files given `#ifdef PORT` ABI edits | 71 files, ~385 blocks |
| New port-layer / tooling files | ~119 |
| Port layer | ~24,450 lines C/C++ (`port/`) |
| PC asset-conversion tooling | ~7,180 lines Python (`tools_pc/`) |
| Outcome | **v0.3.0 released**: full campaign playable end to end (all 21 missions playtested at Agent difficulty) at a steady 60 fps, full audio (music + SFX), file-backed saves; Windows, Linux and Steam Deck; known cosmetic defects documented in the finding log |

### Commit velocity

```text
# commits per week (week start)
8/16  ## 20
8/23  ########### 109
8/30  #################### 203
9/6   ############## 141
9/13  ###################### 220
9/20  # 6   (release day only)
      (each # ~ 10 commits)
```

Weekly totals (the final week is release day only). The step up from the
8/23 week onward is the collaborative phase in full swing, including the
parallel multi-agent "bursts"; the 9/13–9/19 week peaks on a single-day
84-commit burst (9/15) when the Steam Deck playtest-feedback batch landed
at once.

### Who did what

```text
# commits by agent (raw count, snapshot through early September)
Claude                    ################  161   (72%)
Local model (Qwen 3.8/pi) ######            62   (28%)
                          (each # ~ 10 commits)
```

(Phase A, the first 26 commits, to 24 Aug, was entirely the local model,
solo; from 27 Aug on the two agents worked in parallel. The raw tally was
not re-run for the final third of the project; treat the milestone-weighted
estimate below as the better number.)

| Milestone / workstream | Primary agent | Weight |
|---|---|---|
| PC build system, region macros, full compile + link of ~230 units | local model | large |
| Boot chain: ROM map, OS-shim layer (`libultra.c`), host threads, dual-mapped DRAM | local model | large |
| Software-RSP integration + replacement scheduler (`gesched.c`) | local model | medium |
| Offline asset-conversion architecture + first converters (`tools_pc/`) | local model | large |
| Intro rendering (logos → gun-barrel → cast) | local model | medium |
| Stage load unblocked (`D69`–`D87`) | Claude | medium |
| 21-level crash sweep; ~12 crash classes root-caused (`D88`–`D169`) | Claude | large |
| SDL input layer (keyboard / mouse / gamepad, mouse-look) | Claude | medium |
| Front-end flow (menu → briefing → start), EEPROM saves | Claude | medium |
| Parallel struct-layout / converter static audits | local model | medium |
| Continuing tasks after Claude hit a usage limit | local model | small |
| The hardest structural bugs (matrix handedness, format specs) | Claude | n/a |
| Every build, playtest, frame capture, integration, and direction | human | n/a |

**Estimated effort split.** The raw commit count above (~28% local / ~72%
Claude) undercounts the local model: Phase A landed the build system and boot
chain in relatively few, large commits, and the local model kept contributing
~15–20% of Phase B in parallel. Weighting by milestone difficulty rather than
raw commits; the foundation is a heavier third of the project than its commit
share suggests; the developer's estimate is roughly:

```text
# agent effort, milestone-weighted
Claude                    ############  60%
Local model (Qwen 3.8/pi) ########      40%
                          (each # ~ 5%)
```

The local model's ~40% is front-loaded and foundational (the build, the boot
chain, the RSP wiring, the converter architecture) plus continuous parallel
support; Claude's ~60% is the higher-volume Phase-B debugging, the two big
discrete features, and the hardest structural bugs. A 27B open-weight model on
one consumer GPU carrying the entire foundation of a project like this is the
result worth taking away.

## The handoff workflow

This is the part worth paying attention to.

```text
# the handoff workflow

    HUMAN
    direction, integration, build + playtest verification
      | scopes task, budget, files    | scopes task, budget, files
      v                               v
  CLAUDE (hosted)             QWEN 3.8 / pi (local, RTX 5090)
      | patch + write-up                | patch + write-up
      +--------------+   +--------------+
                     v   v
      SHARED ARTIFACTS (both agents read + append)
      HANDOFF.md  .  findings.md  .  porting-notes.md
```

Both agents worked against the **same three written artifacts**, which is what
let them substitute for each other:

1. **`HANDOFF.md`**; the current state, the immediate next task, and the
   environment gotchas. Originally a session-to-session note for one agent, it
   became the **interface between the two agents**: when Claude reached a
   usage limit mid-problem, the local model picked the task up from the
   HANDOFF state and continued; when the local model hit a bug that needed
   deeper structural reasoning, it wrote up where it was and Claude took over.
2. **`findings.md`**; the chronological finding log. 321 numbered `D`
   entries by v0.3.0 (199 tracked in the index; some labels merged or
   withdrawn), each a root cause with `file:line` evidence and the fix. New
   agents (either model) are pointed at the relevant entries before they
   start.
3. **`porting-notes.md`**; the append-only "recurring bug classes" file. The
   single highest-leverage artifact: it stopped both models from
   re-deriving the same class of N64→PC bug over and over.

Around these, the working rules (full detail in
[`../dev-process.md`](../dev-process.md)):

- **file-partitioned tasks**; each agent's task scoped to a disjoint set of
  files so patches never collided;
- **visible budgets**; every investigation task carried an explicit
  "N build→run cycles" limit and a defined fallback (revert probes, write up
  with a confidence rating);
- **the human owns verification**; building, running the game, capturing a
  frame, and judging it against N64 reference footage was never delegated.

## Assessment

Honest notes, for anyone weighing whether this transfers.

**Worked well**

- The **local model carried the groundwork phase**; build system, boot
  chain, OS shims, and the offline-converter architecture. None of it was
  rewritten later.
- **The written-artifact discipline made agent output compound.** An agent
  starting cold late in the project was more effective than one early on,
  because the accumulated notes were good. This is also what made the two
  models interchangeable on a given task.
- **Handoff on limit** turned Claude's usage cap from a hard stop into a
  slowdown; the local model kept the problem moving.
- Bounded behavioural bugs (a truncated pointer, a byte-swap off by one) are a
  good fit for an agent given a tight loop and a reference to diff against.

**Worked poorly / needed the human**

- **Anything requiring the running game.** Build, playtest, capture a frame,
  decide whether it looks right; that loop was the human's job throughout,
  and it was the bottleneck.
- **Non-deterministic bugs.** Frame-timing stalls and concurrent-build
  flakiness repeatedly fooled agents into "fixing" regressions that were not
  real. A number of findings are corrections of earlier findings.
- **Long structural bugs** (matrix handedness, a whole serialized-format
  spec) often ended in a "here is what I know, confidence medium" writeup
  rather than a fix, even with the budget raised.
- **The capability gap is a gradient, not a wall.** The local model was strong
  on well-scoped work and weaker when a bug needed several interacting facts
  held at once; those went to Sonnet 5, and the few that stalled Sonnet went
  to Opus 5. Each tier earned its place on the problems the tier below it
  couldn't close.

**Still outstanding:** see
[`docs/ROADMAP.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/ROADMAP.md),
the single tracker for open work.

## Since the two-agent phase

The two-agent setup above is the **initial phase**, snapshotted at v0.3.0
(the project has since shipped v0.4.x and is heading to v0.5.0; the model
and harness record below is current through that point).

Since then the emphasis has shifted from "the two models that built it" to
**testing a lot of new models and harnesses**, to find what's fastest and
cheapest per task. The long-time local workhorse stayed **Unsloth's Qwen 3.8 27B**;
in the final week that broadened into a menu, and the **current standard is
NInfer + Strata**. On the hosted side, the Claude orchestrator stepped up from
**Sonnet 5 / Opus 5** to the **5.5 tier** (Sonnet 5.5 / Opus 5.5, out in the
last few weeks), which has driven the most recent work. The local menu:

- **Qwen 3.8 27B** ([Unsloth](https://huggingface.co/unsloth/Qwen3.8-27B-GGUF),
  `UD-Q4_K_XL`) — the primary local workhorse for most of the port's life, doing
  nearly all of the local groundwork on its own. A larger-context
  [`ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF`](https://huggingface.co/ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF)
  (`IQ3_S`) was also briefly trialed for bigger-context workloads, then shelved
  (little use since v0.2.0).
- **NInfer (Windows)**
  ([natpate/ninfer-windows](https://github.com/natpate/ninfer-windows), v0.9.0,
  CUDA 13.1 — a standalone Windows port of the NInfer engine, with MTP /
  DFlash2 speculative decoding) — ran **Qwen 3.8 27B** in base, NVFP4 and
  NVFP4-full artifacts; trialed in the final week, now half of the standard
  and the fast day-to-day local driver.
- **Qwen 3.8 Flash-Next** via
  [Strata](https://github.com/Niko1221/Strata) (Niko1221's MIT-licensed
  C++/CUDA/HIP engine) — a 125B mixture-of-experts model that normally needs a
  server, run **locally on a single gaming GPU** (IQ1_M, 1-bit) for the
  biggest-context coding passes. The other half of the standard.
- **[claude-code-delegate-local](https://github.com/fegone/claude-code-delegate-local)**
  — an MCP server that let the Claude Code orchestrator and the local models
  talk, delegating Claude's subagents to the local engines. A nice tool, but
  **mostly deprecated now**: hand-offs between agents are done by hand for now,
  pending a better parallel-agent handoff system that drops the MCP overhead.
- **GPT-6 (Sol)** via [OpenRouter](https://openrouter.ai) — a frontier hosted
  model, **trialed** to see how it held up (not the hardest-work tool). Its
  code output is still owed a code-quality audit in this doc.

The practical effect: the project stopped depending on any one model or its
usage limits. The local GPU is now the default path for high-volume work, with
hosted frontier models reserved for the problems the local models can't close.

**Measurement owed.** Which of these was best on a given task — and each
model's rough cost-to-performance ratio — has not yet been measured properly;
attributing individual commits to the model that produced them is still a
manual pass. That includes a **code-quality audit of the GPT-6 (Sol) trial**.
All of it is pending and will be added here when it lands.

A concrete reason the commit-date route is unreliable: the `D350` commit
(`1303c0eb`, "expose watch-backed Bond settings in PC options and F10")
carries a **27 Sep** timestamp on both its author and committer dates, but
the session notes place that work on **4 Oct**. Because the two git dates
agree, this is **not** a rebase artifact (a rebase would have moved the
committer date while preserving the author date); it is a genuine divergence
between the recorded commit time and the working session, and resolving it
takes the session log, not the commit date. This is the class of edge case
the attribution pass has to walk by hand.
