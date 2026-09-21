# macOS Apple Silicon (native arm64) port — implementation plan

Session 2026-09-18. Scope decision: **native arm64 only** (no Rosetta/x86_64
build). Nothing in this plan has run yet; §1 is measured, §2 is design, §3 is
the work. Companion: `LINUX-PORT.md` (same shape, earlier platform).

Execution model: research/design/integration items are run by the session
model ("integrator"); items marked **mechanic** are self-contained briefs for
the Sonnet-backed `.opencode/agents/mechanic.md` subagent. Every mechanic
item lists files, recipe, budget, verify. Items in the same phase that touch
disjoint files may run in parallel (dev-process.md §3).

---

## 0. Summary

The compiler is not the problem. Apple clang builds 235/237 C TUs of the
existing tree with `-fms-extensions` (in place of GCC's `-fplan9-extensions`)
plus small fixes. fast3d already selects a 4.1 Core GL context on macOS and
uses no compat or >4.1 features.

**The blocker is the port's address model.** The port realises the N64's
32-bit address space as *host* addresses: DRAM at `0x70000000` (+ KSEG0
mirror at `0x80000000`), cart at `0x10000000`, and ~1700 linker-absolute
symbols with those values; `osVirtualToPhysical` is a `u32` truncation and
`u32 → pointer` is an identity cast. That works on Windows/Linux because the
image and all mappings sit below 4 GiB. On native arm64 macOS **nothing can be
mapped below 4 GiB**: `__PAGEZERO` covers it, `-pagezero_size` gets the
process SIGKILLed, PIE is mandatory, and `MAP_32BIT` fails.

The fix (§2) is a **shifted window**: keep the N64 32-bit space intact but
place it at a fixed host base `B` (`PORT_ADDR_BASE`), so every game-visible
32-bit value is *identical* to today's Windows values and only the
`u32 → host pointer` direction changes (add `B`). `B = 0` on Windows/Linux, so
those builds are byte-for-byte unchanged. All conversions go through a
port-owned chokepoint; the residue is a compiler-driven census of raw
`u32 → pointer` casts in game code, each rewritten through a `PORT_*` macro
(the sanctioned D3x ABI/layout class — no logic change).

Phases: **M0** compile+link on macOS → **M1** address model in `port/` →
**M2** game-code cast census + rewrites → **M3** runtime bring-up (Cocoa
threading, first frame, level sweep) → **M4** packaging/CI/docs.

---

## 0b. Base change (2026-09-18, later): rebased onto PR #88 + GCC

After M0/M1 were working on Apple clang, PR #88 ("Add native Intel macOS
support", JunielKatarn) turned up: a single commit on this repo's exact base,
targeting **Intel** macOS and explicitly leaving Apple Silicon unsupported. The
arm64 work was rebased onto it rather than maintained in parallel. Branch:
**`macos-arm64-gcc`** (the earlier clang branch `macos-arm64` is superseded).

**Adopted from #88 — this deletes the corresponding work in §3 M0:**
- Homebrew GNU GCC (`gcc-16`) + libstdc++. Removes every clang workaround:
  no `-fms-extensions`/`-Wno-microsoft-anon-tag`, no libc++ wrapper header
  shims (the old D324), no `-D_FORTIFY_SOURCE=0`.
- `scripts/gen_macho_syms.py` — build-tree Mach-O symbol generation; the
  committed `*.darwin.s` duplicates are gone.
- `scripts/strip_weak_pragmas.py` + `port/src/macho_weak_aliases.s` — the
  `#pragma weak` handling, so **`src/` carries no macOS edit for it** (the old
  D323 in-source `#if defined(PORT) && defined(__APPLE__)` equates are gone).
- POSIX `shm_open` DRAM backing, Darwin crash handling, and the AppKit
  main-thread event-pump guard in `gfx_sdl2.cpp` (that last one was §3 M3.1).

**Kept from this plan — the arm64 delta:**
- §2's shifted-window address model (M1 / D327). `-pagezero_size 0x10000`,
  the core of #88's Intel approach, is fatal on arm64 (shrinking `__PAGEZERO`
  gets the process SIGKILLed), so CMake now sets it for x86_64 only and arm64
  uses `PORT_ADDR_BASE`.
- D325 (`__x86_64__` → `PLATFORM_64BIT`). Without it #88's tree does not even
  *link* on arm64 — 23 undefined `_ANIM_DATA_*`.
- `gen_macho_syms.py --base` (bakes `PORT_ADDR_BASE` into the absolute
  symbols; `--base 0` output is byte-identical to #88's transform).
- arm64 crash registers (`__ss.__pc/__lr/__sp/__fp`) and
  `_dyld_get_image_header` / `_NSGetExecutablePath`.
- All of M2 (the game-code re-basing sweep).

Mergeability consequence: the arm64 contribution is now a small additive delta
on top of #88, and `src/` has no macOS-specific toolchain/alias edits.

## 1. Measured facts (this machine: macOS 26.3, arm64, Apple clang 21, SDL2 via `sdl2-compat` 2.32)

| # | Test | Result |
|---|------|--------|
| F1 | `mmap(MAP_FIXED)` at `0x10000000` / `0x70000000` / `0x80000000`, native arm64 | **ENOMEM** — first 4 GiB is `__PAGEZERO` |
| F2 | Link with `-Wl,-pagezero_size,0x4000` (and 0x1000…0x90000000), native arm64 | links; **process SIGKILLed at exec (exit 137)** |
| F3 | `-Wl,-no_pie`, `-Wl,-image_base` | ignored ("Linking with PIE"); image lands at `0x1_0000_0000 + slide`, slide observed `0x32_0000`–`0x4EF_8000` (varies per run) |
| F4 | `mmap(MAP_32BIT)` | defined, **fails**; default thread stacks at `0x16b_xxxx_xxxx` |
| F5 | Same code as x86_64 + `-pagezero_size 0x1000` under Rosetta | all fixed maps succeed (not our target; recorded for completeness) |
| F6 | `mmap(MAP_FIXED)` at `B + n64addr` for `B ∈ {1,2,3,4} << 32` | all succeed — **but `MAP_FIXED` silently replaces existing mappings on macOS** (no `MAP_FIXED_NOREPLACE`), so success ≠ range was free. A 64 MiB `malloc` was observed at `0x4_8400_0000`. |
| F7 | Non-clobbering 4 GiB `PROT_NONE` reservation via `mach_vm_map(VM_FLAGS_FIXED)` (no `OVERWRITE`) | `0x3<<32` free (this run); `0x2,0x4,0x8,0x10,0x20,0x40 <<32` **occupied** (libmalloc/VM regions, varies); **`0x100_0000_0000` (1 TiB), `0x400_0000_0000`, `0x1000_0000_0000` (16 TiB) reserved cleanly**, DRAM+cart carved inside OK |
| F8 | Hinted `mmap` without `MAP_FIXED` | hint ignored (relocated to `0x70_0000_0000`) — cannot rely on hints; must reserve |
| F9 | `shm_open`+`ftruncate`+2×`mmap(MAP_SHARED\|MAP_FIXED)` at `B+0x70000000` / `B+0x80000000` | works; alias verified (`memfd_create` replacement) |
| F10 | `.set _cfb_16, 0x470000000` (64-bit-valued absolute symbol) referenced from C | links, `nm` type `A`, **no rebase fixups** (`dyld_info -fixups`) → ASLR-stable; clang emits GOT loads |
| F11 | `-fplan9-extensions` | unknown argument (hard error). `-fms-extensions` accepts `inherits struct`; derived→base pointer passing becomes `-Wincompatible-pointer-types` (already demoted) |
| F12 | `-fpermissive` (C), `-Og`, `-Wno-unused-but-set-variable`, `-fno-pie` | accepted (`-fpermissive` is a silent no-op in C) |
| F13 | `-Wl,--disable-dynamicbase`, `-Wl,-Ttext-segment=` | ld64 unknown options (already not applied on APPLE) |
| F14 | `.section .data` in both `.s` files | assembler error; `.data` works. `.global LameE` (88 `L*` names in `romassets_u.s`) → "non-local symbol required" until `_`-prefixed |
| F15 | `#pragma weak X = Y` (sinf.c, cosf.c, objective_status.c, lv.c, spectrum.c) | Darwin emits a *local* alias: cross-TU users get undefined symbol; same-TU `extern` + use → "reference to X is ambiguous" (lv.c:603-606, spectrum.c:266) |
| F16 | `-fsyntax-only` sweep, 237 C TUs, with `-fms-extensions -D_FORTIFY_SOURCE=0` + corrected host-header shim paths | **235/237 pass** (fails: lv.c, spectrum.c — F15). 4 fast3d `.cpp` TUs fail on libc++ header shadowing (`include/math.h`, `limits.h`, `assert.h` ahead of the SDK) |
| F17 | `port/include/pc_protos.h:34` is gated on `__x86_64__` | on arm64 all ~400 D38 prototypes vanish → 45–63 implicit-declaration warnings per big TU (bg.c, chrai.c, propobj.c, front.c, boss.c) = silent pointer-return truncation. 23 game/asset sites use the same gate |
| F18 | GL | fallback table lands on `{4,1,CORE}` on macOS; GLSL `#version 410 core`; VAO created for Core; no compat/>4.1 entry points. 2.1 compat is **not** enough (GLSL 1.30 min, FBO/blit/MSAA, `GL_DEPTH_CLAMP`) |

---

## 2. Design: the shifted-window address model

### 2.1 Invariant

Let `B = PORT_ADDR_BASE` (compile-time, per platform). Every host pointer that
game code may truncate to 32 bits lives in the window `W = [B, B + 4 GiB)`,
at host address `B + n64addr`. Therefore:

- `(u32)host_ptr` for any `host_ptr ∈ W` yields **exactly today's Windows
  value**. The pointer→u32 direction needs no game-code edits for in-window
  pointers.
- `u32 → host pointer` becomes `B + u32`. This is the only direction that
  changes, and it is funnelled through one chokepoint.
- On Windows/Linux `B = 0`: every chokepoint is an identity, behaviour and
  codegen are unchanged.

### 2.2 N64-space layout inside `W` (window offsets; unchanged from today)

| Range | Content | Host mapping |
|---|---|---|
| `[0x0000_0000, 0x1000_0000)` | segmented DL addresses (never host memory) | none (reserved `PROT_NONE`) |
| `[0x1000_0000, 0x2000_0000)` | cart image + sidecars (`CART_BASE`) | `MAP_FIXED` carve (romdata.c) |
| `[0x4000_0000, 0x7000_0000)` | **image-relative encoding** (§2.4) — virtual, never mapped | none |
| `[0x7000_0000, +8 MiB)` | DRAM V1 | `shm_open` view 1 (dram.c) |
| `[0x8000_0000, +8 MiB)` | DRAM V2 / KSEG0 mirror | `shm_open` view 2 |
| `[0xA000_0000, 0xC000_0000)` | game-thread stacks, 8 MiB + guard page each (replaces `MAP_32BIT`) | `MAP_FIXED` carve (libultra.c) |
| everything else | reserved, unmapped | `PROT_NONE` |

`B` proposal: **`0x1000_0000_0000` (16 TiB)** — reserved cleanly (F7), far
above libmalloc's regions (`0x1..0x8 <<32`, observed) and the mmap hint region
(`0x70_0000_0000`), below `fast3d_ptr_ok`'s `0x8000_0000_0000` bound. Fallback
candidate `0x100_0000_0000`. Item **M1.1** confirms on a second machine and
fixes the constant.

### 2.3 Startup contract

`portAddrInit()` is the **first thing in `main()`** (before SDL, before any
large allocation): `mach_vm_map(task, &B, 4 GiB, VM_FLAGS_FIXED, PROT_NONE)`.
Failure is fatal with a clear message — the same contract as the Windows
build's `0x140000000` load-base check (`main.c:176`). Once reserved, carving
sub-ranges with `MAP_FIXED` is safe (we own the range). No heap-copy
fallbacks on macOS: `romdata` cart map failure is fatal, not degraded.

### 2.4 Chokepoint API — `port/include/port_addr.h` (new)

```c
#define PORT_ADDR_BASE  /* 0 on Windows/Linux; B on PLATFORM_MACOS && PLATFORM_ARM */
/* u32 (N64/"physical") -> host pointer. */
static inline void *portN64ToHost(u32 a) {
    if (a >= 0x40000000u && a < 0x70000000u)          /* image-relative (D131 generalised) */
        return (void *)(portImageBase() + (a - 0x40000000u));
    return (void *)(PORT_ADDR_BASE + (uintptr_t)a);
}
/* host pointer -> u32. In-window: window offset. In-image: 0x40000000 + image offset. */
static inline u32 portHostToN64(const void *p);
/* Cast-site macro for game code (D3x class). Identity semantics at B == 0. */
#define PORT_N64PTR(T, expr) ((T *)portN64ToHost((u32)(expr)))
```

Rewired to it (all in `port/`): `osVirtualToPhysical`/`osPhysicalToVirtual`
(`libultra.c:1442-1443`), `OS_K0_TO_PHYSICAL`/`OS_PHYSICAL_TO_K0`
(`port/shim/PR/os.h:34,37` — subtract/add `B + 0x70000000`), `PHYS_TO_K0`
(`port/shim/PR/R4300.h:25`, `port/shim/R4300.h:14`), fast3d `seg_addr`
(`gfx_pc.cpp:2881-2935` — replace the `mod_hi` block with the image-relative
rule; add `B` to the `< 0x800000` and pass-through returns), the fast3d
range classifier (`gfx_pc.cpp:630-635` — compare `p - B`), `G_MW_SEGMENT`
(`:2324-2327`) and `G_MW_NUMLIGHT` (`:2317`), mixer `osPhysicalToVirtual`
users (`mixer.c:147,152,297`), `piServiceDma` (`libultra.c:1001`),
`dramHostAddrValid` (`libultra.c:909-911`), `romdataIsCartAddr`
(`romdata.c:441-451`), `n64stubs.c:94-107` (`0x70700000` literal).

**Why image-relative reproduces D131 exactly on Windows:** the image base is
`0x1_4000_0000`, so `0x40000000 + (p - image_base) == (u32)p` — the current
truncation result. On Linux (`-Ttext-segment=0x20000000`) `&sym` currently
passes through `seg_addr` untouched; under the new rule it round-trips
through `0x4xxxxxxx` to the same pointer. No behaviour change either way.

### 2.5 Absolute symbols

`dram_syms.s` (3 symbols) and `romassets_<region>.s` (1685 cart symbols) are
generated with `B +` added on Darwin, Mach-O syntax (`.data`, `_` prefix).
Verified viable by F10. The generator (`scripts/gen_romassets.py`) grows a
`--darwin --base 0x...` mode; `B` is defined in exactly two places
(`port_addr.h` and the CMake variable that feeds the generator) with a
`_Static_assert`/CMake check that they agree.

### 2.6 The residue: raw casts in game code

Game code sites that turn a `u32` back into a pointer without going through a
chokepoint are the only thing the model cannot fix by construction. Grep
estimates (heuristic, noisy): 117 `(u32|s32)&sym` in 21 files, ~278
`(T *)obj->field`-shaped casts, 7 `romptr_t`, 5 existing `PORT_PTRADD`. The
authoritative census is the compiler (item **M2.1**): clang's
`-Wint-to-pointer-cast`, `-Wpointer-to-int-cast` and `-Wint-conversion`
(all warnings, none demoted to off) list every site precisely once M0 links.
Each `u32 → pointer` site becomes `PORT_N64PTR(T, expr)`; each
`&image_symbol → u32` site becomes `portHostToN64(&sym)`; in-window
`ptr → u32` truncations are left alone. Every edit is identity at `B = 0`.

### 2.7 What this model deliberately does NOT do

- No PD-style relocatable/offset addressing rewrite of game structures.
- No change to `Gwords {uintptr_t w0, w1}` (already 64-bit-safe).
- No attempt to place the executable inside `W` (F3: not controllable).

---

## 3. Work items

Sizes: **S** ≤1 h, **M** 1–3 h, **L** half-day+. "mechanic" = Sonnet subagent
with the brief below; "integrator" = session model. Budgets are per
dev-process.md §1; on expiry: revert probes, leave the tree buildable, write up.

Common to every item — **READ FIRST:** this doc §2; `docs/porting-notes.md`
(headers); `AGENTS.md` non-negotiables. **CONSTRAINTS:** no game-logic
changes; `src/`/`assets/` edits only where an item lists them and only in the
sanctioned mechanical classes; N64 build (`Makefile`, `tools/`, `rsp/`, `ld/`)
untouched; new findings appended to `docs/dev/findings.md` §F/§H under the
next free `Dxx` (last used at time of writing: D293) and indexed at the top of
§F. **VERIFY** for build-affecting items always ends with
`./build-pc.sh ntsc-final` (or the explicit target-only build the item names).

### Phase M0 — compile and link on macOS arm64 (no runtime expected)

Goal: `./build-pc.sh ntsc-final` produces `build-pc/ge007.*` on this Mac.
`B` is still 0 in this phase (address model stubbed), so the binary will not
boot; the milestone is a clean link + the M2 census becoming available.

| ID | Title | Who | Size | Deps | Files |
|---|---|---|---|---|---|
| M0.1 | CMake: Apple-clang toolchain path | mechanic | M | — | `CMakeLists.txt`, `cmake/*.cmake`, `build-pc.sh`, `docs/building.md` |
| M0.2 | `__x86_64__` → `PLATFORM_64BIT` as the 64-bit-PC gate | mechanic | S | — | `port/include/pc_protos.h`, `port/include/platform.h`, + the 23 game/asset sites listed |
| M0.3 | Darwin mode for generated assembly | mechanic | M | M0.1 | `scripts/gen_romassets.py`, `port/src/dram_syms.s` (→ generated), `CMakeLists.txt` (gen invocation) |
| M0.4 | `#pragma weak X = Y` on Darwin | integrator → mechanic | S | — | `src/libultra/gu/sinf.c`, `cosf.c`, `src/game/objective_status.c`, `lv.c`, `spectrum.c`, `port/src/darwin_compat.c` (new) |
| M0.5 | Header/libc clashes on Darwin | mechanic | M | M0.1 | `port/include/pc_protos.h`, `port/src/pc_netorder.c`, `port/shim/*`, `port/src/{romdata,romconvert,pccg,pcmodels,fs}.c`, `port/src/system.c` |
| M0.6 | fast3d C++ TUs vs libc++ header shadowing | mechanic | M | M0.1 | `port/shim/` (new C++-route shims for `math.h`/`limits.h`/`assert.h`), `CMakeLists.txt` |
| M0.7 | macOS branches: `crash.c`, `system.c` | mechanic | M | — | `port/src/crash.c`, `port/src/system.c` |
| M0.8 | macOS stubs for `dram.c`/`romdata.c`/`libultra.c` low-stack (compile-only) | mechanic | S | — | `port/src/dram.c`, `port/src/romdata.c`, `port/src/libultra.c` |
| M0.9 | Link-check + first `findings.md` batch | integrator | S | M0.1–M0.8 | `docs/dev/findings.md` |

**M0.1 — CMake: Apple-clang toolchain path.**
Recipe: (a) detect clang (`CMAKE_C_COMPILER_ID STREQUAL "Clang"` or
`AppleClang`) and in the `if(NOT MINGW)` block at `CMakeLists.txt:289-298`
emit `-fms-extensions -Wno-microsoft-anon-tag` for clang instead of
`-fplan9-extensions`; drop `-fpermissive` for clang; keep the GCC branch
intact. (b) For clang add `-Wno-error=implicit-int
-Wno-error=incompatible-function-pointer-types` next to the existing
`-Wno-error=` set (`:273-277`); do **not** silence `-Wint-to-pointer-cast`,
`-Wpointer-to-int-cast`, `-Wint-conversion` (they are the M2 census).
(c) `add_definitions(-D_FORTIFY_SOURCE=0)` on APPLE (F16: the N64
`sprintf` prototype vs `__builtin___sprintf_chk`). (d) Host-header shim
derivation at `:165-205`: on APPLE derive from
`execute_process(xcrun --show-sdk-path)` / `CMAKE_OSX_SYSROOT` →
`${SDK}/usr/include/{string,sched,stdlib}.h` instead of
`${_PORT_GCC_DIR}/../include`. (e) `:39-45`: set
`CMAKE_OSX_DEPLOYMENT_TARGET 11.0` when `CMAKE_SYSTEM_PROCESSOR` is
`arm64`/`aarch64` (currently keys off empty `CMAKE_OSX_ARCHITECTURES` →
10.13). (f) `find_program(gcc)` at `:27-37` — prefer `clang` on APPLE and
update the comment (it says clang rejects `inherits`; F11 shows it does not
with `-fms-extensions`). (g) `build-pc.sh`: brew line already right; add a
note that `zlib` is keg-only (CMake's `find_package(ZLIB)` finds the SDK
zlib — verify, and if so say brew zlib is optional). Budget: 6 configure/build
cycles. Verify: `cmake -S . -B build-pc -DROMID=ntsc-final` succeeds; `cmake
--build build-pc --target ge007 -- -k 2>&1 | tee /tmp/m01.log`; report the
distinct remaining error classes (expected: F14 asm, F15 pragma weak, F17
implicit decls, libc++ shadowing, `memfd_create`, `crashDumpThreads`) — those
belong to M0.2–M0.8, not this item.

**M0.2 — `__x86_64__` → `PLATFORM_64BIT`.** Sites (from
`grep -rn __x86_64__ src assets include port`): `port/include/pc_protos.h:34`
(the gate for the whole D38 header — critical, F17); `src/memp.c:66`;
`src/music.c:55,688,711`; `src/game/objecthandler.c:7` (D40 — N64 branch
overruns `.bss` by 12 KB on any 64-bit host); `src/game/initunk_005450.c:41`;
`src/game/rsp.c:276`; `src/game/image_bank.c:170`;
`assets/animationtable_data.h:200-575` (D34); `src/audi.c:10,666,734`;
`src/libultra/audio/load.c:26,200,393,496`; `synthesizer.c:23,125`;
`synsetfxmix.c:22`, `synsetpan.c:22`, `synsetvol.c:22`; `src/save.c:24`;
`port/src/system.c:177` (keep — that one really is x86: `_mm_pause`). Recipe:
replace `defined(__x86_64__)` with `defined(PLATFORM_64BIT)` at each listed
site (spelling change only; every site is an existing, documented port-class
edit); ensure `platform.h` is reachable from those TUs (it is included via
`versioninfo.h`/`-include`? — check; if not, `pc_protos.h`-style
`-include` or add `#include "platform.h"` to `port/shim/bondconstants.h`).
`CMakeLists.txt:217-219` already defines `PLATFORM_64BIT=1` for aarch64 via
`cmake/TargetArch.cmake`. Budget: 2 build cycles. Verify: `grep -rn
__x86_64__ src assets include port` shows only `system.c:177`; build of
`bg.c`, `chrai.c`, `propobj.c`, `front.c`, `boss.c` shows **0**
`-Wimplicit-function-declaration` (was 45–63 each).

**M0.3 — Darwin mode for generated assembly.** Recipe: add
`--darwin` (and `--base 0xHEX`, default 0) to `scripts/gen_romassets.py`:
emit `.data` not `.section .data`; prefix every symbol with `_`; emit `.set
_sym, (base + value)` — 64-bit values are fine (F10). Regenerate
`port/src/romassets_u.s` (and `_e`/`_j` if the generator supports them — note
they are currently absent, `CMakeLists.txt:417-420`). Convert
`port/src/dram_syms.s` into generator output (or a tiny Darwin twin selected
by CMake) with the same `--base`. Wire the CMake invocation (pass
`PORT_ADDR_BASE` from a cache var; default 0 in M0). Keep ELF output
byte-identical when `--darwin` is absent (diff the regenerated file against
git). Budget: 4 cycles. Verify: `nm build-pc/CMakeFiles/.../romassets_u.s.o
| head` shows `A`-type `_`-prefixed symbols; `nm ... | grep -c ' A '` = 1685;
`dyld_info -fixups build-pc/ge007.* | grep -c romassets-symbol-name` = 0 once
linked (M0.9).

**M0.4 — `#pragma weak X = Y` on Darwin.** Integrator first (30 min):
read the five sites (`sinf.c:33-34`, `cosf.c:33-34`, `objective_status.c:162`
+ consumer `chrai.c:41,2538`, `lv.c:208-216,603-606`, `spectrum.c:49-50,266`)
and decide per site the minimal semantics-preserving form under
`#if defined(PORT) && defined(__APPLE__)`: for `sinf`/`cosf` a strong
definition in a **new port file** `port/src/darwin_compat.c`
(`float sinf(float x){return __sinf(x);}` …) so the 25 game TUs keep binding
to Rare's polynomial, not libSystem's (fidelity); for the three game-file
aliases, a `PORT`-gated `#define X Y` or wrapper — record the choice as a
`Dxx` (symbol-aliasing mechanism, ABI class). Then mechanic applies it.
Budget: 3 cycles. Verify: `lv.c`, `spectrum.c` compile; link has no
undefined `objectiveGetStatus_WEAK`; `nm build-pc/ge007.* | grep -E
' T _(sinf|cosf)$'` shows both defined in the executable.

**M0.5 — Header/libc clashes.** Recipe: (a) `ntohl/ntohs`: on APPLE
`<sys/_endian.h>` defines them as macros with `__uint32_t` prototypes;
guard the K&R declarations in `pc_protos.h:54-60` and the definitions in
`pc_netorder.c:24-32` with `#if !defined(__APPLE__)` (the game's own macros
are already `#undef`'d in `port/shim/bondconstants.h:37-38`). (b) K&R
redeclarations spelling `size_t` as `unsigned long long`
(`romdata.c:25`, `romconvert.c:28`, `pccg.c:24`, `pcmodels.c:26`,
`fs.c:18`) → `size_t` (identical on all shipping platforms; hard error the
moment the SDK prototype is visible). (c) `system.c:26` `_POSIX_C_SOURCE
199309L` hides `readlink` on Darwin → add `_DARWIN_C_SOURCE` on APPLE.
(d) Anything else M0.1's log lists under this class. Budget: 3 cycles.
Verify: the listed TUs compile warning-free for these classes.

**M0.6 — fast3d C++ vs libc++.** Recipe: the four `port/fast3d/*.cpp` TUs
fail because `include/math.h`, `include/limits.h`, `include/assert.h`
shadow the SDK ahead of libc++'s `<cmath>`/`<climits>`/`<cassert>` (F16).
Add C++-routed shims in `port/shim/` following the existing pattern
(`port/shim/stdlib.h:16-30`: `#ifdef __cplusplus` → host header by absolute
SDK path via the CMake-generated `host*.h`, else N64 header). Alternative if
that fights libc++: compile the four fast3d TUs with a private include order
(`target_include_directories` on an OBJECT library, SDK first). Budget: 5
cycles. Verify: all four `.cpp` TUs compile.

**M0.7 — macOS branches in `crash.c` and `system.c`.** Recipe (crash.c):
change the `PLATFORM_LINUX` block guard (`:306-473`) to `PLATFORM_LINUX ||
PLATFORM_MACOS`; replace the glibc `uc_mcontext.gregs[REG_*]` reads
(`:344-358, 413-417`) with a `PLATFORM_MACOS && PLATFORM_ARM` variant
reading `uc->uc_mcontext->__ss.__pc/__fp/__sp/__lr/__x[]`
(`<mach/arm/_structs.h>`); keep `backtrace`/`backtrace_symbols`/`dladdr`;
ensure `crashDumpThreads` is defined (it is referenced by
`libultra.c:264`); note in the log that symbolication is
`atos -o ge007 -l <slide>` and print `_dyld_get_image_vmaddr_slide(0)` at
crash time. Recipe (system.c): `sysImageBase()` → `(uintptr_t)
_dyld_get_image_header(0)` on APPLE; `sysGetExeDir()` → `_NSGetExecutablePath`
+ `dirname` (or `SDL_GetBasePath`) instead of `"."`; `$S/` resolution at
`:260` (`/proc/self/exe`) → same helper. Budget: 4 cycles. Verify: both TUs
compile; `nm | grep crashDumpThreads` defined once.

**M0.8 — compile-only macOS stubs (address model lands in M1).** Recipe:
`dram.c`: add a `PLATFORM_MACOS` branch that compiles (`shm_open` skeleton
per F9, addresses still `0x70000000`/`0x80000000` — will fail at runtime,
expected); `romdata.c`: `MAP_FIXED_NOREPLACE` path already `#ifdef`'d — no
change unless the log says otherwise; `libultra.c:417-438`
`portAllocLowStack`: compiles as-is (`MAP_32BIT` exists). Budget: 2 cycles.
Verify: link completes.

**M0.9 — link-check + findings batch (integrator).** Run the AGENTS.md
verification ritual (`/linkcheck`): undefined/duplicate symbols across the
compiled set; record M0's findings as one `findings.md` batch (F11–F17 each
get a `Dxx` row or are folded into one "macOS toolchain" row) and update
`docs/building.md` "macOS: compiles, does not run yet (address model: M1)".

### Phase M1 — the address model in `port/`

Goal: the macOS binary boots to the first frame with `B ≠ 0`, with the
game-code census still pending (expect crashes at un-rewritten cast sites —
those are M2). All items here are `port/`-only.

| ID | Title | Who | Size | Deps | Files |
|---|---|---|---|---|---|
| M1.1 | Fix `B`; write `port_addr.h` + `portAddrInit()` | integrator | S | M0.9 | `port/include/port_addr.h` (new), `port/src/main.c`, `CMakeLists.txt` |
| M1.2 | Rewire OS chokepoints | mechanic | M | M1.1 | `port/src/libultra.c`, `port/shim/PR/os.h`, `port/shim/PR/R4300.h`, `port/shim/R4300.h`, `port/src/n64stubs.c` |
| M1.3 | DRAM double-map + cart map + stacks inside `W` | mechanic | M | M1.1 | `port/src/dram.c`, `port/src/romdata.c`, `port/src/libultra.c` (`portAllocLowStack`) |
| M1.4 | fast3d `seg_addr` / classifier / moveword on `B` | mechanic | M | M1.1 | `port/fast3d/gfx_pc.cpp` |
| M1.5 | Mixer + PI DMA + validity helpers on `B` | mechanic | S | M1.1 | `port/src/mixer.c`, `port/src/libultra.c` (`piServiceDma`, `dramHostAddrValid`), `port/src/romdata.c` (`romdataIsCartAddr`) |
| M1.6 | Absolute symbols regenerated with `B` | mechanic | S | M0.3, M1.1 | generated `.s` via CMake var |
| M1.7 | Boot attempt + triage | integrator | M | M1.2–M1.6 | — |

**M1.1 (integrator).** Confirm `B` on a second Mac if available (run the
F7 probe: reserve 4 GiB `PROT_NONE` at candidates via `mach_vm_map
VM_FLAGS_FIXED`); fix `PORT_ADDR_BASE = 0x1000_0000_0000` (fallback
`0x100_0000_0000`). Write `port_addr.h` per §2.4 with `portImageBase()`
(`_dyld_get_image_header(0)` on macOS; `sysImageBase()` elsewhere — on
Windows this must equal `0x140000000` so the image-relative rule reproduces
D131 exactly; assert it). `portAddrInit()` as §2.3, called first in
`main()`; on non-macOS it is a no-op. Expose `PORT_ADDR_BASE` to CMake
(`-DPORT_ADDR_BASE=` cache var, APPLE default the constant, else 0) so M1.6
can feed the generator; add a `_Static_assert` tying the two.

**M1.2 (mechanic).** Recipe: `osVirtualToPhysical` → `portHostToN64`,
`osPhysicalToVirtual` → `portN64ToHost` (`libultra.c:1442-1443`);
`OS_K0_TO_PHYSICAL(x)` → `(u32)((uintptr_t)(x) - (PORT_ADDR_BASE +
0x70000000))`, `OS_PHYSICAL_TO_K0(x)` → `portN64ToHost(x)`
(`port/shim/PR/os.h:34,37`); `PHYS_TO_K0` likewise (`port/shim/PR/R4300.h:25`,
`port/shim/R4300.h:14`); `n64stubs.c:94-107` `0x70700000` literal →
`portN64ToHost(0x70700000)`. Budget: 3 cycles. Verify: Windows/Linux
semantics unchanged is provable by inspection (`B = 0` ⇒ identity) — state
it; macOS builds.

**M1.3 (mechanic).** Recipe: `dram.c` macOS branch: `shm_open` (unique
name, `shm_unlink` immediately) + `ftruncate(8 MiB)` + two
`mmap(MAP_SHARED|MAP_FIXED)` at `B+0x70000000` and `B+0x80000000` (F9); fatal
on any failure (no fallback). `romdata.c`: on macOS `mmap(MAP_FIXED)` at
`B+CART_BASE` (safe post-reservation), fatal on failure — do **not** take the
heap-copy path on macOS (comment why: window invariant). `libultra.c`
`portAllocLowStack`: macOS branch carves 8 MiB + 16 KiB guard per thread from
`[B+0xA0000000, B+0xC0000000)` with a simple bump allocator + free list;
`pthread_attr_setstack` as today. Budget: 4 cycles. Verify: a debug log line
per mapping with the host address; run `build-pc/ge007 -level_09` far enough
to see "DRAM V1 @0x1000_7000_0000 … cart @ … stacks @ …" (crash afterwards is
expected in M1).

**M1.4 (mechanic).** Recipe (`gfx_pc.cpp`): `seg_addr` (`:2881-2935`) —
the `w1 < 0x800000` branch returns `portN64ToHost(w1 + 0x80000000)`; the
D131 `mod_hi` block is replaced by the image-relative rule (`0x40000000 ≤ w1
< 0x70000000` → `portN64ToHost(w1)`); the final pass-through stays for
full-width host pointers but a `w1 < 0x1_0000_0000` value there means
"in-window N64 address" → `portN64ToHost`. `G_MW_SEGMENT` (`:2324-2327`)
and `G_MW_NUMLIGHT` (`:2317`) same treatment. Range classifier
(`:630-635`): compare `(uintptr_t)p - PORT_ADDR_BASE` against the cart and
DRAM windows. `fast3d_ptr_ok`/`addr_bad` bounds: unchanged (16 TiB <
128 TiB). Budget: 3 cycles. Verify: builds; on Windows semantics unchanged
(identity) — state the argument per site.

**M1.5 (mechanic).** `mixer.c:147,152,297` already call
`osPhysicalToVirtual` → covered by M1.2; check `mixer.h`'s `u32 dramAddr`
ABI needs nothing. `piServiceDma` (`libultra.c:1001`): `memcpy` source from
`portN64ToHost(srcPA)`. `dramHostAddrValid` (`:909-911`) and
`romdataIsCartAddr` (`romdata.c:441-451`): ranges offset by `B`. Budget: 2
cycles.

**M1.7 (integrator).** Boot `-level_09` (the standard sweep level); triage
the first crash against the census (M2.1), which by now exists. Expect the
first failures at `(T *)u32field` sites in game code.

### Phase M2 — game-code cast census and rewrites (the D3x class)

| ID | Title | Who | Size | Deps | Files |
|---|---|---|---|---|---|
| M2.1 | Compiler census | mechanic | S | M0.9 | none (report only) |
| M2.2 | Classify census → work packets by file | integrator | M | M2.1 | `docs/dev/MACOS-ARM64-PLAN.md` (this doc, §3 table) |
| M2.3a–n | Rewrite packets (one per file group, disjoint) | mechanic ×N (parallel) | S–M each | M2.2 | per packet |
| M2.4 | Re-run census; assert zero unclassified | mechanic | S | M2.3 | — |

**M2.1 — census (mechanic).** Recipe: `cmake --build build-pc -- -k 2>&1 |
tee /tmp/census.log`; extract `grep -E 'warning: .*\[-W(int-to-pointer-cast|
pointer-to-int-cast|int-conversion)\]' /tmp/census.log | sort -u` into
`/tmp/census.txt`; also grep `src/ assets/` for `(u32)&`, `(s32)&`,
`romptr_t`, `PORT_PTRADD`; produce a table `file:line | direction (u32→ptr /
ptr→u32 / implicit) | operand kind (field / &global / local / call result)`.
No edits. Budget: 45 min.

**M2.2 — classify (integrator).** Per site decide: **(a)** `u32 → pointer`
where the `u32` holds an N64 address (DRAM/cart/stack) →
`PORT_N64PTR(T, expr)`; **(b)** `&global → u32` (image pointer) →
`portHostToN64(&sym)` (identical value on Windows); **(c)** in-window
`pointer → u32` truncation → leave; **(d)** ROM-serialized field (D3x) →
already handled or `PORT_PTRADD`/`romptr_t` pattern; **(e)** genuine
integer (not an address) → leave. Group (a)+(b) into packets by file so
mechanics never collide (dev-process.md §3); each packet lists exact
`file:line` sites and the replacement. Record the class counts in this doc.

**M2.3 packets (mechanic, parallel).** Per packet brief: FILES = the
packet's files only; recipe = the per-site replacement table; every edit is
one of the two macro forms — no other change; add a one-line `/* D3x/M2:
window-model cast, identity at PORT_ADDR_BASE==0 */`-style comment only
where the file has no adjacent D3x comment already. Budget: 2 cycles per
packet. Verify: packet TUs compile; census diff shows the packet's sites
gone; `./build-pc.sh ntsc-final` green.

### Phase M3 — runtime bring-up

| ID | Title | Who | Size | Deps | Files |
|---|---|---|---|---|---|
| M3.1 | Cocoa main-thread hygiene | mechanic | M | M0 | `port/fast3d/gfx_sdl2.cpp`, `port/src/video.c`, `port/src/input.c` |
| M3.2 | Two Core-profile GL bugs + MSAA clamp | mechanic | S | M0 | `port/fast3d/gfx_opengl.cpp`, `port/src/video.c` |
| M3.3 | First frame → intro → menu → `-level_09` | integrator | L | M1.7, M2.4, M3.1 | triage only |
| M3.4 | arm64 float→int semantics check | integrator | M | M3.3 | `docs/dev/findings.md` |
| M3.5 | Level sweep (21 levels, unattended window) | mechanic (runs) + integrator (triage) | L | M3.3 | `docs/dev/LEVEL-STATUS.md` |
| M3.6 | Weak-memory-ordering audit of cross-thread flags | integrator | M | M3.3 | `port/src/video.c`, `optionsoverlay.c`, `libultra.c` |

**M3.1 (mechanic).** Recipe: remove the render-thread `SDL_PollEvent` pump
(`gfx_sdl2.cpp:311-360`, called from `gfx_pc.cpp:3301`) — the host thread
already pumps (`video.c:502-591`); route Alt+Enter through
`videoRequestFullscreen` (`video.c:298-309`); replace the render-thread
`exit(0)` calls (`gfx_sdl2.cpp:322/348/356`) with a quit request the host
loop honours. Marshal `SDL_SetRelativeMouseMode`/`SDL_ShowCursor`
(`input.c:1227,1256,1310,1313`, reached from render + game threads) onto the
host thread via the existing `winReq*` queue. `SDL_GameControllerUpdate`
(`input.c:622`, game thread) — move to the host pump, snapshot state under
the existing lock. Budget: 5 cycles (Windows/Linux behaviour must be
unchanged — this is a correctness fix everywhere). Verify: builds; on macOS
no `NSInternalInconsistencyException`/main-thread assertions in the log once
M3.3 boots.

**M3.2 (mechanic).** `gfx_opengl.cpp:1271` binds the vector *index* instead
of `.fbo` (cf. correct `:1335`) — fix; `:1328` `glReadBuffer(GL_FRONT)` when
`use_back=false` (`video.c:722`) is undefined on Cocoa → read the back buffer
or the FBO; clamp `gfx_msaa_level` to `GL_MAX_SAMPLES` before `:1187/:1199`.
Budget: 2 cycles.

**M3.4 (integrator).** arm64 `fcvtzs` **saturates** and yields 0 for NaN;
x86 `cvttss2si` yields `0x80000000` for overflow and NaN; MIPS yields
`0x7FFFFFFF`. Sites validated against x86 behaviour: `floorFloatToInt`
(`src/game/math_floor.c:42`, 19 call sites), `FTOFIX32` in `guMtxF2L`
(`include/PR/gu.h:38` — also `long` = 64-bit on LP64), D73/D155/D156 guards
(`src/game/model.c`). Re-read each; where behaviour differs for
out-of-range/NaN inputs, decide whether a port-side helper is needed to
match the x86-validated behaviour (a `port/` change; no game-code edit).
Record as a `Dxx`.

### Phase M4 — packaging, CI, docs

| ID | Title | Who | Size | Files |
|---|---|---|---|---|
| M4.1 | `.app` bundle target + `Info.plist` + `$S/` under `~/Library/Application Support/ge007` when bundled | mechanic | M | `CMakeLists.txt`, `port/src/system.c`, `port/src/romconvert.c` |
| M4.2 | GitHub Actions `macos-14` (arm64) compile job (mirror of the Ubuntu job) | mechanic | S | `.github/workflows/ci.yml` |
| M4.3 | `docs/building.md` macOS section; README "Status"; AGENTS.md build note (currently says MSYS2 only) | mechanic | S | docs |
| M4.4 | Bundle script (`bundle-mac.sh`, mirrors `bundle-win.sh`; reuse `prepare-assets.py`) | mechanic | M | scripts |

---

## 4. Decisions needed from a human

1. **`B` value** — `0x1000_0000_0000` proposed (F7). Needs one more machine's
   probe before M1.1 fixes it. If no second Mac: accept the fatal-on-failure
   contract and revisit if a user report shows the range occupied.
2. **`#pragma weak` handling (M0.4)** touches three game files with a
   `PORT`-gated alias/wrapper. Sanctioned as ABI/linker-mechanism class?
   (Recommendation: yes, documented as one `Dxx`.)
3. **Heap-copy fallback on macOS = fatal** (§2.3). Confirms we never run
   degraded on macOS; a Mac that cannot reserve 4 GiB `PROT_NONE` at `B`
   cannot run the port. (Recommendation: yes; it costs nothing — `PROT_NONE`
   reservations are free.)
4. **M3.1 changes Windows/Linux behaviour too** (removing the render-thread
   event pump). Recommendation: yes — it is an SDL-rules violation on every
   platform; verify Windows once after.

## 5. Risks (ranked)

1. **Census size (M2).** If the compiler census exceeds a few hundred
   `u32 → pointer` sites, M2 becomes the dominant cost. Mitigation: the
   rewrites are mechanical and file-partitioned; run 4–6 mechanics in parallel.
2. **Game code keeping host pointers in `u32` fields *implicitly*** (no cast,
   `-Wint-conversion`): each is a latent truncation today too; the census
   catches them, but classification needs judgement (integrator time).
3. **Pointers reaching game code from host allocations outside `W`** (e.g. a
   port-side `malloc` result handed into a game struct) — invisible to the
   census. Mitigation: M1.7/M3.3 triage; `dramHostAddrValid`-style debug
   assert in `portHostToN64` (env-gated) that logs any out-of-window,
   out-of-image pointer.
4. **arm64 float→int and weak memory ordering** (M3.4, M3.6): subtle,
   non-crashing divergences; needs framediff against the Windows reference.
5. **SDL2 on macOS is `sdl2-compat` over SDL3** (brew default): behaviour
   differences in relative mouse mode / HiDPI are possible; pin to real SDL2
   (`brew install sdl2` gives compat; real SDL2 is `sdl2@2`? — verify in M0.1).
6. **`fast3d_ptr_ok` 47-bit bound** and any other `< 0x8000_0000_0000`
   checks are fine at 16 TiB but would not be at ≥128 TiB — do not raise `B`
   above that without checking.

## 6. Status

| Phase | State | Notes |
|---|---|---|
| Research | done | §1 F1–F18, measured 2026-09-18 |
| M0 | **superseded by #88** | the clang build/toolchain work is replaced by PR #88's GCC-based macOS layer; only D325 + `gen_macho_syms --base` survive (§0b) |
| M1 | **done** | shifted-window address model (D327); self-test ALL PASS on the #88 base |
| M2 | **done** | game-code re-basing sweep complete; **21/21 solo levels boot, render and run crash-free**, including with full controller input |
| M3 | **done (core)** | #88's AppKit main-thread guard; `tools_pc/level_sweep_mac.sh` (crash + rendered-pixel check); the two Core-profile GL nits are cosmetic and parked in `docs/dev/GRAPHICS-BACKLOG.md` |
| M4 | **done (core)** | `tools_pc/bundle-mac.sh` → signed, double-clickable `GoldenEye.app` (+ zip/sha256); `docs/building.md` macOS section + `tools_pc/README.md`. Outstanding: CI for macOS, and a live run of the `.app` from Finder |

**Verified state (2026-09-18).** `build-pc/ge007.aarch64` (Homebrew GCC 16.2)
opens a window, renders, plays music **and sound effects**, and runs every solo
level crash-free:

- `tools_pc/level_sweep_mac.sh` — 21/21, all with non-clear pixels
  (76–92%).
- Full-control sweep (`GE_INPUTSCRIPT`: every button + 8-way stick, 26 s per
  level) — 21/21, with SFX voices allocating on every level.
- Findings D323–D329; D328 (silent SFX) and D329 (stage-unload truncation)
  were both arm64-specific and are fixed.

### Prerequisites to run (easy to miss)

Two things must be in `data/` before the binary will render anything, both
documented in `docs/building.md`:

1. **The ROM** as `data/ge007.ntsc-final.z64` (SHA-1 `abe01e4a…`, matching the
   repo's `ge007.u.sha1`).
2. **The PC asset sidecars** — `data/pcmodels-ntsc-final/` and
   `data/pccg-ntsc-final/`. These are ROM-derived and **gitignored**, so a
   fresh clone has empty directories and the game will look like it has an
   address bug when it is really serving raw big-endian ROM bytes (that is
   finding **D69** reproducing). Generate them once:

   ```sh
   python3 tools_pc/d43_emit.py ntsc-final          # 512 model sidecars
   python3 tools_pc/d69_emit.py ntsc-final          # 25 bg + 27 stan
   python3 tools_pc/d88_emit.py ntsc-final --regen  # + 21 stage-setup
   ```

   This cost a detour during M2: the `bg.c` big-endian-header crash looked like
   an un-rebased pointer but was the missing sidecars. PR #88's author ran the
   same three passes before their 1,500-frame `-level_09` run.

### Where the boot is (2026-09-18, branch `macos-arm64-gcc`)

`./build-pc/ge007.aarch64 -level_09`, ROM `data/ge007.ntsc-final.z64`
(SHA-1 `abe01e4a…`, matching the repo's `ge007.u.sha1`):

- ROM load, config, EEPROM, GL 4.1 core, audio, input, threads, VI, timers — OK
- language banks, animation tables, `texReset`/`texLoad`, `gimgSync…` — OK
- faults in `bg.c` `load_bg_file` (level geometry) — the next M2 site

M2 batches so far (all `#if defined(PORT)` and identity at
`PORT_ADDR_BASE == 0`): the mempool boundary; `language.c` `g_LangBanks`;
`initanitable.c` `expand_ani_table_entries`; `initactorpropstuff.c` anim
groups / `ANIM_PTR`; `model.c` `bitDescriptors`/`bitStream`; `image_bank.c`
`globalbank_rdram_offset` (structural, ~43 sites at once) + `texSetBitstring`;
`image.c` `texLoadFromDisplayList`; `bg.c` `BG_SEG_TO_PTR`/`ptr_bg_data`
(**under review** — the fold's value depends on the stack address, which
differs between Windows and the macOS window, so it needs a careful look).

### The M2 census is a checklist, not a scoreboard

The clang census is ~474 game-code rows (`-Wint-conversion` 253,
`-Wpointer-to-int-cast` 122, `-Wint-to-pointer-cast` 54,
`-Wint-to-void-pointer-cast` 35, `-Wvoid-pointer-to-int-cast` 10). Re-basing
edits *add* explicit `u32 → pointer` conversions, so the raw count can rise
while progress is made, and one structural fix can clear dozens of rows. The
honest progress metric is boot-path reach.

### Next

1. Finish the `bg.c` level-geometry re-basing (the current fault), then keep
   triaging `-level_09` until a frame renders.
2. Refresh §1/§3 for the #88/GCC base (the M0 rows elsewhere in this doc are
   historical).
3. M3: the two Core-profile GL bugs (`gfx_opengl.cpp:1271` binds the FBO
   index instead of `.fbo`; `glReadBuffer(GL_FRONT)`), the MSAA clamp, then
   the level sweep and the arm64 float→int / memory-ordering checks.
