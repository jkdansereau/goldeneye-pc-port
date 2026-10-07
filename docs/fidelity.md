---
title: Fidelity status
description: How faithfully the port reproduces the N64 game: unmodified decompiled logic, Makefile-mirrored region macros, one game-logic-adjacent change, and PRNG notes.
date: '2026-10-05'
modified_time: '2026-10-05'
---

## Fidelity status

This page answers one question plainly: *how faithfully does the port
actually reproduce the original N64 game's logic*?
The audit was run end-to-end on 2026-09-15 and re-verified for the v0.3.0
release bundles on 2026-09-18.

**v0.5.0 update pass, 2026-10-05:** two of the checks were re-run
directly against the current tree — the region build macros were
compared line-by-line against the original N64 Makefile's macro sets
(all three regions match exactly), and the library file classification
(which original files compile versus get PC-shimmed) was re-checked
against the project's own ground-truth manifest (the CMake build
compiles the libultra and libultrare audio sets, the gu matrix helpers,
and `io/vitbl.c` only — no silent gaps, no silent duplicates). The
full game-logic audit was not re-run in this pass; recent `src/game`
edits on the release branch are finding-gated (for example the D519
front-end options work, and a model-c scripted-camera clamp removal
that returned those files to pure decompilation). The full engineering
record behind each item is in the project's finding log
([`docs/dev/findings.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/dev/findings.md)).

## The rule and what was checked

The port's non-negotiable rule is that decompiled game logic is never
changed — only genuine N64-hardware dependencies get a PC-side
replacement, in the `port/` layer. This was audited directly:

- **Region build macros** exactly mirror the original N64 Makefile's macro
  sets for all three regions — verified line-by-line (re-run 2026-10-05).
- **The audio/library file classification** (which original files compile
  vs. get PC-shimmed) exactly matches the project's own ground-truth
  manifest — no silent gaps, no silent duplicates (re-run 2026-10-05).
- **The one shipped game-logic-adjacent change** (a portal-culling
  float-precision fix, D271) is `#ifdef PORT`-gated, documented in the
  finding log, and moves *toward* matching N64 behavior, not away from it.
  Every other deviation from the decompiled source is one of the documented
  32→64-bit pointer-width ABI corrections described above.

## One issue, found and fixed before v0.3.0 shipped

The port's random-number generator (`port/src/random.c`) was documented
as a bit-exact port of the N64's PRNG, but wasn't — a shift-operation
helper didn't match the real MIPS64 instruction semantics it was meant to
mirror. This was independently confirmed by hand-tracing the assembly
against the C port, tracked as finding D284, and fixed before the release
(verified clean across a full level sweep — all 20 missions plus the
ending sequence — no crashes). It affected loot placement, AI behavior
variance, and replay-state determinism from the very first random draw,
in every release before v0.3.0. Because of the fix, RNG-derived output —
including the save-file CRC — changed, which meant existing save files
and any previously recorded replays no longer validate; v0.3.0's release
notes describe the automatic save migration that handles this. We'd
rather say this plainly than let a "verified bit-exact" claim stand
uncorrected, both when it was wrong and now that it's fixed.

Everything else checked (the documented ABI/pointer-width exceptions used
for the 32-to-64-bit transition, a sample of the finding log's own claimed
fixes against the actual current code) matched what's documented.

---

*This page is kept current alongside future releases. Its source review is a
manual, human-reviewed process today — see the project roadmap for plans to
partially automate the mechanical parts of it.*
