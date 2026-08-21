/*
 * PC port shim for <stdarg.h>.
 *
 * The N64 decomp's include/stdarg.h does `#include <ultra64.h>` (the console
 * dev-kit convenience: one header pulls in all of libultra). On the PC that
 * means any HOST header that internally includes <stdarg.h> (SDL_stdinc.h,
 * parts of MinGW's own headers, ...) drags PR/os.h into the middle of host
 * header parsing. If MinGW's <errno.h> has already run (it defines the
 * `errno` macro), PR/os.h's controller structs — which have fields literally
 * named `errno` — fail to parse ("field '_errno' declared as a function").
 *
 * Parse the N64 header with the errno macro suppressed; restore it after.
 * Inert in the N64 build (port/shim is not on its include path).
 */

#pragma push_macro("errno")
#undef errno
#include "include/stdarg.h"
#pragma pop_macro("errno")
