# D198

<!-- from the old section-F/H table row -->
| D198 | **`alAdpcmPull`/`_decodeChunk` used `K0_TO_PHYS` (an unconditional `& 0x1FFFFFFF` mask) on 64-bit heap pointers (`src/libultra/audio/load.c`).** — full `## D198` entry at file tail | FIXED (`src/libultra/audio/load.c`) — part of the Phase-3 mixer landing, not yet build/runtime-verified in isolation |

## D198 — `alAdpcmPull`/`_decodeChunk` used `K0_TO_PHYS` (an unconditional `& 0x1FFFFFFF` mask) on 64-bit heap pointers (`src/libultra/audio/load.c`).

**`alAdpcmPull`/`_decodeChunk` used `K0_TO_PHYS` (an unconditional `& 0x1FFFFFFF` mask) on 64-bit heap pointers (`src/libultra/audio/load.c`).** `K0_TO_PHYS` is the N64 kseg0→physical mask, valid only because N64 pointers fit in 32 bits; on PC's 64-bit heap it truncates any pointer whose value exceeds 512MB, corrupting the `aLoadADPCM`/`aSetLoop`/`aADPCMdec` DRAM addresses at 3 call sites (`load.c:71`, `:468`, `:474`). Every sibling call site in `env.c`/`resample.c`/`reverb.c` already resolves state pointers via `osVirtualToPhysical` (the port shim, `port/src/libultra.c`), which is bit-identical to `K0_TO_PHYS` for real N64 kseg0 addresses but correctly PC-shimmed. **Fix (ABI-layer, `#ifdef`-free — same call resolves correctly on both platforms):** replace the 3 `K0_TO_PHYS(...)` calls with `osVirtualToPhysical(...)`. No behavior change on N64 (identical result for kseg0 pointers); fixes PC. Surfaced while building the Phase-3 software mixer (D199) — the acmd list these opcodes feed now actually executes on PC instead of being ignored.

**Status (from §F table):** FIXED (`src/libultra/audio/load.c`) — part of the Phase-3 mixer landing, not yet build/runtime-verified in isolation
