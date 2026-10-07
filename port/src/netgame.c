/*
 * netgame.c -- online multiplayer: game-side glue (D413, D414).
 *
 * See port/include/netgame.h for the contract and docs/dev/NETPLAY-PLAN.md
 * for the design. Summary of the match lifecycle (all on the game thread):
 *
 *   front end (title stage)
 *     netgameGameTick(): a MATCH_START arrived -> remember the user's own MP
 *       setup, write the agreed one, frontChangeMenu(MENU_RUN_STAGE) -- the
 *       exact route the MP options screen's "start" tab takes.
 *   bossMainloop, top of the per-stage loop
 *     netgameOnStageLoad(stage): before the stage's first PRNG draw, write
 *       every piece of state the RAMROM replay system says determines a stage
 *       (seeds, MP setup, save options via the RAM-only folder 100, cheats),
 *       reset frame counters / player perm data, install the joy playback
 *       hook, enable the frame-locked clock, pin the port knobs.
 *   every frame, joyConsumeSamplesWrapper() -> netgamePlaybackFunc()
 *       frame 0: report loaded, wait for the start barrier;
 *       sample the local player's input for frame f + D and send it;
 *       wait for bundle f (lockstep), hash the state every 30 frames,
 *       write one controller sample for all players, apply each player's
 *       port-side input, update the presentation rect.
 *   any later stage load (normally the title after the results screen)
 *       -> match over: uninstall everything, restore the user's own setup.
 *
 * Nothing here writes game state outside those points, and nothing runs at
 * all until a MATCH_START is received.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ultra64.h>
#include "joy.h"
#include "boss.h"
#include "front.h"
#include "file.h"
#include "file2.h"   /* DEFAULT_OPTIONS (used by BLANKSAVEDATA) */
#include "lv.h"
#include "mp_weapon.h"
#include "mpmenu.h"
#include "frametiming.h"
#include "player.h"
#include "random.h"
#include "chrObjRandom.h"

#include "platform.h"
#include "system.h"
#include "config.h"
#include "input.h"
#include "video.h"   /* videoQuitRequested */
#include "netgame.h"

/* port/src/netui.c (its header pulls SDL + gbi; keep this TU game-only). */
extern void netuiOpenFromCli(void);

#include "net_client.h"
#include "net_dir.h"
#include "net_gamedata.h"
#include "net_plat.h"
#include "net_proto.h"
#include "net_runtime.h"

/* The N64 headers name struct fields `errno`; never let a libc macro of the
 * same name leak in below (we only touch pads through memset/struct copy). */
#ifdef errno
#undef errno
#endif

/* ---- game symbols without a header declaration ---- */
extern struct mp_stage_setup multi_stage_setups[];          /* front.c */
extern MENU current_menu;                                    /* front.c */
extern MENU menu_update;                                     /* front.c */
extern MENU maybe_prev_menu;                                 /* front.c */
extern save_data *fileGetSaveForFoldernum(u32 folder);       /* file2.c */
extern void default_player_perspective_and_height(void);     /* player.c */
extern void set_players_team_or_scenario_item_flag(int player, s32 flag); /* front.c */
extern void init_mp_options_for_scenario(s32 numplayers);    /* front.c */
extern s32 portCrosshairHide;                                /* video.c */
extern s32 portNoHitFlash;                                   /* video.c */
/* port/src/libultra.c: frame-locked osGetCount() for the game thread. */
extern void portNetClockEnable(void);
extern void portNetClockDisable(void);
/* port/src/input.c netplay capture / replay. */
extern void inputNetCaptureBegin(void);
extern void inputNetCaptureEnd(NetInputRec *out);
extern void inputNetApply(int playerIndex, const NetInputRec *rec);
extern void inputNetMatchReset(void);
/* fast3d: present only the local viewport (port/fast3d/gfx_pc.cpp). */
extern void gfx_netplay_set_view(int enable, float x, float y, float w, float h);

/* ---- config ([Net] in ge007.ini) ---- */
static char s_cfgName[NET_NAME_MAX] = "";
static char s_cfgServer[128] = "";
static int s_cfgHostPort = NET_DEFAULT_HOST_PORT;
static int s_cfgShowStats = 1;
static int s_cfgCharacter = 0;   /* last lobby pick (mp_chr_setup index) */
/* D414 online service: "" = this build's (GE007_ONLINE_SERVICE_URL, set at
 * configure time), "off" = none, else a URL. Empty means "the default" and
 * not "none", so an ini written by an older build never switches a newer
 * build's service off. Same for the STUN server ("" = stun.cloudflare.com). */
static char s_cfgService[NET_DIR_URL_MAX] = "";
static char s_cfgStun[128] = "";
/* D416 quick-match preferences: mode (scenario), map, weapons, length,
 * players; 255 = any. */
static int s_cfgQuick[5] = { NDP_ANY, NDP_ANY, NDP_ANY, NDP_ANY, NDP_ANY };

PD_CONSTRUCTOR static void netgameConfigInit(void)
{
    configRegisterString("Net.Name", s_cfgName, sizeof(s_cfgName));
    configRegisterString("Net.Server", s_cfgServer, sizeof(s_cfgServer));
    configRegisterInt("Net.HostPort", &s_cfgHostPort, 1024, 65535);
    configRegisterInt("Net.ShowStats", &s_cfgShowStats, 0, 1);
    configRegisterInt("Net.Character", &s_cfgCharacter, 0, NG_NUM_CHARACTERS - 1);
    configRegisterString("Net.Service", s_cfgService, sizeof(s_cfgService));
    configRegisterString("Net.Stun", s_cfgStun, sizeof(s_cfgStun));
    configRegisterInt("Net.QuickMode", &s_cfgQuick[0], 0, 255);
    configRegisterInt("Net.QuickStage", &s_cfgQuick[1], 0, 255);
    configRegisterInt("Net.QuickWeapons", &s_cfgQuick[2], 0, 255);
    configRegisterInt("Net.QuickLength", &s_cfgQuick[3], 0, 255);
    configRegisterInt("Net.QuickPlayers", &s_cfgQuick[4], 0, 255);
}

static void ngQuickPrefs(NdpPrefs *p)
{
    p->scenario = (uint8_t)s_cfgQuick[0];
    p->stage = (uint8_t)s_cfgQuick[1];
    p->weapons = (uint8_t)s_cfgQuick[2];
    p->length = (uint8_t)s_cfgQuick[3];
    p->players = (uint8_t)s_cfgQuick[4];
    ndpNormalizePrefs(p);   /* whatever the ini says */
}

/* ---- state ---- */
enum { NG_IDLE = 0, NG_PENDING, NG_RUNNING };

typedef struct NgSaved {
    GAMEMODE gamemode;
    s32 numPlayers, scenario, stageSel, length, weapons, aimsight;
    s32 chars[4], handicaps[4], controls[4];
    u8 cheats[CHEAT_MAX];
    s32 appendCheat;
    s32 folder, folderCopy;
    DIFFICULTY difficulty;
    s32 crossHide, noHitFlash;
} NgSaved;

static struct {
    volatile int phase;
    NetMatchStart ms;
    int localSlot;
    int numPlayers;
    int delay;
    s32 levelId;            /* LEVELID of the match stage */
    volatile uint32_t frame;
    int ringReset;
    int loadedSent;
    int leaveReq;           /* user asked to quit (any thread) */
    int stageExitRequested;
    NgSaved saved;
    int savedValid;
    /* this frame's records, for the PD mouse-aim hook */
    NetInputRec cur[NET_MAX_PLAYERS];
    uint8_t curPresent;
    /* hud */
    volatile int waiting;
    volatile int desync;
    volatile int discMask;
    int viewEnabled;
    float viewX, viewY, viewW, viewH;
    char buildId[NET_BUILDID_MAX];
    /* a MATCH_START taken while the front end was mid-transition */
    int held;
    NetMatchStart heldMs;
    int heldSlot;
    uint64_t heldSinceUs;
    /* waiting screen (D414): what the game thread is parked on, since when */
    volatile int waitKind;
    volatile uint64_t waitSinceUs;
    volatile uint32_t waitFrame;
    /* scores as of the last frame, for the online service's match report:
     * kills[i][j] = times player i killed player j (player_data.kill_counts) */
    int kills[NET_MAX_PLAYERS][NET_MAX_PLAYERS];
} NG;

static int s_inited = 0;

static NetClient *ngClient(void)
{
    return netRuntimeRunning() ? netRuntimeClient() : NULL;
}

static void ngLog(void *ctx, int level, const char *msg)
{
    (void)ctx;
    sysLogPrintf(level >= NETLOG_ERROR ? LOG_ERROR : level >= NETLOG_WARN ? LOG_WARNING : LOG_INFO, "net: %s", msg);
}

const char *netgameBuildId(void)
{
    if (!NG.buildId[0]) {
        /* Same build hash, region, pointer width and protocol: anything else
         * may compute differently. Non-git source trees report "unknown"; the
         * desync detector is the backstop there. */
        netStrFmt(NG.buildId, sizeof(NG.buildId), "%s|%s|%s|p%d|%dbit",
                  GE007_VERSION_HASH, GE007_ROMID, GE007_TARGET_PLATFORM,
                  NET_PROTO_VERSION, (int)(sizeof(void *) * 8));
    }
    return NG.buildId;
}

/* ------------------------------------------------------------------------ */
/* Queries used by the rest of the port                                      */
/* ------------------------------------------------------------------------ */

int netgameSimPinned(void)
{
    return NG.phase == NG_RUNNING;
}

int netgameOwnsInput(void)
{
    return NG.phase == NG_RUNNING;
}

int netgamePdTurn(float *tx, float *ty)
{
    s32 p;
    if (NG.phase != NG_RUNNING) return 0;
    p = get_cur_playernum();
    if (p < 0 || p >= NET_MAX_PLAYERS || !(NG.curPresent & (1u << p))) return 0;
    if (!(NG.cur[p].flags & NIR_PDTURN)) return 0;
    if (tx) *tx = NG.cur[p].pdturn_x;
    if (ty) *ty = NG.cur[p].pdturn_y;
    return 1;
}

int netgameLocalView(float *x, float *y, float *w, float *h)
{
    if (NG.phase != NG_RUNNING || !NG.viewEnabled) return 0;
    *x = NG.viewX;
    *y = NG.viewY;
    *w = NG.viewW;
    *h = NG.viewH;
    return 1;
}

void netgameGetHud(NetgameHud *out)
{
    NetClient *c = ngClient();
    memset(out, 0, sizeof(*out));
    out->inMatch = (NG.phase != NG_IDLE || NG.held);
    out->starting = (NG.phase == NG_PENDING || NG.held);
    out->frame = (int)NG.frame;
    out->delay = NG.delay;
    out->waiting = NG.waiting;
    out->desync = NG.desync;
    out->discMask = NG.discMask;
    out->localSlot = NG.localSlot;
    out->numPlayers = NG.numPlayers;
    if (c) {
        NetClientStatus st;
        netClientGetStatus(c, &st);
        out->pingMs = (int)(st.rttMs + 0.5f);
        if (st.desync) out->desync = 1;
    }
}

void netgameRequestLeaveMatch(void)
{
    if (NG.phase != NG_IDLE || NG.held) NG.leaveReq = 1;
}

#define NG_WAIT_SCREEN_US 300000ull   /* shorter hitches stay invisible */

void netgameGetWait(NetgameWait *out)
{
    const int kind = NG.waitKind;
    const uint64_t since = NG.waitSinceUs;
    int i;
    memset(out, 0, sizeof(*out));
    if (NG.phase != NG_RUNNING || kind == NGW_NONE) return;
    out->kind = kind;
    out->ms = (int)((netTimeUs() - since) / 1000ull);
    out->frame = NG.waitFrame;
    out->localSlot = NG.localSlot;
    out->numPlayers = NG.numPlayers;
    for (i = 0; i < NG.numPlayers && i < NET_MAX_PLAYERS; i++) {
        netStrCopy(out->names[i], (int)sizeof(out->names[i]), NG.ms.players[i].name);
    }
}

int netgameWantWaitFrame(void)
{
    static uint64_t lastUs;
    uint64_t now;
    if (NG.phase != NG_RUNNING || NG.waitKind == NGW_NONE) return 0;
    now = netTimeUs();
    if (now - NG.waitSinceUs < NG_WAIT_SCREEN_US) return 0;
    if (now - lastUs < 33000ull) return 0;   /* ~30 fps is plenty for a status screen */
    lastUs = now;
    return 1;
}

int netgameOnlineActive(void)
{
    NetClient *c = ngClient();
    NetClientStatus st;
    char busy[8];
    if (!c) return 0;
    if (NG.phase != NG_IDLE || NG.held || netRuntimeOnlineBusy(busy, sizeof(busy))) return 1;
    netClientGetStatus(c, &st);
    return st.state == NCS_CONNECTING || st.state == NCS_CONNECTED || st.state == NCS_LOBBY;
}

/* ------------------------------------------------------------------------ */
/* Validation of the (remote) match description                              */
/* ------------------------------------------------------------------------ */

static int ngValidateStart(const NetMatchStart *ms, int slot, char *why, int whyLen)
{
    NetSettings s = ms->settings;
    if (ms->num_players < 2 || ms->num_players > NET_MAX_PLAYERS || slot < 0 || slot >= ms->num_players) {
        netStrCopy(why, whyLen, "bad player count");
        return -1;
    }
    if (ms->delay < 1 || ms->delay > NET_MAX_DELAY) {
        netStrCopy(why, whyLen, "bad input delay");
        return -1;
    }
    if (ms->stage < 1 || ms->stage >= NG_NUM_STAGES) {
        netStrCopy(why, whyLen, "bad stage");
        return -1;
    }
    s.stage = ms->stage;
    /* Every index the game will use to subscript a table is range-checked
     * here -- remote data never reaches front.c's arrays unchecked. */
    return ngValidateMatch(&s, ms->players, ms->num_players, why, whyLen);
}

/* ------------------------------------------------------------------------ */
/* Saving / applying / restoring the MP setup                                */
/* ------------------------------------------------------------------------ */

static void ngSaveUserState(void)
{
    int i;
    NgSaved *s = &NG.saved;
    s->gamemode = gamemode;
    s->numPlayers = selected_num_players;
    s->scenario = scenario;
    s->stageSel = MP_stage_selected;
    s->length = game_length;
    s->weapons = getMPWeaponSet();
    s->aimsight = aim_sight_adjustment;
    for (i = 0; i < 4; i++) {
        s->chars[i] = player_char[i];
        s->handicaps[i] = player_handicap[i];
        s->controls[i] = controlstyle_player[i];
    }
    memcpy(s->cheats, g_CheatActivated, sizeof(s->cheats));
    s->appendCheat = g_AppendCheatSinglePlayer;
    s->folder = selected_folder_num;
    s->folderCopy = selected_folder_num_copy;
    s->difficulty = selected_difficulty;
    s->crossHide = portCrosshairHide;
    s->noHitFlash = portNoHitFlash;
    NG.savedValid = 1;
}

static void ngRestoreUserState(void)
{
    int i;
    NgSaved *s = &NG.saved;
    if (!NG.savedValid) return;
    selected_num_players = s->numPlayers;
    scenario = s->scenario;
    MP_stage_selected = s->stageSel;
    game_length = s->length;
    setMPWeaponSet(s->weapons);
    aim_sight_adjustment = s->aimsight;
    for (i = 0; i < 4; i++) {
        player_char[i] = s->chars[i];
        player_handicap[i] = s->handicaps[i];
        controlstyle_player[i] = s->controls[i];
    }
    memcpy(g_CheatActivated, s->cheats, sizeof(s->cheats));
    g_AppendCheatSinglePlayer = s->appendCheat;
    selected_folder_num = s->folder;
    selected_folder_num_copy = s->folderCopy;
    selected_difficulty = s->difficulty;
    portCrosshairHide = s->crossHide;
    portNoHitFlash = s->noHitFlash;
    /* gamemode stays MULTI: the front end returns to the MP options screen
     * (interface_menu0B_runstage), exactly like after a local MP game. */
    NG.savedValid = 0;
}

/* The agreed MP setup, written through the same globals / setters the MP
 * options menus use. Called twice: before RUN_STAGE (front end) and again at
 * the stage-load hook (immune to any front-end tick in between). */
static void ngApplyMpSetup(void)
{
    const NetMatchStart *ms = &NG.ms;
    int i;
    gamemode = GAMEMODE_MULTI;
    scenario = ms->settings.scenario;
    MP_stage_selected = ms->stage;
    game_length = ms->settings.length;
    setMPWeaponSet(ms->settings.weapons);
    aim_sight_adjustment = ms->settings.aimsight;
    for (i = 0; i < 4; i++) {
        if (i < ms->num_players) {
            player_char[i] = ms->players[i].character;
            player_handicap[i] = ms->players[i].handicap;
            controlstyle_player[i] = ms->players[i].control;
        } else {
            player_char[i] = -1;
            player_handicap[i] = NG_DEFAULT_HANDICAP;
            controlstyle_player[i] = 0;
        }
    }
    /* front.c's own consistency pass (sets selected_num_players; a no-op for
     * a validated setup). */
    init_mp_options_for_scenario(ms->num_players);
    selected_num_players = ms->num_players;
    scenario = ms->settings.scenario;
    MP_stage_selected = ms->stage;
    game_length = ms->settings.length;
    setMPWeaponSet(ms->settings.weapons);
    selected_stage = multi_stage_setups[ms->stage].stage_id;
    briefingpage = -1;
    selected_difficulty = DIFFICULTY_AGENT;
    g_AppendCheatSinglePlayer = FALSE;
    memset(g_CheatActivated, 0, CHEAT_MAX);
}

/* ------------------------------------------------------------------------ */
/* Match start (front end)                                                   */
/* ------------------------------------------------------------------------ */

/* An interactive front-end screen with no menu switch in flight: the same
 * places a player could press the MP options screen's start tab (or back out
 * to it) from. Logo / intro / debrief / transition screens and the PC
 * options screen are not -- the match waits for them (or is declined). */
static int ngFrontEndSettled(void)
{
    if (menu_update != MENU_INVALID || maybe_prev_menu != MENU_INVALID) return 0;
    switch (current_menu) {
    case MENU_FILE_SELECT:
    case MENU_MODE_SELECT:
    case MENU_MISSION_SELECT:
    case MENU_DIFFICULTY:
    case MENU_007_OPTIONS:
    case MENU_BRIEFING:
    case MENU_MP_OPTIONS:
    case MENU_MP_CHAR_SELECT:
    case MENU_MP_HANDICAP:
    case MENU_MP_CONTROL_STYLE:
    case MENU_MP_STAGE_SELECT:
    case MENU_MP_SCENARIO_SELECT:
    case MENU_MP_TEAMS:
    case MENU_CHEAT:
        return 1;
    default:
        return 0;
    }
}

int netgameAtFrontEnd(void)
{
    return bossGetStageNum() == LEVELID_TITLE && ngFrontEndSettled();
}

#define NG_HOLD_US 15000000ull   /* how long a start may wait for the menus */

static void ngBeginMatch(NetClient *c)
{
    NetMatchStart ms;
    int slot;
    char why[96];

    if (!NG.held) {
        if (!netClientMatchTake(c, &NG.heldMs, &NG.heldSlot)) return;
        NG.held = 1;
        NG.heldSinceUs = netTimeUs();
    }
    {
        NetClientStatus st;
        netClientGetStatus(c, &st);
        if (st.matchPhase == NMP_NONE || st.matchPhase == NMP_OVER) {
            NG.held = 0;   /* ended before we got there */
            return;
        }
    }
    if (NG.leaveReq) {
        NG.held = 0;
        NG.leaveReq = 0;
        netClientMatchLeave(c);
        return;
    }
    if (bossGetStageNum() != LEVELID_TITLE) {
        sysLogPrintf(LOG_WARNING, "net: match start declined -- not on the front end (stage %d)", (int)bossGetStageNum());
        NG.held = 0;
        netClientMatchLeave(c);
        return;
    }
    if (!ngFrontEndSettled()) {
        if (netTimeUs() - NG.heldSinceUs > NG_HOLD_US) {
            sysLogPrintf(LOG_WARNING, "net: match start declined -- front end busy (menu %d)", (int)current_menu);
            NG.held = 0;
            netClientMatchLeave(c);
        }
        return;   /* try again next frame */
    }
    ms = NG.heldMs;
    slot = NG.heldSlot;
    NG.held = 0;
    if (ngValidateStart(&ms, slot, why, sizeof(why)) != 0) {
        sysLogPrintf(LOG_ERROR, "net: rejecting match description: %s", why);
        netClientMatchLeave(c);
        return;
    }
    if (multi_stage_setups[ms.stage].stage_id < 0) {
        sysLogPrintf(LOG_ERROR, "net: stage %d has no level", ms.stage);
        netClientMatchLeave(c);
        return;
    }

    memset(&NG.cur, 0, sizeof(NG.cur));
    NG.ms = ms;
    NG.localSlot = slot;
    NG.numPlayers = ms.num_players;
    NG.delay = ms.delay;
    NG.levelId = multi_stage_setups[ms.stage].stage_id;
    NG.frame = 0;
    NG.ringReset = 0;
    NG.loadedSent = 0;
    NG.leaveReq = 0;
    NG.stageExitRequested = 0;
    NG.desync = 0;
    NG.discMask = 0;
    NG.viewEnabled = 0;
    NG.waitKind = NGW_NONE;
    memset(NG.kills, 0, sizeof(NG.kills));

    ngSaveUserState();
    ngApplyMpSetup();
    frontChangeMenu(MENU_RUN_STAGE, 1);
    NG.phase = NG_PENDING;
    sysLogPrintf(LOG_INFO, "net: match %08x: player %d of %d, %s, delay %d frames",
                 (unsigned)ms.match_id, slot + 1, ms.num_players, ngStageName(ms.stage), ms.delay);
}

/* ------------------------------------------------------------------------ */
/* Playback hook (lockstep)                                                  */
/* ------------------------------------------------------------------------ */

static void ngLeaveStage(void)
{
    if (!NG.stageExitRequested) {
        NG.stageExitRequested = 1;
        bossSetLoadedStage(LEVELID_TITLE);   /* the MP game's own exit route */
    }
}

static uint32_t fnv1a(uint32_t h, const void *p, int n)
{
    const uint8_t *b = (const uint8_t *)p;
    int i;
    for (i = 0; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

/* Desync detector: a digest of state every peer must agree on. */
static uint32_t ngStateHash(void)
{
    uint32_t h = 2166136261u;
    int i;
    h = fnv1a(h, &g_randomSeed, sizeof(g_randomSeed));
    h = fnv1a(h, &g_chrObjRandomSeed, sizeof(g_chrObjRandomSeed));
    h = fnv1a(h, &g_GlobalTimer, sizeof(g_GlobalTimer));
    for (i = 0; i < NG.numPlayers; i++) {
        struct player *p = g_playerPointers[i];
        if (!p) continue;
        h = fnv1a(h, &p->vv_theta, sizeof(p->vv_theta));
        h = fnv1a(h, &p->vv_verta, sizeof(p->vv_verta));
        h = fnv1a(h, &p->bondhealth, sizeof(p->bondhealth));
        h = fnv1a(h, &p->bondarmour, sizeof(p->bondarmour));
        h = fnv1a(h, &p->bonddead, sizeof(p->bonddead));
        h = fnv1a(h, &p->hand_item, sizeof(p->hand_item));
        if (p->prop) h = fnv1a(h, &p->prop->pos, sizeof(p->prop->pos));
        h = fnv1a(h, &g_playerPlayerData[i].kill_counts, sizeof(g_playerPlayerData[i].kill_counts));
    }
    return h;
}

static void ngRecToPad(const NetInputRec *r, OSContPad *pad)
{
    memset(pad, 0, sizeof(*pad));
    pad->button = r->buttons;
    pad->stick_x = r->stick_x;
    pad->stick_y = r->stick_y;
}

/* Input for a player whose peer dropped out: neutral, except an A tap on the
 * game-over screen so mpwatchMenuTick (which waits for EVERY player to
 * confirm) can still return everyone to the menus. Derived from game state
 * every peer shares, so it is deterministic. */
static void ngSynthDisconnected(NetInputRec *r, uint32_t frame)
{
    memset(r, 0, sizeof(*r));
    if (g_gameOverFlag && (frame & 1)) r->buttons = A_BUTTON;
}

static void ngUpdateView(void)
{
    struct player *p;
    if (NG.localSlot < 0 || NG.localSlot >= NET_MAX_PLAYERS) return;
    p = g_playerPointers[NG.localSlot];
    /* The viewport fields are computed during the first frame's player tick
     * (bondviewMovePlayerUpdateViewport); before that they hold defaults. */
    if (!p || NG.frame < 2 || p->viewx < 16 || p->viewy < 16) return;
    NG.viewX = (float)p->viewleft;
    NG.viewY = (float)p->viewtop;
    NG.viewW = (float)p->viewx;
    NG.viewH = (float)p->viewy;
    if (!NG.viewEnabled) {
        NG.viewEnabled = 1;
        sysLogPrintf(LOG_INFO, "net: presenting player %d view %dx%d at (%d,%d)", NG.localSlot + 1,
                     (int)p->viewx, (int)p->viewy, (int)p->viewleft, (int)p->viewtop);
    }
    gfx_netplay_set_view(1, NG.viewX, NG.viewY, NG.viewW, NG.viewH);
}

/* The game's own tallies (mpmenu.c's results screen reads the same ones),
 * kept as of the last frame: the stage-load reset (lv.c) may run before the
 * match-over hook gets to look. */
static void ngSnapshotScores(void)
{
    int i, j;
    for (i = 0; i < NG.numPlayers && i < NET_MAX_PLAYERS; i++) {
        for (j = 0; j < NG.numPlayers && j < NET_MAX_PLAYERS; j++) {
            NG.kills[i][j] = g_playerPlayerData[i].kill_counts[j];
        }
    }
}

/* Host of an online lobby: the result for the service's status page.
 * Kills / deaths exactly as the results screen counts them (mpmenu.c):
 * kills = others killed, deaths = every death including suicides. */
static void ngReportResult(void)
{
    NdpResult r;
    int i, j;
    if (!netRuntimeIsHostingOnline() || NG.frame < 600) return;   /* < 10 s: not a match */
    memset(&r, 0, sizeof(r));
    r.durationSec = (uint16_t)(NG.frame / 60 > 65535 ? 65535 : NG.frame / 60);
    r.scenario = NG.ms.settings.scenario;
    r.stage = NG.ms.stage;
    r.n = (uint8_t)NG.numPlayers;
    for (i = 0; i < NG.numPlayers && i < NET_MAX_PLAYERS; i++) {
        int kills = 0, deaths = 0;
        for (j = 0; j < NG.numPlayers && j < NET_MAX_PLAYERS; j++) {
            if (j != i) kills += NG.kills[i][j];
            deaths += NG.kills[j][i];
        }
        netStrCopy(r.players[i].name, NET_NAME_MAX, NG.ms.players[i].name);
        r.players[i].character = NG.ms.players[i].character;
        r.players[i].team = NG.ms.players[i].team;
        r.players[i].kills = (int16_t)(kills > 32767 ? 32767 : kills);
        r.players[i].deaths = (int16_t)(deaths > 32767 ? 32767 : deaths);
    }
    netRuntimeReportMatch(&r);
}

static s32 netgamePlaybackFunc(struct contsample *samples, s32 curlast)
{
    NetClient *c = ngClient();
    s32 idx = (curlast + 1) % CONTSAMPLE_LEN;
    NetBundle b;
    uint32_t f = NG.frame;
    int p, r;
    uint64_t waitStart;

    if (!NG.ringReset) {
        /* Stale samples from an earlier playback (attract demo / last
         * match) would otherwise form button edges in frame 0. */
        memset(samples, 0, sizeof(struct contsample) * CONTSAMPLE_LEN);
        NG.ringReset = 1;
    }
    memset(&samples[idx], 0, sizeof(samples[idx]));

    if (NG.phase != NG_RUNNING || !c || NG.stageExitRequested) {
        return idx;   /* neutral frame while leaving */
    }

    /* Start barrier: everyone finishes loading, then frame 0 runs. */
    if (!NG.loadedSent) {
        netClientMatchLoaded(c);
        NG.loadedSent = 1;
        sysLogPrintf(LOG_INFO, "net: stage loaded, waiting for the other players");
    }
    if (f == 0) {
        waitStart = netTimeUs();
        NG.waiting = 1;
        NG.waitFrame = 0;
        NG.waitSinceUs = waitStart;
        NG.waitKind = NGW_START;
        while (!netClientMatchIsGo(c)) {
            NetClientStatus st;
            netClientGetStatus(c, &st);
            if (NG.leaveReq || st.state == NCS_FAILED || st.matchPhase == NMP_OVER || st.matchPhase == NMP_NONE ||
                netTimeUs() - waitStart > 120000000ull || videoQuitRequested()) {
                NG.waiting = 0;
                NG.waitKind = NGW_NONE;
                netClientMatchLeave(c);
                ngLeaveStage();
                return idx;
            }
            netSleepUs(1000);
        }
        NG.waiting = 0;
        NG.waitKind = NGW_NONE;
    }
    ngSnapshotScores();

    if (NG.leaveReq) {
        netClientMatchLeave(c);
        ngLeaveStage();
        return idx;
    }

    /* 1. Local input, scheduled D frames ahead. */
    {
        NetInputRec rec;
        s32 saved = get_cur_playernum();
        signed char sx = 0, sy = 0;
        memset(&rec, 0, sizeof(rec));
        if (g_playerPointers[NG.localSlot]) {
            set_cur_player(NG.localSlot);
            inputNetCaptureBegin();
            inputUpdate();
            rec.buttons = (uint16_t)inputComputePad(0, &sx, &sy);
            inputNetCaptureEnd(&rec);
            rec.stick_x = sx;
            rec.stick_y = sy;
            if (g_playerPointers[saved]) set_cur_player(saved);
        }
        netClientMatchSubmit(c, f + (uint32_t)NG.delay, &rec);
    }

    /* 2. Timesync: yield a little when we run ahead of the slowest peer. */
    {
        uint32_t us = netClientMatchAdvise(c, f);
        if (us) netSleepUs(us);
    }

    /* 3. Lockstep: this frame's bundle. */
    waitStart = netTimeUs();
    for (;;) {
        r = netClientMatchGetBundle(c, f, &b);
        if (r != 0) break;
        if (!NG.waiting && netTimeUs() - waitStart > 50000ull) {
            NG.waiting = 1;
            NG.waitFrame = f;
            NG.waitSinceUs = waitStart;
            NG.waitKind = NGW_STALL;
        }
        if (NG.leaveReq || videoQuitRequested() || netTimeUs() - waitStart > 60000000ull) {
            NG.waiting = 0;
            NG.waitKind = NGW_NONE;
            netClientMatchLeave(c);
            ngLeaveStage();
            return idx;
        }
        netSleepUs(500);
    }
    NG.waiting = 0;
    NG.waitKind = NGW_NONE;
    if (r < 0) {
        /* Aborted by the leader, host lost, or removed: leave the stage. */
        sysLogPrintf(LOG_INFO, "net: match ended at frame %u", (unsigned)f);
        ngLeaveStage();
        return idx;
    }

    /* 4. Desync detector: state BEFORE this frame's input. */
    if (f > 0 && (f % 30) == 0) {
        netClientMatchHash(c, f, ngStateHash());
        {
            NetClientStatus st;
            netClientGetStatus(c, &st);
            NG.desync = st.desync;
        }
    }

    /* 5. One controller sample for every player + their port-side input. */
    NG.curPresent = 0;
    NG.discMask = b.disc;
    for (p = 0; p < NET_MAX_PLAYERS; p++) {
        NetInputRec rec;
        if (p >= NG.numPlayers) {
            memset(&NG.cur[p], 0, sizeof(NG.cur[p]));
            continue;
        }
        if (b.present & (1u << p)) {
            rec = b.rec[p];
        } else {
            ngSynthDisconnected(&rec, f);
        }
        NG.cur[p] = rec;
        NG.curPresent |= (uint8_t)(1u << p);
        ngRecToPad(&rec, &samples[idx].pads[p]);
    }
    for (p = 0; p < NG.numPlayers; p++) {
        if (g_playerPointers[p]) inputNetApply(p, &NG.cur[p]);
    }

    /* Port knobs that change which game code runs stay at their defaults
     * even if the F10 overlay flips them mid-match (hazard H12). */
    portCrosshairHide = 0;
    portNoHitFlash = 0;

    netClientMatchConsumed(c, f + 1);
    NG.frame = f + 1;
    ngUpdateView();
    return idx;
}

/* ------------------------------------------------------------------------ */
/* Stage-load hook (src/boss.c)                                              */
/* ------------------------------------------------------------------------ */

static void ngEndMatch(const char *why)
{
    NetClient *c = ngClient();
    joySetPlaybackFunc(NULL, -1);
    joySetContDataIndex(0);
    portNetClockDisable();
    gfx_netplay_set_view(0, 0, 0, 0, 0);
    ngRestoreUserState();
    ngReportResult();
    if (c) netClientMatchFinished(c);
    NG.phase = NG_IDLE;
    NG.viewEnabled = 0;
    NG.waiting = 0;
    NG.waitKind = NGW_NONE;
    NG.curPresent = 0;
    NG.leaveReq = 0;
    sysLogPrintf(LOG_INFO, "net: match over (%s)", why);
}

void netgameOnStageLoad(int stage)
{
    if (NG.phase == NG_RUNNING) {
        /* Any stage load after the match stage ends the match (normally the
         * title stage after the results screen, or an abort / leave). */
        ngEndMatch("left the stage");
    }
    if (NG.phase != NG_PENDING) return;

    if (stage != NG.levelId) {
        if (stage == LEVELID_TITLE) return;   /* still in the front end */
        sysLogPrintf(LOG_ERROR, "net: expected stage %d, got %d -- leaving the match", (int)NG.levelId, stage);
        {
            NetClient *c = ngClient();
            if (c) netClientMatchLeave(c);
        }
        ngRestoreUserState();
        NG.phase = NG_IDLE;
        return;
    }

    /* ---- the deterministic starting state ---- */
    ngApplyMpSetup();

    /* PRNG streams (H3). */
    g_randomSeed = NG.ms.seed_random;
    g_chrObjRandomSeed = NG.ms.seed_chrobj;

    /* Frame counters accumulate since boot; give every peer the same ones. */
    lastFrameCounter = -1;
    currentFrameCounter = 0;
    speedgraphframes = 1;
    previousFrameCounter = -1;
    halfFrameCounter = 0;
    isFrameCounterOdd = 0;
    halfMinusPreviousCounter = 0;

    /* Per-player perm data: stats / team flags not all reset per stage
     * (H16). Re-seed like boot, then the agreed teams. */
    memset(g_playerPlayerData, 0, sizeof(struct player_data) * 4);
    default_player_perspective_and_height();
    {
        int i;
        for (i = 0; i < 4; i++) {
            int team = (i < NG.numPlayers && ngScenarioIsTeam(NG.ms.settings.scenario)) ? NG.ms.players[i].team : 0;
            set_players_team_or_scenario_item_flag(i, team);
        }
    }

    /* Save options via the RAMROM path's RAM-only folder 100 (H13): the
     * local folder's save (volumes, etc.) with the agreed option bits. */
    {
        save_data ram;
        save_data *mine = fileGetSaveForFoldernum((u32)selected_folder_num);
        u16 opts = (u16)(NG.ms.settings.options & NG_OPTIONS_ALLOWED);
        if (mine) {
            ram = *mine;
        } else {
            save_data blank = BLANKSAVEDATA;
            ram = blank;
        }
        ram.options = opts;
        set_selected_foldernum_and_copy_demo_eeprom(&ram);
    }

    set_selected_difficulty(DIFFICULTY_AGENT);
    lvlSetSelectedDifficulty(DIFFICULTY_AGENT);

    /* Controllers: one networked sample per frame for every player. */
    inputNetMatchReset();
    joySetPlaybackFunc(netgamePlaybackFunc, NG.numPlayers);
    joySetContDataIndex(1);

    /* deltaFrames == 1 every frame (H1). */
    portNetClockEnable();

    NG.phase = NG_RUNNING;
    NG.frame = 0;
    sysLogPrintf(LOG_INFO, "net: match stage %d loading (seed %016llx)", stage,
                 (unsigned long long)NG.ms.seed_random);
}

/* ------------------------------------------------------------------------ */
/* Per-frame tick (game thread)                                              */
/* ------------------------------------------------------------------------ */

void netgameGameTick(void)
{
    NetClient *c;
    if (!s_inited) return;
    c = ngClient();
    if (!c) return;

    /* A leave requested while PENDING (RUN_STAGE already queued) is honoured
     * by the playback hook on the stage's first frame: the stage loads with
     * the agreed setup and exits straight back through the normal route,
     * so no half-configured local MP game is ever left running. */
    if (NG.phase == NG_IDLE) {
        ngBeginMatch(c);
    }
}

/* ------------------------------------------------------------------------ */
/* Init / CLI                                                                */
/* ------------------------------------------------------------------------ */

static void ngAtExit(void)
{
    if (netRuntimeRunning()) netRuntimeShutdownForExit();
}

void netgameInit(void)
{
    const char *name;
    if (s_inited) return;
    s_inited = 1;
    memset(&NG, 0, sizeof(NG));
    NG.localSlot = -1;
    (void)netgameBuildId();

    if (!s_cfgName[0]) netStrCopy(s_cfgName, sizeof(s_cfgName), "Agent");
    name = sysArgGetString("--net-name");
    if (name && *name) {
        netStrCopy(s_cfgName, sizeof(s_cfgName), name);
        netSanitizeText(s_cfgName, sizeof(s_cfgName), "Agent");
    }
    {
        const char *srv = sysArgGetString("--net-server");
        if (srv && *srv) netStrCopy(s_cfgServer, sizeof(s_cfgServer), srv);
        srv = sysArgGetString("--net-service");
        if (srv && *srv) netStrCopy(s_cfgService, sizeof(s_cfgService), srv);
    }
    atexit(ngAtExit);

    /* Command-line shortcuts (also handy for two-instance testing). */
    if (sysArgCheck("--net-host") || sysArgGetString("--net-join") || sysArgCheck("--net-quick") ||
        sysArgCheck("--net-online-host") || sysArgGetString("--net-online-join") ||
        sysArgCheck("--net-online-quick")) {
        if (netgameStartRuntime() != 0) {
            sysLogPrintf(LOG_ERROR, "net: could not start networking");
            return;
        }
        if (sysArgCheck("--net-online-host")) {
            char err[128], nm[NET_LOBBY_NAME_MAX];
            netStrFmt(nm, sizeof(nm), "%s's game", s_cfgName);
            if (netRuntimeHostOnline((uint16_t)s_cfgHostPort, nm, NET_MAX_PLAYERS, 1, 0, err, sizeof(err)) != 0) {
                sysLogPrintf(LOG_ERROR, "net: cannot host: %s", err);
            }
        } else if (sysArgGetString("--net-online-join")) {
            netRuntimeJoinOnline(0, sysArgGetString("--net-online-join"));
        } else if (sysArgCheck("--net-online-quick")) {
            NdpPrefs prefs;
            ngQuickPrefs(&prefs);
            netRuntimeQuickOnline(&prefs);
        } else if (sysArgCheck("--net-host")) {
            const char *ps = sysArgGetString("--net-host");
            int port = (ps && ps[0] >= '0' && ps[0] <= '9') ? atoi(ps) : s_cfgHostPort;
            char err[128];
            if (netRuntimeHost((uint16_t)port, s_cfgName, NET_MAX_PLAYERS, err, sizeof(err)) != 0) {
                sysLogPrintf(LOG_ERROR, "net: cannot host on port %d: %s", port, err);
            }
        } else if (sysArgGetString("--net-join")) {
            netRuntimeConnect(sysArgGetString("--net-join"), NET_DEFAULT_HOST_PORT, 0);
        } else if (s_cfgServer[0]) {
            netRuntimeConnect(s_cfgServer, NET_DEFAULT_SERVER_PORT, 1);
        }
        netuiOpenFromCli();
    }
}

/* Accessors for the UI (netui.c). */
const char *netgameCfgName(void) { return s_cfgName; }
void netgameCfgSetName(const char *n)
{
    netStrCopy(s_cfgName, sizeof(s_cfgName), n);
    netSanitizeText(s_cfgName, sizeof(s_cfgName), "Agent");
    netRuntimeSetName(s_cfgName);   /* client + online service; no-op if not running */
}
const char *netgameCfgServer(void) { return s_cfgServer; }
void netgameCfgSetServer(const char *s)
{
    netStrCopy(s_cfgServer, sizeof(s_cfgServer), s);
    netSanitizeText(s_cfgServer, sizeof(s_cfgServer), "");
}
int netgameCfgHostPort(void) { return s_cfgHostPort; }
void netgameCfgSetHostPort(int port)
{
    if (port >= 1024 && port <= 65535) s_cfgHostPort = port;
}
int netgameCfgShowStats(void) { return s_cfgShowStats; }
void netgameCfgSetShowStats(int on) { s_cfgShowStats = on ? 1 : 0; }
int netgameCfgCharacter(void) { return s_cfgCharacter; }
void netgameCfgSetCharacter(int c)
{
    if (c >= 0 && c < NG_NUM_CHARACTERS) s_cfgCharacter = c;
}
int netgameStartRuntime(void)
{
    if (netRuntimeStart(s_cfgName, netgameBuildId(), ngLog, NULL) != 0) return -1;
    netRuntimeSetService(netgameServiceUrl(), 0);
    netRuntimeSetStun(s_cfgStun[0] ? s_cfgStun : NULL);
    return 0;
}

/* D414 online service. */
const char *netgameServiceUrl(void)
{
    if (!strcmp(s_cfgService, "off")) return "";
    return s_cfgService[0] ? s_cfgService : GE007_ONLINE_SERVICE_URL;
}
void netgameCfgQuickPrefs(unsigned char out[5])
{
    NdpPrefs p;
    ngQuickPrefs(&p);
    out[0] = p.scenario;
    out[1] = p.stage;
    out[2] = p.weapons;
    out[3] = p.length;
    out[4] = p.players;
}
void netgameCfgSetQuickPrefs(const unsigned char in[5])
{
    NdpPrefs p;
    p.scenario = in[0];
    p.stage = in[1];
    p.weapons = in[2];
    p.length = in[3];
    p.players = in[4];
    ndpNormalizePrefs(&p);
    s_cfgQuick[0] = p.scenario;
    s_cfgQuick[1] = p.stage;
    s_cfgQuick[2] = p.weapons;
    s_cfgQuick[3] = p.length;
    s_cfgQuick[4] = p.players;
}
const char *netgameCfgService(void) { return s_cfgService; }
void netgameCfgSetService(const char *url)
{
    netStrCopy(s_cfgService, sizeof(s_cfgService), url);
    netSanitizeText(s_cfgService, sizeof(s_cfgService), "");
    if (netRuntimeRunning()) netRuntimeSetService(netgameServiceUrl(), 0);
}
