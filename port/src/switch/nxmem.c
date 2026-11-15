/*
 * nxmem.c — fixed-address memory for the Nintendo Switch build. See nxmem.h.
 */
#include "platform.h"

#if defined(PLATFORM_SWITCH)

#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <switch.h>

#include "nxmem.h"
#include "portaddr.h"
#include "system.h"

#define NX_PAGE       0x1000ULL
#define NX_MAX_BLOCKS 32

enum { NX_NONE = 0, NX_STACKMAP, NX_CODEMEM, NX_ALIAS };

typedef struct {
    uintptr_t addr;
    size_t size;
    void *src;   /* heap backing (NX_STACKMAP / NX_CODEMEM) */
    uintptr_t primary;   /* NX_ALIAS: the code-memory block it views */
    int kind;
} NxBlock;

int g_nxDramK0Folded = 0;

static NxBlock s_blocks[NX_MAX_BLOCKS];
static int s_init = 0;
static u64 s_stackBase, s_stackSize;
static Handle s_self = INVALID_HANDLE;

static void nxInit(void)
{
    if (s_init) {
        return;
    }
    s_init = 1;
    svcGetInfo(&s_stackBase, InfoType_StackRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&s_stackSize, InfoType_StackRegionSize, CUR_PROCESS_HANDLE, 0);
    s_self = envGetOwnProcessHandle();
    sysLogPrintf(LOG_NOTE, "nxmem: stack region 0x%010lx-0x%010lx, own process handle %s",
                 s_stackBase, s_stackBase + s_stackSize,
                 s_self != INVALID_HANDLE ? "available" : "MISSING (not title takeover / hbloader?)");
}

static NxBlock *nxSlot(void)
{
    for (int i = 0; i < NX_MAX_BLOCKS; i++) {
        if (s_blocks[i].kind == NX_NONE) {
            return &s_blocks[i];
        }
    }
    return NULL;
}

static int nxRangeFree(uintptr_t a, size_t s)
{
    while (s) {
        MemoryInfo mi;
        u32 pi;
        if (R_FAILED(svcQueryMemory(&mi, &pi, a)) || mi.type != MemType_Unmapped) {
            return 0;
        }
        u64 end = mi.addr + mi.size;
        if (end >= a + s) {
            return 1;
        }
        s -= end - a;
        a = end;
    }
    return 1;
}

static const char *nxDescribe(uintptr_t a)
{
    MemoryInfo mi;
    u32 pi;
    static char buf[96];
    if (R_FAILED(svcQueryMemory(&mi, &pi, a))) {
        return "query failed";
    }
    snprintf(buf, sizeof(buf), "type 0x%x block 0x%010lx+0x%lx", (unsigned)mi.type, mi.addr, mi.size);
    return buf;
}

uintptr_t g_portAddrBase = 0;

/* The parts of the N64 window the port maps (offsets from the window base;
 * see portaddr.h). Sizes are the room reserved, not what is used. */
static const struct {
    u64 off;
    u64 size;
    const char *what;
} s_windowParts[] = {
    { 0x10000000ULL, 0x08000000ULL, "cart" },    /* ROM + sidecars (128 MiB room) */
    { 0x70000000ULL, 0x00800000ULL, "DRAM V1" },
    { 0x80000000ULL, 0x00800000ULL, "DRAM KSEG0" },
    { 0xA0000000ULL, 0x20000000ULL, "stacks" },  /* libultra.c PORT_STACK_REGION_* */
};

void nxChooseAddrBase(void)
{
    nxInit();
    if (g_portAddrBase != 0) {
        return;
    }
    if (s_self == INVALID_HANDLE) {
        /* No own process handle (Ryujinx, and any launch that is not
         * hbloader/title takeover): code memory is out of reach, and only the
         * stack region can be mapped at a chosen address. That region is below
         * 4 GiB, so the base stays 0 and the cart/DRAM windows sit at their
         * N64 addresses, as before the variable base. */
        sysLogPrintf(LOG_NOTE, "nxmem: no own process handle, N64 window base 0 (stack-region mapping only)");
        return;
    }
    u64 aslrBase = 0, aslrSize = 0;
    svcGetInfo(&aslrBase, InfoType_AslrRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&aslrSize, InfoType_AslrRegionSize, CUR_PROCESS_HANDLE, 0);
    const u64 aslrEnd = aslrBase + aslrSize;
    const u64 slot = 1ULL << 32;

    /* The kernel keeps the heap and alias regions for itself inside the ASLR
     * range: svcQueryMemory reports their free pages as unmapped, but code
     * memory cannot be mapped there (svcMapProcessCodeMemory fails with
     * rc=0xd401). Slots that touch them are skipped. */
    u64 heapBase = 0, heapSize = 0, aliasBase = 0, aliasSize = 0;
    svcGetInfo(&heapBase, InfoType_HeapRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&heapSize, InfoType_HeapRegionSize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&aliasBase, InfoType_AliasRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&aliasSize, InfoType_AliasRegionSize, CUR_PROCESS_HANDLE, 0);
    sysLogPrintf(LOG_NOTE, "nxmem: heap region 0x%010lx+0x%lx, alias region 0x%010lx+0x%lx",
                 (unsigned long)heapBase, (unsigned long)heapSize, (unsigned long)aliasBase,
                 (unsigned long)aliasSize);

    /* Slot 0 is skipped on purpose: every launch takes the same code path
     * (code memory in the ASLR region), instead of one rarely-exercised
     * fallback. The first free slot from the random start wins. */
    const u64 image = sysImageBase();
    /* Slots 1..n fit in the ASLR region. The search starts at a random one and
     * wraps, so every launch exercises a different base (and the "slot taken,
     * try the next" path) instead of one slot that never fails. */
    u64 n = 0;
    while ((n + 1) * slot + 0xC0000000ULL <= aslrEnd) {
        n++;
    }
    const u64 start = n ? armGetSystemTick() % n : 0;
    int tried = 0;
    for (u64 k = 0; k < n; k++) {
        const u64 h = (1 + (start + k) % n) * slot;
        if (image - h < slot) {
            continue;   /* keep the executable outside the window, like on every other host */
        }
        int ok = 1;
        for (size_t i = 0; i < sizeof(s_windowParts) / sizeof(s_windowParts[0]); i++) {
            uintptr_t a = (uintptr_t)(h + s_windowParts[i].off);
            const u64 e = a + s_windowParts[i].size;
            if (a < aslrBase || e > aslrEnd ||
                (a < heapBase + heapSize && e > heapBase) ||
                (a < aliasBase + aliasSize && e > aliasBase) ||
                !nxRangeFree(a, (size_t)s_windowParts[i].size)) {
                ok = 0;
                break;
            }
        }
        tried++;
        if (ok) {
            g_portAddrBase = (uintptr_t)h;
            sysLogPrintf(LOG_NOTE, "nxmem: N64 window base 0x%010lx (slot %lu of %lu, %d tried; ASLR 0x%010lx-0x%010lx)",
                         (unsigned long)h, (unsigned long)(h / slot), (unsigned long)n, tried,
                         (unsigned long)aslrBase, (unsigned long)aslrEnd);
            return;
        }
    }
    sysFatalError("nxmem: no free 4 GiB slot for the N64 window (%d tried, ASLR 0x%lx-0x%lx)",
                  tried, (unsigned long)aslrBase, (unsigned long)aslrEnd);
}

void *nxMapFixed(uintptr_t addr, size_t size)
{
    nxInit();
    size = (size + NX_PAGE - 1) & ~(size_t)(NX_PAGE - 1);
    NxBlock *b = nxSlot();
    if (!b) {
        sysLogPrintf(LOG_ERROR, "nxmem: out of block slots");
        return NULL;
    }
    if (!nxRangeFree(addr, size)) {
        sysLogPrintf(LOG_ERROR, "nxmem: 0x%010lx+0x%lx is not free (%s)", addr, size, nxDescribe(addr));
        return NULL;
    }
    void *src = memalign(NX_PAGE, size);
    if (!src) {
        sysLogPrintf(LOG_ERROR, "nxmem: out of heap for 0x%lx bytes", size);
        return NULL;
    }
    memset(src, 0, size);

    Result rc;
    int inStack = addr >= s_stackBase && addr + size <= s_stackBase + s_stackSize;
    virtmemLock();
    if (inStack) {
        rc = svcMapMemory((void *)addr, src, size);
        if (R_SUCCEEDED(rc)) {
            b->kind = NX_STACKMAP;
        }
    } else if (s_self != INVALID_HANDLE) {
        rc = svcMapProcessCodeMemory(s_self, addr, (u64)src, size);
        if (R_SUCCEEDED(rc)) {
            rc = svcSetProcessMemoryPermission(s_self, addr, size, Perm_Rw);
            if (R_FAILED(rc)) {
                svcUnmapProcessCodeMemory(s_self, addr, (u64)src, size);
            } else {
                b->kind = NX_CODEMEM;
            }
        }
    } else {
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    virtmemUnlock();

    if (b->kind == NX_NONE) {
        sysLogPrintf(LOG_ERROR, "nxmem: could not map 0x%010lx+0x%lx via %s (rc=0x%x)", addr, size,
                     inStack ? "svcMapMemory" : "code memory", rc);
        free(src);
        return NULL;
    }
    b->addr = addr;
    b->size = size;
    b->src = src;
    sysLogPrintf(LOG_NOTE, "nxmem: mapped 0x%010lx+0x%lx (%s)", addr, size,
                 b->kind == NX_STACKMAP ? "stack region" : "code memory");
    return (void *)addr;
}

void *nxMapAlias(uintptr_t view, uintptr_t primary, size_t size)
{
    nxInit();
    size = (size + NX_PAGE - 1) & ~(size_t)(NX_PAGE - 1);
    NxBlock *b = nxSlot();
    if (!b || s_self == INVALID_HANDLE || !nxRangeFree(view, size)) {
        return NULL;
    }
    virtmemLock();
    Result rc = svcMapProcessMemory((void *)view, s_self, primary, size);
    virtmemUnlock();
    if (R_FAILED(rc)) {
        sysLogPrintf(LOG_NOTE, "nxmem: alias 0x%010lx -> 0x%010lx refused (rc=0x%x)", view, primary, rc);
        return NULL;
    }
    b->kind = NX_ALIAS;
    b->addr = view;
    b->size = size;
    b->primary = primary;
    sysLogPrintf(LOG_NOTE, "nxmem: alias 0x%010lx -> 0x%010lx (+0x%lx)", view, primary, size);
    return (void *)view;
}

void nxUnmapFixed(uintptr_t addr)
{
    for (int i = 0; i < NX_MAX_BLOCKS; i++) {
        NxBlock *b = &s_blocks[i];
        if (b->kind == NX_NONE || b->addr != addr) {
            continue;
        }
        Result rc = 0;
        virtmemLock();
        if (b->kind == NX_STACKMAP) {
            rc = svcUnmapMemory((void *)b->addr, b->src, b->size);
        } else if (b->kind == NX_CODEMEM) {
            rc = svcUnmapProcessCodeMemory(s_self, b->addr, (u64)b->src, b->size);
        } else if (b->kind == NX_ALIAS) {
            rc = svcUnmapProcessMemory((void *)b->addr, s_self, b->primary, b->size);
        }
        virtmemUnlock();
        if (R_FAILED(rc)) {
            sysLogPrintf(LOG_ERROR, "nxmem: unmap 0x%010lx failed (rc=0x%x); leaking it", b->addr, rc);
        } else if (b->src) {
            free(b->src);
        }
        memset(b, 0, sizeof(*b));
        return;
    }
}

#ifdef NX_LINKSTDIO
/* Test builds: libnx's usual pair, sockets first and then the nxlink stream
 * (stdout + stderr go to the PC that uploaded the NRO). */
extern void videoRequestQuit(const char *why);   /* port/src/video.c */

static Thread s_watchdog;

extern volatile int g_portQuit;                   /* port/src/libultra.c: orderly exit under way */

static void nxWatchdog(void *arg)
{
    (void)arg;
    int lim = 80;   /* tenths of a second after a 2 s head start; NX_TEST_SECS=<n> lengthens the run (perf tests) */
    svcSleepThread(2000000000LL);   /* main() sets the NAME=value arguments right after this thread starts */
    {
        const char *e = getenv("NX_TEST_SECS");
        if (e && atoi(e) > 0) lim = atoi(e) * 10;
    }
    for (int i = 0; i < lim && !g_portQuit; i++) {
        svcSleepThread(100000000LL);
    }
    if (g_portQuit) {
        return;   /* the player quit first */
    }
    sysLogPrintf(LOG_NOTE, "nxlink test build: %d s elapsed, quitting like \"Quit to desktop\"", lim / 10 + 2);
    videoRequestQuit("nxlink test build: time elapsed");   /* the host thread then takes the orderly exit */
    for (int i = 0; i < 100 && !g_portQuit; i++) {
        svcSleepThread(100000000LL);
    }
    if (g_portQuit) {
        return;
    }
    sysLogPrintf(LOG_ERROR, "nxlink test build: still running 10 s after the quit request, hard exit");
    nxExitProcess();
}

/* Test builds: what is still mapped in a special way just before control goes
 * back to the launcher. The loader unloads our image and heap afterwards and
 * aborts (rc=0xd401, InvalidCurrentMemory) when some pages are not in the
 * plain state it expects (aliases, transfer memory, locked or shared pages):
 * every block that is neither unmapped nor ordinary heap/code, or has a
 * reference count, is listed. */
void nxDumpMemState(void)
{
    u64 addr = 0;
    int shown = 0;
    sysLogPrintf(LOG_NOTE, "nxmem: memory state before returning to the launcher:");
    for (int guard = 0; guard < 4096 && shown < 80; guard++) {
        MemoryInfo mi;
        u32 pi;
        if (R_FAILED(svcQueryMemory(&mi, &pi, addr))) {
            break;
        }
        const int plain = mi.type == MemType_Unmapped || mi.type == MemType_Heap ||
                          mi.type == MemType_CodeStatic || mi.type == MemType_CodeMutable ||
                          mi.type == MemType_ModuleCodeStatic || mi.type == MemType_ModuleCodeMutable ||
                          mi.type == MemType_Io || mi.type == MemType_Reserved ||
                          mi.type == MemType_ThreadLocal;
        if ((!plain || mi.attr != 0 || mi.ipc_refcount || mi.device_refcount) &&
            mi.type != MemType_Unmapped) {
            sysLogPrintf(LOG_NOTE, "  0x%010lx+0x%lx type 0x%x attr 0x%x perm 0x%x ipc %u dev %u",
                         (unsigned long)mi.addr, (unsigned long)mi.size, (unsigned)mi.type,
                         (unsigned)mi.attr, (unsigned)mi.perm, (unsigned)mi.ipc_refcount,
                         (unsigned)mi.device_refcount);
            shown++;
        }
        const u64 next = mi.addr + mi.size;
        if (next <= addr || next == 0) {
            break;
        }
        addr = next;
    }
    sysLogPrintf(LOG_NOTE, "nxmem: end of the memory state (%d special block(s) listed)", shown);
}

/* Close the nxlink stream (and the sockets): last thing before returning. */
void nxLinkStdioClose(void)
{
    fflush(NULL);
    socketExit();
}

/* The watchdog must not outlive the game: control goes back to the launcher. */
static void nxWatchdogStop(void)
{
    threadWaitForExit(&s_watchdog);
    threadClose(&s_watchdog);
}

void nxLinkStdio(void)
{
    socketInitializeDefault();
    nxlinkStdio();
    /* Test builds only: end the run by itself so a series of launches needs no
     * button presses (the stream closes when the process exits). */
    if (R_SUCCEEDED(threadCreate(&s_watchdog, nxWatchdog, NULL, NULL, 0x4000, 0x2C, -2))) {
        threadStart(&s_watchdog);
    }
}
#endif

/* Release every block (orderly exit, once no thread runs on them any more):
 * aliases first, they view the code-memory blocks. */
void nxReleaseAll(void)
{
#ifdef NX_LINKSTDIO
    nxWatchdogStop();
#endif
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < NX_MAX_BLOCKS; i++) {
            NxBlock *b = &s_blocks[i];
            if (b->kind == NX_NONE) {
                continue;
            }
            if ((pass == 0) == (b->kind == NX_ALIAS)) {
                nxUnmapFixed(b->addr);
            }
        }
    }
    sysLogPrintf(LOG_NOTE, "nxmem: mapped blocks released");
}

/* 1 when the console runs handheld (built-in 1280x720 panel), 0 when docked. */
int nxIsHandheld(void)
{
    return appletGetOperationMode() == AppletOperationMode_Handheld;
}

/* D959: host-thread applet service. Keeps the applet message queue serviced
 * (HOME, sleep, dock/undock) without touching SDL's event queue: on Switch
 * every SDL_PollEvent runs SDL's SWITCH_PumpEvents, which on an operation
 * mode change destroys and recreates the EGL surface on the CALLING thread,
 * and that must be the render thread (see video.c videoPumpEvents). Logs the
 * mode change. Returns 0 once the system asked the application to exit. */
int nxAppletAlive(void)
{
    static int lastMode = -1;
    int alive = appletMainLoop() ? 1 : 0;
    int mode = (int)appletGetOperationMode();
    if (mode != lastMode) {
        if (lastMode >= 0) {
            sysLogPrintf(LOG_INFO, "nxmem: operation mode %s -> %s",
                         lastMode == AppletOperationMode_Handheld ? "handheld" : "docked",
                         mode == AppletOperationMode_Handheld ? "handheld" : "docked");
        }
        lastMode = mode;
    }
    return alive;
}

void nxExitProcess(void)
{
    sysLogPrintf(LOG_INFO, "nxmem: exiting process");
    fflush(NULL);
    /* A process that just vanishes is, to AM, a crash: HOME shows "The
     * software was closed". The end AM expects for an application is
     * ISelfController::Exit (command 0), the request libnx's own teardown
     * sends before it waits to be ended. Sent while the applet session is
     * still open; AM then ends the process. If the request fails, or AM has
     * not ended it within 3 s, the kernel does (emulators ignore Exit). (Same fix as dusklight's
     * switch_startup.cpp end_process_via_am.) */
    AppletType type = appletGetAppletType();
    if (type == AppletType_Application || type == AppletType_SystemApplication) {
        Service *self = appletGetServiceSession_SelfController();
        if (self != NULL && serviceIsActive(self) && R_SUCCEEDED(serviceDispatch(self, 0))) {
            for (int i = 0; i < 30; i++) {
                svcSleepThread(100000000LL);
            }
        }
    }
    svcExitProcess();
    __builtin_unreachable();
}

void nxSetThreadCore(int idealCore)
{
    /* Cores 0..2 belong to the application; core 3 is the system's. */
    Result rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, idealCore, 0x7);
    if (R_FAILED(rc)) {
        sysLogPrintf(LOG_WARNING, "nxthread: svcSetThreadCoreMask(%d, 0x7) failed (rc=0x%x)", idealCore, rc);
    }
}

#endif /* PLATFORM_SWITCH */
