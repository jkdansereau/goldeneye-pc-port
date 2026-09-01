/*
 * PC port shim for include/string.h (see docs/internals.md §11 D12).
 *
 * The N64 header declares:
 *
 *     extern size_t strlen(const unsigned char *);
 *     extern unsigned char *strchr(const unsigned char *, int);
 *
 * GCC treats `const unsigned char *` and `const char *` as distinct types
 * (even with -funsigned-char), so these conflict with the host declarations
 * that SDL2/SDL_stdinc.h and intrin.h pull in via their own
 * `#include <string.h>`. On the port the host libc provides memcpy/strlen/
 * strchr, and every game call site passes `u8 *` (== `unsigned char *`),
 * which compiles against the host prototypes with at most a pointer-type
 * warning. So the shim simply maps `<string.h>` to the HOST header.
 *
 * The host header is included by absolute path through the generated
 * hoststring.h (CMake derives it from the compiler location, since this
 * file sits earlier on the include path than the toolchain's own string.h
 * and #include_next would find the N64 header first).
 *
 * Inert in the N64 build (port/shim is not on its include path).
 */
#include "hoststring.h"
