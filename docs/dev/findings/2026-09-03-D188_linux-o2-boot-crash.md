# D188

<!-- from the old section-F/H table row -->
| D188 | **Linux `-O2` boot crash: `_Printf` passes `&args` of a by-value `va_list` param (M-48).** — full `## D188` entry at file tail | FIXED (`src/libultrare/libc/xprintf.c`, `src/sprintf.c`, `#ifdef PORT`); PR #22 |

## D188 — Linux `-O2` boot crash: `_Printf` passes `&args` of a by-value `va_list` param (M-48).

**Linux `-O2` boot crash: `_Printf` passes `&args` of a by-value `va_list` param (M-48).** On the x86-64 System V ABI `va_list` is an array type → a by-value `va_list` parameter decays to a pointer, so `_Printf`'s `_Putfld(&x, &args, …)` handed `_Putfld` the address of its own local pointer slot, not the arg list; `_Putfld`'s `va_arg(*args, …)` then walked garbage. Latent at `-Og`, SIGSEGV at `-O2` (PR #9) — crash path `bossInitMainthreadData` (`boss.c:247`, D121 per-level `-m` sprintf) → `sprintf("-level_%c%c %s", …)` → `_Putfld` @ `xprintf.c`. Same UB class as D187. Windows x64 `va_list` is a scalar so `&args` worked there (Windows build unaffected — `-level_09` framediff 3/3). Fix: `#ifdef PORT` — `_Printf` takes `va_list *`, threads it straight to `_Putfld`; `src/sprintf.c` (sole PC caller; `rmon.c` excluded) passes `&args` of a real object. N64 build untouched (`#ifdef PORT`; file byte-matches ROM).

**Status (from §F table):** FIXED (`src/libultrare/libc/xprintf.c`, `src/sprintf.c`, `#ifdef PORT`); PR #22
