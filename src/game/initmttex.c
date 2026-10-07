#include <ultra64.h>
#include <memp.h>
#include "image.h"
#include "initmttex.h"
#ifdef PORT
#include <stdlib.h>
#endif

void set_mt_tex_alloc(void)
{  
    g_TexCacheCount = 0;

    if (tokenFind(1, "-mt"))
    {
        bytes = strtol(tokenFind(1, "-mt"), 0x0, 0) * 1024; //get KB
    }

    texInitPool(&ptr_texture_alloc_start, mempAllocBytesInBank(bytes, MEMPOOL_STAGE), bytes);
#ifdef PORT
    /* D336 probe: the stage texture pool size actually in effect. When it
     * fills, texLoad points textures at the pool start (garbage textures,
     * e.g. rainbow Frigate water) -- see GE_D85TEX's pool-full line. */
    {
        extern s32 g_StageNum;
        if (getenv("GE_D85TEX"))
            osSyncPrintf("D85TEX stage=%d texpool bytes=%d (-mt token %s) range=[%p..%p)\n", (int)g_StageNum,
                         (int)bytes, tokenFind(1, "-mt") ? "present" : "ABSENT",
                         (void *)ptr_texture_alloc_start.start, (void *)ptr_texture_alloc_start.end);
    }
#endif
}
