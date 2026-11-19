# D402

<!-- from the old section-F/H table row -->
| D402 | **First clean Linux (glibc / SteamOS) build of `release/v0.4.0` broke in two port-layer include-resolution spots MinGW tolerated (`bondconstants.h` first-include cycle; `video.c` `_SHIFTL`); fixed in `port/shim/` only, no `src/` edits, no rule-2 (2026-09-27).** Full `## D402` entry at file tail. | RESOLVED (Linux/Ubuntu-24.04 build → glibc-2.41-compatible binary for the Steam Deck): three `port/shim/` fixes — `PR/ucode.h` + `bondconstants.h` defer `pc_protos.h`/`bondtypes.h` during `bondconstants.h`'s own `ultra64.h` pass (breaks the `bondconstants.h`→`ultra64.h`→`pc_protos.h`→`bondtypes.h`→`bondconstants.h` re-entry); `PR/gbi.h` defines `_SHIFTL`/`_SHIFTR` when absent (the `gbi.h` macros in `video.c` lost their `mbi.h` definitions on Linux). Builds + links clean on Ubuntu-24.04; bundled `libSDL2` with `rpath=$ORIGIN`; pushed to the Deck. Shim files uncommitted at log time. |

## D402 — Linux/Steam-Deck build broken: `bondconstants.h`-first TUs re-enter `bondtypes.h` from a half-parsed `bondconstants.h`, and `video.c` loses the `_SHIFTL` gbi macro (found 2026-09-28, building `release/v0.4.0` on Ubuntu 24.04 for the v0.4.0 Deck pre-release).

**Context.** Every v0.4.0 wave item since the modern-options work was validated on Windows/MinGW only. The first clean Linux (glibc GCC) compile of `release/v0.4.0` surfaced two port-layer build breaks, both include-resolution issues that MinGW happened to tolerate.

**Break 1 — half-parsed `bondconstants.h` (header include cycle).**
`src/bondconstants.h` includes `<ultra64.h>` at its **top** (line 16), before it defines `ITEM_IDS`/`ACT_TYPE`/`DIFFICULTY`/`TICKOP`/… (those land from ~line 4100 on). On PC, `ultra64.h` → `port/shim/PR/ucode.h` → `pc_protos.h` → `bondtypes.h` (whose own line-28 `#include <bondconstants.h>` normally pulls the *complete* constants header). A TU that includes `<bondconstants.h>` **first** — `port/src/optionsoverlay.c` (and any sibling port TU with the same shape) — sets `src/bondconstants.h`'s include-guard, then that header's top `#include <ultra64.h>` re-enters the `pc_protos.h`→`bondtypes.h`→`bondconstants.h` chain while `src/bondconstants.h` is still mid-parse (guard already set → body skipped). `src/bondtypes.h` then needs `ITEM_IDS`/`ACT_TYPE`/`CHRFLAG`/`DROPTYPE`/`AMMOTYPE`/`DIFFICULTY`/`MPSCENARIOS`/… which are not yet defined:
```
src/bondtypes.h:1722:9: error: unknown type name ‘ITEM_IDS’
… :2445 ACT_TYPE, :2457 CHRFLAG, :2621 DROPTYPE, :3230 AMMOTYPE, :3485 DIFFICULTY
port/include/pc_protos.h:219 ITEM_IDS, :260 MPSCENARIOS, :359 TICKOP
```
`-H` on `optionsoverlay.c` shows the exact cycle: `. shim/bondconstants.h → .. src/bondconstants.h → … ultra64.h → pc_protos.h → shim/bondtypes.h → chrobjdata.h → shim/bondconstants.h (guard set, body skipped) → src/bondtypes.h:1722` error. The natural `ultra64.h`-first path (the common N64 TU shape) is unaffected: `pc_protos.h`→`bondtypes.h`→`bondconstants.h` reaches `src/bondconstants.h` *fresh* and fully parses it before any type is needed.

**Break 2 — `video.c` `_SHIFTL` undefined (gbi macro without mbi.h).**
`port/src/video.c` includes `<PR/gbi.h>` (whose `gDP*` DL macros expand to `_SHIFTL(...)`/`_SHIFTR(...)`) but never reaches `PR/mbi.h` (where `_SHIFTL` is `#define`d) on Linux — no header in its include chain pulls it in, so the `gDPSetCombineLERP` in `portCrosshairApplyTintCombine` (crosshair tint, M2 wave D373/D379) compiles as an *implicit `_SHIFTL()` function call* and fails at **link**: `undefined reference to '_SHIFTL'` from `video.c:131`. `port/src/optionsoverlay.c` already worked around this locally (it defines `_SHIFTL`/`_SHIFTR` before its `gbi.h` include); `video.c` did not. MinGW reaches `mbi.h` through a different include path for these TUs, so it linked; glibc does not.

**Fix (port-layer only, zero `src/` edits; D402):**
1. `port/shim/bondconstants.h` (PC branch): `#define _PORT_DEFER_PC_PROTOS`, `#include <ultra64.h>`, `#undef` — i.e. pre-parse the PR base headers *first* while telling `PR/ucode.h` to **defer** `pc_protos.h` for that pass; then `#include "src/bondconstants.h"` (completes all the constants; its own top `ultra64.h` is now a no-op); then `#include "pc_protos.h"` so the D38 prototypes are still pulled in exactly once, *after* every type is defined.
2. `port/shim/PR/ucode.h`: wrap `#include "pc_protos.h"` in `#if !defined(_PORT_DEFER_PC_PROTOS)`, so it is skipped only during the bondconstants-defer pass and runs normally for every other `ultra64.h` consumer.
3. `port/shim/PR/gbi.h` (PC branch): after `#define Gfx Gfx_le`, `#ifndef _SHIFTL / #define _SHIFTL(v,s,w)…`, `#ifndef _SHIFTR / #define _SHIFTR(v,s,w)…` (verbatim from `mbi.h`), guarded so a TU that also reaches `mbi.h` keeps `mbi.h`'s definitions. Fixes `video.c` and protects any future `<PR/gbi.h>`-only TU.

**Verified (Ubuntu 24.04 / GCC, WSL):** full `cmake -S . -B build-pc -DROMID=ntsc-final && cmake --build build-pc -j16` of the freshly cloned branch compiles and **links** `build-pc/ge007.x86_64` (ELF x86-64, links `libSDL2-2.0`) with zero errors after the fix; the pre-fix build fails exactly on the two errors above. Windows/MinGW build unaffected (the `#ifndef`/`#if !defined` guards make both fixes no-ops where the macros are already provided).

**Landed** (see commit). **Lever:** this is the first clean Linux compile of the v0.4.0 branch; CI's `linux-build` job should now pass. The Deck pre-release test bundle (exe + bundled `libSDL2`, `rpath=$ORIGIN`) is produced from this build — note `tools_pc/bundle-linux.sh` still requires the CI-frozen `prepare-assets/ge007-convert` for the *drop-in-ROM* first-run path; a Deck running from an existing `data/` (ROM + pre-generated sidecars) never invokes it.
