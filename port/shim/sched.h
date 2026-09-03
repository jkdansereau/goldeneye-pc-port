/*
 * PC port shim for <sched.h>.
 *
 * The decomp ships src/sched.h (the N64 OSSc* scheduler client interface),
 * which is on the compiler include path. libstdc++'s <thread> / gthr headers
 * (pulled into the fast3d C++ TUs) do `#include <sched.h>` and need the HOST
 * header — cpu_set_t, sched_yield — so C++ TUs must be routed to the real
 * <sched.h> by absolute path (generated hostsched.h, same trick as
 * port/shim/string.h). C TUs keep resolving <sched.h> to the decomp's own
 * src/sched.h, exactly as on the console.
 *
 * Inert in the N64 build (port/shim is not on its include path).
 */
#if defined(__cplusplus)
#include "hostsched.h"
#else
#include_next <sched.h>
#endif
