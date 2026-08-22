/*
 * Platform primitives: time, logging, paths, exit.
 *
 * Modelled on the PD port's port/src/system.c.
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
  /* D38: <stdlib.h>/<time.h> are shadowed by the decomp's N64 stubs on this
   * include path; declare the host functions this file uses. */
  extern long long time(long long *t);
  extern void abort(void);
  extern void exit(int status);
#else
  #define _POSIX_C_SOURCE 199309L
  #include <unistd.h>
  #include <time.h>
#endif

#include "system.h"

/* --- Time --------------------------------------------------------------- */

#if defined(PLATFORM_WINDOWS)
static LARGE_INTEGER g_qpcFreq;
#endif

uint64_t sysGetMicroseconds(void)
{
#if defined(PLATFORM_WINDOWS)
    static int inited = 0;
    LARGE_INTEGER c;
    if (!inited) {
        QueryPerformanceFrequency(&g_qpcFreq);
        inited = 1;
    }
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1000000.0 / (double)g_qpcFreq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)(ts.tv_nsec / 1000);
#endif
}

uintptr_t sysImageBase(void)
{
#if defined(PLATFORM_WINDOWS)
    return (uintptr_t)GetModuleHandleW(NULL);
#else
    /* TODO: parse /proc/self/maps for the first executable mapping. */
    return 0;
#endif
}

int64_t sysGetTime(void)
{
    return (int64_t)time(NULL);
}

void sysSleep(uint32_t micros)
{
#if defined(PLATFORM_WINDOWS)
    Sleep((DWORD)((micros + 999) / 1000));
#else
    struct timespec req;
    req.tv_sec = micros / 1000000u;
    req.tv_nsec = (long)(micros % 1000000u) * 1000L;
    while (nanosleep(&req, &req) != 0) { /* EINTR: keep sleeping the rest */
    }
#endif
}

/* --- Logging ------------------------------------------------------------ */

void sysLogPrintf(enum LogLevel level, const char *fmt, ...)
{
    static const char *tags[] = { "ERROR", "WARN ", "NOTE ", "INFO ", "DEBUG" };
    va_list ap;
    if ((int)level >= 5) level = LOG_DEBUG;
    fprintf(stderr, "[%s] ", tags[level]);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

void sysFatalError(const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fflush(stderr);
    fprintf(stderr, "[FATAL] ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    fflush(stderr);
    abort();
}

/* --- Command line ------------------------------------------------------- */

static int   g_argc = 0;
static char **g_argv = NULL;

void sysSetArgs(int argc, char **argv)
{
    g_argc = argc;
    g_argv = argv;
}

int sysArgCheck(const char *arg)
{
    for (int i = 1; i < g_argc; ++i) {
        if (g_argv[i] && strcmp(g_argv[i], arg) == 0)
            return 1;
    }
    return 0;
}

const char *sysArgGetString(const char *arg)
{
    for (int i = 1; i < g_argc; ++i) {
        if (g_argv[i] && strcmp(g_argv[i], arg) == 0) {
            if (i + 1 < g_argc)
                return g_argv[i + 1];
        }
    }
    return NULL;
}

/* --- CPU ---------------------------------------------------------------- */

#if defined(PLATFORM_WINDOWS)
  #define PORT_DO_YIELD() YieldProcessor()
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  #include <immintrin.h>
  #define PORT_DO_YIELD() _mm_pause()
#else
  #define PORT_DO_YIELD() do { } while (0)
#endif

void sysCpuRelax(void)
{
    PORT_DO_YIELD();
}

/* --- Paths -------------------------------------------------------------- */

static char exeDir[512] = ".";

const char *sysGetExeDir(void)
{
#if defined(PLATFORM_WINDOWS)
    if (exeDir[0] == '.' && exeDir[1] == 0) {
        DWORD n = GetModuleFileNameA(NULL, exeDir, sizeof(exeDir) - 1);
        if (n) {
            char *slash = strrchr(exeDir, '\\');
            if (slash)
                *slash = 0;
        }
    }
#endif
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
