# Security Policy

This project is a non-commercial, fan-made research port. It has no server
component, no networking of any kind, and no telemetry. Each release ships
prebuilt Windows and Linux binaries, but never a ROM or any game asset — you
supply those yourself from a copy you legally own. The realistic security
surface is:

- the PC port executable parsing your own ROM and asset files at load time
  (a malformed ROM/asset could in principle crash it or worse), and
- the build/extraction scripts and CI workflow.

A full walkthrough of what a release actually installs on your machine, and
what a from-source build pulls in, is in
[`docs/security-and-fidelity-status.md`](../docs/security-and-fidelity-status.md).

### Why the binaries are unsigned

The Windows `.exe` and the Linux build are **not code-signed** — this is a
free, single-developer, non-commercial hobby project with no code-signing
budget, not a sign of anything wrong with the binary. Two things you may
run into as a result:

- **Windows SmartScreen** may show "Windows protected your PC" on first
  run. Click "More info" → "Run anyway."
- Some antivirus engines flag `ge007-convert.exe` (the one-time ROM-asset
  converter bundled in the release) specifically, because it's packaged
  with PyInstaller's `--onefile` mode — a well-known trigger for heuristic
  AV engines industry-wide, unrelated to what this project's code actually
  does. The game launches this converter itself on the first run, when the
  derived asset folders are missing, so an AV prompt can appear at that
  moment rather than when you unpack the release. If you want to verify what
  it does, its full source is `prepare-assets.py`, shipped right next to it in
  the same bundle folder. The build pins the PyInstaller version and the CI
  actions it uses (by commit SHA), so what ships is reproducible from this
  repository.

## Reporting a vulnerability

Please **do not** open a public issue for a security problem.

Use GitHub's private vulnerability reporting:
**Security → Report a vulnerability** on this repository
(<https://github.com/jkdansereau/goldeneye-pc-port/security/advisories/new>).

If that is unavailable, email the maintainer at the address on their GitHub
profile with `SECURITY` in the subject.

Please include:

- affected version / commit hash (from `git rev-parse HEAD`),
- OS and how you built (region, `IDO_RECOMP`, MSYS2 vs WSL, …),
- a minimal reproduction, and
- the crash log (`ge007.crash.log`) or a stack trace if you have one.

## Scope

In scope: memory-safety bugs in the `port/` layer and PC-port tooling,
issues in the CI workflow or build scripts, and dependency problems we can
act on.

Out of scope: bugs inherited unchanged from the upstream
[GoldenEye 007 decompilation](https://github.com/n64decomp/007) that are not
made worse by the port (report those upstream), missing-asset or wrong-ROM
errors, and anything requiring a ROM or assets we do not distribute.

## Supported versions

Only the tip of the default branch and the
[latest release](https://github.com/jkdansereau/goldeneye-pc-port/releases/latest)
are supported.
