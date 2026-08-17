/*
 * PRNG — ported verbatim from src/random.s (MIPS assembly, not compiled for
 * the PC). This is a REAL implementation, not a stub: the game uses it for
 * genuine gameplay logic, so a no-op would silently corrupt behaviour.
 *
 *   - randomGetNextFrom() feeds the CRC in src/game/crc.c
 *   - g_randomSeed is persisted in replay state (src/game/ramromreplay.c)
 *   - randomGetNext() drives RANDOMFRAC()/RANDOMGETNEXT_F32() (bondconstants.h)
 *
 * The assembly operates on the low 32 bits of the seed (the high 32 bits are
 * never read), but the seed is stored as a full u64. We mirror each
 * instruction with explicit masks to guarantee bit-exactness. The initial
 * seed is the two .words in random.s: 0xAB8D9F77 (hi) / 0x81280783 (lo).
 */

#include <ultra64.h>
#include <random.h>

u64 g_randomSeed = 0xAB8D9F7781280783ULL;

/*
 * MIPS64 shift helpers, matching the .s instructions exactly:
 *   dsll32 rd,rs,n  ->  (rs << n) & 0xFFFFFFFF   (low 32 bits kept, hi zeroed)
 *   dsrl32 rd,rs,n  ->  (rs >> n) & 0xFFFFFFFF   (low 32 bits kept, hi zeroed)
 * Plain `<<`/`>>` on u64 match dsll/dsrl (full 64-bit shifts).
 */
static inline u64 dsll32(u64 x, int n) { return (x << n) & 0xFFFFFFFFULL; }
static inline u64 dsrl32(u64 x, int n) { return (x >> n) & 0xFFFFFFFFULL; }

/*
 * Advance the global seed and return the new low 32 bits.
 * Mirrors randomGetNext in random.s line-by-line.
 */
u32 randomGetNext(void)
{
    u64 a0 = g_randomSeed;
    u64 a1, a2;

    a2 = dsll32(a0, 0x1f);   /* dsll32 $a2, $a0, 0x1f */
    a1 = a0 << 0x1f;         /* dsll   $a1, $a0, 0x1f */
    a2 = a2 >> 0x1f;         /* dsrl   $a2, $a2, 0x1f */
    a1 = dsrl32(a1, 0);      /* dsrl32 $a1, $a1, 0    */
    a0 = dsll32(a0, 0xc);    /* dsll32 $a0, $a0, 0xc  */
    a2 = a2 | a1;            /* or     $a2, $a2, $a1  */
    a0 = dsrl32(a0, 0);      /* dsrl32 $a0, $a0, 0    */
    a2 = a2 ^ a0;            /* xor    $a2, $a2, $a0  */
    a0 = a2 >> 0x14;         /* dsrl   $a0, $a2, 0x14 */
    a0 = a0 & 0xfff;         /* andi   $a0, $a0, 0xfff*/
    a0 = a0 ^ a2;            /* xor    $a0, $a0, $a2  */

    g_randomSeed = a0;       /* sd     $a0, g_randomSeed */
    return (u32)a0;          /* v0 = a0 & 0xFFFFFFFF    */
}

/* Set the seed (the .s adds 1 before storing). */
void randomSetSeed(u32 seed)
{
    g_randomSeed = (u64)seed + 1;   /* daddiu $a0,$a0,1; sd $a0, g_randomSeed */
}

/*
 * Same transform as randomGetNext, but applied to *seed (a caller-owned u64)
 * instead of the global. Mirrors randomGetNextFrom in random.s.
 */
u32 randomGetNextFrom(u64 *seed)
{
    u64 a3 = *seed;          /* ld     $a3, ($a0) */
    u64 a1, a2;

    a2 = dsll32(a3, 0x1f);   /* dsll32 $a2, $a3, 0x1f */
    a1 = a3 << 0x1f;         /* dsll   $a1, $a3, 0x1f */
    a2 = a2 >> 0x1f;         /* dsrl   $a2, $a2, 0x1f */
    a1 = dsrl32(a1, 0);      /* dsrl32 $a1, $a1, 0    */
    a3 = dsll32(a3, 0xc);    /* dsll32 $a3, $a3, 0xc  */
    a2 = a2 | a1;            /* or     $a2, $a2, $a1  */
    a3 = dsrl32(a3, 0);      /* dsrl32 $a3, $a3, 0    */
    a2 = a2 ^ a3;            /* xor    $a2, $a2, $a3  */
    a3 = a2 >> 0x14;         /* dsrl   $a3, $a2, 0x14 */
    a3 = a3 & 0xfff;         /* andi   $a3, $a3, 0xfff*/
    a3 = a3 ^ a2;            /* xor    $a3, $a3, $a2  */

    *seed = a3;              /* sd     $a3, ($a0) */
    return (u32)a3;          /* v0 = a3 & 0xFFFFFFFF */
}
