/*
 * net_plat.h -- tiny platform layer for the netplay core (D413).
 *
 * The netplay core (port/net) is plain C99 with no game dependencies so the
 * same sources build into the game, the standalone matchmaking server and the
 * selftest, with GCC (MinGW / Linux) or MSVC. Everything OS-specific lives
 * behind this header: a monotonic clock, sleeping, a mutex, a thread, and an
 * OS-backed random source for nonces / ids / match seeds.
 */
#ifndef GE_NET_PLAT_H
#define GE_NET_PLAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Monotonic microseconds (arbitrary epoch). */
uint64_t netTimeUs(void);

/* Sleep at least `us` microseconds (best effort; ~1 ms granularity). */
void netSleepUs(uint32_t us);

/* Fill `buf` with OS-provided random bytes (falls back to a time-seeded
 * xorshift if the OS source fails). Not used for anything security-critical:
 * ids, nonces, lobby codes and the match PRNG seeds. */
void netRandomBytes(void *buf, int n);
uint32_t netRandom32(void);
uint64_t netRandom64(void);
/* Tests only (D416 netfuzz): a nonzero seed makes every "random" value --
 * nonces, connection ids, lobby codes, match seeds -- repeatable, so a
 * failing fuzz run can be replayed. The game never calls it. */
void netRandomTestSeed(uint64_t seed);

typedef struct NetMutex NetMutex;
NetMutex *netMutexCreate(void);
void netMutexDestroy(NetMutex *m);
void netMutexLock(NetMutex *m);
void netMutexUnlock(NetMutex *m);

typedef struct NetThread NetThread;
NetThread *netThreadStart(void (*fn)(void *), void *arg);
void netThreadJoin(NetThread *t);

/* Bounded string helpers (always NUL-terminate when n > 0). */
void netStrCopy(char *dst, int n, const char *src);
int netStrFmt(char *dst, int n, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* GE_NET_PLAT_H */
