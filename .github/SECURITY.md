# Security Policy

This project is a non-commercial, fan-made research port. It has no server
component and ships no binaries. The realistic security surface is:

- the PC port executable parsing your own ROM and asset files at load time
  (a malformed ROM/asset could in principle crash it or worse), and
- the build/extraction scripts and CI workflow.

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

Only the tip of the default branch is supported. There are no releases yet.
