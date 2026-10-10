# AGENTS.md — GoldenEye 007 PC Port

## What this is

- `n64decomp/007`: WIP decompilation of GoldenEye 007 (N64), byte-matches US/EU/JP ROMs.
- Active work: **PC port** modelled on the Perfect Dark PC port (same Rare "Indy" engine family).
- **Reference docs:** `docs/internals.md` — architecture, GE-specific RSP deltas, phased plan (§1–§10). `docs/dev/findings.md` — the `Dxx` finding log (§F + §H). **Look up findings via `docs/dev/findings-index.csv` (label, one-liner, status; regenerate with `tools_pc/gen_findings_index.py`), then read only the specific `## Dxx` entry (multi-pass labels like D202/D176(a) have large sections — grep within the section or read with offset/limit, don't slurp it whole). Never linear-read.** `docs/porting-notes.md` — the recurring N64→PC bug classes (dense; skim the headers, read what's relevant).
- **Open work / known issues:** `docs/ROADMAP.md` — the single tracker; add new items there, never start a new backlog/roadmap file.
- **Current status:** the README "Status" section (`docs/dev/LEVEL-STATUS.md` is a historical per-level sweep record). Current task + environment: `docs/HANDOFF.md` (a rolling local working file — may be absent in a fresh clone; fall back to the README "Status" section).
- **Dispatching subagents?** `docs/dev-process.md` — task budgets/deadlines, file partitioning, pre-flight, the standard brief template. Every investigation subagent reads `docs/porting-notes.md` first and appends to it.

## Public site pages (`docs/`) — presentation layer, not a source of truth

The repo's homepage and sub-pages are published by GitHub Pages; the local
preview is `node scratch/build_preview.mjs site` → `scratch/_site/` (served
with `node scratch/serve_preview.mjs 8777`). The site is a **presentation
layer over the reference docs, kept in a separate area, and allowed to go
stale** — never let it poison a work item:

- **Pure site pages** — `docs/index.md`, `documentation.md`,
  `the-software-rsp.md`, `the-asset-pipeline.md`, `framerate-and-pacing.md`,
  `input-and-aim.md`, `saves.md`, `security.md`, `fidelity.md` —
  summarise the ROADMAP / README / finding log / release notes. **Do not
  consult them as work information, cross-check against them, or treat a
  mismatch with `docs/ROADMAP.md` or the finding log as a bug** (that is
  the expected, independent-staleness case). Update them only when the task
  says so, and source every fact from the ROADMAP / README / finding log —
  never the other way round. The footer "last updated" date in
  `docs/_layouts/default.html` is the same class of site-only detail.
- **Shared files** — `docs/internals.md`, `porting-notes.md`,
  `building.md`, `dev-process.md`, `dev/agentic-development.md` are
  authoritative working docs that the site publishes verbatim; edit them
  normally as part of dev work and the site picks up the change for free.

Keep the split intact: no working notes migrate into pure site pages, and
never edit a shared doc merely to make the site read better — if the site is
stale, fix the site page (or the fact upstream, as its own task).

## Non-negotiables

1. **N64 build untouched.** `Makefile`, `tools/`, `rsp/`, `ld/` belong to the N64 build. Never modify them for the PC port.
2. **Game logic is unmodified.** The decomp's control flow and behavior are ground truth for 1:1 fidelity — never change them. All N64 *hardware* dependencies are satisfied by the `port/` layer; if a game file seems to need a behavioral change, stop and check the exception below before assuming the fix belongs in `port/`. **Diagnosis is never restricted — only the fix.** Trace a bug as deep into `src/game` state as the evidence leads, and name the exact struct/field/logic responsible, before deciding which of three buckets it falls in (full framing: `docs/dev-process.md`). **Narrow exception (ABI/layout only):** the 32→64-bit pointer-width transition forces a small class of mechanical, semantics-preserving edits that cannot be isolated in `port/` — **any struct-layout or pointer-width-driven misread caused by 32→64-bit widening**, whether in a ROM-serialized record (a struct with a 32-bit-pointer field misaligns when read as 64-bit) or a **live runtime struct/union** (a raw-byte offset alias into a union arm whose true field shifted because an earlier member in the same union widened — the D209/D210/D255 pattern; see `docs/porting-notes.md` §A1 for the full catalogue and its diagnostic tells). These follow the PD ground-truth pattern (store the embedded address as `u32` and cast to a real pointer at the use site; or read the correctly-named/typed field instead of a raw-offset/mistyped alias), change no logic or behavior, and are each documented in `docs/dev/findings.md` §F/D3x, cross-tagged to §A1 where that pattern applies. **A genuine behavioral difference** — the decomp's byte-identical code, given verified-correct inputs, still diverging from real N64 behavior — is not covered by this exception and requires the rule-2 sign-off procedure (`docs/dev-process.md`) before any `src/game` edit. No other game-code edits are permitted.
3. **Region macros mirror the Makefile.** `CMakeLists.txt` `REGION_DEFS` must match the N64 Makefile's per-region macro set exactly (finding A1). Divergence = silent branch divergence + link failures.
4. **`src/libultrare/Makefile.libultrare` is ground truth** for original-vs-Rare libultra files (finding B3). The PC build compiles: `libultra/audio`, `libultrare/audio` (drvrNew/env/reverb), `libultra/gu`, and `libultrare/io/vitbl.c` only. All other `io/` + `os/` files are excluded and shimmed in `port/src/libultra.c`.
5. **`rsp/graphics/gmain.s` is the RSP ground truth** — the authoritative reference for which GBI commands GE emits (modified fast3d, 1545 lines). We do not run it on PC; `port/fast3d/` replaces it. Use it to validate the software RSP's command decoding and the custom CC/RM modes.

## Tree hygiene & release gate

Standing rules for anything that lands in the tracked tree or in a push. These
exist because the repo is public, MIT-licensed, forked, and its history is
mostly agent-authored.

1. **No local machine paths.** Tracked files carry no local absolute paths and
   no usernames. Use the placeholder convention: `<repo>` = this working
   directory, `<repos>` = the local source-repos root, `<games>` / `<videos>` =
   local media roots, `<python>` = the local CPython install, `<temp>` = a
   writable native temp dir (legend at the top of
   `docs/dev/HANDOFF-ARCHIVE.md`). Tools must not default to a machine-specific
   path — take the path as an argument and error if it is missing.
2. **No contributor emails in prose.** Credit people by name plus the PR/issue
   that carries the change ("co-authored with X (#123)"), never an inline
   `<email>` trailer inside findings text or docs. Commit-trailer emails must
   be noreply forms, never a contributor's real address.
3. **Every vendored third-party component is declared in `NOTICE`** with its
   license and holder — the "Third-party code vendored into the port layer" and
   "Vendored build tooling" lists. GPL sources must ship their license text
   in-tree (`tools/gzipsrc/COPYING`). MIT covers only our original work; never
   relicense inherited code and never re-license the repo (forks depend on the
   MIT `LICENSE` staying put).
4. **Screenshots are game content.** A framebuffer capture is a derivative work
   of Nintendo/MGM art regardless of which engine drew it. Keep repo imagery
   documentary and small (bug shots, previews — downscaled), and never commit
   bulk ROM-derived captures: capture output dirs are gitignored
   (`tools_pc/sweep-captures/cap-*/`, `tools_pc/sweep-captures/**/*.ppm`); the
   only bulk imagery in the tree is `tools_pc/golden/**/*.png`
   (63 frames × 3 platforms, 189 total, nested per level at
   `tools_pc/golden/<level>/{win,linux,deck}/`; the `deck` set added
   2026-10-05 with maintainer approval), which exists because `verify.sh`
   consumes it. Downloads ship no game content at all.
5. **Batch, don't spam.** Accumulate approved doc/tree fixes across a session
   into ONE commit; no per-item branches.
6. **Release gate.** Before pushing a release: the PII scrub and legal/copyright
   review reports (gitignored, `docs/dev/notes/`) must be run and clean, and the
   README Legal section must name every rights holder the game's trademarks
   implicate. Rewriting pushed history (squash) needs explicit maintainer
   consent, and any such report states the remote + branch it applies to.
7. **Squashes and history rebuilds must not erase contributor PRs.** Release
   lines may squash *our own* work, but every contributor PR whose content
   shipped gets a zero-diff record merge at its timeline position (merge
   commit with the PR head as second parent, tree unchanged) so the PR shows
   as merged and the contributor's commits stay reachable in public history
   with their authorship. If the PR's base is a stale internal branch, merge
   it there via `gh pr merge` so the badge is true; GitHub refuses a base
   change to a branch that already contains the head. Superseded/reworked
   PRs are never merged — comment + let the author close. Before force-
   pushing a rewritten release branch: back up the pushed tip to Forgejo
   (`backup/<branch>-pre-rebuild-<date>`), pass a byte-identical tree-diff
   gate against the old tip, re-run CI, and note the rebuild in any open
   issue that tells people to build the branch.

## Critical files

| File | Role |
|---|---|
| `docs/internals.md` | Architecture + RSP deltas + phased plan (§1–§10). Reference, not a linear read. |
| `docs/dev/findings.md` | The `Dxx` finding log (§F/§H); lookups via `docs/dev/findings-index.csv`. |
| `CMakeLists.txt` | PC build (parallel to the N64 Makefile). Source list + `REGION_DEFS` live here. |
| `port/src/` | Shims: `libultra.c` (OS API; also shims the scheduler's hardware leaf calls and runs the software RSP inline — the game's real `src/sched.c` is compiled), `n64stubs.c` (boot/TLB/FPU/rmon), `random.c` (PRNG ported verbatim from `random.s`), `ucode.c` (microcode segment markers), `main.c`, `video.c`, … |
| `port/fast3d/` | Software RSP (adapted from the PD port). The main Phase 2 work. |
| `rsp/graphics/gmain.s` | GE's RSP ucode — ground truth for GBI/CC/RM. |
| `reference/mouse-injector/README.md` | **Stub only** — the vendored GEPD-Edition Mouse Injector source (GPLv2) was removed from the public repo 2026-09-19 (license hygiene: GPL code + prebuilt binary in an MIT project; it was never compiled here). The stub records provenance + how to re-vendor locally (gitignored). The ported mouse-aim model lives in `port/src/input.c` (D194 lineage); design record: `docs/dev/GEPD-INPUT-PLAN.md`. |
| A local **Perfect Dark PC port** checkout ([fgsfdsfgs/perfect_dark](https://github.com/fgsfdsfgs/perfect_dark)) | **Standing reference** — consult it whenever a work item has a PD analogue (same Rare engine family): port-layer ground truth (`port/fast3d/`, crash/system/video), plus copy candidates `port/src/preprocess/` (N64→PC asset conversion; `filemodel.c` is the D43 near-analogue) and `mixer.c`/`input.c`/`fs.c`. Port-layer files only; same family ≠ identical format — validate per field. Full audit: `docs/internals.md` §2.4. |

## Build

```sh
./build-pc.sh ntsc-final   # or pal-final / jpn-final
```

Needs CMake + SDL2 + zlib + OpenGL, and must run from the MSYS2 MINGW64 shell
(see `build-pc.sh` header and `docs/building.md`). ROM goes in `./data/`
(not distributed); assets must be extracted from it first (`docs/building.md`).

**Windows build environment (three recurring failure modes — diagnose in this order):**

1. **`Cannot create temporary file in C:\Windows\: Permission denied`** at the
   link step. The PE toolchain (ninja → cmd → gcc/ld) needs a writable
   TMP/TEMP; the msys→native env conversion drops it in non-login shells
   (agent harnesses; the `C:/msys64` tree here was built for a relocated MSYS2
   root — `<msys>` in the placeholder legend — so its path conversion is
   unreliable). `build-pc.sh` now **self-heals**: it
   probes a native child's TMP (via a file — piped `cmd.exe` stdout is
   unreliable under msys console emulation) and, if broken, re-runs
   cmake+build under PowerShell with `TMP`/`TEMP` set natively (the
   native→native boundary passes env through intact; only msys→native is
   broken). If you run cmake/ninja *by hand* from a broken shell: run them
   from cmd/PowerShell with `C:\msys64\mingw64\bin` on PATH.
   **Guard revision (2026-09-28):** the re-exec writes a self-contained
   `.ps1` to a temp file and runs `powershell -File` with plain path/word
   args only (the msys→native argv conversion mangles `$`-bearing
   `-Command` strings; a file write is conversion-free). Inside: PATH is
   built explicitly (never trust the inherited value); TMP is chosen from
   writable candidates (`%LocalAppData%\Temp` first —
   `[System.IO.Path]::GetTempPath()` honours the inherited *broken* TMP,
   observed as `C:\Windows\`); every native step self-logs to
   `build-pc/ge007-native-reexec-{diag,cmake,build}.log`; a null
   `$LASTEXITCODE` ("did not execute") is a distinct failure (exit 2/3) —
   in PS 5.1 a piped native command errors out non-terminating and never
   sets it, so an unguarded `if ($LASTEXITCODE -ne 0)` passes vacuously.
   In `.ps1`, `-DROMID=$Var` is a literal (bare tokens don't expand) — it
   must be `"-DROMID=$Var"`. Launch powershell plainly — `env -i` before it
   breaks the nested cmake launch (verified 2026-09-28).
   **FIXED 2026-09-30 (`fix/build-pc-tmp-reexec`):** the heredoc `.ps1`
   was byte-identical to a hand-extracted copy — not line endings. Three
   real bugs: (a) the TMP probe's inline `cmd //c "... \"path\" ..."` never
   ran (msys re-escapes embedded quotes as `\"`, which cmd rejects), so
   *every* build took the re-exec, and it only tested that TMP existed;
   (b) the `.ps1`'s toolchain dirs were derived from `command -v cmake`,
   which in Git Bash/agent shells is a pip cmake (no MSYS2 toolchain) —
   the hand-run worked because it passed explicit `C:\msys64\...` paths;
   (c) `/tmp` + `cygpath` disagree when Git Bash and MSYS2 `usr/bin` are
   both on PATH, so the `.ps1` could be written to one `/tmp` and
   powershell pointed at the other (it then exits 0 having done nothing).
   Now: the MSYS2 `mingw64/bin` is located and validated (override:
   `GE_MSYS2_ROOT`) and prepended to PATH; the probe is a `.cmd` file that
   tests TMP *writability*; all native-facing files live in the build dir
   with paths from bash's own `pwd -W`. Verified from an agent Git Bash
   shell: fresh + incremental builds, both on the direct path and with a
   forced unwritable TMP (`TMP=C:\Windows\`) through the re-exec.
   **FIXED 2026-09-30 (`fix/build-pc-reexec-noop`): the re-exec exited rc=2
   with the diag log ending `cmake ran LASTEXITCODE=[]`.** From a shell with
   MSYS2 `usr/bin` first on PATH, powershell inherits a stripped ~13-var env
   with `PATHEXT=.CPL` and no ComSpec/TMP/TEMP, so `& cmake.exe` silently
   launches nothing. The `.ps1` now restores PATHEXT and ComSpec. In that
   shell the re-exec itself is legitimately needed (no TMP), so the probe
   is correct. Verified: MSYS2-PATH shell, forced `TMP=C:\Windows\`, and a
   no-change incremental run all exit 0 with a fresh exe.
   **FIXED 2026-10-04 (`fix/build-arch-tag`, D515):** a FRESH build dir
   configured through the re-exec cached `CMAKE_HOST_SYSTEM_PROCESSOR ""` /
   `CMAKE_SYSTEM_PROCESSOR ""` (the stripped env also drops
   `PROCESSOR_ARCHITECTURE`, which CMake reads for the host processor — same
   class as PATHEXT/ComSpec), so `cmake/TargetArch.cmake` produced an empty
   arch tag: `Target arch:  (64bit=FALSE)`, binary named `ge007..exe`
   (a correct 64-bit build; only the name was wrong — found during T7, D513).
   The `.ps1` now restores `PROCESSOR_ARCHITECTURE` from the machine
   environment (fallback `AMD64`; `SystemRoot`/`windir` likewise) and logs
   the four vars to the diag log; `cmake/TargetArch.cmake` falls back to
   `${CMAKE_C_COMPILER} -dumpmachine` when `CMAKE_SYSTEM_PROCESSOR` is empty
   and FATAL_ERRORs instead of emitting an untagged binary. Existing build
   dirs are unaffected (value already cached); a dir configured before the
   fix picks up x86_64 on its next in-place reconfigure.
2. **`cannot open output file ge007.x86_64.exe: Permission denied`.** A
   **running** `ge007.x86_64.exe` locks the output file (Windows rule; you
   can't relink over a live PE). Check with `Get-Process | Where-Object {
   $_.ProcessName -like '*ge007*' }` and close the game before rebuilding.
   An agent must not kill the user's game process to make a link succeed.
3. **`cc1.exe: ... libmpfr-6.dll: cannot open shared object file`.** The
   calling PATH lacks `C:\msys64\mingw64\bin` (the gcc driver finds cc1 via
   its own directory, but the child needs the mingw DLL dir on PATH). In an
   msys shell: `export PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH"`
   before building.

## Verification ritual (after any build-affecting change)

1. **Undefined symbols.** Every symbol referenced by the compiled set (see `CMakeLists.txt`: `SRC_GAME`, `SRC_ENGINE`, `SRC_LIBAUDIO`, `SRC_LIBULTRARE_AUDIO`, `SRC_LIBULTRARE_DATA`, `SRC_GU`, `SRC_PORT*`) must be defined exactly once in the compiled set or in `port/`. Symbols that live in EXCLUDED files (`libultra/io/*`, `libultrare/io/*` except `vitbl.c`, `libultra/os/*`, `libultrare/os/*`, `sched.c`, `rmon.c`, `vi.c`, `src/*.s`) must be provided by `port/src/libultra.c`, `n64stubs.c`, `random.c`, or `ucode.c`.
2. **Duplicates.** No symbol defined twice across the compiled set (watch `sp_*` stacks, `rmon*`, `os*` shims, segment markers).
3. **Syntax.** Every touched file must parse; `./build-pc.sh` is the final word.

Run `/linkcheck` for this sweep. Record new findings in `docs/dev/findings.md` §F/§H style (next `Dxx` label after the last used) and add the label to the §F index.

## Phase status

Do not keep a status summary here — it goes stale. Open work, known issues
and decisions owed: **`docs/ROADMAP.md`** (the single tracker). Current
release state: the README "Status" section.
