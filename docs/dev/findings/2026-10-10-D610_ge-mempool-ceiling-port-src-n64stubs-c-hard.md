# D610

<!-- from the old section-F/H table row -->
| D610 | **`GE_MEMPOOL_CEILING` (`port/src/n64stubs.c`) hard-coded the D441 base-0 arena top; now `#if PORT_ADDR_BASE != 0`-keyed so arm64 gets the arena-relative ceiling and base-0 platforms keep the literal** — full `## D610` entry at file tail | FIXED 2026-10-07 (macos branch; base-0 identity by construction) |

## D610: `GE_MEMPOOL_CEILING` in `port/src/n64stubs.c` assumed the D441 base-0 arena — now keyed on `PORT_ADDR_BASE` — FIXED 2026-10-07 (macos branch)

*Labelled D582 on the macos branch; renumbered at the v0.5.1 merge (the interpolation work had used D579-D585).*

`port/src/n64stubs.c` caps the `-m` mempool size at
`GE_MEMPOOL_CEILING = 0x70000000u + 0x700000`, i.e. "the top of the 8 MB
V1 view minus a page", which is only meaningful when the arena sits at
0x70000000 (D441's base-0 model). With `PORT_ADDR_BASE != 0` (arm64 macOS,
D607/D609) the arena is at `PORT_ADDR_BASE + 0x70000000` and the ceiling
must move with it, so the define is now `#if PORT_ADDR_BASE != 0`-guarded:
base-0 platforms keep the literal `0x700000` offset byte-identically (the
D441 comment stays true there), arm64 gets `+0x7FF000` — the top of the
8 MB V1 mapping less 0x1000, consistent with `port/src/dram_syms.S`, which
parks `animations_frame_buffer` at `PORT_DRAM_V1_BASE + 0x7FFD30`.
