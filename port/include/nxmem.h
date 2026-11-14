#ifndef PORT_NXMEM_H
#define PORT_NXMEM_H
/*
 * nxmem — fixed-address memory for the Nintendo Switch build (project-dam).
 *
 * Horizon has no mmap(MAP_FIXED). This places page-aligned heap memory at an
 * exact virtual address, the way the PC port's fixed mmaps do, using the two
 * methods verified on hardware by tools/switch/memprobe
 * (docs/switch/address-model.md):
 *   - inside the process's stack region: svcMapMemory (heap -> stack region);
 *   - elsewhere in the ASLR region: svcMapProcessCodeMemory on the own process
 *     handle, then svcSetProcessMemoryPermission(RW).
 * Used for the cart image (0x10000000), the DRAM arena (0x70000000 + KSEG0
 * mirror) and the game-thread stacks (0xA0000000).
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pick the host base of the N64 address window: a 4 GiB-aligned address
 * whose cart, DRAM (V1 + KSEG0) and game-stack windows are all unmapped, and
 * which does not hold the executable image. Sets g_portAddrBase (portaddr.h).
 * Must run before the first nxMapFixed(). Aborts the boot if no slot is free. */
void nxChooseAddrBase(void);

/* Map `size` bytes (rounded up to a page) of fresh zeroed memory at exactly
 * `addr`. Returns (void *)addr, or NULL (and logs why). */
void *nxMapFixed(uintptr_t addr, size_t size);

/* Map a second view of an existing nxMapFixed() block `primary` at `view`
 * (svcMapProcessMemory). Only works when `primary` was placed as code memory.
 * Returns (void *)view or NULL. */
void *nxMapAlias(uintptr_t view, uintptr_t primary, size_t size);

/* Undo nxMapFixed()/nxMapAlias() for the block that starts at `addr`. */
void nxUnmapFixed(uintptr_t addr);

/* Undo every mapping (aliases first). Only once no thread runs on them. */
void nxReleaseAll(void);

/* Give the calling thread an ideal core (0..2) while allowing the kernel to
 * migrate it across the three application cores. libnx starts every pthread
 * on the process's default core, so without this the game logic, the render
 * worker and audio all share one core. */
void nxSetThreadCore(int idealCore);

/* Set when the DRAM KSEG0 view (0x80000000) could not be mapped as a real
 * mirror: portN64ToHost() then folds KSEG0 addresses onto V1 in software. */
extern int g_nxDramK0Folded;

/* port/src/switch/nxgame.c: applies the Switch gameplay feedback options
 * (Game.BloodOnHit) to the game's data; at boot and on every change. */
void nxGameApply(void);

/* D959: host-thread applet service (appletMainLoop + operation-mode log);
 * 0 once the system asked the application to exit. Never touches SDL. */
int nxAppletAlive(void);

/* End the process at once, every thread included, after flushing stdio.
 * exit() would close the system services (applet, audio, nv) while the
 * game's other threads still run, and the first of them to call one
 * aborts (svcBreak). Called last from the clean-exit handler. */
void nxExitProcess(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* PORT_NXMEM_H */
