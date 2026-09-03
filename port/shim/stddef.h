/*
 * PC port shim for <stddef.h>.
 *
 * The N64 decomp ships an essentially empty include/stddef.h (just its
 * `_STDDEF_H_` guard). In C game code that is harmless. In C++ TUs (fast3d)
 * it shadows the host header, so libstdc++'s <cstddef> never sees the host's
 * max_align_t/size_t and fails to compile. Route C++ TUs to the host header
 * by absolute path (generated hoststddef.h — same trick as
 * port/shim/string.h / port/shim/stdlib.h).
 *
 * Inert in the N64 build (port/shim is not on its include path).
 */
#if defined(__cplusplus)
/* MinGW's <stdint.h> does:
 *     #define __need_wint_t
 *     #define __need_wchar_t
 *     #include <stddef.h>
 * and never undefs the __need_* macros. stddef.h then refuses to run its
 * main body (which defines max_align_t) for the rest of the TU, breaking
 * libstdc++'s <cstddef>. Clear the poison before pulling in the host header.
 */
#undef __need_wchar_t
#undef __need_size_t
#undef __need_ptrdiff_t
#undef __need_NULL
#undef __need_wint_t
#include "hoststddef.h"
#elif defined(_WIN32)
/*
 * C TUs on MinGW: keep the empty N64 include/stddef.h stub. MinGW's CRT
 * headers leak size_t/ptrdiff_t transitively, and its own <stddef.h> drags in
 * the `#define errno (*_errno())` CRT macro, which collides with PR/os.h's
 * `u8 errno;` struct field — so routing to the host header is not an option
 * here.
 */
#include "include/stddef.h"
#else
/*
 * C TUs on Linux/macOS: the N64 include/stddef.h stub defines nothing, glibc
 * only defines these in the compiler's shadowed builtin <stddef.h>, and the
 * MinGW hoststddef trick (bin/../include/stddef.h) does not exist on Linux.
 * Provide the standard types straight from the always-predefined GCC/Clang
 * builtins — no header, so nothing to collide with. C11 permits the benign
 * identical redefinition if a real <stddef.h> is ever reached another way.
 */
#ifndef _PORT_SHIM_STDDEF_TYPES
#define _PORT_SHIM_STDDEF_TYPES
typedef __SIZE_TYPE__    size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
/* SDL's headers (SDL_stdinc.h, SDL_hidapi.h) and glibc's <bits/wchar2.h>
 * reference wchar_t, which in C is a library typedef owned by the compiler's
 * shadowed <stddef.h>. Provide it from the builtin, guarded by the same
 * macro GCC/glibc use so a real <stddef.h> reached later is a no-op. */
#ifndef _WCHAR_T
#define _WCHAR_T
typedef __WCHAR_TYPE__ wchar_t;
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif
#ifndef offsetof
#define offsetof(t, m) __builtin_offsetof(t, m)
#endif
#endif
#endif
