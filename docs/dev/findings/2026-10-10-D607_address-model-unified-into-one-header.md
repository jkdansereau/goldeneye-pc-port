# D607

<!-- from the old section-F/H table row -->
| D607 | **Address model unified into one header: D441's `portaddr.h` arena macros + #95's `port_addr.h` window model (`PORT_ADDR_BASE`, `portN64ToHost`, image-relative encoding), with #95's `portAddrIsInWindow` folded into D573's `portAddrIsMapped`** — full `## D607` entry at file tail | DONE 2026-10-07 (macos branch; Windows identity gate: build + dam golden PASS; Mac `as` build owed) |

## D607: Address model unified into one header (D441 `portaddr.h` + the #95 `port_addr.h`) — DONE 2026-10-07 (macos branch)

*Labelled D579 on the macos branch; renumbered at the v0.5.1 merge (the interpolation work had used D579-D585).*

The macOS arm64 window model from #95 (danturn) and the D441/D573 DRAM-arena
macros were two headers describing the same address space in different
dialects (`PORT_ADDR_BASE` + image-relative `portN64ToHost` vs
`PORT_DRAM_V1_BASE` + `portAddrIsMapped`). They are now ONE header,
`port/include/portaddr.h` (the D441 name; #95's `port_addr.h` is
superseded — re-applying #95 is a mechanical `#include` rename plus
folding its two `portAddrIsInWindow` guard sites into `portAddrIsMapped`,
which is the same check with the same call sites, src/snd.c +
src/game/language.c):

- `PORT_ADDR_BASE`: 0 on Windows/Linux/x86_64 macOS, `0x100000000000`
  (16 TiB) on `__APPLE__ && __aarch64__` (native preprocessor macros,
  mirroring platform.h's PLATFORM_MACOS/PLATFORM_ARM; the header stays
  self-contained — no platform.h include, so it survives the asm
  prepass and C++ TUs alike). `#ifndef`-guarded, overridable by -D
  (the D441 high-arena flavour, ASan LowMem bases, the #95
  `PORT_ADDR_BASE` cache var).
- `PORT_DRAM_V1_BASE`/`PORT_DRAM_K0_BASE` are now `PORT_ADDR_BASE +
  0x70000000UL` / `+ 0x80000000UL` (still `-D`-overridable): bit-identical
  to the pre-unification literals on x86_64 (base 0), and one-line shifts
  for arm64.
- `portN64ToHost`/`portHostToN64` + `PORT_N64PTR` (from #95) declared
  here; their `port_addr.c` implementation and `g_portImageBase` /
  `g_portUseImageRel` / `portAddrInit` land with the #95
  re-application. Identity at base 0: the image-relative path
  round-trips the exact truncation D131 fixed.
- CMake (Apple branch): the `dram_syms.S` prepass now needs NO
  `-DPORT_DRAM_V1_BASE` default (the header self-detects; only the
  GE_HIGHARENA_BASE flavour passes an override). `gen_macho_syms.py`
  also strips C-style integer suffixes (`UL`, `ULL`, ...) from `.set`
  expressions, so Apple's `as` never parses them.

**Identity gate (Windows, x86_64, base 0):** build 218/218 rc 0;
`tools_pc/verify.sh dam` PASS (worst_cell 1.15 < 3.0). Full 21-level
sweep + 60-second level_09 run + Linux X220 build: owed with Task 4.
**Mac gate:** `gcc -E -D__APPLE__=1 -D__aarch64__=1` on `dram_syms.S`
emits `(0x100000000000 + 0x70000000)` `.set` lines that
`gen_macho_syms.py` normalises to clean Mach-O; the real Apple `as`
build on the maintainer's Mac is the outstanding proof.
| D607 | **Address model unified into one header: D441's `portaddr.h` arena macros + #95's `port_addr.h` window model (`PORT_ADDR_BASE`, `portN64ToHost`, image-relative encoding), with #95's `portAddrIsInWindow` folded into D573's `portAddrIsMapped`** — full `## D607` entry at file tail | DONE 2026-10-07 (macos branch; Windows identity gate: build + dam golden PASS; Mac `as` build owed) |
