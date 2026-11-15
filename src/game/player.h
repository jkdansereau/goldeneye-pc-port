#ifndef _PLAYER_H_
#define _PLAYER_H_
#include <ultra64.h>
#include <bondconstants.h>
#include <bondtypes.h>
#include "bondview.h"

struct player_data {
    /* 0x0 */
    s32 shot_count[7];

    /* 0x1c */
    int kill_count;

    /* 0x20 */
    int killed_gg_owner_count;

    /* 0x24 */
    int kill_counts[MAX_PLAYER_COUNT];

    /* 0x34 */
    int time_other_players_on_screen;

    /* 0x38 */
    float distance_traveled;

    /* 0x3c */
    int damage_to_backside;

    /* 0x40 */
    float body_armor_pickups;

    /* 0x44 */
    int min_time_between_kills;

    /* 0x48 */
    int max_time_between_kills;

    /* 0x4c */
    int longest_inning;

    /* 0x50 */
    s32 shortest_inning;

    /* 0x54 */
    int most_killed_one_life;

    /* 0x58 */
    int most_killed_one_time;

    /* 0x5c */
    float handicap;

    /* 0x60 */
    int flag_counter;

    /* 0x64 */
    float player_perspective_height;

    /* 0x68 */
    u8 order_out_in_yolt;

    /* 0x69 */
    u8 have_token_or_goldengun;

    /* 0x6a */
    u8 autoaim;

    /* 0x6b */
    u8 sight;

    /* 0x6c */
    int killed_civilians;
};

extern struct player *g_playerPointers[4];

// extern struct player_data player1_player_data;
// extern struct player_data player2_player_data;
// extern struct player_data player3_player_data;
// extern struct player_data player4_player_data;

extern struct player_data g_playerPlayerData[4];

extern struct player *g_CurrentPlayer;
extern struct player_data *g_playerPerm;
extern s32 player_num;
extern s32 random_byte;
extern PLAYER_ID array_PLAYER_IDs[4];


void reset_play_data_ptrs(void);
void init_player_data_ptrs_construct_viewports(int playercount);
void shuffle_player_ids(void);
s32 get_nth_player_from_shuffled(PLAYER_ID id);
void set_cur_player(s32 playernum);
s32 getPlayerCount(void);
void set_cur_player_screen_size(u32 width, u32 height);
void set_cur_player_viewport_size(u32 ulx, u32 uly);


s32 get_cur_playernum(void);
void set_cur_player_fovy(f32 fovy);
s32 getPlayerPointerIndex( PropRecord* prop);
void set_cur_player_aspect(f32 aspect);
void initBONDdataforPlayer(s32 player_num);
s32 get_player_position_in_shuffled(s32 current_player_num);
void sub_GAME_7F09B368(enum GUNHAND hand);
void sub_GAME_7F09B398(enum GUNHAND hand);

#ifdef PORT
/* Fork-local co-op campaign (port/src/coop.c, docs/dev/findings.md D960).
 * Each macro is the original N64 expression when built without PORT. */
struct ChrRecord;
s32 coopIsMission(void);
s32 coopRuleMulti(void);
s32 coopDeathFinal(void);
s32 coopPvpAllowed(void);
s32 coopSimOwner(void);
void coopVersusDeathState(void);
f32 coopDownFade(f32 frac);
f32 coopDownChrFade(f32 frac);
s32 coopRespawnButton(s32 pressed);
void coopSetBondKia(void);
s32 coopMissionEndOk(void);
void coopAiTick(struct ChrRecord *chr, s32 perView);
void coopFrameTick(void);
s32 coopStageNumPlayers(s32 stage, s32 selected);
void coopSpawnOffset(f32 *x, f32 *z, void *pstan, f32 lookx, f32 lookz);
s32 coopWatchStart(void);
s32 coopWatchPad(void);
s32 coopPickupTaken(struct PropRecord *prop);
s32 coopPickupOp(struct PropRecord *prop, s32 op);
s32 coopHasKeyFlags(u32 flags);
void coopPropFreed(struct PropRecord *prop);
s32 coopAnyHasPropInInv(struct PropRecord *prop);
s32 coopAnyCopiedGoldenEye(void);
s32 coopChrIsPlayer(struct ChrRecord *chr);
s32 coopAutoAimAtPlayer(void);
void coopNpcHurt(struct ChrRecord *chr, f32 damage, f32 subroty, s32 item);
s32 coopAiStep(void *list, s32 offset, s32 isPlayerTest);
s32 coopBondBody(void);
s32 coopProjSimOwner(s32 ownedByCurrent, struct PropRecord *ownerprop, s32 viewOwner);
s32 coopSplitView(void);
void coopViewOrder(void);
s32 coopWatchLock(s32 lock);
void coopFrozenCamera(u16 buttons, u16 oldbuttons, struct coord3d *pos, struct coord3d *pos2, struct coord3d *offset, struct StandTile **stan, struct coord3d *arg6);
#define GE_RULE_SOLO()           coopIsMission()
#define GE_RULE_MULTI()          coopRuleMulti()
#define GE_DEATH_SOLO()          (getPlayerCount() == 1 || coopDeathFinal())
#define GE_PVP_ALLOWED()         coopPvpAllowed()
#define GE_AUTOAIM_AT_PLAYER()   coopAutoAimAtPlayer()
#define GE_VERSUS_DEATH_STATE()  coopVersusDeathState()
#define GE_DOWN_FADE(f)          coopDownFade(f)
#define GE_DOWN_CHRFADE(f)       coopDownChrFade(f)
#define GE_RESPAWN_BUTTON(x)     coopRespawnButton(x)
/* co-op: a death is KIA only when final; a player still down at the exit fails the mission (revive first) */
#define GE_SET_BOND_KIA()        coopSetBondKia()
#define GE_MISSION_END_OK()      coopMissionEndOk()
#define GE_SIM_TICKS(x)          (coopSimOwner() ? (x) : 0)
#define GE_AI_TICK_VIEW(chr)     coopAiTick((chr), 1)
#define GE_AI_TICK_FRAME(chr)    coopAiTick((chr), 0)
#define GE_COOP_FRAME_TICK()     coopFrameTick()
#define GE_COOP_SPAWN_OFFSET(x, z, st, lx, lz) coopSpawnOffset((x), (z), (st), (lx), (lz))
#define GE_WATCH_START()         (getPlayerCount() == 1 || coopWatchStart())
#define GE_WATCH_LOCK(l)         coopWatchLock(l)
#define GE_WATCH_PAD()           coopWatchPad()
#define GE_PICKUP_TAKEN(prop)    coopPickupTaken(prop)
#define GE_PICKUP_VISIBLE(prop)  (!coopPickupTaken(prop))
#define GE_PICKUP_OP(prop, op)   coopPickupOp((prop), (op))
#define GE_HAS_KEY_FLAGS(f)      coopHasKeyFlags(f)
#define GE_NPC_HURT(chr, dmg, rot, item) coopNpcHurt((chr), (dmg), (rot), (item))
#define GE_CHR_PICKABLE(chr)     ((chr)->model && !coopChrIsPlayer(chr))
/* AI hide_all_chrs (before the exit camera): a co-op player's body is a chr too, and hiding it hid Bond in the outro */
#define GE_CHR_HIDEABLE(chr)     ((chr)->model != NULL && !coopChrIsPlayer(chr))
#define GE_HAS_PROP_IN_INV(prop) coopAnyHasPropInInv(prop)
#define GE_KEY_COPIED()          coopAnyCopiedGoldenEye()
#define GE_PROP_FREED(prop)      coopPropFreed(prop)
/* stage scripts ask "fewer than N players?" to tell a mission from a versus match: a co-op mission is a mission */
#define GE_AI_PLAYERS()          (coopIsMission() ? 1 : getPlayerCount())
/* AI commands that test a condition of "Bond" (position, pad distance, room, items): true if ANY living player meets it */
#define GE_AI_IS_PLAYER_TEST(c)  ((c) == AI_IFBondYPosLessThan || (c) == AI_IFBondInRoomWithPad || \
                                  (c) == AI_IFBondDistanceToPadLessThanDecimeter || (c) == AI_IFBondDistanceToPadGreaterThanDecimeter || \
                                  (c) == AI_IFBondCollectedObject)
#define GE_BOND_BODY()           (getPlayerCount() == 1 || coopBondBody())
#define GE_PROJ_SIM_OWNER(own, op, view) coopProjSimOwner((own), (op), (view))
#define GE_SPLIT_GE2()           (getPlayerCount() >= 2 && coopSplitView())
#define GE_SPLIT_EQ2()           (getPlayerCount() == 2 && coopSplitView())
#define GE_COOP_VIEW_ORDER()     coopViewOrder()
#define GE_FROZEN_CAMERA         coopFrozenCamera
#else
#define GE_RULE_SOLO()           (getPlayerCount() == 1)
#define GE_RULE_MULTI()          (getPlayerCount() >= 2)
#define GE_DEATH_SOLO()          (getPlayerCount() == 1)
#define GE_PVP_ALLOWED()         1
#define GE_AUTOAIM_AT_PLAYER()   1
#define GE_VERSUS_DEATH_STATE()  set_missionstate(6)
#define GE_DOWN_FADE(f)          (f)
#define GE_DOWN_CHRFADE(f)       (f)
#define GE_RESPAWN_BUTTON(x)     (x)
#define GE_SET_BOND_KIA()        (g_isBondKIA = 1)
#define GE_MISSION_END_OK()      1
#define GE_SIM_TICKS(x)          (x)
#define GE_AI_TICK_VIEW(chr)     chrlvActionTick(chr)
#define GE_AI_TICK_FRAME(chr)    chrlvActionTick(chr)
#define GE_COOP_FRAME_TICK()
#define GE_COOP_SPAWN_OFFSET(x, z, st, lx, lz)
#define GE_WATCH_START()         (getPlayerCount() == 1)
#define GE_WATCH_LOCK(l)         (l)
#define GE_FROZEN_CAMERA         bondviewFrozenCameraTick
#define GE_SPLIT_GE2()           (getPlayerCount() >= 2)
#define GE_SPLIT_EQ2()           (getPlayerCount() == 2)
#define GE_COOP_VIEW_ORDER()
#define GE_PICKUP_TAKEN(prop)    0
#define GE_PICKUP_VISIBLE(prop)  1
#define GE_PICKUP_OP(prop, op)   (op)
#define GE_HAS_KEY_FLAGS(f)      bondinvCheckHasKeyFlags(f)
#define GE_NPC_HURT(chr, dmg, rot, item) bondviewCallRecordDamageKills((dmg), (rot), -1, 1)
#define GE_CHR_PICKABLE(chr)     ((chr)->model)
#define GE_CHR_HIDEABLE(chr)     ((chr)->model != NULL)
#define GE_HAS_PROP_IN_INV(prop) bondinvHasPropInInv(prop)
#define GE_KEY_COPIED()          get_keyanalyzer_flag()
#define GE_PROP_FREED(prop)
#define GE_AI_PLAYERS()          getPlayerCount()
#define GE_BOND_BODY()           (getPlayerCount() == 1)
#define GE_PROJ_SIM_OWNER(own, op, view) (own)
#endif

#endif
