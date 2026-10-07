/*
 * net_plat.c -- platform layer for the netplay core (D409). See net_plat.h.
 */
#include "net_plat.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
/* BCryptGenRandom needs the Vista+ API surface (MinGW gates it). */
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>   /* timeBeginPeriod (winmm) */
#include <bcrypt.h>
#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winmm.lib")
#endif
#else
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#endif

/* ------------------------------------------------------------------------ */
/* Time                                                                      */
/* ------------------------------------------------------------------------ */

#if defined(_WIN32)
static LARGE_INTEGER s_qpcFreq;
static volatile LONG s_timeInit = 0;

static void netTimeInit(void)
{
    if (InterlockedCompareExchange(&s_timeInit, 1, 0) == 0) {
        QueryPerformanceFrequency(&s_qpcFreq);
        /* 1 ms scheduler granularity for Sleep()/select() while netplay runs.
         * SDL normally does this already; harmless to repeat. */
        timeBeginPeriod(1);
        InterlockedExchange(&s_timeInit, 2);
    }
    while (s_timeInit != 2) {
        Sleep(0);
    }
}

uint64_t netTimeUs(void)
{
    LARGE_INTEGER now;
    netTimeInit();
    QueryPerformanceCounter(&now);
    /* Split to avoid 64-bit overflow of now*1e6 on long uptimes. */
    return (uint64_t)(now.QuadPart / s_qpcFreq.QuadPart) * 1000000ull +
           (uint64_t)(now.QuadPart % s_qpcFreq.QuadPart) * 1000000ull / (uint64_t)s_qpcFreq.QuadPart;
}

void netSleepUs(uint32_t us)
{
    netTimeInit();
    if (us < 1000) {
        /* Sub-millisecond: yield, then spin briefly on the clock. */
        uint64_t end = netTimeUs() + us;
        Sleep(0);
        while (netTimeUs() < end) {
            YieldProcessor();
        }
        return;
    }
    Sleep((DWORD)(us / 1000));
}
#else
uint64_t netTimeUs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)(ts.tv_nsec / 1000);
}

void netSleepUs(uint32_t us)
{
    struct timespec ts;
    ts.tv_sec = us / 1000000u;
    ts.tv_nsec = (long)(us % 1000000u) * 1000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}
#endif

/* ------------------------------------------------------------------------ */
/* Random                                                                    */
/* ------------------------------------------------------------------------ */

static uint64_t s_fallbackState = 0;

static uint64_t fallbackNext(void)
{
    /* splitmix64 seeded from the clock -- only used if the OS RNG fails. */
    uint64_t z;
    if (s_fallbackState == 0) {
        s_fallbackState = netTimeUs() ^ 0x9E3779B97F4A7C15ull ^ (uint64_t)(uintptr_t)&s_fallbackState;
    }
    z = (s_fallbackState += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static uint64_t s_testSeed;   /* netRandomTestSeed: tools only */

void netRandomTestSeed(uint64_t seed)
{
    s_testSeed = seed;
}

void netRandomBytes(void *buf, int n)
{
    uint8_t *p = (uint8_t *)buf;
    int ok = 0;
    if (n <= 0) {
        return;
    }
    if (s_testSeed) {
        int i;
        for (i = 0; i < n; i++) {
            uint64_t z = (s_testSeed += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            p[i] = (uint8_t)((z ^ (z >> 31)) >> 32);
        }
        return;
    }
#if defined(_WIN32)
    ok = BCryptGenRandom(NULL, p, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    {
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd >= 0) {
            int got = 0;
            while (got < n) {
                ssize_t r = read(fd, p + got, (size_t)(n - got));
                if (r <= 0) {
                    if (r < 0 && errno == EINTR) continue;
                    break;
                }
                got += (int)r;
            }
            close(fd);
            ok = (got == n);
        }
    }
#endif
    if (!ok) {
        int i;
        for (i = 0; i < n; i++) {
            p[i] = (uint8_t)(fallbackNext() >> 32);
        }
    }
}

uint32_t netRandom32(void)
{
    uint32_t v;
    netRandomBytes(&v, sizeof(v));
    return v;
}

uint64_t netRandom64(void)
{
    uint64_t v;
    netRandomBytes(&v, sizeof(v));
    return v;
}

/* ------------------------------------------------------------------------ */
/* Mutex / thread                                                            */
/* ------------------------------------------------------------------------ */

#if defined(_WIN32)
struct NetMutex { CRITICAL_SECTION cs; };

NetMutex *netMutexCreate(void)
{
    NetMutex *m = (NetMutex *)calloc(1, sizeof(*m));
    if (m) InitializeCriticalSection(&m->cs);
    return m;
}
void netMutexDestroy(NetMutex *m)
{
    if (!m) return;
    DeleteCriticalSection(&m->cs);
    free(m);
}
void netMutexLock(NetMutex *m)   { EnterCriticalSection(&m->cs); }
void netMutexUnlock(NetMutex *m) { LeaveCriticalSection(&m->cs); }

struct NetThread { HANDLE h; void (*fn)(void *); void *arg; };

static DWORD WINAPI netThreadTramp(LPVOID p)
{
    NetThread *t = (NetThread *)p;
    t->fn(t->arg);
    return 0;
}

NetThread *netThreadStart(void (*fn)(void *), void *arg)
{
    NetThread *t = (NetThread *)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->fn = fn;
    t->arg = arg;
    t->h = CreateThread(NULL, 256 * 1024, netThreadTramp, t, 0, NULL);
    if (!t->h) {
        free(t);
        return NULL;
    }
    return t;
}

void netThreadJoin(NetThread *t)
{
    if (!t) return;
    WaitForSingleObject(t->h, INFINITE);
    CloseHandle(t->h);
    free(t);
}
#else
struct NetMutex { pthread_mutex_t mx; };

NetMutex *netMutexCreate(void)
{
    NetMutex *m = (NetMutex *)calloc(1, sizeof(*m));
    if (m) pthread_mutex_init(&m->mx, NULL);
    return m;
}
void netMutexDestroy(NetMutex *m)
{
    if (!m) return;
    pthread_mutex_destroy(&m->mx);
    free(m);
}
void netMutexLock(NetMutex *m)   { pthread_mutex_lock(&m->mx); }
void netMutexUnlock(NetMutex *m) { pthread_mutex_unlock(&m->mx); }

struct NetThread { pthread_t th; void (*fn)(void *); void *arg; };

static void *netThreadTramp(void *p)
{
    NetThread *t = (NetThread *)p;
    t->fn(t->arg);
    return NULL;
}

NetThread *netThreadStart(void (*fn)(void *), void *arg)
{
    NetThread *t = (NetThread *)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->fn = fn;
    t->arg = arg;
    if (pthread_create(&t->th, NULL, netThreadTramp, t) != 0) {
        free(t);
        return NULL;
    }
    return t;
}

void netThreadJoin(NetThread *t)
{
    if (!t) return;
    pthread_join(t->th, NULL);
    free(t);
}
#endif

/* ------------------------------------------------------------------------ */
/* Strings                                                                   */
/* ------------------------------------------------------------------------ */

void netStrCopy(char *dst, int n, const char *src)
{
    int i = 0;
    if (!dst || n <= 0) return;
    if (src) {
        for (; i < n - 1 && src[i]; i++) {
            dst[i] = src[i];
        }
    }
    dst[i] = 0;
}

int netStrFmt(char *dst, int n, const char *fmt, ...)
{
    va_list ap;
    int r;
    if (!dst || n <= 0) return 0;
    va_start(ap, fmt);
    r = vsnprintf(dst, (size_t)n, fmt, ap);
    va_end(ap);
    dst[n - 1] = 0;
    return r;
}
