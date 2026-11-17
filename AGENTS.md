# AGENTS.md — GoldenEye 007 PC Port

## What this is

- `n64decomp/007`: WIP decompilation of GoldenEye 007 (N64), byte-matches US/EU/JP ROMs.
- Active work: **PC port** modelled on the Perfect Dark PC port (same Rare "Indy" engine family).
- **Reference docs** (reading order: `CLAUDE.md`):
  - `docs/internals.md`: architecture, GE-specific RSP deltas, phased plan (§1–§10).
  - `docs/dev/findings.md`: the `Dxx` finding log. **Look findings up in `docs/dev/findings-index.csv` (regenerate with `tools_pc/gen_findings_index.py`), then read only that `## Dxx` entry, using grep or offset/limit for large ones. Never linear-read.**
  - `docs/porting-notes.md`: the recurring N64→PC bug classes. Skim the headers and read what is relevant.
- **Open work / known issues:** `docs/ROADMAP.md`, the single tracker. Add new items there; never start another backlog file.
- **Current status:** the README "Status" section. Current task and environment: `docs/HANDOFF.md` (rolling and local; may be absent).
- **Dispatching subagents?** `docs/dev-process.md` (budgets, file partitioning, brief template). Every investigation subagent reads `docs/porting-notes.md` first and appends to it.
- **Site pages** (`docs/index.md` and the other pure site pages) are a presentation layer that may go stale. Never use them as work information. Rules and the page list: `docs/dev/site-pages.md`.

## Non-negotiables

1. **N64 build untouched.** `Makefile`, `tools/`, `rsp/`, `ld/` belong to the N64 build. Never modify them for the PC port.
2. **Game logic is unmodified.** All N64 hardware dependencies are satisfied in `port/`. **Diagnosis is never restricted, only the fix.** The one exception that needs no sign-off is 32→64-bit ABI/layout fixes (pointer width, struct layout, raw-offset aliases into widened unions). They must be mechanical and semantics-preserving, each recorded in findings §F and tagged to `docs/porting-notes.md` §A1. A genuine behaviour difference needs the rule-2 sign-off (`docs/dev-process.md` §7) before any `src/game` edit. Full rule and the three buckets: `docs/dev-process.md` §Non-negotiable #2 in full.
3. **Region macros mirror the Makefile.** `CMakeLists.txt` `REGION_DEFS` must match the N64 Makefile's per-region macro set exactly (finding A1).
4. **`src/libultrare/Makefile.libultrare` is ground truth** for original-vs-Rare libultra files (finding B3). The PC build compiles `libultra/audio`, `libultrare/audio`, `libultra/gu` and `libultrare/io/vitbl.c` only. Every other `io/` and `os/` file is excluded and shimmed in `port/src/libultra.c`.
5. **`rsp/graphics/gmain.s` is the RSP ground truth** for the GBI commands GE emits and its custom CC/RM modes. `port/fast3d/` replaces it on PC.

## Tree hygiene & release gate

The repo is public, MIT-licensed and forked, so these apply to anything tracked or pushed:

1. **No local machine paths or usernames.** Use the placeholders `<repo>`, `<repos>`, `<games>`, `<videos>`, `<python>`, `<temp>` (legend at the top of `docs/dev/HANDOFF-ARCHIVE.md`). Tools take paths as arguments and never default to a machine-specific one.
2. **No contributor emails in prose.** Credit by name plus PR/issue number. Commit trailers use noreply addresses only.
3. **Every vendored third-party component is declared in `NOTICE`** with its license and holder. GPL sources ship their license text in-tree. Never relicense inherited code or the repo.
4. **Screenshots are game content.** Keep repo imagery documentary and small. Never commit bulk ROM-derived captures (the capture dirs are gitignored). The only bulk imagery is `tools_pc/golden/**/*.png`, which `verify.sh` consumes.
5. **Batch, don't spam.** One commit per session for approved doc/tree fixes; no per-item branches.
6. **Releases, squashes and history rebuilds** (PII and legal reports, contributor record merges, force-push backups): `docs/dev/RELEASE-GATE.md` §Release and history rules.

## Critical files

| File | Role |
|---|---|
| `CMakeLists.txt` | PC build (parallel to the N64 Makefile): source list and `REGION_DEFS`. |
| `port/src/` | Shims: `libultra.c` (OS API, scheduler leaf calls, runs the software RSP inline; the real `src/sched.c` is compiled), `n64stubs.c`, `random.c`, `ucode.c`, `main.c`, `video.c`, … |
| `port/fast3d/` | Software RSP (adapted from the PD port). |
| `rsp/graphics/gmain.s` | GE's RSP ucode: ground truth for GBI/CC/RM. |
| `reference/mouse-injector/README.md` | Stub (the GPL source was removed 2026-09-19). The mouse-aim model is in `port/src/input.c`. |
| A local **Perfect Dark PC port** checkout ([fgsfdsfgs/perfect_dark](https://github.com/fgsfdsfgs/perfect_dark)) | Standing reference whenever a work item has a PD analogue. Port-layer files only; same family does not mean the same format, so validate per field. Audit: `docs/internals.md` §2.4. |

## Build

All three targets build in Docker. No ROM is needed to build: the ROM-asset symbols are pre-generated and the game reads the user's ROM at run time.

```sh
docker compose build                    # once: Dockerfile.build (devkitA64 + Linux host libs)
docker compose run --rm switch          # -> build-switch/ge007.aarch64.nro (what CI ships)
docker compose run --rm linux           # -> build-linux/ge007.x86_64
docker compose build windows            # once: Dockerfile.windows (MinGW-w64)
docker compose run --rm windows         # -> build-win/ge007.x86_64.exe + DLLs
```

The Switch build is the reference: `.github/workflows/switch.yml` builds it in the same pinned devkitA64 image and is the only workflow that runs on this fork. Linux and Windows check that the PC port still compiles. SD-card layout, nxlink test builds and release packaging: `docs/building.md` §Nintendo Switch.

A native Windows build (`./build-pc.sh ntsc-final` from the MSYS2 MINGW64 shell, upstream's reference) still works. Its three recurring failures, to check in this order:

1. **`Cannot create temporary file in C:\Windows\`** at link: TMP is lost in a non-login shell. `build-pc.sh` re-runs itself under PowerShell to fix this. By hand, run cmake/ninja from cmd or PowerShell with `C:\msys64\mingw64\bin` on PATH.
2. **`cannot open output file ge007.x86_64.exe: Permission denied`**: the game is running. Never kill the user's game process to make a link succeed.
3. **`cc1.exe: ... libmpfr-6.dll`**: `C:\msys64\mingw64\bin` is missing from PATH.

Details and the history of the re-run fixes (D515 and earlier): `docs/building.md` §Windows build environment troubleshooting.

## Verification ritual (after any build-affecting change)

Run `/linkcheck`. Every symbol the compiled set references (`CMakeLists.txt` `SRC_*` lists) is defined exactly once, in the compiled set or in `port/`. Symbols from excluded files (`libultra/io|os`, `libultrare/io|os` except `vitbl.c`, `sched.c`, `rmon.c`, `vi.c`, `src/*.s`) come from `port/src/libultra.c`, `n64stubs.c`, `random.c` or `ucode.c`. No duplicates (watch `sp_*` stacks, `rmon*`, `os*` shims, segment markers). Every touched file parses; `docker compose run --rm switch` (plus `linux` / `windows` when shared PC code changed) is the final word. Record new findings in `docs/dev/findings.md` under the next free `Dxx` label and add it to the §F index.

## Phase status

Not kept here. Open work: `docs/ROADMAP.md`. Release state: the README "Status" section.
