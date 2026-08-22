#include <ultra64.h>
#include <ramrom.h>
#include <memp.h>
#include "image_bank.h"

// bss
//8008D0A0
u8* img_curpos;
//8008D0A4
u32 img_curdatatable;
//8008D0A8
s32 img_bitcount;
//8008D0AC
s32 dword_CODE_bss_8008D0AC;
//8008D0B0;
s32 globalbank_rdram_offset;
//8008D0B4;
s32 *pGlobalimagetable;
//8008D0B8;
struct sImageTableEntry *genericimage;
//8008D0BC
struct sImageTableEntry *impactimages;
//8008D0C0
struct sImageTableEntry *explosion_smokeimages;
//8008D0C4
struct sImageTableEntry *scattered_explosions;
//8008D0C8
struct sImageTableEntry *flareimage1;
//8008D0CC
struct sImageTableEntry *flareimage2;
//8008D0D0
struct sImageTableEntry *flareimage3;
//8008D0D4
struct sImageTableEntry *flareimage4;
//8008D0D8
struct sImageTableEntry *flareimage5;
//8008D0DC
struct sImageTableEntry *ammo9mmimage;
//8008D0E0
struct sImageTableEntry *rifleammoimage;
//8008D0E4
struct sImageTableEntry *shotgunammoimage;
//8008D0E8
struct sImageTableEntry *knifeammoimage;
//8008D0EC
struct sImageTableEntry *glaunchammoimage;
//8008D0F0
struct sImageTableEntry *rocketammoimage;
//8008D0F4
struct sImageTableEntry *genericmineammoimage;
//8008D0F8
struct sImageTableEntry *grenadeammoimage;
//8008D0FC
struct sImageTableEntry *magnumammoimage;
//8008D100
struct sImageTableEntry *goldengunammoimage;
//8008D104
struct sImageTableEntry *remotemineammoimage;
//8008D108
struct sImageTableEntry *timedmineammoimage;
//8008D10C
struct sImageTableEntry *proxmineammoimage;
//8008D110
struct sImageTableEntry *tankammoimage;
//8008D114;
struct sImageTableEntry *crosshairimage;
//8008D118
struct sImageTableEntry *betacrosshairimage;
//8008D11C
struct sImageTableEntry *glassoverlayimage;
//8008D120
struct sImageTableEntry *monitorimages;
//8008D124
struct sImageTableEntry *skywaterimages;
//8008D128
struct sImageTableEntry *mainfolderimages;
//8008D12C
struct sImageTableEntry *mpradarimages;
//8008D130
struct sImageTableEntry *mpcharselimages;
//8008D134
struct sImageTableEntry *mpstageselimages;


extern u8* _GlobalimagetableSegmentRomStart;


void texSetBitstring(s32 pos) {
    img_curpos = pos;
    img_curdatatable = 0;
    img_bitcount = 0;
}



u32 texReadBits(s32 bitCount)
{
    if (img_bitcount < bitCount)
    {
        do
        {
            img_curdatatable = (*img_curpos | (img_curdatatable << 8));
            img_curpos++;
            img_bitcount = img_bitcount + 8;
        } while (img_bitcount < bitCount);
    }
    
    img_bitcount -= bitCount;
    return (img_curdatatable >> img_bitcount) & ((1 << bitCount) - 1);
}



extern u32* _GlobalimagetableSegmentStart;
extern u32* _GlobalimagetableSegmentEnd;
extern void* s_genericimage;
extern void* s_impactimages;
extern void* s_explosion_smokeimages;
extern void* s_scattered_explosions;
extern void* s_flareimage1;
extern void* s_flareimage2;
extern void* s_flareimage3;
extern void* s_flareimage4;
extern void* s_flareimage5;
extern void* s_ammo9mmimage;
extern void* s_rifleammoimage;
extern void* s_shotgunammoimage;
extern void* s_knifeammoimage;
extern void* s_glammoimage;
extern void* s_rocketammoimage;
extern void* s_genericmineammoimage;
extern void* s_grenadeammoimage;
extern void* s_magnumammoimage;
extern void* s_goldengunammoimage;
extern void* s_remotemineammoimage;
extern void* s_timedmineammoimage;
extern void* s_proxmineammoimage;
extern void* s_tankammoimage;
extern void* s_crosshairimage;
extern void* s_betacrosshairimage;
extern void* s_glassoverlayimage;
extern void* s_monitorimages;
extern void* s_skywaterimages;
extern void* s_mainfolderimages;
extern void* s_mpradarimages;
extern void* s_mpcharselimages;
extern void* s_mpstageselimages;

extern Gfx* globalDL_0x000;
extern Gfx* globalDL_0x078;
extern Gfx* globalDL_0x120;
extern Gfx* globalDL_0x1c8;
extern Gfx* globalDL_0x270;
extern Gfx* globalDL_0x318;
extern Gfx* globalDL_0x3c0;
extern Gfx* globalDL_0x468;
extern Gfx* globalDL_0x510;
extern Gfx* globalDL_0x5b8;
extern Gfx* globalDL_0x660;
extern Gfx* globalDL_0x708;
extern Gfx* globalDL_0x7b0;
extern Gfx* globalDL_0x858;
extern Gfx* globalDL_0x900;
extern Gfx* globalDL_0x9a8;
extern Gfx* globalDL_0xa50;

#if defined(__x86_64__)
/* D39 (docs/PCPortResearch.md): on N64 all 49 symbols above link inside the
 * Globalimagetable segment at physical 0x02xxxxxx (ge007.ld), so
 * `globalbank_rdram_offset + (u32)&sym` rebases each onto pGlobalimagetable.
 * A PE image cannot sit at 0x02xxxxxx, so (u32)&sym is the low 32 bits of an
 * exe address — garbage. Use each symbol's offset within the segment instead
 * (verified by byte-tiling all 49 arrays against ROM [0x1029D160, 0x1029E558),
 * NTSC; offsets are region-independent). pGlobalimagetable is s32-safe (V1
 * @ 0x70xxxxxx, see port/src/dram.c), so the same u32 math yields the correct
 * 64-bit pointer. NOTE: this also requires _GlobalimagetableSegmentEnd to span
 * the full linked .data (Gfx DLs + sImageTableEntry tables = 0x13F8), not just
 * the 0xAC8-byte CSV asset — see port/src/romassets_u.s. */
enum {
    g_pc_gimg_off_s_genericimage = 0xAC8,
    g_pc_gimg_off_s_impactimages = 0xAD4,
    g_pc_gimg_off_s_explosion_smokeimages = 0xBC4,
    g_pc_gimg_off_s_scattered_explosions = 0xC0C,
    g_pc_gimg_off_s_flareimage1 = 0xC48,
    g_pc_gimg_off_s_flareimage2 = 0xC54,
    g_pc_gimg_off_s_flareimage3 = 0xC60,
    g_pc_gimg_off_s_flareimage4 = 0xC6C,
    g_pc_gimg_off_s_flareimage5 = 0xC78,
    g_pc_gimg_off_s_ammo9mmimage = 0xC84,
    g_pc_gimg_off_s_rifleammoimage = 0xC90,
    g_pc_gimg_off_s_shotgunammoimage = 0xC9C,
    g_pc_gimg_off_s_knifeammoimage = 0xCA8,
    g_pc_gimg_off_s_glammoimage = 0xCB4,
    g_pc_gimg_off_s_rocketammoimage = 0xCC0,
    g_pc_gimg_off_s_genericmineammoimage = 0xCCC,
    g_pc_gimg_off_s_grenadeammoimage = 0xCD8,
    g_pc_gimg_off_s_magnumammoimage = 0xCE4,
    g_pc_gimg_off_s_goldengunammoimage = 0xCF0,
    g_pc_gimg_off_s_remotemineammoimage = 0xCFC,
    g_pc_gimg_off_s_timedmineammoimage = 0xD08,
    g_pc_gimg_off_s_proxmineammoimage = 0xD14,
    g_pc_gimg_off_s_tankammoimage = 0xD20,
    g_pc_gimg_off_s_crosshairimage = 0xD2C,
    g_pc_gimg_off_s_betacrosshairimage = 0xD38,
    g_pc_gimg_off_s_glassoverlayimage = 0xD44,
    g_pc_gimg_off_s_monitorimages = 0xD5C,
    g_pc_gimg_off_s_skywaterimages = 0xFB4,
    g_pc_gimg_off_s_mainfolderimages = 0xFD8,
    g_pc_gimg_off_s_mpradarimages = 0x1020,
    g_pc_gimg_off_s_mpcharselimages = 0x102C,
    g_pc_gimg_off_s_mpstageselimages = 0x132C,
    g_pc_gimg_off_globalDL_0x000 = 0x0,
    g_pc_gimg_off_globalDL_0x078 = 0x78,
    g_pc_gimg_off_globalDL_0x120 = 0x120,
    g_pc_gimg_off_globalDL_0x1c8 = 0x1C8,
    g_pc_gimg_off_globalDL_0x270 = 0x270,
    g_pc_gimg_off_globalDL_0x318 = 0x318,
    g_pc_gimg_off_globalDL_0x3c0 = 0x3C0,
    g_pc_gimg_off_globalDL_0x468 = 0x468,
    g_pc_gimg_off_globalDL_0x510 = 0x510,
    g_pc_gimg_off_globalDL_0x5b8 = 0x5B8,
    g_pc_gimg_off_globalDL_0x660 = 0x660,
    g_pc_gimg_off_globalDL_0x708 = 0x708,
    g_pc_gimg_off_globalDL_0x7b0 = 0x7B0,
    g_pc_gimg_off_globalDL_0x858 = 0x858,
    g_pc_gimg_off_globalDL_0x900 = 0x900,
    g_pc_gimg_off_globalDL_0x9a8 = 0x9A8,
    g_pc_gimg_off_globalDL_0xa50 = 0xA50
};
#define GIMG_OFF(sym) (0x02000000u + g_pc_gimg_off_##sym) /* == N64 (u32)&sym */
#else
#define GIMG_OFF(sym) ((u32)&(sym))
#endif

void texReset(void)
{
    u32 size;
    s32 i;

    size = (u32)&_GlobalimagetableSegmentEnd - (u32)&_GlobalimagetableSegmentStart;
    pGlobalimagetable = mempAllocBytesInBank(size + 0x1000, MEMPOOL_STAGE);
    pGlobalimagetable = ((u32)pGlobalimagetable + 0xFFFU) & 0xFFFFF000;

    romCopy(pGlobalimagetable, &_GlobalimagetableSegmentRomStart, size);

    globalbank_rdram_offset = (u32)pGlobalimagetable + 0xFE000000;
    genericimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_genericimage));
    impactimages = (void *) (globalbank_rdram_offset + GIMG_OFF(s_impactimages));
    explosion_smokeimages = (void *) (globalbank_rdram_offset + GIMG_OFF(s_explosion_smokeimages));
    scattered_explosions = (void *) (globalbank_rdram_offset + GIMG_OFF(s_scattered_explosions));
    flareimage1 = (void *) (globalbank_rdram_offset + GIMG_OFF(s_flareimage1));
    flareimage2 = (void *) (globalbank_rdram_offset + GIMG_OFF(s_flareimage2));
    flareimage3 = (void *) (globalbank_rdram_offset + GIMG_OFF(s_flareimage3));
    flareimage4 = (void *) (globalbank_rdram_offset + GIMG_OFF(s_flareimage4));
    flareimage5 = (void *) (globalbank_rdram_offset + GIMG_OFF(s_flareimage5));
    ammo9mmimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_ammo9mmimage));
    rifleammoimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_rifleammoimage));
    shotgunammoimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_shotgunammoimage));
    knifeammoimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_knifeammoimage));
    glaunchammoimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_glammoimage));
    rocketammoimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_rocketammoimage));
    genericmineammoimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_genericmineammoimage));
    grenadeammoimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_grenadeammoimage));
    magnumammoimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_magnumammoimage));
    goldengunammoimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_goldengunammoimage));
    remotemineammoimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_remotemineammoimage));
    timedmineammoimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_timedmineammoimage));
    proxmineammoimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_proxmineammoimage));
    tankammoimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_tankammoimage));
    crosshairimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_crosshairimage));
    betacrosshairimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_betacrosshairimage));
    glassoverlayimage = (void *) (globalbank_rdram_offset + GIMG_OFF(s_glassoverlayimage));
    monitorimages = (void *) (globalbank_rdram_offset + GIMG_OFF(s_monitorimages));
    skywaterimages = (void *) (globalbank_rdram_offset + GIMG_OFF(s_skywaterimages));
    mainfolderimages = (void *) (globalbank_rdram_offset + GIMG_OFF(s_mainfolderimages));
    mpradarimages = (void *) (globalbank_rdram_offset + GIMG_OFF(s_mpradarimages));
    mpcharselimages = (void *) (globalbank_rdram_offset + GIMG_OFF(s_mpcharselimages));
    mpstageselimages = (void *) (globalbank_rdram_offset + GIMG_OFF(s_mpstageselimages));

    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0x000), 0);
    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0x078), 0);
    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0x120), 0);
    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0x1c8), 0);
    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0x270), 0);
    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0x318), 0);
    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0x3c0), 0);
    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0x468), 0);
    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0x510), 0);
    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0x5b8), 0);
    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0x660), 0);
    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0x708), 0);
    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0x7b0), 0);
    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0x858), 0);
    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0x900), 0);
    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0x9a8), 0);
    texLoadFromDisplayList(globalbank_rdram_offset + GIMG_OFF(globalDL_0xa50), 0);

    texLoad(genericimage, 0);

    for (i=0; i < 6; i++)
    {
        texLoad(&explosion_smokeimages[i], 0);
    }

    for (i=0; i < 5; i++)
    {
        texLoad(&scattered_explosions[i], 0);
    }
}
