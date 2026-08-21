/*
 * PC port shim for PR/os.h — DRAM address conversions (see docs/PCPortResearch.md).
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
#    include "include/PR/os.h"

#    undef OS_K0_TO_PHYSICAL
#    define OS_K0_TO_PHYSICAL(x) ((u32)((char *)(x) - 0x70000000))

#    undef OS_PHYSICAL_TO_K0
#    define OS_PHYSICAL_TO_K0(x) ((void *)(x))

#else
#    include <PR/os.h>
#endif

#endif /* _PORT_SHIM_OS_H_ */
