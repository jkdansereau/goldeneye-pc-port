# D131

<!-- from the old section-F/H table row -->
| D131 | **crash class C2m (Jungle `-level_37`):** — full `## D131` entry at file tail | resolved — `seg_addr()` restores the module high word for any fallthrough w1 in `[0x40000000, 0x70000000)` (module fixed-based at `0x140000000`, no ASLR; DRAM/KSEG0/segmented/phys all handled earlier).… |

## D131 — crash class C2m (Jungle `-level_37`):

crash class C2m (Jungle `-level_37`): fast3d `gfx_sp_matrix` AV on a wild matrix pointer `0x401c68e0` (`gfx_pc.cpp:1046`) ~frame 300, when the first explosion/smoke prop renders. `explosionRenderPropSmoke` builds `gSPMatrix(gdl++, osVirtualToPhysical((void*)&dword_CODE_bss_8007A100), …MODELVIEW)` (+ `applyRoomMatrixToDisplayList`). `osVirtualToPhysical()` is a `u32`-returning shim (`libultra.c:1207`) → truncates the compiled `.bss` symbol's `0x1_00000000` module high word → w1 = `0x40xxxxxx` → `seg_addr()` returns it raw. Same class as D94 (chraction 32-bit ptr truncation), but in a GBI DL word. The projection matrix in the same DL is fine (`get_BONDdata_field_10E0()` is a runtime `0x70xxxxxx` ptr, fits u32); the `gSPDisplayList(&globalDL_0xNNN)` refs are fine (port `Gwords.w1` is 64-bit `uintptr_t`, no macro truncation). ~30 `osVirtualToPhysical(<compiled matrix/vtx symbol>)` sites exist (`explosion.c`, `glass*.c`, `blood_animation.c`, `bondview2.c`) — all latent until that effect first draws.

**Status (from §F table):** resolved — `seg_addr()` restores the module high word for any fallthrough w1 in `[0x40000000, 0x70000000)` (module fixed-based at `0x140000000`, no ASLR; DRAM/KSEG0/segmented/phys all handled earlier). Jungle renders to frame 2400+ clean; `-level_20`/`-level_24` unregressed. **18→19/21.** (`-level_09` has a separate pre-existing boot crash, see below.)
