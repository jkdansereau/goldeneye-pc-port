#ifndef PORT_ROMDATA_H
#define PORT_ROMDATA_H

/*
 * ROM loading: reads the .z64 ROM from the data dir and exposes its segments.
 * Modelled on the PD port's port/include/romdata.h.
 *
 * The N64 build links assets in from the ROM at fixed addresses. The PC port
 * instead loads the ROM file and maps the game's data segments (code, data,
 * assets) into a heap-allocated image, then patches the segment table so the
 * game's IMAGESEG / asset references resolve into that image.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Load the ROM for the configured ROMID from the data dir.
 * Returns 0 on success, non-zero on failure.
 */
int romdataInit(void);
void romdataDestroy(void);

/* Pointer to the loaded ROM image (start of the .z64). */
const u8 *romdataGetRom(void);
/* Size of the loaded ROM in bytes. */
u32       romdataGetRomSize(void);

/*
 * Map an N64 virtual address (0xA0000000 RDRAM space) to the corresponding
 * pointer in the loaded image. Used to resolve asset/segment references.
 */
const void *romdataMapVa(u32 va);

#ifdef __cplusplus
}
#endif

#endif /* PORT_ROMDATA_H */
