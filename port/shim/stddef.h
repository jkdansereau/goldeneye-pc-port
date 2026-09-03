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
#else
/*
 * C TUs: the N64 include/stddef.h is an empty stub (its active body defines
 * neither size_t nor ptrdiff_t nor NULL). MinGW leaked those in transitively
 * through its CRT headers, so the stub was harmless there; glibc/GCC defines
 * them ONLY in the compiler's own <stddef.h>, so a C TU on Linux that relies
 * on <stddef.h> (e.g. include/PR/ultratypes.h's non-N64 path) fails to
 * compile. Route C TUs to the host header too — purely additive, the active
 * N64 stub typedefs nothing that could clash.
 */
#include "hoststddef.h"
#endif
