/*
 * gimgfixup.c - D68 (docs/PCPortResearch.md): Globalimagetable endianness
 * fixup for the PC port. See port/include/gimgfixup.h for the rationale.
 *
 * The offset tables below must stay in lockstep with the g_pc_gimg_off_*
 * values in src/game/image_bank.c (D39) and with assets/oddtextures.c, whose
 * linked layout defines the segment: 17 Gfx display lists followed by 32
 * sImageTableEntry arrays, total size 0x13F8.
 */

#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include "gimgfixup.h"

#define GIMG_REGION_SIZE 0x13F8u

/* Segment offsets of the 17 global display lists (globalDL_0xNNN). */
static const u32 s_dl_offs[] = {
    0x0, 0x78, 0x120, 0x1C8, 0x270, 0x318, 0x3C0, 0x468,
    0x510, 0x5B8, 0x660, 0x708, 0x7B0, 0x858, 0x900, 0x9A8, 0xA50
};

/* Segment offsets of the 32 sImageTableEntry arrays (12-byte entries). */
static const u32 s_tbl_offs[] = {
    0xAC8, 0xAD4, 0xBC4, 0xC0C, 0xC48, 0xC54, 0xC60, 0xC6C,
    0xC78, 0xC84, 0xC90, 0xC9C, 0xCA8, 0xCB4, 0xCC0, 0xCCC,
    0xCD8, 0xCE4, 0xCF0, 0xCFC, 0xD08, 0xD14, 0xD20, 0xD2C,
    0xD38, 0xD44, 0xD5C, 0xFB4, 0xFD8, 0x1020, 0x102C, 0x132C
};

#define N_DLS ((int)(sizeof(s_dl_offs) / sizeof(s_dl_offs[0])))
#define N_TBLS ((int)(sizeof(s_tbl_offs) / sizeof(s_tbl_offs[0])))

/* Compiled shadows of the ROM-copied display lists (assets/oddtextures.c).
 * Incomplete-type externs are fine: we only do pointer arithmetic into them. */
extern Gfx globalDL_0x000;
extern Gfx globalDL_0x078;
extern Gfx globalDL_0x120;
extern Gfx globalDL_0x1c8;
extern Gfx globalDL_0x270;
extern Gfx globalDL_0x318;
extern Gfx globalDL_0x3c0;
extern Gfx globalDL_0x468;
extern Gfx globalDL_0x510;
extern Gfx globalDL_0x5b8;
extern Gfx globalDL_0x660;
extern Gfx globalDL_0x708;
extern Gfx globalDL_0x7b0;
extern Gfx globalDL_0x858;
extern Gfx globalDL_0x900;
extern Gfx globalDL_0x9a8;
extern Gfx globalDL_0xa50;

static Gfx *const s_dl_syms[] = {
    &globalDL_0x000, &globalDL_0x078, &globalDL_0x120, &globalDL_0x1c8,
    &globalDL_0x270, &globalDL_0x318, &globalDL_0x3c0, &globalDL_0x468,
    &globalDL_0x510, &globalDL_0x5b8, &globalDL_0x660, &globalDL_0x708,
    &globalDL_0x7b0, &globalDL_0x858, &globalDL_0x900, &globalDL_0x9a8,
    &globalDL_0xa50
};

void gimgFixupGlobalimagetable(u8 *base)
{
    int i, j;
    u8 *end = base + GIMG_REGION_SIZE;

    /* 1) Gfx display lists: bswap the w1 word of every IMAGESEG-marked
     *    G_SETTIMG command. In the raw BE bytes the marker sits at
     *    offsets 4..5 (AB CD); after the swap it is at 6..7 (CD AB). */
    for (i = 0; i < N_DLS; i++)
    {
        u8 *p = base + s_dl_offs[i];

        while (p + 8 <= end && p[0] != (u8)G_ENDDL)
        {
            if (p[4] == 0xAB && p[5] == 0xCD)
            {
                u32 *w1 = (u32 *)(p + 4);
                *w1 = __builtin_bswap32(*w1);
            }
            p += 8;
        }
    }

    /* 2) sImageTableEntry arrays: bswap the index field (word 0 of each
     *    12-byte entry). Entry counts fall out of the D39 layout: each
     *    table runs up to the next symbol. */
    for (i = 0; i < N_TBLS; i++)
    {
        u32 start = s_tbl_offs[i];
        u32 stop = (i + 1 < N_TBLS) ? s_tbl_offs[i + 1] : GIMG_REGION_SIZE;
        u32 count = (stop - start) / 12u;

        for (j = 0; j < (int)count; j++)
        {
            u32 *idx = (u32 *)(base + start + (u32)j * 12u);
            *idx = __builtin_bswap32(*idx);
        }
    }
}

void gimgSyncCompiledGlobalDLs(u8 *base)
{
    int i;
    u8 *end = base + GIMG_REGION_SIZE;

    for (i = 0; i < N_DLS; i++)
    {
        u8 *p = base + s_dl_offs[i];
        Gfx *dst = s_dl_syms[i];
        int j = 0;

        while (p + 8 <= end && p[0] != (u8)G_ENDDL)
        {
            /* D68/C2: texLoadFromDisplayList() has already replaced every
             * IMAGESEG w1 word in the ROM copy with a real texture pointer,
             * so the 0xABCDxxxx marker is gone from `p`. Identify the slots
             * that need syncing from the COMPILED array instead: a G_SETTIMG
             * command whose w1 is still an unresolved IMAGESEG marker
             * (0xABCDxxxx). Copy the ROM copy's resolved word into it. */
            if ((u8)(dst[j].words.w0 >> 24) == (u8)G_SETTIMG &&
                (dst[j].words.w1 >> 16) == 0xABCDu)
            {
                dst[j].words.w1 = *(u32 *)(p + 4);
            }
            p += 8;
            j++;
        }
    }
}
