/*
 * ROM loading: reads the .z64 ROM from the data dir and exposes its segments.
 *
 * The N64 build links assets in from the ROM at fixed addresses. The PC port
 * loads the ROM file into a heap image and maps the game's data segments into
 * it, then patches the segment table so asset references resolve.
 *
 * Modelled on the PD port's port/src/romdata.c (~605 lines).
 *
 * STATUS: scaffolding stub — implement during Phase 1.
 *
 * NOTE: GE's ROM layout (segments, asset offsets) differs from PD's. The
 * segment table and asset map must be derived from this decomp's
 * ge007.ld / imagelist.u.csv / assets/ layout. See docs/PCPortResearch.md.
 */

#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "system.h"
#include "fs.h"
#include "romdata.h"

static u8  *rom = NULL;
static u32  romSize = 0;

/* Expected ROM file names per region (place one in the data/ dir). */
static const char *romFileName(void)
{
#if GE007_IS_PAL
    return "ge007.pal-final.z64";
#else
    return "ge007.ntsc-final.z64";
#endif
}

int romdataInit(void)
{
    const char *path = sysResolvePath("$S/" romFileName());

    /* TODO(Phase 1):
     *  - open + read the whole ROM into `rom`
     *  - validate the N64 magic ("Nintendo" @ 0x100) + region
     *  - build the segment map (code/data/assets) from the decomp's layout
     *  - patch the game's segment table so IMAGESEG / asset refs resolve
     */
    sysLogPrintf(LOG_INFO, "romdataInit: loading %s (TODO Phase 1)", path);

    FSFile *f = fsOpen(path, "rb");
    if (!f) {
        sysLogPrintf(LOG_ERROR, "romdataInit: cannot open %s", path);
        return -1;
    }
    romSize = (u32)fsSize(f);
    rom = (u8 *)malloc(romSize);
    if (!rom || fsRead(f, rom, romSize) != (int32_t)romSize) {
        sysLogPrintf(LOG_ERROR, "romdataInit: failed to read ROM");
        fsClose(f);
        return -1;
    }
    fsClose(f);

    if (romSize < 0x104 || memcmp(rom + 0x100, "Nintendo", 8) != 0) {
        sysLogPrintf(LOG_ERROR, "romdataInit: bad N64 ROM magic");
        return -1;
    }

    return 0;
}

void romdataDestroy(void)
{
    free(rom);
    rom = NULL;
    romSize = 0;
}

const u8 *romdataGetRom(void)   { return rom; }
u32       romdataGetRomSize(void) { return romSize; }

const void *romdataMapVa(u32 va)
{
    /* TODO(Phase 1): map an N64 RDRAM VA (0xA0000000+) into the image. */
    (void)va;
    return NULL;
}
