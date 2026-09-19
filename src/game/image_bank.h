#ifndef _IMAGE_BANK_H_
#define _IMAGE_BANK_H_
#include <ultra64.h>
#include <bondtypes.h>
#include "bondview.h"

extern struct sImageTableEntry *crosshairimage;

extern struct sImageTableEntry *mainfolderimages;
extern struct sImageTableEntry *mpstageselimages;
extern struct sImageTableEntry *genericimage;
extern struct sImageTableEntry *skywaterimages;
extern struct sImageTableEntry *monitorimages;
extern struct sImageTableEntry *mpcharselimages;
extern struct sImageTableEntry *mpradarimages;
extern struct sImageTableEntry *impactimages;
extern struct sImageTableEntry *explosion_smokeimages;
extern struct sImageTableEntry *scattered_explosions;
extern struct sImageTableEntry *flareimage2;
extern struct sImageTableEntry *glassoverlayimage;
extern struct sImageTableEntry *flareimage3;
extern struct sImageTableEntry *flareimage4;
extern struct sImageTableEntry *flareimage5;

extern u8* img_curpos;
extern s32 img_bitcount;
extern s32 *pGlobalimagetable;
#if defined(PORT)
/* D298/M2: holds a host-based RDRAM offset (pGlobalimagetable - 0x02000000)
 * so `globalbank_rdram_offset + GIMG_OFF(sym)` is a live host pointer in the
 * DRAM window. s32 on N64 (see image_bank.c). */
extern uintptr_t globalbank_rdram_offset;
#else
extern s32 globalbank_rdram_offset;
#endif

void texReset(void);
u32 texReadBits(s32 bitCount);
void texSetBitstring(s32 pos);

#endif
