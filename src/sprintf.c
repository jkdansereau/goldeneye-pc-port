#include <stdarg.h>
#include <ultra64.h>
/*#include <stddef.h>*/



char *proutSprintf(char *dst, const char *src, size_t count) {
    return (char *) memcpy((u8 *) dst, (u8 *) src, count) + count;
}

int sprintf(char *dst, const char *fmt, ...) {
    s32 written;
    va_list args;
    va_start(args, fmt);
#ifdef PORT
    /* xprintf.c's _Printf takes a va_list* under PORT — see the comment there
     * (x86-64 SysV va_list-is-an-array ABI). */
    written = _Printf(proutSprintf, dst, fmt, &args);
#else
    written = _Printf(proutSprintf, dst, fmt, args);
#endif
    if (written >= 0) {
        dst[written] = 0;
    }
    return written-1;
}




