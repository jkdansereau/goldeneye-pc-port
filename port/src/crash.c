/*
 * Crash handler / stack traces.
 *
 * Catches signals (SIGSEGV etc.) and prints a backtrace + the game's state,
 * to make the inevitable porting crashes debuggable.
 *
 * Modelled on the PD port's port/src/crash.c (~360 lines).
 *
 * STATUS: scaffolding stub — implement during Phase 1.
 */

#include <stdio.h>
#include <stdlib.h>

#include "platform.h"
#include "system.h"

void crashInit(void)
{
    /* TODO(Phase 1):
     *  - install signal handlers (SIGSEGV, SIGFPE, SIGILL, SIGBUS)
     *  - on crash: print backtrace (dbghelp on Windows, backtrace() on POSIX)
     *  - dump relevant game state (current level, frame, etc.)
     */
    sysLogPrintf(LOG_INFO, "crashInit: TODO (Phase 1)");
}
