/*
 * coop.c -- opt-in 2-player split-screen co-op campaign (Switch only; D960).
 *
 * The engine has no mode flag: "mission or versus" is inferred from
 * getPlayerCount(). Co-op runs the solo campaign with two players, so the
 * rule sites in src/game ask coopIsMission() / coopRuleMulti() (macros GE_RULE_*
 * in src/game/player.h) instead of the raw count. Every other behaviour of
 * co-op is in this file; src/game only carries one-line hooks.
 *
 * Off (option off, no second pad, a non-mission stage, any non-Switch build)
 * s_active stays 0 and every function below returns what the original
 * expression gave.
 *
 *  - coopStageNumPlayers: stage start (src/boss.c) asks for 2 players.
 *  - coopAiTick: a guard's AI tick runs with the nearest LIVING player as
 *    "Bond" (the AI reads g_CurrentPlayer through getCurrentPlayerProp()).
 *  - coopSimOwner: chrTick runs once per VIEW (propsTick is inside the render
 *    loop), so the sim part of a guard's tick runs for one view per frame only.
 *  - coopFrameTick: enemy health, down + revive.
 */
#include "platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <ultra64.h>
#include "game/chr.h"
#include "game/chraction.h"
#include "game/bondview.h"
#include "game/front.h"
#include "game/lv.h"
#include "game/gun.h"
#include "game/player.h"
#include "game/stan.h"
#include "joy.h"
#include "config.h"
#include "system.h"
#include "coop.h"
#include "bondaicommands.h"   /* if_bond_y_pos_less_than_ID */

extern struct player *g_playerPointers[4];
extern s32 g_ClockTimer;
extern f32 g_AiHealthModifier;
extern s32 g_MainStageNum;
extern int objectiveIsAllComplete(void);
extern int get_mission_state(void);   /* MISSION_STATE_ID (mp_music.c) */
extern int gameScriptedCameraActive(void);
void bondviewFrozenCameraTick(u16 buttons, u16 oldbuttons, struct coord3d *pos, struct coord3d *pos2, struct coord3d *offset, struct StandTile **stan, struct coord3d *arg6);

#define COOP_REVIVE_TICKS   600     /* 10 s at 60 ticks per second */
#define COOP_REVIVE_RADIUS  120.0f  /* horizontal distance to the body, game units */
#define COOP_REVIVE_HEIGHT  100.0f
#define COOP_SPAWN_OFFSET      90.0f  /* player 2's offset from start pad 0, game units */
#define COOP_SPAWN_OFFSET_NEAR 65.0f  /* second try, still outside player 1's radius */

static int s_intent = 0;      /* the mode menu's Co-op entry was chosen (cleared on the mode menu, after a co-op stage) */
static int s_health  = 1;     /* Game.CoopEnemyHealth: 0 = 100 %, 1 = 150 %, 2 = 200 % */
static int s_active  = 0;     /* the running stage is a co-op campaign stage */
static int s_nTaken = 0;   /* per-player pickups taken by one player so far (see coopPickupOp) */
static struct { struct ChrRecord *chr; int player; } s_stick[96];
static int s_spawnLogged = 0;
static int s_sawMp = 0, s_fpDone = 0;   /* the spawn swirl (CAMERAMODE_MP) was seen / the switch to FP was made */
static int s_cineDriver = -1;   /* the player the running cinematic script belongs to, -1 = none latched */

struct CoopRevive {
    int  active;
    f32  timer;       /* ticks left */
    int  lastShown;   /* seconds last put on screen, -1 = none */
};
static struct CoopRevive s_rv[2];

static int coopForced(void)
{
    static int forced = -1;
    if (forced < 0) {
        const char *e = getenv("GE_COOP");
        forced = (e != NULL && e[0] != '\0' && e[0] != '0');
    }
    return forced;
}

static int coopIsMissionStage(s32 stage)
{
    return (stage >= LEVELID_BUNKER1 && stage <= LEVELID_CRADLE) || stage == LEVELID_SURFACE2;
}

/* src/boss.c: stage start. Solo missions arrive with < 2; versus with >= 2. */
s32 coopStageNumPlayers(s32 stage, s32 selected)
{
    int i;
    if (s_intent || coopForced()) {
        sysLogPrintf(LOG_NOTE, "coop: stage load stage=%d selected=%d gamemode=%d selected_num_players=%d intent=%d forced=%d "
                               "player_char=%d/%d pads=%d",
                     (int)stage, (int)selected, (int)gamemode, (int)selected_num_players, s_intent, coopForced(),
                     (int)player_char[0], (int)player_char[1], (int)joyGetControllerCount());
    }
    if (s_active) {
        sysLogPrintf(LOG_NOTE, "coop: leaving a co-op stage, next stage %d (selected %d)", (int)stage, (int)selected);
    }
    s_active = 0;
    s_cineDriver = -1;
    memset(s_stick, 0, sizeof(s_stick));
    s_nTaken = 0;
    s_sawMp = 0;
    s_fpDone = 0;
    s_spawnLogged = 0;
    for (i = 0; i < 2; i++) { s_rv[i].active = 0; s_rv[i].lastShown = -1; }
#if defined(PLATFORM_SWITCH)
    if (selected >= 2 || gamemode != GAMEMODE_SOLO || !coopIsMissionStage(stage)) {
        return selected;
    }
    if (coopForced() || (s_intent && joyGetControllerCount() >= 2)) {
        s_active = 1;
        sysLogPrintf(LOG_NOTE, "coop: co-op stage %d, 2 players", (int)stage);
        return 2;
    }
#else
    (void)stage;
#endif
    return selected;
}

int coopActive(void)
{
    return s_active;
}

/* bondview2.c solo_char_load: which player gets the solo Bond body and head. Player 1 always; player 2 only when
 * no character was chosen (GE_COOP=1 without the menu), otherwise the character picked on the multiplayer
 * character select (get_player_mp_char_head / body, slot 1). */
s32 coopBondBody(void)
{
    int cur = get_cur_playernum();
    if (!s_active) {
        return 0;
    }
    return cur == 0 || player_char[1] < 0;
}

/* initmttex.c: the stage texture pool. When it fills, textures load at the pool start (garbage, D336). A player 2 with a
 * character of their own (menu choice) adds that character's body and head textures, which the level's -mt budget (solo
 * Bond, shared with player 1) does not include. */
#define COOP_TEX_EXTRA (384 * 1024)
s32 coopTexExtra(s32 bytes)
{
    if (s_active && player_char[1] >= 0) {
        sysLogPrintf(LOG_NOTE, "coop: texture pool %d + %d bytes for player 2's character %d", (int)bytes, COOP_TEX_EXTRA,
                     (int)player_char[1]);
        return bytes + COOP_TEX_EXTRA;
    }
    return bytes;
}

s32 coopIsMission(void)
{
    s32 c = getPlayerCount();
    return c == 1 || (s_active && c == 2);
}

s32 coopRuleMulti(void)
{
    return getPlayerCount() >= 2 && !s_active;
}

s32 coopPvpAllowed(void)
{
    return !s_active;
}

/* The player who is not the current one is down (bonddead is non-zero from the moment of death). */
static int coopPartnerDown(void)
{
    struct player *o = g_playerPointers[1 - get_cur_playernum()];
    return o != NULL && o->bonddead != 0;
}

s32 coopDeathFinal(void)
{
    return s_active && coopPartnerDown();
}

void coopVersusDeathState(void)
{
    /* Versus sets the "dead" music state here; co-op keeps the level music while the partner plays on. */
    if (!s_active) {
        set_missionstate(6);
    }
}

f32 coopDownFade(f32 frac)
{
    return (s_active && !coopPartnerDown()) ? 0.0f : frac;     /* no black-out while the partner is up */
}

f32 coopDownChrFade(f32 frac)
{
    return (s_active && !coopPartnerDown()) ? 1.0f : frac;     /* the body stays, the revive needs it */
}

s32 coopRespawnButton(s32 pressed)
{
    return s_active ? 0 : pressed;     /* versus respawn on a button; co-op revives instead */
}

s32 coopSimOwner(void)
{
    if (!s_active) {
        return 1;
    }
    return get_player_position_in_shuffled(get_cur_playernum()) == 0;
}

/* Player 2 starts beside player 1, clear of player 1's collision radius.
 * The offset is walked across the floor tiles from the start pad so the
 * player's tile follows its position; a direction that hits a wall or leaves
 * the floor is skipped. Tries right, left, behind, ahead, far then near. */
void coopSpawnOffset(f32 *x, f32 *z, void *pstan, f32 lookx, f32 lookz)
{
    static const f32 dist[5] = { COOP_SPAWN_OFFSET, COOP_SPAWN_OFFSET_NEAR, 45.0f, 120.0f, 150.0f };
    f32 px, pz;
    StandTile **stan = (StandTile **)pstan;
    f32 len, fx, fz, dx[4], dz[4];
    int d, i;
    if (!s_active || get_cur_playernum() != 1 || stan == NULL || *stan == NULL) {
        return;
    }
    len = sqrtf(lookx * lookx + lookz * lookz);
    if (len < 0.001f) { fx = 0.0f; fz = 1.0f; } else { fx = lookx / len; fz = lookz / len; }
    dx[0] =  fz; dz[0] = -fx;
    dx[1] = -fz; dz[1] =  fx;
    dx[2] = -fx; dz[2] = -fz;
    dx[3] =  fx; dz[3] =  fz;
    px = *x;
    pz = *z;
    for (d = 0; d < 5; d++) {
        for (i = 0; i < 4; i++) {
            StandTile *t = *stan;
            f32 nx = *x + dx[i] * dist[d];
            f32 nz = *z + dz[i] * dist[d];
            if (walkTilesBetweenPoints_NoCallback(&t, *x, *z, nx, nz)) {
                *x = nx; *z = nz; *stan = t;
                if (!s_spawnLogged) { s_spawnLogged = 1; sysLogPrintf(LOG_NOTE, "coop: player 2 spawn: offset %d units from pad 0", (int)dist[d]); }
                return;
            }
        }
    }
    /* No walkable offset: another start pad (its tile is the game's own, so the position is valid), the nearest one at
     * least 100 units from pad 0. */
    {
        int k, best = -1;
        f32 bd = 0.0f;
        for (k = 0; k < startpadcount && k < 16; k++) {
            PadRecord *pad = g_Startpad[k];
            f32 ddx, ddz, dd;
            if (pad == NULL || pad->stan == NULL) {
                continue;
            }
            ddx = pad->pos.f[0] - px;
            ddz = pad->pos.f[2] - pz;
            dd = ddx * ddx + ddz * ddz;
            if (dd >= 100.0f * 100.0f && (best < 0 || dd < bd)) {
                best = k;
                bd = dd;
            }
        }
        if (best >= 0) {
            *x = g_Startpad[best]->pos.f[0];
            *z = g_Startpad[best]->pos.f[2];
            *stan = g_Startpad[best]->stan;
            if (!s_spawnLogged) { s_spawnLogged = 1; sysLogPrintf(LOG_NOTE, "coop: player 2 spawn: start pad %d (no walkable offset from pad 0)", best); }
            return;
        }
    }
    if (!s_spawnLogged) { s_spawnLogged = 1; sysLogPrintf(LOG_NOTE, "coop: player 2 spawn: stacked on pad 0 (no offset and no other start pad)"); }
}

static int coopDriver(void);

/* Nearest living player to a guard; the current player when none is alive. */
static int coopNearestLiving(const struct ChrRecord *chr, int fallback)
{
    int   i, best = -1;
    f32   bd = 0.0f;
    if (chr == NULL || chr->prop == NULL) {
        /* A script chr without a prop (a trigger / background list such as Dam's exit list) has no position: keep the
         * current player if it is alive, else the living one. Never a dead one: "if_bond_is_dead" would send the list
         * to its idle list for good (a solo death ends the mission, so the scripts never expect to continue). */
        struct player *c = (fallback >= 0 && fallback < 2) ? g_playerPointers[fallback] : NULL;
        if (c != NULL && c->bonddead == 0) {
            return fallback;
        }
        for (i = 0; i < 2; i++) {
            if (g_playerPointers[i] != NULL && g_playerPointers[i]->bonddead == 0) {
                return i;
            }
        }
        return fallback;
    }
    for (i = 0; i < 2; i++) {
        struct player *p = g_playerPointers[i];
        f32 dx, dy, dz, d;
        if (p == NULL || p->prop == NULL || p->bonddead != 0) {
            continue;
        }
        dx = p->prop->pos.f[0] - chr->prop->pos.f[0];
        dy = p->prop->pos.f[1] - chr->prop->pos.f[1];
        dz = p->prop->pos.f[2] - chr->prop->pos.f[2];
        d = dx * dx + dy * dy + dz * dz;
        if (best < 0 || d < bd) {
            best = i;
            bd = d;
        }
    }
    return best < 0 ? fallback : best;
}

/* The AI interpreter (chrai.c) calls this at the top of every step. A command that tests "Bond" runs as the current
 * player; when it falls through (false) it is run again as the other living player, and if that one satisfies it the
 * script goes on as that player (so a trigger fires when EITHER player is at the exit pad / below the dam / holds the
 * item, and what the script then locks, moves or fades is that player). If nobody satisfies it the original player is
 * back. offset is the interpreter's Offset; the return value is the (possibly rewound) Offset. */
static int s_inAi;
static struct { void *list; s32 off; int result; } s_aiLog[128];

static void coopLogTest(void *list, s32 off, int result, int player)
{
    int i, slot = -1;
    for (i = 0; i < 128; i++) {
        if (s_aiLog[i].list == list && s_aiLog[i].off == off) {
            slot = i;
            break;
        }
        if (slot < 0 && s_aiLog[i].list == NULL) {
            slot = i;
        }
    }
    if (slot < 0) {
        return;   /* table full: stay silent rather than flood the log */
    }
    if (s_aiLog[slot].list == list && s_aiLog[slot].off == off && s_aiLog[slot].result == result) {
        return;
    }
    s_aiLog[slot].list = list;
    s_aiLog[slot].off = off;
    s_aiLog[slot].result = result;
    sysLogPrintf(LOG_NOTE, "coop: ai player test list=%p offset=%d cmd=%d result=%d player=%d (alive %d/%d)", list, (int)off,
                 (int)((u8 *)list)[off], result, player,
                 g_playerPointers[0] ? g_playerPointers[0]->bonddead == 0 : -1,
                 g_playerPointers[1] ? g_playerPointers[1]->bonddead == 0 : -1);
}
static struct { int pending, rewound, orig; void *list; s32 off; unsigned tried; } s_ai;
extern s32 chraiitemsize(u8 *AIList, s32 offset);

s32 coopAiStep(void *list, s32 offset, s32 isPlayerTest)
{
    if (!s_active || !s_inAi) {
        return offset;
    }
    if (s_ai.pending) {
        if (s_ai.rewound) {          /* this step is the retry itself */
            s_ai.rewound = 0;
            return offset;
        }
        if (list == s_ai.list && offset == s_ai.off + chraiitemsize((u8 *)s_ai.list, s_ai.off)) {   /* it fell through: false */
            int n;
            for (n = 0; n < 2; n++) {
                struct player *p = g_playerPointers[n];
                if (!(s_ai.tried & (1u << n)) && p != NULL && p->prop != NULL && p->bonddead == 0) {
                    s_ai.tried |= 1u << n;
                    set_cur_player(n);
                    s_ai.rewound = 1;
                    return s_ai.off;
                }
            }
            set_cur_player(s_ai.orig);   /* nobody satisfies it */
            coopLogTest(s_ai.list, s_ai.off, 0, s_ai.orig);
        } else {
            coopLogTest(s_ai.list, s_ai.off, 1, get_cur_playernum());   /* jumped: true for the current player */
        }
        s_ai.pending = 0;
    }
    if (isPlayerTest) {
        /* "Bond fell below height Y" (Dam's jump, then the exit camera): ask the LOWEST living player first. Asked in
         * player order, a partner standing low on the ground passed it at once, became the cinematic driver, and the
         * exit camera aimed at him from the jump platform, into the void. */
        if (((u8 *)list)[offset] == if_bond_y_pos_less_than_ID) {
            int n, low = -1;
            for (n = 0; n < 2; n++) {
                struct player *p = g_playerPointers[n];
                if (p != NULL && p->prop != NULL && p->bonddead == 0 &&
                    (low < 0 || p->prop->pos.f[1] < g_playerPointers[low]->prop->pos.f[1])) {
                    low = n;
                }
            }
            if (low >= 0 && low != get_cur_playernum()) {
                set_cur_player(low);
            }
        }
        s_ai.pending = 1;
        s_ai.rewound = 0;
        s_ai.list = list;
        s_ai.off = offset;
        s_ai.orig = get_cur_playernum();
        s_ai.tried = 1u << get_cur_playernum();
    }
    return offset;
}

/*
 * Which player is "Bond" for an NPC's tick. Enemies: the nearest living player, as before. An ESCORT (Natalya, the one ally
 * whose list follows Bond: `if_guard_distance_to_bond_*`, `guard_try_running_to_bond_position`) keeps ONE player while
 * that player is alive: with two players moving, "the nearest" flips from tick to tick, the distance tests and the run
 * target then belong to different players and the escort stops or jitters between them. She only switches when the
 * other player is less than half as far away (or hers is down). The change of the chosen player is logged, at most one
 * line per second.
 */

static int coopIsEscort(const struct ChrRecord *chr)
{
    return chr->bodynum == BODY_Natalya_Skirt || chr->bodynum == BODY_Natalya_Jungle_Fatigues;
}

static f32 coopDist2(const struct player *p, const struct ChrRecord *chr)
{
    f32 dx = p->prop->pos.f[0] - chr->prop->pos.f[0];
    f32 dy = p->prop->pos.f[1] - chr->prop->pos.f[1];
    f32 dz = p->prop->pos.f[2] - chr->prop->pos.f[2];
    return dx * dx + dy * dy + dz * dz;
}

static int coopChooseTarget(struct ChrRecord *chr, int save)
{
    int nearest = coopNearestLiving(chr, save);
    int i, slot = -1, prev = -1, chosen = nearest;
    struct player *pn, *pp;
    if (chr == NULL || chr->prop == NULL) {
        return nearest;
    }
    for (i = 0; i < 96; i++) {
        if (s_stick[i].chr == chr) {
            slot = i;
            prev = s_stick[i].player;
            break;
        }
        if (slot < 0 && s_stick[i].chr == NULL) {
            slot = i;
        }
    }
    if (coopIsEscort(chr) && prev >= 0 && prev != nearest) {
        pp = g_playerPointers[prev];
        pn = g_playerPointers[nearest];
        if (pp != NULL && pp->prop != NULL && pp->bonddead == 0 && pn != NULL && pn->prop != NULL &&
            !(coopDist2(pn, chr) < 0.25f * coopDist2(pp, chr))) {
            chosen = prev;   /* keep her player */
        }
    }
    if (slot >= 0) {
        s_stick[slot].chr = chr;
        s_stick[slot].player = chosen;
    }
    if (chosen != prev) {
        static double last = 0.0;
        struct timespec ts;
        double now;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        now = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
        if (now - last >= 1.0) {
            last = now;
            sysLogPrintf(LOG_NOTE, "coop: chr %d (body %d%s) now follows / targets player %d (was %d)", (int)chr->chrnum,
                         (int)chr->bodynum, coopIsEscort(chr) ? ", escort" : "", chosen, prev);
        }
    }
    return chosen;
}

void coopAiTick(struct ChrRecord *chr, s32 perView)
{
    int save, target;
    if (!s_active) {
        chrlvActionTick(chr);
        return;
    }
    if (perView && !coopSimOwner()) {
        return;     /* the other view of this frame already ran the sim */
    }
    save = get_cur_playernum();
    /* before the tick, which may free the chr; during a cinematic or the level-end wait the driver is "Bond" */
    target = (gameScriptedCameraActive() || stop_time_flag != 0) ? coopDriver() : coopChooseTarget(chr, save);
    if (target != save) {
        set_cur_player(target);
    }
    s_ai.pending = 0;
    s_inAi = 1;
    chrlvActionTick(chr);
    s_inAi = 0;
    s_ai.pending = 0;
    target = get_cur_playernum();   /* the player the script ended up acting as (a test may have switched it) */
    /* A scripted sequence (mission end: lock, walk, fall, fade, camera shots) tests and moves "Bond" over MANY ticks,
     * so it must see the same player in all of them. The first tick after which a cinematic is running belongs to
     * the player that tick ran as (the one who triggered it); that player is the driver until it is over. */
    if (s_cineDriver < 0 && (stop_time_flag != 0 || gameScriptedCameraActive())) {
        s_cineDriver = target;
        sysLogPrintf(LOG_NOTE, "coop: cinematic started, driver latched to player %d", target);
    }
    if (get_cur_playernum() != save) {
        set_cur_player(save);
    }
}

static void coopShow(int player, const char *text)
{
    char buf[48];
    int  save = get_cur_playernum();
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    set_cur_player(player);
    HUDMESSAGEBOTTOM(buf);
    set_cur_player(save);
}

/* The bottom HUD message lives for bondmesscnt ticks after it was set; a negative value hides it. The revive
 * texts used to linger on both views after the revive (or a cancel), so every exit from a revive clears them. */
static void coopClearMsgs(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        if (g_playerPointers[i] != NULL) {
            g_playerPointers[i]->bondmesscnt = -1;
        }
    }
}

static void coopEndRevive(struct CoopRevive *rv)
{
    if (rv->active) {
        rv->active = 0;
        coopClearMsgs();
    }
    rv->lastShown = -1;
}

/* What each player was holding while alive: the game empties both hands once a player has died
 * (bondview2.c, "bonddead && !redbloodfinished"), and a revive has to give them back. */
static s32 s_held[2][2];

static void coopSnapshotHeld(void)
{
    int i, save = get_cur_playernum();
    for (i = 0; i < 2; i++) {
        struct player *p = g_playerPointers[i];
        if (p == NULL || p->bonddead != 0) {
            continue;
        }
        set_cur_player(i);
        s_held[i][GUNRIGHT] = (s32)getCurrentPlayerWeaponId(GUNRIGHT);
        s_held[i][GUNLEFT] = (s32)getCurrentPlayerWeaponId(GUNLEFT);
    }
    set_cur_player(save);
}

#define COOP_PLAYER_SEP 70.0f   /* two players closer than this (horizontally) overlap and block each other */

/* Put a player on a spot, the fields mp_respawn_handler sets for a spawn (height, collision, prop, room tile). */
static void coopPlaceAt(int idx, f32 x, f32 z, StandTile *tile)
{
    struct player *p = g_playerPointers[idx];
    int            save = get_cur_playernum();
    struct coord3d pos;
    f32            h;
    set_cur_player(idx);
    h = bondviewYPositionRelated(tile, x, z);
    pos.f[0] = x;
    pos.f[1] = p->eyeheight + h;
    pos.f[2] = z;
    p->field_70 = h;
    p->stanHeight = h;
#if defined(VERSION_EU)
    p->field_6C = h / 0.2004f;
#else
    p->field_6C = h / 0.17000002f;
#endif
    change_player_pos_to_target(&p->field_488, &pos, tile);
    p->prop->pos.f[0] = p->bondprevpos.f[0] = pos.f[0];
    p->prop->pos.f[1] = p->bondprevpos.f[1] = pos.f[1];
    p->prop->pos.f[2] = p->bondprevpos.f[2] = pos.f[2];
    p->prop->stan = tile;
#if defined(VERSION_EU)
    p->field_3B8.f[0] = p->field_488.pos.f[0] / 0.118799984f;
    p->field_3B8.f[1] = p->field_488.pos.f[1] / 0.118799984f;
    p->field_3B8.f[2] = p->field_488.pos.f[2] / 0.118799984f;
#else
    p->field_3B8.f[0] = p->field_488.pos.f[0] / 0.100000024f;
    p->field_3B8.f[1] = p->field_488.pos.f[1] / 0.100000024f;
    p->field_3B8.f[2] = p->field_488.pos.f[2] / 0.100000024f;
#endif
    set_cur_player(save);
}

/* The ring of points `r` around (x, z) must all be walkable from the tile at (x, z): the player's collision radius
 * then clears every tile edge (the walk test alone only proves a segment lies on the tile mesh). */
static int coopWallClear(StandTile *tile, f32 x, f32 z, f32 r)
{
    static const f32 kC[8] = { 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f, 0.0f, 0.70710678f };
    static const f32 kS[8] = { 0.0f, 0.70710678f, 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f };
    int k;
    for (k = 0; k < 8; k++) {
        StandTile *t = tile;
        if (!walkTilesBetweenPoints_NoCallback(&t, x, z, x + kC[k] * r, z + kS[k] * r)) {
            return 0;
        }
    }
    return 1;
}

#define COOP_WALL_MARGIN 40.0f

/* A spot for `mover` at COOP_PLAYER_SEP or more from the body, reached by walking from the body's tile, clear of walls
 * by COOP_WALL_MARGIN; the direction the mover already is from the body is tried first, then around it. */
static int coopFindSpotAway(const struct player *body, const struct player *mover, f32 *rx, f32 *rz, StandTile **rt)
{
    static const f32 kC[8] = { 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f, 0.0f, 0.70710678f };
    static const f32 kS[8] = { 0.0f, 0.70710678f, 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f };
    static const f32 kDist[5] = { 70.0f, 85.0f, 100.0f, 130.0f, 160.0f };
    f32 bx, bz, ux, uz, len;
    int d, k;
    if (body == NULL || mover == NULL || body->prop == NULL || mover->prop == NULL || body->prop->stan == NULL) {
        return 0;
    }
    bx = body->prop->pos.f[0];
    bz = body->prop->pos.f[2];
    ux = mover->prop->pos.f[0] - bx;
    uz = mover->prop->pos.f[2] - bz;
    len = sqrtf(ux * ux + uz * uz);
    if (len < 0.001f) { ux = 1.0f; uz = 0.0f; } else { ux /= len; uz /= len; }
    for (d = 0; d < 5; d++) {
        for (k = 0; k < 8; k++) {
            StandTile *t = body->prop->stan;
            f32 dx = ux * kC[k] - uz * kS[k];
            f32 dz = ux * kS[k] + uz * kC[k];
            f32 nx = bx + dx * kDist[d];
            f32 nz = bz + dz * kDist[d];
            if (walkTilesBetweenPoints_NoCallback(&t, bx, bz, nx, nz) && coopWallClear(t, nx, nz, COOP_WALL_MARGIN)) {
                *rx = nx;
                *rz = nz;
                *rt = t;
                return 1;
            }
        }
    }
    return 0;
}

static void coopRevive(int d)
{
    struct player *p = g_playerPointers[d];
    struct player *r = g_playerPointers[1 - d];
    int save = get_cur_playernum();
    f32 sx, sz;
    StandTile *st;

    /* Stand up exactly where the body lies (a position that was valid when the player died, so never inside a wall);
     * if the reviver overlaps it, move the REVIVER aside to a spot that is walkable and clear of walls. */
    if (r != NULL && r->prop != NULL && p->prop != NULL) {
        f32 ox = r->prop->pos.f[0] - p->prop->pos.f[0];
        f32 oz = r->prop->pos.f[2] - p->prop->pos.f[2];
        if (ox * ox + oz * oz < COOP_PLAYER_SEP * COOP_PLAYER_SEP && coopFindSpotAway(p, r, &sx, &sz, &st)) {
            coopPlaceAt(1 - d, sx, sz, st);
        }
    }

    set_cur_player(d);
    p->bonddead = 0;
    p->deathanimfinished = 0;
    p->redbloodfinished = 0;
    p->startnewbonddie = 1;        /* the next death plays a fresh animation (as a versus respawn) */
    p->bondhealth = 0.5f;
    p->oldhealth = 0.5f;
    p->apparenthealth = 0.5f;
    p->bondarmour = 0.0f;
    p->oldarmour = 0.0f;
    p->apparentarmour = 0.0f;
    p->damageshowtime = -1;
    p->healthshowtime = -1;
    p->crouchpos = CROUCH_STAND;
    p->autocrouchpos = CROUCH_STAND;
    p->ducking_height_offset = 0.0f;
    /* The death fade and the red tint, as init_player_BONDdata leaves them. */
    p->colourscreenfrac = 0.0f;
    p->colourfadetime60 = -1.0f;
    p->colourfadetimemax60 = -1.0f;
    p->bondfadetime60 = -1.0f;
    p->bondfadetimemax60 = -1.0f;
    if (p->prop != NULL && p->prop->chr != NULL) {
        p->prop->chr->fadealpha = 255;
    }
    /* mp_respawn_handler's HUD / gun state, and the weapons the death emptied. */
    p->healthdamagetype = 7;
    p->damagetype = 7;
    p->gunammooff = 0;
    p->gunsightmode = 2;
    p->hand_invisible[GUNRIGHT] = 0;
    p->hand_invisible[GUNLEFT] = 0;
    p->insightaimmode = FALSE;
    currentPlayerEquipWeaponWrapper(GUNRIGHT, s_held[d][GUNRIGHT] != 0 ? s_held[d][GUNRIGHT] : ITEM_FIST);
    currentPlayerEquipWeaponWrapper(GUNLEFT, s_held[d][GUNLEFT]);
    set_cur_player(save);
}

static void coopReviveTick(void)
{
    int d;
    if (g_ClockTimer <= 0) {
        return;     /* paused or controls locked */
    }
    for (d = 0; d < 2; d++) {
        struct player *dn = g_playerPointers[d];
        struct player *pt = g_playerPointers[1 - d];
        struct CoopRevive *rv = &s_rv[d];
        f32 dx, dy, dz;
        int near;

        if (dn == NULL || pt == NULL || dn->prop == NULL || pt->prop == NULL) {
            continue;
        }
        if (dn->bonddead == 0 || pt->bonddead != 0) {
            coopEndRevive(rv);   /* the player is up (or the partner is down too): nothing to revive */
            continue;
        }
        dx = pt->prop->pos.f[0] - dn->prop->pos.f[0];
        dy = pt->prop->pos.f[1] - dn->prop->pos.f[1];
        dz = pt->prop->pos.f[2] - dn->prop->pos.f[2];
        near = (dx * dx + dz * dz) < (COOP_REVIVE_RADIUS * COOP_REVIVE_RADIUS) &&
               dy < COOP_REVIVE_HEIGHT && dy > -COOP_REVIVE_HEIGHT;

        if (!rv->active) {
            /* Switch A is the N64 B button on a second player's pad (use / open). */
            if (near && dn->bonddead != 0 && dn->deathanimfinished &&
                (joyGetButtonsPressedThisFrame((s8)(1 - d), B_BUTTON) & B_BUTTON)) {
                rv->active = 1;
                rv->timer = (f32)COOP_REVIVE_TICKS;
                rv->lastShown = -1;
            }
        } else if (!near) {
            coopEndRevive(rv);   /* walked away: the countdown text goes with it */
        }
        if (rv->active) {
            int secs;
            rv->timer -= (f32)g_ClockTimer;
            if (rv->timer <= 0.0f) {
                coopEndRevive(rv);
                coopRevive(d);
                continue;
            }
            secs = (int)((rv->timer + 59.0f) / 60.0f);
            if (secs != rv->lastShown) {
                char buf[32];
                rv->lastShown = secs;
                snprintf(buf, sizeof(buf), "Reviving: %d", secs);
                coopShow(1 - d, buf);
                snprintf(buf, sizeof(buf), "Being revived: %d", secs);
                coopShow(d, buf);
            }
        }
    }
}

/*
 * Pause and watch. In a 2-player stage the game's own START handler would open the versus watch (Scores / Pause /
 * Exit); co-op wants the SOLO watch (inventory, objectives, mission status, abort). Hooks: START opens the solo
 * watch for player 1 only (GE_WATCH_START; the watch screens read PLAYER_1's pad, options.c), mpwatchMenuTick is
 * turned off (mpmenu.c 589), and the controls lock the solo watch raises while it is open is not cleared by the
 * other player's tick (GE_WATCH_LOCK: bondview2.c clears it every call when THAT player's watch is closed).
 * lvlManageMpGame turns the lock into g_ClockTimer = 0, so both players are paused while the watch is up.
 */
s32 coopWatchStart(void)
{
    /* Each player opens their own watch, but only one at a time: the watch screens keep their state (selected page,
     * inventory cursor, abort prompt) in globals. */
    struct player *o;
    int cur = get_cur_playernum();
    if (!s_active || cur < 0 || cur > 1) {
        return 0;
    }
    o = g_playerPointers[1 - cur];
    return o == NULL || o->watch_animation_state == WATCH_ANIMATION_0x0;
}

/* options.c: every watch screen reads this pad (it used to read PLAYER_1's). The watch code runs in the tick of
 * the player whose watch it is, so that is the current player. */
/* propobj.c objTick: in a 2+ player stage an in-flight projectile (a dropped gun, a thrown grenade) is simulated
 * only in the view of the player who owns it, which in versus is always a player. A guard-owned one (dropped
 * weapons, guard grenades and rockets) matched no view, so it was never simulated: guns did not fall. In co-op a
 * projectile owned by anything but a player is simulated by the usual one-view-per-frame owner. */
s32 coopProjSimOwner(s32 ownedByCurrent, struct PropRecord *ownerprop, s32 viewOwner)
{
    if (s_active && ownerprop != NULL && ownerprop != g_playerPointers[0]->prop &&
        (g_playerPointers[1] == NULL || ownerprop != g_playerPointers[1]->prop)) {
        return viewOwner;
    }
    return ownedByCurrent;
}

/*
 * A co-op player's body is a real chr (the partner must be visible), with a model, so the engine's "nearest guard"
 * search (chraction.c sub_GAME_7F033B38, AI command guard_try_setting_chr_preset_to_guard_within_distance) could pick
 * it. That is how Natalya in the Jungle took a Bond as her target (her "mdk" list, ai_8, fires at that chr preset),
 * and kept firing at the body after death: a player's chr never reaches ACT_DEAD, so chrIsDead() stays false. In solo
 * the player's body is removed after the intro (model NULL) and the search never sees it. Hook GE_CHR_PICKABLE.
 */
s32 coopChrIsPlayer(struct ChrRecord *chr)
{
    return s_active && chr != NULL && chr->prop != NULL && chr->prop->type == PROP_TYPE_VIEWER;
}

/*
 * Objectives "collect object" / "deposit object" (objective_status.c) test bondinvHasPropInInv, i.e. g_CurrentPlayer's
 * inventory, and the status is polled from both players' ticks: with the item in one player's inventory the result
 * flipped every frame (Bunker 1 on the console: objectives_complete 0/1 thousands of times, and the end-of-mission
 * exit never completed). In co-op the question is "does either player hold it" (the object is unique, so only one
 * player can). Hook GE_HAS_PROP_IN_INV. Only g_CurrentPlayer is swapped (bondinvHasPropInInv reads nothing else).
 */
extern bool bondinvHasPropInInv(PropRecord *prop);
s32 coopAnyHasPropInInv(struct PropRecord *prop)
{
    struct player *saved = g_CurrentPlayer;
    int i, has = 0;
    if (!s_active) {
        return bondinvHasPropInInv(prop);
    }
    for (i = 0; i < 2 && !has; i++) {
        if (g_playerPointers[i] != NULL && g_playerPointers[i]->ptr_inventory_first_in_cycle != NULL) {
            g_CurrentPlayer = g_playerPointers[i];
            has = bondinvHasPropInInv(prop);
        }
    }
    g_CurrentPlayer = saved;
    return has;
}

/*
 * NPC shots at a player (chraction.c chrlvUpdateShotbondsum -> GE_NPC_HURT): logged, at most one line per second,
 * so a console log shows who hurts whom: "coop: player N hurt by chr C body B ailist P offset O weapon W attacktype T dmg D".
 */
void coopNpcHurt(struct ChrRecord *chr, f32 damage, f32 subroty, s32 item)
{
    if (s_active) {
        static double last = 0.0;
        struct timespec ts;
        double now;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        now = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
        if (now - last >= 1.0) {
            last = now;
            sysLogPrintf(LOG_NOTE, "coop: player %d hurt by chr %d body %d ailist %p offset %d weapon %d attacktype %d dmg %.3f",
                         get_cur_playernum(), (int)chr->chrnum, (int)chr->bodynum, (void *)chr->ailist, (int)chr->aioffset, (int)item,
                         chr->actiontype == ACT_ATTACK ? (int)chr->act_attack.attacktype : -1, (double)damage);
        }
    }
    bondviewCallRecordDamageKills(damage, subroty, -1, 1);
}

/*
 * Per-player pickups. Every collectable that the original code frees on pickup (ammo crates and loose ammo, body armour,
 * weapons lying in the level and guard-dropped ones: propobj.c propPickupByPlayer, op == TICKOP_FREE) exists once per
 * player in co-op: the player who collects it gets it exactly as in solo (the item code runs for g_CurrentPlayer), the
 * prop is NOT freed, it is marked "taken by player N" here; it then disappears from that player's view (objTick:
 * GE_PICKUP_VISIBLE) and cannot be collected again by that player (objTickPlayer: GE_PICKUP_TAKEN) but stays for the other
 * one; when the other player collects it too the prop is freed as the original code does. Keys and the other objects
 * that move into an inventory (TICKOP_GIVETOPLAYER, INV_ITEM_PICKUP: keycards, objective items, tagged props) stay a
 * single prop, because a world prop can be in only one inventory list and cloning an arbitrary object record is not
 * possible from here; doors check the keys of EITHER player (GE_HAS_KEY_FLAGS) and the objective code already asks
 * "either player holds it" (GE_HAS_PROP_IN_INV), so shared objective items stay correct.
 */
struct CoopTaken {
    struct PropRecord *prop;
    struct ObjectRecord *obj;
    s32 type, sub;   /* object type and weapon number / amount: a reused prop slot holds a different object */
    unsigned mask;
};

/* What identifies a pickup besides its prop and object record. NOT its position: guard-dropped weapons fall and
 * settle, and a position test made every frame of the fall look like a new pickup (the console log showed one player
 * taking the same gun 12 and 15 times in a row). */
static s32 coopPickupSub(struct ObjectRecord *obj)
{
    switch (obj->type) {
        case PROPDEF_COLLECTABLE: return ((struct WeaponObjRecord *)obj)->weaponnum;
        case PROPDEF_MAGAZINE:    return ((struct AmmoCrateRecord *)obj)->ammoType;
        case PROPDEF_ARMOUR:      return (s32)((struct BodyArmourRecord *)obj)->amount;
        default:                  return 0;
    }
}
static struct CoopTaken s_taken[256];

static struct CoopTaken *coopFindTaken(struct PropRecord *prop)
{
    int i;
    for (i = 0; i < s_nTaken; i++) {
        struct CoopTaken *e = &s_taken[i];
        if (e->prop == prop) {
            struct ObjectRecord *obj = prop->obj;
            /* a freed and reused prop slot is not the same pickup: same object record, type and weapon/amount */
            if (obj != e->obj || obj == NULL || obj->type != e->type || coopPickupSub(obj) != e->sub) {
                s_taken[i] = s_taken[--s_nTaken];
                return NULL;
            }
            return e;
        }
    }
    return NULL;
}

/* chrprop.c chrpropFree: a prop goes back to the pool, so its address may be handed to another object next; forget
 * any per-player pickup state for it. The table is keyed by the prop's address; the object/type/weapon test in
 * coopFindTaken stays as a second guard for a free that would not go through chrpropFree. */
void coopPropFreed(struct PropRecord *prop)
{
    int i;
    for (i = 0; i < s_nTaken; i++) {
        if (s_taken[i].prop == prop) {
            s_taken[i] = s_taken[--s_nTaken];
            return;
        }
    }
}

s32 coopPickupTaken(struct PropRecord *prop)
{
    struct CoopTaken *e;
    int cur;
    if (!s_active || s_nTaken == 0) {
        return 0;
    }
    cur = get_cur_playernum();
    if (cur < 0 || cur > 1) {
        return 0;
    }
    e = coopFindTaken(prop);
    return e != NULL && (e->mask & (1u << cur)) != 0;
}

/* propobj.c propPickupByPlayer, between the item code and the free: op is what the original code decided. */
s32 coopPickupOp(struct PropRecord *prop, s32 op)
{
    struct ObjectRecord *obj;
    struct CoopTaken *e;
    int cur, other;
    char what[40];
    if (!s_active || op != TICKOP_FREE || getPlayerCount() != 2) {
        return op;
    }
    obj = prop->obj;
    if (obj->runtime_bitflags & RUNTIMEBITFLAG_TAGGED) {
        return op;   /* objective / script props stay single */
    }
    cur = get_cur_playernum();
    if (cur < 0 || cur > 1) {
        return op;
    }
    other = 1 - cur;
    e = coopFindTaken(prop);
    if (e == NULL) {
        if (s_nTaken >= 256) {
            return op;   /* table full: behave like solo, nothing is doubled */
        }
        e = &s_taken[s_nTaken++];
        e->prop = prop;
        e->obj = obj;
        e->type = obj->type;
        e->sub = coopPickupSub(obj);
        e->mask = 0;
    }
    e->mask |= 1u << cur;
    switch (obj->type) {
        case PROPDEF_COLLECTABLE: snprintf(what, sizeof(what), "weapon %d", (int)((struct WeaponObjRecord *)obj)->weaponnum); break;
        case PROPDEF_MAGAZINE:    snprintf(what, sizeof(what), "ammo crate type %d", (int)((struct AmmoCrateRecord *)obj)->ammoType); break;
        case PROPDEF_AMMO:        snprintf(what, sizeof(what), "ammo box"); break;
        case PROPDEF_ARMOUR:      snprintf(what, sizeof(what), "body armour %d", (int)((struct BodyArmourRecord *)obj)->amount); break;
        default:                  snprintf(what, sizeof(what), "object type %d", (int)obj->type); break;
    }
    if (e->mask & (1u << other)) {
        sysLogPrintf(LOG_NOTE, "coop: player %d took %s @%p (other player: also taken -> removed)", cur, what, (void *)prop);
        *e = s_taken[--s_nTaken];   /* the prop is freed now: forget it */
        return op;
    }
    sysLogPrintf(LOG_NOTE, "coop: player %d took %s @%p (other player: still available)", cur, what, (void *)prop);
    return TICKOP_NONE;   /* keep the prop for the other player */
}

/* propobj.c door key test: the keys of either player open a door. */
extern bool bondinvCheckHasKeyFlags(u32 wantkeyflags);
s32 coopHasKeyFlags(u32 flags)
{
    int i, save;
    s32 ret = 0;
    if (!s_active) {
        return bondinvCheckHasKeyFlags(flags);
    }
    save = get_cur_playernum();
    for (i = 0; i < 2 && !ret; i++) {
        if (g_playerPointers[i] != NULL) {
            set_cur_player(i);
            ret = bondinvCheckHasKeyFlags(flags) != 0;
        }
    }
    set_cur_player(save);
    return ret;
}

s32 coopWatchPad(void)
{
    int cur = get_cur_playernum();
    return (s_active && cur >= 0 && cur < 4) ? cur : 0;
}

s32 coopWatchLock(s32 lock)
{
    int i;
    if (!s_active) {
        return lock;
    }
    for (i = 0; i < 2; i++) {
        struct player *p = g_playerPointers[i];
        if (p != NULL && (p->watch_animation_state == WATCH_ANIMATION_0x5 || p->watch_animation_state == WATCH_ANIMATION_0xc)) {
            return 1;
        }
    }
    return lock;
}

/*
 * Cinematics (scripted end-of-mission cameras, the death cam): g_CameraMode and its helpers are global and
 * advanced by whichever player's tick runs. In co-op player 1 (or the surviving player) is the DRIVER: scripted AI
 * runs as the driver, the camera is computed once for the driver, and the other view copies it.
 */
static int coopDriver(void)
{
    struct player *p0 = g_playerPointers[0];
    if (s_cineDriver >= 0 && g_playerPointers[s_cineDriver] != NULL) {
        return s_cineDriver;
    }
    struct player *p1 = g_playerPointers[1];
    if (p0 != NULL && p0->bonddead == 0) {
        return 0;
    }
    if (p1 != NULL && p1->bonddead == 0) {
        return 1;
    }
    return 0;
}

static int coopMirrorCamera(void)
{
    u32 m = bondviewGetCameraMode();
    return gameScriptedCameraActive() || m == CAMERAMODE_DEATH_CAM_SP || m == CAMERAMODE_DEATH_CAM_MP;
}

static struct {
    int            valid;
    struct coord3d pos, pos2, offset, walk;
    StandTile     *stan;
} s_cam;

void coopFrozenCamera(u16 buttons, u16 oldbuttons, struct coord3d *pos, struct coord3d *pos2, struct coord3d *offset,
                      struct StandTile **stan, struct coord3d *arg6)
{
    int cur;
    if (!s_active || !coopMirrorCamera()) {
        s_cam.valid = 0;
        bondviewFrozenCameraTick(buttons, oldbuttons, pos, pos2, offset, stan, arg6);
        return;
    }
    cur = get_cur_playernum();
    if (cur != coopDriver() && s_cam.valid) {
        *pos = s_cam.pos;
        *pos2 = s_cam.pos2;
        *offset = s_cam.offset;
        *arg6 = s_cam.walk;
        *stan = s_cam.stan;
        return;
    }
    bondviewFrozenCameraTick(buttons, oldbuttons, pos, pos2, offset, stan, arg6);
    if (cur == coopDriver()) {
        s_cam.pos = *pos;
        s_cam.pos2 = *pos2;
        s_cam.offset = *offset;
        s_cam.walk = *arg6;
        s_cam.stan = *stan;
        s_cam.valid = 1;
    }
}

/*
 * One full-screen view while a cinematic camera or the watch is up (GE_COOP_SPLIT=1 keeps the split). The player
 * who owns the picture (the one whose watch is open, else the cinematic driver) gets the solo viewport sizes
 * (GE_SPLIT_GE2 / GE_SPLIT_EQ2 in the viewport getters, bondview2.c) and is drawn LAST (coopViewOrder, boss.c,
 * after the per-frame shuffle), so it covers the other half. The other view still ticks and renders underneath:
 * every per-view sim/tick path keeps running, nothing is skipped, and no buffer sized for the split changes.
 */
static int coopForceSplit(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("GE_COOP_SPLIT");
        on = (e != NULL && e[0] != 0 && e[0] != '0');
    }
    return on;
}

static int coopFullPlayer(void)
{
    int i;
    if (!s_active || coopForceSplit() || getPlayerCount() != 2) {
        return -1;
    }
    for (i = 0; i < 2; i++) {
        struct player *p = g_playerPointers[i];
        if (p != NULL && p->watch_animation_state != WATCH_ANIMATION_0x0) {
            return i;
        }
    }
    /* Cinematics are full screen (the driver's view) unless GE_COOP_CINEFULL=0. It was off after the round-4
     * end-of-mission hang, whose real causes (the FP switch after the swirl, any-player AI tests) are fixed since. */
    {
        static int cine = -1;
        if (cine < 0) {
            const char *e = getenv("GE_COOP_CINEFULL");
            cine = !(e != NULL && e[0] == '0');
        }
        return (cine && coopMirrorCamera()) ? coopDriver() : -1;
    }
}

s32 coopSplitView(void)
{
    int f = coopFullPlayer();
    return !(f >= 0 && f == get_cur_playernum());
}

void coopViewOrder(void)
{
    int f = coopFullPlayer(), pf = -1, po = -1, i;
    if (f < 0) {
        return;
    }
    for (i = 0; i < 4; i++) {
        if (array_PLAYER_IDs[i] == f) {
            pf = i;
        } else if (array_PLAYER_IDs[i] == 1 - f) {
            po = i;
        }
    }
    if (pf >= 0 && po >= 0 && pf < po) {
        PLAYER_ID t = array_PLAYER_IDs[pf];
        array_PLAYER_IDs[pf] = array_PLAYER_IDs[po];
        array_PLAYER_IDs[po] = t;
    }
}

/* The scripted screen fades (AI_ScreenFadeToBlack ...) hit the driver only; the other view follows. */
static void coopMirrorFade(void)
{
    int d = coopDriver();
    struct player *src = g_playerPointers[d];
    struct player *dst = g_playerPointers[1 - d];
    if (src == NULL || dst == NULL || !coopMirrorCamera() || stop_time_flag == 2) {
        return;
    }
    dst->colourscreenred = src->colourscreenred;
    dst->colourscreengreen = src->colourscreengreen;
    dst->colourscreenblue = src->colourscreenblue;
    dst->colourscreenfrac = src->colourscreenfrac;
    dst->colourfadetime60 = src->colourfadetime60;
    dst->colourfadetimemax60 = src->colourfadetimemax60;
    dst->colourfaderedold = src->colourfaderedold;
    dst->colourfaderednew = src->colourfaderednew;
    dst->colourfadegreenold = src->colourfadegreenold;
    dst->colourfadegreennew = src->colourfadegreennew;
    dst->colourfadeblueold = src->colourfadeblueold;
    dst->colourfadebluenew = src->colourfadebluenew;
    dst->colourfadefracold = src->colourfadefracold;
    dst->colourfadefracnew = src->colourfadefracnew;
}

/* src/game/lv.c, once per frame right after lvlSetMultipliersForDifficulty (which resets g_AiHealthModifier). */
/* Permanent diagnostics: one "coop:" log line per CHANGE of a state the end-of-mission / pause logic depends on
 * (not per frame), so a hang leaves the last state reached in the log. */
static void coopLogChanges(void)
{
    static int init = 0;
    static s32 mission, cam, stopt, lock, mainstage, full, driver, w0, w1, d0, d1, od;
    s32 nmission = (s32)get_mission_state(), ncam = (s32)bondviewGetCameraMode(), nstop = stop_time_flag;
    s32 nlock = lvlGetControlsLockedFlag(), nmain = g_MainStageNum, nfull = coopFullPlayer(), ndriver = coopDriver();
    s32 nw0 = g_playerPointers[0] ? (s32)g_playerPointers[0]->watch_animation_state : -1;
    s32 nw1 = g_playerPointers[1] ? (s32)g_playerPointers[1]->watch_animation_state : -1;
    s32 nd0 = g_playerPointers[0] ? (s32)g_playerPointers[0]->bonddead : -1;
    s32 nd1 = g_playerPointers[1] ? (s32)g_playerPointers[1]->bonddead : -1;
    s32 nod = (s32)objectiveIsAllComplete();
    if (init && nmission == mission && ncam == cam && nstop == stopt && nlock == lock && nmain == mainstage &&
        nfull == full && ndriver == driver && nw0 == w0 && nw1 == w1 && nd0 == d0 && nd1 == d1 && nod == od) {
        return;
    }
    sysLogPrintf(LOG_NOTE, "coop: state missionstate=%d cameramode=%d stop_time_flag=%d locked=%d mainstage=%d fullscreen=%d driver=%d "
                           "watch=%d/%d dead=%d/%d scripted=%d objectives_complete=%d",
                 (int)nmission, (int)ncam, (int)nstop, (int)nlock, (int)nmain, (int)nfull, (int)ndriver, (int)nw0, (int)nw1,
                 (int)nd0, (int)nd1, gameScriptedCameraActive(), (int)nod);
    init = 1;
    mission = nmission; cam = ncam; stopt = nstop; lock = nlock; mainstage = nmain; full = nfull; driver = ndriver;
    w0 = nw0; w1 = nw1; d0 = nd0; d1 = nd1; od = nod;
}

void coopFrameTick(void)
{
    static const f32 kHealth[3] = { 1.0f, 1.5f, 2.0f };
    if (!s_active) {
        return;
    }
    if (s_cineDriver >= 0 && stop_time_flag == 0 && !gameScriptedCameraActive()) {
        s_cineDriver = -1;
    }
    coopLogChanges();
    if (getPlayerCount() != 2) {
        return;
    }
    /* A solo mission leaves its intro in CAMERAMODE_FP; the versus swirl co-op starts with leaves CAMERAMODE_NONE. The
     * mission scripts depend on the difference: hud_hide_and_lock_controls sets is_timer_active = 0, which only moves a
     * player onto the frozen path (bond_set_locked_velocity walks Bond off the dam, the controls are locked, the
     * end-of-mission camera runs) in mode FP; in mode NONE (bondview2.c 8201) the player stays free and the Dam jump
     * script waits for a fall that never happens. Switch to FP once, for both players, when the swirl is over. */
    {
        u32 m = bondviewGetCameraMode();
        if (m == CAMERAMODE_MP) {
            s_sawMp = 1;
        } else if (s_sawMp && !s_fpDone && m == CAMERAMODE_NONE) {
            int i, save = get_cur_playernum();
            s_fpDone = 1;
            for (i = 0; i < 2; i++) {
                if (g_playerPointers[i] != NULL) {
                    set_cur_player(i);
                    bondviewSetCameraMode(CAMERAMODE_FP);
                }
            }
            set_cur_player(save);
            sysLogPrintf(LOG_NOTE, "coop: spawn swirl over, camera mode set to FP like a solo mission");
        }
    }
    /* A lower g_AiHealthModifier makes the player's shots weaker against guards. */
    g_AiHealthModifier /= kHealth[(s_health < 0 || s_health > 2) ? 1 : s_health];
    coopMirrorFade();
    coopReviveTick();
    coopSnapshotHeld();
}

/*
 * Mode menu (front.c, "Co-op" between Select Mission and Multiplayer) and the character select for player 2.
 * Choosing Co-op runs the multiplayer character select for two players with slot 0 pre-selected as Bond
 * (mp_chr_setup[0]) and only slot 1 free (pad 2 picks; a duplicate of Bond is refused by the screen itself), then
 * continues exactly like Select Mission with gamemode solo; the intent makes the next stage start a co-op stage.
 * B on the screen goes back to the mode menu. The multiplayer state the screen borrows (gamemode,
 * selected_num_players) is put back on every exit.
 */
extern GAMEMODE gamemode;
extern s32 selected_num_players;
extern s32 player_char[];
extern s32 player_has_selected_char[];
extern s32 size_mp_select_image_player[];
extern s32 mp_char_cur_select_player[4];
extern s32 mp_char_prev_select_player[4];
extern s32 mp_char_select_scroll_offset[4];
extern void frontChangeMenu(MENU menu, s32 reload);
extern void set_cursor_to_stage_solo(int level);
extern void setCursorPOSforMode(int mode);
static int s_chosen = 0, s_charsel = 0, s_coopCursor = 0;
static s32 s_savedNum = 0;

/* front.c setCursorPOSforMode(0): the cursor goes back to the CO-OP row when co-op was the choice (mission select or the
 * character select was left with B); init_menu06_modeselect clears it after the cursor has been placed. */
int coopMenuIntent(void)
{
    return s_intent || s_coopCursor;
}

void coopMenuClear(void)
{
    s_coopCursor = 0;
    s_intent = 0;
    s_chosen = 0;
    if (s_charsel) {
        s_charsel = 0;
        selected_num_players = s_savedNum;
    }
}

void coopMenuSelect(void)
{
    s_chosen = 1;
}

int coopMenuTake(void)
{
    if (!s_chosen) {
        return 0;
    }
    s_chosen = 0;
    s_intent = 0;
    s_charsel = 1;
    s_savedNum = selected_num_players;
    gamemode = GAMEMODE_MULTI;     /* the character select asks get_selected_num_players() */
    selected_num_players = 2;
    player_char[0] = 0;            /* Bond */
    if (player_char[1] < 0 || player_char[1] == 0) {
        player_char[1] = 1;
    }
    return 1;
}

/* Start of interface_menu0F_mpcharsel: slot 0 stays ready as Bond, B leaves. Returns 1 when the frame is done. */
int coopCharSelectPre(void)
{
    if (!s_charsel) {
        return 0;
    }
    if (joyGetButtonsPressedThisFrame(0, B_BUTTON) ||
        (!player_has_selected_char[1] && joyGetButtonsPressedThisFrame(1, B_BUTTON))) {
        s_charsel = 0;
        s_coopCursor = 1;
        selected_num_players = s_savedNum;
        gamemode = GAMEMODE_INTRO;
        frontChangeMenu(MENU_MODE_SELECT, 0);
        setCursorPOSforMode(0);
        return 1;
    }
    player_char[0] = 0;
    player_has_selected_char[0] = 1;
    size_mp_select_image_player[0] = 0xb;
    mp_char_cur_select_player[0] = 0;
    mp_char_prev_select_player[0] = 0;
    mp_char_select_scroll_offset[0] = 0;
    return 0;
}

/* End of interface_menu0F_mpcharsel, both ready: on to the mission select instead of the multiplayer options. */
int coopCharSelectDone(void)
{
    if (!s_charsel) {
        return 0;
    }
    s_charsel = 0;
    s_intent = 1;
    selected_num_players = s_savedNum;
    gamemode = GAMEMODE_SOLO;
    frontChangeMenu(MENU_MISSION_SELECT, 0);
    set_cursor_to_stage_solo(0);
    return 1;
}

#if defined(PLATFORM_SWITCH)
PD_CONSTRUCTOR static void coopRegisterConfig(void)
{
    configRegisterInt("Game.CoopEnemyHealth", &s_health, 0, 2);
}
#endif
