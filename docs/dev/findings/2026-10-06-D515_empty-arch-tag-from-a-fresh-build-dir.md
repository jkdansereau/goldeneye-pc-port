# D515

<!-- from the old section-F/H table row -->
| D515 | **Empty arch tag from a fresh build dir: the re-exec env drops `PROCESSOR_ARCHITECTURE`, CMake caches an empty processor, and the binary is named `ge007..exe`** — Build infra; D513 follow-up; full `## D515` entry at file tail | FIXED (headless-verified) |

## D515: empty arch tag from a fresh build dir (`ge007..exe`) — the re-exec env drops `PROCESSOR_ARCHITECTURE` (build infra; D513 follow-up)

**Symptom.** The first build in a fresh worktree configured through
`build-pc.sh`'s PowerShell re-exec cached `CMAKE_HOST_SYSTEM_PROCESSOR ""`
and `CMAKE_SYSTEM_PROCESSOR ""` in `build-*/CMakeFiles/<ver>/CMakeSystem.cmake`.
`cmake/TargetArch.cmake` takes `CMAKE_SYSTEM_PROCESSOR` when not
cross-compiling, so `TARGET_ARCH` came out empty: configure printed
`Target arch:  (64bit=FALSE)` / `Output binary: ge007.` and the link produced
`ge007..exe`. The binary was a **correct 64-bit build** — `PLATFORM_64BIT`
comes from `port/include/platform.h`'s own `__x86_64__`, not the CMake value
(only `tools_pc/d88_layoutprobe.c` reads the CMake side) — so only the name
and tag were wrong. Found during T7 (D513 build note); existing build dirs
were immune because the value was cached from an earlier good configure.

**Root cause.** The stripped re-exec env (the msys→native conversion that
also dropped PATHEXT/ComSpec, AGENTS.md 2026-09-30 notes) can drop
`PROCESSOR_ARCHITECTURE`, which CMake reads to fill
`CMAKE_HOST_SYSTEM_PROCESSOR`. Same bug class, one more missing var.

**Fix (`fix/build-arch-tag`).**
1. `build-pc.sh` re-exec `.ps1`: next to the PATHEXT/ComSpec restore, restore
   `PROCESSOR_ARCHITECTURE` from the machine environment
   (`[Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE','Machine')`,
   fallback `'AMD64'`); `SystemRoot` and `windir` the same way (fallback
   `C:\Windows`). One diag line logging all four vars goes to
   `build-*/ge007-native-reexec-diag.log` (kept permanently; cheap, useful).
2. Belt and braces in `cmake/TargetArch.cmake`: when not cross-compiling and
   `CMAKE_SYSTEM_PROCESSOR` is empty, ask the compiler
   (`${CMAKE_C_COMPILER} -dumpmachine`, same regex as the cross-compile
   branch); if that is empty too, `message(FATAL_ERROR)` naming this bug
   instead of silently producing `ge007.`. The non-empty path is unchanged.

**Verified (Git Bash agent shell).** Note on reproduction: this harness's
msys→native conversion passes the full env through (71 vars intact; a plain
forced re-exec kept `PROCESSOR_ARCHITECTURE`), so the stripped-env case was
simulated with `env -u PROCESSOR_ARCHITECTURE` — the same mechanism D513 hit.
1. Before the fix, fresh dir + forced re-exec (`TMP='C:\Windows\'`) +
   stripped env: reproduced `ge007..exe`, diag showed
   `PROCESSOR_ARCHITECTURE=[]`, `CMakeSystem.cmake` empty/empty.
2. After the fix, same command: diag shows `PROCESSOR_ARCHITECTURE=[AMD64]`
   (restored), `Target arch: x86_64 (64bit=TRUE)`, `ge007.x86_64.exe`,
   `CMakeSystem.cmake` AMD64/AMD64.
3. After the fix, direct path (normal TMP), fresh dir: same as 2.
4. TargetArch fallback on its own: MSYS2 cmake + gcc + Ninja, stripped env,
   fresh dir → `CMakeSystem.cmake` still empty/empty but
   `Target arch: x86_64 (64bit=TRUE)` via `-dumpmachine`.
5. Incremental rerun in 2's dir: one-time relink (cache normalisation), then
   `ninja: no work to do`, rc=0.

**Owed / notes.** `../gh-fullhist-wt-t7/build-pc` still holds the empty cached
value and a `ge007..exe` — it needs one reconfigure (or a fresh build dir);
left untouched by this fix. A build dir configured before the fix is fine to
keep: the next `./build-pc.sh` run reconfigures in place and picks up x86_64.
