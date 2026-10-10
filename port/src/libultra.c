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
#include <math.h>   /* D583: floor/fabs in the present grid */

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
#include "portaddr.h"
#include "crash.h"   /* D38: crashDumpThreads() */
#include "video.h"
#include "audio.h"
#include "input.h"
#include "fs.h"
#include "romdata.h"
#include "watchsettings.h"
#include "crash.h"

#if defined(PLATFORM_WINDOWS)
#include <windows.h> /* GetCurrentThreadId for thread-dump labels */
#endif

/* fast3d (C++): the software RSP entry point. */
extern void gfx_run(Gfx *commands);
/* D578: frame interpolation (port/fast3d/gfx_pc.cpp). */
extern void gfx_interp_reset(void);
extern int gfx_interp_last_exact(void);
extern int gfx_interp_body_runs(void);
extern void gfx_interp_trigger_counts(unsigned* turn, unsigned* clamp, unsigned* set, unsigned* room);
extern int gfx_interp_tick(Gfx *commands, const float *alphas, int n, int base);
extern void gfx_tick_counts(unsigned *batches, unsigned *tris, unsigned *texloads, unsigned *texmiss, unsigned *dynbytes, unsigned *texus);   /* D583 SLOW line */
extern void videoSyncSplitScreen(void); /* D416 */

/* ------------------------------------------------------------------------ */
/* Globals the game expects to exist (normally set by osInitialize).        */
/* ------------------------------------------------------------------------ */

u64 osClockRate = 6250000;          /* RSP counter rate (Hz) — see PD port */
u32 osMemSize   = 16 * 1024 * 1024; /* pretend 16MB RDRAM (+expansion)     */
/* TV type (normally set from console hardware in os/initialize.c, excluded).
 * Set to what the matching console reports: a European (PAL) N64 reports
 * OS_TV_PAL (0); OS_TV_MPAL (2) is Brazil's 60 Hz PAL-M console. GE's shared
 * code only tests for MPAL (sched.c / init.c / fr.c: MPAL LAN1 mode, else
 * NTSC LAN1), so on a real PAL console those take the NTSC branch and the
 * EU build's own video_related_8 (fr.c, #ifdef VERSION_EU) then installs
 * the PAL modes. D488: this used to be MPAL (a scaffolding-era inference
 * from those checks), which sent the port down the Brazil-only branches. */
#ifdef REFRESH_PAL
u32 osTvType    = OS_TV_PAL;        /* EU: PAL  (0=PAL 1=NTSC 2=MPAL)      */
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

/* GE_DETERM=1 — fixed-tick deterministic mode (speed-ups plan Step 7 / R1,
 * design in docs/dev/findings.md §D117). Test-only, default OFF: replaces
 * the wall-clock osGetCount() below with a virtual clock advanced exactly
 * once per virtual VI retrace (generated synchronously in osRecvMesg() —
 * see the GE_DETERM block there — when __scMain, the sole consumer of
 * g_viRetraceMQ, asks for the next message; NOT paced by an independent
 * thread) instead of real elapsed time, so two runs of the same build take
 * the same simulation path. Never the shipping timing model — the default
 * (GE_DETERM unset) path below is byte-for-byte what it always was. */
static int g_determEnabled = 0;
static u32 g_determQuantum = 775875; /* NTSC; portKernelInit sets 931050 PAL */
static u32 g_determTicks = 0;
/* M-52 3rd attempt: __scMain is the sole consumer of g_viRetraceMQ, but
 * before the first real gfx/audio task ever runs (src/boss.c's bounded
 * osSetTimer(100ms)-based controller-detection loop -- genuinely real-time,
 * out of scope by design), NOTHING is consuming its forwards, so unbounded
 * per-ask generation free-spins for that whole real-time window and however
 * many ticks accumulate is real-scheduling-dependent (confirmed: 2nd
 * attempt's 33/38/14% divergence). waitForNextFrame() (frametiming.c) is a
 * tight busy-spin on osGetCount(), not a queue wait -- so FREEZING the
 * clock during that window (0 extra ticks) hangs it forever, and letting it
 * free-run is exactly the bug. The fix: cap the clock to exactly ONE extra
 * quantum beyond the seed before the first task runs -- fixed, identical
 * every run, and >=1 quantum is already enough to clear
 * waitForNextFrame()'s first-call threshold (see the arithmetic in
 * frametiming.c), so it can't hang either. Set true by osSpTaskStartGo. */
static int g_determTaskEverRun = 0;
static int g_determPreTaskTicksGranted = 0;
/* GE_DETERM_TRACE=1 (with GE_DETERM=1): log every virtual-clock advance with a
 * monotonic sequence number, for diffing two runs' logs to find the exact
 * first point they diverge -- see docs/dev/findings.md §D117 M-52. Test-only,
 * both env-gated off by default; zero cost when unset. */
static int g_determTraceEnabled = 0;
static u32 g_determTraceSeq = 0;

u32 osGetCount(void)
{
    if (g_determEnabled) {
        return g_determTicks;
    }
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
/* D578: scheduled time of the latest posted retrace (the sim timeline the
 * render worker blends against; regular, unlike the posting time). */
static uint64_t g_lastViUs = 0;
static uint32_t g_tickIntervalUs = 1000000 / 60; /* NTSC frame rate */

/* D600: display-locked retrace. The pacemaker below is a free-running wall
 * clock (16666 us: 1e6/60 truncated, ~40 ppm fast) while the swap blocks on
 * the display's vblank, so with VSync on the tick's phase slides against the
 * vblank (~7 min beat on a 60.000 Hz panel). Once the tick lands inside the
 * render+swap window every frame is late and boss.c's tick gate skips
 * (22-33 ms frames) until the phase slides back out; every launch starts at a
 * random phase, so a restart "re-rolls" it. Fix: a first-order phase lock.
 * After each swap return (= a vblank) the render side nudges the NEXT tick so
 * it fires D600_TARGET_US after the vblank, leaving the rest of the period
 * for game logic + render. Only engaged when VSync is on and consecutive swap
 * returns are one game frame apart (a 60 Hz-class display); otherwise the
 * pacemaker free-runs exactly as before. GE_NOTICKLOCK=1 disables it (A/B). */
#define D600_TARGET_US    1500
#define D600_MAX_NUDGE_US 100
static int64_t  g_tickNudgeUs = 0;        /* written by the render side, consumed by the pacemaker */
static uint64_t g_vbLastUs = 0;
static int      g_tickLockMode = 0;       /* 0 undecided, 1 on, -1 off (env / runaway guard) */
static int      g_tickLockSame = 0;       /* consecutive saturated nudges, runaway guard */
static int      g_tickLockLogN = 0;

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
    /* A real hang is a permanent stall; transient >3 s gaps are normal on a
     * slow/software-GL/debugger setup (level loads, gdb pauses, SIGTERM
     * teardown) and were spamming ge007.crash.log with dozens of thread
     * dumps. Use a longer window and stop after a few reports. */
    static int fired = 0;
    uint64_t now = sysGetMicroseconds();
    if (fired < 3 && now - g_lastFrameUs > 8000000 && now - g_lastHeartbeatUs > 5000000) {
        fired++;
        g_lastHeartbeatUs = now;
        sysLogPrintf(LOG_ERROR,
            "kernel heartbeat: no frame rendered for %llu ms (frames=%d); state:",
            (unsigned long long)(now - g_lastFrameUs) / 1000, g_framesRendered);
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
        /* GE_DETERM (§D117, M-52 2nd attempt): retrace generation lives in
         * osRecvMesg() now (see there) -- synchronous with the sole
         * consumer asking for the next message, not paced by this thread.
         * The M-52 FIRST attempt polled g_viRetraceMQ's empty/non-empty
         * state from here on a real-time cadence; that poll's own
         * OS-scheduling latency varied run-to-run and leaked directly into
         * the tick count (measured: 90%/28%/16% frame divergence between
         * two runs, worse than the port's baseline wall-clock
         * nondeterminism this mode exists to remove). This thread still
         * only does what osGetTime()-based real-time bookkeeping needs
         * (timers, hang-heartbeat) -- deliberately NOT touching
         * g_viRetraceMQ at all in this mode. */
        if (g_determEnabled) {
            portServiceTimers();
            portHeartbeatCheck();
            sysSleep(g_tickIntervalUs);
            continue;
        }

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
            __atomic_store_n(&g_lastViUs, g_nextTickUs, __ATOMIC_RELEASE);   /* D578 */
            g_nextTickUs += (int64_t)g_tickIntervalUs + __atomic_exchange_n(&g_tickNudgeUs, 0, __ATOMIC_RELAXED);   /* D600 */
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

    if (osTvType == OS_TV_PAL) {    /* D488: MPAL (PAL-M) is a 60 Hz system */
        g_tickIntervalUs = 1000000 / 50; /* PAL: 50 frames/s -> 20ms */
        g_determQuantum = 931050;
    } else {
        g_tickIntervalUs = 1000000 / 60; /* NTSC: 60 frames/s -> ~16.7ms */
        g_determQuantum = 775875;
    }

    /* GE_DETERM=1: seed so the first waitForNextFrame() computes exactly
     * (quantum + 387937) / quantum == 1 tick, per the §D117 design. */
    g_determEnabled = getenv("GE_DETERM") != NULL;
    if (g_determEnabled) {
        g_determTicks = g_determQuantum;
        g_determTraceEnabled = getenv("GE_DETERM_TRACE") != NULL;
        sysLogPrintf(LOG_NOTE,
            "GE_DETERM=1: fixed-tick deterministic mode (test-only, "
            "quantum=%u) -- osGetCount() is now frame-locked, not wall-clock",
            g_determQuantum);
        if (g_determTraceEnabled) {
            sysLogPrintf(LOG_NOTE, "DETERMTRACE seq=0 ticks=%u event=seed", g_determTicks);
        }
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

#if !defined(_WIN32)
#include <sys/mman.h>
/* The decomp aligns/compares stack-buffer pointers by truncating to u32
 * (e.g. `(u32)compbuffer` in image.c texLoad) — an N64 assumption. Windows
 * pthread stacks are already low, so every such idiom works there.
 *
 * On macOS the whole N64 window is reserved PROT_NONE by portAddrInit, so each
 * game-thread stack is carved from the window's stack region
 * [PORT_ADDR_BASE + 0xA0000000, + 0xC0000000), leaving a PROT_NONE guard page
 * below it; the stack's host address then truncates to its N64 address exactly
 * as on Windows.
 *
 * On Linux (PORT_ADDR_BASE == 0) the window is not reserved, so keep the
 * original MAP_32BIT allocation: addresses below 2 GiB truncate identically. */
#define PORT_STACK_REGION_OFF  0xA0000000ULL
#define PORT_STACK_REGION_SIZE (512ULL << 20)
#define PORT_STACK_GUARD       0x4000ULL

static uintptr_t s_stackNext = 0;

static void *portAllocLowStack(size_t sz)
{
#if PORT_ADDR_BASE != 0
    const uintptr_t region = (uintptr_t)PORT_ADDR_BASE + PORT_STACK_REGION_OFF;
    if (s_stackNext == 0)
        s_stackNext = region;
    const uintptr_t base = s_stackNext + PORT_STACK_GUARD;
    const uintptr_t end = base + sz;
    if (end > region + PORT_STACK_REGION_SIZE)
        return NULL;
    void *p = mmap((void *)base, sz, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p != (void *)base)
        return NULL;
    s_stackNext = end;
    return p;
#elif defined(MAP_32BIT)
    void *p = mmap(NULL, sz, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT
#if defined(MAP_STACK)
                   | MAP_STACK
#endif
                   , -1, 0);
    if (p != MAP_FAILED)
        return p;
    return NULL;
#else
    (void)sz;
    return NULL;
#endif
}
#endif

void osStartThread(OSThread *t)
{
    PortThread *pt = portFind(t);
    if (!pt || pt->started) return;
    pt->started = 1;
    t->state = OS_STATE_RUNNABLE;

    pthread_attr_t attr;
    int rc;
    pthread_attr_init(&attr);
#if !defined(_WIN32)
    void *lowStack = portAllocLowStack(PORT_THREAD_STACK);
    if (lowStack)
        pthread_attr_setstack(&attr, lowStack, PORT_THREAD_STACK);
    else
        pthread_attr_setstacksize(&attr, PORT_THREAD_STACK);
#else
    pthread_attr_setstacksize(&attr, PORT_THREAD_STACK);
#endif
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

extern void interpPortalOnSend(void *mq, void *msg);   /* D578: port/src/interpportal.c */

s32 osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flag)
{
    interpPortalOnSend(mq, msg);   /* D578: game-thread snapshot for the portal-scissor replay */
    PortQueue *pq = portQueueGet(mq);
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

extern OSMesgQueue gfxFrameMsgQ; /* src/init.c */

s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flag)
{
    PortQueue *pq = portQueueGet(mq);
    pthread_mutex_lock(&pq->lock);
    while (mq->validCount == 0) {
        if (flag != OS_MESG_BLOCK) {
            pthread_mutex_unlock(&pq->lock);
            return -1;
        }
        /* GE_DETERM (§D117, M-52 2nd attempt): __scMain (sched.c) is the
         * SOLE consumer of g_viRetraceMQ, and everything the rest of the
         * boot/frame chain waits on (task execution via osSpTaskStartGo,
         * joyPoll() -- which is also what unblocks joyCheckStatusThreadSafe
         * -- and the forward to mainThread's gfxFrameMsgQ) happens
         * synchronously inside __scMain's handling of THAT message, on this
         * same thread, before it comes back to ask for the next one. So
         * "the consumer is asking and none exists" is itself the correct,
         * purely call-sequenced trigger for the next virtual retrace --
         * driven by program order, not by an independent thread polling on
         * a wall-clock cadence (the M-52 first attempt's mistake: that
         * poll's timing varied run-to-run and leaked into the tick count).
         * Enqueue inline (not via osEnqueueMesg -- would re-lock pq->lock,
         * which we already hold). */
        if (g_determEnabled && mq == g_viRetraceMQ) {
            /* Still synthesize the message every time (keeps __scMain/
             * joyPoll unblocked -- no deadlock), but only advance the clock
             * up to a fixed cap before the first real task runs. See the
             * g_determTaskEverRun comment above. */
            if (g_determTaskEverRun || g_determPreTaskTicksGranted < 1) {
                g_determTicks += g_determQuantum;
                if (!g_determTaskEverRun) ++g_determPreTaskTicksGranted;
                if (g_determTraceEnabled) {
                    sysLogPrintf(LOG_NOTE,
                        "DETERMTRACE seq=%u ticks=%u event=advance taskEverRun=%d",
                        ++g_determTraceSeq, g_determTicks, g_determTaskEverRun);
                }
            } else if (g_determTraceEnabled) {
                sysLogPrintf(LOG_NOTE,
                    "DETERMTRACE seq=%u ticks=%u event=capped taskEverRun=%d",
                    ++g_determTraceSeq, g_determTicks, g_determTaskEverRun);
            }
            mq->msg[(mq->first + mq->validCount) % mq->msgCount] = g_viRetraceMsg;
            ++mq->validCount;
            break;
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
    pthread_cond_signal(&pq->cond);
    pthread_mutex_unlock(&pq->lock);
    /* gfxFrameMsgQ is consumed only by boss.c's game thread. Apply queued
     * F10 watch edits there, after releasing the OS queue lock; the SDL
     * input/scheduler and render threads never touch GE watch/save state. */
    if (mq == &gfxFrameMsgQ) watchSettingsGameTick();
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

static void portPostVIEvent(void)
{
    if (g_viRetraceMQ) {
        /* D134: keep two slots free for the SP/DP done events posted at the
         * end of a synchronous fast3d frame. A retrace posted into a backlog
         * is stale anyway -- sched has not drained the previous one yet. */
        if (g_viRetraceMQ->validCount >= g_viRetraceMQ->msgCount - 2) return;
        osSendMesg(g_viRetraceMQ, g_viRetraceMsg, OS_MESG_NOBLOCK);
    }
}

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
#ifdef VERSION_EU
    /* D487: the EU build's video_related_8 (src/fr.c, #ifdef VERSION_EU)
     * encodes yScale = bufy * 0x800 / (0x220 + (bufy == 330 ? 28 : 0)),
     * i.e. against 544 (572) lines, not 480. Inverting with 480 gave a CFB
     * ~12% too short (269 -> 237), so fast3d cropped the bottom of every
     * PAL frame (HUD ammo counter reduced to a dot). Recover bufy exactly:
     * the smallest height whose EU encoding reproduces this yScale. Keyed on
     * the build, not the OS_VI_BIT_PAL ctrl bit: the modes reaching here
     * (the scheduler's LAN1 start mode and the EU video_related_8 modes) do
     * not reliably carry that bit (observed: yScale 0x400 = 272 without it). */
    {
        u32 ys = vm->fldRegs[0].yScale & 0xFFF;
        s32 cand;
        for (cand = 1; cand <= 576; cand++) {
            u32 den = 0x220u + (cand == 330 ? 28u : 0u);
            if (((u32)cand * 0x800u) / den == ys) { h = cand; break; }
        }
        if (cand > 576) h = 0;
    }
    if (h <= 0 || h > 576) h = 272; /* fallback: the EU CFB height (fr.h SCREEN_HEIGHT_272) */
#else
    if (h <= 0 || h > 480) h = pal ? 272 : 240; /* fallback: LAN1 CFB height */
#endif
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
    audioSetNextBuffer((const s16 *)buf, size);
    return 1;
}
u32 osAiGetLength(void)
{
    /* D204/F1: AI_LEN_REG is the bytes remaining in the buffer the DAC is
     * CURRENTLY playing -- not the whole queue. Reporting the full SDL queue
     * depth here pinned src/audi.c:531's frame-size regulator at its lower
     * clamp and let its u32 subtraction wrap into a 74x heap overrun. See the
     * long comment on audioGetAiLengthBytes() in port/src/audio.c. */
    return audioGetAiLengthBytes();
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
/* A ROM-read DMA target must land in the game DRAM views
 * (V1/V2) or the current thread's stack (texLoad's compbuffer). Anything
 * else is unmapped host memory on PC. */
static int dramHostAddrValid(uintptr_t addr, u32 size)
{
    static const uintptr_t bases[2] = { PORT_DRAM_V1_BASE, PORT_DRAM_K0_BASE };   /* portaddr.h */
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
        if (!romdataCartAddrValid(srcPA, size)) {
            sysLogPrintf(LOG_WARNING,
                         "osPiStartDma: ROM read out of range "
                         "(src=0x%08X size=0x%X); skipped",
                         srcPA, size);
            return;
        }
        /* Validate the DMA target too. The N64 PI happily DMAs to any KSEG
         * address; on PC an unmapped target is a wild memcpy. */
        if (!dramHostAddrValid((uintptr_t)dstVA, size)) {
            /* No __builtin_return_address here: the caller's frame chain is
             * not always walkable (it faulted). Log the raw stack window
             * instead — return addresses are in it and symbolicate offline.
             * The FATAL below re-dumps RSP/registers via the crash handler. */
            const uint64_t *sp = (const uint64_t *)__builtin_frame_address(0);
            char win[1200] = "";
            char *wp = win;
            for (int i = 0; i < 32; i++) {
                /* D478: the bound is the bytes left, not a pointer. */
                wp += snprintf(wp, sizeof(win) - (size_t)(wp - win),
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
        memcpy(dstVA, portN64ToHost(srcPA), size);
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
        /* D401: present a Rumble Pak (CONT_CARD_ON) whenever the connected
         * pad can actually rumble. joyRumblePakInit (src/joy.c:184) gates
         * the whole rumble-pak path on this bit. */
        g_contStatus[i].status = (connected && inputRumbleSupported(i)) ? CONT_CARD_ON : 0;
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
    /* D514: before the game's first EEPROM read, convert an emulator-format
     * save in place (port/src/eepimport.c). Runs before fopen so the load
     * below sees the converted bytes; no-op for a port-format save. */
    geEepImportEmulatorSave(path);
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

extern int geLegacyCrcMaybeMigrateSlots(u8 *slots5); /* port/src/legacycrc.c (D297) */
extern int geEepImportEmulatorSave(const char *path); /* port/src/eepimport.c (D514) */

/* D442: Game.AllUnlocked is a pure RAM/query-time override (file2.c
 * fileGetIsCheatUnlocked + the debug flags seeded in main.c). The EEPROM
 * bytes the game reads and writes are always the raw disk bytes -- the old
 * read-time save patch (D257/D259/D281) and its write-time merge (D387) are
 * gone. */

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
        /* D297: one-time migration of pre-D284 slot CRCs (port/src/legacycrc.c).
         * This IS the read fileValidateSaves performs (block 4, five slots), so
         * the re-stamped checksums are exactly what the game validates — no
         * ordering hazard. Only pristine+migrated bytes are ever persisted.
         * Block-0 smallSave is deliberately untouched (factory seal, D297). */
        if (block == 4 && nbytes == (int)(96 * 5) /* 5 x save_data, file.h */) {
            int migrated = geLegacyCrcMaybeMigrateSlots(buf);
            if (migrated) {
                memcpy(s_eeprom + off, buf, nbytes);
                geEepromStore();
                sysLogPrintf(LOG_INFO,
                             "eeprom: D297-migrated %d pre-D284 save slot(s) to current CRC",
                             migrated);
            }
        }
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

/* Memory Pak (PFS) + Rumble Pak (motor): no Memory Pak on the PC (saves are
 * file-backed EEPROM), but the Rumble Pak path is routed to real gamepad
 * haptics (D401): a pad that can rumble presents itself as a "card is on"
 * accessory, and the motor calls become SDL_GameControllerRumble via
 * inputRumble (port/src/input.c). */
s32 osPfsInit(OSMesgQueue *queue, OSPfs *pfs, int channel)
{
    (void)queue; (void)pfs;
    /* PFS_ERR_DEVICE ("wrong device type") = a Rumble Pak is present, not a
     * Memory Pak: exactly what joyRumblePakInit (src/joy.c:186-190) needs
     * to proceed to osMotorInit. PD's osPfsInitPak does the same. */
    return inputRumbleSupported(channel) ? PFS_ERR_DEVICE : PFS_ERR_NOPACK;
}
s32 osPfsIsPlug(OSMesgQueue *queue, u8 *pattern)
{
    (void)queue;
    if (pattern) {
        *pattern = 0;
        /* JPN: MAXCONTROLLERS can be 6 > MAX_PADS; inputRumbleSupported()
         * bounds-checks, so the extra channels just never light up. */
        for (int i = 0; i < MAXCONTROLLERS; ++i)
            if (inputRumbleSupported(i)) *pattern |= (u8)(1 << i);
    }
    return 0;
}
s32 osMotorInit(OSMesgQueue *mq, OSPfs *pfs, int channel)
{
    if (pfs && inputRumbleSupported(channel)) {
        pfs->queue = mq;
        pfs->channel = channel;
        pfs->activebank = 0xff;
        /* NOTE: PFS_MOTOR_INITIALIZED is not defined in GE's headers and
         * joy.c never reads pfs->status -- it only tests osMotorInit()==0
         * (src/joy.c:190) to mark the pad RUMBLEPAKINITSTATE_READY, so no
         * status store is needed (PD's osMotorProbe writes one). */
        return 0;
    }
    (void)mq; (void)channel;
    return PFS_ERR_NOPACK;
}
s32 osMotorStart(OSPfs *pfs)
{
    if (!pfs) return PFS_ERR_NOPACK;
    /* The N64 motor has no duration; the game's joyRumblePakTimer60 (src/joy.c)
     * arms a per-event countdown and turns the motor off via osMotorStop.
     * Issue one generous 5 s window, PD-style ("hope the timer stops it");
     * inputRumble no-ops internally for non-rumble pads. */
    inputRumble(pfs->channel, 1.0f, 5.0f);
    return 0;
}
s32 osMotorStop(OSPfs *pfs)
{
    if (!pfs) return PFS_ERR_NOPACK;
    inputRumble(pfs->channel, 0.0f, 0.0f);   /* zero strength/duration = stop */
    return 0;
}

/* ------------------------------------------------------------------------ */
/* SP (RSP) — runs the software RSP inline, then posts the done messages    */
/* that sched.c's __scMain waits for.                                       */
/* ------------------------------------------------------------------------ */

void osSpTaskLoad(OSTask *t) { (void)t; /* nothing to load on the host */ }

/* D600: called by the render side right after videoEndFrame() returns, i.e.
 * just after the blocking swap returned at a vblank. */
static void portNoteSwapReturn(void)
{
    if (g_tickLockMode == 0) {
        g_tickLockMode = (g_determEnabled || getenv("GE_NOTICKLOCK") != NULL) ? -1 : 1;
        if (g_tickLockMode < 0) sysLogPrintf(LOG_NOTE, "D600: display-locked tick disabled");
    }
    if (g_tickLockMode < 0) return;
    uint64_t t = sysGetMicroseconds();
    uint64_t last = g_vbLastUs;
    g_vbLastUs = t;
    if (!videoVSyncOn() || last == 0) return;
    const int64_t P = (int64_t)g_tickIntervalUs;
    const int64_t gap = (int64_t)(t - last);
    if (gap < P * 9 / 10 || gap > P * 11 / 10) return;   /* not one vblank per game frame: leave it free-running */
    int64_t next = (int64_t)__atomic_load_n(&g_nextTickUs, __ATOMIC_RELAXED);
    int64_t phase = (next - (int64_t)t) % P;
    if (phase < 0) phase += P;
    int64_t err = phase - D600_TARGET_US;               /* >0: tick is later than target */
    if (err >= P / 2) err -= P; else if (err < -P / 2) err += P;
    int64_t nudge = -err / 8;
    if (nudge > D600_MAX_NUDGE_US) nudge = D600_MAX_NUDGE_US;
    if (nudge < -D600_MAX_NUDGE_US) nudge = -D600_MAX_NUDGE_US;
    /* Runaway guard (a driver that ignores vsync makes the swap return track
     * the render, and the loop would chase it): a lock that stays saturated
     * far longer than a full-period slew needs is not converging. */
    if (nudge == D600_MAX_NUDGE_US || nudge == -D600_MAX_NUDGE_US) {
        if (++g_tickLockSame > 400) {
            g_tickLockMode = -1;
            sysLogPrintf(LOG_WARNING, "D600: display-locked tick not converging; disabled (swap may not block on vblank)");
            return;
        }
    } else {
        g_tickLockSame = 0;
    }
    __atomic_store_n(&g_tickNudgeUs, nudge, __ATOMIC_RELAXED);
    if ((++g_tickLockLogN % 300) == 1)
        sysLogPrintf(LOG_NOTE, "D600: tick lock phase=%lld us err=%lld us nudge=%lld us", (long long)phase, (long long)err, (long long)nudge);
}

static void portRenderGfxTask(Gfx *dl)
{
    uint64_t t0 = sysGetMicroseconds();
    videoStartFrame();
    videoSyncSplitScreen();
    gfx_run(dl);
    videoEndFrame();
    portNoteSwapReturn();   /* D600 */
    g_lastFrameUs = sysGetMicroseconds();
    if (++g_framesRendered <= 5 || (g_framesRendered % 300) == 0)
        sysLogPrintf(LOG_NOTE, "frame %d rendered in %llu us",
                     g_framesRendered,
                     (unsigned long long)(sysGetMicroseconds() - t0));
}

/* D481: render worker. The N64 RSP/RDP run beside the CPU; the scheduler
 * starts a task and learns it finished from the SP/DP interrupts. The port
 * used to run fast3d inline here, on the scheduler thread, so a VI retrace
 * arriving mid-render was forwarded to the game only after the render: the
 * game frame started late, the next retrace came < half a frame after it, and
 * boss.c's tick gate skipped it (a 33 ms frame). Invisible with 1-3 ms
 * renders, ~1 frame in 4 on an Intel HD 3000 (8-12 ms renders). Gfx tasks now
 * go to this worker; it posts SP/DP done exactly as the inline path did.
 * One-slot mailbox: the game submits frame N+1 only after frame N's done event
 * has round-tripped (worker -> interruptQ -> __scTaskComplete -> client DONE
 * -> boss.c), so the slot is always empty at hand-off. GE_DETERM keeps the
 * inline path (its retrace synthesis is call-sequenced, see osRecvMesg), and
 * GE_RENDERINLINE=1 restores it as a falsifier. */
static pthread_mutex_t s_rwLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_rwCond = PTHREAD_COND_INITIALIZER;
static Gfx *s_rwTask = NULL;   /* submitted display list; NULL = worker idle */
static uint64_t s_rwTaskViUs;  /* D578: g_lastViUs when it was submitted */
/* D583: when the game submitted the task, and when the worker last posted
 * SP/DP done: split a late frame into the game's own work, time the game was
 * still waiting for our previous frame, and the worker's pickup delay. */
static int64_t s_rwTaskSubmitUs, s_rwLastDoneUs;
static int s_rwMode = 0;       /* 0 = undecided, 1 = worker, -1 = inline */

/* D578: frame interpolation pacing (worker thread only; fast3d does the
 * drawing, see gfx_pc.cpp). A game frame is stamped with the scheduled time
 * S of the retrace it was submitted after. A present at wall time t shows sim
 * time t - L, i.e. the blend alpha = (t - L - S_prev) / (S - S_prev): every
 * refresh between two game frames lands at its position on the sim timeline,
 * whatever the present rate (90 on a Deck OLED, 120, 144). L follows the
 * frame's last present, so that one shows the frame exactly (see below);
 * about half a tick of extra latency at 120. All passes for a game frame are drawn when it arrives, before
 * SP/DP done (the game reuses its buffers after that); the worker then swaps
 * the stored slots on its present clock, waking early for a new frame. */
#define IP_SLOTS 8   /* = GFX_INTERP_SLOTS (port/fast3d/gfx_rendering_api.h) */
static int64_t s_ipPrevS, s_ipCurS;
static double s_ipDelayEma;        /* submit time - S, us */
static double s_ipLag;             /* present time - sim time shown, us */
static double s_ipPassEma;         /* CPU cost of one pass, us */
static double s_ipDrawPk;          /* D583: decaying peak of a whole frame's draw time, us */
static double s_ipSwapEma;         /* D583: time a present takes (show + swap + finish), us */
static int s_ipWasOn;
/* D583: present slots are a ring. Slots [s_qHead, s_qHead + s_qLen) are
 * stored and waiting, in due order; a new game frame's passes go after them,
 * so a frame never drops the previous frame's unshown presents (that left
 * ~8% of 120 Hz ticks with one present and a 16 ms hole: VRR flicker). */
static int64_t s_ipTimes[IP_SLOTS];   /* due time per ring slot */
static uint8_t s_ipIdx[IP_SLOTS];     /* position within its game frame */
static uint8_t s_ipWhy[IP_SLOTS];      /* D583 hold causes: 0 blended, 1 exact fallback, 2 alpha clamped to 0, 3 clamped to 1 (late/stale) */
static double s_ipSim[IP_SLOTS];      /* D583: sim time each slot shows (prevS + alpha * dS), us */
static int s_qHead, s_qLen;
/* D583: one continuous present grid. s_gNext = the next grid point no frame
 * has taken yet; each game frame takes the next round(dS / period) points
 * (2 per tick at 120 Hz), so presents are evenly spaced whatever the game
 * frame's arrival jitter. Re-anchored only when a frame is ready after its
 * first point (a stall) or the period changes. Rebuilding it every tick from
 * the last swap return (under VRR, our own present time) wobbled it. */
static double s_gNext, s_gPeriod, s_gAcc;
static double s_gMinSlack;         /* least spare time (grid point - ready) this window */
static int s_gWin;                 /* frames in the window */
/* Swap-driven pacing (VSync on): a blocking swap returns at a vblank, so the
 * swap-return times ARE the display clock. s_vbLast = last return, s_vbP =
 * measured refresh period (EMA of gaps near one period). */
static int64_t s_vbLast;
static double s_vbP;
static unsigned s_vbOut;           /* consecutive off-period gaps */
static double s_vbOutGap[8];

/* 10 s stats window (worker thread only). */
static int64_t s_stStart;
static unsigned s_stFrames, s_stExactFrames, s_stPresents;
/* D578 VRR cadence: present-gap histogram (<7, 7-9.5, 9.5-12, 12-20, >20 ms),
 * slots dropped unshown, ticks by slot count (1, 2, 3, 4+). */
static unsigned s_stGap[5], s_stDrop, s_stSlotsN[4], s_stAnchor, s_stPull;
/* D583 motion: sim-time step between consecutive presents, in grid periods:
 * hold (< 0.25), short (< 0.75), ok (< 1.25), long (< 1.75), jump. Even
 * present timing can still show uneven motion (a frame forced exact shows the
 * same image twice: a hold, then a jump). */
static unsigned s_stMot[5];
static unsigned s_stHoldWhy[4];   /* D583: holds by the cause of the second present (s_ipWhy) */
/* D583: present cost (show + swap + finish): longest, and how many took over
 * half a refresh (a swap that blocks holds the worker: the next game frame
 * starts its draw late). */
static double s_stSwapMax;
static unsigned s_stSwapLong;
/* D583: game-side split (see s_rwTaskSubmitUs): game work after its retrace /
 * our previous done, time it was blocked by our previous frame, our pickup. */
static double s_stGameSum, s_stGameMax, s_stBlockSum, s_stBlockMax, s_stPickSum, s_stPickMax;
static unsigned s_stGameLong, s_stBlocked;
static double s_stLastSim;
static int64_t s_stLastPres;

static int portInterpSwapDriven(void)
{
    /* D578: the LIVE swap interval decides, not the setting: a driver that
     * forces VSync off (swap interval 0) gives non-blocking swaps, and
     * swap-driven pacing then measures swap cost as the display period. */
    static int64_t s_ivAt;
    static int s_iv = 1;
    const int64_t now = (int64_t)sysGetMicroseconds();
    if (now - s_ivAt > 1000000) {   /* re-read ~1/s (render worker thread) */
        int rf = 0;
        videoInterpInfo(&rf, &s_iv);
        s_ivAt = now;
    }
    return videoInterpVSync() && videoInterpHz() > 0 && s_iv != 0;
}

static int portInterpLog(void)
{
    static int v = -1;   /* GE_INTERPLOG: pacing trace (first ~2000 lines) */
    if (v < 0) { const char *e = getenv("GE_INTERPLOG"); v = e ? (atoi(e) > 1 ? atoi(e) : 2000) : 0; }   /* =<n>: line cap */
    return v > 0 ? v-- : 0;
}

static int portInterpAlpha1(void)
{
    static int v = -1;   /* GE_INTERP_ALPHA1: every pass exact (identity gate) */
    if (v < 0) v = getenv("GE_INTERP_ALPHA1") != NULL;
    return v;
}

static void portRenderGfxTaskInterp(Gfx *dl, uint64_t viUs, int hz)
{
    const int64_t tick = (int64_t)g_tickIntervalUs;
    const int64_t P = 1000000 / hz;
    const int64_t A = (int64_t)sysGetMicroseconds();
    const int64_t S = (int64_t)viUs;
    float alphas[IP_SLOTS];
    int n = 0;
    /* D583: game-side split of this frame's submit delay (A - S). */
    const int64_t subU = s_rwTaskSubmitUs, doneU = s_rwLastDoneUs;
    const int64_t gStart = (doneU > S) ? doneU : S;
    const int64_t gameUs = subU > gStart ? subU - gStart : 0;
    const int64_t blockUs = doneU > S ? doneU - S : 0;
    const int64_t pickUs = A > subU ? A - subU : 0;
    s_stGameSum += (double)gameUs; if ((double)gameUs > s_stGameMax) s_stGameMax = (double)gameUs;
    s_stBlockSum += (double)blockUs; if ((double)blockUs > s_stBlockMax) s_stBlockMax = (double)blockUs;
    s_stPickSum += (double)pickUs; if ((double)pickUs > s_stPickMax) s_stPickMax = (double)pickUs;
    if (gameUs > (tick * 3) / 4) s_stGameLong++;
    if (blockUs > 0) s_stBlocked++;

    const int stale = !s_ipWasOn || s_ipCurS == 0 || S <= s_ipCurS || S - s_ipCurS > 4 * tick;
    if (stale) {
        gfx_interp_reset();
        s_ipPrevS = S - tick;
        s_ipDelayEma = (double)(A - S);
    } else {
        s_ipPrevS = s_ipCurS;
        s_ipDelayEma = 0.9 * s_ipDelayEma + 0.1 * (double)(A - S);
    }
    s_ipCurS = S;
    s_ipWasOn = 1;

    /* D583: slot times come from one continuous present grid (s_gNext), so
     * every present is one display period after the last whatever the game
     * frame's arrival jitter. Grid period: the display's reported refresh
     * with VSync (not the measured swap gaps: under G-Sync/VRR a swap returns
     * when we present, so the "period" is our own cadence fed back), else the
     * target rate. */
    const int swapDriven = portInterpSwapDriven();
    int maxN = IP_SLOTS / 2;   /* the ring holds this frame + one still waiting */
    /* With the F10 overlay open, one pass per game frame: it is a menu, and
     * replaying its text in a second pass measured a few px of difference
     * between passes in a level (shimmer). The timeline continues, so
     * nothing jumps when it closes. */
    {
        extern int optionsOverlayIsOpen(void);
        if (optionsOverlayIsOpen()) maxN = 1;
    }
    if (s_ipPassEma > 0 && s_ipPassEma * maxN > 0.8 * (double)tick) {
        maxN = (int)(0.8 * (double)tick / s_ipPassEma);   /* slow GPU/CPU: fewer passes */
        if (maxN < 1) maxN = 1;
    }
    double Pg = (double)P;
    if (swapDriven) {
        int rfN = 0, ivN = 0;
        videoInterpInfo(&rfN, &ivN);
        Pg = (rfN > 0) ? 1e6 / (double)rfN : (s_vbP > 0.0) ? s_vbP : (double)P;
    }
    {
        /* Within 1% of tick/k: use exactly tick/k. A 60 Hz game on a 120 Hz
         * grid is then exactly 2 presents per tick, with no beat between the
         * two clocks (the reported refresh is an integer, 119 for 119.88). */
        const double k = floor((double)tick / Pg + 0.5);
        if (k >= 1.0 && fabs((double)tick / k - Pg) < 0.01 * Pg) Pg = (double)tick / k;
    }
    /* Grid points this frame spans: dS / period, the fraction carried
     * (90 Hz: 1.5 -> 2, 1, 2, 1, ...). */
    if (stale) s_gAcc = 0.0;
    s_gAcc += (double)(S - s_ipPrevS) / Pg;
    int want = (int)floor(s_gAcc + 0.5);
    if (want < 1) want = 1;
    s_gAcc -= (double)want;
    if (s_gAcc < -1.0 || s_gAcc > 1.0) s_gAcc = 0.0;
    if (want > IP_SLOTS / 2) want = IP_SLOTS / 2;   /* a long hitch; the grid re-anchors below */
    n = want < maxN ? want : maxN;
    /* D583: a frame that arrives a tick late (a stall on the game side) gets
     * more grid points (3 at 90 Hz), and with a restart that was 6 passes and a
     * 16 ms draw on top of the stall. Two passes keep it short. */
    /* D583: a frame that arrives a tick late (a stall on the game side) gets
     * more grid points (3 at 90 Hz), and with a restart that was 6 passes and a
     * 16 ms draw on top of the stall. Two passes keep it short. */
    if ((S - s_ipPrevS) > tick + tick / 2 && n > 2) n = 2;
    /* Nothing can be shown before this frame's passes are drawn. D583: a
     * frame's real draw time, not passes x pass cost: a turn-softened or
     * exact restart redraws its passes (about twice the estimate), and the
     * first present then went out late, then the next on time (an uneven
     * pair of gaps). The peak decays, so a past spike stops costing latency. */
    double drawEst = (double)n * s_ipPassEma;
    if (s_ipDrawPk > drawEst) drawEst = s_ipDrawPk;
    const int64_t ready = A + (int64_t)drawEst + 500;
    {
        const double margin = 1000.0;   /* arrival jitter the grid absorbs without a re-anchor */
        const double slack = s_gNext - (double)ready;
        if (stale) {
            /* The waiting presents belong to a timeline that is gone. */
            s_stDrop += (unsigned)s_qLen;
            s_qLen = 0;
        }
        if (stale || s_gNext == 0.0 || fabs(Pg - s_gPeriod) > 0.002 * Pg || slack < 0.0) {
            /* Ready after the next grid point (a stall, a late frame) or a new
             * period: start the grid at ready. One uneven gap, then even again. */
            if (!stale && s_gNext != 0.0) s_stAnchor++;
            s_gNext = (double)ready + margin;
            if (s_qLen > 0) {
                /* Never ahead of a present still waiting (a period change). */
                const double after = (double)s_ipTimes[(s_qHead + s_qLen - 1) % IP_SLOTS] + 0.75 * Pg;
                if (s_gNext < after) s_gNext = after;
            }
            s_gMinSlack = 1e18;
            s_gWin = 0;
        } else {
            /* Latency trim: a late frame re-anchors the grid later, nothing moves
             * it back. Once a second, if every frame had spare time, move the
             * grid earlier by part of it (at most a quarter period: the gap to
             * the waiting presents stays >= 0.75 period). */
            if (slack < s_gMinSlack) s_gMinSlack = slack;
            /* Ten frames when over a period is spare (a load, a stall that
             * left a deep queue), else a second. */
            if (++s_gWin >= (s_gMinSlack > margin + Pg ? 10 : 60)) {
                if (s_gMinSlack > margin + Pg) {
                    /* Over a period spare (after a load or a stall): take it
                     * all back at once and drop the waiting presents it
                     * overtakes (older images; the timeline stays in order). */
                    s_gNext -= s_gMinSlack - margin;
                    s_ipLag -= s_gMinSlack - margin;   /* the lag follows (it only falls slowly) */
                    while (s_qLen > 0 &&
                           (double)s_ipTimes[(s_qHead + s_qLen - 1) % IP_SLOTS] > s_gNext - 0.75 * Pg) {
                        s_qLen--;
                        s_stDrop++;
                    }
                    s_stPull++;
                } else if (s_gMinSlack > margin + 500.0) {
                    double d = s_gMinSlack - margin;
                    if (d > 0.25 * Pg) d = 0.25 * Pg;
                    s_gNext -= d;
                    s_ipLag -= d;
                    s_stPull++;
                }
                s_gMinSlack = 1e18;
                s_gWin = 0;
            }
        }
        s_gPeriod = Pg;
    }
    int64_t times[IP_SLOTS];
    for (int j = 0; j < n; j++) {
        /* n < want (slow GPU, overlay): spread over the frame's grid points,
         * the last pass on the last point (it shows the frame exactly). */
        const int idx = want - 1 - ((n - 1 - j) * want) / n;
        times[j] = (int64_t)(s_gNext + (double)idx * Pg);
    }
    s_gNext += (double)want * Pg;
    /* The lag (present time minus the sim time it shows) is set so this
     * frame's LAST present shows the frame exactly (alpha 1) and earlier ones
     * fall between it and the previous frame. Only matrices are blended;
     * the sky, effects and anything not matched are always this frame's, so
     * ending each frame on alpha 1 keeps them in step (a fixed one-tick lag
     * measured alpha ~0 / 0.5 and the world trailed the rest: ghosting in
     * the maintainer's 120 Hz test). Rises at once, falls slowly, so a frame
     * with one slot fewer (90 Hz) does not swing it back and forth. */
    {
        const double wantLag = (double)(times[n - 1] - S);
        if (stale || wantLag > s_ipLag) s_ipLag = wantLag;
        else s_ipLag += 0.05 * (wantLag - s_ipLag);
    }
    uint8_t why[IP_SLOTS];
    for (int i = 0; i < n; i++) {
        double a = ((double)times[i] - s_ipLag - (double)s_ipPrevS) / (double)(S - s_ipPrevS);
        why[i] = 0;
        if (a < 0.0) { a = 0.0; why[i] = 2; }
        if (a > 1.0 + 1e-6 || stale) why[i] = 3;
        if (a > 1.0 || stale || portInterpAlpha1()) a = 1.0;
        alphas[i] = (float)a;
    }

    if (portInterpLog()) {
        char b[160]; int o = 0;
        for (int i = 0; i < n && o < (int)sizeof(b) - 24; i++)
            o += snprintf(b + o, sizeof(b) - o, " %.3f@%+lld", alphas[i], (long long)(times[i] - A));
        sysLogPrintf(LOG_NOTE, "IPLOG tick S=%lld dS=%lld delay=%lld n=%d:%s", (long long)S,
                     (long long)(S - s_ipPrevS), (long long)(A - S), n, b);
    }
    videoStartFrame();
    videoSyncSplitScreen();
    const uint64_t t0 = sysGetMicroseconds();
    /* D583: this frame's passes go after the presents still waiting. */
    while (s_qLen > IP_SLOTS - n) {
        s_qHead = (s_qHead + 1) % IP_SLOTS;
        s_qLen--;
        s_stDrop++;
    }
    const int base = (s_qHead + s_qLen) % IP_SLOTS;
    const int filled = gfx_interp_tick(dl, alphas, n, base);
    for (int j = 0; j < filled; j++) {
        const int slot = (base + j) % IP_SLOTS;
        s_ipTimes[slot] = times[j];
        s_ipIdx[slot] = (uint8_t)j;
        /* An exact fallback inside gfx_interp_tick drew every pass at alpha 1. */
        const double aj = gfx_interp_last_exact() ? 1.0 : (double)alphas[j];
        s_ipSim[slot] = (double)s_ipPrevS + aj * (double)(S - s_ipPrevS);
        s_ipWhy[slot] = gfx_interp_last_exact() ? 1 : why[j];
    }
    s_qLen += filled;
    unsigned tkB = 0, tkT = 0, tkX = 0, tkM = 0, tkD = 0, tkU = 0;
    gfx_tick_counts(&tkB, &tkT, &tkX, &tkM, &tkD, &tkU);
    if (filled > 0) {
        /* Capped: a shader prewarm or level-load frame (50-100 ms) is not a
         * draw time, and learning it held ~40 ms of latency for seconds. */
        double drew = (double)(sysGetMicroseconds() - t0);
        if ((S - s_ipPrevS) > tick + tick / 2 || (A - S) > (tick * 5) / 6 || drew > 0.5 * (double)tick) {
            /* D583: a slow game frame (spawn-in drops): late from the game
             * (dS > 1 tick, a large submit delay) or slow to draw here. */
            static int s_slowLog = 0;
            if (s_slowLog++ < 300)
                sysLogPrintf(LOG_NOTE, "D583 SLOW frame %d: dS %lld us, submit delay %lld us (game %lld, blocked by us %lld, pickup %lld), draw %.0f us (%d passes, %u batches, %u tris, %u texture lookups: %u misses, %u dyn-hash bytes, %u us in import)",
                             g_framesRendered + 1, (long long)(S - s_ipPrevS), (long long)(A - S), (long long)gameUs,
                             (long long)blockUs, (long long)pickUs, drew, gfx_interp_body_runs(), tkB, tkT, tkX, tkM, tkD, tkU);
        }
        if (drew > 0.4 * (double)tick) drew = 0.4 * (double)tick;
        s_ipDrawPk = drew > s_ipDrawPk ? drew : s_ipDrawPk - 0.05 * (s_ipDrawPk - drew);
        /* D578 (c): divide by the passes actually drawn (an exact restart
         * discards earlier ones) and reject single slow outliers. */
        int runs = gfx_interp_body_runs();
        if (runs < filled) runs = filled;
        double per = (double)(sysGetMicroseconds() - t0) / runs;
        if (s_ipPassEma > 0 && per > 1.5 * s_ipPassEma) per = 1.5 * s_ipPassEma;
        s_ipPassEma = s_ipPassEma > 0 ? 0.9 * s_ipPassEma + 0.1 * per : per;
    }
    videoEndFrame();
    g_lastFrameUs = sysGetMicroseconds();
    if (++g_framesRendered <= 5 || (g_framesRendered % 300) == 0)
        sysLogPrintf(LOG_NOTE, "frame %d rendered (%d interp pass(es), %.0f us each, %d Hz presents)",
                     g_framesRendered, filled, s_ipPassEma, hz);

    /* D578: always-on pacing stats, once per 10 s while interpolating. */
    {
        int allOne = 1;
        for (int i = 0; i < n; i++) if (alphas[i] < 1.0f) allOne = 0;
        s_stFrames++;
        s_stSlotsN[n >= 4 ? 3 : n >= 1 ? n - 1 : 0]++;
        if (allOne || gfx_interp_last_exact()) s_stExactFrames++;
        const int64_t now = (int64_t)sysGetMicroseconds();
        int refresh = 0, swapIv = 0;
        if (s_stStart == 0) {
            s_stStart = now;
            videoInterpInfo(&refresh, &swapIv);
            sysLogPrintf(LOG_NOTE, "D578 interp start: target %d Hz, refresh %d Hz, swap interval %d, %s pacing",
                         hz, refresh, swapIv, swapDriven ? "swap-driven" : "timer");
        } else if (now - s_stStart >= 10 * 1000000) {
            const double secs = (double)(now - s_stStart) / 1e6;
            videoInterpInfo(&refresh, &swapIv);
            unsigned trTurn = 0, trClamp = 0, trSet = 0, trRoom = 0;
            gfx_interp_trigger_counts(&trTurn, &trClamp, &trSet, &trRoom);
            sysLogPrintf(LOG_NOTE,
                         "D578 interp triggers: %u turn-exact, %u turn-softened, %u set-change-exact, "
                         "%u room-change-exact",
                         trTurn, trClamp, trSet, trRoom);
            {
                extern void interpPortalCounts(unsigned out[8]);
                extern unsigned interpPortalExtraCount(void);
                unsigned pc[8];
                interpPortalCounts(pc);
                sysLogPrintf(LOG_NOTE,
                             "D578 portal replay: replaced %u, replays %u, fullview %u, gatefail %u, noroom %u, "
                             "unreached %u, nocam %u, nosnap %u, undrawn-room replays %u",
                             pc[0], pc[1], pc[2], pc[3], pc[4], pc[5], pc[6], pc[7], interpPortalExtraCount());
            }
            sysLogPrintf(LOG_NOTE,
                         "D578 interp: %.1f presents/s, target %d Hz, refresh %d Hz, swap interval %d, "
                         "%s, vblank period %.0f us, %u game frames, %.0f%% exact",
                         (double)s_stPresents / secs, hz, refresh, swapIv,
                         swapDriven ? "swap-driven" : "timer", s_vbP, s_stFrames,
                         s_stFrames ? 100.0 * (double)s_stExactFrames / (double)s_stFrames : 0.0);
            sysLogPrintf(LOG_NOTE,
                         "D578 cadence: gaps <7:%u 7-9.5:%u 9.5-12:%u 12-20:%u >20:%u ms | dropped %u | "
                         "slots/tick 1:%u 2:%u 3:%u 4+:%u | tick %.0f us | grid %.1f us, re-anchors %u, trims %u, draw peak %.0f us | "
                         "motion hold:%u (exact %u, a=0 %u, late %u, other %u) short:%u ok:%u long:%u jump:%u | present avg %.0f max %.0f us, %u over half a refresh",
                         s_stGap[0], s_stGap[1], s_stGap[2], s_stGap[3], s_stGap[4], s_stDrop,
                         s_stSlotsN[0], s_stSlotsN[1], s_stSlotsN[2], s_stSlotsN[3],
                         s_stFrames ? secs * 1e6 / (double)s_stFrames : 0.0, s_gPeriod, s_stAnchor, s_stPull, s_ipDrawPk,
                         s_stMot[0], s_stHoldWhy[1], s_stHoldWhy[2], s_stHoldWhy[3], s_stHoldWhy[0], s_stMot[1], s_stMot[2], s_stMot[3], s_stMot[4], s_ipSwapEma, s_stSwapMax, s_stSwapLong);
            s_stSwapMax = 0.0;
            s_stSwapLong = 0;
            if (s_stFrames > 0) {
                sysLogPrintf(LOG_NOTE,
                             "D583 game side: work avg %.0f max %.0f us (%u over 3/4 tick) | blocked by our previous frame: %u frames, avg %.0f max %.0f us | pickup avg %.0f max %.0f us",
                             s_stGameSum / s_stFrames, s_stGameMax, s_stGameLong, s_stBlocked, s_stBlockSum / s_stFrames,
                             s_stBlockMax, s_stPickSum / s_stFrames, s_stPickMax);
            }
            s_stGameSum = s_stGameMax = s_stBlockSum = s_stBlockMax = s_stPickSum = s_stPickMax = 0.0;
            s_stGameLong = s_stBlocked = 0;
            s_stAnchor = s_stPull = 0;
            memset(s_stMot, 0, sizeof(s_stMot));
            memset(s_stHoldWhy, 0, sizeof(s_stHoldWhy));
            memset(s_stGap, 0, sizeof(s_stGap));
            memset(s_stSlotsN, 0, sizeof(s_stSlotsN));
            s_stDrop = 0;
            s_stStart = now;
            s_stFrames = s_stExactFrames = s_stPresents = 0;
        }
    }
}

/* Show the next waiting slot; called once it is due. */
static void portInterpPresentNext(void)
{
    /* D583: a present whose successor is already due is skipped: the display
     * fell a whole period behind (a stall, a fixed refresh a little slower
     * than the grid), and showing it would delay every later one. */
    const int64_t leadS = portInterpSwapDriven() ? 1000 : 0;
    while (s_qLen >= 2 && (int64_t)sysGetMicroseconds() >= s_ipTimes[(s_qHead + 1) % IP_SLOTS] - leadS) {
        s_qHead = (s_qHead + 1) % IP_SLOTS;
        s_qLen--;
        s_stDrop++;
    }
    const int k = s_qHead;
    s_qHead = (s_qHead + 1) % IP_SLOTS;
    s_qLen--;
    const int64_t due = s_ipTimes[k];
    if (portInterpSwapDriven() && s_vbLast != 0) {
        /* Swap-driven: no timer, the blocking swap is the clock. Guard only
         * against a swap that does not block (no real VSync): never present
         * twice within 0.7 of the target period. */
        const int hz0 = videoInterpHz();
        /* Based on the measured display period, not the target rate. */
        int rfG = 0, ivG = 0;
        videoInterpInfo(&rfG, &ivG);
        const double pG = rfG > 0 ? 1e6 / (double)rfG : s_vbP > 0.0 ? s_vbP : 1000000.0 / (hz0 > 0 ? hz0 : 60);
        const int64_t minGap = (int64_t)(0.7 * pG);
        for (;;) {
            const int64_t left = s_vbLast + minGap - (int64_t)sysGetMicroseconds();
            if (left <= 0) break;
            if (left > 2000) sysSleep(1000);
            else sysCpuRelax();
        }
    }
    const int64_t t0 = (int64_t)sysGetMicroseconds();
    videoInterpPresent(k);
    const int64_t r = (int64_t)sysGetMicroseconds();
    if (portInterpLog())
        sysLogPrintf(LOG_NOTE, "IPLOG present k=%d at=%lld late=%lld swap=%lld", k, (long long)t0,
                     (long long)(t0 - due), (long long)(r - t0));
    const int hz = videoInterpHz();
    const int64_t P = 1000000 / (hz > 0 ? hz : 60);
    s_stPresents++;
    s_ipSwapEma = s_ipSwapEma > 0.0 ? 0.95 * s_ipSwapEma + 0.05 * (double)(r - t0) : (double)(r - t0);
    if ((double)(r - t0) > s_stSwapMax) s_stSwapMax = (double)(r - t0);
    if ((r - t0) > P / 2) s_stSwapLong++;
    if (s_stLastPres != 0) {
        const int64_t g = r - s_stLastPres;
        s_stGap[g < 7000 ? 0 : g < 9500 ? 1 : g < 12000 ? 2 : g < 20000 ? 3 : 4]++;
    }
    s_stLastPres = r;
    if (s_gPeriod > 0.0) {
        const double st = (s_ipSim[k] - s_stLastSim) / s_gPeriod;
        if (s_stLastSim != 0.0 && st > -1.0 && st < 8.0)
        {
            s_stMot[st < 0.25 ? 0 : st < 0.75 ? 1 : st < 1.25 ? 2 : st < 1.75 ? 3 : 4]++;
            if (st < 0.25) s_stHoldWhy[s_ipWhy[k] & 3]++;
        }
        s_stLastSim = s_ipSim[k];
    }
    if (portInterpSwapDriven()) {
        /* The swap returned at a vblank: that is the display clock. Track the
         * refresh period from gaps near one period (a missed vblank or an idle
         * stretch is not a period). */
        if (s_vbLast != 0) {
            const double gap = (double)(r - s_vbLast);
            if (s_vbP <= 0.0) {
                /* Seed from the REAL refresh, not the target rate (120 target
                 * on a 90 Hz Deck, gamescope caps): else every gap is rejected. */
                int rf = 0, iv = 0;
                videoInterpInfo(&rf, &iv);
                s_vbP = (rf > 0) ? 1e6 / (double)rf : (double)P;
            }
            if (gap > 0.75 * s_vbP && gap < 1.3 * s_vbP) {
                s_vbP = 0.95 * s_vbP + 0.05 * gap;
                s_vbOut = 0;
            } else {
                /* Slow re-seed: many consecutive off-period gaps mean the
                 * estimate is wrong (swap interval change, cap); take the
                 * median of the recent gaps. */
                s_vbOutGap[s_vbOut++ & 7] = gap;
                if (s_vbOut >= 16) {
                    double a[8];
                    for (int i = 0; i < 8; i++) a[i] = s_vbOutGap[i];
                    for (int i = 1; i < 8; i++)
                        for (int j = i; j > 0 && a[j - 1] > a[j]; j--) { double t = a[j]; a[j] = a[j - 1]; a[j - 1] = t; }
                    s_vbP = 0.5 * (a[3] + a[4]);
                    s_vbOut = 0;
                }
            }
            {
                /* D578: never estimate a display period longer than the real
                 * refresh's (+10%). A run of missed vblanks or one-present
                 * ticks re-seeded it to ~16.7 ms on a 120 Hz panel, and the
                 * 0.7-period gate above then held presents at 60. */
                int rf = 0, iv = 0;
                videoInterpInfo(&rf, &iv);
                if (rf > 0 && s_vbP > 1.1e6 / (double)rf) {
                    s_vbP = 1e6 / (double)rf;
                }
            }
        }
        s_vbLast = r;
    }
}

/* D583: a waiting present that falls due while the new frame draws would go
 * out late (by up to the draw time), then the next one on time: an uneven
 * pair of gaps. Show it first, at its time, when the frame can afford the
 * wait: the game is waiting for this frame's SP/DP done, so never when the
 * wait + present + draw would run past ~60% of a tick (a compositor whose
 * swap blocks a whole refresh, a slow GPU: then draw first, as before). */
static void portInterpPresentBeforeDraw(uint64_t viUs)
{
    const int64_t tick = (int64_t)g_tickIntervalUs;
    const int64_t lead = portInterpSwapDriven() ? 1000 : 0;
    const double drawEst = s_ipDrawPk > 2.0 * s_ipPassEma ? s_ipDrawPk : 2.0 * s_ipPassEma;
    while (s_qLen > 0) {
        const int64_t now = (int64_t)sysGetMicroseconds();
        const int64_t due = s_ipTimes[s_qHead] - lead;
        if ((double)due > (double)now + drawEst + 300.0) break;   /* falls after the draw */
        const int64_t wait = due > now ? due - now : 0;
        if ((double)(now - (int64_t)viUs + wait) + s_ipSwapEma + drawEst > 0.6 * (double)tick) break;
        while ((int64_t)sysGetMicroseconds() < due) {
            if (due - (int64_t)sysGetMicroseconds() > 2000) sysSleep(1000);
            else sysCpuRelax();
        }
        portInterpPresentNext();
    }
}

static void *portRenderWorker(void *arg)
{
    (void)arg;
    for (;;) {
        int presentDue = 0;
        pthread_mutex_lock(&s_rwLock);
        while (s_rwTask == NULL) {
            /* Quit while idle: stop taking frames and never touch GL again.
             * The host only waits for the renderer while it is inside a frame
             * (video.c videoHostExitIfRequested), so an idle worker is already
             * a safe exit point; a frame that is running parks itself at its
             * boundary as before (D344). */
            if (videoQuitRequested()) {
                pthread_mutex_unlock(&s_rwLock);
                for (;;) sysSleep(100000);
            }
            /* D578: sleep until the next stored present is due, or a frame. */
            int64_t waitUs = 100 * 1000;
            /* D578: with VSync the slot times are predicted vblanks. Presenting
             * back-to-back and letting the blocking swap pace only works on a
             * fixed refresh: under G-Sync/VRR a swap does not wait for a beat,
             * the slots went out bunched and early, the panel followed that
             * uneven cadence (brightness shimmer) and NVIDIA's layered-DXGI
             * fullscreen path showed black frames. Present each slot at its
             * time minus a small lead; on a fixed refresh the swap still lands
             * on the same vblank. */
            const int64_t lead = portInterpSwapDriven() ? 1000 : 0;
            if (s_qLen > 0) {
                waitUs = s_ipTimes[s_qHead] - lead - (int64_t)sysGetMicroseconds();
                if (waitUs <= 0) {
                    presentDue = 1;
                    break;
                }
            }
            /* An absolute CLOCK_REALTIME deadline is too coarse for this
             * (MinGW: off by several ms either way, measured). While a present
             * is pending, sleep 1 ms at a time to 2 ms before it, then spin,
             * watching for a new frame throughout. */
            if (s_qLen > 0) {
                const int64_t due = s_ipTimes[s_qHead] - lead;
                pthread_mutex_unlock(&s_rwLock);
                for (;;) {
                    const int64_t left = due - (int64_t)sysGetMicroseconds();
                    if (left <= 0 || __atomic_load_n(&s_rwTask, __ATOMIC_ACQUIRE) != NULL) break;
                    if (left > 2000) sysSleep(1000);
                    else sysCpuRelax();
                }
                pthread_mutex_lock(&s_rwLock);
                continue;
            }
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            uint64_t ns = (uint64_t)ts.tv_nsec + (uint64_t)waitUs * 1000u;
            ts.tv_sec += (time_t)(ns / 1000000000u);
            ts.tv_nsec = (long)(ns % 1000000000u);
            pthread_cond_timedwait(&s_rwCond, &s_rwLock, &ts);
        }
        if (presentDue) {
            pthread_mutex_unlock(&s_rwLock);
            portInterpPresentNext();
            continue;
        }
        Gfx *dl = s_rwTask;
        const uint64_t viUs = s_rwTaskViUs;
        pthread_mutex_unlock(&s_rwLock);

        /* D578: with VSync nothing is presented here: a swap blocks for a
         * refresh, and the game waits for SP/DP done (posted below, after the
         * passes are drawn), so a swap before the draw cost the game whole
         * retraces (Deck Game Mode: 90 fps unreachable on idle hardware). The
         * finished passes live in the present-slot FBOs
         * (gfx_opengl_interp_store), so presenting after SP/DP is safe. D583:
         * the previous frame's unshown slots stay queued (ring), they are not
         * dropped: the new frame's slots come after them on the grid. */
        const int hz = videoInterpHz();
        if (hz > 0 && viUs != 0) {
            portInterpPresentBeforeDraw(viUs);
            const int64_t d0 = (int64_t)sysGetMicroseconds();
            const int q0 = s_qLen;
            portRenderGfxTaskInterp(dl, viUs, hz);
            if (portInterpLog())   /* D583: line late presents up against the draw they fell in */
                sysLogPrintf(LOG_NOTE, "IPLOG draw at=%lld dur=%lld vi+%lld q %d->%d next_due=%lld",
                             (long long)d0, (long long)((int64_t)sysGetMicroseconds() - d0),
                             (long long)(d0 - (int64_t)viUs), q0, s_qLen,
                             (long long)(s_qLen > 0 ? s_ipTimes[s_qHead] - d0 : -1));
        } else {
            if (s_ipWasOn) {
                s_ipWasOn = 0;
                s_vbLast = 0; s_vbP = 0.0; s_vbOut = 0;   /* re-measure next time */
                s_qLen = 0; s_gNext = 0.0;
                gfx_interp_reset();
            }
            portRenderGfxTask(dl);
        }

        pthread_mutex_lock(&s_rwLock);
        s_rwTask = NULL;
        s_rwLastDoneUs = (int64_t)sysGetMicroseconds();   /* D583 */
        pthread_cond_broadcast(&s_rwCond);
        pthread_mutex_unlock(&s_rwLock);
        portPostEventForce(OS_EVENT_SP);   /* D134: must not be dropped */
        portPostEventForce(OS_EVENT_DP);   /* gfx task is its own DP task */
    }
    return NULL;
}

static int portRenderWorkerOn(void)
{
    if (s_rwMode == 0) {
        s_rwMode = -1;
        if (g_determEnabled) {
            sysLogPrintf(LOG_NOTE, "D481: GE_DETERM -> gfx tasks run inline");
        } else if (getenv("GE_RENDERINLINE") != NULL) {
            sysLogPrintf(LOG_NOTE, "D481: GE_RENDERINLINE -> gfx tasks run inline");
        } else {
            pthread_attr_t attr;
            pthread_t th;
            pthread_attr_init(&attr);
#if !defined(_WIN32)
            /* Same low stack as the game threads (see portAllocLowStack). */
            void *lowStack = portAllocLowStack(PORT_THREAD_STACK);
            if (lowStack)
                pthread_attr_setstack(&attr, lowStack, PORT_THREAD_STACK);
            else
                pthread_attr_setstacksize(&attr, PORT_THREAD_STACK);
#else
            pthread_attr_setstacksize(&attr, PORT_THREAD_STACK);
#endif
            if (pthread_create(&th, &attr, portRenderWorker, NULL) == 0) {
                pthread_detach(th);
                s_rwMode = 1;
                sysLogPrintf(LOG_NOTE, "D481: render worker started");
            } else {
                sysLogPrintf(LOG_ERROR, "D481: render worker failed to start; gfx tasks run inline");
            }
            pthread_attr_destroy(&attr);
        }
    }
    return s_rwMode == 1;
}

void osSpTaskStartGo(OSTask *t)
{
    if (g_determTraceEnabled && !g_determTaskEverRun) {
        sysLogPrintf(LOG_NOTE,
            "DETERMTRACE seq=%u ticks=%u event=first_task_run tasktype=%d",
            ++g_determTraceSeq, g_determTicks, (int)t->t.type);
    }
    g_determTaskEverRun = 1;   /* GE_DETERM: §D117 M-52 3rd attempt */
    if (t->t.type == M_AUDTASK) {
        /* Phase 3: execute the audio ucode against t->t.data_ptr (an Acmd
         * list). For now the task simply completes; libaudio's amMain still
         * runs its per-frame bookkeeping. */
    } else if (portRenderWorkerOn()) {
        /* D481: hand the display list to the render worker; it posts the
         * SP/DP done events when the frame is presented. */
        pthread_mutex_lock(&s_rwLock);
        if (s_rwTask != NULL) {
            sysLogPrintf(LOG_ERROR, "D481: gfx task submitted while the previous one is still rendering (invariant broken); waiting");
            while (s_rwTask != NULL) {
                pthread_cond_wait(&s_rwCond, &s_rwLock);
            }
        }
        s_rwTask = (Gfx *)t->t.data_ptr;
        s_rwTaskViUs = __atomic_load_n(&g_lastViUs, __ATOMIC_ACQUIRE);   /* D578 */
        s_rwTaskSubmitUs = (int64_t)sysGetMicroseconds();   /* D583 */
        pthread_cond_broadcast(&s_rwCond);
        pthread_mutex_unlock(&s_rwLock);
        return;
    } else {
        /* Graphics task: run the software RSP on the display list inline. */
        portRenderGfxTask((Gfx *)t->t.data_ptr);
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
u32   osVirtualToPhysical(void *va)                { return portHostToN64(va); }
void *osPhysicalToVirtual(u32 pa)                  { return portN64ToHost(pa); }

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

/* ------------------------------------------------------------------------ */
/* D202/M-70: serialized trace writer for the GE_AUDIOTRACE / WIRE probes.  */
/* See port/include/audiotrace.h for why (cross-thread mid-line            */
/* interleaving corrupted the M-69 corpus).                                */
/* ------------------------------------------------------------------------ */
#include <stdarg.h>
#include <stdio.h>

static volatile int geTraceLock = 0;

struct geTraceFile { const char *path; FILE *f; };
static struct geTraceFile geTraceFiles[4];

void geTracePrintf(const char *path, const char *fmt, ...)
{
    struct geTraceFile *tf = NULL;
    va_list ap;

    while (__sync_lock_test_and_set(&geTraceLock, 1)) { /* spin */ }

    for (int i = 0; i < (int)(sizeof(geTraceFiles) / sizeof(geTraceFiles[0])); i++) {
        if (!geTraceFiles[i].path) {
            if (!tf) tf = &geTraceFiles[i];
            continue;
        }
        if (strcmp(geTraceFiles[i].path, path) == 0) { tf = &geTraceFiles[i]; break; }
    }
    if (tf && !tf->f) {
        tf->path = path;
        tf->f = fopen(path, "a");
        if (tf->f) setvbuf(tf->f, NULL, _IONBF, 0);
    }

    if (tf && tf->f) {
        va_start(ap, fmt);
        vfprintf(tf->f, fmt, ap);
        va_end(ap);
    }

    __sync_lock_release(&geTraceLock);
}
