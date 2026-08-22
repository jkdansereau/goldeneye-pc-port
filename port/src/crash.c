/*
 * Crash handler / stack traces.
 *
 * Catches access violations (Windows) / fatal signals (POSIX) and prints a
 * backtrace with file:line info (build with -g; MinGW emits PDBs so symbols
 * resolve), to make the inevitable porting crashes debuggable.
 *
 * Adapted from the PD port's port/src/crash.c.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <PR/ultratypes.h>
#include "system.h"
#include "platform.h"
#include "crash.h"

#define CRASH_LOG_FNAME "ge007.crash.log"
#define CRASH_MAX_MSG 8192
#define CRASH_MAX_SYM 256
#define CRASH_MAX_FRAMES 32
#define CRASH_MSG(...) \
    if (msglen < CRASH_MAX_MSG) msglen += snprintf(msg + msglen, CRASH_MAX_MSG - msglen, __VA_ARGS__)

#if defined(PLATFORM_WINDOWS)

#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <inttypes.h>
#include <excpt.h>

/* D38: MinGW's <windows.h> does not declare this; add it (NT4+ API). */
extern int GetCurrentThreadStackLimits(PVOID *lpLowAddress, PVOID *lpHighAddress);

static LPTOP_LEVEL_EXCEPTION_FILTER prevExFilter;

static void *crashGetModuleBase(const void *addr)
{
    HMODULE h = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, addr, &h);
    return (void *)h;
}

/*
 * Phase 1 — raw crash info, NO dbghelp. Every call here is a plain Win32
 * query; nothing allocates or walks memory, so this cannot re-fault even if
 * the heap or a stack is corrupted. Written to console + file BEFORE any
 * risky symbolication.
 */
static void crashStackTraceRaw(char *msg, PEXCEPTION_POINTERS exinfo)
{
    CONTEXT context = *exinfo->ContextRecord;
    DWORD msglen = 0;

    CRASH_MSG("EXCEPTION: 0x%08lx\n", exinfo->ExceptionRecord->ExceptionCode);
#if defined(PLATFORM_X86_64)
    CRASH_MSG("PC: %p (Rip=%p Rsp=%p Rbp=%p)\n",
              exinfo->ExceptionRecord->ExceptionAddress,
              (void *)context.Rip, (void *)context.Rsp, (void *)context.Rbp);
    /* FPU state: for STATUS_FLOATING_POINT_* we want to know whether the
     * exception masks were actually cleared (MxCsr bits 7..10 / x87 CW
     * mask bits) or this is a stale status bit. */
    CRASH_MSG("MxCsr=0x%08lx  x87CW=0x%04x x87SW=0x%04x\n",
              (unsigned long)context.MxCsr,
              (unsigned)(context.FloatSave.ControlWord & 0xffff),
              (unsigned)(context.FloatSave.StatusWord & 0xffff));
    PVOID low = NULL, high = NULL;
    if (GetCurrentThreadStackLimits(&low, &high)) {
        CRASH_MSG("thread stack: %p..%p  Rsp=%p  used=%llu bytes\n",
                  low, high, (void *)context.Rsp,
                  (unsigned long long)((uintptr_t)high - (uintptr_t)context.Rsp));
    }
#else
    CRASH_MSG("PC: %p\n", exinfo->ExceptionRecord->ExceptionAddress);
#endif
    CRASH_MSG("MODULE: [%p]\n", crashGetModuleBase(exinfo->ExceptionRecord->ExceptionAddress));
    CRASH_MSG("MAIN MODULE: [%p]\n", crashGetModuleBase(crashInit));
}

/*
 * Phase 2 — backtrace by walking the EBP chain manually. The build keeps
 * frame pointers (-fno-omit-frame-pointer), so every frame is a pair of
 * qwords at RBP: [saved RBP, return address]. No dbghelp, no allocation:
 * each pointer is validated against the thread's stack bounds BEFORE being
 * dereferenced, and the chain must strictly ascend (x64 stacks grow down),
 * so a corrupted chain terminates instead of re-faulting. Addresses are
 * symbolicated offline with addr2line when needed.
 */
static void crashStackTraceSym(char *msg, PEXCEPTION_POINTERS exinfo)
{
    CONTEXT context = *exinfo->ContextRecord;
    DWORD msglen = 0;

#if defined(PLATFORM_X86_64)
    PVOID low = NULL, high = NULL;
    if (!GetCurrentThreadStackLimits(low, high)) {
        snprintf(msg, CRASH_MAX_MSG, "\nBACKTRACE: (no stack limits)\n");
        return;
    }

    uintptr_t fp = context.Rbp;
    int i = 0;
    CRASH_MSG("\nBACKTRACE:\n#00: %p  (crash PC)\n", exinfo->ExceptionRecord->ExceptionAddress);

    while (i < CRASH_MAX_FRAMES - 1) {
        /* The frame record must live inside the committed stack. */
        if (fp < (uintptr_t)low || fp > (uintptr_t)high) break;

        const uintptr_t *frame = (const uintptr_t *)fp;
        const uintptr_t saved_fp = frame[0];
        const uintptr_t ret_addr = frame[1];

        CRASH_MSG("#%02d: %p", i + 1, (void *)ret_addr);
        const uintptr_t modbase = (uintptr_t)crashGetModuleBase((void *)ret_addr);
        if (modbase) {
            CRASH_MSG("  [%p]+%llx", (void *)modbase,
                      (unsigned long long)(ret_addr - modbase));
        }
        CRASH_MSG("\n");

        /* Chain must ascend; stop on the first frame that doesn't. */
        if (saved_fp <= fp || saved_fp > (uintptr_t)high) break;
        fp = saved_fp;
        ++i;
    }
    if (i == CRASH_MAX_FRAMES - 1) {
        CRASH_MSG("...\n");
    }
#else
    snprintf(msg, CRASH_MAX_MSG, "\nBACKTRACE: not implemented on this arch\n");
#endif
}

static void crashWriteLog(const char *msg, int append)
{
    FILE *f = fopen(CRASH_LOG_FNAME, append ? "ab" : "wb");
    if (f) {
        if (!append) fprintf(f, "Crash!\n\n");
        fprintf(f, "%s", msg);
        fclose(f);
    }
}

/*
 * Thread dump: unwind every thread in this process and print a short
 * backtrace for each, labelled with the game-thread name when known.
 * Called from the kernel heartbeat when no frame has rendered for a while,
 * so a hang shows WHERE every thread is stuck instead of as silence.
 *
 * tids/names: parallel arrays (may be NULL/0). Threads not in the list are
 * labelled by TID only. Uses SuspendThread + GetThreadContext + StackWalk64;
 * each thread is resumed before the next is touched, so a hang in one walk
 * cannot wedge the others.
 */
void crashDumpThreads(const unsigned long *tids, const char **names, int count)
{
    HANDLE process = GetCurrentProcess();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    /* Function symbols only — line tables are slow to load and the dump
     * prints function+offset, which is enough to find a hang. */
    SymSetOptions(SymGetOptions() | SYMOPT_DEBUG);
    SymInitialize(process, NULL, TRUE);

    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    if (!Thread32First(snap, &te)) {
        CloseHandle(snap);
        return;
    }

        do {
        if (te.th32OwnerProcessID != GetCurrentProcessId()) continue;

        char label[64] = "?";
        int matched = 0;
        for (int i = 0; i < count; ++i) {
            if (tids && tids[i] == te.th32ThreadID) {
                snprintf(label, sizeof(label), "%s", names ? names[i] : "?");
                matched = 1;
                break;
            }
        }
        /* When a specific thread list is given, skip everything else —
         * walking ~20 system threads with symbol resolution is slow and
         * buries the interesting frames. */
        if (count > 0 && !matched) continue;

        HANDLE hth = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!hth) continue;

        CONTEXT ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_FULL;
        DWORD susps = SuspendThread(hth);
        if (susps != (DWORD)-1 && GetThreadContext(hth, &ctx)) {
            sysLogPrintf(LOG_ERROR, "  [thread %lu '%s']", te.th32ThreadID, label);

#if defined(PLATFORM_X86_64)
            STACKFRAME64 sf;
            memset(&sf, 0, sizeof(sf));
            sf.AddrPC.Offset = ctx.Rip;
            sf.AddrPC.Mode = AddrModeFlat;
            sf.AddrFrame.Offset = ctx.Rbp;
            sf.AddrFrame.Mode = AddrModeFlat;
            sf.AddrStack.Offset = ctx.Rsp;
            sf.AddrStack.Mode = AddrModeFlat;

            char symbuf[sizeof(SYMBOL_INFO) + CRASH_MAX_SYM * sizeof(TCHAR)];
            PSYMBOL_INFO sym = (PSYMBOL_INFO)symbuf;
            sym->SizeOfStruct = sizeof(*sym);
            sym->MaxNameLen = CRASH_MAX_SYM;
            DWORD64 disp64 = 0;

            for (int i = 0; i < 8; ++i) {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, hth, &sf, &ctx, NULL,
                                 SymFunctionTableAccess64, SymGetModuleBase64, NULL))
                    break;
                if (sym->SizeOfStruct == 0) {
                    sym->SizeOfStruct = sizeof(*sym);
                    sym->MaxNameLen = CRASH_MAX_SYM;
                }
                const char *where = "?";
                if (SymFromAddr(process, sf.AddrPC.Offset, &disp64, sym)) {
                    static char linebuf[CRASH_MAX_FRAMES][512];
                    snprintf(linebuf[i % CRASH_MAX_FRAMES], sizeof(linebuf[0]), "%s+%llx",
                             sym->Name, (unsigned long long)disp64);
                    where = linebuf[i % CRASH_MAX_FRAMES];
                }
                sysLogPrintf(LOG_ERROR, "    #%d %p %s", i,
                             (void *)(uintptr_t)sf.AddrPC.Offset, where);
            }
#endif
        }
        ResumeThread(hth);
        CloseHandle(hth);
    } while (Thread32Next(snap, &te));

    CloseHandle(snap);
    SymCleanup(process);
}

static LONG __stdcall crashHandler(PEXCEPTION_POINTERS exinfo)
{
    char msg[CRASH_MAX_MSG + 1] = { 0 };

    if (IsDebuggerPresent()) {
        if (prevExFilter) {
            return prevExFilter(exinfo);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    /* Phase 1: raw info, guaranteed to survive. */
    crashStackTraceRaw(msg, exinfo);
    sysLogPrintf(LOG_ERROR, "FATAL: %s", msg);
    fflush(stderr);
    fflush(stdout);
    crashWriteLog(msg, 0);

    /* Phase 2: EBP-chain backtrace (validated reads only — see
     * crashStackTraceSym). The raw data is already on disk either way. */
    {
        char sym[CRASH_MAX_MSG + 1] = { 0 };
        crashStackTraceSym(sym, exinfo);
        if (sym[0]) {
            sysLogPrintf(LOG_ERROR, "%s", sym);
            crashWriteLog(sym, 1);
        }
    }

    /* Terminate directly — abort() raises SIGABRT which can re-enter the
     * exception machinery while we are already inside it. */
    TerminateProcess(GetCurrentProcess(), exinfo->ExceptionRecord->ExceptionCode);
    return EXCEPTION_CONTINUE_EXECUTION;
}

#elif defined(PLATFORM_LINUX)

#include <ucontext.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <ctype.h>
#include <dlfcn.h>
#include <sys/fcntl.h>

static struct sigaction prevSigAction;

static void *crashGetModuleBase(const void *addr)
{
    Dl_info info;
    if (dladdr(addr, &info)) {
        return info.dli_fbase;
    }
    return NULL;
}

static void crashStackTrace(char *msg, int sig, void *pc)
{
    unsigned msglen = 0;
    void *frames[CRASH_MAX_FRAMES] = { NULL };

    const int nframes = backtrace(frames, CRASH_MAX_FRAMES);
    if (nframes <= 0) {
        CRASH_MSG("no information\n");
        return;
    }

    char **strings = backtrace_symbols(frames, nframes);

    CRASH_MSG("SIGNAL: %d\n", sig);
    CRASH_MSG("PC: ");
    if (pc) {
        CRASH_MSG("%p\n", pc);
    } else if (strings) {
        CRASH_MSG("%s\n", strings[0]);
    } else {
        CRASH_MSG("%p\n", frames[0]);
    }

    CRASH_MSG("MODULE: %p\n", crashGetModuleBase(frames[0]));
    CRASH_MSG("MAIN MODULE: %p\n", crashGetModuleBase(crashInit));
    CRASH_MSG("\nBACKTRACE:\n");

    int i;
    for (i = 0; i < nframes; ++i) {
        CRASH_MSG("#%02d: ", i);
        if (strings && strings[i]) {
            CRASH_MSG("%s\n", strings[i]);
        } else {
            CRASH_MSG("%p\n", frames[i]);
        }
    }

    if (i == CRASH_MAX_FRAMES) {
        CRASH_MSG("...\n");
    } else if (i <= 1) {
        CRASH_MSG("no information\n");
    }

    free(strings);
}

static void crashHandler(int sig, siginfo_t *siginfo, void *ctx)
{
    char msg[CRASH_MAX_MSG + 1] = { 0 };

    void *pc = NULL;
    if (ctx) {
        ucontext_t *ucontext = (ucontext_t *)ctx;
#ifdef PLATFORM_X86
        pc = (void *)ucontext->uc_mcontext.gregs[REG_EIP];
#elif defined(PLATFORM_X86_64)
        pc = (void *)ucontext->uc_mcontext.gregs[REG_RIP];
#endif
    }

    sysLogPrintf(LOG_ERROR, "FATAL: Crashed: PC=%p SIGNAL=%d", pc, sig);

    fflush(stderr);
    fflush(stdout);

    crashStackTrace(msg, sig, pc);

    {
        FILE *f = fopen(CRASH_LOG_FNAME, "wb");
        if (f) {
            fprintf(f, "Crash!\n\n%s", msg);
            fclose(f);
        }
    }

    sysFatalError("Crash!\n\n%s", msg);
}

#endif /* PLATFORM_WINDOWS / PLATFORM_LINUX */

int g_CrashEnabled = 0;

void crashInit(void)
{
#if defined(PLATFORM_WINDOWS)
    SetErrorMode(SEM_FAILCRITICALERRORS);
    prevExFilter = SetUnhandledExceptionFilter(crashHandler);
    g_CrashEnabled = 1;
#elif defined(PLATFORM_LINUX)
    struct sigaction sigact = { 0 };
    sigact.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigact.sa_sigaction = crashHandler;
    sigaction(SIGSEGV, &sigact, &prevSigAction);
    sigaction(SIGABRT, &sigact, &prevSigAction);
    sigaction(SIGBUS,  &sigact, &prevSigAction);
    sigaction(SIGILL,  &sigact, &prevSigAction);
    g_CrashEnabled = 1;
#endif
}

void crashShutdown(void)
{
    if (!g_CrashEnabled) {
        return;
    }
#if defined(PLATFORM_WINDOWS)
    if (prevExFilter) {
        SetUnhandledExceptionFilter(prevExFilter);
    }
#elif defined(PLATFORM_LINUX)
    sigaction(SIGSEGV, &prevSigAction, NULL);
    sigaction(SIGABRT, &prevSigAction, NULL);
    sigaction(SIGBUS,  &prevSigAction, NULL);
    sigaction(SIGILL,  &prevSigAction, NULL);
#endif
    g_CrashEnabled = 0;
}
