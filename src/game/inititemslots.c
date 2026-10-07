#include <ultra64.h>
#include <memp.h>
#include "player.h"
#include "bondinv.h"
#include "inititemslots.h"

void reinit_gunheld_totaltime(void) {
    s32 i;
  
    g_CurrentPlayer->equipallguns = FALSE;
    
    for (i = 0; i != 10; i++) {
        g_CurrentPlayer->gunheldarr[i].totaltime = -1;
    }
}

void alloc_additional_item_slots(s32 additionalentries) {
  g_CurrentPlayer->equipmaxitems = additionalentries + 0x1e;
#ifdef PORT
    /* D458b: InvItem is 32 bytes on PC (pointer-bearing union and next/prev widened) but the
     * N64 stride is 0x14. bondinvReinitInv() indexes p_itemcur[i] at the C stride, so the
     * 0x14 allocation was overrun by ~12 bytes per item, scribbling type=-1 into the next
     * MEMPOOL_STAGE allocation (the first prop model file: TT33 root Child). ABI/layout fix. */
    g_CurrentPlayer->p_itemcur     = mempAllocBytesInBank((g_CurrentPlayer->equipmaxitems * (s32)sizeof(InvItem) + 0xfU | 0xf) ^ 0xf, MEMPOOL_STAGE);
#else
    g_CurrentPlayer->p_itemcur     = mempAllocBytesInBank((g_CurrentPlayer->equipmaxitems * 0x14 + 0xfU | 0xf) ^ 0xf, MEMPOOL_STAGE);
#endif
  bondinvReinitInv();
}
