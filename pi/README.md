# pi/ — agent-development tooling (local, not part of the game build)

This directory is a [pi package](https://pi.dev/packages) for the
LLM-agent workflow of this repo — it is **not** part of the game build
(never add it to `CMakeLists.txt`). It exists because the 2026-10-09
session forensics on the #150 cull hunt (see `docs/dev-process.md` §8
and the `Tooling workstream` row in `docs/ROADMAP.md` §7) showed the
loss mode is model + discipline, not retrieval.

**Install (per machine):**

```sh
pi install ./pi                      # local package: agents + compaction-ledger
pi install npm:pi-subagents@0.76.1   # the subagent runtime (pinned; audit: docs/dev/notes/)
```

`.pi/` is gitignored, so the install declarations land in machine-local
settings; the reviewable source of truth is this directory, committed.

**What it provides**

- `agents/` — four model-pinned subagent roles (via `pi-subagents`), all on the
  standing **ninfer** backend (only one of ninfer/strata runs at a time;
  children inherit the running backend, so pins must match it):
  - `build-runner` — `./build-pc.sh` + capture + golden sweep, reports
    pass/fail. `ninfer-windows/qwen3.8-27b-nvfp4full` (fast 4-bit).
  - `pairing` — D579T-style log pairing (E-rows vs C-rows by x/y/w),
    names candidate cull rules, read-only. `model: inherit` — the
    reasoning step, so it follows the parent session's tier; run the
    parent on a strong model when doing D579 diagnosis.
  - `sweep-watcher` — watches long golden-sweep runs, triages new vs
    regressed frames. `ninfer-windows/qwen3.8-27b-nvfp4full`.
  - `doc-sync` — ROADMAP rows, findings-index.csv, findings.md §F index
    after a verified change. `ninfer-windows/qwen3.8-27b-nvfp4full`.

  Note: NInfer serves one model per launcher — `ninfer-coding.bat`
  (4-bit `qwen3.8-27b-nvfp4full`) is the standing default; the
  `ninfer-quality.bat` launcher serves the ≈6-bit groupwise-int
  `qwen3.8-27b` as the strong local tier (a launcher restart + new pi
  session, not a backend switch). If a better/faster artifact appears in
  the closed NInfer set, promote the labor-role pins to it.

  Model pins live in each agent file's frontmatter (`model:
  provider/model`) — keep them here, not in untracked settings, so the
  pins are reviewable, and keep them on the standing backend (ninfer).
  Planning/diagnosis stays on the parent session's strong model
  (`openrouter/deepseek/deepseek-v4-pro`, or `strata-coder-hard` with the
  local backend switched); only mechanical labor goes to these agents.

- `extensions/compaction-ledger/` — overrides the compaction summary
  with an evidence-ledger prompt (every live hypothesis must be tagged
  `refuted` / `unvalidated` / `validated (<artifact>)`; "diagnosed"
  without a maintainer A/B or golden sweep is forbidden) and, when
  `GE_COMPACT_MODEL` is set (e.g. `openrouter/deepseek/deepseek-v4-pro` or
  `strata/strata-coder-hard`), summarizes with that
  stronger model instead of the session model. The 01a11ec6 session
  lost the evening because a mid-session compaction promoted an
  unvalidated hypothesis to "diagnosed" and the post-compaction turns
  built on it.

Rules for anything added here: machine paths are forbidden (tree
hygiene), third-party deps are forbidden without a ROADMAP §7 approval
row (like `pi-subagents`), and `src/game` is still read-only ground
truth for every agent.
