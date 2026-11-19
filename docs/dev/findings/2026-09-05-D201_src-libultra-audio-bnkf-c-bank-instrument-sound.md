# D201

<!-- from the old section-F/H table row -->
| D201 | **`src/libultra/audio/bnkf.c` bank/instrument/sound/wavetable relocation offsets were `s32`, truncating the real 64-bit base pointer.** — full `## D201` entry at file tail | FIXED (`src/libultra/audio/bnkf.c`) — part of the Phase-3 mixer landing, not yet build/runtime-verified in isolation |

## D201 — `src/libultra/audio/bnkf.c` bank/instrument/sound/wavetable relocation offsets were `s32`, truncating the real 64-bit base pointer.

**`src/libultra/audio/bnkf.c` bank/instrument/sound/wavetable relocation offsets were `s32`, truncating the real 64-bit base pointer.** `alSeqFileNew`/`alBnkfNew` compute `offset = (s32) base` from a heap pointer and thread it through `_bnkfPatchBank/Inst/Sound/WaveTable` as `s32 offset, table` parameters, used to rebase every embedded pointer field in a loaded bank/inst/sound/wavetable/book/loop record. On N64 (32-bit pointers) the truncation is a no-op; on PC, sign-extension of the truncated `s32` during the rebase arithmetic reconstructs the wrong 64-bit address whenever the base allocation's low 32 bits have the top bit set (~50% of allocations, allocator-dependent) — corrupting every relocated pointer in the bank file, a plausible contributor to bad model/audio pointers surfacing downstream (D191-family symptoms are a node-tree walk, not this, but the class is the same). **Fix (ABI-layer, no logic change):** widen `offset`/`table`/`woffset` and all four `_bnkfPatch*` parameters from `s32` to `uintptr_t` (`<stdint.h>`) — already this codebase's idiom for pointer-safe integers in `src/game/*.c`. No behavior change on the 32-bit N64 build.

**Status (from §F table):** FIXED (`src/libultra/audio/bnkf.c`) — part of the Phase-3 mixer landing, not yet build/runtime-verified in isolation
