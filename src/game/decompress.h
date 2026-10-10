#ifndef _DECOMPRESS_H_
#define _DECOMPRESS_H_
#include <ultra64.h>
#include <inflate/inflate.h>


u32 decompressdata(u8 *src, u8 *dst, struct huft *hlist);
#ifdef PORT
/* D588: full pointer width — returns the live DRAM-window input cursor
 * (s_rz_nextin in port/src/rzdecomp.c); callers feed it to
 * texSetBitstring(), which takes pointer width. */
u8 *rzipGetSomething(void);
#else
s32 rzipGetSomething(void);
#endif

#endif
