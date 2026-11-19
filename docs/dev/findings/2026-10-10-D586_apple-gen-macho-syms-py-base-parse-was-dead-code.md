# D586

<!-- from the old section-F/H table row -->
| D586 | **Apple `gen_macho_syms.py --base` parse was dead code (undefined `args`), the shifted literals emitted bare (assembler "expected newline"), and the generated `.s` dir missing on a fresh macOS build dir — arm64 macOS build now completes and boots (self-test ALL PASS, 600+ frames, no SIGSEGV)** — full `## D586` entry at file tail | FIXED 2026-10-07 (macos branch) |

## D586: Apple `gen_macho_syms.py --base` parse was dead code (referenced an undefined `args`) and the generated `.s` dir is missing on a fresh macOS build dir — FIXED 2026-10-07 (macos branch)

Three build-layer fixes for the Apple path: (a) the `--base` branch in
`scripts/gen_macho_syms.py` referenced `args` — the script has no argparse;
replaced with a manual `sys.argv` parse (`base = int(argv[1], 0)`); (b) the
`--base`-shifted literals were emitted bare (`10001049A390` — the assembler
reads it as invalid, "expected newline"); now `0x`-prefixed; (c) the
`cc -E -o` custom commands for `romassets_*_macho.s` / `dram_syms` fail on a
fresh macOS build dir because `build-pc/port/src` doesn't exist yet — added
`file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/port/src")` before the Apple
custom commands. `dram_syms` is NOT `--base`d (it includes `portaddr.h` and
uses `PORT_DRAM_V1_BASE + offset`, preprocessed); `romassets_u.s`'s 1762 raw
cart-address `.set` lines are. Result: the arm64 macOS build completes and
boots — self-test ALL PASS, ROM mapped, 600+ frames rendered, no SIGSEGV
(the D612 ladder's endpoint cleared by D613's re-application).
