/*
 * PC port shim for PR/os.h — DRAM address conversions (see docs/internals.md).
 *
 * The game's working RAM lives in the s32-safe DRAM view at 0x70000000
 * ("V1"), with a byte-identical KSEG0 mirror at 0x80000000 ("V2"); see
 * port/src/dram.c. Addresses with bit 31 set sign-extend to invalid
 * pointers through s32 parameters (src/memp.c), so live pointers must stay
 * in V1, while `offset | 0x80000000` rebuilds (src/game/bg.c) must land in
 * V2. The two views share one backing store, so both are the same data.
 *
 * "Physical" addresses in GBI words and ROM data are therefore OFFSETS FROM
 * THE V1 BASE, and the conversions become:
 *
 *   OS_K0_TO_PHYSICAL(x) = (u32)((char *)x - 0x70000000)
 *       small offset P; fast3d's seg_addr() resolves it with +0x80000000,
 *       landing in V2. `P | 0x80000000` (bg.c et al.) also lands in V2.
 *   OS_PHYSICAL_TO_K0(x) = x   (identity)
 *       Callers pass EITHER live V1 pointers (bondview2.c, front.c) — which
 *       then go into GBI words as full addresses that fast3d passes through
 *       — OR small physical offsets, which fast3d remaps to V2. Both forms
 *       resolve correctly.
 *   osVirtualToPhysical / osPhysicalToVirtual stay the identity (port/src/
 *   libultra.c): V1 addresses are already live host pointers < 4 GB.
 *
 * Inert in the N64 build (no -DPORT): pure pass-through.
 */
#ifndef _PORT_SHIM_OS_H_
#define _PORT_SHIM_OS_H_

#if defined(PORT)
#    if defined(__APPLE__)
/* Darwin exposes errno and sprintf as function-like macros. Suppress them
 * while parsing the N64 API's errno fields and sprintf declaration. */
#        pragma push_macro("errno")
#        pragma push_macro("sprintf")
#        undef errno
#        undef sprintf
#    endif
#    include "include/PR/os.h"
#    if defined(__APPLE__)
#        pragma pop_macro("sprintf")
#        pragma pop_macro("errno")
#    endif
#    include "port_addr.h" /* PORT_ADDR_BASE for the V1-base subtractions */

#    undef OS_K0_TO_PHYSICAL
/* Offset of a DRAM V1 pointer from the V1 base. On macOS the V1 base is
 * PORT_ADDR_BASE + 0x70000000, so the base must be subtracted too; the result
 * is a small (< 8 MB) offset that fits s32 and that fast3d's seg_addr()
 * re-bases into the V2/KSEG0 mirror. Identity behaviour at PORT_ADDR_BASE==0. */
#    define OS_K0_TO_PHYSICAL(x) ((u32)((uintptr_t)(x) - (PORT_ADDR_BASE + 0x70000000ULL)))

#    undef OS_PHYSICAL_TO_K0
/* Identity: callers pass either a live V1 pointer (which is preserved with
 * its full 64-bit value through the 64-bit GBI words) or a small physical
 * offset (which fast3d re-bases). Both are unaffected by the window base. */
#    define OS_PHYSICAL_TO_K0(x) ((void *)(x))

#else
#    include <PR/os.h>
#endif

#endif /* _PORT_SHIM_OS_H_ */
