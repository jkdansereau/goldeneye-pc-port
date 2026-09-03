/*
 * libultra OS shims for the PC port, plus the cooperative thread kernel.
 *
 * The game code calls the libultra OS API (threads, message queues, VI, PI,
 * SI, SP, AI, cache, memory). On the PC none of that hardware exists:
 *
 *  - Threads are REAL host threads (pthreads). The N64 OS is a preemptive
 *    priority scheduler, so this is both faithful and robust: blocking
 *    osRecvMesg() waits on a per-queue condition variable. (An earlier
 *    setjmp/longjmp green-thread kernel corrupted FPU state across longjmps
 *    on Windows x64/MinGW — synthetic STATUS_FLOATING_POINT_INVALID_OPERATION
 *    crashes at PC=0; see docs/internals.md.)
 *  - RSP tasks (osSpTaskStartGo) run the software RSP (fast3d) inline and
 *    post the RSP_DONE / RDP_DONE messages sched.c waits for.
 *  - VI retrace is a vsync-paced tick posted by a dedicated kernel thread,
 *    once per frame at the console's frame rate.
 *  - Controllers are filled from the keyboard in osContStartQuery().
 *
 * Modelled on the PD port's port/src/libultra.c.
 */

/* NOTE: no <sched.h> here — angle-bracket form would resolve to the game's
 * src/sched.h (it is on the include path), not MinGW's. osYieldThread uses a
 * short sleep instead of sched_yield(). */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include <SDL.h>

/* N64 headers declare struct fields named `errno`; the C macro (pulled in
 * by pthread.h/sched.h -> errno.h) must not be active while they parse. */
#pragma push_macro("errno")
#undef errno
#include <PR/os.h>
#include <PR/os_internal.h>
#include <PR/rcp.h>
#include <PR/rdb.h>
#include <PR/sptask.h>
#include <PR/gbi.h>
#pragma pop_macro("errno")

#include "platform.h"
#include "system.h"
#include "crash.h"   /* D38: crashDumpThreads() */
#include "video.h"
#include "audio.h"
#include "input.h"
#include "fs.h"
#include "romdata.h"
#include "crash.h"

#if defined(PLATFORM_WINDOWS)
#include <windows.h> /* GetCurrentThreadId for thread-dump labels */
#endif

/* fast3d (C++): the software RSP entry point. */
extern void gfx_run(Gfx *commands);

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

OSTime osGetTime(void)
{
    return (OSTime)sysGetMicroseconds();
}

u32 osGetCount(void)
{
    /* The N64 RSP core counter advances at ~OS_CPU_COUNTER (~46.5 MHz), NOT
     * microseconds. GE's pacing assumes this rate: waitForNextFrame() in
     * frametiming.c waits for 775,875 - 387,937 ticks per NTSC frame (931,050
     * per PAL frame) and bossMainloop gates DL builds on
     * MAIN_LOOP_TICK_INTERVAL = 387,937 ticks. Feeding it microseconds made
     * waitForNextFrame block ~388 ms/frame and the gate never pass in steady
     * state (hang after the first couple of frames). Scale real time to
     * 46.5525 ticks/us (= 775875/16666.67us = 931050/20000us, both regions)
     * and wrap like the 32-bit hardware counter. */
    return (u32)(((uint64_t)sysGetMicroseconds() * 465525ull) / 10000ull);
}

/* ------------------------------------------------------------------------ */
/* Real-OS-thread kernel                                                     */
/* ------------------------------------------------------------------------
 *
 * Each game thread becomes a real pthread with its own 8 MB stack (the N64
 * gave each thread a dedicated sp_* stack; sharing one host stack across
 * green threads is what broke the setjmp design). Message queues carry a
 * side-table entry with a mutex + condition variable: osRecvMesg(BLOCK)
 * waits on it, osSendMesg() signals it. A dedicated tick thread paces the
 * VI retrace message and services software timers.
 */

#define PORT_MAX_THREADS 16
#define PORT_MAX_QUEUES  64
#define PORT_THREAD_STACK (8u * 1024u * 1024u)

typedef struct PortThread {
    OSThread *os;       /* game-visible thread object */
    OSId id;
    void (*entry)(void *);
    void *arg;
    pthread_t th;
    unsigned long tid;  /* OS thread id (thread dumps) */
    int started;        /* osStartThread called */
    int exited;         /* entry returned / stopped / parked (idle) */
} PortThread;

static PortThread g_pt[PORT_MAX_THREADS];

/* Side table: OSMesgQueue* -> lock/cond. The game's OSMesgQueue struct has a
 * fixed N64 layout, so the synchronization state lives here instead. */
typedef struct PortQueue {
    OSMesgQueue *os;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int loggedFirstBlock; /* heartbeat aid: log each queue's first blocker */
} PortQueue;

static PortQueue g_pq[PORT_MAX_QUEUES];
static int g_pqCount = 0;

/* Heartbeat: if no frame has rendered for a while, dump thread + queue
 * states so a hang shows up in the log instead of as a silent
 * "Not Responding" window. */
static uint64_t g_lastFrameUs = 0;
static uint64_t g_lastHeartbeatUs = 0;
static int g_framesRendered = 0;

/* Forward decls: the VI retrace registration lives further down. */
static OSMesgQueue *g_viRetraceMQ = NULL;
static OSMesg g_viRetraceMsg = 0;

/* Vsync tick state: one VI retrace message per FRAME, at the console's
 * frame rate (the N64 VI interrupt fires once per frame; GE calls
 * osViSetEvent with NUM_FIELDS=1, i.e. "every retrace"). */
static uint64_t g_nextTickUs = 0;
static uint32_t g_tickIntervalUs = 1000000 / 60; /* NTSC frame rate */

static PortThread *portFind(OSThread *t)
{
    for (int i = 0; i < PORT_MAX_THREADS; ++i) {
        if (g_pt[i].os == t) return &g_pt[i];
    }
    return NULL;
}

static PortQueue *portQueueGet(OSMesgQueue *mq);
static void portPostVIEvent(void);
static void portServiceTimers(void);
static uint64_t portNextTimerUs(void);

static void portHeartbeatCheck(void)
{
    uint64_t now = sysGetMicroseconds();
    if (now - g_lastFrameUs > 3000000 && now - g_lastHeartbeatUs > 2000000) {
        g_lastHeartbeatUs = now;
        sysLogPrintf(LOG_ERROR,
            "kernel heartbeat: no frame rendered for %llu ms (frames=%d); state:",
            (unsigned long long)(now - g_lastFrameUs), g_framesRendered);
        for (int i = 0; i < PORT_MAX_THREADS; ++i) {
            PortThread *t = &g_pt[i];
            if (!t->os) continue;
            sysLogPrintf(LOG_ERROR,
                "  T%d(id=%d) os=%p entry_rel=%p started=%d exited=%d",
                i, (int)t->id, (void *)t->os,
                (void *)((uintptr_t)t->entry - sysImageBase()),
                t->started, t->exited);
        }
        for (int i = 0; i < g_pqCount; ++i) {
            OSMesgQueue *mq = g_pq[i].os;
            if (!mq) continue;
            sysLogPrintf(LOG_ERROR,
                "  mq=%p valid=%d/%d first=%d",
                (void *)mq, mq->validCount, mq->msgCount, mq->first);
        }
        sysLogPrintf(LOG_ERROR,
            "  viRetraceMQ=%p msg=%llu tickInterval=%u us",
            (void *)g_viRetraceMQ,
            (unsigned long long)g_viRetraceMsg, g_tickIntervalUs);

        /* Where is every thread actually stuck? Unwind them all. */
        /* Indexed by game OSId (thread_config.h): RMON=0 IDLE=1 SCHED=2
         * MAIN=3 AUDI=4 TLB=5. */
        static const char *threadNames[6] = {
            "rmonThread", "idleThread", "shedThread",
            "mainThread", "audioThread", "tlbThread"
        };
        unsigned long tids[PORT_MAX_THREADS];
        const char *names[PORT_MAX_THREADS];
        int n = 0;
        for (int i = 0; i < PORT_MAX_THREADS && n < PORT_MAX_THREADS; ++i) {
            if (g_pt[i].os && g_pt[i].started && !g_pt[i].exited) {
                tids[n] = g_pt[i].tid;
                names[n] = threadNames[g_pt[i].id >= 0 && g_pt[i].id < 6 ? g_pt[i].id : 3];
                ++n;
            }
        }
        crashDumpThreads(tids, names, n);
    }
}

/* Dedicated pacemaker: posts the VI retrace message once per frame and
 * services software timers. Runs forever on its own pthread so pacing
 * continues no matter which game threads are blocked where. */
static void *portTickThread(void *arg)
{
    (void)arg;
    for (;;) {
        uint64_t now = sysGetMicroseconds();
        if (!g_nextTickUs) g_nextTickUs = now + g_tickIntervalUs;

        /* Sleep until the earlier of the next tick and a due timer, in
         * small chunks (simple, portable, accurate enough for 16.7 ms). */
        uint64_t wake = g_nextTickUs;
        uint64_t tnext = portNextTimerUs();
        if (tnext < wake) wake = tnext;
        while ((now = sysGetMicroseconds()) < wake) {
            uint64_t chunk = wake - now;
            if (chunk > 4000) chunk = 4000;
            sysSleep((uint32_t)chunk);
        }

        now = sysGetMicroseconds();
        if (now >= g_nextTickUs) {
            g_nextTickUs += g_tickIntervalUs;
            if (g_nextTickUs <= now) g_nextTickUs = now + g_tickIntervalUs;
            portPostVIEvent();
        }
        portServiceTimers();
        portHeartbeatCheck();
    }
    return NULL;
}

/* Call once from main() before running game code. */
void portKernelInit(void)
{
    memset(g_pt, 0, sizeof(g_pt));
    g_pqCount = 0;
    /* Anchor the heartbeat clock so the first check doesn't compare against
     * epoch 0 (the host QPC has a large absolute offset). */
    g_lastFrameUs = sysGetMicroseconds();
    g_lastHeartbeatUs = g_lastFrameUs;

    if (osTvType == OS_TV_MPAL || osTvType == OS_TV_PAL) {
        g_tickIntervalUs = 1000000 / 50; /* PAL: 50 frames/s -> 20ms */
    } else {
        g_tickIntervalUs = 1000000 / 60; /* NTSC: 60 frames/s -> ~16.7ms */
    }

    pthread_t th;
    if (pthread_create(&th, NULL, portTickThread, NULL) != 0) {
        sysFatalError("portKernelInit: pthread_create(tick) failed");
    }
    pthread_detach(th);
}

/* ------------------------------------------------------------------------ */
/* Threads                                                                   */
/* ------------------------------------------------------------------------ */

void imThreadExitRelease(void);   /* D152: defined in the interrupt-mask section below */

/* pthread entry wrapper. */
static void *portThreadWrapper(void *arg)
{
    PortThread *pt = (PortThread *)arg;

#if defined(PLATFORM_WINDOWS)
    pt->tid = GetCurrentThreadId();
#else
    pt->tid = (unsigned long)pthread_self();
#endif

    /* The N64 idle thread is an infinite no-yield loop (idleproc). On the
     * host it would just burn a core and nothing depends on it running, so
     * park it instead. */
    if (pt->id == 1 /* IDLE_THREAD_ID */) {
        pt->exited = 1;
        for (;;) sysSleep(1000000);
        return NULL;
    }

    pt->entry(pt->arg);
    imThreadExitRelease();   /* D152: don't leak an OS_IM_NONE section on exit */
    pt->exited = 1;
    if (pt->os) pt->os->state = OS_STATE_STOPPED;
    return NULL;
}

void osCreateThread(OSThread *t, OSId id, void (*entry)(void *), void *arg,
                    void *sp, OSPri prio)
{
    PortThread *pt = NULL;
    (void)sp; /* each host thread gets its own stack */
    for (int i = 0; i < PORT_MAX_THREADS; ++i) {
        if (!g_pt[i].os) { pt = &g_pt[i]; break; }
    }
    if (!pt) {
        sysFatalError("osCreateThread: out of port thread slots");
        return;
    }

    memset(pt, 0, sizeof(*pt));
    pt->os = t;
    pt->id = id;
    pt->entry = entry;
    pt->arg = arg;

    t->id = id;
    t->priority = prio;
    t->state = OS_STATE_STOPPED;
}

void osStartThread(OSThread *t)
{
    PortThread *pt = portFind(t);
    if (!pt || pt->started) return;
    pt->started = 1;
    t->state = OS_STATE_RUNNABLE;

    pthread_attr_t attr;
    int rc;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, PORT_THREAD_STACK);
    rc = pthread_create(&pt->th, &attr, portThreadWrapper, pt);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        sysFatalError("osStartThread: pthread_create failed (%d)", rc);
    }
}

void osStopThread(OSThread *t)
{
    PortThread *pt = portFind(t);
    if (!pt) return;
    /* A host thread cannot be killed mid-flight; mark it and let the entry
     * run to completion (game code only stops threads at quiescent points). */
    pt->exited = 1;
    t->state = OS_STATE_STOPPED;
}

void osDestroyThread(OSThread *t)
{
    PortThread *pt = portFind(t);
    if (!pt) return;
    pt->os = NULL;
    t->id = 0;
}

void osYieldThread(void)
{
    /* GE rarely yields explicitly; a 1 ms sleep is a fine host equivalent
     * (sched_yield() would need MinGW's <sched.h>, which the game's own
     * src/sched.h shadows on the include path). */
    sysSleep(1);
}

OSPri osGetThreadPri(OSThread *t)
{
    return t ? t->priority : 0;
}

void osSetThreadPri(OSThread *t, OSPri prio)
{
    if (t) t->priority = prio;
}

/* ------------------------------------------------------------------------ */
/* Message queues                                                            */
/* ------------------------------------------------------------------------ */

/* D59: the lookup/create below MUST be serialized. Two threads first
 * touching the same (or different) OSMesgQueue concurrently used to race on
 * g_pqCount and end up with two PortQueues — or one re-initialized slot —
 * for a single queue. Sends then signal a cond the receiver never waits on
 * (lost wakeup: permanent stall after frame 2), and re-initing a live mutex
 * is UB that can corrupt unrelated state (wild-pointer crashes). Boot-time
 * osCreateMesgQueue() pre-registers most queues, but first-touches still race
 * for any queue created later or touched before its create call. */
static pthread_mutex_t s_pqLock = PTHREAD_MUTEX_INITIALIZER;

static PortQueue *portQueueGet(OSMesgQueue *mq)
{
    pthread_mutex_lock(&s_pqLock);
    for (int i = 0; i < g_pqCount; ++i) {
        if (g_pq[i].os == mq) {
            pthread_mutex_unlock(&s_pqLock);
            return &g_pq[i];
        }
    }
    if (g_pqCount >= PORT_MAX_QUEUES) {
        sysFatalError("portQueueGet: out of port queue slots");
    }
    PortQueue *pq = &g_pq[g_pqCount++];
    pq->os = mq;
    pthread_mutex_init(&pq->lock, NULL);
    pthread_cond_init(&pq->cond, NULL);
    pq->loggedFirstBlock = 0;
    pthread_mutex_unlock(&s_pqLock);
    return pq;
}

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *first, s32 count)
{
    memset(mq, 0, sizeof(*mq));
    mq->msg = first;
    mq->msgCount = count;
    (void)portQueueGet(mq); /* register lock/cond */
}

OSMesg osDequeueMesg(OSMesgQueue *mq)
{
    PortQueue *pq = portQueueGet(mq);
    pthread_mutex_lock(&pq->lock);
    OSMesg m = NULL;
    if (mq->validCount > 0) {
        m = mq->msg[mq->first];
        mq->first = (mq->first + 1) % mq->msgCount;
        --mq->validCount;
        pthread_cond_signal(&pq->cond);
    }
    pthread_mutex_unlock(&pq->lock);
    return m;
}

OSMesg osMesgQueueLast(OSMesgQueue *mq)
{
    PortQueue *pq = portQueueGet(mq);
    pthread_mutex_lock(&pq->lock);
    OSMesg m = (mq->validCount > 0)
        ? mq->msg[(mq->first + mq->validCount - 1) % mq->msgCount]
        : NULL;
    pthread_mutex_unlock(&pq->lock);
    return m;
}

void osEnqueueMesg(OSMesgQueue *mq, OSMesg msg)
{
    PortQueue *pq = portQueueGet(mq);
    pthread_mutex_lock(&pq->lock);
    if (mq->validCount < mq->msgCount) {
        mq->msg[(mq->first + mq->validCount) % mq->msgCount] = msg;
        ++mq->validCount;
        pthread_cond_signal(&pq->cond);
    }
    pthread_mutex_unlock(&pq->lock);
}

/* TEMP D62: full message-flow trace (env GE_D62=1 -> d62mesg.log). */
static FILE *s_d62log = NULL;
static int s_d62opened = 0;
static pthread_mutex_t s_d62lock = PTHREAD_MUTEX_INITIALIZER;
static void d62log(const char *op, OSMesgQueue *mq, OSMesg msg)
{
    if (!s_d62opened) {
        s_d62opened = 1;
        if (getenv("GE_D62"))
            s_d62log = fopen("d62mesg.log", "w");
    }
    if (s_d62log) {
        pthread_mutex_lock(&s_d62lock);
        fprintf(s_d62log, "%s mq=%p msg=%p valid=%d/%d first=%d\n",
                op, (void *)mq, (void *)msg, mq->validCount,
                mq->msgCount, mq->first);
        fflush(s_d62log);
        pthread_mutex_unlock(&s_d62lock);
    }
}


s32 osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flag)
{
    PortQueue *pq = portQueueGet(mq);
    d62log("SEND", mq, msg); /* TEMP D62 */
    /* TEMP D51: watch sends to the 32-slot queues (sched cmdQ + client Qs) */
    if (getenv("GE_D51") && mq->msgCount == 32 && !(g_viRetraceMQ && mq == g_viRetraceMQ)) {
        void *ra = __builtin_return_address(0);
        sysLogPrintf(LOG_NOTE, "D51 send32 mq=%p from %p msg=%p valid=%d/%d",
                     (void *)mq, ra, (void *)msg, mq->validCount, mq->msgCount);
    }
    pthread_mutex_lock(&pq->lock);
    while (mq->validCount >= mq->msgCount) {
        if (flag != OS_MESG_BLOCK) {
            pthread_mutex_unlock(&pq->lock);
            return -1;
        }
        pthread_cond_wait(&pq->cond, &pq->lock);
    }
    mq->msg[(mq->first + mq->validCount) % mq->msgCount] = msg;
    ++mq->validCount;
    pthread_cond_signal(&pq->cond);
    pthread_mutex_unlock(&pq->lock);
    return 0;
}

/* TEMP D60: gfxFrameMsgQ receive watch (env GE_D60=1). The main loop only
 * acts on msg type 1 (retrace) / 4 (RSP done); anything else means the
 * message object's .type field was clobbered. */
extern OSMesgQueue gfxFrameMsgQ; /* src/init.c */
static void d60logRecv(OSMesgQueue *mq, OSMesg m) {
    if (mq != &gfxFrameMsgQ || !getenv("GE_D60")) return;
    static uint64_t n = 0;
    const u16 type = *(const u16 *)m; /* OSScMsg.type */
    if (n < 20 || (n % 300) == 0 || (type != 1 && type != 4))
        sysLogPrintf(LOG_NOTE, "D60 recv #%llu mq=%p msg=%p type=%u",
                     (unsigned long long)n, (void *)mq, (void *)m, type);
    ++n;
}

s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flag)
{
    PortQueue *pq = portQueueGet(mq);
    d62log("RECV", mq, 0); /* TEMP D62 */
    pthread_mutex_lock(&pq->lock);
    while (mq->validCount == 0) {
        if (flag != OS_MESG_BLOCK) {
            pthread_mutex_unlock(&pq->lock);
            return -1;
        }
        /* Log each queue's first blocking call site (the return address is
         * the caller); symbolicate with addr2line to find where in game code
         * this wait happens. */
        if (!pq->loggedFirstBlock) {
            pq->loggedFirstBlock = 1;
            void *ra = __builtin_return_address(0);
            sysLogPrintf(LOG_NOTE, "first block on mq=%p from %p (rel %p)",
                         (void *)mq, ra,
                         (void *)((uintptr_t)ra - sysImageBase()));
        }
        pthread_cond_wait(&pq->cond, &pq->lock);
    }
    OSMesg m = mq->msg[mq->first];
    mq->first = (mq->first + 1) % mq->msgCount;
    --mq->validCount;
    if (msg) *msg = m;
    d60logRecv(mq, m); /* TEMP D60 */
    pthread_cond_signal(&pq->cond);
    pthread_mutex_unlock(&pq->lock);
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Events (osSetEventMesg: "interrupt" -> message queue mapping)             */
/* ------------------------------------------------------------------------ */

typedef struct {
    OSMesgQueue *mq;
    OSMesg msg;
    int used;
} PortEvent;

static PortEvent g_events[16];

void osSetEventMesg(OSEvent e, OSMesgQueue *mq, OSMesg msg)
{
    if (e < 16) {
        g_events[e].mq = mq;
        g_events[e].msg = msg;
        g_events[e].used = 1;
    }
}

/* D134: an SP/DP "task done" event must NEVER be dropped.
 *
 * These land in the scheduler's 8-slot interruptQ, which the 60 Hz VI
 * pacemaker also posts VIDEO_MSG into. osSpTaskStartGo runs the whole frame
 * synchronously on the sched thread (fast3d), so a slow frame (the first two
 * are 30-80 ms) lets the pacemaker queue several retraces meanwhile. Once the
 * queue is full the NOBLOCK done-post is silently discarded, __scMain never
 * clears sc->curRSPTask, the client never gets OS_SC_DONE_MSG, and the main
 * loop's pendingGfx never clears -> permanent stall at frames=2.
 *
 * OS_MESG_BLOCK is NOT the fix: the done event is posted from the sched thread
 * itself, the only consumer of that queue, so blocking self-deadlocks. Instead
 * make room by dropping the OLDEST message (always a stale retrace -- retrace
 * drops are already normal, N64 osViSetEvent posts NOBLOCK too). */
static void portPostEventForce(OSId e)
{
    PortEvent *ev = &g_events[e];
    if (!ev->used || !ev->mq) return;
    if (osSendMesg(ev->mq, ev->msg, OS_MESG_NOBLOCK) == 0) return;
    (void)osDequeueMesg(ev->mq);
    if (osSendMesg(ev->mq, ev->msg, OS_MESG_NOBLOCK) != 0) {
        sysLogPrintf(LOG_ERROR, "D134: task-done event %d lost (mq=%p full)",
                     (int)e, (void *)ev->mq);
    }
}

static void portPostEvent(OSId e)
{
    PortEvent *ev = &g_events[e];
    if (ev->used && ev->mq) {
        osSendMesg(ev->mq, ev->msg, OS_MESG_NOBLOCK);
    }
}

/* ------------------------------------------------------------------------ */
/* VI (video) — retrace pacing + framebuffer bookkeeping                    */
/* ------------------------------------------------------------------------ */

static int g_viBlack = 1; /* start black, like the N64 VI */

void osViSetEvent(OSMesgQueue *mq, OSMesg msg, u32 retraceCount)
{
    (void)retraceCount;
    g_viRetraceMQ = mq;
    g_viRetraceMsg = msg;
}

/* TEMP D51: instrument the retrace pacemaker */
static uint64_t g_viPostCount = 0;
static void portPostVIEvent(void)
{
    if (g_viRetraceMQ) {
        /* D134: keep two slots free for the SP/DP done events posted at the
         * end of a synchronous fast3d frame. A retrace posted into a backlog
         * is stale anyway -- sched has not drained the previous one yet. */
        if (g_viRetraceMQ->validCount >= g_viRetraceMQ->msgCount - 2) return;
        s32 r = osSendMesg(g_viRetraceMQ, g_viRetraceMsg, OS_MESG_NOBLOCK);
        if (++g_viPostCount % 60 == 1 || r != 0) {
            sysLogPrintf(r != 0 ? LOG_ERROR : LOG_NOTE,
                         "D51 vi post #%llu mq=%p msg=%d valid=%d/%d ret=%d",
                         (unsigned long long)g_viPostCount, (void *)g_viRetraceMQ,
                         (int)g_viRetraceMsg, g_viRetraceMQ->validCount,
                         g_viRetraceMQ->msgCount, r);
        }
    }
}
/* END TEMP D51 */

void osViSetMode(OSViMode *vm)
{
    if (!vm) return;
    int pal = (vm->comRegs.ctrl & OS_VI_BIT_PAL) != 0;
    /* D103: fast3d's SCREEN_WIDTH/SCREEN_HEIGHT (== the native viewport) must
     * be the CFB space GE authors its gSPViewport / G_SETSCISSOR / 2D texrect
     * commands in — that is bufx x bufy (320x240 NTSC, 320x272 PAL), NOT the
     * visible scanline count.  comRegs.width already carries bufx; recover the
     * matching CFB height from fldRegs.yScale (GE sets it to
     * bufy * YSCALE_MAX(0x800) / SCREEN_HEIGHT_MAX(480), fr.c) instead of the
     * old hard-coded 480/400, which made RATIO_Y half of RATIO_X and squashed
     * every frame into a half-height band. */
    u32 w = vm->comRegs.width;
    s32 h = (s32)(((vm->fldRegs[0].yScale & 0xFFF) * 480u) / 0x800u);
    if (h <= 0 || h > 480) h = pal ? 272 : 240; /* fallback: LAN1 CFB height */
    if (w > 640) w = 640;
    videoUpdateNativeResolution((s32)w, h);
}

void osViSetSpecialFeatures(u32 f) { (void)f; }
void osViVSyncCallback(OSMesgQueue *mq, OSMesg msg)
{
    /* Legacy VI sync hook; retrace comes from osViSetEvent instead. */
    (void)mq; (void)msg;
}
void osViWaitVSync(void) { /* pacing is handled by the kernel tick */ }
void osViSetSync(OSMesgQueue *mq, OSMesg msg) { (void)mq; (void)msg; }
void osViBlack(u8 active) { g_viBlack = active; (void)g_viBlack; }
void osViSetYScale(f32 value) { (void)value; }
void osViSetXScale(f32 value) { (void)value; }

/* __scTaskReady() requires these to compare equal, otherwise no gfx task
 * ever runs. The software RSP draws straight into the GL back buffer, so a
 * single constant "framebuffer" is all that's needed. */
static u32 g_dummyFramebuffer = 0x1;
void *osViGetCurrentFramebuffer(void) { return &g_dummyFramebuffer; }
void *osViGetNextFramebuffer(void)    { return &g_dummyFramebuffer; }

/* The frame was already presented by videoEndFrame() inside
 * osSpTaskStartGo(); nothing to do here. */
void osViSwapBuffer(void *fb) { (void)fb; }
void osViRepeatLine(u8 line) { (void)line; }

/* ------------------------------------------------------------------------ */
/* AI (audio) — map onto the port audio layer. Phase 3.                     */
/* ------------------------------------------------------------------------ */

s32 osAiSetFrequency(u32 hz)
{
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
 * base (0x10000000), so an OS_READ from a cart address is a plain memcpy.
 * The N64 PI hardware posts the caller's OSMesgPI to the message queue on
 * completion; osPiStartDma() below replicates that, because the game blocks
 * in romReceiveMesg()/osRecvMesg() after every romCopy().
 */
/* TEMP D60: identify sidecar model reads (env GE_D60=1). srcPA-base is the
 * manifest offset; cross-reference data/pcmodels-<region>/manifest.csv for
 * the file name. */
extern uintptr_t pcmodelsSidecarBase(void);
extern uint32_t  pcmodelsTotalSize(void);
/* D69: same diagnostic for the bg/stan sidecar image. */
extern uintptr_t pccgSidecarBase(void);
extern uint32_t  pccgTotalSize(void);
static void d60logSidecarRead(u32 srcPA, void *dstVA, u32 size) {
    if (!getenv("GE_D60")) return;
    uintptr_t base = pcmodelsSidecarBase();
    if (base && srcPA >= base && srcPA < base + pcmodelsTotalSize()) {
        sysLogPrintf(LOG_NOTE, "D60 sidecar read off=%llu size=0x%X dst=%p",
                     (unsigned long long)(srcPA - base), size, dstVA);
        return;
    }
    uintptr_t cgBase = pccgSidecarBase();
    if (cgBase && srcPA >= cgBase && srcPA < cgBase + pccgTotalSize())
        sysLogPrintf(LOG_NOTE, "D69 pccg sidecar read off=%llu size=0x%X dst=%p",
                     (unsigned long long)(srcPA - cgBase), size, dstVA);
}

/* TEMP D60/D61: a ROM-read DMA target must land in the game DRAM views
 * (V1/V2) or the current thread's stack (texLoad's compbuffer). Anything
 * else is unmapped host memory on PC. */
static FILE *s_d61log = NULL;
static int s_d61opened = 0;

static int dramHostAddrValid(uintptr_t addr, u32 size)
{
    static const uintptr_t bases[2] = { 0x70000000UL, 0x80000000UL };
    for (int i = 0; i < 2; i++) {
        if (addr >= bases[i] && addr + size <= bases[i] + 0x00800000UL)
            return 1;
    }
    /* Any other host-committed region is a legitimate DMA target: .bss/.data
     * buffers (e.g. ramrom_data_target), stack compbuffers, sidecar images.
     * Truncated wild addresses (0x40xxxxxx from s32 pointer math) are not
     * committed, so VirtualQuery still catches them. */
#if defined(PLATFORM_WINDOWS)
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi) &&
            mbi.State == MEM_COMMIT &&
            (uintptr_t)mbi.BaseAddress <= addr &&
            (uintptr_t)mbi.BaseAddress + mbi.RegionSize >= addr + size)
            return 1;
    }
    return 0;
#else
    /* No cheap committed-memory probe on POSIX; this is a TEMP D60 diagnostic.
     * Be permissive so legitimate .bss/stack/sidecar DMA targets are not
     * flagged as fatal (a genuinely wild target still faults in the memcpy). */
    (void)size;
    return 1;
#endif
}

static void piServiceDma(s32 direction, u32 srcPA, void *dstVA, u32 size)
{
    if (size == 0)
        return;
    if (direction == OS_READ) {
        d60logSidecarRead(srcPA, dstVA, size); /* TEMP D60 */
        if (!romdataCartAddrValid(srcPA, size)) {
            sysLogPrintf(LOG_WARNING,
                         "osPiStartDma: ROM read out of range "
                         "(src=0x%08X size=0x%X); skipped",
                         srcPA, size);
            return;
        }
        /* TEMP D60: validate the DMA target too. The N64 PI happily DMAs to
         * any KSEG address; on PC an unmapped target is a wild memcpy.
         * Log the whole call chain (romCopy <- doRomCopy <- osPiStartDma) so
         * the game-side caller can be symbolicated offline. */
        /* TEMP D61: log every ROM read (dst/src/size). The last line before
         * a crash identifies the culprit; dst symbolizes offline via nm. */
        if (!s_d61opened && getenv("GE_D61")) {
            s_d61log = fopen("d61dma.log", "w");
            s_d61opened = 1;
        }
        if (s_d61log) {
            static int d61count = 0;
            fprintf(s_d61log, "D61 %06d dst=%p src=0x%08X size=0x%X\n",
                    ++d61count, dstVA, srcPA, size);
            fflush(s_d61log);
        }
        if (!dramHostAddrValid((uintptr_t)dstVA, size)) {
            /* No __builtin_return_address here: the caller's frame chain is
             * not always walkable (it faulted). Log the raw stack window
             * instead — return addresses are in it and symbolicate offline.
             * The FATAL below re-dumps RSP/registers via the crash handler. */
            const uint64_t *sp = (const uint64_t *)__builtin_frame_address(0);
            char win[1200] = "";
            char *wp = win;
            for (int i = 0; i < 32; i++) {
                wp += snprintf(wp, win + sizeof(win) - (wp - win),
                               " %p", (void *)sp[i]);
            }
#if defined(PLATFORM_WINDOWS)
            {
                PVOID tlow = NULL, thigh = NULL;
                GetCurrentThreadStackLimits(&tlow, &thigh);
                sysLogPrintf(LOG_ERROR,
                             "D60 thread stack: %p..%p  dst-in-stack=%d\n",
                             (void *)tlow, (void *)thigh,
                             ((uintptr_t)dstVA >= (uintptr_t)tlow &&
                              (uintptr_t)dstVA < (uintptr_t)thigh));
            }
#else
            /* TEMP D60 diagnostic: no portable committed-stack query without
             * _GNU_SOURCE (pthread_getattr_np); the raw stack window above and
             * the crash handler's register dump are enough to symbolicate. */
            sysLogPrintf(LOG_ERROR, "D60 thread stack: (n/a on POSIX)\n");
#endif
            sysLogPrintf(LOG_ERROR,
                         "D60 BAD DMA TARGET dst=%p src=0x%08X size=0x%X "
                         "stack@rbp:%s\n",
                         dstVA, srcPA, size, win);
            sysFatalError("D60: ROM-read target %p + 0x%X not host-mapped "
                          "(src=0x%08X)", dstVA, size, srcPA);
        }
        memcpy(dstVA, (const void *)(uintptr_t)srcPA, size);
    } else {
        /* OS_WRITE: the game never writes the cart (saves go to EEPROM via
         * osEeprom*, shimmed separately). Log and drop. */
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
    (void)prio;
    /* Post the completion message exactly like the PI hardware would —
     * even when the DMA was skipped/dropped above, so callers never
     * deadlock in osRecvMesg(). */
    if (mq) {
        osSendMesg(mq, mesg ? (OSMesg)mesg : (OSMesg)1, OS_MESG_BLOCK);
    }
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
 * it to read the cartridge token string in 32-bit words starting at
 * 0xFFB000. On the PC there is no cartridge token register, so serve the
 * bytes of the host command line (sysGetTokenString) instead — this makes
 * N64 debug switches like "-level_09" / "-hard1" work on PC. Each word is
 * packed so byte N of the string lands at the lower address (matches the
 * N64's in-order byte delivery once read back through the char buffer).
 * Any other address reads as 0.
 */
#define PORT_TOKEN_IO_BASE 0xFFB000u

s32 osPiReadIo(u32 devAddr, u32 *data)
{
    if (!data)
        return 0;

    if (devAddr >= PORT_TOKEN_IO_BASE &&
        devAddr < PORT_TOKEN_IO_BASE + 0x1000) {
        const char *tok = sysGetTokenString();
        u32 off = devAddr - PORT_TOKEN_IO_BASE;
        u32 len = (u32)strlen(tok);
        u32 w = 0;
        for (u32 i = 0; i < 4; ++i) {
            u8 c = (off + i < len) ? (u8)tok[off + i] : 0;
            w |= (u32)c << (8 * i);
        }
        *data = w;
        return 0;
    }

    *data = 0;
    return 0;
}

/* ------------------------------------------------------------------------ */
/* SI (controller) — keyboard-backed.                                       */
/* ------------------------------------------------------------------------ */

/* errno is a macro in C (errno.h); the N64 structs have an `errno` field.
 * Undefine it for this section and restore it afterwards. */
#pragma push_macro("errno")
#undef errno

static OSContStatus g_contStatus[MAXCONTROLLERS];
static OSContPad g_contPad[MAXCONTROLLERS];
static u8 g_contConnected = 0x1; /* controller 0 connected */

/* Snapshot all controllers from the SDL input module (port/src/input.c).
 * That module owns the keyboard/mouse/gamepad -> N64 mapping and the
 * mouse-look -> C-button bridge; this just marshals its output into the
 * OSContPad / OSContStatus arrays joy.c reads via osContGetReadData(). */
static void contSnapshotFromKeyboard(void)
{
    inputUpdate();

    const s32 mask = inputConnectedMask();
    g_contConnected = (u8)mask;

    for (int i = 0; i < MAXCONTROLLERS; ++i) {
        const int connected = (mask & (1 << i)) != 0;
        s8 sx = 0, sy = 0;
        u16 button = connected ? (u16)inputComputePad(i, &sx, &sy) : 0;

        g_contStatus[i].type   = connected ? CONT_TYPE_NORMAL : 0;
        g_contStatus[i].status = 0;
        g_contStatus[i].errno  = connected ? 0 : CONT_NO_RESPONSE_ERROR;

        g_contPad[i].button  = button;
        g_contPad[i].stick_x = connected ? sx : 0;
        g_contPad[i].stick_y = connected ? sy : 0;
        g_contPad[i].errno   = connected ? 0 : CONT_NO_RESPONSE_ERROR;
    }
}

#pragma pop_macro("errno")

s32 osContInit(OSMesgQueue *mesgq, u8 *bitpattern, OSContStatus *data)
{
    (void)data; /* the game passes its own array; osContGetQuery fills it */
    if (bitpattern) *bitpattern = g_contConnected;
    return 0;
}

s32 osContStartReadData(OSMesgQueue *mesgq)
{
    contSnapshotFromKeyboard();
    /* Complete immediately: post the SI "done" message so a blocking
     * osRecvMesg() right after (joy.c) returns at once. */
    if (mesgq) osSendMesg(mesgq, NULL, OS_MESG_NOBLOCK);
    return 0;
}

s32 osContStartQuery(OSMesgQueue *mq)
{
    contSnapshotFromKeyboard();
    if (mq) osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    return 0;
}

void osContGetQuery(OSContStatus *status)
{
    memcpy(status, g_contStatus, sizeof(g_contStatus));
}

void osContGetReadData(OSContPad *pad)
{
    /* N64 semantics: fill one OSContPad per controller channel. joy.c passes
     * g_ContData[0].samples[i].pads (a MAXCONTROLLERS-long array). */
    memcpy(pad, g_contPad, sizeof(g_contPad));
}

s32 osContReset(OSMesgQueue *mq, OSContStatus *status)
{
    (void)mq;
    if (status) memcpy(status, g_contStatus, sizeof(g_contStatus));
    return 0;
}

/* EEPROM: file-backed 16Kbit save (Phase 4, BACKLOG B5). GE addresses the
 * device in 8-byte blocks (src/game/file2.c: checksum @block 0, five
 * save_data slots from block 4). Backed by $S/ge007.eep, loaded lazily on
 * first access and rewritten on every write. Pattern mirrors the PD port
 * (pd_port/port/src/libultra.c). */
#define GE_EEP_BLOCKS  EEP16K_MAXBLOCKS          /* 256 */
#define GE_EEP_SIZE    (GE_EEP_BLOCKS * 8)       /* 2048 bytes */
#define GE_EEP_PATH    "$S/ge007.eep"

static u8   s_eeprom[GE_EEP_SIZE];
static int  s_eepromLoaded = 0;

static void geEepromLoad(void)
{
    if (s_eepromLoaded) return;
    s_eepromLoaded = 1;
    const char *path = sysResolvePath(GE_EEP_PATH);
    FILE *fp = fopen(path, "rb");
    if (fp) {
        fread(s_eeprom, 1, GE_EEP_SIZE, fp);
        fclose(fp);
        sysLogPrintf(LOG_INFO, "eeprom: loaded %s", path);
    } else {
        memset(s_eeprom, 0, GE_EEP_SIZE);
        sysLogPrintf(LOG_INFO, "eeprom: no %s yet (fresh save)", path);
    }
}

static void geEepromStore(void)
{
    const char *path = sysResolvePath(GE_EEP_PATH);
    FILE *fp = fopen(path, "wb");
    if (fp) {
        fwrite(s_eeprom, 1, GE_EEP_SIZE, fp);
        fclose(fp);
    } else {
        sysLogPrintf(LOG_ERROR, "eeprom: cannot write %s", path);
    }
}

static s32 geEepromRW(u8 block, u8 *buf, int nbytes, int write)
{
    u32 off = (u32)block * 8;
    if (!buf || nbytes < 0 || off + (u32)nbytes > GE_EEP_SIZE) return -1;
    geEepromLoad();
    if (write) {
        memcpy(s_eeprom + off, buf, nbytes);
        geEepromStore();
    } else {
        memcpy(buf, s_eeprom + off, nbytes);
    }
    return 0;
}

s32 osEepromProbe(OSMesgQueue *mq) { (void)mq; return EEPROM_TYPE_16K; }
s32 osEepromRead(OSMesgQueue *mq, u8 addr, u8 *buf)
{ (void)mq; return geEepromRW(addr, buf, 8, 0); }
s32 osEepromWrite(OSMesgQueue *mq, u8 addr, u8 *buf)
{ (void)mq; return geEepromRW(addr, buf, 8, 1); }
s32 osEepromLongRead(OSMesgQueue *mq, u8 addr, u8 *buf, int nbytes)
{ (void)mq; return geEepromRW(addr, buf, nbytes, 0); }
s32 osEepromLongWrite(OSMesgQueue *mq, u8 addr, u8 *buf, int nbytes)
{ (void)mq; return geEepromRW(addr, buf, nbytes, 1); }

/* Memory Pak (PFS) + Rumble Pak (motor): no accessories on the PC. */
s32 osPfsInit(OSMesgQueue *queue, OSPfs *pfs, int channel)
{ (void)queue; (void)pfs; (void)channel; return PFS_ERR_NOPACK; }
s32 osPfsIsPlug(OSMesgQueue *queue, u8 *pattern)
{ (void)queue; if (pattern) *pattern = 0; return 0; }
s32 osMotorInit(OSMesgQueue *mq, OSPfs *pfs, int channel)
{ (void)mq; (void)pfs; (void)channel; return -1; }
s32 osMotorStart(OSPfs *pfs) { (void)pfs; return -1; }
s32 osMotorStop(OSPfs *pfs)  { (void)pfs; return -1; }

/* ------------------------------------------------------------------------ */
/* SP (RSP) — runs the software RSP inline, then posts the done messages    */
/* that sched.c's __scMain waits for.                                       */
/* ------------------------------------------------------------------------ */

void osSpTaskLoad(OSTask *t) { (void)t; /* nothing to load on the host */ }

void osSpTaskStartGo(OSTask *t)
{
    if (t->t.type == M_AUDTASK) {
        /* Phase 3: execute the audio ucode against t->t.data_ptr (an Acmd
         * list). For now the task simply completes; libaudio's amMain still
         * runs its per-frame bookkeeping. */
    } else {
        /* Graphics task: run the software RSP on the display list. */
        uint64_t t0 = sysGetMicroseconds();
        videoStartFrame();
        gfx_run((Gfx *)t->t.data_ptr);
        videoEndFrame();
        g_lastFrameUs = sysGetMicroseconds();
        if (++g_framesRendered <= 5 || (g_framesRendered % 300) == 0)
            sysLogPrintf(LOG_NOTE, "frame %d rendered in %llu us",
                         g_framesRendered,
                         (unsigned long long)(sysGetMicroseconds() - t0));
    }

    portPostEventForce(OS_EVENT_SP);   /* D134: must not be dropped */
    if (t->t.type != M_AUDTASK) {
        /* A gfx task is also its own DP task (sp == dp in __scExec): the
         * DP "finishes" right after the RSP. */
        portPostEventForce(OS_EVENT_DP);
    }
}

void osSpTaskYield(void)             { /* no cooperative RSP on the host */ }
OSYieldResult osSpTaskYielded(OSTask *t) { (void)t; return 0; }
void osSpFlush(void)            { /* no-op */ }
void osSpSetFifo(void *fifo, int size, int flags) { (void)fifo; (void)size; (void)flags; }
void *osSpGetFifo(void)         { return NULL; }
int   osSpTaskDone(void)        { return 1; }

/* ------------------------------------------------------------------------ */
/* RDP — bypassed (the software RSP emits GL directly).                     */
/* ------------------------------------------------------------------------ */

void osDpSetStatus(u32 status) { (void)status; }
u32  osDpGetStatus(void) { return 0; }
s32  osDpSetNextBuffer(void *buf, u64 size)
{
    /* DP-only task: "rendering" is done by the time the RSP finished. */
    (void)buf; (void)size;
    portPostEventForce(OS_EVENT_DP);   /* D134 */
    return 1;
}
void osDpGetCounters(u32 *counters)
{
    if (counters) memset(counters, 0, sizeof(u32) * 4);
}

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
/* Timers / interrupts                                                       */
/* ------------------------------------------------------------------------
 *
 * Software timers. bossInitMainthreadData() paces controller init with a
 * one-shot 100 ms osSetTimer()/osRecvMesg() pair, so timers must actually
 * fire. They are serviced by the kernel tick thread: portServiceTimers()
 * posts due messages and portNextTimerUs() lets the tick thread shorten its
 * sleep to the earliest deadline.
 */

#define PORT_MAX_TIMERS 8

typedef struct PortTimer {
    OSTimer *os;
    uint64_t fireUs;     /* absolute deadline (sysGetMicroseconds domain) */
    uint32_t periodUs;   /* 0 = one-shot */
    int active;
} PortTimer;

static PortTimer g_ptimers[PORT_MAX_TIMERS];

/* cycles -> microseconds at the RSP counter rate. Inverse of
 * OS_USEC_TO_CYCLES(). */
static uint64_t portCyclesToUs(OSTime cycles)
{
    return (uint64_t)((double)cycles * 1000000.0 / (double)osClockRate);
}

int osSetTimer(OSTimer *timer, OSTime start, OSTime period,
               OSMesgQueue *mq, OSMesg msg)
{
    PortTimer *pt = NULL;
    int i;

    timer->interval = period;
    timer->value = start;
    timer->mq = mq;
    timer->msg = msg;

    for (i = 0; i < PORT_MAX_TIMERS; ++i) {
        if (g_ptimers[i].os == timer) { pt = &g_ptimers[i]; break; }
        if (!g_ptimers[i].active && !pt) pt = &g_ptimers[i];
    }
    if (!pt) return -1;

    pt->os = timer;
    pt->active = 1;
    pt->periodUs = (uint32_t)portCyclesToUs(period);
    pt->fireUs = sysGetMicroseconds() + portCyclesToUs(start);
    sysLogPrintf(LOG_NOTE, "timer set: t=%p start=%lluus period=%uus mq=%p",
                 (void *)timer, (unsigned long long)portCyclesToUs(start),
                 pt->periodUs, (void *)mq);
    return 0;
}

int osStopTimer(OSTimer *timer)
{
    int i;
    for (i = 0; i < PORT_MAX_TIMERS; ++i) {
        if (g_ptimers[i].os == timer) {
            g_ptimers[i].active = 0;
            return 0;
        }
    }
    return -1;
}

static void portServiceTimers(void)
{
    uint64_t now = sysGetMicroseconds();
    int i;
    for (i = 0; i < PORT_MAX_TIMERS; ++i) {
        PortTimer *pt = &g_ptimers[i];
        if (!pt->active) continue;
        while (pt->fireUs <= now) {
            sysLogPrintf(LOG_NOTE, "timer fire: t=%p mq=%p",
                         (void *)pt->os, (void *)pt->os->mq);
            osSendMesg(pt->os->mq, pt->os->msg, OS_MESG_NOBLOCK);
            if (!pt->periodUs) { pt->active = 0; break; }
            pt->fireUs += pt->periodUs;
        }
    }
}

/* Earliest timer deadline, or UINT64_MAX. */
static uint64_t portNextTimerUs(void)
{
    uint64_t earliest = ~0ull;
    int i;
    for (i = 0; i < PORT_MAX_TIMERS; ++i) {
        PortTimer *pt = &g_ptimers[i];
        if (pt->active && pt->fireUs < earliest) earliest = pt->fireUs;
    }
    return earliest;
}

/* Interrupt mask -> global recursive lock (D147), self-healing (D152).
 *
 * On N64 `osSetIntMask(OS_IM_NONE)` disables interrupts so the following
 * region cannot be preempted; the paired `osSetIntMask(saved)` restores it.
 * libaudio relies on this to serialise the event queue (alEvtqPostEvent,
 * sndRemoveEvents, alEvtqNextEvent, ...) between the game thread and the
 * audio-manager "interrupt". On PC that audio manager is a real preemptible
 * thread (amMain), and a no-op shim let both threads mutate the same
 * ALEventQueue linked list concurrently -> list corruption -> the game
 * thread spins forever in alEvtqPostEvent's insert walk (hang seen when a
 * door finishes opening and posts its close SFX, propobj.c objTick).
 *
 * Model the mask as one process-wide recursive critical section: OS_IM_NONE
 * acquires, OS_IM_ALL (the value we hand back, so every paired restore
 * passes it) releases. Other specific masks (OS_IM_VI in sched.c) are not
 * lock ops and pass through.
 *
 * D152: the recursive-mutex form CAN wedge permanently. libaudio has
 * unbalanced / early-return mask calls, and a transient host thread can
 * acquire OS_IM_NONE and exit without the paired OS_IM_ALL, leaving the
 * lock owned forever -> every later alEvtq* on mainThread + amMain blocks
 * -> black screen (repro: mission-failed audio fade-out,
 * sndSetScalerApplyVolumeAllSfxSlot posts a release event per active sound
 * per frame). Real audio critical sections are microseconds; anything
 * holding for OS_IM_STUCK_NS is leaked, so a waiter that has blocked that
 * long STEALS the section (logging the stale owner + the caller so the leak
 * site can be found and fixed narrowly later). Bookkeeping lives under
 * s_imMx (only ever held briefly); s_imHeld/s_imOwner/s_imDepth are the
 * logical lock. */
#define OS_IM_STUCK_NS  (2000ll * 1000 * 1000)   /* 2 s */

static pthread_mutex_t s_imMx  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_imCv  = PTHREAD_COND_INITIALIZER;
static int             s_imHeld;
static unsigned long   s_imOwner;
static int             s_imDepth;

static unsigned long imSelf(void)
{
    return (unsigned long)(uintptr_t)pthread_self();
}

static void imAcquire(void *caller)
{
    unsigned long self = imSelf();
    struct timespec now;

    pthread_mutex_lock(&s_imMx);

    if (s_imHeld && s_imOwner == self) {   /* recursive re-entry */
        s_imDepth++;
        pthread_mutex_unlock(&s_imMx);
        return;
    }

    long long startNs = 0;   /* set on first contended iteration */

    while (s_imHeld) {
        struct timespec dl;
        if (startNs == 0) {
            clock_gettime(CLOCK_REALTIME, &now);
            startNs = (long long)now.tv_sec * 1000000000ll + now.tv_nsec;
        }
        clock_gettime(CLOCK_REALTIME, &now);
        long long nowNs = (long long)now.tv_sec * 1000000000ll + now.tv_nsec;
        if (nowNs - startNs >= OS_IM_STUCK_NS) {
            sysLogPrintf(LOG_ERROR,
                "D152: osSetIntMask lock stuck >2s -- stealing from owner=%lu "
                "depth=%d (this caller=%p). A prior OS_IM_NONE was never "
                "restored; find that call site.",
                s_imOwner, s_imDepth, caller);
            break;                          /* fall through, steal it */
        }
        /* wake ~10x/s to re-check the steal deadline */
        long long wakeNs = nowNs + 100ll * 1000 * 1000;
        dl.tv_sec  = (time_t)(wakeNs / 1000000000ll);
        dl.tv_nsec = (long)(wakeNs % 1000000000ll);
        pthread_cond_timedwait(&s_imCv, &s_imMx, &dl);
    }

    s_imHeld  = 1;
    s_imOwner = self;
    s_imDepth = 1;
    pthread_mutex_unlock(&s_imMx);
}

static void imRelease(void)
{
    unsigned long self = imSelf();

    pthread_mutex_lock(&s_imMx);
    if (!s_imHeld || s_imOwner != self) {   /* spurious / already stolen: no-op */
        pthread_mutex_unlock(&s_imMx);
        return;
    }
    if (--s_imDepth <= 0) {
        s_imHeld  = 0;
        s_imOwner = 0;
        s_imDepth = 0;
        pthread_cond_signal(&s_imCv);
    }
    pthread_mutex_unlock(&s_imMx);
}

/* D152: a host thread that returns from its entry function while still
 * "inside" an OS_IM_NONE section (an unbalanced acquire on a code path only
 * that transient thread takes, or an osStopThread that let it bail early)
 * would leave s_imHeld owned forever -> mainThread + amMain both wedge in
 * alEvtq* until the 2 s steal fires, and can re-wedge every cycle once a new
 * host thread reuses the dead thread's pthread id (imAcquire then
 * mis-detects recursion). The steal-lock recovers from it but with a visible
 * hitch; releasing the orphaned section the instant its owner dies removes
 * the hitch entirely. Called from portThreadWrapper on thread exit. */
void imThreadExitRelease(void)
{
    unsigned long self = imSelf();
    pthread_mutex_lock(&s_imMx);
    if (s_imHeld && s_imOwner == self) {
        sysLogPrintf(LOG_ERROR,
            "D152: thread %lu exited still holding osSetIntMask section "
            "(depth=%d) -- releasing the orphan. An OS_IM_NONE on this "
            "thread's path was never restored.", self, s_imDepth);
        s_imHeld  = 0;
        s_imOwner = 0;
        s_imDepth = 0;
        pthread_cond_signal(&s_imCv);
    }
    pthread_mutex_unlock(&s_imMx);
}

OSIntMask osSetIntMask(OSIntMask mask)
{
    if (mask == OS_IM_NONE) {          /* enter critical section */
        imAcquire(__builtin_return_address(0));
        return OS_IM_ALL;             /* paired restore will pass this back */
    }
    if (mask == OS_IM_ALL) {           /* leave critical section */
        imRelease();
        return OS_IM_NONE;
    }
    return mask;                        /* OS_IM_VI etc: not a lock operation */
}

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
    /* VI init (normally src/vi.c, EXCLUDED). The port video layer is
     * already up by the time the game calls this. */
}

/* VI debug message queue (normally src/vi.c, EXCLUDED). fr.c references it. */
OSMesgQueue vi_c_debug_MQ;
