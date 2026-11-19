---
title: Security status
description: What a GoldenEye PC port release installs and doesn't: no networking unless you opt in to the update check, no telemetry, no ROM, no system writes — re-verified against the v0.5.0 source.
date: '2026-10-05'
modified_time: '2026-10-05'
---

## Security status

This page answers one question plainly: *what does installing this put on
your machine*? The review was done
end-to-end on 2026-09-15 and re-verified for the v0.3.0 release bundles on
2026-09-18 (bundle contents, no-networking source check, and the asset
converter). The no-networking source check was repeated against the
`release/v0.4.1` tree on 2026-09-30 (no socket, HTTP or name-resolution
calls in `src/` or `port/`; neither the engine nor the bundled `SDL2.dll`
imports a networking DLL; no registry calls in the port's own code); the
bundle contents were not re-checked then.

**v0.5.0 update pass, 2026-10-05:** the source checks were re-run against
the current tree — no socket/network includes, no network symbols and no
HTTP string literals in the compiled `src/` and `port/` sets; no registry
or library-loading calls in either (since D551 the one exception is the
opt-in update check: a System32 `winhttp.dll` load on Windows or a `curl`
process on Linux, only when `Game.CheckUpdates` is on); the ROM converter (`port/convert/`, C, compiled into the engine) uses
no network or process-spawning calls. The bundle contents were re-checked against the packaging
manifests (`tools_pc/bundle-win.sh` / `bundle-linux.sh` — the scripts CI
runs): an explicit allowlist of engine, runtime DLLs, SDL2, licenses and
converter tools, with a hard-fail guard against a ROM image or oversized
blob sneaking in. The v0.5.0 artifacts themselves were not re-unpacked
locally; they are built by the same scripts. The full engineering record
behind each item is in the project's finding log
([`docs/dev/findings.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/dev/findings.md)).

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

- has **no networking unless you turn on the opt-in update check**
  (`Game.CheckUpdates`, off by default): then it makes one HTTPS request to
  GitHub's releases API per launch (a fixed User-Agent; GitHub sees the
  request and your IP, as with any web request) and never downloads or
  installs anything. On Windows it loads the system `winhttp.dll` from
  System32 at runtime only in that case (it is not in the import table); on
  Linux/Steam Deck it runs the system `curl`. With the setting off there
  are no sockets and no HTTP (D551; checked against the source and run live
  on Windows, Linux and Steam Deck),
- has **no telemetry, crash reporting, or analytics** of any kind,
- **never touches the Windows registry** and **never requests elevated
  permissions**,
- writes save data, config, and derived ROM assets only inside its own
  folder — never to `%APPDATA%`, `Program Files`, or any system location.

The one-time ROM conversion runs inside the game (`port/convert/`, called by
`port/src/romconvert.c`); a standalone `ge007-convert` command-line build of the
same code exists for development. It has no network access, reads only your
ROM (already rejected earlier by the engine's header check unless it is a
retail GoldenEye 007 image; the region is taken from the header's country
byte) and writes only inside the game folder (`data/pcmodels-*`,
`data/pccg-*`). It runs when those two folders are missing and is not run again
after that.

See [`SECURITY.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/.github/SECURITY.md) for why the binaries are
unsigned and what that means in practice (a SmartScreen prompt on first
run).

## Building from source

Cloning and building pulls in only standard, well-known packages: MSYS2
`pacman` packages on Windows, `apt` packages on Linux — no `npm`, no
`cargo`, nothing downloaded at CMake configure time, no git submodules, and
no prebuilt binaries checked into the repository. Every third-party GitHub Action the workflows use is pinned to a full
commit SHA. A release is reproducible from this repository alone.

One development-only backdoor exists in source builds: the `GE_DEBUG_UNLOCKALL`
environment variable (`src/game/file2.c`) pre-unlocks all cheat options at boot.
It is off unless explicitly set, changes no shipped behavior, and is documented
here so the env-var surface stays fully accounted for.
