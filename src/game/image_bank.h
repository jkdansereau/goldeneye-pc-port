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
#ifdef PORT
extern uintptr_t globalbank_rdram_offset;
#else
extern s32 globalbank_rdram_offset;
#endif

void texReset(void);
u32 texReadBits(s32 bitCount);
#ifdef PORT
/* D588: full pointer width. Callers pass a LIVE host pointer (the
 * compressed-source cursor: texLoad's stack compbuffer, or the DRAM
 * mempool cursor from rzipGetSomething()); the s32 parameter truncated
 * it and the #95 portN64ToHost() re-base then mis-mapped native
 * (out-of-window) pointers whose low 32 bits fall in the D131
 * image-relative range [0x40000000, 0x70000000). */
void texSetBitstring(u8 *pos);
#else
void texSetBitstring(s32 pos);
#endif

#endif
