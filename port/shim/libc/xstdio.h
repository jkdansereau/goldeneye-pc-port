/*
 * PC port shadow of src/libultra/libc/xstdio.h.
 *
 * xprintf.c (the IDO printf engine, compiled for the PC — see CMakeLists.txt)
 * DEFINES _Printf() with `char *` parameters:
 *     int _Printf(outfun prout, char *arg, const char *fmt, va_list args)
 * while this header DECLARES it with `u8 *`:
 *     int _Printf(outfun prout, u8 *arg, const u8 *fmt, va_list args);
 * IDO treats signed/unsigned-char pointers as compatible; GCC makes the
 * mismatch a hard error. So under PORT we rename the declaration before
 * pulling in the real header: the renamed prototype is harmless (it names a
 * function that is never defined or called), and xprintf.c's definition
 * becomes the sole _Printf prototype. The only caller, src/sprintf.c, does
 * not include this header at all — it calls _Printf with an implicit
 * declaration and passes its char*-based proutSprintf, which matches
 * xprintf.c's definition exactly.
 *
 * Inert in the N64 build: port/shim is not on its include path.
 */

#if defined(PORT)
#define _Printf _Printf_u8decl
#include_next <libc/xstdio.h>
#undef _Printf
#endif
