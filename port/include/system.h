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
/* Wall-clock seconds since the epoch (for timestamps). */
int64_t  sysGetTime(void);
/* Sleep for the given number of microseconds. */
void     sysSleep(uint32_t micros);

/* --- Logging ------------------------------------------------------------ */

void sysLogPrintf(enum LogLevel level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* --- Paths -------------------------------------------------------------- */

/*
 * Resolve a path relative to the data dir. "$S/" prefixes the data dir,
 * "$E/" prefixes the executable dir. Returns a pointer to a static buffer.
 */
const char *sysResolvePath(const char *path);

/* Return the directory containing the executable. */
const char *sysGetExeDir(void);

/* --- Lifecycle ---------------------------------------------------------- */

void sysExit(int code);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_H */
