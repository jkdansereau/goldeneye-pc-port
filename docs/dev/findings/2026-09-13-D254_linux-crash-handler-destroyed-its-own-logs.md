# D254

<!-- from the old section-F/H table row -->
| D254 | **Linux crash handler destroyed its own logs: `ge007.crash.log` only ever contained the SIGABRT block, never the original SEGV.** — full `## D254` entry at file tail | FIXED — field-verified. |

## D254 — Linux crash handler destroyed its own logs: `ge007.crash.log` only ever contained the SIGABRT block, never the original SEGV.

**Linux crash handler destroyed its own logs: `ge007.crash.log` only ever contained the SIGABRT block, never the original SEGV.** The POSIX `crashHandler` (`port/src/crash.c`) finished by calling `sysFatalError()` → `abort()` → SIGABRT, which re-entered the *same* handler (SIGABRT was one of the installed signals); the second pass reopened the log with `"wb"` and overwrote the first pass's SEGV block — faulting PC, signal, everything. The Deck firefight crash (D255) came back as a bare `SIGNAL: 6` abort record for exactly this reason, forcing offline disassembly of the CI binary to recover what the log should have said. **Fix (port-only):** re-entrancy guard (`inCrashHandler`, second pass `_exit(128+sig)` immediately) + terminate directly with `_exit()` instead of `sysFatalError()` (the Windows path already terminated directly for the same reason) + the Linux block now dumps the faulting context: `FAULT ADDR` (`si_addr`) and the full general register set (Rax/Rcx/Rdx/Rsi/Rdi/R8–R11, Rsp/Rbp) from the `ucontext`, so a field crash now names the bad pointer on its face. **Field-verified 2026-09-13:** the D255 Deck crash came back as `SIGNAL: 11` + `FAULT ADDR: 0x10` + full register set — exactly the format designed here; it directly identified the NULL-model case (see D255).

**Status (from §F table):** FIXED — field-verified.
