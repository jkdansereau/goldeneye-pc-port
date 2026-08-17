/*
 * Port RCP scheduler.
 *
 * Replaces the hardware task submission in src/sched.c. On the N64,
 * __scExec() hands a gfx/audio task to the RSP via osSpTaskLoad/StartGo.
 * Here, instead of writing RSP registers, we:
 *   - gfx task: call videoSubmitCommands(display_list)  ->  the software RSP
 *   - audio task: feed the mixed buffer to the audio layer
 * and reproduce the gfx/audio interleaving + VSync pacing the game expects.
 *
 * Modelled on the PD port's port/src/pdsched.c (~405 lines).
 *
 * STATUS: scaffolding stub — implement during Phase 2.
 *
 * NOTE: GE's src/sched.c has speed-graph / profiling hooks
 * (speedgraphMarkerHandler) and a slightly different task model than PD.
 * Reproduce the exact gfx/audio/VSync behaviour; see
 * docs/PCPortResearch.md §9.3.
 */

#include <PR/ultratypes.h>
#include <PR/os.h>
#include <PR/gbi.h>

#include "sched.h"
#include "fr.h" /* NUM_VIDEO_FRAME_BUFFERS */

#include "platform.h"
#include "video.h"
#include "audio.h"
#include "system.h"

/*
 * Scheduler globals (normally defined in src/sched.c, which is EXCLUDED from
 * the PC build). init.c's schedulerInitThread() references os_scheduler and
 * gfxClient, so the port provides them here (matching src/sched.c:70-71).
 */
OSSched os_scheduler;
OSScClient gfxClient[3];

/*
 * Scheduler/VI globals (normally defined in src/sched.c, EXCLUDED). fr.c
 * references these; the port provides them so it links. Values are the
 * sched.c initializers.
 */
s32 g_schedViCurrentFrameBuffer = 0;
s32 g_ViChangeVideoModes[NUM_VIDEO_FRAME_BUFFERS] = {0, 0};
OSViMode g_ViModes[NUM_VIDEO_FRAME_BUFFERS];
OSViMode *g_ViModePtrs[NUM_VIDEO_FRAME_BUFFERS];

/*
 * Performance counters (normally in src/sched.c). speed_graph.c reads them
 * via get_counters().
 */
static u32 g_DisplayPerformanceCounters[4]; // clock, cmc, pipe, tmem
u32 *get_counters(void) { return g_DisplayPerformanceCounters; }

/* Debug stderr control (normally in src/sched.c). boss.c calls it. */
void permit_stderr(u32 flag) { (void)flag; }

/*
 * Scheduler API (declared in sched.h, normally defined in src/sched.c which
 * is EXCLUDED from the PC build). The port provides these so engine code that
 * calls osCreateScheduler/osScAddClient/osScGetCmdQ links. The real work —
 * driving the software RSP per frame — happens in geschedRunFrame().
 */
void osCreateScheduler(OSSched *s, void *stack, u8 mode, u32 numFields)
{
    /* TODO(Phase 2): initialise the port scheduler state in *s. */
    (void)s; (void)stack; (void)mode; (void)numFields;
    sysLogPrintf(LOG_INFO, "osCreateScheduler: TODO (Phase 2)");
}
void osScAddClient(OSSched *s, OSScClient *c, OSMesgQueue *msgQ, OSScClient *next)
{
    /* TODO(Phase 2): register a gfx/audio client. */
    (void)s; (void)c; (void)msgQ; (void)next;
}
void osScRemoveClient(OSSched *s, OSScClient *c)
{
    (void)s; (void)c;
}
OSMesgQueue *osScGetCmdQ(OSSched *s)
{
    /* TODO(Phase 2): return the scheduler's command queue. */
    (void)s;
    return NULL;
}

void geschedInit(void)
{
    /* TODO(Phase 2): set up the scheduler + gfx client, mirroring
     * schedulerInitThread() in src/init.c but with the port's frame hooks. */
    sysLogPrintf(LOG_INFO, "geschedInit: TODO (Phase 2)");
}

/*
 * Per-frame driver. Called by the main loop (or the VI vsync callback shim).
 * Sequence (mirrors PD's pdsched.c):
 *   videoStartFrame()
 *   ... game builds the display list ...
 *   videoSubmitCommands(cmds)
 *   videoEndFrame()
 */
void geschedRunFrame(void *displayList)
{
    videoStartFrame();
    videoSubmitCommands((Gfx *)displayList);
    audioEndFrame();
    videoEndFrame();
}
