/*
 * PC port shim for <stdlib.h>.
 *
 * The N64 decomp ships include/stdlib.h (a libc stub: lldiv_t/ldiv_t typedefs
 * + a couple of prototypes). C game code gets it, exactly as on the console.
 *
 * In C++ TUs (fast3d) it must NOT be used: MinGW's own headers pull in the
 * host <stdlib.h> through their include chains (sec_api/stdlib_s.h does
 * `#include <stdlib.h>`, which resolves to the N64 header via -I), and the
 * lldiv_t/ldiv_t typedefs then conflict with the host's. The shim routes C++
 * TUs to the host header by absolute path (generated hoststdlib.h, same
 * trick as port/shim/string.h — see docs/PCPortResearch.md §11 D12).
 *
 * Inert in the N64 build (port/shim is not on its include path).
 */
#if defined(__cplusplus)
#include "hoststdlib.h"
#else
#include "include/stdlib.h"
#endif
