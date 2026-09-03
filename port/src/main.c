/*
 * PC entry point for the GoldenEye 007 port.
 *
 * Replaces the N64 boot path (boot.s -> init() -> mainproc() -> bossEntry()).
 * On the PC we:
 *   1. set up system / config / fs / rom
 *   2. load the ROM and map it at the cart base (0x10000000)
 *   3. init video (SDL2 + GL via fast3d), audio, input
 *   4. start the thread kernel and run the game's mainproc() as a real OS
 *      thread (it IS the N64 mainThread) — which runs bossEntry(), the real
 *      game loop. The game's own scheduler (src/sched.c) drives frames; see
 *      docs/internals.md.
 *   5. the host main thread then owns SDL event pumping for the lifetime of
 *      the process (Windows only dispatches window messages to the creating
 *      thread, and every game thread can be blocked on a queue).
 */

#include <stdlib.h>
#include <stdio.h>

#include <PR/ultratypes.h>
#include <PR/os.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "fs.h"
#include "romdata.h"
#include "dram.h"
#include "video.h"
#include "audio.h"
#include "input.h"
#include "mixer.h"
#include "crash.h"
#include "thread_config.h"

/* Defined in the game (src/init.c). The port calls into the real game entry. */
extern void mainproc(void *args);
extern OSThread mainThread; /* src/init.c:75 */

/* name:number pairs for the 21 solo levels (matches tools_pc/level_sweep.sh
 * and playtest.sh --list). boss.c decodes -level_XX as d0*10 + d1 - 0x210. */
static const struct { const char *name; const char *num; } kSoloLevels[] = {
    {"Dam","33"}, {"Facility","34"}, {"Runway","35"}, {"Surface1","36"},
    {"Bunker1","09"}, {"Silo","20"}, {"Frigate","26"}, {"Surface2","43"},
    {"Bunker2","27"}, {"Statue","22"}, {"Archives","24"}, {"Streets","29"},
    {"Depot","30"}, {"Train","25"}, {"Jungle","37"}, {"Control","23"},
    {"Caverns","39"}, {"Cradle","41"}, {"Aztec","28"}, {"Egypt","32"},
    {"Cuba","54"},
};

static void portPrintVersion(void)
{
    printf("GoldenEye 007 PC port\n"
           "  rom      : %s\n"
           "  platform : %s\n"
           "  build    : %s\n",
           GE007_ROMID, GE007_TARGET_PLATFORM, GE007_VERSION_HASH);
}

static void portPrintHelp(const char *argv0)
{
    portPrintVersion();
    printf("\nusage: %s [options] [-level_XX]\n\n"
           "  --help            this message\n"
           "  --version         build id only\n"
           "  -level_XX         boot straight into a solo level (per-level\n"
           "                    memory pools are auto-injected)\n\n"
           "config: ge007.ini in the data dir (written on first run).\n\n"
           "solo levels (-level_XX):\n", argv0 ? argv0 : "ge007");
    for (size_t i = 0; i < sizeof(kSoloLevels) / sizeof(kSoloLevels[0]); ++i) {
        printf("  %-10s -level_%s\n", kSoloLevels[i].name, kSoloLevels[i].num);
    }
}

static void portAtExit(void)
{
    /* Clean-exit only (exit(0) from videoPumpEvents). Crash/fatal paths call
     * abort(), which does not run atexit handlers. */
    videoSaveWindowState();
    configSave();
}

int main(int argc, char **argv)
{
    sysSetArgs(argc, argv);

    if (sysArgCheck("--version")) { portPrintVersion(); return 0; }
    if (sysArgCheck("--help") || sysArgCheck("-h")) {
        portPrintHelp(argv[0]);
        return 0;
    }

    sysLogPrintf(LOG_INFO, "GoldenEye 007 PC port starting "
                "(%s, %s)", GE007_ROMID, GE007_VERSION_HASH);

    /* Crash handler first, so any failure below is debuggable. */
    crashInit();

    /* 1. Platform + config + filesystem. */
    configLoad();
    atexit(portAtExit);   /* persist config + window geometry on clean exit */

    /* 2. Load the ROM and map segments. */
    if (romdataInit() != 0) {
        sysLogPrintf(LOG_ERROR, "Failed to load ROM (expected a .z64 in the "
                    "data/ dir, see README)");
        return 1;
    }

    /* 2a. Sanity: the image MUST have loaded at its preferred base.
     *     dram_syms.s absolute symbols are referenced through pointer-typed
     *     externs, which on x86-64/PE become .refptr slots with BASE
     *     relocations. The build disables ASLR (--disable-dynamic-base) so the
     *     loader loads at 0x140000000 and those relocations are no-ops; if we
     *     ever got relocated, every such slot would be silently corrupted.
     *     Fail loudly instead. */
#if defined(PLATFORM_WINDOWS)
    if (sysImageBase() != 0x140000000ul) {
        sysLogPrintf(LOG_ERROR,
            "image loaded at %p, expected preferred base 0x140000000; "
            "absolute DRAM symbols would be corrupted (ASLR must be off)",
            (void *)sysImageBase());
        return 1;
    }
#else
    /* Linux/ELF no-PIE: dram_syms.s absolute symbols resolve to their literal
     * values independent of the image base (no .refptr indirection), so the
     * load address is not constrained. sysImageBase() is a stub here anyway. */
#endif

    /* 2b. Reserve the N64-DRAM region: s32-safe view @ 0x70000000 (cfb_16,
     *     mempools) + KSEG0 mirror @ 0x80000000 (see port/src/dram.c). */
    dramReserve();

    /* 3. Video / audio / input. */
    if (videoInit() != 0) {
        sysLogPrintf(LOG_ERROR, "videoInit failed");
        return 1;
    }
    audioInit();
    mixerInit();
    inputInit();

    /* 4. Run the game. mainproc() runs as the N64 mainThread (a real OS
     *    thread with its own stack); it creates the rmon/idle/scheduler/
     *    audio threads and never returns in practice. */
    sysLogPrintf(LOG_INFO, "ROM mapped at 0x%08X (%u bytes); starting game",
                (unsigned)0x10000000, romdataGetRomSize());
    portKernelInit();
    osCreateThread(&mainThread, MAIN_THREAD_ID, &mainproc, NULL, NULL,
                   MAIN_THREAD_PRIORITY);
    osStartThread(&mainThread);

    /* 5. Host thread: pump SDL events until the window is closed / ESC.
     *    videoPumpEvents() exits the process on quit. */
    for (;;) {
        videoPumpEvents();
        sysSleep(8);
    }

    /* Unreachable in practice; clean up if we ever get here. */
    inputDestroy();
    mixerDestroy();
    audioDestroy();
    videoDestroy();
    romdataDestroy();
    configSave();

    return 0;
}
