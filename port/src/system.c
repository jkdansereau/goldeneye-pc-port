/*
 * Platform primitives: time, logging, paths, exit.
 *
 * Modelled on the PD port's port/src/system.c (~315 lines).
 *
 * STATUS: scaffolding stub — implement during Phase 1.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "platform.h"
#include "system.h"

/* --- Time --------------------------------------------------------------- */

uint64_t sysGetMicroseconds(void)
{
    /* TODO(Phase 1): use a monotonic clock.
     *  - Windows: QueryPerformanceCounter
     *  - POSIX:   clock_gettime(CLOCK_MONOTONIC)
     */
    return 0;
}

int64_t sysGetTime(void)
{
    /* TODO(Phase 1): wall clock (time(NULL)). */
    return 0;
}

void sysSleep(uint32_t micros)
{
    /* TODO(Phase 1): Sleep() / usleep(). */
    (void)micros;
}

/* --- Logging ------------------------------------------------------------ */

void sysLogPrintf(enum LogLevel level, const char *fmt, ...)
{
    static const char *tags[] = { "ERROR", "WARN ", "INFO ", "DEBUG" };
    va_list ap;
    fprintf(stderr, "[%s] ", tags[level]);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/* --- Paths -------------------------------------------------------------- */

static char exeDir[512] = ".";

const char *sysGetExeDir(void)
{
    /* TODO(Phase 1): resolve the executable's directory.
     *  - Windows: GetModuleFileName
     *  - Linux:   readlink /proc/self/exe
     *  - macOS:   _NSGetExecutablePath
     */
    return exeDir;
}

const char *sysResolvePath(const char *path)
{
    static char out[1024];
    /* TODO(Phase 1): expand "$S/" (data dir) and "$E/" (exe dir) prefixes. */
    strncpy(out, path, sizeof(out) - 1);
    out[sizeof(out) - 1] = 0;
    return out;
}

/* --- Lifecycle ---------------------------------------------------------- */

void sysExit(int code)
{
    exit(code);
}
