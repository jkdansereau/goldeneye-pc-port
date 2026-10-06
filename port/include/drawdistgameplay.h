#ifndef _DRAWDISTGAMEPLAY_H_
#define _DRAWDISTGAMEPLAY_H_

/* D466 (#125): Video.DrawDistance must not change GAMEPLAY visibility.
 * Rendering keeps the extended range; gameplay readers (AI "on screen",
 * "has been seen", out-of-view spawn checks, shot tests, ...) see exactly
 * what the N64 would at the level's authored FarFog. Design + reader triage:
 * docs/dev/findings.md D466. Every hook is a no-op when the draw-distance
 * multiplier is 1, unless GE_D466_FORCE=1; GE_D466_OFF=1 disables the split
 * (reproduces the leak, for A/B). */

struct PropRecord;

/* prop->flags is a u8 and bit 0x80 is unused by the game. Set (together with
 * PROPFLAG_ONSCREEN) when a prop is drawn only thanks to the extended range. */
#define PORT_PROPFLAG_EXTONLY 0x80

int  portD466Active(void);                         /* split in effect right now */
void portD466SetAuthoredPass(int on);              /* bgDetermineVisibleRooms pass 1 */
int  portD466InAuthoredPass(void);
void portD466StoreRoom(int room, int rendered, int neighbor);
void portD466FrameLog(void);                       /* GE_D466LOG, called once per rendered frame */

int  portRoomGameplayVisible(int room);            /* = getROOMID_isRendered at mult 1 */
int  portRoomGameplayNeighbor(int room);
int  portPropGameplayOnScreen(struct PropRecord *prop);
void portPropSetGameplayOnScreen(struct PropRecord *prop, int gameplayVisible);

/* posIsOnScreen records the authored-distance verdict of its last call here. */
void portD466SetLastPosVerdict(int gameplayVisible);
int  portD466LastPosVerdict(void);

/* D468: AI view clamp (cartridge-widest view = 16:9 at the game's FOV). */
void portD468UpdateClamp(void);                   /* per player view, bgDetermineVisibleRooms */
int  portD468ClampActive(void);
void portD468ShrinkBox(float *minmax);            /* {min.x,min.y,max.x,max.y} */

/* authored-distance fog (bgfog.c) */
float portD466AuthoredFar(void);
int   portFogPositionVisibleGameplay(float *pos3, float range);
float portFogScaledFarFogIntensitySquaredGameplay(void);

#endif
