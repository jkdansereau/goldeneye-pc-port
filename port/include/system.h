#ifndef PORT_SYSTEM_H
#define PORT_SYSTEM_H

/*
 * Platform primitives: time, logging, paths, exit.
 * Modelled on the PD port's port/include/system.h.
 */

#include <stdint.h>
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Time --------------------------------------------------------------- */

/* Monotonic microseconds since an arbitrary origin. */
uint64_t sysGetMicroseconds(void);

/*
 * Base address the executable was loaded at (for turning runtime addresses
 * into file offsets for addr2line; ASLR moves the image on Windows).
 */
uintptr_t sysImageBase(void);
/* Wall-clock seconds since the epoch (for timestamps). */
int64_t  sysGetTime(void);
/* Sleep for the given number of microseconds. */
void     sysSleep(uint32_t micros);

/* --- Logging ------------------------------------------------------------ */

void sysLogPrintf(enum LogLevel level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/*
 * Print a fatal error message and terminate the process. Used by fast3d for
 * unrecoverable conditions (bad Gfx command, OOM in the texture cache, ...).
 */
void sysFatalError(const char *fmt, ...)
    __attribute__((format(printf, 1, 2))) __attribute__((noreturn));

/* --- Paths -------------------------------------------------------------- */

/*
 * Resolve a path relative to the data dir. "$S/" prefixes the data dir,
 * "$E/" prefixes the executable dir. Returns a pointer to a static buffer.
 */
const char *sysResolvePath(const char *path);

/* Return the directory containing the executable. */
const char *sysGetExeDir(void);

/* --- Command line ------------------------------------------------------- */

/* Store the host argv (call once from main). */
void sysSetArgs(int argc, char **argv);
/* Nonzero if the given argument (e.g. "--debug-gl") was passed. */
int  sysArgCheck(const char *arg);
/* Value following the given argument on the command line, or NULL. */
const char *sysArgGetString(const char *arg);

/* --- CPU ---------------------------------------------------------------- */

/* Briefly yield/relax the CPU (busy-wait hint). Used by fast3d's frame sync. */
void sysCpuRelax(void);

/* --- Lifecycle ---------------------------------------------------------- */

void sysExit(int code);

/*
 * Start the cooperative thread kernel (green threads + vsync tick). Call
 * once from main() before running any game code; after this, blocking
 * osRecvMesg() calls in the game suspend/resume threads.
 */
void portKernelInit(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_H */
