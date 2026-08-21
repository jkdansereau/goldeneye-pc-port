/*
 * PC entry point for the GoldenEye 007 port.
 *
 * Replaces the N64 boot path (boot.s -> init() -> mainproc() -> bossEntry()).
 * On the PC we:
 *   1. set up system / config / fs / rom
 *   2. load the ROM and map it at the cart base (0x10000000)
 *   3. init video (SDL2 + GL), audio, input
 *   4. hand control to the game's mainproc() / bossEntry()
 *
 * Modelled on the PD port's port/src/main.c.
 *
 * STATUS: Phase 1 — ROM loads and maps, a window opens and clears each frame.
 * Step 4 (mainproc) is deferred until the software RSP (fast3d) and the
 * scheduler can service the game's rendering/audio threads; see
 * docs/PCPortResearch.md.
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

    /* 4. Phase 1 demo loop: clear colour until the RSP/scheduler can drive
     *    real frames (Phase 2). Press ESC or close the window to quit.
     *
     * When fast3d lands, replace this with mainproc(NULL) — defined in
     * src/init.c (compiled for the PC; see A3 in docs/PCPortResearch.md).
     * init.c's N64-only init() is never called; its hardware deps are
     * stubbed in port/src/n64stubs.c and the libultra OS calls are shimmed
     * in port/src/libultra.c.
     */
    sysLogPrintf(LOG_INFO, "Phase 1: ROM mapped at 0x%08X (%u bytes); "
                "demo loop running — ESC to quit",
                (unsigned)0x10000000, romdataGetRomSize());
    for (;;) {
        if (videoHandleEvents())
            break;
        videoStartFrame();
        videoSubmitCommands(NULL);   /* no-op until fast3d lands */
        videoEndFrame();
    }

    /* Clean up. */
    inputDestroy();
    mixerDestroy();
    audioDestroy();
    videoDestroy();
    romdataDestroy();
    configSave();

    return 0;
}
