/*
 * PC port shim for <stdarg.h>.
 *
 * The N64 decomp's include/stdarg.h does `#include <ultra64.h>` (the console
 * dev-kit convenience: one header pulls in all of libultra). On the PC that
 * means any HOST header that internally includes <stdarg.h> (SDL_stdinc.h,
 * parts of the CRT's own headers, glibc's <stdio.h>, ...) drags PR/os.h into
 * the middle of host header parsing.
 */

#if defined(_WIN32)
/*
 * MinGW: parse the N64 header with the errno macro suppressed (PR/os.h has
 * fields literally named `errno`); restore it after. MinGW's CRT headers do
 * not need __gnuc_va_list.
 */
#pragma push_macro("errno")
#undef errno
#include "include/stdarg.h"
#pragma pop_macro("errno")

#else
/*
 * Linux/macOS: do NOT pull <ultra64.h> into system-header parsing. glibc's
 * <stdio.h> needs both va_list and the __gnuc_va_list alias that GCC's real
 * <stdarg.h> would provide; the decomp header defines neither. Provide the
 * whole set straight from the always-available compiler builtins.
 */
#ifndef _PORT_SHIM_STDARG_H
#define _PORT_SHIM_STDARG_H
typedef __builtin_va_list va_list;
typedef __builtin_va_list __gnuc_va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_copy(dst, src)  __builtin_va_copy(dst, src)
#endif
#endif
