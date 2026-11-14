/*
 * nxcrash.c — crash reports for the Nintendo Switch build (project-dam).
 *
 * libnx calls __libnx_exception_handler on a dedicated stack when a thread
 * faults. Write a report to data/ge007-crash.log with addresses relative to
 * the NRO image (symbolize with: aarch64-none-elf-addr2line -fipe
 * build-switch/ge007.aarch64.elf 0x<offset>), then leave to the HOME menu.
 * The port is built with -fno-omit-frame-pointer, so the x29 chain gives a
 * backtrace.
 */
#include "platform.h"

#if defined(PLATFORM_SWITCH)

#include <stdio.h>
#include <switch.h>

#include "system.h"

__attribute__((aligned(16))) u8 __nx_exception_stack[0x10000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
u32 __nx_exception_ignoredebug = 1;

static const char *nxExcName(u32 d)
{
    switch (d) {
    case ThreadExceptionDesc_InstructionAbort: return "instruction abort";
    case ThreadExceptionDesc_MisalignedPC: return "misaligned PC";
    case ThreadExceptionDesc_MisalignedSP: return "misaligned SP";
    case ThreadExceptionDesc_SError: return "SError";
    case ThreadExceptionDesc_BadSVC: return "bad SVC";
    case ThreadExceptionDesc_Trap: return "trap";
    case ThreadExceptionDesc_Other: return "data abort / other";
    default: return "?";
    }
}

/* Readable, mapped memory? (avoid faulting inside the handler) */
static int nxReadable(u64 a)
{
    MemoryInfo mi;
    u32 pi;
    if (a & 7) {
        return 0;
    }
    if (R_FAILED(svcQueryMemory(&mi, &pi, a))) {
        return 0;
    }
    return mi.type != MemType_Unmapped && (mi.perm & Perm_R);
}

static void nxAddr(FILE *f, const char *what, u64 a, u64 base)
{
    if (a >= base && a - base < 0x10000000ULL) {
        fprintf(f, "  %-4s 0x%016lx  (nro+0x%lx)\n", what, a, a - base);
    } else {
        fprintf(f, "  %-4s 0x%016lx\n", what, a);
    }
}

static void nxCrashDump(FILE *f, ThreadExceptionDump *ctx, u64 base)
{
    fprintf(f, "\n==== project-dam crash: %s (desc 0x%x) ====\n", nxExcName(ctx->error_desc),
            ctx->error_desc);
    fprintf(f, "nro base 0x%016lx\n", base);
    nxAddr(f, "pc", ctx->pc.x, base);
    nxAddr(f, "lr", ctx->lr.x, base);
    fprintf(f, "  far  0x%016lx\n", ctx->far.x);
    fprintf(f, "  sp   0x%016lx  fp 0x%016lx\n", ctx->sp.x, ctx->fp.x);
    for (int i = 0; i < 29; i++) {
        fprintf(f, "  x%-2d 0x%016lx%s", i, ctx->cpu_gprs[i].x, (i % 2) ? "\n" : "");
    }
    fprintf(f, "\nbacktrace (x29 chain):\n");
    u64 fp = ctx->fp.x;
    for (int i = 0; i < 32 && fp && nxReadable(fp) && nxReadable(fp + 8); i++) {
        u64 next = ((u64 *)fp)[0];
        u64 ret = ((u64 *)fp)[1];
        char tag[8];
        snprintf(tag, sizeof(tag), "#%02d", i);
        nxAddr(f, tag, ret, base);
        if (next <= fp) {
            break;
        }
        fp = next;
    }
}

void __libnx_exception_handler(ThreadExceptionDump *ctx)
{
    /* Not libnx's __start__: it is an absolute symbol (0). */
    const u64 base = (u64)sysImageBase();
    FILE *f = fopen("data/ge007-crash.log", "a");
    if (!f) {
        f = fopen("sdmc:/switch/project-dam/ge007-crash.log", "a");
    }
    if (f) {
        nxCrashDump(f, ctx, base);
        fclose(f);
    }
#ifdef NX_LINKSTDIO
    nxCrashDump(stderr, ctx, base);   /* reaches the PC over nxlink */
    fflush(stderr);
#endif
    svcExitProcess();
}

#endif /* PLATFORM_SWITCH */
