#include <ultra64.h>
#include "chrobjdata.h"
#include "image.h"
#include "math_asinfacosf.h"
#include "math_ceil.h"
#include "math_floor.h"
#include "math_unk_05A9E0.h"
#include "model.h"
#include "ob.h"
#include "objecthandler.h"
#include "quaternion.h"
#include "tex.h"

#ifdef PORT
#include <stdio.h>
#include <stdlib.h>
#include "pcmodels.h" /* D50: PC-layout model sidecars (Plan B, D48/D49) */
#include "pccg.h"     /* D69: PC-layout bg/stan sidecars */
extern resource_lookup_data_entry resource_lookup_data_array[]; /* ob.c */
#endif


/***
 * Perfect Dark:
 * void modeldef0f1a7560(struct modeldef *modeldef, u16 filenum, u32 arg2, struct modeldef *modeldef2, struct texpool *texpool, bool arg5)
 * 
 * NTSC address 0x7F0762E0.
*/
void sub_GAME_7F0762E0(ModelFileHeader *objheader, u8 *name, u8 *dst, struct texpool *buffer)
{
    ModelNode *node;
    s32 romremaining;
    Gfx *gdl;
    s32 pcremaining;
    u32 replacementgdl;
    ModelNode *curnode;
    Gfx *curgdl;
    s32 delta;
    s32 filedata;
    s32 filenum;

    filedata = (s32) objheader->Switches;
    filenum = fileGetIndex((char *) name);

    romremaining = get_rom_remaining_buffer_for_index(filenum);
    pcremaining = get_pc_remaining_buffer_for_index(filenum);
    node = 0;
    modelIterateDisplayLists(objheader, &node, &gdl);

    if (gdl != 0)
    {
        name = (u8 *) ((pcremaining - ((s32) (((u8 *) objheader->Switches) + (((u32) gdl) & 0x00ffffff)))) + ((s32) filedata));
        
        /* The signed lvalue cast is required for the compiler to choose the target registers. */
        replacementgdl = (u32)*(s32 *)&gdl;
        
        delta = ((s32) ((romremaining + filedata) - (s32) name)) - ((s32) (((u8 *) objheader->Switches) + (((u32) gdl) & 0x00ffffff)));
        
        texCopyGdls((Gfx *) (((u8 *) objheader->Switches) + (((u32) gdl) & 0x00ffffff)), (Gfx *) ((romremaining + filedata) - (s32) name), (s32) name);

        texLoadFromModelFileHeader(objheader, buffer);

        if (node != 0)
        {
            do
            {
                curnode = node;
                curgdl = gdl;
                modelIterateDisplayLists(objheader, &node, &gdl);
                
                if (gdl != 0)
                {
                    name = (u8 *) (((s32) gdl) - ((s32) curgdl));
                }
                else
                {
                    name = (u8 *) ((((s32) (filedata + pcremaining)) - ((s32) objheader->Switches)) - (((u32) curgdl) & 0x00ffffff));
                }
                
                modelNodeReplaceGdl((u32) objheader, curnode, curgdl, (Gfx *) replacementgdl);
                
                replacementgdl += texLoadFromGdl( (Gfx *) ((((u8 *) objheader->Switches) + (((u32) curgdl) & 0x00ffffff)) + delta), (s32) name, (Gfx *) (((u8 *) objheader->Switches) + (replacementgdl & 0x00ffffff)), buffer);
            } 
            while (node != 0);
        }

        name = (u8 *) (((s32) (((u8 *) objheader->Switches) + (replacementgdl & 0x00ffffff))) - filedata);

        fileSetSize(filenum, (u8 *) filedata, (((s32) name + 0xf) & (~0xf)), dst == 0);
    }
}


/***
 * NTSC addres 0x7F0764A4.
*/
void load_object_fill_header(struct ModelFileHeader *objheader, u8 *name, u8* dst, s32 size, struct texpool * buffer)
{
    void *filedata;

#ifdef PORT
    /* PC port (D50, Plan B D48/D49): serve PC-layout model sidecars from the
     * cart extension region. One-shot table patch (safe to call repeatedly);
     * by first model load obInit() has definitely run (boss.c:179). Must run
     * before _fileNameLoadTo* below reads hw_address/rom_size. */
    pcmodelsPatchTable();
    /* D69: same one-shot pattern for the bg/stan sidecars -- piggybacks on
     * this call site purely for a "definitely after obInit()" hook; it has
     * nothing to do with model loading. */
    pccgPatchTable();
    if (dst == 0) {
        /* D48.3: a stale poolRemaining from an earlier load of this file
         * would under-allocate the fresh bank chunk (P_old < round8(C_pc)+8)
         * and load_resource would fail with poolRemaining=0. Resetting to 0
         * forces the full-bank allocation; the bump allocator ends at the
         * same cursor either way (mempAddEntryOfSizeToBank shrinks it back). */
        resource_lookup_data_array[fileGetIndex((char *)name)].poolRemaining = 0;
    }
#endif

    if (dst != 0)
    {
        filedata = _fileNameLoadToAddr(name, 0, dst, size);
    }
    else
    {
        filedata = _fileNameLoadToBank(name, 0, 0x100, 4);
    }
    
    objheader->Switches = (struct ModelNode **)filedata;

#ifdef PORT
    /* PC port (D43/D45): the switches array is NS x 8B pointer slots, not NS x 4B words. */
    objheader->Textures = (struct ModelFileTextures *)((u8 *)filedata + sizeof(struct ModelNode *) * objheader->numSwitches);
#else
    // hmmmmmmmmmmmm
    objheader->Textures = (struct ModelFileTextures *)&((s32*)filedata)[objheader->numSwitches];
#endif
    
    objheader->RootNode = (struct ModelNode *)&objheader->Textures[objheader->numtextures];

#if defined(PORT) /* TEMP D86: correlate header identity with the model name at load time */
    if (getenv("GE_D86")) {
        fprintf(stderr, "[D86] load_object_fill_header name=%s objheader=%p filedata=%p RootNode=%p numSwitches=%d numtextures=%d\n",
                (const char *)name, (void *)objheader, (void *)filedata, (void *)objheader->RootNode,
                objheader->numSwitches, objheader->numtextures);
        fflush(stderr);
    }
#endif
    sub_GAME_7F075A90(objheader, 0x5000000, filedata);
    sub_GAME_7F0762E0(objheader, name, dst, buffer);
}




void fileLoad(struct ModelFileHeader *header,char *name)
{
   load_object_fill_header(header,name,0,0,0);
   return;
}


void load_object_into_memory_unused_maybe(struct ModelFileHeader *header,int *recallstring,int *targetloc,int sizeleft)
{
   load_object_fill_header(header,recallstring,targetloc,sizeleft,0);
   return;
}





