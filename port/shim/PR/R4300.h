/*
 * PC port shim for PR/R4300.h (see docs/PCPortResearch.md).
 *
 * On the N64, PHYS_TO_K0 turns a physical address into a KSEG0 virtual
 * address by OR'ing in 0x80000000. On the PC port the game's working RAM
 * lives in the s32-safe DRAM view at 0x70000000 (see port/src/dram.c):
 * addresses with bit 31 set would sign-extend to invalid pointers when they
 * pass through s32 parameters (e.g. mempCheckMemflagTokens in src/memp.c).
 * The KSEG0 view at 0x80000000 still exists as a byte-identical mirror for
 * code that rebuilds pointers with `offset | 0x80000000`, and "physical"
 * addresses are offsets from the V1 base (see the PR/os.h shim).
 *
 * So PHYS_TO_K0 is the identity here: its argument is already a live host
 * pointer in the V1 view.
 *
 * Inert in the N64 build (no -DPORT): pure pass-through.
 */
#ifndef _PORT_SHIM_R4300_H_
#define _PORT_SHIM_R4300_H_

#if defined(PORT)
#    include "include/PR/R4300.h"

#    undef PHYS_TO_K0
#    define PHYS_TO_K0(x) ((u32)(x)) /* identity: V1 view is already virtual */

#else
#    include <PR/R4300.h>
#endif

#endif /* _PORT_SHIM_R4300_H_ */
