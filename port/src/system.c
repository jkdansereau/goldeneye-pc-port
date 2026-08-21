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

#if defined(PLATFORM_WINDOWS)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <io.h>
#else
  #include <unistd.h>
#endif

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

/*
 * Expand the "$S/" (data dir) and "$E/" (exe dir) prefixes used throughout
 * the port. Anything else is returned unchanged (relative to the CWD).
 *
 * $E: directory containing the executable.
 * $S: "data/" next to the CWD if it exists there, else "data/" next to the
 *     executable (so running build-pc/ge007.*.exe from anywhere finds
 *     ./data/ when run from the repo root).
 */
const char *sysResolvePath(const char *path)
{
    static char out[1024];
#if defined(PLATFORM_WINDOWS)
    static char exedir[1024] = "";
    if (!exedir[0]) {
        DWORD n = GetModuleFileNameA(NULL, exedir, sizeof(exedir) - 1);
        if (n) {
            char *slash = strrchr(exedir, '\\');
            if (slash)
                *slash = 0;
        } else {
            exedir[0] = 0;
        }
    }
#endif

    if (!strncmp(path, "$E/", 3)) {
#if defined(PLATFORM_WINDOWS)
        snprintf(out, sizeof(out), "%s\\%s", exedir, path + 3);
#else
        snprintf(out, sizeof(out), "./%s", path + 3);
#endif
        return out;
    }
    if (!strncmp(path, "$S/", 3)) {
#if defined(PLATFORM_WINDOWS)
        if (_access("data", 0) == 0)
            snprintf(out, sizeof(out), "data\\%s", path + 3);
        else
            snprintf(out, sizeof(out), "%s\\data\\%s", exedir, path + 3);
#else
        if (access("data", F_OK) == 0)
            snprintf(out, sizeof(out), "data/%s", path + 3);
        else
            snprintf(out, sizeof(out), "./data/%s", path + 3);
#endif
        return out;
    }

    strncpy(out, path, sizeof(out) - 1);
    out[sizeof(out) - 1] = 0;
    return out;
}

/* --- Lifecycle ---------------------------------------------------------- */

void sysExit(int code)
{
    exit(code);
}
