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
  #define _POSIX_C_SOURCE 200112L
  #include <unistd.h>
  #include <time.h>
  #include <sys/stat.h>
  #if defined(PLATFORM_MACOS)
    #include <mach-o/dyld.h> /* _dyld_get_image_header, _NSGetExecutablePath */
  #endif
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
#elif defined(PLATFORM_MACOS)
    /* The mach_header of the main image == its load address. Unlike Windows
     * there is no fixed base: arm64 is PIE-only (and `-image_base` is ignored),
     * so this is 0x1_0000_0000 + a per-run ASLR slide. M1's image-relative
     * address rule uses this; get the slide from _dyld_get_image_vmaddr_slide. */
    return (uintptr_t)_dyld_get_image_header(0);
#else
    /* TODO: parse /proc/self/maps for the first executable mapping. */
    return 0;
#endif
}

/* Fill `buf` with the directory containing the executable, or "" on failure.
 * Linux reads /proc/self/exe; macOS uses _NSGetExecutablePath (no /proc, and
 * _POSIX_C_SOURCE=199309L above hides readlink on Darwin). */
static void getExeDir(char *buf, size_t buflen)
{
    buf[0] = 0;
#if defined(PLATFORM_MACOS)
    uint32_t n = (uint32_t)buflen;
    if (_NSGetExecutablePath(buf, &n) == 0) {
        char *slash = strrchr(buf, '/');
        if (slash) *slash = 0; else buf[0] = 0;
    } else {
        buf[0] = 0;
    }
#elif defined(PLATFORM_LINUX)
    ssize_t n = readlink("/proc/self/exe", buf, buflen - 1);
    if (n > 0) {
        buf[n] = 0;
        char *slash = strrchr(buf, '/');
        if (slash) *slash = 0; else buf[0] = 0;
    } else {
        buf[0] = 0;
    }
#else
    (void)buf; (void)buflen;
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

/* D576: also send every log line to $S/ge007.log (next to ge007.ini),
 * because a normal (double-click) launch loses stderr. One fputs per line
 * keeps lines whole when several threads log at once (the CRT locks per
 * call); the file is flushed after every line (the game may crash and the
 * log must survive; this is not a per-pixel hot path). Capped at 8 MiB
 * total (debug env vars can make the log very chatty).
 * Plain C stdio + remove()/rename() only, so Windows (mingw) and Linux
 * both build this. */
#define SYS_LOG_FILE_CAP (8u * 1024u * 1024u)

static FILE  *g_logFile      = NULL;
static size_t g_logFileBytes = 0;
static int    g_logFileCapped = 0;
static int    g_logFileTried  = 0;

static void sysLogCloseFile(void)
{
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = NULL;
    }
}

/* Write one already-formatted line (NUL-terminated, ends in '\n') to the
 * log file, honouring the size cap. */
static void sysLogWriteFile(const char *line)
{
    if (!g_logFile || g_logFileCapped)
        return;
    size_t len = strlen(line);
    if (fwrite(line, 1, len, g_logFile) != len) {
        g_logFileCapped = 1; /* unwriteable: stop trying (no cap note) */
        return;
    }
    g_logFileBytes += len;
    if (g_logFileBytes >= SYS_LOG_FILE_CAP) {
        fputs("[NOTE ] log: size cap reached, further lines go to stderr only\n",
              g_logFile);
        g_logFileCapped = 1;
    }
    fflush(g_logFile);
}

void sysLogOpenFile(void)
{
    char logpath[1024], prevpath[1024];
    if (g_logFile || g_logFileTried)
        return; /* idempotent */
    g_logFileTried = 1;

    /* sysResolvePath returns one static buffer; copy each path out. */
    strncpy(logpath, sysResolvePath("$S/ge007.log"), sizeof(logpath) - 1);
    logpath[sizeof(logpath) - 1] = 0;
    strncpy(prevpath, sysResolvePath("$S/ge007.prev.log"), sizeof(prevpath) - 1);
    prevpath[sizeof(prevpath) - 1] = 0;

    /* Roll the previous run's log to ge007.prev.log (drop any older
     * one first); failures are ignored (std C has no portable stat). */
    remove(prevpath);
    rename(logpath, prevpath);
    g_logFile = fopen(logpath, "w");
    if (!g_logFile) {
        /* Carry on with stderr only; at most one warn (no error spam). */
        sysLogPrintf(LOG_WARNING, "log: cannot open %s, stderr only", logpath);
        return;
    }
    g_logFileBytes = 0;
    g_logFileCapped = 0;
    atexit(sysLogCloseFile);
}

void sysLogPrintf(enum LogLevel level, const char *fmt, ...)
{
    static const char *tags[] = { "ERROR", "WARN ", "NOTE ", "INFO ", "DEBUG" };
    char buf[1024];
    int off, n;
    va_list ap;

    if ((int)level >= 5) level = LOG_DEBUG;
    off = snprintf(buf, sizeof(buf), "[%s] ", tags[level]);
    if (off < 0) off = 0;
    va_start(ap, fmt);
    n = vsnprintf(buf + off, sizeof(buf) - (size_t)off - 1, fmt, ap);
    va_end(ap);
    if (n > 0)
        off += n;
    else
        off = (int)strlen(buf); /* message truncated: advance past what fit */
    if (off > (int)sizeof(buf) - 2)   /* vsnprintf returns the untruncated length */
        off = (int)sizeof(buf) - 2;   /* room for '\n' + NUL */
    buf[off++] = '\n';
    buf[off] = 0;

    fputs(buf, stderr);
    sysLogWriteFile(buf);
}

void sysFatalError(const char *fmt, ...)
{
    char buf[1024];
    int off, n;
    va_list ap;

    off = snprintf(buf, sizeof(buf), "[FATAL] ");
    if (off < 0) off = 0;
    va_start(ap, fmt);
    n = vsnprintf(buf + off, sizeof(buf) - (size_t)off - 1, fmt, ap);
    va_end(ap);
    if (n > 0)
        off += n;
    else
        off = (int)strlen(buf);
    if (off > (int)sizeof(buf) - 2)   /* vsnprintf returns the untruncated length */
        off = (int)sizeof(buf) - 2;   /* room for '\n' + NUL */
    buf[off++] = '\n';
    buf[off] = 0;

    fflush(stdout);
    fflush(stderr);
    fputs(buf, stderr);
    if (g_logFile) {   /* the fatal line bypasses the size cap: it is the one that matters */
        fputs(buf, g_logFile);
        fflush(g_logFile);
    }
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

/* D443: relaunch this executable with the original argv (same cwd -- a child
 * inherits it). Called ONLY from the atexit handler, i.e. after the D344
 * orderly quit has parked the render thread and the config/eeprom are saved.
 * Returns 0 on success. */
int sysRelaunchSelf(void)
{
#ifdef _WIN32
    char exe[MAX_PATH * 2];
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
    if (n == 0 || n >= sizeof(exe)) return -1;
    /* GetCommandLineA is the exact original (already-quoted) command line. */
    const char *cl = GetCommandLineA();
    if (!cl) return -1;
    char *cmd = _strdup(cl);
    if (!cmd) return -1;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    BOOL ok = CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    free(cmd);
    if (!ok) return -1;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
#else
    if (!g_argv || g_argc <= 0) return -1;
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    const char *path = g_argv[0];
    if (n > 0) { exe[n] = 0; path = exe; }
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execv(path, g_argv);
        _exit(127);
    }
    return 0;
#endif
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

const char *sysGetTokenString(void)
{
    /* The N64 build reads a "token string" of debug switches (-level_XX,
     * -hardN, -m..., -d, -s, -j) from a cartridge register via osPiReadIo.
     * On PC there is no such register, so synthesise the same string from
     * the host command line: argv[1..] joined with single spaces. The N64
     * buffer is 60 bytes (G_TOKEN_STRING_LEN words); keep well inside it. */
    static char buf[256];
    static int  built = 0;

    if (!built) {
        size_t n = 0;
        buf[0] = '\0';
        for (int i = 1; i < g_argc && g_argv[i]; ++i) {
            size_t len = strlen(g_argv[i]);
            if (n + len + 2 >= sizeof(buf))
                break;
            if (n)
                buf[n++] = ' ';
            memcpy(buf + n, g_argv[i], len);
            n += len;
            buf[n] = '\0';
        }
        built = 1;
    }
    return buf;
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
#else
    if (exeDir[0] == '.' && exeDir[1] == 0) {
        char d[512];
        getExeDir(d, sizeof(d));
        if (d[0])
            snprintf(exeDir, sizeof(exeDir), "%s", d);
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
        /* D256: mirror the Windows branch. The ROM itself is found via
         * $S/ OR $E/ (exe dir) by romdataInit, so a launch from any CWD
         * (Steam shortcut, terminal in ~) runs fine while saves/config —
         * which only knew the CWD-relative path — silently failed to write.
         * Prefer CWD data/ (dev/repo workflow), else exe-dir data/, and
         * create the directory best-effort so first-run writes don't fail. */
        if (access("data", F_OK) == 0)
            snprintf(out, sizeof(out), "data/%s", path + 3);
        else {
            static char sexedir[1024] = "";
            if (!sexedir[0]) {
                getExeDir(sexedir, sizeof(sexedir));
            }
            if (sexedir[0])
                snprintf(out, sizeof(out), "%s/data/%s", sexedir, path + 3);
            else
                snprintf(out, sizeof(out), "data/%s", path + 3);
        }
        {
            char dirbuf[1024];
            char *slash;
            strncpy(dirbuf, out, sizeof(dirbuf) - 1);
            dirbuf[sizeof(dirbuf) - 1] = 0;
            slash = strrchr(dirbuf, '/');
            if (slash && slash != dirbuf)
                *slash = 0, mkdir(dirbuf, 0755); /* best effort; EEXIST is fine */
        }
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
