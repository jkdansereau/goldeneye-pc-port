/*
 * pc_netorder.c - D38: host byte-order helpers (see docs/PCPortResearch.md).
 *
 * src/bondconstants.h implements ntohl()/ntohs() as function-like macros over
 * CharArrayTo16/32 ("rewrite these to use char array as system provided funcs
 * do not"). Those macros expand <winsock.h>'s own `u_long WSAAPI ntohl(u_long)`
 * declarations into garbage in any TU that parses both, so
 * port/shim/bondconstants.h neutralizes them on PC. This file provides the
 * real functions those calls now bind to (declared in pc_protos.h).
 *
 * On little-endian x86-64 a byte swap is exactly what CharArrayTo16/32
 * computed:
 *   CharArrayTo16(v,0) = v[1] | v[0]<<8        == ntohs(v) on LE
 *   CharArrayTo32(v,0) = v[1]<<16|v[2]<<8|v[3]|v[0]<<24 == ntohl(v) on LE
 * so game code (e.g. the ntohs() calls in src/game/chrai.c) keeps its
 * N64-correct semantics without touching winsock or linking ws2_32.
 */

#include <PR/ultratypes.h>

/* Signatures match winsock's (u_short/u_long) so TUs that also parse
 * <winsock.h> see one consistent declaration; game code only ever passes
 * 16/32-bit values, for which the byte swap below is exact. */
unsigned short ntohs(unsigned short v)
{
    return (unsigned short)((v >> 8) | (v << 8));
}

unsigned long ntohl(unsigned long v)
{
    return __builtin_bswap32((unsigned int)v);
}
