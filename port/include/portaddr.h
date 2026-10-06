#ifndef PORT_PORTADDR_H
#define PORT_PORTADDR_H

/* Stage B step 1 (STAGEB-ABI-DESIGN, D441 follow-on): the ONE place that
 * names the N64-DRAM arena windows. port/src/dram.c maps an 8 MB backing
 * store at BOTH views; everything that converts or range-checks an arena
 * address uses these instead of a literal, so moving the arena later (the
 * high-arena test flavour, macOS/ARM port_addr) is a change in one header.
 * Compile-time constants: no codegen change from the old literals. */
#define PORT_DRAM_V1_BASE  0x70000000UL  /* s32-safe "virtual" view (game RAM symbols) */
#define PORT_DRAM_K0_BASE  0x80000000UL  /* KSEG0 mirror of V1 (same pages) */
#define PORT_DRAM_SIZE     0x00800000UL  /* 8 MB, like an 8-MB-RAM N64 */

#endif /* PORT_PORTADDR_H */
