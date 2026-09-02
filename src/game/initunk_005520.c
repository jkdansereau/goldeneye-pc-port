#include <ultra64.h>
#include <memp.h>
#include "model.h"
#include "initunk_005520.h"
#include "objecthandler.h"
#include "memp.h"

#define MODEL_SPARE_SLOTS          30
#define ANIM_MODEL_SPARE_SLOTS     10

#ifdef PORT
/* PC port (D57, docs/dev/findings.md): two rwdata record structs contain
 * pointer fields — ModelRwData_HeadPlaceholderRecord (ModelFileHeader* +
 * void*) and ModelRwData_DisplayList_CollisionRecord (Vertex* + Gfx*) — so
 * they grow 8 -> 16 bytes on x86-64. modelCalculateRwDataLen() accumulates
 * sizeof(record)/4 per node, so every HEAD/DLCOLLISION node adds +2 words
 * vs N64, and the largest models (e.g. BODY_Brosnan_Tuxedo: 153 words on PC)
 * no longer fit the N64-sized spare pools below. Grow the spare capacities
 * with headroom over the measured max; modelmgrInstantiate*() additionally
 * falls back to a dynamic slot+pool (D57) if a model still exceeds them. */
#define MODEL_SPARE_RWDATALEN      0x38
#define ANIM_MODEL_SPARE_RWDATALEN 0xA8
#else
#define MODEL_SPARE_RWDATALEN      0x14
#define ANIM_MODEL_SPARE_RWDATALEN 0x8c
#endif


void modelmgrResetSlotCounts(void)
{
    g_MaxAnimModelSlots = 0;
    g_MaxModelSlots = 0;
}


void modelmgrSetLevelResetting(bool resetting) 
{
    g_ModelIsLvResetting = resetting;
}

//this may be a file split

/**
 * NTSC address 0x7F005540.
*/
void modelmgrAllocateModelSlots(s32 numobjs)
{
    s32 i;

    g_MaxModelSlots = numobjs + MODEL_SPARE_SLOTS;
    
    g_ModelSlots = mempAllocBytesInBank(g_MaxModelSlots * sizeof(struct ModelSlot), MEMPOOL_STAGE);

    for (i = 0; i < g_MaxModelSlots; i++)
    {
        g_ModelSlots[i].unk08 = 0;

        if (i < numobjs)
        {
            g_ModelSlots[i].unk10 = NULL;
        }
        else
        {
            g_ModelSlots[i].unk10 = mempAllocBytesInBank(MODEL_SPARE_RWDATALEN * sizeof(u32), MEMPOOL_STAGE);
            g_ModelSlots[i].unk02 = MODEL_SPARE_RWDATALEN;
        }
    }
}


/**
 * NTSC address 0x7F005540.
*/
void modelmgrAllocateAnimModelSlots(s32 numanimated)
{
    s32 temp_t6;
    s32 i;

    g_MaxAnimModelSlots = numanimated + ANIM_MODEL_SPARE_SLOTS;

    // mips2c says: g_AnimModelSlots = mempAllocBytesInBank(temp_t6 * 0xC0, 4);
    // however, the pointer is incremented by 0xbc in the loop below.
    g_AnimModelSlots = mempAllocBytesInBank(g_MaxAnimModelSlots * (4 + sizeof(struct AnimModelSlot)), MEMPOOL_STAGE);

    for (i = 0; i < g_MaxAnimModelSlots; i++)
    {
        g_AnimModelSlots[i].unk08 = 0;

        if (i < numanimated)
        {
            g_AnimModelSlots[i].unk10 = NULL;
        }
        else
        {
            g_AnimModelSlots[i].unk10 = mempAllocBytesInBank(ANIM_MODEL_SPARE_RWDATALEN * sizeof(u32), MEMPOOL_STAGE);
            g_AnimModelSlots[i].unk02 = ANIM_MODEL_SPARE_RWDATALEN;
        }
    }
}
