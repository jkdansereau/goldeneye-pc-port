---
title: Security & fidelity status
description: What a release actually installs (no networking, no telemetry, no ROM or assets)
and how faithfully the port tracks the original N64 game's logic.
---

## Security & fidelity status

This page answers two questions plainly: *what does installing this put on
your machine*, and *how faithfully does the port actually reproduce the
original N64 game's logic*. Both were reviewed end-to-end on 2026-09-15 and
re-verified for the v0.3.0 release bundles on 2026-09-18 (bundle contents,
no-networking source check, and the asset converter). The no-networking source
check was repeated against the `release/v0.4.1` tree on 2026-09-30 (no socket,
HTTP or name-resolution calls in `src/` or `port/`; neither the engine nor the
bundled `SDL2.dll` imports a networking DLL; no registry calls in the port's
own code); the bundle contents were not re-checked then. The full engineering
record behind each item is in the project's finding log
([`docs/dev/findings.md`](dev/findings.md)).

## What a release actually installs

Each release (`goldeneye-pc-port-*-win64.zip` / `-linux-x86_64.tar.gz`)
contains, and only contains: the game engine executable, a fixed set of
standard runtime libraries (SDL2, zlib, and the MinGW/Linux runtime — no
third-party downloads), the one-time ROM-to-PC asset converter, a README,
and license texts. The build scripts hard-fail if a ROM image or an
oversized blob ends up in the bundle. **No ROM, no game assets (textures,
audio, models, levels, text) are ever shipped** — you supply your own
legally-owned copy.

At runtime, the engine:

- has **no networking of any kind** (no sockets, no HTTP, checked directly
  against the source),
- has **no telemetry, crash reporting, or analytics** of any kind,
- **never touches the Windows registry** and **never requests elevated
  permissions**,
- writes save data, config, and derived ROM assets only inside its own
  folder — never to `%APPDATA%`, `Program Files`, or any system location.

The one-time asset converter (`ge007-convert` / `prepare-assets.py`) — the
part of the bundle most likely to draw suspicion, since it's the thing that
reads your ROM and writes new files — has no network access at all,
verifies your ROM against a fixed table of known retail GoldenEye 007
hashes before touching it (an unrecognized file is rejected outright), and
writes only inside the bundle folder. Its full source ships right next to
the compiled version in every release if you'd like to read it yourself. The
engine starts it automatically on the first run, when the two derived asset
folders (`data/pcmodels-*`, `data/pccg-*`) are missing, passing it the ROM it
found and the output folder; after that first run it is not launched again.

See [`SECURITY.md`](../.github/SECURITY.md) for why the binaries are
unsigned and what that means in practice (a SmartScreen prompt on first
run, and why some antivirus engines flag the converter specifically).

## Building from source

Cloning and building pulls in only standard, well-known packages: MSYS2
`pacman` packages on Windows, `apt` packages on Linux — no `npm`, no
`cargo`, nothing downloaded at CMake configure time, no git submodules, and
no prebuilt binaries checked into the repository. The CI build freezes the release's asset-converter tool with `pyinstaller`,
which is pinned to an exact version, and every third-party GitHub Action the
workflows use is pinned to a full commit SHA (both from the release after
v0.4.0; earlier releases installed an unpinned `pyinstaller`). What
PyInstaller packages is entirely in-repo, stdlib-only Python, so a release is
reproducible from a diff of this repository.

One development-only backdoor exists in source builds: the `GE_DEBUG_UNLOCKALL`
environment variable (`src/game/file2.c`) pre-unlocks all cheat options at boot.
It is off unless explicitly set, changes no shipped behavior, and is documented
here so the env-var surface stays fully accounted for.

## Fidelity to the original N64 game

The port's non-negotiable rule is that decompiled game logic is never
changed — only genuine N64-hardware dependencies get a PC-side
replacement, in the `port/` layer. This was audited directly:

- **Region build macros** exactly mirror the original N64 Makefile's macro
  sets for all three regions — verified line-by-line.
- **The audio/library file classification** (which original files compile
  vs. get PC-shimmed) exactly matches the project's own ground-truth
  manifest — no silent gaps, no silent duplicates.
- **The one shipped game-logic-adjacent change** (a portal-culling
  float-precision fix, D271) is `#ifdef PORT`-gated, documented in the
  finding log, and moves *toward* matching N64 behavior, not away from it.
  Every other deviation from the decompiled source is one of the documented
  32→64-bit pointer-width ABI corrections described above.

**One issue, found and fixed before v0.3.0 shipped**: the port's
random-number generator (`port/src/random.c`) was documented as a bit-exact
port of the N64's PRNG, but wasn't — a shift-operation helper didn't match
the real MIPS64 instruction semantics it was meant to mirror. This was
independently confirmed by hand-tracing the assembly against the C port,
tracked as finding D284, and fixed before the release (verified clean
across a full level sweep — all 20 missions plus the ending sequence —
no crashes). It affected loot placement, AI
behavior variance, and replay-state determinism from the very first random
draw, in every release before v0.3.0. Because of the fix, RNG-derived
output — including the save-file CRC — changed, which meant existing save
files and any previously recorded replays no longer validate; v0.3.0's
release notes describe the automatic save migration that handles this. We'd
rather say this plainly than let a "verified bit-exact" claim stand
uncorrected, both when it was wrong and now that it's fixed.

Everything else checked (the documented ABI/pointer-width exceptions used
for the 32-to-64-bit transition, a sample of the finding log's own claimed
fixes against the actual current code) matched what's documented.

---

*This page is kept current alongside future releases. Its source review is a
manual, human-reviewed process today — see the project roadmap for plans to
partially automate the mechanical parts of it.*
