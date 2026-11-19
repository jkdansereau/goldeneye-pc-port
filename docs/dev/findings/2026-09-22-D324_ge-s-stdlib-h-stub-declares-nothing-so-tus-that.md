# D324

<!-- from the old section-F/H table row -->
| D324 | **GE's <stdlib.h> stub declares nothing, so TUs that include it get NO CRT prototypes — implicit `int f()` declarations silently corrupt non-int returns: `atof` garbage in every float ini parse (D38 class).** — full `## D324` entry at file tail | FIXED (2026-09-22) — proven impact: `Game.ScreenShakeIntensity` (the only float ini option) silently parsed to garbage whenever a user set it; caught via the D294 `GE_ROOMPOOL` hook. Fix: `port/shim/stdlib.h` now declares atof/strtod/strtol/atexit/getenv unconditionally (K&R form); pc_protos.h gains atof/strtod for game TUs; glass2.c PORT block uses `__builtin_memset`. Residual harmless implicit decls (atoi/floorf/sqrtf/memset builtins, lround) documented. |

## D324 — GE's <stdlib.h> stub declares nothing, so TUs that include it get NO CRT prototypes: implicit `int f()` declarations silently corrupt non-int returns (atof garbage in every float ini parse; D38 class)

**Context.** Found while verifying the D294 fix: the new `GE_ROOMPOOL` env hook in `port/src/video.c` called `atof()`, and the parsed value was deterministic-per-call-site garbage (observed 15279232 / 7087232 / 6817888 / 14288992 across runs) instead of the env string's value. The scale override was silently ignored on every run until prototypes were added.

**Mechanism.** `include/stdlib.h` (the N64 decomp's libc stub) wraps its entire body in `#if 0` — only the `lldiv_t`/`ldiv_t` typedefs are live. `port/shim/stdlib.h` (what `<stdlib.h>` resolves to for C TUs; see docs/internals.md §11 D12) includes that stub and then declared a couple of stragglers — but `getenv`/`realloc` **only on non-Windows**, on the empirically-wrong assumption that MinGW's other CRT headers "leak" prototypes (config.c gets no leaked `getenv`; the implicit-declaration warning proves it). So any port/src or game TU that calls a CRT function nothing else declared compiles with an **implicit `int f()` declaration**. On x86-64 SysV that means: a `double`/`float` return in **xmm0 is read from eax** (garbage), pointer returns are truncated to 32 bits, and `long` returns truncate on LP64 (Linux) builds.

**Why only some functions actually misbehave (the triage rule for this class):**
- **GCC builtins** (`floorf`, `sqrtf`, `memset`, …): GCC recognises them even when implicitly declared — it warns (`-Wbuiltin-declaration-mismatch`) but generates the *correct* xmm0/inline code. Harmless in practice. (This is why `sky.c`'s implicit `floorf` never visibly broke: correct values, and its fold result is additionally masked by `SKY_TC_WRAP` periodicity — any multiple of the wrap period is visually identical on a wrapping sky texture.)
- **int-returning** (`atoi`, `atexit`): the 32-bit int fits in eax exactly. Harmless (still UB per the standard; warning only).
- **Non-builtin float/double/pointer/64-bit-long returns** (`atof`, `strtod`, `getenv`, `strtol` on LP64, …): genuine silent corruption. This is the only bucket that needs fixing.

**Proven impact.** (1) `port/src/config.c` `parseFloat` → **every float ini option parsed to garbage whenever a user set one** — currently exactly one exists, `Game.ScreenShakeIntensity`, and the affected user had it set to 10 (silently became an eax-derived float for months). (2) The D294 `GE_ROOMPOOL` hook (video.c) — caught during verification. (3) `getenv` in 8 port TUs + `src/libultra/audio/csplayer.c`: pointer truncated to 32 bits — harmless *so far* on Windows (CRT env block sits below 4 GB), a landmine otherwise.

**Ruled out during diagnosis** (all tested, all clean): msvcrt corruption, locale, stack alignment, C++ runtime interference — plain MinGW test programs parse identically; only this build is affected, purely via the header shadowing. Garbage values are deterministic per call-site (same eax state each time) but vary between runs.

**Fix (port-layer only):**
- `port/shim/stdlib.h`: now declares `atof` / `strtod` / `strtol` / `atexit` / `getenv` **unconditionally** (K&R form — compatible with any real or leaked prototype), after including the N64 stub. `realloc` stays non-Windows-only (strict-GCC leak gap).
- `port/include/pc_protos.h`: gains `double atof(); double strtod();` for game TUs that include `<ultra64.h>` but never `<stdlib.h>` (no game TU currently calls them; future-proofing + canonical D38 list).
- `src/game/glass2.c` (the D219 `#ifdef PORT` block): `memset` → `__builtin_memset` so that block needs no prototype at all.

**Residual implicit declarations (audited, all in the harmless buckets above — warning-only, no action):** `atoi` in `src/game/chr.c`, `src/game/sky.c`, `src/game/bondview2.c`, `port/src/optionsoverlay.c`; `lround` in `optionsoverlay.c` (fine on Windows LLP64 where `long`=32 bits; would truncate on an LP64 Linux build — revisit only if a Linux port is actively maintained); the builtin family (`floorf`/`sqrtf`/`memset`) wherever it appears.

**Status:** FIXED (2026-09-22). Build clean; D294 verification battery re-run post-fix (faithful→authored size, user settings→0 failures, `GE_ROOMPOOL=0.25`→honoured + exhaustion on demand). Cross-ref: D38 (original implicit-declaration finding), D294 (where it was caught).
