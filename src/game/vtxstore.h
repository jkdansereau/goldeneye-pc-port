#ifndef _VTXSTORE_H_
#define _VTXSTORE_H_
#include <ultra64.h>

void sub_GAME_7F09B820(void);
void sub_GAME_7F09BBBC(void);
#ifdef PORT
void *vtxstore_allocate(s32 arg0, s32 type, void *arg2, s32 arg3);
#else
s32 vtxstore_allocate(s32 arg0, s32 type, s32 arg2, s32 arg3);
#endif
void sub_GAME_7F09C044(Vertex* arg0);

#endif
