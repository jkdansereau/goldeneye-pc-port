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
process on Linux, only when `Game.CheckUpdates` is on); the converter's Python sources
(`prepare-assets.py` and its emit-script imports) import no network
module. The bundle contents were re-checked against the packaging
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

The one-time asset converter (`ge007-convert` / `prepare-assets.py`) — the
part of the bundle most likely to draw suspicion, since it's the thing that
reads your ROM and writes new files — has no network access at all,
verifies your ROM against a fixed table of known retail GoldenEye 007
hashes before touching it (an unrecognized file is rejected outright), and
writes only inside the bundle folder. Its full source ships right next to
the compiled version in every release if you'd like to read it yourself.
The engine starts it automatically on the first run, when the two derived
asset folders (`data/pcmodels-*`, `data/pccg-*`) are missing, passing it
the ROM it found and the output folder; after that first run it is not
launched again.

See [`SECURITY.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/.github/SECURITY.md) for why the binaries are
unsigned and what that means in practice (a SmartScreen prompt on first
run, and why some antivirus engines flag the converter specifically).

## Building from source

Cloning and building pulls in only standard, well-known packages: MSYS2
`pacman` packages on Windows, `apt` packages on Linux — no `npm`, no
`cargo`, nothing downloaded at CMake configure time, no git submodules, and
no prebuilt binaries checked into the repository. The CI build freezes the
release's asset-converter tool with `pyinstaller`, which is pinned to an
exact version, and every third-party GitHub Action the workflows use is
pinned to a full commit SHA (both from the release after v0.4.0; earlier
releases installed an unpinned `pyinstaller`). What PyInstaller packages
is in-repo, stdlib-only Python — a release is reproducible from this
repository alone.

One development-only backdoor exists in source builds: the `GE_DEBUG_UNLOCKALL`
environment variable (`src/game/file2.c`) pre-unlocks all cheat options at boot.
It is off unless explicitly set, changes no shipped behavior, and is documented
here so the env-var surface stays fully accounted for.
