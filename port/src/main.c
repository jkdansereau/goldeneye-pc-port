/*
 * PC entry point for the GoldenEye 007 port.
 *
 * Replaces the N64 boot path (boot.s -> init() -> mainproc() -> bossEntry()).
 * On the PC we:
 *   1. set up system / config / fs / rom
 *   2. load the ROM and map its segments
 *   3. init video (SDL2 + GL), audio, input
 *   4. hand control to the game's mainproc() / bossEntry()
 *
 * Modelled on the PD port's port/src/main.c.
 *
 * STATUS: scaffolding stub — fill in during Phase 1.
 */

#include <stdlib.h>
#include <stdio.h>

#include <PR/ultratypes.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "fs.h"
#include "romdata.h"
#include "video.h"
#include "audio.h"
#include "input.h"
#include "mixer.h"

/* Defined in the game (src/init.c). The port calls into the real game entry. */
extern void mainproc(void *args);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    sysLogPrintf(LOG_INFO, "GoldenEye 007 PC port starting "
                "(%s, %s)", GE007_ROMID, GE007_VERSION_HASH);

    /* 1. Platform + config + filesystem. */
    configLoad();

    /* 2. Load the ROM and map segments. */
    if (romdataInit() != 0) {
        sysLogPrintf(LOG_ERROR, "Failed to load ROM (expected a .z64 in the "
                    "data/ dir, see README)");
        return 1;
    }

    /* 3. Video / audio / input. */
    if (videoInit() != 0) {
        sysLogPrintf(LOG_ERROR, "videoInit failed");
        return 1;
    }
    audioInit();
    mixerInit();
    inputInit();

    /* 4. Hand control to the game.
     *
     * mainproc() is defined in src/init.c, which IS compiled for the PC (see
     * A3 in docs/PCPortResearch.md). init.c's N64-only init() is compiled but
     * NOT called — its hardware deps (segment starts, decompress, TLB, FPU
     * CSR) are stubbed in port/src/n64stubs.c, and the libultra OS calls are
     * shimmed in port/src/libultra.c. mainproc() itself just sets up the
     * threads and calls schedulerInitThread() + bossEntry().
     */
    mainproc(NULL);

    /* mainproc() normally does not return; clean up if it does. */
    inputDestroy();
    mixerDestroy();
    audioDestroy();
    videoDestroy();
    romdataDestroy();
    configSave();

    return 0;
}
