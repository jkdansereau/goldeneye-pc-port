# Local-agent handoff tasks (Qwen 27B)

Two self-contained, low-risk tasks that are **disjoint from the active Jungle
`-level_37` G_MTX investigation** (that work is in `port/fast3d/gfx_pc.cpp`
around `gfx_sp_matrix` / `seg_addr` / `gfx_run_dl` — do NOT touch that file).

Source: `docs/BACKLOG.md` "Technical cleanup" section.

---

## Task A — doc only, zero code risk (do this first)

**Goal:** add two entries to `docs/PORT-LEARNINGS.md`.

1. New bug class **"D24-implications — host-scheduling nondeterminism / fake
   priority semantics"**: the pthread kernel does not enforce N64's 0–31
   thread priorities (`osYieldThread` == `Sleep(0)`), so thread interleavings
   impossible on real hardware can occur, and frame timing carries host
   jitter. **How to apply:** when a flaky timing bug appears (especially
   Phase 3 audio underruns), reach first for host thread priorities
   (`SetThreadPriority`: scheduler thread time-critical, tick thread normal)
   and the deferred `GE_DETERM` fixed-tick mode (§F D117) — NOT for kernel
   changes. Link `[[d117]]`-style to the D117 finding.
2. Watch item under the same class: **`osYieldThread` == `Sleep(0)`** is the
   one place the port fakes cooperative scheduling. If a level-sweep hot loop
   misbehaves under host load, inspect this shim first. Keep as-is until then.

Put both in section **E. Process / method notes** (or a new subsection if
that reads better). Match the file's existing terse style. No code changes.

**Verify:** file still renders as markdown; `git diff` is docs-only.

**On expiry:** commit whatever is written; partial is fine.

---

## Task B — mechanical dead-code removal, single file

**Goal:** remove the closed-out **D63** debug scaffolding from
**`port/src/libultra.c` ONLY** (leave `port/fast3d/gfx_pc.cpp`'s D63 code
alone — that file is in use by the Jungle work; a separate cleanup later).

**FILES YOU MAY TOUCH:** `port/src/libultra.c` only.

D63 is a closed finding; this code is dead weight in the hottest function in
the port. Remove, in `port/src/libultra.c`:

- the helper block around lines ~490–610: `d63WatchGunbarrelSlot`,
  `d63CheckSlot` (the "log only when the slot value changes" fn), the
  `D63Act` struct + `s_d63act` ring buffer + `D63_ACT_N`, `d63On`, `d63Tid`,
  `d63Act`, the watchdog thread fn + `d63WatchdogStart`, and the
  `D63 clobber-watch` block.
- every call site marked `/* TEMP D63 */`: `d63WatchdogStart()` (~261),
  `d63Act("send")` + `d63WatchGunbarrelSlot()` (~617–618),
  `d63Act("recv")` + `d63WatchGunbarrelSlot()` (~658–659),
  `d63Act(...)` (~1110), and the two `getenv("GE_D63")` gfx-task logging
  blocks (~1117–1147).

Grep `D63` and `d63` in the file afterwards — should be zero hits.

**CONSTRAINTS:** no behavior change; deletion only; do not touch any other
file; do not remove non-D63 code even if it looks unused.

**BUDGET:** <= 3 build cycles. `export PATH="/c/msys64/mingw64/bin:$PATH" &&
./build-pc.sh ntsc-final` must stay green (240/240 link).

**Verify:** build green; `grep -in d63 port/src/libultra.c` empty;
`./build-pc/ge007.x86_64.exe -level_09` still boots to frames (no crash in
first ~15 s).

**On expiry:** `git checkout port/src/libultra.c` (revert) and write up which
symbols were still entangled.

**REPORT:** list of removed symbols + line count delta + build result.
