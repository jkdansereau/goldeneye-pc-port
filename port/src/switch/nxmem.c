/*
 * nxmem.c — fixed-address memory for the Nintendo Switch build. See nxmem.h.
 */
#include "platform.h"

#if defined(PLATFORM_SWITCH)

#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "nxmem.h"
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
