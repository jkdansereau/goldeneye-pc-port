#ifndef PORT_DRAM_H
#define PORT_DRAM_H

/*
 * Reserve the N64-DRAM region: one 8 MB backing store mapped at BOTH
 * 0x70000000 (s32-safe "virtual" view, where game RAM symbols live) and
 * 0x80000000 (KSEG0 mirror). See port/src/dram.c. Must be called before any
 * game thread runs; the absolute symbols in port/src/dram_syms.s point into
 * the V1 view.
 */
void *dramReserve(void);

#endif /* PORT_DRAM_H */
