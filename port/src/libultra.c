/*
 * libultra OS shims for the PC port.
 *
 * The game code calls the libultra OS API (threads, message queues, timers,
 * VI, PI, SI, SP, AI, cache, memory). On the PC none of that hardware exists,
 * so this file provides single-threaded / host-backed implementations.
 *
 * Modelled on the PD port's port/src/libultra.c (~490 lines). That file is the
 * reference implementation to adapt; this stub marks the surface area.
 *
 * STATUS: scaffolding stub — implement during Phase 1 (OS core) and Phase 2
 * (SP/VI), Phase 3 (AI/SI).
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <PR/os.h>
#include <PR/os_internal.h>
#include <PR/rcp.h>
#include <PR/rdb.h>
#include <PR/sptask.h>

#include "platform.h"
#include "system.h"
#include "video.h"
#include "audio.h"
#include "input.h"
#include "fs.h"
#include "romdata.h"

/* ------------------------------------------------------------------------ */
/* Globals the game expects to exist (normally set by osInitialize).        */
/* ------------------------------------------------------------------------ */

u64 osClockRate = 6250000;          /* RSP counter rate (Hz) — see PD port */
u32 osMemSize   = 16 * 1024 * 1024; /* pretend 16MB RDRAM (+expansion)     */
/* TV type (normally set from console hardware in os/initialize.c, excluded).
 * The game reads it to pick PAL/NTSC behaviour (e.g. schedulerInitThread
 * checks osTvType == OS_TV_MPAL). Set to match the emulated region: PAL
 * consoles report MPAL, NTSC consoles report NTSC. */
#ifdef REFRESH_PAL
u32 osTvType    = OS_TV_MPAL;       /* EU: PAL  (0=PAL 1=NTSC 2=MPAL)      */
#else
u32 osTvType    = OS_TV_NTSC;       /* US/JP: NTSC                         */
#endif
u32 osResetType = 0;          /* GE's PR/os.h declares it u32 */
s32 osViClock   = 0;

/* ------------------------------------------------------------------------ */
/* Time                                                                      */
/* ------------------------------------------------------------------------ */

/*
 * The R4300 "SP timer" is a free-running counter. Map it to the host clock.
 * The PD port scales host microseconds into the N64 counter domain so that
 * osGetTime()-based timing in the game behaves correctly.
 */
OSTime osGetTime(void)
{
    /* TODO(Phase 1): scale sysGetMicroseconds() into the N64 counter domain. */
    return (OSTime)sysGetMicroseconds();
}

u32 osGetCount(void)
{
    return (u32)osGetTime();
}

/* ------------------------------------------------------------------------ */
/* Threads — single-threaded: create/start are no-ops, the "main" thread is  */
/* the host thread. The game's scheduler drives everything cooperatively.    */
/* ------------------------------------------------------------------------ */

void osCreateThread(OSThread *t, OSId id, void (*entry)(void *), void *arg,
                    void *sp, OSPri prio)
{
    (void)entry; (void)arg; (void)sp; (void)prio;
    if (t) { t->id = id; t->state = OS_STATE_STOPPED; }
}

void osStartThread(OSThread *t)   { if (t) t->state = OS_STATE_RUNNING; }
void osStopThread(OSThread *t)    { if (t) t->state = OS_STATE_STOPPED; }
void osDestroyThread(OSThread *t) { if (t) t->id = 0; }
void osYieldThread(void)          { /* cooperative: nothing to do */ }
OSPri osGetThreadPri(OSThread *t) { (void)t; return 0; }
void osSetThreadPri(OSThread *t, OSPri prio) { (void)t; (void)prio; }

/* ------------------------------------------------------------------------ */
/* Message queues — used by the scheduler / VI / controller callbacks.       */
/* Implement a simple ring buffer; see PD port for the exact semantics.      */
/* ------------------------------------------------------------------------ */

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *first, s32 count)
{
    /* TODO(Phase 1): allocate + init ring buffer. */
    (void)mq; (void)first; (void)count;
}
OSMesg osDequeueMesg(OSMesgQueue *mq)
{
    /* TODO(Phase 1) */
    (void)mq;
    return NULL;
}
OSMesg osMesgQueueLast(OSMesgQueue *mq)
{
    /* TODO(Phase 1) */
    (void)mq;
    return NULL;
}
void osEnqueueMesg(OSMesgQueue *mq, OSMesg msg)
{
    /* TODO(Phase 1) */
    (void)mq; (void)msg;
}

/*
 * High-level message API — the one the game actually uses (osSendMesg in 13
 * files, osRecvMesg in 26). The PD port's libultra.c defines these; in the
 * single-threaded port they are largely no-ops / immediate, but the scheduler
 * and audio code rely on the semantics, so implement them properly in Phase 1.
 */
s32 osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flag)
{
    /* TODO(Phase 1): enqueue msg; return 0 on success. */
    (void)mq; (void)msg; (void)flag;
    return 0;
}
s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flag)
{
    /* TODO(Phase 1): dequeue into *msg; return 0 on success, -1 if empty. */
    (void)mq; (void)msg; (void)flag;
    return 0;
}
void osSetEventMesg(OSEvent e, OSMesgQueue *mq, OSMesg msg)
{
    /* TODO(Phase 1) */
    (void)e; (void)mq; (void)msg;
}

/* ------------------------------------------------------------------------ */
/* VI (video) — map onto the port video layer.                              */
/* ------------------------------------------------------------------------ */

void osViSetMode(OSViMode *vm)
{
    /* TODO(Phase 2): translate the requested mode into a window/GL viewport. */
    (void)vm;
}
void osViSetSpecialFeatures(u32 f) { (void)f; }
void osViVSyncCallback(OSMesgQueue *mq, OSMesg msg)
{
    /* TODO(Phase 2): the port scheduler calls this each frame. */
    (void)mq; (void)msg;
}
void osViWaitVSync(void)
{
    /* TODO(Phase 2): block until the next vsync (frame pacing). */
}
void osViSetSync(OSMesgQueue *mq, OSMesg msg) { (void)mq; (void)msg; }
void osViBlack(u8 active)
{
    /* TODO(Phase 2): if active, clear the screen (see PD port). */
    (void)active;
}
void osViSetYScale(f32 value) { (void)value; }
void osViSetXScale(f32 value) { (void)value; }
void osViSetEvent(OSMesgQueue *mq, OSMesg msg, u32 retraceCount)
{ (void)mq; (void)msg; (void)retraceCount; }

/* Per-mode VI scale tables. Defined in vi.c on N64 (excluded); fr.c computes
 * and stores the values at runtime, so zero-init is safe until then. */
f32 g_ViXScales[2] = { 0.0f, 0.0f };
f32 g_ViYScales[2] = { 0.0f, 0.0f };

/* ------------------------------------------------------------------------ */
/* AI (audio) — map onto the port audio layer.                              */
/* ------------------------------------------------------------------------ */

s32 osAiSetFrequency(u32 hz)
{
    /* TODO(Phase 3): store the output rate; audio.c uses it.
     * Return the requested rate, as the N64 implementation reports the
     * frequency actually set. */
    return (s32)hz;
}
s32 osAiSetNextBuffer(void *buf, u32 size)
{
    /* TODO(Phase 3): hand the mixed buffer to audioSetNextBuffer(). */
    (void)buf; (void)size;
    return 1;
}
u32 osAiGetLength(void)
{
    /* TODO(Phase 3): return audioGetSamplesBuffered() in the right units. */
    return 0;
}
void osAiSetConvert(u32 convert) { (void)convert; }

/* ------------------------------------------------------------------------ */
/* PI (peripheral) — ROM/cart reads come from the loaded ROM image.         */
/* ------------------------------------------------------------------------ */

/*
 * PI DMA is synchronous on the PC: romdata.c maps the .z64 at the N64 cart
 * base (0x10000000), so an OS_READ from a cart address is a plain memcpy and
 * the "completion" is already delivered by the time we return. The game's
 * romReceiveMesg()/osRecvMesg() after every romCopy() is therefore satisfied
 * trivially (see the message-queue shims below).
 */
static void piServiceDma(s32 direction, u32 srcPA, void *dstVA, u32 size)
{
    if (size == 0)
        return;
    if (direction == OS_READ) {
        if (!romdataCartAddrValid(srcPA, size)) {
            sysLogPrintf(LOG_WARNING,
                         "osPiStartDma: ROM read out of range "
                         "(src=0x%08X size=0x%X); skipped",
                         srcPA, size);
            return;
        }
        memcpy(dstVA, (const void *)(uintptr_t)srcPA, size);
    } else {
        /* OS_WRITE: the game never writes the cart (saves go to EEPROM via
         * osEeprom*, shimmed separately). Log once-style and drop. */
        sysLogPrintf(LOG_WARNING,
                     "osPiStartDma: cart write dropped (src=0x%08X size=0x%X)",
                     srcPA, size);
    }
}

void osPiCreateManager(OSMesgQueue *mq, int prio) { (void)mq; (void)prio; }
s32 osPiStartDma(OSIoMesg *mesg, s32 prio, s32 direction, u32 addr,
                 void *buf, u32 size, OSMesgQueue *mq)
{
    piServiceDma(direction, addr, buf, size);
    (void)mesg; (void)prio; (void)mq;
    return 1; /* done */
}
s32 osPiRawStartDma(s32 direction, u32 srcPA, void *dstVA, u32 size)
{
    piServiceDma(direction, srcPA, dstVA, size);
    return 1; /* done */
}
u32  osPiGetStatus(void) { return 0; } /* not busy */

/*
 * PI device register read (normally src/libultra/io, EXCLUDED). token.c uses
 * it to read the cartridge token string. On the PC there is no cartridge
 * token register; return 0 (no token) so the game proceeds with an empty
 * token. TODO(Phase 1): optionally back with a value from the ROM/config.
 */
s32 osPiReadIo(u32 devAddr, u32 *data)
{
    (void)devAddr;
    if (data) *data = 0;
    return 0;
}

/* ------------------------------------------------------------------------ */
/* SI (controller / eeprom / mempak) — map onto input + file-backed saves.  */
/* ------------------------------------------------------------------------ */

s32 osContInit(OSMesgQueue *mesgq, u8 *bitpattern, OSContStatus *data)
{
    /* TODO(Phase 3): report inputGetNumControllers() as connected. */
    (void)mesgq; (void)bitpattern; (void)data;
    return 0;
}
s32 osContStartReadData(OSMesgQueue *mesgq)
{
    /* TODO(Phase 3): snapshot the current SDL input into the controller buf. */
    (void)mesgq;
    return 0;
}
s32 osContStartQuery(OSMesgQueue *mq)
{
    /* TODO(Phase 3) */
    (void)mq;
    return 0;
}
void osContGetQuery(OSContStatus *status)
{
    /* TODO(Phase 3): fill status for each connected controller. */
    (void)status;
}
void osContGetReadData(OSContPad *pad)
{
    /* TODO(Phase 3): fill pad from the current SDL input snapshot. */
    (void)pad;
}

/* EEPROM: back with a file in the data dir (see B2 — saves are EEPROM-based).
 * TODO(Phase 4): see docs/PCPortResearch.md §7. */
s32 osEepromProbe(OSMesgQueue *mq)            { (void)mq; return 0; }
s32 osEepromRead(OSMesgQueue *mq, u8 addr, u8 *buf)
{ (void)mq; (void)addr; (void)buf; return 0; }
s32 osEepromWrite(OSMesgQueue *mq, u8 addr, u8 *buf)
{ (void)mq; (void)addr; (void)buf; return 0; }
s32 osEepromLongRead(OSMesgQueue *mq, u8 addr, u8 *buf, int nbytes)
{ (void)mq; (void)addr; (void)buf; (void)nbytes; return 0; }
s32 osEepromLongWrite(OSMesgQueue *mq, u8 addr, u8 *buf, int nbytes)
{ (void)mq; (void)addr; (void)buf; (void)nbytes; return 0; }

/* Memory Pak (PFS) + Rumble Pak (motor): accessory detection only (see B2).
 * The PC has no Memory Pak; report "not plugged in" so the game proceeds
 * without rumble/mempak. TODO(Phase 4): optionally back PFS with a file. */
s32 osPfsInit(OSMesgQueue *queue, OSPfs *pfs, int channel)
{ (void)queue; (void)pfs; (void)channel; return PFS_ERR_NOPACK; }
s32 osPfsIsPlug(OSMesgQueue *queue, u8 *pattern)
{ (void)queue; if (pattern) *pattern = 0; return 0; }
s32 osMotorInit(OSMesgQueue *mq, OSPfs *pfs, int channel)
{ (void)mq; (void)pfs; (void)channel; return -1; }
s32 osMotorStart(OSPfs *pfs) { (void)pfs; return -1; }
s32 osMotorStop(OSPfs *pfs)  { (void)pfs; return -1; }

/* ------------------------------------------------------------------------ */
/* SP (RSP) — the port scheduler (gesched.c) drives the software RSP, so     */
/* these are mostly no-ops / bookkeeping. See gesched.c.                    */
/* ------------------------------------------------------------------------ */

void osSpTaskLoad(OSTask *t)    { (void)t; /* handled by gesched.c */ }
void osSpTaskStartGo(OSTask *t) { (void)t; /* handled by gesched.c */ }
void osSpTaskYield(void)        { /* handled by gesched.c */ }
void osSpFlush(void)            { /* no-op */ }
void osSpSetFifo(void *fifo, int size, int flags) { (void)fifo; (void)size; (void)flags; }
void *osSpGetFifo(void)         { return NULL; }
int   osSpTaskDone(void)        { return 1; }

/* ------------------------------------------------------------------------ */
/* RDP — bypassed (the software RSP emits GL directly). No-ops.             */
/* ------------------------------------------------------------------------ */

void osDpSetStatus(u32 status) { (void)status; }
u32  osDpGetStatus(void) { return 0; }
s32  osDpSetNextBuffer(void *buf, u64 size) { (void)buf; (void)size; return 1; }

/* ------------------------------------------------------------------------ */
/* Cache — no-op on the host (no split I/D cache to manage).                */
/* ------------------------------------------------------------------------ */

void osWritebackDCache(void *addr, int size)      { (void)addr; (void)size; }
void osWritebackDCacheAll(void)                    { }
void osInvalICache(void *addr, int size)           { (void)addr; (void)size; }
void osInvalDCache(void *addr, int size)           { (void)addr; (void)size; }
u32   osVirtualToPhysical(void *va)                { return (u32)(uintptr_t)va; }
void *osPhysicalToVirtual(u32 pa)                  { return (void *)(uintptr_t)pa; }

/* ------------------------------------------------------------------------ */
/* Misc                                                                      */
/* ------------------------------------------------------------------------ */

void osInitialize(void)
{
    /* The game calls this early. On the PC there is little to do; the port
     * has already set up video/audio/input/rom in main(). */
}

void osExit(void) { sysExit(0); }

u32 osGetFpcCsr(void) { return 0; }
void osSetFpcCsr(u32 csr) { (void)csr; }

/*
 * FPU CSR accessors used by init.c (the __os* variants are the raw libultra
 * names; init() saves/restores the FPU control/status register around the
 * decompress step). Unused on the PC but must link.
 */
u32 __osGetFpcCsr(void) { return 0; }
u32 __osSetFpcCsr(u32 csr) { (void)csr; return 0; }

/* TLB unmap (libultra/os/unmaptlb.s). The PC has its own MMU; no-op. */
void osUnmapTLB(int index) { (void)index; }

/* ------------------------------------------------------------------------ */
/* Timers / interrupts                                                      */
/* ------------------------------------------------------------------------ */

/* Timers: the port runs a single-threaded loop; timers are serviced by the
 * scheduler. TODO(Phase 1): wire to the port's frame clock. */
int osSetTimer(OSTimer *timer, OSTime start, OSTime period,
               OSMesgQueue *mq, OSMesg msg)
{ (void)timer; (void)start; (void)period; (void)mq; (void)msg; return 0; }
int osStopTimer(OSTimer *timer) { (void)timer; return 0; }

/* Interrupt mask: no hardware interrupts on the PC. */
OSIntMask osSetIntMask(OSIntMask mask) { (void)mask; return 0; }

/* ------------------------------------------------------------------------ */
/* PI / VI managers referenced by init.c (pi.c / vi.c are not compiled)     */
/* ------------------------------------------------------------------------ */

void piCreateManager(OSMesgQueue *mq, int prio)
{
    /* ROM access is handled by romdata.c; no PI manager needed on the PC. */
    (void)mq; (void)prio;
}
void viDebugRemoved(void)
{
    /* N64 debug VI hook; no-op on the PC. */
}
void viInit(void)
{
    /* VI init (normally src/vi.c, EXCLUDED). boss.c calls it during startup.
     * TODO(Phase 2): initialise the port video layer here. */
}

/* VI debug message queue (normally src/vi.c, EXCLUDED). fr.c references it. */
OSMesgQueue vi_c_debug_MQ;
