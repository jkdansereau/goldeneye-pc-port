/*
 * PC port shim for PR/gbi.h (see docs/PCPortResearch.md).
 *
 * The real header (include/PR/gbi.h) already has 64-bit-ready Gwords and
 * gSP* macros that pack full 64-bit pointers into words.w1. However, the
 * Gfx union's command members (Gdma, Gtri, Gloadtile, ...) are guarded by
 * `IS_BIG_ENDIAN && !IS_64_BIT`, which is false on a little-endian 64-bit
 * host, leaving Gfx with only the `words` member.
 *
 * Even if we fake those macros to expose the N64-layout members, they are
 * FUNCTIONALLY WRONG on a 64-bit little-endian host: the N64 bitfield
 * layouts read the wrong bytes. For example, the N64 Gdma puts `cmd` at
 * bits 0-7, but the gSP* macros pack `cmd` at bits 24-31, so `dma.cmd`
 * reads 0 instead of the command.
 *
 * This shim therefore:
 *   1. Includes the real header with faked IS_BIG_ENDIAN/IS_64_BIT so all
 *      the types and gSP* macros are available.
 *   2. Defines LE-correct versions of the members the game code actually
 *      reads: Gdma, Gtri/Tri, Gloadtile.
 *   3. Defines a Gfx_le union with `words` + the LE-correct members + the
 *      N64-layout members (for completeness).
 *   4. #defines Gfx to Gfx_le so all game code and gSP* macros use it.
 *
 * The port layer (Phase 2+) is responsible for placing/convert-
 * ing display-list data to match these layouts.
 *
 * Inert in the N64 build (no -DPORT): pure pass-through.
 */
#ifndef _PORT_SHIM_GBI_H_
#define _PORT_SHIM_GBI_H_
#if defined(PORT)
/*
 * The real header wraps Gwords and all command structs in
 * `#if defined(_LANGUAGE_C) || defined(_LANGUAGE_C_PLUS_PLUS)`. This GCC
 * does not define _LANGUAGE_C, so we define it here to pull in those types.
 */
#    ifndef _LANGUAGE_C
#        define _LANGUAGE_C 1
#    endif
#    undef IS_BIG_ENDIAN
#    undef IS_64_BIT
#    define IS_BIG_ENDIAN 1
#    define IS_64_BIT 0
#    include "include/PR/gbi.h"
#    undef IS_BIG_ENDIAN
#    undef IS_64_BIT
#    include <stdint.h>
#    define IS_BIG_ENDIAN (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#    define IS_64_BIT (UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFU)

/* ------------------------------------------------------------------ */
/* LE-correct command layouts for the members the game code reads.     */
/* ------------------------------------------------------------------ */

/*
 * Gdma: the gSP* macros pack w0 = (cmd << 24) | (len & 0xFFFFFF),
 * w1 = addr. On a 64-bit LE host, that means:
 *   bits 0-23   = len
 *   bits 24-31  = cmd
 *   bits 32-63  = 0 (unused)
 *   bits 64-127 = addr
 * So the LE-correct layout is:
 */
typedef struct {
    intptr_t  par : 24;   /* bits 0-23   = len (from packing) */
    intptr_t  cmd : 8;    /* bits 24-31  = cmd (from packing) */
    intptr_t  len : 32;   /* bits 32-63  = 0 (unused)         */
    uintptr_t addr;       /* bits 64-127 = addr (from packing)*/
} Gdma_le;

/*
 * Gtri/Tri: the gSP1Triangle macro packs w0 = (cmd << 24) |
 * (v0*2 << 16) | (v1*2 << 8) | (v2*2), w1 = 0. On a 64-bit LE host,
 * the vertex data is in bytes 0-2 (reversed order: v2, v1, v0).
 * The game code reads tri.tri.v[0..2], so we place the Tri struct at
 * bytes 0-3. The vertex order reversal is a port-layer concern.
 */
typedef struct {
    unsigned char v[3];   /* bytes 0-2 = vertex data (reversed on LE) */
    unsigned char flag;   /* byte 3  = cmd (from packing)             */
} Tri_le;

typedef struct {
    Tri_le          tri;  /* bytes 0-3 */
    unsigned char   rest[12];          /* bytes 4-15 */
} Gtri_le;

/*
 * Gloadtile: the gSPLoadTile macro packs w0 = (cmd << 24) |
 * (sl << 12) | tl | ... On a 64-bit LE host, that means:
 *   bits 0-11   = tl
 *   bits 12-23  = sl
 *   bits 24-31  = cmd
 *   bits 32-34  = tile
 *   bits 35-39  = pad
 *   bits 40-51  = sh
 *   bits 52-63  = th
 * So the LE-correct layout is:
 */
typedef struct {
    unsigned int tl   : 12;  /* bits 0-11  */
    unsigned int sl   : 12;  /* bits 12-23 */
    unsigned int cmd  : 8;   /* bits 24-31 */
    unsigned int tile : 3;   /* bits 32-34 */
    unsigned int pad  : 5;   /* bits 35-39 */
    unsigned int sh   : 12;  /* bits 40-51 */
    unsigned int th   : 12;  /* bits 52-63 */
} Gloadtile_le;

/* ------------------------------------------------------------------ */
/* LE-correct Gfx union.                                               */
/* ------------------------------------------------------------------ */
typedef union {
    Gwords        words;
    Gdma_le       dma;
    Gtri_le       tri;
    Gloadtile_le  loadtile;
    /* N64-layout members (not used by game code, kept for completeness) */
    Gline3D       line;
    Gpopmtx       popmtx;
    Gsegment      segment;
    GsetothermodeH setothermodeH;
    GsetothermodeL setothermodeL;
    Gtexture      texture;
    Gperspnorm    perspnorm;
    Gsetimg       setimg;
    Gsetcombine   setcombine;
    Gsetcolor     setcolor;
    Gfillrect     fillrect;
    Gsettile      settile;
    Gsettilesize  settilesize;
    Gloadtlut     loadtlut;
    long long int force_structure_alignment;
} Gfx_le;

#define Gfx Gfx_le

#endif /* defined(PORT) */
#endif /* _PORT_SHIM_GBI_H_ */
