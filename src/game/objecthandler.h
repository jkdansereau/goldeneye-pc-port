#ifndef _OBJECTHANDLER_
#define _OBJECTHANDLER_
#include <ultra64.h>
#include <bondtypes.h>
#include <image.h>

struct bondstruct_unk_animation_related {
    char* uselessPointer; // Is incremented like a count when an animation is copied from ROM to RAM but it's never read
    char* animBufferPtr1; 
    char* animBufferPtr2; 
};

struct bondstruct_unk_op07_related {
    s32 unk00;
    s32 unk04;
    s32 unk0C;
};

#ifdef PORT
/* PC port (D53.2): the game type-puns these slot structs with struct Model —
 * model.c does `(Model *)&g_ModelSlots[i]` / `(Model *)&g_AnimModelSlots[i]`
 * and reads/writes Model fields through the slot storage. On N64 the punned
 * pairs line up exactly: slot.unk08@0x08 IS the low word of Model.obj (the
 * in-use flag — modelInit's `obj = header` marks a slot used, obj=NULL frees
 * it), slot.unk10@0x10 IS Model.datas (the RW-data pool pointer), and
 * slot.unk02@0x02 IS Model.rwdatalen.
 *
 * On x86-64 natural alignment moves the Model pointers: obj@0x10, datas@0x20
 * (probe-verified with vsize.c; bondtypes.h has no pack pragma). The old slot
 * layout kept N64 offsets, so the pun silently diverged:
 *   - clear_model_obj() (`model->obj = NULL`, called every frame by
 *     update_menu00_legalscreen) wrote 8 bytes at @0x10..0x17 — clobbering
 *     slot.unk10 (the pool pointer lived at @0x10 here);
 *   - modelmgrCanSlotFitRwdata() read `(Model*)&slot)->datas` at @0x20
 *     (garbage: old unk14/unk18) while the assignment read unk10@0x10 —
 *     check passed on garbage, assignment got NULL;
 *   - modelInit/animInit write through the whole punned Model (up to PC
 *     offset 0xE3), overflowing the 0x20/0xC0-byte slots.
 * Result: frame-5 SIGSEGV in modelInitRwData (NULL pool deref).
 *
 * Fix: re-lay the slots out on top of the PC struct Model so every punned
 * pair matches again, and pad each slot to sizeof(struct Model) so a full
 * Model fits. Member names are kept — only unk02/unk08/unk10 are referenced
 * anywhere (whole-tree grep). N64 layout verbatim under #else. */
struct AnimModelSlot {
    s16  unk00;                  /* 0x00 */
    s16  unk02;                  /* 0x02 — Model.rwdatalen */
    char pad04[0x0C];            /* 0x04..0x0F: Model.chr@8 (PC) */
    s32  unk08;                  /* 0x10 — low word of Model.obj = in-use flag */
    char pad14[0x0C];            /* 0x14..0x1F: Model.render_pos@0x18 (PC) */
    void *unk10;                 /* 0x20 — Model.datas (RW-data pool pointer) */
    char pad28[sizeof(struct Model) - 0x28]; /* rest of the punned Model */
};

struct ModelSlot {
    s16  unk00;                  /* 0x00 */
    s16  unk02;                  /* 0x02 — Model.rwdatalen */
    char pad04[0x0C];            /* 0x04..0x0F: Model.chr@8 (PC) */
    s32  unk08;                  /* 0x10 — low word of Model.obj = in-use flag */
    char pad14[0x0C];            /* 0x14..0x1F: Model.render_pos@0x18 (PC) */
    void *unk10;                 /* 0x20 — Model.datas (RW-data pool pointer) */
    char pad28[sizeof(struct Model) - 0x28]; /* rest of the punned Model */
};
#else
struct AnimModelSlot {
    s16 unk00;
    s16 unk02;
    s32 unk04;
    s32 unk08;
    s32 unk0c;
    void *unk10;
    s32 unk14;
    s32 unk18;
    s32 unk1c;
    s32 unk20;
    s32 unk24;
    s32 unk28;
    s32 unk2c;
    s32 unk30;
    s32 unk34;
    s32 unk38;
    s32 unk3c;
    s32 unk40;
    s32 unk44;
    s32 unk48;
    s32 unk4c;
    s32 unk50;
    s32 unk54;
    s32 unk58;
    s32 unk5c;
    s32 unk60;
    s32 unk64;
    s32 unk68;
    s32 unk6c;
    s32 unk70;
    s32 unk74;
    s32 unk78;
    s32 unk7c;
    s32 unk80;
    s32 unk84;
    s32 unk88;
    s32 unk8c;
    s32 unk90;
    s32 unk94;
    s32 unk98;
    s32 unk9c;
    s32 unka0;
    s32 unka4;
    s32 unka8;
    s32 unkac;
    s32 unkb0;
    s32 unkb4;
    s32 unkb8;

    // is this struct size 0xbc or 0xc0 ?
    //s32 unkbc;
};

struct ModelSlot {
    s16 unk00;
    s16 unk02;
    s32 unk04;
    s32 unk08;
    s32 unk0c;
    void *unk10;
    s32 unk14;
    s32 unk18;
    s32 unk1c;
};
#endif

extern struct AnimModelSlot *g_AnimModelSlots;
extern struct ModelSlot *g_ModelSlots;

extern struct ModelHitEntry *g_ModelHitFreeList;
extern s32 g_ModelDistanceDisabled;
extern f32 g_ModelDistanceScale;
extern u32 g_ModelAnimMergingEnabled;
extern s32 D_80036410;
extern struct bondstruct_unk_animation_related* D_80036414;
extern s32 D_80036418;
extern s32 D_8003641C;
extern u32 D_800363F0;

extern coord3d D_80036094;
extern coord3d D_800360A0;
extern coord3d D_800360AC;
extern coord3d D_800360B8;
extern coord3d D_80036244;
extern coord3d D_80036254;

extern struct Vertex* (*vtxallocator)(s32 numvertices);
extern void (*g_ModelJointPositionedFunc)(s32 mtxindex, Mtxf *mtx);
extern struct bondstruct_unk_op07_related D_800360C4[];
extern Vertex D_800363E0;
extern Vtx D_800363F8;
extern coord3d D_80036408;

void fileLoad(ModelFileHeader *header,char *name);
void load_object_into_memory_unused_maybe(ModelFileHeader *header,int *recallstring,int *targetloc,int sizeleft);

// tentative signature
PropRecord *chrGiveWeapon(ChrRecord *self, s32 PropID, ITEM_IDS ItemID, s32 flags);

// called with struct ChrRecord->field_20
ModelHitEntry* sub_GAME_7F06B120(ModelHitEntry* head, Model* context);
void sub_GAME_7F06B248(ModelHitEntry *entry);
void drawjointlist(ModelRenderData *arg0, ModelHitEntry *entry);
void sub_GAME_7F06B29C(ModelHitEntry *arg0);
ModelHitEntry *sub_GAME_7F06BB28(ModelHitEntry *modelhit);
s32 probably_damage_detail_blood_effect_related(ModelHitEntry **entryptr, coord3d *raypos, coord3d *raydir, Model **outModel, ModelNode **inoutNode);
s32 sub_GAME_7F06C010(ModelHitEntry **entryptr, coord3d *modelRayStart, coord3d *modelRayDir, Model **outModel, ModelNode **outNode);

void load_object_fill_header(struct ModelFileHeader *objheader, u8 *name, u8* dst, s32 size, struct texpool * buffer);


#endif
