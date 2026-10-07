---
title: Security & fidelity status
description: What a release actually installs (opt-in online multiplayer only, no telemetry, no ROM or assets)
and how faithfully the port tracks the original N64 game's logic.
---

# Security & fidelity status

This page answers two questions plainly: *what does installing this put on
your machine*, and *how faithfully does the port actually reproduce the
original N64 game's logic*. Both were reviewed end-to-end on 2026-09-15 and
re-verified for the v0.3.0 release bundles on 2026-09-18 (bundle contents,
no-networking source check, and the asset converter). The full engineering
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

- opens **no network connection unless you use online multiplayer**
  ([`netplay.md`](netplay.md), added 2026-10-07; findings D409 / D410).
  - **Before you use it:** until you open the **Online** menu (F9, or the
    entries in the menus) or pass a `--net-*` option, no socket exists.
  - **Game traffic** is plain UDP to the other players, or to a host or
    server you choose. It carries your player name, lobby choices, chat and
    controller input.
  - **The online service:** a build configured with one
    (`GE007_ONLINE_SERVICE_URL`, or `[Net] Service`) also makes one HTTPS /
    WebSocket connection to it while the menu is open or you host or join
    through it. It sends your name, game build, and, while you host, your
    game's details and addresses. It is a Cloudflare Worker run by whoever
    deployed it (`tools_pc/netplay/cloudflare`). The game refuses any
    service address that is not `https://`, except this machine (D412).
    The service only passes on addresses players publish for themselves, so
    it cannot be used to aim traffic at others. The game also sends game
    traffic only to ordinary internet or LAN addresses.
  - **STUN:** one request to `stun.cloudflare.com` learns your public
    address. Nothing is ever sent to the developers.
  - **LAN games:** opening it broadcasts one small query on your local
    network.
  - No downloads, and no executable content over the network.
  - Every received packet is bounds-checked and every index range-checked.
    All text is forced to printable ASCII, and every number a peer sends is
    made finite and bounded before the game sees it (D412).
  - The network code is fuzzed under AddressSanitizer, against hostile hosts,
    hostile joiners and tampered packets (`tools_pc/netplay/netfuzz.c`).
    *It is new and has not yet had the independent end-to-end review the
    rest of this page describes,*
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
the compiled version in every release if you'd like to read it yourself.

See [`SECURITY.md`](../.github/SECURITY.md) for why the binaries are
unsigned and what that means in practice (a SmartScreen prompt on first
run, and why some antivirus engines flag the converter specifically).

## Building from source

Cloning and building pulls in only standard, well-known packages: MSYS2
`pacman` packages on Windows, `apt` packages on Linux — no `npm`, no
`cargo`, nothing downloaded at CMake configure time, no git submodules, and
no prebuilt binaries checked into the repository. **One real gap**: the CI
build installs `pyinstaller` from PyPI without pinning a version, which is
used to freeze the release's asset-converter tool. It's a widely-used
packaging tool, and what it packages is entirely in-repo, stdlib-only
Python — but an unpinned dependency means a future PyInstaller release
could change what ships without a corresponding diff in this repository.
Pinning it is a tracked follow-up.

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
- **Online multiplayer (D409)** adds one `#ifdef PORT` call in `src/boss.c`
  (a stage-load hook next to the existing D294 / D235 hooks). It returns
  immediately unless an online match is starting, and then only sets the
  state the game's own demo-replay system sets (random seeds, multiplayer
  setup, a RAM-only save slot) — the game logic itself is unchanged and
  simply sees four controllers.

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
