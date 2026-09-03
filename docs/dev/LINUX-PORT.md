# Linux port — status & gap analysis

Session M-38 (2026-09-02). Goal-2 on the roadmap (after the Windows alpha /
release artifact). This is a scoping + first-patch pass; **nothing here has run
on a Linux machine yet.**

## Method

Read `build-pc.sh`, `CMakeLists.txt`, `docs/building.md`, and grepped
`port/src`, `port/fast3d`, `port/include` for Windows-only headers / Win32 APIs
/ unguarded `_WIN32` branches. A local Qwen `triage` pass seeded the hunt; every
claim below was re-checked against the real files (the 27B was wrong on two of
three headline items — see "Ruled out").

## What is already Linux-ready (verified by reading)

- **`port/include/platform.h`** — sets `PLATFORM_LINUX` / `PLATFORM_MACOS` /
  `PLATFORM_WINDOWS` cleanly.
- **`CMakeLists.txt`** — `find_package(SDL2 REQUIRED)`, `find_package(ZLIB
  REQUIRED)`, and `else()` branches already present for the non-Windows case:
  `GL_LIBRARY = GL` (l.291), `EXTRA_LIBRARIES = stdc++ m dl pthread` (l.304),
  `elseif(UNIX)` platform block (l.86). `WIN32_EXECUTABLE` and the MinGW zlib
  fallback are correctly `if(WIN32)` / `if(MINGW)` guarded.
- **`build-pc.sh`** — already documents the Linux apt deps
  (`cmake libsdl2-dev zlib1g-dev libgl1-mesa-dev`) and is a plain bash script,
  no MSYS-isms in the executable body.
- **`port/src/system.c`** — `sysSleep()` has a `nanosleep()` `#else` branch
  (l.76-82); `<windows.h>` is `PLATFORM_WINDOWS`-guarded.
- **`port/src/libultra.c`** — the OS-thread kernel has a real pthreads branch
  next to the Win32 `_beginthreadex` one.
- **`port/src/crash.c`** — `MessageBoxW` / `SetThreadPriority` / `__debugbreak`
  all `_WIN32`-guarded, with `PLATFORM_LINUX` counterparts.
- **`port/src/video.c`** — `GE_MKDIR` has both `_mkdir` and `mkdir(p,0777)`.
- No `__declspec`, `#pragma comment`, or hardcoded `\`-path / drive-letter
  usage anywhere in `port/`.

## Ruled out (Qwen triage false positives)

- ~~`system.c:75` `Sleep()` has no POSIX branch~~ — it does (`nanosleep`).
- ~~CMake needs `else()` branches for zlib/pthread on Linux~~ — already there.

## The one real code gap — FIXED this session (verification owed)

**`port/src/romdata.c`** — the fixed-address cart map (`0x10000000`, so absolute
ROM-asset symbols are live host pointers) had a **Windows-only** implementation
(`VirtualAlloc`); the `#else` branch was a stub that logged "not implemented on
this platform" and fell straight to the heap-copy fallback. In heap-copy mode
anything that dereferences a cart address *directly* (not via
`romdataGetRom()` / the PI shims) reads wrong memory — so Linux would run, if at
all, in the same degraded mode Windows only hits when `VirtualAlloc` loses the
address race.

**Change (M-38):**
- Factored the post-map work (memcpy + `pcmodelsLoadSidecars` +
  `pccgLoadSidecars` + D55 RLE-header fixup) into `romdataFinishCartMap()`,
  shared by both paths so they cannot drift.
- POSIX `#else` now does an anonymous `mmap((void*)CART_BASE, len,
  PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE, -1, 0)`.
  `MAP_FIXED_NOREPLACE` (Linux 4.17+) fails rather than clobbering an existing
  mapping; where the macro is absent the plain hint is advisory and the
  `at == CART_BASE` check catches a relocated result. On any failure it
  `munmap`s and falls through to the identical heap-copy path as before.
- `romdataDestroy()` `munmap`s on POSIX (tracked length in `mappedLen`).

**Owed verification (needs a Linux box):**
1. Compiles under GCC on Linux (`<sys/mman.h>` resolves; no decomp-include-path
   shadowing surprise).
2. `mmap` actually lands at `0x10000000` on a stock ASLR Linux — `0x10000000`
   (256 MB) is low but usually free; if the kernel routinely refuses it,
   consider linking the binary no-PIE or reserving the range via a linker
   script so the loader keeps it clear.
3. `-level_09` boots crash-free with the mapped path taken (check the log says
   "mapped at 0x10000000 (cart base)", not "using heap copy").
4. Whether the heap-copy fallback is actually survivable on Linux if step 2
   fails — audit for direct `0x1xxxxxxx` derefs outside the PI shims.

## Remaining Linux work after the romdata patch lands

| # | Item | Size | Note |
|---|------|------|------|
| 1 | Run the Ubuntu **`validate`** CI job → upgrade it to a real **compile** job | S | `.github/workflows/ci.yml` already configures on Ubuntu; flipping it to `cmake --build` surfaces the real GCC-strictness gaps cheaply |
| 2 | First actual Linux runtime bring-up (`-level_09`) | M | expect a short tail of unguarded calls / struct-layout asserts GCC-on-Linux vs MinGW |
| 3 | `docs/building.md` — promote Linux from "untested" to "supported" once 1+2 pass | S | |
| 4 | SDL2 / GL context creation on X11 + Wayland | M | fast3d is GL 3.3 core; PD port runs on Linux so the path is known-good |
| 5 | Package: an AppImage or tarball equivalent of `bundle-win.sh` | M | reuse `prepare-assets.py` verbatim — it is already OS-agnostic |

## Recommended next step

Land the romdata patch behind CI (item 1) — turn the Ubuntu `validate` job into
a compile job in the same PR so the "does it build on Linux" question is
answered by the bots, then do the runtime bring-up on a real machine.
