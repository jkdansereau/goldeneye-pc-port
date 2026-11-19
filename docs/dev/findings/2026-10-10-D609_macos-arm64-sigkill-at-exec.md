# D609

<!-- from the old section-F/H table row -->
| D609 | **macOS arm64 SIGKILL at exec: `-Wl,-pagezero_size,0x10000` (#88, x86_64 rationale) violates the kernel's 4 GiB minimum `__PAGEZERO` on Apple Silicon; plus the deployment-target floor probe is blind on macOS (`CMAKE_HOST_SYSTEM_PROCESSOR` empty there)** — full `## D609` entry at file tail | FIXED 2026-10-07 (macos branch; probe-confirmed; arm64 keeps the 4 GiB pagezero, floor 11.0) |

## D609: macOS arm64 SIGKILL at exec — the `-pagezero_size` link flag is x86_64-only (and the deployment-target floor probe is blind on macOS) — FIXED 2026-10-07 (macos branch)

*Labelled D581 on the macos branch; renumbered at the v0.5.1 merge (the interpolation work had used D579-D585).*

The first arm64 Mac binary was killed ~0.3 s after launch with no output and
no port-side crash log: `rc=137` (SIGKILL) from the shell, before `main`
rans. Cause: the CMake APPLE branch applied `-Wl,-pagezero_size,0x10000`
unconditionally (inherited from #88, whose rationale is explicitly *x86_64*
macOS — the 4 GiB `__PAGEZERO` default wastes virtual address space for the
low-arena model). On Apple Silicon the kernel enforces a **4 GiB minimum**
`__PAGEZERO` (the SVE ABI constraint); a smaller value makes `execve` fail
outright, so nothing runs — not dyld, not the port's `crashHandler`, hence
no log line. Reproduced in isolation: a six-line `printf` program linked
with `-Wl,-pagezero_size,0x10000` exits 137 on the Mac, and the same
program with the default pagezero runs. Fix: gate the flag to non-arm64
Apple (`TARGET_ARCH` match `^aarch64$` keeps the default 4 GiB), which is
also what the low-arena-vs-`PORT_ADDR_BASE` decision in D607 already keys
on.

Same block, second macOS-only trap: the deployment-target floor was chosen
from `CMAKE_OSX_ARCHITECTURES`, which is **empty** in this build (the build
targets the host arch), so an arm64 Mac took the 10.13 (Intel) floor and
`ld` warned that the brew-gcc aarch64 runtime objects and the generated
`.s` objects carry a newer floor than the linked one. CMake's own
`CMAKE_HOST_SYSTEM_PROCESSOR` is *also* empty on macOS — its `uname -p`
probe returns `unknown` there, the same class of blind arch probe as D515's
stripped-env `PROCESSOR_ARCHITECTURE` — so the block now asks `uname -m`
directly (bare name: there is no `/bin/uname` on macOS, and
`execute_process` looks the name up in PATH) and takes the 11.0 (Big Sur,
first arm64 release) floor on `aarch64`. Cosmetic (warnings only), but it
keeps the floor honest for the shipped Mac bundle.
