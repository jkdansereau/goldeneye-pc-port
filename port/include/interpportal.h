#ifndef _INTERPPORTAL_H_
#define _INTERPPORTAL_H_

#include <stdint.h>

/* D578: port-side replay of bg.c's portal-visibility traversal for an
 * interpolated camera, so each drawn room's scissor box can be recomputed for
 * the in-between pass instead of using the one the game computed for the
 * current camera. Read-only on game data. See port/src/interpportal.c and
 * docs/dev/findings.md D578.
 *
 * Env: GE_INTERPPORTAL_OFF=1    never replace scissors (old matrix correction)
 *      GE_INTERPPORTAL_UNION=1  union the replayed box with the game's box */

#ifdef __cplusplus
extern "C" {
#endif

/* osSendMesg hook, GAME thread: when `msg` is the frame's graphics task going to
 * the scheduler, snapshot everything the replay reads (the render worker draws
 * the frame while the game thread is already on the next one). */
void interpPortalOnSend(void *mq, void *msg);

/* Start of a frame body / interpolation pass on the render thread: select the
 * snapshot taken for display list `dl` and drop the cached replay. */
void interpPortalPassBegin(const void *dl);

/* Every G_MTX_PROJECTION|G_MTX_LOAD: `raw` is the matrix as the game loaded
 * it, `blended` the in-between version (before the far-clip widening); equal
 * on exact passes. Row-vector 4x4 floats as the gfx layer holds them. */
void interpPortalNoteProj(const float raw[4][4], const float blended[4][4]);

/* The scissor about to be applied. seg14 = last gSPSegment(14) value (the room
 * vertices base), (rl,rt,rr,rb) = the game's scissor in N64 pixels. When it
 * returns 1, *ol..*ob is the replacement (N64 pixels, left/top/right/bottom,
 * same clamping as bgScissorCurrentPlayerView). 0 = leave the scissor alone
 * (not a room scissor, no replay available, or replace == 0). D583: 2 = a room
 * scissor whose room the replay at this camera does not reach (the caller
 * hides a room that just entered the drawn set). */
int interpPortalScissor(uint32_t seg14, float rl, float rt, float rr, float rb,
                        int replace, int *ol, int *ot, int *or_, int *ob);

/* D578: replay counters {replaced, replays, fullview, gatefail, noroom, unreached, nocam, nosnap} */
void interpPortalCounts(unsigned out[8]);

/* D578: rooms the replay reached at the interpolated camera that the game did
 * not draw (a hole risk); read-and-clear after a pass. interpPortalExtraCount
 * = replays that found any. */
/* D583: rooms the last replay reached that the game did not draw for the
 * current camera: returns the count; fills up to `max` vertex bases
 * (SPSEGMENT_BG_VTX values) and whether the previous snapshot drew each
 * (1 / 0 / -1 unknown). */
#define IP_MAXEXTRA 64
int interpPortalTakeExtraRooms(uint32_t *vphys, int *prevdrew, int max);
/* D583: 1 when this pass ran the portal replay at a blended camera (then the
 * REACH test above decides holes geometrically). */
int interpPortalReplayedThisPass(void);
unsigned interpPortalExtraCount(void);

#ifdef __cplusplus
}
#endif

#endif
