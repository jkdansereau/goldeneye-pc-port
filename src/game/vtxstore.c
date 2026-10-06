#include <ultra64.h>
#include <memp.h>
#include "bondtypes.h"
#include "vtxstore.h"
#include "propobj.h"
#include "model.h"
#ifdef PORT
#include <stdio.h>
#include <stdlib.h>
#endif

// unsure if these structs are defined as something else, elsewhere
struct unk_09B7A0_struct_parent {
    Vertex* unk00;
#ifdef PORT
    void *unk04; /* D457: owner tag (model->obj), was s32 */
#else
    s32 unk04;
#endif
    s32 unk08;
    s16 unk0C;
    s16 unk0E;
    s16 unk10;
    s16 unk12;
};

// bss
//CODE.bss:8007A0D0
s32 dword_CODE_bss_8007A0D0; // item count for dword_CODE_bss_8007A0E0
//CODE.bss:8007A0D4
s32 dword_CODE_bss_8007A0D4; // item count for dword_CODE_bss_8007A0E8
//CODE.bss:8007A0D8
s32 dword_CODE_bss_8007A0D8; // item count for dword_CODE_bss_8007A0E4
//CODE.bss:8007A0DC
s32 dword_CODE_bss_8007A0DC; // item count for dword_CODE_bss_8007A0EC
//CODE.bss:8007A0E0
Vertex* dword_CODE_bss_8007A0E0; // array ( uses dword_CODE_bss_8007A0D0 as alloc count, item size 0x10 )
//CODE.bss:8007A0E4
Vertex* dword_CODE_bss_8007A0E4; // array ( uses dword_CODE_bss_8007A0D8 as alloc count, item size 0x10 )
//CODE.bss:8007A0E8
struct unk_09B7A0_struct_parent* dword_CODE_bss_8007A0E8; // array ( uses dword_CODE_bss_8007A0D4 as alloc count, item size 0x14 )
//CODE.bss:8007A0EC
struct unk_09B7A0_struct_parent* dword_CODE_bss_8007A0EC; // array ( uses dword_CODE_bss_8007A0DC as alloc count, item size 0x14 )
//CODE.bss:8007A0F0
s16 word_CODE_bss_8007A0F0;
//CODE.bss:8007A0F2
s16 word_CODE_bss_8007A0F2;

void null_init_main_1(void)
{
    return;
}


void sub_GAME_7F09B7A8(void)
{
    s32 i;
    for (i = 0; i < dword_CODE_bss_8007A0D4; i++)
    {
        if (dword_CODE_bss_8007A0E8[i].unk00);
    }
}

void sub_GAME_7F09B7E4(void)
{
    s32 i;
    for (i = 0; i < dword_CODE_bss_8007A0DC; i++)
    {
        if (dword_CODE_bss_8007A0EC[i].unk00);
    }
}

/*
* Address: 7F09B820(
* PD name: vtxstore_reset
*/

void sub_GAME_7F09B820(void)
{
    u32 tmp;
    s32 stage;
    s32 i;

    tmp = 0x5DC;

    if (getPlayerCount() >= 2)
    {
        dword_CODE_bss_8007A0D0 = 0xBB8;
        dword_CODE_bss_8007A0D4 = 0x50;
        dword_CODE_bss_8007A0D8 = 0x1F4;
        dword_CODE_bss_8007A0DC = 0x14;
    }
    else
    {
        stage = lvlGetCurrentStageToLoad();
        if ((stage != 0x1E) && (stage != 0x1D))
        {
            dword_CODE_bss_8007A0D0 = 0x1F4;
            dword_CODE_bss_8007A0D4 = 0x14;
            dword_CODE_bss_8007A0D8 = tmp;
            dword_CODE_bss_8007A0DC = 0x28;
        }
        else
        {
            dword_CODE_bss_8007A0D0 = 0x1F4;
            dword_CODE_bss_8007A0D4 = 0x14;
            dword_CODE_bss_8007A0D8 = 0x1F4;
            dword_CODE_bss_8007A0DC = 0x14;
        }
    }

#ifdef PORT
    /* D462: struct unk_09B7A0_struct_parent is 24 bytes on x86-64 (Vertex* unk00 widened) but the
     * N64 stride 0x14 was used to size both tables, which are indexed at the C stride below
     * (heap overrun of ~4 bytes per entry, up to 500 entries). ABI/layout fix, section A1. */
    tmp = sizeof(struct unk_09B7A0_struct_parent);
#else
    tmp = 0x14;
#endif
    dword_CODE_bss_8007A0E8 = mempAllocBytesInBank(dword_CODE_bss_8007A0D4 * tmp, MEMPOOL_STAGE);
    dword_CODE_bss_8007A0E0 = mempAllocBytesInBank(dword_CODE_bss_8007A0D0 * 0x10, MEMPOOL_STAGE);
    dword_CODE_bss_8007A0EC = mempAllocBytesInBank(dword_CODE_bss_8007A0DC * tmp, MEMPOOL_STAGE);
    dword_CODE_bss_8007A0E4 = mempAllocBytesInBank(dword_CODE_bss_8007A0D8 * 0x10, MEMPOOL_STAGE);

    word_CODE_bss_8007A0F0 = (s16) dword_CODE_bss_8007A0D0;
    dword_CODE_bss_8007A0E8->unk00 = dword_CODE_bss_8007A0E0;
    dword_CODE_bss_8007A0E8->unk0C = (s16) dword_CODE_bss_8007A0D0;
    dword_CODE_bss_8007A0E8->unk0E = 0;
    dword_CODE_bss_8007A0E8->unk10 = -1;
    dword_CODE_bss_8007A0E8->unk12 = -1;

    for (i = 1; i < dword_CODE_bss_8007A0D4; i++)
    {
        dword_CODE_bss_8007A0E8[i].unk0E = -1;
    }

    word_CODE_bss_8007A0F2 = (s16) dword_CODE_bss_8007A0D8;
    dword_CODE_bss_8007A0EC->unk00 = dword_CODE_bss_8007A0E4;
    dword_CODE_bss_8007A0EC->unk0C = (s16) dword_CODE_bss_8007A0D8;
    dword_CODE_bss_8007A0EC->unk0E = 0;
    dword_CODE_bss_8007A0EC->unk10 = -1;
    dword_CODE_bss_8007A0EC->unk12 = -1;

    for (i = 1; i < dword_CODE_bss_8007A0DC; i++)
    {
        dword_CODE_bss_8007A0EC[i].unk0E = -1;
    }
}


/*
* Address: 0x7F09BAC4
*
* PD name: vtxstore_fix_refs
* PD description:
*  Search all props and their model data for references to the `find` address
*  and replace it with the `replacement` address.
*/
void sub_GAME_7F09BAC4(s32 find, s32 replacement) {
    PropRecord* var_s1;
#ifdef PORT
    /* D255 (M-140): the original decomp read this record's Model* through
     * `((Model*)((ChrRecord*)prop->chr)->chrflags)` -- a union pun onto
     * ChrRecord.chrflags, which is typed `CHRFLAG` (a 4-byte enum). On
     * 32-bit N64 that's bit-exact: both the enum and a pointer are 4
     * bytes, so casting the enum's VALUE to Model* reproduces the real
     * bytes stored there. On 64-bit PC, Model* is 8 bytes but the enum
     * read is still only 4 -- the cast reads just the pointer's low 32
     * bits as an int and zero-extends it back out to a 64-bit address,
     * discarding the real high bits entirely. The result is either a wild
     * pointer or (as observed live on a Steam Deck, 2026-09-15: FAULT ADDR
     * 0x10, PC in this function's `var_v1->RootNode` dereference) exactly
     * NULL, whenever the true pointer's low 32 bits happen to be zero.
     * Root-caused by comparing this truncated read against
     * `var_s1->obj->model` (ObjectRecord.model, correctly typed as
     * `Model*`) on the same live capture: the ObjectRecord-typed read was
     * always a valid, non-NULL pointer -- confirming the record's model
     * was never actually missing, only misread. Fix: read the pointer
     * through its real 64-bit-wide field (`ObjectRecord.model`, the same
     * union memory, just correctly typed) instead of through the
     * mistyped `ChrRecord.chrflags` alias. Semantics-preserving on N64
     * (identical bytes, identical result) and the D3x pointer-width ABI
     * class documented in AGENTS.md / docs/dev/findings.md. */
    ObjectRecord* var_v0;
#else
    ChrRecord* var_v0;
#endif
    Model* temp_a0;
    s32* temp_v0_2;
    ModelNode* var_a1;
    ModelFileHeader* var_v1;
    s32 val;

    var_s1 = chrpropGetActiveTail();
    while (var_s1 != NULL) {
        if (var_s1->type == 1) {
#ifdef PORT
            var_v0 = var_s1->obj;
            var_v1 = ((Model*)var_v0->model)->obj;
#else
            var_v0 = var_s1->chr;
            var_v1 = ((Model*)var_v0->chrflags)->obj;
#endif
            var_a1 = var_v1->RootNode;
            while (var_a1 != NULL) {
                val = var_a1->Opcode & 0xFF;
                if (val == 0x18) {
#ifdef PORT
                    temp_v0_2 = modelGetNodeRwData((Model*)var_v0->model, var_a1);
#else
                    temp_v0_2 = modelGetNodeRwData((Model*)var_v0->chrflags, var_a1);
#endif
                    if (find == *temp_v0_2) {
                        *temp_v0_2 = replacement;
                    }
                    break;
                } else {
                    if (var_a1->Child != NULL) {
                        var_a1 = var_a1->Child;
                    } else {
                        while (var_a1 != NULL) {
                            if (var_a1->Next != NULL) {
                                var_a1 = var_a1->Next;
                                break;
                            }
                            var_a1 = var_a1->Parent;
                        }
                    }
                }
            }
        }

        var_s1 = var_s1->prev;
    }
}





/*
* Address: 7F09BBBC
* PD name: vtxstore_tick
* Description: Merge duplicate batches. May free memory.
*/
void sub_GAME_7F09BBBC(void)
{
    s16 temp_s2;
    s16 var_fp;
    s16 var_s2;
    s32 stop;
    s32 var_a1;
    s32 var_s6;

    var_s6 = 0;

    if (word_CODE_bss_8007A0F2 < ((s32)dword_CODE_bss_8007A0D8 >> 2))
    {
        for (var_fp = 0; var_fp < dword_CODE_bss_8007A0DC - 1; var_fp++) {
            if (dword_CODE_bss_8007A0EC[var_fp].unk0E > 0)
            {
                for (var_s2 = var_fp + 1; var_s2 < dword_CODE_bss_8007A0DC; var_s2++) {
                    if ((dword_CODE_bss_8007A0EC[var_s2].unk0E > 0) &&
                        (dword_CODE_bss_8007A0EC[var_fp].unk04 == dword_CODE_bss_8007A0EC[var_s2].unk04) &&
                        (dword_CODE_bss_8007A0EC[var_fp].unk08 == dword_CODE_bss_8007A0EC[var_s2].unk08))
                    {
                        sub_GAME_7F09BAC4((s32)dword_CODE_bss_8007A0EC[var_s2].unk00, (s32)dword_CODE_bss_8007A0EC[var_fp].unk00);
                        var_s6 = 1;

                        dword_CODE_bss_8007A0EC[var_fp].unk0E += dword_CODE_bss_8007A0EC[var_s2].unk0E;
                        dword_CODE_bss_8007A0EC[var_s2].unk0E = 0;
                        
                        word_CODE_bss_8007A0F2 += dword_CODE_bss_8007A0EC[var_s2].unk0C;
                    }
                }
            }
        }
    }

    if (var_s6 != 0) {
        stop  = 0;
        var_fp = 0;

        while (stop == 0) {
            var_s2 = dword_CODE_bss_8007A0EC[var_fp].unk10;

            if (var_s2 >= 0) {
                if (dword_CODE_bss_8007A0EC[var_fp].unk0E == 0) {
                    if (dword_CODE_bss_8007A0EC[var_s2].unk0E == 0) {
                        dword_CODE_bss_8007A0EC[var_fp].unk0C += dword_CODE_bss_8007A0EC[var_s2].unk0C;
                        dword_CODE_bss_8007A0EC[var_s2].unk0E = -1;
                        var_s2 = dword_CODE_bss_8007A0EC[var_s2].unk10;
                        dword_CODE_bss_8007A0EC[var_fp].unk10 = var_s2;

                        if (var_s2 >= 0) {
                            dword_CODE_bss_8007A0EC[var_s2].unk12 = var_fp;
                        }
                        continue;
                    }
                }
                var_fp = var_s2;
            } else {
                stop = 1;
            }
        }
    }

    if (word_CODE_bss_8007A0F2 < ((s32)dword_CODE_bss_8007A0D8 >> 2)) {
        sub_GAME_7F056690();
    }
}


/*
* Address: 7F09BE4C
* PD name: vtxstore_allocate
* Description: Allocation for batches within the storage space
*/
#ifdef PORT
void *vtxstore_allocate(s32 arg0, s32 type, void *arg2, s32 arg3)
#else
s32 vtxstore_allocate(s32 arg0, s32 type, s32 arg2, s32 arg3) 
#endif
{
    s16* var_t3;
    s16 temp_t2;
    s32 var_a1;
    s32 var_a2;
    s32 var_t4;
    s32 var_v0;
    s32 var_v1;
    s16 var_v1_2;
    struct unk_09B7A0_struct_parent* var_t0;

    switch (type) {
        case 0xCCCC:
            var_t0 = dword_CODE_bss_8007A0E8;
            var_t3 = &word_CODE_bss_8007A0F0;
            var_a2 = ((s16 *)&dword_CODE_bss_8007A0D4)[1];
            break;
        case 0xB0B:
            var_t0 = dword_CODE_bss_8007A0EC;
            var_t3 = &word_CODE_bss_8007A0F2;
            var_a2 = ((s16 *)&dword_CODE_bss_8007A0DC)[1];
            break;
        default:
#ifdef PORT
            return NULL;
#else
            return 0;
#endif
    }

    var_v1_2 = 0;
    var_v0 = 0;
    var_a1 = 0;
    
    do {
        if ((var_t0[var_a1].unk0E == 0) && (var_t0[var_a1].unk0C >= arg0)) {
            var_v1_2 = 1;
        } else {
            var_a1 = var_t0[var_a1].unk10;
            var_v0 += 1;
            if ((var_a1 == -1) || (var_a2 < var_v0)) {
                var_v1_2 = (s16)-1;
            }
        }
    } while (var_v1_2 == 0);
    if (var_a2 < var_v0) {
        sub_GAME_7F09B7A8();
        sub_GAME_7F09B7E4();
#ifdef PORT
        return NULL;
#else
        return 0;
#endif
    }
    // FAKE
    if (var_v0) {}
    if (var_v1_2 == 1) {
        var_t4 = 0;
        temp_t2 = var_t0[var_a1].unk0C;
        var_t0[var_a1].unk04 = arg2;
        var_t0[var_a1].unk08 = arg3;
        var_t0[var_a1].unk0E++;
        if (temp_t2 != arg0) {
            for (var_v1 = 0; var_v1 < var_a2; var_v1++) {
                if (var_t0[var_v1].unk0E == -1) {
                    var_t0[var_a1].unk0C = arg0;
                    var_t0[var_v1].unk00 = var_t0[var_a1].unk00 + arg0;
                    var_t0[var_v1].unk0C = temp_t2 - arg0;
                    var_t0[var_v1].unk0E = 0;
                    var_t0[var_v1].unk12 = var_a1;
                    var_t4 = 1;
                    var_t0[var_v1].unk10 = var_t0[var_a1].unk10;
                    if (var_t0[var_a1].unk10 >= 0) {
                        var_t0[var_t0[var_a1].unk10].unk12 = var_v1;
                    }
                    var_t0[var_a1].unk10 = var_v1;
                    break;
                }
            }
        }
        if (var_t4 != 0) {
            *var_t3 -= arg0;
        } else {
            *var_t3 -= temp_t2;
        }
#ifdef PORT
        return (void *)var_t0[var_a1].unk00;
    }
    return NULL;
#else
        return (s32)var_t0[var_a1].unk00;
    }
    return 0;
#endif
}


/*
* Address: 7F09C044
* PD name: vtxstore_free (likely)
* Description: Either deforming a vertex or frees a vertex (deallocation)
*/
void sub_GAME_7F09C044(Vertex* arg0) {
    s16* var_t2;
    struct unk_09B7A0_struct_parent* var_a3;
    s16 var_a1;
    s32 stop;
    s32 var_v1;

    if ((arg0 >= dword_CODE_bss_8007A0E0) &&
        (arg0 <= dword_CODE_bss_8007A0E0 + (dword_CODE_bss_8007A0D0 - 1))) {
        var_a3 = dword_CODE_bss_8007A0E8;
        var_t2 = &word_CODE_bss_8007A0F0;
    } else if ((arg0 >= dword_CODE_bss_8007A0E4) &&
               (arg0 <= dword_CODE_bss_8007A0E4 + (dword_CODE_bss_8007A0D8 - 1))) {
        var_a3 = dword_CODE_bss_8007A0EC;
        var_t2 = &word_CODE_bss_8007A0F2;
    } else {
        sub_GAME_7F09B7A8();
        sub_GAME_7F09B7E4();
        return;
    }

    var_a1 = 0;
    stop = 0;

    while (stop == 0) {
        if (var_a3[var_a1].unk00 == arg0) {
            stop = 1;
            // FAKE
            if (var_a3[var_a1].unk0E);
            var_a3[var_a1].unk0E--;
            if (var_a3[var_a1].unk0E == 0) {
                *var_t2 += var_a3[var_a1].unk0C;

                // Merge with next free block
                var_v1 = var_a3[var_a1].unk10;
                if (var_v1 >= 0) {
                    if (var_a3[var_v1].unk0E == 0) {
                        var_a3[var_a1].unk0C += var_a3[var_v1].unk0C;
                        var_a3[var_a1].unk10 = var_a3[var_v1].unk10;
                        var_a3[var_v1].unk0E = -1;
                        var_v1 = var_a3[var_a1].unk10;
                        if (var_v1 >= 0) {
                            var_a3[var_v1].unk12 = var_a1;
                        }
                    }
                }

                // Merge with prev free block
                var_v1 = var_a3[var_a1].unk12;
                if (var_v1 >= 0) {
                    if (var_a3[var_v1].unk0E == 0) {
                        var_a3[var_v1].unk0C += var_a3[var_a1].unk0C;
                        var_a3[var_v1].unk10 = var_a3[var_a1].unk10;
                        var_a3[var_a1].unk0E = -1;
                        var_a1 = var_v1;
                        var_v1 = var_a3[var_a1].unk10;
                        if (var_v1 >= 0) {
                            var_a3[var_v1].unk12 = var_a1;
                        }
                    }
                }
            }
        } else {
            var_a1 = var_a3[var_a1].unk10;
            if (var_a1 == -1) {
                stop = 1;
            }
        }
    }
}

