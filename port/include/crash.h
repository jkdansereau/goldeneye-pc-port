#ifndef PORT_CRASH_H
#define PORT_CRASH_H

/*
 * Crash handler: installs an unhandled-exception filter (Windows) or signal
 * handlers (POSIX) that print a symbolicated backtrace and write
 * ge007.crash.log, then abort. Call crashInit() before running game code.
 */

#ifdef __cplusplus
extern "C" {
#endif

void crashInit(void);
void crashShutdown(void);

/* Unwind every thread in the process and log a short symbolicated
 * backtrace for each (labelled via tids/names when known). For diagnosing
 * hangs: call from the kernel heartbeat. */
void crashDumpThreads(const unsigned long *tids, const char **names, int count);

#ifdef __cplusplus
}
#endif

#endif /* PORT_CRASH_H */
