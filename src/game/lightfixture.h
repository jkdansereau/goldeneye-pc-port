#ifndef _LIGHTFIXTURE_H_
#define _LIGHTFIXTURE_H_

#include <ultra64.h>
#include <bondtypes.h>

typedef struct s_lightfixture {
    s16 room_index;
    s16 RESERVED;
    Gfx *ptr_start_pertinent_DL;
    Gfx *ptr_end_pertinent_DL;
} s_lightfixture;

struct s_darkened_light {
    u16 room_index;
    u16 vtx_index;
};

void lightFixtureEntryEnd(Gfx *param_1);
bool check_if_imageID_is_light(s32 imageID);
void lightFixtureEntryBegin(Gfx *DL);
#ifdef PORT
/* D431 (#119): image id of a fixture's texture (stored in the otherwise-unused
 * RESERVED slot) and the lookup the bullet-hit code uses in place of the KSEG0
 * texnum recovery that is invalid for the port's converted room GDLs. */
void lightFixtureSetTexnum(s32 texnum);
s32 lightFixtureTexnumForGfx(Gfx *gfx, s32 room_index);
#endif

#endif
