/*
 * netui.c -- F9 online-play overlay (D409, D410).
 *
 * See port/include/netui.h for the hooks and the thread model, and
 * docs/netplay.md for the player-facing guide. D410 added the online service
 * (quick match / browse / host / join by code through the central directory,
 * net_dir.h + net_runtime.h), the "own server" page for ge007-netserver, and
 * the waiting-for-players screen drawn while the game is stalled. The panel reuses the F10
 * overlay's look (optionsoverlay.c): the game's own Bank Gothic / Zurich text
 * and primitive-colour fill rects, drawn in the 2D canvas (viGetX() x
 * viGetY()). Rows are rebuilt from the live client status every frame; the
 * selection follows a row's identity, not its position, so a lobby update (a
 * player joining) never moves the cursor onto a different row.
 *
 * Nothing here touches game state: lobby actions go to the net client, match
 * actions to netgame.c, config to the [Net] keys.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include <PR/ultratypes.h>
/* gbi.h's DL macros need these two pure macros (see optionsoverlay.c). */
#ifndef _SHIFTL
#define _SHIFTL(v, s, w) ((u32)(((u32)(v) & ((0x01 << (w)) - 1)) << (s)))
#define _SHIFTR(v, s, w) ((u32)(((u32)(v) >> (s)) & ((0x01 << (w)) - 1)))
#endif
#include <PR/gbi.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "input.h"
#include "optionsoverlay.h"
#include "frontoptions.h"
#include "netgame.h"
#include "netui.h"
#include "../fast3d/gfx_api.h"

#include "net_client.h"
#include "net_dir.h"
#include "net_gamedata.h"
#include "net_plat.h"
#include "net_proto.h"
#include "net_runtime.h"
#include "net_sock.h"

/* ---- game symbols (rendering only; same pattern as optionsoverlay.c) ---- */
struct font;
struct fontchar;
extern struct font     *ptrFontBankGothic;
extern struct fontchar *ptrFontBankGothicChars;
extern struct font     *ptrFontZurichBold;
extern struct fontchar *ptrFontZurichBoldChars;
extern Gfx  *microcode_constructor(Gfx *gdl);
extern Gfx  *textRender(Gfx *gdl, s32 *x, s32 *y, char *text, struct fontchar *chars,
                        struct font *font, u32 colour, s32 width, s32 height,
                        u32 yOffset, s32 lineheight);
extern void  textMeasure(s32 *textheight, s32 *textwidth, char *text,
                         struct fontchar *chars, struct font *font, s32 lineheight);
extern s16   viGetX(void);
extern s16   viGetY(void);

/* ------------------------------------------------------------------------ */
/* Palette (the F10 overlay's watch green)                                   */
/* ------------------------------------------------------------------------ */

#define INK_TITLE 0xa0ffa0ffu
#define INK_SEL   0xa0ffa0ffu
#define INK_ROW   0x66ca77ffu
#define INK_DIM   0x498053ffu
#define INK_META  0x829e91ffu
#define INK_HEAD  0xa6baaaffu
#define INK_WARN  0xffd75affu
#define INK_ERR   0xff7a64ffu
#define INK_GOOD  0x8cf5c8ffu

/* ------------------------------------------------------------------------ */
/* Host thread -> scheduler thread event queue                               */
/* ------------------------------------------------------------------------ */

enum { EV_KEY = 1, EV_TEXT, EV_CLICK, EV_WHEEL };

typedef struct UiEvent {
    int kind;
    int a, b, c;
    char text[8];
} UiEvent;

#define EVQ 128
static UiEvent s_evq[EVQ];
static int s_evHead, s_evTail;
static SDL_SpinLock s_evLock;

static void evPush(const UiEvent *e)
{
    int next;
    SDL_AtomicLock(&s_evLock);
    next = (s_evTail + 1) % EVQ;
    if (next != s_evHead) {   /* full: drop (a flood of keys is not input) */
        s_evq[s_evTail] = *e;
        s_evTail = next;
    }
    SDL_AtomicUnlock(&s_evLock);
}

static int evPop(UiEvent *e)
{
    int got = 0;
    SDL_AtomicLock(&s_evLock);
    if (s_evHead != s_evTail) {
        *e = s_evq[s_evHead];
        s_evHead = (s_evHead + 1) % EVQ;
        got = 1;
    }
    SDL_AtomicUnlock(&s_evLock);
    return got;
}

static void evClear(void)
{
    SDL_AtomicLock(&s_evLock);
    s_evHead = s_evTail = 0;
    SDL_AtomicUnlock(&s_evLock);
}

/* Printable ASCII only: the game's glyph tables cover 0x21..0x7E and index
 * them unchecked (textRender: chars[c - 0x21]). */
static void pushText(const char *s)
{
    UiEvent e;
    int n = 0;
    memset(&e, 0, sizeof(e));
    e.kind = EV_TEXT;
    for (; s && *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch < 0x20 || ch > 0x7E) continue;
        e.text[n++] = (char)ch;
        if (n == (int)sizeof(e.text) - 1) {
            e.text[n] = 0;
            evPush(&e);
            memset(e.text, 0, sizeof(e.text));
            n = 0;
        }
    }
    if (n > 0) {
        e.text[n] = 0;
        evPush(&e);
    }
}

/* ------------------------------------------------------------------------ */
/* Open state (any thread)                                                   */
/* ------------------------------------------------------------------------ */

static SDL_atomic_t s_open;
static SDL_atomic_t s_textWanted;   /* scheduler: a text row is selected */
static SDL_atomic_t s_cliOpen;      /* --net-* at startup */
static SDL_atomic_t s_cliQuick;
static int s_textActive = -1;       /* host thread: -1 = SDL's default, untouched */

int netuiIsOpen(void)
{
    return SDL_AtomicGet(&s_open);
}

void netuiOpenFromCli(void)
{
    SDL_AtomicSet(&s_cliOpen, 1);
    if (sysArgCheck("--net-quick")) SDL_AtomicSet(&s_cliQuick, 1);
    SDL_AtomicSet(&s_open, 1);
}

static SDL_atomic_t s_reqHome;   /* opened from a menu entry: start at home */

int netuiRequestOpen(void)
{
    if (optionsOverlayIsOpen()) return 0;   /* one options UI at a time (D343) */
    SDL_AtomicSet(&s_reqHome, 1);
    SDL_AtomicSet(&s_open, 1);
    return 1;
}

Gfx *netuiWaitFrameDl(void)
{
    static Gfx dl[1];
    gSPEndDisplayList(dl);
    return dl;
}

int netuiHostKeyDown(const SDL_KeyboardEvent *ev)
{
    const SDL_Keycode k = ev->keysym.sym;
    const int open = SDL_AtomicGet(&s_open);

    if (k == SDLK_F4 && (ev->keysym.mod & KMOD_ALT)) return 0;   /* quit always works */
    if (k == SDLK_F9) {
        if (ev->repeat) return 1;
        if (ev->keysym.mod & KMOD_SHIFT) {
            /* Leave a running match at once -- also while the game is
             * stalled waiting for a peer (no frames, so no overlay). */
            NetgameHud h;
            netgameGetHud(&h);
            if (h.inMatch) {
                netgameRequestLeaveMatch();
                return 1;
            }
        }
        if (open) {
            SDL_AtomicSet(&s_open, 0);
        } else if (!optionsOverlayIsOpen() && !frontOptionsBlocksOverlay()) {
            SDL_AtomicSet(&s_open, 1);   /* one options UI at a time (D343) */
        }
        return 1;
    }
    if (!open) return 0;
    if (k == SDLK_F12) return 0;   /* screenshots still work */
    if (k == SDLK_v && (ev->keysym.mod & KMOD_CTRL)) {
        char *clip = SDL_GetClipboardText();   /* host thread: SDL video API */
        if (clip) {
            char tmp[128];
            netStrCopy(tmp, sizeof(tmp), clip);
            pushText(tmp);
            SDL_free(clip);
        }
        return 1;
    }
    {
        UiEvent e;
        memset(&e, 0, sizeof(e));
        e.kind = EV_KEY;
        e.a = (int)k;
        e.b = (int)ev->keysym.mod;
        evPush(&e);
    }
    return 1;
}

void netuiHostText(const char *utf8)
{
    if (SDL_AtomicGet(&s_open)) pushText(utf8);
}

int netuiHostMouseDown(const SDL_MouseButtonEvent *ev)
{
    UiEvent e;
    if (!SDL_AtomicGet(&s_open)) return 0;
    memset(&e, 0, sizeof(e));
    e.kind = EV_CLICK;
    e.a = ev->x;
    e.b = ev->y;
    e.c = ev->button;
    evPush(&e);
    return 1;
}

int netuiHostWheel(int dir)
{
    UiEvent e;
    if (!SDL_AtomicGet(&s_open) || dir == 0) return SDL_AtomicGet(&s_open);
    memset(&e, 0, sizeof(e));
    e.kind = EV_WHEEL;
    e.a = dir;
    evPush(&e);
    return 1;
}

void netuiHostPump(void)
{
    const int open = SDL_AtomicGet(&s_open);
    int want;
    if (!open && s_textActive < 0) return;   /* never used: leave SDL's default alone */
    want = open && SDL_AtomicGet(&s_textWanted);
    if (want != s_textActive) {
        s_textActive = want;
        if (want) {
            SDL_StartTextInput();
        } else {
            SDL_StopTextInput();
        }
    }
}

int netuiTitleStatus(char *out, int n)
{
    static NetClientStatus st;   /* host thread only */
    NetgameHud h;
    NetClient *c;
    if (!netRuntimeRunning()) return 0;
    c = netRuntimeClient();
    if (!c) return 0;
    netClientGetStatus(c, &st);
    netgameGetHud(&h);
    if (h.inMatch) {
        if (h.starting) {
            netStrCopy(out, n, "Online: starting the match...");
        } else if (h.waiting) {
            netStrCopy(out, n, "Online: waiting for players...  (Shift+F9 leaves)");
        } else {
            netStrFmt(out, n, "Online: player %d, %d ms, delay %d%s", h.localSlot + 1, h.pingMs, h.delay,
                      h.desync ? "  - OUT OF SYNC" : "");
        }
        return 1;
    }
    {
        char busy[96];
        if (netRuntimeOnlineBusy(busy, sizeof(busy)) && st.state != NCS_LOBBY) {
            netStrFmt(out, n, "Online: %s", busy);
            return 1;
        }
    }
    switch (st.state) {
    case NCS_CONNECTING:
        netStrCopy(out, n, "Online: connecting...");
        return 1;
    case NCS_CONNECTED:
        netStrCopy(out, n, "Online: connected to the server (F9)");
        return 1;
    case NCS_LOBBY:
        if ((st.lobby.flags & NL_QUICK) && st.lobby.num_players < 2) {
            netStrCopy(out, n, "Online: quick match - searching for players... (F9)");
        } else if ((st.lobby.flags & (NL_ONLINE | NL_SERVER)) && st.lobby.code[0]) {
            netStrFmt(out, n, "Online: lobby %s (%d/%d), code %s - F9", st.lobby.name,
                      (int)st.lobby.num_players, (int)st.lobby.max_players, st.lobby.code);
        } else {
            netStrFmt(out, n, "Online: lobby %s (%d/%d) - F9", st.lobby.name, (int)st.lobby.num_players,
                      (int)st.lobby.max_players);
        }
        return 1;
    case NCS_FAILED:
        netStrCopy(out, n, "Online: disconnected (F9)");
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------------ */
/* Model (scheduler thread only from here down)                              */
/* ------------------------------------------------------------------------ */

enum { PG_HOME, PG_LAN, PG_SETTINGS, PG_BROWSE, PG_CUSTOM, PG_QPREFS };
enum { SC_HOME, SC_LAN, SC_SETTINGS, SC_CONNECTING, SC_SERVER, SC_LOBBY, SC_MATCH, SC_BROWSE, SC_CUSTOM, SC_QPREFS };
enum { IN_NONE, IN_QUICK, IN_LIST, IN_CREATE_PUB, IN_CREATE_PRIV, IN_JOIN_CODE };

enum {
    RID_NONE = 0,
    RID_QUICK, RID_BROWSE, RID_CREATE_PUB, RID_CREATE_PRIV,
    RID_LAN, RID_HOST, RID_JOIN_ADDR, RID_SETTINGS, RID_CLOSE,
    RID_REFRESH, RID_LIST_ENTRY, RID_DISCONNECT,
    RID_LAN_SCAN, RID_LAN_ENTRY, RID_BACK,
    RID_SET_NAME, RID_SET_SERVER, RID_SET_PORT, RID_SET_STATS,
    RID_CANCEL,
    RID_CHAR, RID_HANDICAP, RID_CONTROL, RID_TEAM, RID_READY,
    RID_SCENARIO, RID_STAGE, RID_LENGTH, RID_WEAPONS, RID_AIMSIGHT, RID_DELAY, RID_OPT,
    RID_CHAT, RID_START, RID_LEAVE,
    RID_RESUME, RID_LEAVE_MATCH, RID_ABORT_MATCH,
    RID_INFO,
    /* D410 online service */
    RID_ON_QUICK, RID_ON_BROWSE, RID_ON_HOST_PUB, RID_ON_HOST_PRIV, RID_ON_JOIN_CODE, RID_ON_ENTRY,
    RID_ON_REFRESH, RID_CUSTOM, RID_SET_SERVICE, RID_SRV_JOIN_CODE,
    /* D412 quick-match preferences, browse filter */
    RID_ON_QPREFS, RID_QP_MODE, RID_QP_STAGE, RID_QP_WEAPONS, RID_QP_LENGTH, RID_QP_PLAYERS, RID_QP_SEARCH,
    RID_BR_FILTER
};

enum { RK_ACTION, RK_CHOICE, RK_TEXT, RK_INFO, RK_HEADER };

/* Editable text fields; a TEXT row is edited in place while selected. */
enum { TF_CODE, TF_ADDR, TF_CHAT, TF_NAME, TF_SERVER, TF_PORT, TF_SERVICE, TF_SRVCODE, TF_COUNT };
static char s_text[TF_COUNT][100];
static const int kTextMax[TF_COUNT] = {
    NET_CODE_LEN, 63, NET_CHAT_MAX - 1, NET_NAME_MAX - 1, 95, 5, 99, NET_CODE_LEN,
};

typedef struct UiRow {
    int kind;
    int id;
    int arg;
    int enabled;
    u32 ink;          /* 0 = by state */
    char label[40];
    char value[100];
} UiRow;

#define UI_MAX_ROWS 64
static UiRow s_rows[UI_MAX_ROWS];
static int s_nrows;

static struct {
    int running;
    NetClient *c;
    NetClientStatus st;
    NetgameHud hud;
    uint64_t now;
    int frontEnd;
    /* online service (D410) */
    const char *service;     /* URL in effect, "" = none */
    int onlineBusy;          /* an online join / quick match is talking to it */
    char onlineText[96];
    int hostingOnline;
    NetDirStatus dir;
} X;

static int s_page = PG_HOME;
static int s_intent = IN_NONE;
static uint64_t s_intentUs;
static uint32_t s_intentErrSeq;
static int s_selId = RID_NONE, s_selArg = 0, s_selPos = 0, s_scroll = 0;
static int s_wasOpen, s_wasInMatch;
static uint32_t s_seenErrorSeq, s_seenNoticeSeq;
static uint64_t s_noticeUntil;
static char s_status[100];
static u32 s_statusInk;
static uint64_t s_statusUntil;
static NetPlayerInfo s_me;
static uint64_t s_meUntil;
static NetSettings s_set;
static uint64_t s_setUntil;
static uint32_t s_lobbySeen;
static uint64_t s_listUs;
static NetLanGame s_lan[16];
static int s_nlan;
static uint64_t s_lanScanUs;
static char s_localIp[32];
static uint64_t s_localIpUs;
static uint64_t s_notFrontSinceUs;
static uint64_t s_dirListUs;
static int s_onlineAttempt;   /* the current connection came from the online service */
static int s_quickSearch;     /* ...and it is a quick match (D411): the "searching" screens */
static uint64_t s_searchStartUs;
static uint64_t s_countsUs;   /* last LIST asked for, to keep the online counts fresh */
static NdpPrefs s_qp;         /* quick-match preferences being edited (D412) */
static int s_qpLoaded, s_qpDirty;
static uint8_t s_browseFilter = NDP_ANY;   /* browse: one mode, or all */
static uint32_t s_hostedCodeShown;
static int s_autoReady;
static int s_sendPrefs;
static int s_prefDirty;   /* Net.Character changed: save when the panel closes */
static int s_inited;

static void setStatus(const char *msg, u32 ink, int seconds)
{
    netStrCopy(s_status, sizeof(s_status), msg);
    netSanitizeText(s_status, sizeof(s_status), "");
    s_statusInk = ink;
    s_statusUntil = X.now + (uint64_t)seconds * 1000000ull;
}

static void uiInit(void)
{
    if (s_inited) return;
    s_inited = 1;
    netStrCopy(s_text[TF_NAME], sizeof(s_text[0]), netgameCfgName());
    netStrCopy(s_text[TF_SERVER], sizeof(s_text[0]), netgameCfgServer());
    netStrFmt(s_text[TF_PORT], sizeof(s_text[0]), "%d", netgameCfgHostPort());
    netStrCopy(s_text[TF_SERVICE], sizeof(s_text[0]), netgameCfgService());
}

static void gather(void)
{
    X.now = netTimeUs();
    X.running = netRuntimeRunning();
    X.c = X.running ? netRuntimeClient() : NULL;
    if (X.c) {
        netClientGetStatus(X.c, &X.st);
    } else {
        memset(&X.st, 0, sizeof(X.st));
        X.st.state = NCS_IDLE;
        X.st.slot = -1;
    }
    netgameGetHud(&X.hud);
    X.frontEnd = netgameAtFrontEnd();
    X.service = netgameServiceUrl();
    if (X.running) {
        X.onlineBusy = netRuntimeOnlineBusy(X.onlineText, sizeof(X.onlineText));
        X.hostingOnline = netRuntimeIsHostingOnline();
        netDirGetStatus(&X.dir);
    } else {
        X.onlineBusy = 0;
        X.onlineText[0] = 0;
        X.hostingOnline = 0;
        memset(&X.dir, 0, sizeof(X.dir));
    }
}

static int screenNow(void)
{
    if (X.hud.inMatch) return SC_MATCH;
    if (X.c) {
        switch (X.st.state) {
        case NCS_LOBBY:      return SC_LOBBY;
        case NCS_CONNECTING: return SC_CONNECTING;
        case NCS_CONNECTED:  return SC_SERVER;
        default:             break;
        }
    }
    if (X.onlineBusy) return SC_CONNECTING;   /* asking the online service */
    switch (s_page) {
    case PG_LAN:      return SC_LAN;
    case PG_SETTINGS: return SC_SETTINGS;
    case PG_BROWSE:   return SC_BROWSE;
    case PG_CUSTOM:   return SC_CUSTOM;
    case PG_QPREFS:   return SC_QPREFS;
    default:          return SC_HOME;
    }
}

static int ensureRuntime(void)
{
    if (netRuntimeRunning()) return 1;
    if (netgameStartRuntime() != 0) {
        setStatus("Could not start networking (see the log)", INK_ERR, 8);
        return 0;
    }
    X.running = 1;
    X.c = netRuntimeClient();
    if (X.c) netClientGetStatus(X.c, &X.st);
    return 1;
}

/* ---- pending local edits (shown ahead of the lobby's echo) ---- */

static NetPlayerInfo myPlayer(void)
{
    NetPlayerInfo p;
    if (X.now < s_meUntil) return s_me;
    memset(&p, 0, sizeof(p));
    p.handicap = NG_DEFAULT_HANDICAP;
    if (X.st.slot >= 0 && X.st.slot < NET_MAX_PLAYERS) p = X.st.lobby.players[X.st.slot];
    return p;
}

static void sendMe(const NetPlayerInfo *m)
{
    s_me = *m;
    s_meUntil = X.now + 1500000ull;
    netClientSetPlayer(X.c, m->character, m->handicap, m->control, m->team, m->ready);
}

static NetSettings mySettings(void)
{
    return X.now < s_setUntil ? s_set : X.st.lobby.settings;
}

static void sendSettings(NetSettings *s)
{
    ngNormalizeSettings(s);
    s_set = *s;
    s_setUntil = X.now + 1500000ull;
    netClientSetSettings(X.c, s);
}

static int lobbyEditable(void)
{
    return X.c && X.st.state == NCS_LOBBY && X.st.lobby.state == NLS_WAITING;
}

/* 1 if the leader may start now; else the reason, worded for the panel. */
static int canStart(char *why, int n)
{
    const NetLobbyState *L = &X.st.lobby;
    NetSettings s = mySettings();
    int i;
    if (L->num_players < 2) {
        netStrCopy(why, n, "Waiting for another player");
        return 0;
    }
    for (i = 0; i < L->num_players; i++) {
        if (i != L->leader && !L->players[i].ready) {
            netStrFmt(why, n, "%s is not ready", L->players[i].name);
            return 0;
        }
    }
    if (ngValidateMatch(&s, L->players, L->num_players, why, n) != 0) return 0;
    if (!X.frontEnd) {
        netStrCopy(why, n, "Go to the main menu first");
        return 0;
    }
    return 1;
}

/* ---- intents (server actions that need a connection first) ---- */

static void fireIntent(void)
{
    char nm[NET_LOBBY_NAME_MAX];
    switch (s_intent) {
    case IN_QUICK:
        netClientQuickMatch(X.c);
        setStatus("Finding a match...", INK_META, 4);
        break;
    case IN_LIST:
        netClientRequestList(X.c);
        s_listUs = X.now;
        break;
    case IN_CREATE_PUB:
    case IN_CREATE_PRIV:
        netStrFmt(nm, sizeof(nm), "%s's game", netgameCfgName());
        netClientCreateLobby(X.c, nm, s_intent == IN_CREATE_PUB, NET_MAX_PLAYERS, 0);
        break;
    case IN_JOIN_CODE:
        netClientJoinLobby(X.c, 0, s_text[TF_SRVCODE]);
        break;
    default:
        break;
    }
    s_intent = IN_NONE;
}

static void serverAction(int intent)
{
    const char *srv = netgameCfgServer();
    if (intent == IN_JOIN_CODE && (int)strlen(s_text[TF_SRVCODE]) != NET_CODE_LEN) {
        setStatus("Type the 6-letter lobby code first", INK_WARN, 5);
        return;
    }
    if (X.c && X.st.state == NCS_CONNECTED && X.st.serverMode) {
        s_intent = intent;
        fireIntent();
        return;
    }
    if (!srv[0]) {
        setStatus("No matchmaking server set -- see Settings", INK_WARN, 6);
        return;
    }
    if (!ensureRuntime()) return;
    s_intent = intent;
    s_intentUs = X.now;
    s_intentErrSeq = X.st.errorSeq;
    netRuntimeConnect(srv, NET_DEFAULT_SERVER_PORT, 1);
    setStatus("Connecting to the server...", INK_META, 4);
}

static void hostAction(void)
{
    char err[128], nm[NET_LOBBY_NAME_MAX];
    if (!ensureRuntime()) return;
    netStrFmt(nm, sizeof(nm), "%s's game", netgameCfgName());
    if (netRuntimeHost((uint16_t)netgameCfgHostPort(), nm, NET_MAX_PLAYERS, err, sizeof(err)) != 0) {
        setStatus(err, INK_ERR, 8);
        return;
    }
    s_intent = IN_NONE;
    s_localIpUs = 0;
    setStatus("Starting your game...", INK_META, 3);
}

/* ---- the online service (D410) ---- */

static int onlineReady(void)
{
    if (!X.service[0]) {
        setStatus("No online service in this build -- see Settings", INK_WARN, 6);
        return 0;
    }
    if (!ensureRuntime()) return 0;
    if (X.dir.state == NDS_UNAVAILABLE && X.dir.lastError[0]) {
        setStatus(X.dir.lastError, INK_ERR, 7);
        return 0;
    }
    return 1;
}

static void qpLoad(void)
{
    unsigned char b[5];
    if (s_qpLoaded) return;
    netgameCfgQuickPrefs(b);
    s_qp.scenario = b[0];
    s_qp.stage = b[1];
    s_qp.weapons = b[2];
    s_qp.length = b[3];
    s_qp.players = b[4];
    ndpNormalizePrefs(&s_qp);
    s_qpLoaded = 1;
}

static void qpSave(void)
{
    unsigned char b[5];
    if (!s_qpDirty) return;
    b[0] = s_qp.scenario;
    b[1] = s_qp.stage;
    b[2] = s_qp.weapons;
    b[3] = s_qp.length;
    b[4] = s_qp.players;
    netgameCfgSetQuickPrefs(b);
    configSave();
    s_qpDirty = 0;
}

/* Step a preference through "any" then lo..hi (wrapping). */
static uint8_t qpCycle(uint8_t v, int dir, int lo, int hi)
{
    const int n = hi - lo + 2;   /* any + the values */
    int idx = (v == NDP_ANY || v < lo || v > hi) ? 0 : v - lo + 1;
    idx = ((idx + dir) % n + n) % n;
    return (uint8_t)(idx == 0 ? NDP_ANY : lo + idx - 1);
}

static void onlineQuickAction(void)
{
    if (!onlineReady()) return;
    qpLoad();
    qpSave();
    netRuntimeQuickOnline(&s_qp);
    s_onlineAttempt = 1;
    s_quickSearch = 1;
    s_searchStartUs = X.now;
    s_intent = IN_NONE;
}

/* "1:07" -- how long a quick match has been searching. */
static void fmtElapsed(char *out, int n, uint64_t sinceUs)
{
    unsigned secs = (unsigned)((X.now - sinceUs) / 1000000ull);
    netStrFmt(out, n, "%u:%02u", secs / 60, secs % 60);
}

static int quickLobby(void)
{
    return X.c && X.st.state == NCS_LOBBY && X.st.lobbyValid && (X.st.lobby.flags & NL_QUICK);
}

static void onlineHostAction(int isPublic)
{
    char err[128], nm[NET_LOBBY_NAME_MAX];
    if (!onlineReady()) return;
    netStrFmt(nm, sizeof(nm), "%s's game", netgameCfgName());
    if (netRuntimeHostOnline((uint16_t)netgameCfgHostPort(), nm, NET_MAX_PLAYERS, isPublic, 0, err, sizeof(err)) != 0) {
        setStatus(err, INK_ERR, 8);
        return;
    }
    s_intent = IN_NONE;
    s_onlineAttempt = 0;
    setStatus(isPublic ? "Starting your game -- it will be listed online" : "Starting your private game...",
              INK_META, 4);
}

static void onlineJoinCodeAction(void)
{
    if ((int)strlen(s_text[TF_CODE]) != NET_CODE_LEN) {
        setStatus("Type the 6-character game code first", INK_WARN, 5);
        return;
    }
    if (!onlineReady()) return;
    netRuntimeJoinOnline(0, s_text[TF_CODE]);
    s_onlineAttempt = 1;
    s_intent = IN_NONE;
}

static void browseOpen(void)
{
    if (!onlineReady()) return;
    s_page = PG_BROWSE;
    netDirRequestList();
    s_dirListUs = X.now;
}

static void joinAddrAction(void)
{
    if (!s_text[TF_ADDR][0]) {
        setStatus("Type the host's address first (e.g. 192.168.1.20)", INK_WARN, 5);
        return;
    }
    if (!ensureRuntime()) return;
    s_intent = IN_NONE;
    netRuntimeConnect(s_text[TF_ADDR], NET_DEFAULT_HOST_PORT, 0);
    setStatus("Connecting...", INK_META, 4);
}

static void lanOpen(void)
{
    if (!ensureRuntime()) return;
    s_page = PG_LAN;
    s_nlan = 0;
    netRuntimeLanScan(NET_DEFAULT_HOST_PORT);
    s_lanScanUs = X.now;
}

static void settingsOpen(void)
{
    netStrCopy(s_text[TF_NAME], sizeof(s_text[0]), netgameCfgName());
    netStrCopy(s_text[TF_SERVER], sizeof(s_text[0]), netgameCfgServer());
    netStrFmt(s_text[TF_PORT], sizeof(s_text[0]), "%d", netgameCfgHostPort());
    netStrCopy(s_text[TF_SERVICE], sizeof(s_text[0]), netgameCfgService());
    s_page = PG_SETTINGS;
}

static void settingsCommit(void)
{
    int port = atoi(s_text[TF_PORT]);
    netgameCfgSetName(s_text[TF_NAME]);
    netgameCfgSetServer(s_text[TF_SERVER]);
    netgameCfgSetService(s_text[TF_SERVICE]);
    if (port >= 1024 && port <= 65535) {
        netgameCfgSetHostPort(port);
    } else {
        setStatus("Host port must be 1024-65535", INK_WARN, 5);
    }
    netStrCopy(s_text[TF_NAME], sizeof(s_text[0]), netgameCfgName());
    netStrCopy(s_text[TF_SERVER], sizeof(s_text[0]), netgameCfgServer());
    netStrFmt(s_text[TF_PORT], sizeof(s_text[0]), "%d", netgameCfgHostPort());
    netStrCopy(s_text[TF_SERVICE], sizeof(s_text[0]), netgameCfgService());
    configSave();
}

static void leaveLobbyAction(void)
{
    if (X.st.serverMode) {
        netClientLeaveLobby(X.c);   /* back to the server's lobby list */
    } else {
        netRuntimeLeave();          /* direct: disconnect (and stop hosting) */
        s_page = PG_HOME;
    }
    s_meUntil = s_setUntil = 0;
}

/* ------------------------------------------------------------------------ */
/* Rows                                                                      */
/* ------------------------------------------------------------------------ */

static UiRow *addRow(int kind, int id, int arg, const char *label, const char *value)
{
    UiRow *r;
    if (s_nrows >= UI_MAX_ROWS) return NULL;
    r = &s_rows[s_nrows++];
    memset(r, 0, sizeof(*r));
    r->kind = kind;
    r->id = id;
    r->arg = arg;
    r->enabled = 1;
    netStrCopy(r->label, sizeof(r->label), label ? label : "");
    netStrCopy(r->value, sizeof(r->value), value ? value : "");
    return r;
}

static void header(const char *label)
{
    addRow(RK_HEADER, RID_NONE, 0, label, NULL);
}

static void info(const char *label, const char *value, u32 ink)
{
    UiRow *r = addRow(RK_INFO, RID_INFO, 0, label, value);
    if (r) r->ink = ink;
}

static UiRow *action(int id, int arg, const char *label, const char *value, int enabled)
{
    UiRow *r = addRow(RK_ACTION, id, arg, label, value);
    if (r) r->enabled = enabled;
    return r;
}

static void choice(int id, int arg, const char *label, const char *value, int editable)
{
    if (editable) {
        addRow(RK_CHOICE, id, arg, label, value);
    } else {
        info(label, value, 0);
    }
}

static void textRow(int id, int field, const char *label)
{
    addRow(RK_TEXT, id, field, label, s_text[field]);
}

/* The ONLINE header: what the service says (players / open games), or why
 * it is not there. */
static void onlineHeader(char *out, int n)
{
    if (!X.service[0]) {
        netStrCopy(out, n, "ONLINE  (NOT SET UP IN THIS BUILD)");
    } else if (X.dir.state == NDS_UNAVAILABLE) {
        netStrCopy(out, n, "ONLINE  (UNAVAILABLE)");
    } else if (X.dir.state == NDS_RETRY) {
        netStrCopy(out, n, "ONLINE  (CANNOT REACH THE SERVICE)");
    } else if (X.dir.infoSeq && X.dir.state == NDS_ONLINE) {
        netStrFmt(out, n, "ONLINE  %d PLAYER%s, %d GAME%s OPEN", (int)X.dir.info.online,
                  X.dir.info.online == 1 ? "" : "S", (int)X.dir.info.lobbies, X.dir.info.lobbies == 1 ? "" : "S");
    } else {
        netStrCopy(out, n, X.running ? "ONLINE  (CONNECTING...)" : "ONLINE");
    }
}

static void buildHome(void)
{
    const int on = X.service[0] != 0 && X.dir.state != NDS_UNAVAILABLE;
    char v[100], label[48];
    onlineHeader(label, sizeof(label));
    header(label);
    qpLoad();
    ndpPrefsLabel(&s_qp, v, sizeof(v));
    if (on && X.dir.infoSeq && X.dir.info.searching) {
        size_t len = strlen(v);
        netStrFmt(v + len, (int)(sizeof(v) - len), ", %d searching", (int)X.dir.info.searching);
    }
    action(RID_ON_QUICK, 0, "QUICK MATCH", on ? v : "", on);
    action(RID_ON_QPREFS, 0, "QUICK MATCH SETTINGS", "", on);
    action(RID_ON_BROWSE, 0, "BROWSE GAMES", "", on);
    action(RID_ON_HOST_PUB, 0, "HOST A PUBLIC GAME", "", on);
    action(RID_ON_HOST_PRIV, 0, "HOST A PRIVATE GAME", "code only", on);
    if (on) {
        textRow(RID_ON_JOIN_CODE, TF_CODE, "JOIN BY CODE");
    } else {
        action(RID_ON_JOIN_CODE, 0, "JOIN BY CODE", "", 0);
    }
    header("LAN AND DIRECT");
    action(RID_LAN, 0, "LAN GAMES", "", 1);
    netStrFmt(v, sizeof(v), "UDP %d", netgameCfgHostPort());
    action(RID_HOST, 0, "HOST ON THIS PC", v, 1);
    textRow(RID_JOIN_ADDR, TF_ADDR, "JOIN ADDRESS");
    action(RID_CUSTOM, 0, "OWN SERVER", netgameCfgServer()[0] ? netgameCfgServer() : "ge007-netserver", 1);
    header("");
    action(RID_SETTINGS, 0, "SETTINGS", netgameCfgName(), 1);
    action(RID_CLOSE, 0, "CLOSE", "", 1);
}

static void buildBrowse(void)
{
    const NdpListed *L = &X.dir.list;
    char label[48], v[100];
    int i;
    /* Keep the list fresh while it is on screen. */
    if (X.now - s_dirListUs > 5000000ull) {
        netDirRequestList();
        s_dirListUs = X.now;
    }
    int shown = 0;
    action(RID_ON_REFRESH, 0, "REFRESH", "", 1);
    action(RID_ON_QUICK, 0, "QUICK MATCH", "", 1);
    choice(RID_BR_FILTER, 0, "SHOW", s_browseFilter == NDP_ANY ? "All modes" : ngScenarioName(s_browseFilter), 1);
    for (i = 0; X.dir.listSeq && i < L->n && i < NDP_LIST_MAX; i++) {
        if (s_browseFilter == NDP_ANY || L->e[i].scenario == s_browseFilter) shown++;
    }
    netStrFmt(label, sizeof(label), "OPEN GAMES (%d)", shown);
    header(label);
    if (!X.dir.listSeq) {
        info("", X.dir.state == NDS_RETRY ? "Cannot reach the online service" : "Asking the online service...",
             X.dir.state == NDS_RETRY ? INK_ERR : INK_DIM);
    } else if (L->n == 0) {
        info("", "None right now -- host one, or quick match", INK_DIM);
    }
    if (X.dir.listSeq && L->n && !shown) info("", "None of that mode right now", INK_DIM);
    for (i = 0; X.dir.listSeq && i < L->n && i < NDP_LIST_MAX; i++) {
        const NdpListEntry *e = &L->e[i];
        const int ok = e->state == NDPS_WAITING && e->numPlayers < e->maxPlayers;
        if (s_browseFilter != NDP_ANY && e->scenario != s_browseFilter) continue;
        if (e->state != NDPS_WAITING) {
            netStrFmt(v, sizeof(v), "%d/%d playing %s", (int)e->numPlayers, (int)e->maxPlayers,
                      ngStageName(e->stage));
        } else {
            netStrFmt(v, sizeof(v), "%d/%d %s, %s", (int)e->numPlayers, (int)e->maxPlayers,
                      ngScenarioName(e->scenario), e->stage ? ngStageName(e->stage) : "random map");
        }
        action(RID_ON_ENTRY, i, e->name[0] ? e->name : e->hostName, v, ok);
    }
    header("");
    action(RID_BACK, 0, "BACK", "", 1);
}

/* What kind of game Quick Match looks for (D412). A field the chosen mode
 * fixes is shown, not offered. */
static void buildQPrefs(void)
{
    const int on = X.service[0] != 0 && X.dir.state != NDS_UNAVAILABLE;
    const int team = s_qp.scenario != NDP_ANY && ngScenarioIsTeam(s_qp.scenario);
    char v[100];
    qpLoad();
    choice(RID_QP_MODE, 0, "MODE", s_qp.scenario == NDP_ANY ? "Any" : ngScenarioName(s_qp.scenario), 1);
    choice(RID_QP_STAGE, 0, "MAP", s_qp.stage == NDP_ANY ? "Any" : ngStageName(s_qp.stage), 1);
    if (s_qp.scenario == NG_SCENARIO_MWTGG) {
        info("WEAPONS", "Golden gun (set by the mode)", INK_DIM);
    } else {
        choice(RID_QP_WEAPONS, 0, "WEAPONS", s_qp.weapons == NDP_ANY ? "Any" : ngWeaponsName(s_qp.weapons), 1);
    }
    if (s_qp.scenario == NG_SCENARIO_YOLT) {
        info("LENGTH", "Last one standing (set by the mode)", INK_DIM);
    } else {
        choice(RID_QP_LENGTH, 0, "LENGTH", s_qp.length == NDP_ANY ? "Any" : ngLengthName(s_qp.length), 1);
    }
    if (team) {
        netStrFmt(v, sizeof(v), "%d (set by the mode)", (int)s_qp.players);
        info("PLAYERS", v, INK_DIM);
    } else {
        if (s_qp.players == NDP_ANY) {
            netStrCopy(v, sizeof(v), "Any");
        } else {
            netStrFmt(v, sizeof(v), "%d", (int)s_qp.players);
        }
        choice(RID_QP_PLAYERS, 0, "PLAYERS", v, 1);
    }
    header("");
    info("", "Any = no preference. Fewer choices find a game sooner.", INK_DIM);
    action(RID_QP_SEARCH, 0, "SEARCH NOW", "", on);
    action(RID_BACK, 0, "SAVE AND GO BACK", "", 1);
}

/* ge007-netserver: a matchmaking server someone runs themselves (D409). */
static void buildCustom(void)
{
    const int has = netgameCfgServer()[0] != 0;
    textRow(RID_SET_SERVER, TF_SERVER, "SERVER");
    header("ON THAT SERVER");
    action(RID_QUICK, 0, "QUICK MATCH", has ? "" : "(set the server)", has);
    action(RID_BROWSE, 0, "BROWSE LOBBIES", "", has);
    action(RID_CREATE_PUB, 0, "CREATE PUBLIC LOBBY", "", has);
    action(RID_CREATE_PRIV, 0, "CREATE PRIVATE LOBBY", "", has);
    textRow(RID_SRV_JOIN_CODE, TF_SRVCODE, "JOIN BY CODE");
    header("");
    action(RID_BACK, 0, "BACK", "", 1);
}

static void buildLan(void)
{
    char label[40], v[100];
    int i;
    s_nlan = netRuntimeLanResults(s_lan, 16);
    action(RID_LAN_SCAN, 0, "SCAN AGAIN", "", 1);
    netStrFmt(label, sizeof(label), "GAMES ON YOUR NETWORK (%d)", s_nlan);
    header(label);
    if (s_nlan == 0) {
        info("", X.now - s_lanScanUs < 2000000ull ? "Searching..." : "None found on this network", INK_DIM);
    }
    for (i = 0; i < s_nlan; i++) {
        const NetLanGame *g = &s_lan[i];
        int ok = g->compatible && g->state == NLS_WAITING && g->numPlayers < g->maxPlayers;
        if (!g->compatible) {
            netStrCopy(v, sizeof(v), "other version");
        } else if (g->state != NLS_WAITING) {
            netStrCopy(v, sizeof(v), "in a match");
        } else {
            netStrFmt(v, sizeof(v), "%d/%d", (int)g->numPlayers, (int)g->maxPlayers);
        }
        action(RID_LAN_ENTRY, i, g->name, v, ok);
    }
    header("");
    action(RID_BACK, 0, "BACK", "", 1);
}

static void buildSettings(void)
{
    textRow(RID_SET_NAME, TF_NAME, "YOUR NAME");
    textRow(RID_SET_SERVICE, TF_SERVICE, "ONLINE SERVICE");
    textRow(RID_SET_PORT, TF_PORT, "HOST PORT");
    choice(RID_SET_STATS, 0, "NET STATS IN MATCH", netgameCfgShowStats() ? "On" : "Off", 1);
    header("");
    action(RID_BACK, 0, "SAVE AND GO BACK", "", 1);
}

static void buildConnecting(void)
{
    if (s_quickSearch && (s_onlineAttempt || X.onlineBusy)) {
        char v[64];
        info("STATUS", X.onlineText[0] ? X.onlineText : "Looking for a game...", INK_WARN);
        fmtElapsed(v, sizeof(v), s_searchStartUs);
        info("SEARCHING FOR", v, 0);
        if (X.dir.infoSeq) {
            netStrFmt(v, sizeof(v), "%d online, %d searching", (int)X.dir.info.online, (int)X.dir.info.searching);
            info("PLAYERS", v, INK_META);
        }
        header("");
        action(RID_CANCEL, 0, "STOP SEARCHING", "", 1);
        return;
    }
    if (s_onlineAttempt || X.onlineBusy) {
        info("STATUS", X.onlineText[0] ? X.onlineText : "Asking the online service...", INK_WARN);
        if (X.c && X.st.state == NCS_CONNECTING) info("ADDRESS", X.st.hostAddr, INK_META);
    } else {
        info("CONNECTING TO", X.st.hostAddr, 0);
        if (s_intent == IN_QUICK) info("THEN", "quick match", INK_META);
    }
    header("");
    action(RID_CANCEL, 0, "CANCEL", "", 1);
}

static void buildServer(void)
{
    char label[40], v[100];
    int i;
    /* Keep the list fresh while it is on screen. */
    if (X.now - s_listUs > 5000000ull) {
        netClientRequestList(X.c);
        s_listUs = X.now;
    }
    action(RID_QUICK, 0, "QUICK MATCH", "", 1);
    action(RID_CREATE_PUB, 0, "CREATE PUBLIC LOBBY", "", 1);
    action(RID_CREATE_PRIV, 0, "CREATE PRIVATE LOBBY", "", 1);
    textRow(RID_SRV_JOIN_CODE, TF_SRVCODE, "JOIN BY CODE");
    netStrFmt(label, sizeof(label), "OPEN LOBBIES (%d)", X.st.listCount);
    header(label);
    if (X.st.listCount == 0) info("", "None yet -- create one, or quick match", INK_DIM);
    for (i = 0; i < X.st.listCount && i < NET_LIST_MAX; i++) {
        const NetListEntry *e = &X.st.list[i];
        int ok = e->state == NLS_WAITING && e->num_players < e->max_players;
        if (e->state != NLS_WAITING) {
            netStrFmt(v, sizeof(v), "%d/%d playing", (int)e->num_players, (int)e->max_players);
        } else {
            netStrFmt(v, sizeof(v), "%d/%d %s", (int)e->num_players, (int)e->max_players,
                      ngScenarioName(e->scenario));
        }
        action(RID_LIST_ENTRY, i, e->name, v, ok);
    }
    header("");
    action(RID_REFRESH, 0, "REFRESH", "", 1);
    action(RID_DISCONNECT, 0, "DISCONNECT", "", 1);
}

static const char *const kOptLabel[] = {
    "INVERT LOOK", "AUTO-AIM", "AIM CONTROL", "SIGHT ON SCREEN", "LOOK AHEAD", "AMMO ON SCREEN",
};
static const int kOptBit[] = {
    NG_OPT_INVERTLOOK, NG_OPT_AUTOAIM, NG_OPT_AIMCONTROL, NG_OPT_SIGHTONSCREEN, NG_OPT_LOOKAHEAD,
    NG_OPT_DISPLAYAMMO,
};

static void buildLobby(void)
{
    const NetLobbyState *L = &X.st.lobby;
    const int editable = lobbyEditable();
    const int leader = X.st.isLeader;
    NetPlayerInfo me = myPlayer();
    NetSettings set = mySettings();
    const int team = ngScenarioIsTeam(set.scenario);
    const int quick = (L->flags & NL_QUICK) != 0;   /* matchmaking: fixed rules, autostart */
    const int setRules = editable && leader && !quick;
    char label[40], v[100], why[96];
    int i, n, first;

    netStrFmt(label, sizeof(label), "PLAYERS %d/%d", (int)L->num_players, (int)L->max_players);
    header(label);
    for (i = 0; i < L->max_players && i < NET_MAX_PLAYERS; i++) {
        const NetPlayerInfo *p = &L->players[i];
        if (i < L->num_players && p->used) {
            const char *state = i == L->leader ? "leader" : p->ready ? "ready" : "not ready";
            if (!p->connected) state = "gone";
            netStrFmt(label, sizeof(label), "%d %s%s", i + 1, p->name, i == X.st.slot ? " (you)" : "");
            if (team) {
                netStrFmt(v, sizeof(v), "%s  T%d  %s", ngCharacterName(p->character), p->team + 1, state);
            } else {
                netStrFmt(v, sizeof(v), "%s  %s", ngCharacterName(p->character), state);
            }
            if (i != X.st.slot && p->ping) {
                size_t len = strlen(v);
                netStrFmt(v + len, (int)(sizeof(v) - len), "  %dms", (int)p->ping);
            }
            info(label, v, i == X.st.slot ? INK_SEL : 0);
        } else {
            netStrFmt(label, sizeof(label), "%d", i + 1);
            info(label, "open", INK_DIM);
        }
    }

    header("YOU");
    choice(RID_CHAR, 0, "CHARACTER", ngCharacterName(me.character), editable);
    choice(RID_HANDICAP, 0, "HEALTH", ngHandicapName(me.handicap), editable);
    choice(RID_CONTROL, 0, "CONTROLS", ngControlName(me.control), editable);
    if (team && (L->flags & NL_QUICK)) {
        info("TEAM", me.team ? "Team 2 (balanced by the game)" : "Team 1 (balanced by the game)", INK_META);
    } else if (team) {
        choice(RID_TEAM, 0, "TEAM", me.team ? "Team 2" : "Team 1", editable);
    }
    /* The leader's START is their ready -- except in auto-start lobbies
     * (quick match), where the server waits for everyone's flag. */
    if (!leader || (set.flags & NS_AUTOSTART)) {
        if (X.frontEnd || me.ready) {
            choice(RID_READY, 0, "READY", me.ready ? "Yes" : "No", editable);
        } else {
            info("READY", "go to the main menu", INK_WARN);
        }
    }

    header(quick ? "MATCH (QUICK MATCH RULES)" : leader ? "MATCH (YOU ARE THE LEADER)" : "MATCH (THE LEADER SETS)");
    choice(RID_SCENARIO, 0, "SCENARIO", ngScenarioName(set.scenario), setRules);
    choice(RID_STAGE, 0, "STAGE", ngStageName(set.stage), setRules);
    choice(RID_LENGTH, 0, "LENGTH", ngLengthName(set.length), setRules && set.scenario != NG_SCENARIO_YOLT);
    choice(RID_WEAPONS, 0, "WEAPONS", ngWeaponsName(set.weapons), setRules && set.scenario != NG_SCENARIO_MWTGG);
    choice(RID_AIMSIGHT, 0, "AIM/SIGHT", ngAimSightName(set.aimsight), setRules);
    if (set.delay) {
        netStrFmt(v, sizeof(v), "%d frames", (int)set.delay);
    } else {
        netStrCopy(v, sizeof(v), "Auto");
    }
    choice(RID_DELAY, 0, "INPUT DELAY", v, setRules);
    for (i = 0; i < (int)(sizeof(kOptBit) / sizeof(kOptBit[0])); i++) {
        const int on = (set.options & kOptBit[i]) != 0;
        const char *val = kOptBit[i] == NG_OPT_AIMCONTROL ? (on ? "Toggle" : "Hold") : (on ? "On" : "Off");
        choice(RID_OPT, kOptBit[i], kOptLabel[i], val, setRules);
    }

    header("CHAT");
    textRow(RID_CHAT, TF_CHAT, "SAY");
    n = (int)(X.st.noticeSeq < 4 ? X.st.noticeSeq : 4);
    first = (int)X.st.noticeSeq - n;
    for (i = 0; i < n; i++) {
        info("", X.st.notice[(uint32_t)(first + i) % NET_NOTICE_RING], INK_META);
    }

    header("");
    if (leader && !quick) {   /* quick-match games start by themselves */
        const int ok = editable && canStart(why, sizeof(why));
        action(RID_START, 0, "START MATCH", ok ? "" : why, ok);
    }
    action(RID_LEAVE, 0, quick ? "LEAVE QUICK MATCH" : "LEAVE LOBBY", "", 1);
}

static void buildMatch(void)
{
    char v[100];
    int i;
    if (X.hud.starting) {
        info("STATUS", X.frontEnd || X.hud.frame ? "Loading the stage..." : "Waiting for the menus...", INK_WARN);
    } else if (X.hud.waiting) {
        info("STATUS", "Waiting for players...", INK_WARN);
    } else {
        netStrFmt(v, sizeof(v), "Frame %d", X.hud.frame);
        info("STATUS", v, X.hud.desync ? INK_ERR : 0);
    }
    netStrFmt(v, sizeof(v), "%d ms, delay %d frames", X.hud.pingMs, X.hud.delay);
    info("CONNECTION", v, 0);
    if (X.hud.desync) info("WARNING", "Out of sync -- leave and rematch", INK_ERR);
    for (i = 0; i < X.hud.numPlayers && i < NET_MAX_PLAYERS; i++) {
        if (X.hud.discMask & (1 << i)) {
            char label[40];
            netStrFmt(label, sizeof(label), "PLAYER %d", i + 1);
            info(label, "left the match", INK_WARN);
        }
    }
    header("");
    action(RID_RESUME, 0, "RESUME", "", 1);
    action(RID_LEAVE_MATCH, 0, "LEAVE MATCH", "", 1);
    if (X.st.isLeader) action(RID_ABORT_MATCH, 0, "END MATCH FOR EVERYONE", "", 1);
}

static void buildRows(int sc)
{
    s_nrows = 0;
    switch (sc) {
    case SC_HOME:       buildHome(); break;
    case SC_LAN:        buildLan(); break;
    case SC_SETTINGS:   buildSettings(); break;
    case SC_CONNECTING: buildConnecting(); break;
    case SC_SERVER:     buildServer(); break;
    case SC_LOBBY:      buildLobby(); break;
    case SC_MATCH:      buildMatch(); break;
    case SC_BROWSE:     buildBrowse(); break;
    case SC_CUSTOM:     buildCustom(); break;
    case SC_QPREFS:     buildQPrefs(); break;
    default:            break;
    }
}

static int selectable(int i)
{
    return s_rows[i].kind == RK_ACTION || s_rows[i].kind == RK_CHOICE || s_rows[i].kind == RK_TEXT;
}

/* Find the selected row by identity; else the nearest selectable row. */
static int resolveSel(void)
{
    int i;
    for (i = 0; i < s_nrows; i++) {
        if (selectable(i) && s_rows[i].id == s_selId && s_rows[i].arg == s_selArg) {
            s_selPos = i;
            return i;
        }
    }
    if (s_selPos >= s_nrows) s_selPos = s_nrows - 1;
    if (s_selPos < 0) s_selPos = 0;
    for (i = s_selPos; i < s_nrows; i++) {
        if (selectable(i)) goto found;
    }
    for (i = s_selPos - 1; i >= 0; i--) {
        if (selectable(i)) goto found;
    }
    return -1;
found:
    s_selPos = i;
    s_selId = s_rows[i].id;
    s_selArg = s_rows[i].arg;
    return i;
}

static void selectIndex(int i)
{
    if (i < 0 || i >= s_nrows) return;
    s_selPos = i;
    s_selId = s_rows[i].id;
    s_selArg = s_rows[i].arg;
}

static void moveSel(int dir)
{
    int cur = resolveSel();
    int i;
    if (cur < 0) return;
    for (i = cur + dir; i >= 0 && i < s_nrows; i += dir) {
        if (selectable(i)) {
            selectIndex(i);
            return;
        }
    }
}

static void selectFirst(void)
{
    s_selId = RID_NONE;
    s_selArg = 0;
    s_selPos = 0;
    s_scroll = 0;
}

/* ------------------------------------------------------------------------ */
/* Actions                                                                   */
/* ------------------------------------------------------------------------ */

static int wrapAdd(int v, int dir, int n)
{
    v += dir;
    while (v < 0) v += n;
    while (v >= n) v -= n;
    return v;
}

static int lengthCount(int scenario)
{
    switch (scenario) {
    case NG_SCENARIO_TLD: return 4;   /* lengths 0..3 */
    default:              return 7;   /* 0..6 ("last one standing" is YOLT's) */
    }
}

static void adjust(const UiRow *r, int dir)
{
    if (r->kind != RK_CHOICE || dir == 0) return;
    if (r->id >= RID_QP_MODE && r->id <= RID_QP_PLAYERS) {
        qpLoad();
        switch (r->id) {
        case RID_QP_MODE:    s_qp.scenario = qpCycle(s_qp.scenario, dir, 0, NG_NUM_SCENARIOS - 1); break;
        case RID_QP_STAGE:   s_qp.stage = qpCycle(s_qp.stage, dir, 1, NG_NUM_STAGES - 1); break;
        case RID_QP_WEAPONS: s_qp.weapons = qpCycle(s_qp.weapons, dir, 0, NG_NUM_WEAPONSETS - 1); break;
        case RID_QP_LENGTH:
            s_qp.length = qpCycle(s_qp.length, dir, 0, s_qp.scenario == NG_SCENARIO_TLD ? 3 : NG_LENGTH_LAST - 1);
            break;
        case RID_QP_PLAYERS: s_qp.players = qpCycle(s_qp.players, dir, 2, NET_MAX_PLAYERS); break;
        default: break;
        }
        ndpNormalizePrefs(&s_qp);
        s_qpDirty = 1;
        return;
    }
    if (r->id == RID_BR_FILTER) {
        s_browseFilter = qpCycle(s_browseFilter, dir, 0, NG_NUM_SCENARIOS - 1);
        return;
    }
    if (r->id == RID_SET_STATS) {
        netgameCfgSetShowStats(!netgameCfgShowStats());
        configSave();
        return;
    }
    if (!lobbyEditable()) return;
    if (r->id >= RID_CHAR && r->id <= RID_READY) {
        NetPlayerInfo m = myPlayer();
        switch (r->id) {
        case RID_CHAR:
            m.character = (uint8_t)wrapAdd(m.character, dir, NG_NUM_CHARACTERS);
            netgameCfgSetCharacter(m.character);
            s_prefDirty = 1;
            break;
        case RID_HANDICAP: m.handicap = (uint8_t)wrapAdd(m.handicap, dir, NG_NUM_HANDICAPS); break;
        case RID_CONTROL:  m.control = (uint8_t)wrapAdd(m.control, dir, NG_NUM_CONTROLS); break;
        case RID_TEAM:     m.team = (uint8_t)(m.team ? 0 : 1); break;
        case RID_READY:    m.ready = (uint8_t)(m.ready ? 0 : 1); break;
        default: break;
        }
        sendMe(&m);
        return;
    }
    if (X.st.isLeader) {
        NetSettings s = mySettings();
        switch (r->id) {
        case RID_SCENARIO: s.scenario = (uint8_t)wrapAdd(s.scenario, dir, NG_NUM_SCENARIOS); break;
        case RID_STAGE:    s.stage = (uint8_t)wrapAdd(s.stage, dir, NG_NUM_STAGES); break;
        case RID_LENGTH:   s.length = (uint8_t)wrapAdd(s.length, dir, lengthCount(s.scenario)); break;
        case RID_WEAPONS:  s.weapons = (uint8_t)wrapAdd(s.weapons, dir, NG_NUM_WEAPONSETS); break;
        case RID_AIMSIGHT: s.aimsight = (uint8_t)wrapAdd(s.aimsight, dir, NG_NUM_AIMSIGHT); break;
        case RID_DELAY:    s.delay = (uint8_t)wrapAdd(s.delay, dir, NET_MAX_DELAY + 1); break;
        case RID_OPT:      s.options ^= (uint16_t)r->arg; break;
        default: return;
        }
        sendSettings(&s);
    }
}

static void submitText(const UiRow *r)
{
    switch (r->id) {
    case RID_JOIN_ADDR:
        joinAddrAction();
        break;
    case RID_CHAT:
        if (s_text[TF_CHAT][0]) {
            netClientChat(X.c, s_text[TF_CHAT]);
            s_text[TF_CHAT][0] = 0;
        }
        break;
    case RID_SET_NAME:
    case RID_SET_SERVER:
    case RID_SET_PORT:
    case RID_SET_SERVICE:
        settingsCommit();
        setStatus("Saved", INK_GOOD, 2);
        moveSel(1);
        break;
    case RID_ON_JOIN_CODE:
        onlineJoinCodeAction();
        break;
    case RID_SRV_JOIN_CODE:   /* a ge007-netserver lobby code */
        serverAction(IN_JOIN_CODE);
        break;
    default:
        break;
    }
}

static void closeOverlay(void)
{
    SDL_AtomicSet(&s_open, 0);
}

static void activate(const UiRow *r)
{
    char why[96];
    if (r->kind == RK_CHOICE) {
        adjust(r, 1);
        return;
    }
    if (r->kind == RK_TEXT) {
        submitText(r);
        return;
    }
    if (r->kind != RK_ACTION) return;
    switch (r->id) {
    case RID_QUICK:       serverAction(IN_QUICK); break;
    case RID_BROWSE:      serverAction(IN_LIST); break;
    case RID_CREATE_PUB:  serverAction(IN_CREATE_PUB); break;
    case RID_CREATE_PRIV: serverAction(IN_CREATE_PRIV); break;
    case RID_ON_QUICK:    onlineQuickAction(); break;
    case RID_ON_QPREFS:
        qpLoad();
        s_page = PG_QPREFS;
        selectFirst();
        break;
    case RID_QP_SEARCH:
        qpSave();
        s_page = PG_HOME;
        onlineQuickAction();
        break;
    case RID_ON_BROWSE:   browseOpen(); selectFirst(); break;
    case RID_ON_HOST_PUB: onlineHostAction(1); break;
    case RID_ON_HOST_PRIV: onlineHostAction(0); break;
    case RID_ON_JOIN_CODE:
        if (!r->enabled) setStatus("No online service in this build -- see Settings", INK_WARN, 6);
        break;
    case RID_ON_REFRESH:
        netDirRequestList();
        s_dirListUs = X.now;
        break;
    case RID_ON_ENTRY:
        if (r->arg >= 0 && r->arg < X.dir.list.n) {
            const NdpListEntry *e = &X.dir.list.e[r->arg];
            if (!r->enabled) {
                setStatus("That game is full or already playing", INK_WARN, 4);
            } else if (onlineReady()) {
                netRuntimeJoinOnline(e->id, e->code);
                s_onlineAttempt = 1;
            }
        }
        break;
    case RID_CUSTOM:
        s_page = PG_CUSTOM;
        selectFirst();
        break;
    case RID_LAN:         lanOpen(); break;
    case RID_HOST:        hostAction(); break;
    case RID_SETTINGS:    settingsOpen(); selectFirst(); break;
    case RID_CLOSE:
    case RID_RESUME:      closeOverlay(); break;
    case RID_REFRESH:
        netClientRequestList(X.c);
        s_listUs = X.now;
        break;
    case RID_LIST_ENTRY:
        if (r->arg >= 0 && r->arg < X.st.listCount) {
            if (!r->enabled) {
                setStatus("That lobby is full or playing", INK_WARN, 4);
            } else {
                netClientJoinLobby(X.c, X.st.list[r->arg].lobby_id, X.st.list[r->arg].code);
            }
        }
        break;
    case RID_DISCONNECT:
        netRuntimeLeave();
        s_page = PG_HOME;
        selectFirst();
        break;
    case RID_LAN_SCAN:
        netRuntimeLanScan(NET_DEFAULT_HOST_PORT);
        s_lanScanUs = X.now;
        break;
    case RID_LAN_ENTRY:
        if (r->arg >= 0 && r->arg < s_nlan) {
            if (!r->enabled) {
                setStatus(s_lan[r->arg].compatible ? "That game is full or playing"
                                                   : "That game runs a different version",
                          INK_WARN, 4);
            } else {
                netRuntimeConnect(s_lan[r->arg].addr, NET_DEFAULT_HOST_PORT, 0);
                setStatus("Connecting...", INK_META, 4);
            }
        }
        break;
    case RID_BACK:
        if (s_page == PG_SETTINGS || s_page == PG_CUSTOM) settingsCommit();
        if (s_page == PG_QPREFS) qpSave();
        s_page = PG_HOME;
        selectFirst();
        break;
    case RID_CANCEL:
        netRuntimeLeave();
        s_intent = IN_NONE;
        s_onlineAttempt = 0;
        s_quickSearch = 0;
        s_page = PG_HOME;
        selectFirst();
        break;
    case RID_START:
        if (!r->enabled) {
            if (!canStart(why, sizeof(why))) setStatus(why, INK_WARN, 4);
        } else {
            netClientStartMatch(X.c);
        }
        break;
    case RID_LEAVE:
        leaveLobbyAction();
        selectFirst();
        break;
    case RID_LEAVE_MATCH:
        netgameRequestLeaveMatch();
        closeOverlay();
        break;
    case RID_ABORT_MATCH:
        netClientAbortMatch(X.c);
        closeOverlay();
        break;
    default:
        break;
    }
}

static void back(int sc)
{
    switch (sc) {
    case SC_LAN:
    case SC_BROWSE:
        s_page = PG_HOME;
        selectFirst();
        break;
    case SC_SETTINGS:
    case SC_CUSTOM:
        settingsCommit();
        s_page = PG_HOME;
        selectFirst();
        break;
    case SC_QPREFS:
        qpSave();
        s_page = PG_HOME;
        selectFirst();
        break;
    case SC_CONNECTING:
    case SC_SERVER:
        netRuntimeLeave();
        s_intent = IN_NONE;
        s_onlineAttempt = 0;
        s_page = PG_HOME;
        selectFirst();
        break;
    default:   /* home / lobby / match: just close (never leaves anything) */
        closeOverlay();
        break;
    }
}

static void typeText(const char *s)
{
    int i = resolveSel(), f, len;
    if (i < 0 || s_rows[i].kind != RK_TEXT) return;
    f = s_rows[i].arg;
    len = (int)strlen(s_text[f]);
    for (; *s && len < kTextMax[f]; s++) {
        char ch = *s;
        if (f == TF_CODE || f == TF_SRVCODE) {
            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))) continue;
        } else if (f == TF_PORT) {
            if (ch < '0' || ch > '9') continue;
        } else if (f == TF_ADDR || f == TF_SERVER || f == TF_SERVICE) {
            if (ch == ' ') continue;
        }
        s_text[f][len++] = ch;
        s_text[f][len] = 0;
    }
}

static void backspace(void)
{
    int i = resolveSel(), len;
    if (i < 0 || s_rows[i].kind != RK_TEXT) return;
    len = (int)strlen(s_text[s_rows[i].arg]);
    if (len > 0) s_text[s_rows[i].arg][len - 1] = 0;
}

/* ------------------------------------------------------------------------ */
/* Layout + hit testing                                                      */
/* ------------------------------------------------------------------------ */

#define UI_LINE 14
#define UI_FOOTER_H 18

typedef struct UiLayout {
    s32 left, right, top, bottom;
    s32 titleY, subY, statusY, contentY, footerY;
    s32 labelX, valueX, valueR;
    int maxRows;
} UiLayout;

static UiLayout layoutFor(int nrows)
{
    UiLayout o;
    s32 w = viGetX(), h = viGetY();
    s32 cardW = w - 16;
    int drawn;
    if (cardW > 400) cardW = 400;
    o.left = (w - cardW) / 2;
    o.right = o.left + cardW;
    o.top = 8;
    o.titleY = o.top + 7;
    o.subY = o.top + 21;
    o.statusY = o.top + 33;
    o.contentY = o.top + 50;
    o.labelX = o.left + 12;
    o.valueR = o.right - 12;
    o.valueX = o.left + (cardW * 2) / 5;
    o.maxRows = (h - 6 - o.contentY - UI_FOOTER_H) / UI_LINE;
    if (o.maxRows < 3) o.maxRows = 3;
    drawn = nrows < o.maxRows ? nrows : o.maxRows;
    o.bottom = o.contentY + drawn * UI_LINE + UI_FOOTER_H;
    if (o.bottom > h - 6) o.bottom = h - 6;
    o.footerY = o.bottom - 13;
    return o;
}

static void updateScroll(const UiLayout *o, int sel)
{
    if (sel < 0) {
        s_scroll = 0;
        return;
    }
    if (sel < s_scroll) s_scroll = sel;
    if (sel >= s_scroll + o->maxRows) s_scroll = sel - o->maxRows + 1;
    /* Show the headers / info rows just above the first selectable row. */
    {
        int i, firstSel = -1;
        for (i = 0; i < s_nrows; i++) {
            if (selectable(i)) {
                firstSel = i;
                break;
            }
        }
        if (sel == firstSel && firstSel < o->maxRows) s_scroll = 0;
    }
    if (s_scroll > s_nrows - o->maxRows) s_scroll = s_nrows - o->maxRows;
    if (s_scroll < 0) s_scroll = 0;
}

static int rowAt(const UiLayout *o, double ox, double oy)
{
    int p, last = s_scroll + o->maxRows - 1;
    if (ox < o->left + 3 || ox >= o->right - 3) return -1;
    if (last >= s_nrows) last = s_nrows - 1;
    for (p = s_scroll; p <= last; p++) {
        double y = o->contentY + (p - s_scroll) * UI_LINE;
        if (oy >= y - 2 && oy < y + UI_LINE - 2) return p;
    }
    return -1;
}

/* ------------------------------------------------------------------------ */
/* Input                                                                     */
/* ------------------------------------------------------------------------ */

static void handleKey(int sc, int key, int mod)
{
    int i;
    (void)mod;
    switch (key) {
    case SDLK_UP:
    case SDLK_KP_8:
        moveSel(-1);
        break;
    case SDLK_DOWN:
    case SDLK_KP_2:
    case SDLK_TAB:
        moveSel(1);
        break;
    case SDLK_PAGEUP:
        for (i = 0; i < 6; i++) moveSel(-1);
        break;
    case SDLK_PAGEDOWN:
        for (i = 0; i < 6; i++) moveSel(1);
        break;
    case SDLK_LEFT:
    case SDLK_KP_4:
        i = resolveSel();
        if (i >= 0) adjust(&s_rows[i], -1);
        break;
    case SDLK_RIGHT:
    case SDLK_KP_6:
        i = resolveSel();
        if (i >= 0) adjust(&s_rows[i], 1);
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        i = resolveSel();
        if (i >= 0) activate(&s_rows[i]);
        break;
    case SDLK_ESCAPE:
        back(sc);
        break;
    case SDLK_BACKSPACE:
        backspace();
        break;
    case SDLK_DELETE:
        i = resolveSel();
        if (i >= 0 && s_rows[i].kind == RK_TEXT) s_text[s_rows[i].arg][0] = 0;
        break;
    default:
        break;
    }
}

static void handleClick(int sc, int mx, int my, int button)
{
    int32_t rx = 0, ry = 0, rw = 0, rh = 0;
    UiLayout o = layoutFor(s_nrows);
    double ox, oy;
    int p;
    gfx_get_ui_screen_rect(&rx, &ry, &rw, &rh);
    if (rw <= 0 || rh <= 0) return;
    ox = (double)(mx - rx) * (double)viGetX() / rw;
    oy = (double)(my - ry) * (double)viGetY() / rh;
    p = rowAt(&o, ox, oy);
    if (button == SDL_BUTTON_RIGHT) {
        if (p >= 0 && s_rows[p].kind == RK_CHOICE) {
            selectIndex(p);
            adjust(&s_rows[p], -1);
        } else {
            back(sc);
        }
        return;
    }
    if (button != SDL_BUTTON_LEFT || p < 0 || !selectable(p)) return;
    selectIndex(p);
    if (s_rows[p].kind != RK_TEXT) activate(&s_rows[p]);
}

/* Gamepad (controller 0): D-pad / left stick move and adjust with
 * hold-repeat, A accepts, B backs out, Start closes. */
static void handlePad(int sc, int reset)
{
    static int prevUp, prevDn, prevLf, prevRt, prevA, prevB, prevStart;
    static int repDir, repTimer, repAxis;
    const int up = inputPadButton(0, SDL_CONTROLLER_BUTTON_DPAD_UP) ||
                   inputPadAxis(0, SDL_CONTROLLER_AXIS_LEFTY) < -12000;
    const int dn = inputPadButton(0, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
                   inputPadAxis(0, SDL_CONTROLLER_AXIS_LEFTY) > 12000;
    const int lf = inputPadButton(0, SDL_CONTROLLER_BUTTON_DPAD_LEFT) ||
                   inputPadAxis(0, SDL_CONTROLLER_AXIS_LEFTX) < -12000;
    const int rt = inputPadButton(0, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
                   inputPadAxis(0, SDL_CONTROLLER_AXIS_LEFTX) > 12000;
    const int a = inputPadButton(0, SDL_CONTROLLER_BUTTON_A);
    const int b = inputPadButton(0, SDL_CONTROLLER_BUTTON_B);
    const int start = inputPadButton(0, SDL_CONTROLLER_BUTTON_START);
    int i;

    if (reset) {   /* opening: a button held through F9 is not a press */
        prevUp = up; prevDn = dn; prevLf = lf; prevRt = rt;
        prevA = a; prevB = b; prevStart = start;
        repDir = 0;
        return;
    }
    if (start && !prevStart) {
        closeOverlay();
    } else if (b && !prevB) {
        back(sc);
    } else if (a && !prevA) {
        i = resolveSel();
        if (i >= 0) activate(&s_rows[i]);
    } else {
        int vdir = (dn && !prevDn) ? 1 : (up && !prevUp) ? -1 : 0;
        int hdir = (rt && !prevRt) ? 1 : (lf && !prevLf) ? -1 : 0;
        if (vdir) {
            moveSel(vdir);
            repDir = vdir;
            repAxis = 0;
            repTimer = 18;
        } else if (hdir) {
            i = resolveSel();
            if (i >= 0) adjust(&s_rows[i], hdir);
            repDir = hdir;
            repAxis = 1;
            repTimer = 18;
        } else if (repDir && ((repAxis == 0 && (up || dn)) || (repAxis == 1 && (lf || rt)))) {
            if (--repTimer <= 0) {
                repTimer = 4;
                if (repAxis == 0) {
                    moveSel(repDir);
                } else {
                    i = resolveSel();
                    if (i >= 0) adjust(&s_rows[i], repDir);
                }
            }
        } else {
            repDir = 0;
        }
    }
    prevUp = up; prevDn = dn; prevLf = lf; prevRt = rt;
    prevA = a; prevB = b; prevStart = start;
}

/* ------------------------------------------------------------------------ */
/* Per-frame update                                                          */
/* ------------------------------------------------------------------------ */

static void update(void)
{
    /* errors from the client (FAIL replies, timeouts, kicks) */
    if (X.c && X.st.errorSeq != s_seenErrorSeq) {
        s_seenErrorSeq = X.st.errorSeq;
        /* while matchmaking works on it (an unreachable game -> the next
         * one), a failed connection is a step, not an error */
        if (X.st.lastError[0] && !X.onlineBusy) setStatus(X.st.lastError, INK_ERR, 7);
    }
    /* a quick match ends in a non-quick lobby, or with nothing going on */
    if (s_quickSearch && !X.onlineBusy &&
        ((X.c && X.st.state == NCS_LOBBY && X.st.lobbyValid && !(X.st.lobby.flags & NL_QUICK)) ||
         (!X.c || X.st.state == NCS_IDLE || X.st.state == NCS_FAILED))) {
        s_quickSearch = 0;
    }
    /* the online counts (players online / searching) arrive with a list;
     * ask for one now and then while a screen shows them */
    if (SDL_AtomicGet(&s_open) && X.running && X.service[0] && !X.hud.inMatch && s_page != PG_BROWSE &&
        (screenNow() == SC_HOME || screenNow() == SC_CONNECTING || quickLobby()) &&
        X.now - s_countsUs > 10000000ull) {
        netDirRequestList();
        s_countsUs = X.now;
    }
    /* chat / notices: a short toast while the panel is closed */
    if (X.c && X.st.noticeSeq != s_seenNoticeSeq) {
        s_seenNoticeSeq = X.st.noticeSeq;
        s_noticeUntil = X.now + 6000000ull;
    }
    /* pending server intent */
    if (s_intent != IN_NONE) {
        if (X.c && X.st.state == NCS_CONNECTED && X.st.serverMode) {
            fireIntent();
        } else if (!X.c || X.st.errorSeq != s_intentErrSeq ||
                   (X.st.state == NCS_IDLE && X.now - s_intentUs > 3000000ull)) {
            s_intent = IN_NONE;   /* the connection failed or was cancelled */
        }
    }
    /* a new lobby: forget local edits made for the previous one */
    if (X.st.state == NCS_LOBBY && X.st.lobby.lobby_id != s_lobbySeen) {
        s_lobbySeen = X.st.lobby.lobby_id;
        s_meUntil = s_setUntil = 0;
        s_autoReady = (X.st.lobby.flags & NL_QUICK) != 0;
        s_sendPrefs = 1;
        selectFirst();
    } else if (X.st.state != NCS_LOBBY) {
        s_lobbySeen = 0;
        s_autoReady = 0;
        s_sendPrefs = 0;
    }
    /* "Ready" promises the start will find this game on its menus (a start
     * anywhere else is declined, and the match runs without us). Away from
     * them for 2 s (not a menu-switch blink) -> withdraw it. */
    if (X.frontEnd || X.hud.inMatch) {
        s_notFrontSinceUs = 0;
    } else if (s_notFrontSinceUs == 0) {
        s_notFrontSinceUs = X.now;
    } else if (X.now - s_notFrontSinceUs > 2000000ull && lobbyEditable() &&
               (!X.st.isLeader || (X.st.lobby.settings.flags & NS_AUTOSTART))) {
        NetPlayerInfo m = myPlayer();
        if (m.ready) {
            m.ready = 0;
            sendMe(&m);
            setStatus("Not ready: go to the main menu to play", INK_WARN, 6);
        }
    }
    /* Arrival in a lobby: propose our remembered character and the control
     * style the PC bindings are tuned for (input.c offline override). */
    if (s_sendPrefs && lobbyEditable()) {
        NetPlayerInfo m = myPlayer();
        s_sendPrefs = 0;
        m.character = (uint8_t)netgameCfgCharacter();
        m.control = (uint8_t)inputPreferredControlStyle();
        sendMe(&m);
    }
    /* Quick match means "play now": ready up on arrival (from the menus). */
    if (s_autoReady && lobbyEditable() && X.frontEnd) {
        NetPlayerInfo m = myPlayer();
        s_autoReady = 0;
        if (!m.ready) {
            m.ready = 1;
            sendMe(&m);
        }
    }
    /* the match takes the screen; the lobby comes back after it */
    if (X.hud.inMatch && !s_wasInMatch) {
        closeOverlay();
    } else if (!X.hud.inMatch && s_wasInMatch && X.c && X.st.state == NCS_LOBBY) {
        SDL_AtomicSet(&s_open, 1);
        /* Quick match keeps going (D412): ready for the next match at once;
         * the countdown gives anyone who wants out time to leave. */
        if (X.st.lobby.flags & NL_QUICK) s_autoReady = 1;
    }
    s_wasInMatch = X.hud.inMatch;
    /* The online service: stay connected while the panel shows what it
     * knows (player counts, the game list), and only then. */
    if (SDL_AtomicGet(&s_open) && X.running && X.service[0] && !X.hud.inMatch) {
        netDirKeepAlive(10);
    }
    /* an online attempt ends in a lobby, or with an error the status shows */
    if (s_onlineAttempt && !X.onlineBusy && X.c &&
        (X.st.state == NCS_LOBBY || X.st.state == NCS_FAILED || X.st.state == NCS_IDLE)) {
        s_onlineAttempt = 0;
    }
    /* our online game got its code: say so once */
    if (X.hostingOnline && X.dir.hostedSeq && X.dir.hostedSeq != s_hostedCodeShown && X.dir.hosted.code[0]) {
        char msg[100];
        s_hostedCodeShown = X.dir.hostedSeq;
        netStrFmt(msg, sizeof(msg), "Online! Game code %s", X.dir.hosted.code);
        setStatus(msg, INK_GOOD, 8);
        s_noticeUntil = X.now + 8000000ull;
    }
    /* the host's address, for "tell your friends" */
    if (netRuntimeIsHosting() && (s_localIpUs == 0 || X.now - s_localIpUs > 10000000ull)) {
        if (netLocalIPv4(s_localIp, sizeof(s_localIp)) != 0) netStrCopy(s_localIp, sizeof(s_localIp), "this PC");
        s_localIpUs = X.now;
    }
}

/* ------------------------------------------------------------------------ */
/* Drawing                                                                   */
/* ------------------------------------------------------------------------ */

#define UI_BUF_CMDS 16384
#define UI_TEXT_RESERVE 1200   /* worst case for one string (~10 cmds / glyph) */
static Gfx s_buf[UI_BUF_CMDS];

static Gfx *fillRect(Gfx *gdl, s32 x0, s32 y0, s32 x1, s32 y1, u8 r, u8 g, u8 b, u8 a)
{
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gdl++, 0, 0, r, g, b, a);
    gDPFillRectangle(gdl++, x0, y0, x1, y1);
    return gdl;
}

static struct font *bodyFont(void) { return ptrFontBankGothic; }
static struct fontchar *bodyChars(void) { return ptrFontBankGothicChars; }
static struct font *metaFont(void)
{
    return ptrFontZurichBold && ptrFontZurichBoldChars ? ptrFontZurichBold : bodyFont();
}
static struct fontchar *metaChars(void)
{
    return ptrFontZurichBold && ptrFontZurichBoldChars ? ptrFontZurichBoldChars : bodyChars();
}

/* Copy with every byte outside the fonts' 0x20..0x7E range replaced. */
static void safeCopy(char *dst, int n, const char *src)
{
    int i = 0;
    for (; src && src[i] && i < n - 1; i++) {
        unsigned char ch = (unsigned char)src[i];
        dst[i] = (ch < 0x20 || ch > 0x7E) ? '?' : (char)ch;
    }
    dst[i] = 0;
}

static s32 textW(int meta, const char *s)
{
    char t[128];
    s32 h = 0, w = 0;
    safeCopy(t, sizeof(t), s);
    textMeasure(&h, &w, t, meta ? metaChars() : bodyChars(), meta ? metaFont() : bodyFont(), 0);
    return w;
}

static Gfx *text(Gfx *gdl, int meta, s32 x, s32 y, const char *s, u32 ink)
{
    char t[128];
    s32 px = x, py = y;
    if (!s || !s[0] || (gdl - s_buf) > UI_BUF_CMDS - UI_TEXT_RESERVE) return gdl;
    safeCopy(t, sizeof(t), s);
    return textRender(gdl, &px, &py, t, meta ? metaChars() : bodyChars(), meta ? metaFont() : bodyFont(),
                      ink, viGetX(), viGetY(), 0, 0);
}

/* Truncate with ".." to fit maxW; align: 0 left at x, 1 right-aligned at x. */
static Gfx *textFit(Gfx *gdl, int meta, s32 x, s32 y, s32 maxW, const char *s, u32 ink, int right)
{
    char t[128];
    int n;
    if (maxW < 10 || !s || !s[0]) return gdl;
    safeCopy(t, sizeof(t), s);
    if (textW(meta, t) > maxW) {
        n = (int)strlen(t);
        while (n > 1) {
            t[--n] = 0;
            if (n + 2 < (int)sizeof(t)) {
                t[n] = '.';
                t[n + 1] = '.';
                t[n + 2] = 0;
            }
            if (textW(meta, t) <= maxW) break;
            t[n] = 0;
        }
    }
    return text(gdl, meta, right ? x - textW(meta, t) : x, y, t, ink);
}

static Gfx *beginDl(Gfx *gdl)
{
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetTexturePersp(gdl++, G_TP_NONE);
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, viGetX(), viGetY());
    return gdl;
}

static Gfx *endDl(Gfx *gdl)
{
    gDPPipeSync(gdl++);
    gSPEndDisplayList(gdl++);
    return gdl;
}

static const char *screenTitle(int sc)
{
    switch (sc) {
    case SC_LAN:        return "ONLINE - LAN GAMES";
    case SC_BROWSE:     return "ONLINE - GAMES";
    case SC_CUSTOM:     return "ONLINE - OWN SERVER";
    case SC_QPREFS:     return "ONLINE - QUICK MATCH SETTINGS";
    case SC_SETTINGS:   return "ONLINE - SETTINGS";
    case SC_CONNECTING: return s_quickSearch ? "ONLINE - QUICK MATCH" : "ONLINE - CONNECTING";
    case SC_SERVER:     return "ONLINE - SERVER";
    case SC_LOBBY:      return (X.st.lobby.flags & NL_QUICK) ? "ONLINE - QUICK MATCH" : "ONLINE - LOBBY";
    case SC_MATCH:      return "ONLINE - MATCH";
    default:            return "ONLINE";
    }
}

/* The subtitle (and the always-visible state line under it). */
static void screenSub(int sc, char *sub, int subN, char *state, int stateN, u32 *stateInk)
{
    const NetLobbyState *L = &X.st.lobby;
    sub[0] = 0;
    state[0] = 0;
    *stateInk = INK_META;
    switch (sc) {
    case SC_HOME:
    case SC_BROWSE:
        netStrFmt(sub, subN, "Playing as %s", netgameCfgName());
        if (X.c && X.st.state == NCS_FAILED && X.st.lastError[0]) {
            netStrCopy(state, stateN, X.st.lastError);
            *stateInk = INK_ERR;
        } else if (X.service[0] && X.dir.state == NDS_UNAVAILABLE && X.dir.lastError[0]) {
            netStrCopy(state, stateN, X.dir.lastError);
            *stateInk = INK_ERR;
        } else if (X.dir.info.motd[0] && X.dir.state == NDS_ONLINE) {
            netStrCopy(state, stateN, X.dir.info.motd);
        } else {
            netStrCopy(state, stateN, "Everyone plays on their own PC");
        }
        break;
    case SC_QPREFS:
        netStrCopy(sub, subN, "What kind of game to look for");
        ndpPrefsLabel(&s_qp, state, stateN);
        break;
    case SC_CUSTOM:
        netStrCopy(sub, subN, "A ge007-netserver someone runs");
        netStrCopy(state, stateN, "tools_pc/netplay -- see docs/netplay.md");
        break;
    case SC_CONNECTING:
        netStrFmt(sub, subN, "Playing as %s", netgameCfgName());
        break;
    case SC_SERVER:
        netStrFmt(sub, subN, "Connected to %s", X.st.hostAddr);
        netStrFmt(state, stateN, "%d ms", (int)(X.st.rttMs + 0.5f));
        break;
    case SC_LOBBY:
        if (L->code[0]) {
            netStrFmt(sub, subN, "%s   CODE %s", L->name, L->code);
        } else if (L->flags & NL_ONLINE) {
            netStrFmt(sub, subN, "%s   CODE ...", L->name);
        } else {
            netStrCopy(sub, subN, L->name);
        }
        if (L->state == NLS_STARTING) {
            netStrCopy(state, stateN, "Match starting...");
            *stateInk = INK_WARN;
        } else if (L->state == NLS_IN_MATCH) {
            netStrCopy(state, stateN, "Waiting for the others to finish the match...");
            *stateInk = INK_WARN;
        } else if (L->countdown) {
            netStrFmt(state, stateN, (L->flags & NL_QUICK) ? "Match starts in %d..." : "Starting in %d...",
                      (int)L->countdown);
            *stateInk = INK_WARN;
        } else if (!X.frontEnd) {
            netStrCopy(state, stateN, "Go to the main menu to play");
            *stateInk = INK_WARN;
        } else if ((L->flags & NL_QUICK) && L->num_players < 2) {
            char t[16];
            fmtElapsed(t, sizeof(t), s_quickSearch ? s_searchStartUs : X.now);
            if (X.dir.infoSeq) {
                netStrFmt(state, stateN, "Searching for players... %s  (%d online)", t, (int)X.dir.info.online);
            } else {
                netStrFmt(state, stateN, "Searching for players... %s", t);
            }
            *stateInk = INK_WARN;
        } else if (L->flags & NL_QUICK) {
            netStrCopy(state, stateN, "Players found -- waiting for everyone to be ready");
        } else if (X.hostingOnline && (X.dir.state == NDS_RETRY || X.dir.state == NDS_UNAVAILABLE)) {
            netStrFmt(state, stateN, "Online service unreachable%s%s", X.dir.lastError[0] ? ": " : "",
                      X.dir.lastError);
            *stateInk = INK_ERR;
        } else if (X.hostingOnline && !L->code[0]) {
            netStrCopy(state, stateN, "Registering with the online service...");
        } else if (X.hostingOnline && L->num_players < 2) {
            netStrCopy(state, stateN, (L->flags & NL_PUBLIC) ? "Listed online -- waiting for players"
                                                             : "Give your friends the code to join");
        } else if (netRuntimeIsHosting() && !X.st.serverMode && !X.hostingOnline) {
            netStrFmt(state, stateN, "Others join: %s port %d", s_localIp[0] ? s_localIp : "this PC",
                      (int)netRuntimeHostPort());
        } else {
            netStrFmt(state, stateN, "%d ms to the host", (int)(X.st.rttMs + 0.5f));
        }
        break;
    case SC_MATCH:
        netStrFmt(sub, subN, "Player %d of %d", X.hud.localSlot + 1, X.hud.numPlayers);
        netStrCopy(state, stateN, "The match keeps running while this is open");
        *stateInk = INK_WARN;
        break;
    default:
        break;
    }
}

static Gfx *drawPanel(Gfx *gdl, int sc)
{
    char sub[100], state[100], val[110];
    u32 stateInk;
    const int sel = resolveSel();
    UiLayout o = layoutFor(s_nrows);
    const s32 W = viGetX(), H = viGetY();
    const int blink = ((X.now / 400000ull) & 1) == 0;
    int p, last;
    const char *help;

    updateScroll(&o, sel);
    last = s_scroll + o.maxRows - 1;
    if (last >= s_nrows) last = s_nrows - 1;
    screenSub(sc, sub, sizeof(sub), state, sizeof(state), &stateInk);

    gdl = beginDl(gdl);

    /* ---- pass 1: fills ---- */
    gdl = fillRect(gdl, 0, 0, W, H, 0, 0, 0, sc == SC_MATCH ? 90 : 150);
    gdl = fillRect(gdl, o.left, o.top, o.right, o.bottom, 5, 17, 13, 228);
    gdl = fillRect(gdl, o.left + 8, o.contentY - 5, o.right - 8, o.contentY - 4, 56, 135, 73, 195);
    gdl = fillRect(gdl, o.left + 3, o.footerY - 4, o.right - 3, o.bottom - 2, 1, 9, 7, 235);
    for (p = s_scroll; p <= last; p++) {
        s32 rowY = o.contentY + (p - s_scroll) * UI_LINE;
        if (p == sel) {
            gdl = fillRect(gdl, o.left + 5, rowY - 2, o.right - 5, rowY + UI_LINE - 3, 28, 72, 44, 215);
        }
        if (s_rows[p].kind == RK_HEADER && p > s_scroll) {
            gdl = fillRect(gdl, o.left + 8, rowY + 1, o.right - 8, rowY + 2, 40, 90, 55, 160);
        }
    }

    /* ---- pass 2: text ---- */
    gdl = microcode_constructor(gdl);
    gdl = text(gdl, 0, o.left + 10, o.titleY, screenTitle(sc), INK_TITLE);
    gdl = textFit(gdl, 1, o.right - 10, o.titleY, (o.right - o.left) / 2, "F9 CLOSE", INK_META, 1);
    gdl = textFit(gdl, 1, o.left + 10, o.subY, o.right - o.left - 20, sub, INK_ROW, 0);
    if (X.now < s_statusUntil && s_status[0]) {
        gdl = textFit(gdl, 1, o.left + 10, o.statusY, o.right - o.left - 20, s_status, s_statusInk, 0);
    } else {
        gdl = textFit(gdl, 1, o.left + 10, o.statusY, o.right - o.left - 20, state, stateInk, 0);
    }

    for (p = s_scroll; p <= last; p++) {
        const UiRow *r = &s_rows[p];
        s32 rowY = o.contentY + (p - s_scroll) * UI_LINE;
        u32 ink = r->ink ? r->ink : (p == sel ? INK_SEL : (r->enabled ? INK_ROW : INK_DIM));
        /* The label takes what it needs (up to 3/5 of the row); the value
         * gets the rest -- one fixed column truncates either side at 320. */
        s32 labelW = textW(0, r->label);
        s32 valueX;
        if (labelW > (o.valueR - o.labelX) * 3 / 5) labelW = (o.valueR - o.labelX) * 3 / 5;
        valueX = o.labelX + labelW + 8;
        switch (r->kind) {
        case RK_HEADER:
            if (r->label[0]) gdl = textFit(gdl, 1, o.labelX - 4, rowY + 3, o.right - o.left - 16, r->label, INK_HEAD, 0);
            break;
        case RK_INFO:
            if (!r->label[0]) {   /* a full-width line (chat, notes) */
                gdl = textFit(gdl, 0, o.labelX, rowY, o.valueR - o.labelX, r->value, r->ink ? r->ink : INK_META, 0);
            } else if (r->value[0]) {
                gdl = textFit(gdl, 0, o.labelX, rowY, labelW, r->label, INK_META, 0);
                gdl = textFit(gdl, 0, o.valueR, rowY, o.valueR - valueX, r->value, r->ink ? r->ink : INK_ROW, 1);
            } else {
                gdl = textFit(gdl, 0, o.labelX, rowY, o.valueR - o.labelX, r->label, r->ink ? r->ink : INK_META, 0);
            }
            break;
        case RK_TEXT: {
            const char *t = s_text[r->arg];
            if (valueX < o.valueX) valueX = o.valueX;   /* text fields line up */
            gdl = textFit(gdl, 0, o.labelX, rowY, labelW, r->label, ink, 0);
            if (p == sel) {
                netStrFmt(val, sizeof(val), "%s%s", t, blink ? "_" : " ");
            } else {
                netStrCopy(val, sizeof(val), t[0] ? t : "(type here)");
            }
            /* keep the end (the caret) visible while typing */
            {
                const char *show = val;
                s32 room = o.valueR - valueX;
                while (show[0] && show[1] && textW(0, show) > room) show++;
                gdl = text(gdl, 0, valueX, rowY, show, t[0] || p == sel ? ink : INK_DIM);
            }
            break;
        }
        case RK_CHOICE:
            gdl = textFit(gdl, 0, o.labelX, rowY, labelW, r->label, ink, 0);
            if (p == sel) {
                netStrFmt(val, sizeof(val), "< %s >", r->value);
            } else {
                netStrCopy(val, sizeof(val), r->value);
            }
            gdl = textFit(gdl, 0, o.valueR, rowY, o.valueR - valueX, val, ink, 1);
            break;
        default:   /* RK_ACTION */
            if (r->value[0]) {
                s32 lw = textW(0, r->label);
                s32 room = o.valueR - (o.labelX + lw + 10);
                gdl = textFit(gdl, 0, o.labelX, rowY, o.valueR - o.labelX, r->label, ink, 0);
                if (room > 20) {
                    gdl = textFit(gdl, 0, o.valueR, rowY, room, r->value,
                                  r->enabled ? INK_META : INK_DIM, 1);
                }
            } else {
                gdl = textFit(gdl, 0, o.labelX, rowY, o.valueR - o.labelX, r->label, ink, 0);
            }
            break;
        }
    }

    if (s_nrows > o.maxRows) {
        const char *arrows = s_scroll > 0 && last < s_nrows - 1 ? "^ v" : s_scroll > 0 ? "^" : "v";
        gdl = textFit(gdl, 1, o.right - 10, o.statusY, 30, arrows, INK_META, 1);
    }

    if (sel >= 0 && s_rows[sel].kind == RK_TEXT) {
        help = "TYPE   ENTER OK   ESC BACK   UP/DOWN MOVE";
    } else if (sel >= 0 && s_rows[sel].kind == RK_CHOICE) {
        help = "LEFT/RIGHT CHANGE   ESC/B BACK";
    } else {
        help = "ENTER/A SELECT   ESC/B BACK   F9 CLOSE";
    }
    gdl = textFit(gdl, 1, o.left + 10, o.footerY, o.right - o.left - 20, help, INK_META, 0);
    return gdl;
}

/* Closed: a one-line status in a match (and in a lobby), plus chat toasts.
 * Appends to gdl; returns it unchanged when there is nothing to show. */
static Gfx *drawHud(Gfx *gdl)
{
    char line[100];
    u32 ink = INK_ROW;
    int any = 0;
    const int toast = X.c && X.now < s_noticeUntil && X.st.noticeSeq > 0;
    const char *note = toast ? X.st.notice[(X.st.noticeSeq - 1) % NET_NOTICE_RING] : NULL;

    if (optionsOverlayIsOpen()) return gdl;   /* the F10 panel owns the screen */
    line[0] = 0;
    if (X.hud.inMatch) {
        if (X.hud.desync) {
            netStrCopy(line, sizeof(line), "OUT OF SYNC - F9");
            ink = INK_ERR;
        } else if (X.hud.waiting) {
            netStrCopy(line, sizeof(line), "WAITING FOR PLAYERS...");
            ink = INK_WARN;
        } else if (netgameCfgShowStats() && !X.hud.starting) {
            netStrFmt(line, sizeof(line), "P%d  %dMS  D%d", X.hud.localSlot + 1, X.hud.pingMs, X.hud.delay);
            ink = INK_META;
        }
    } else if (X.c && X.st.state == NCS_LOBBY) {
        if (X.st.lobby.countdown) {
            netStrFmt(line, sizeof(line), "ONLINE: STARTING IN %d - F9", (int)X.st.lobby.countdown);
            ink = INK_WARN;
        } else if ((X.st.lobby.flags & NL_QUICK) && X.st.lobby.num_players < 2) {
            char t[16];
            fmtElapsed(t, sizeof(t), s_quickSearch ? s_searchStartUs : X.now);
            netStrFmt(line, sizeof(line), "QUICK MATCH: SEARCHING FOR PLAYERS %s - F9", t);
            ink = INK_WARN;
        } else if ((X.st.lobby.flags & (NL_ONLINE | NL_SERVER)) && X.st.lobby.code[0]) {
            netStrFmt(line, sizeof(line), "ONLINE LOBBY %d/%d  CODE %s - F9", (int)X.st.lobby.num_players,
                      (int)X.st.lobby.max_players, X.st.lobby.code);
            ink = INK_META;
        } else {
            netStrFmt(line, sizeof(line), "ONLINE LOBBY %d/%d - F9", (int)X.st.lobby.num_players,
                      (int)X.st.lobby.max_players);
            ink = INK_META;
        }
    } else if (X.onlineBusy) {
        netStrFmt(line, sizeof(line), "ONLINE: %s", X.onlineText);
        ink = INK_WARN;
    }
    if (!line[0] && !note) return gdl;

    gdl = beginDl(gdl);
    gdl = microcode_constructor(gdl);
    if (line[0]) {
        gdl = text(gdl, 1, 8, 6, line, ink);
        any = 1;
    }
    if (note) gdl = textFit(gdl, 1, 8, any ? 18 : 6, viGetX() - 16, note, INK_ROW, 0);
    return gdl;
}

/* The waiting screen (D410): drawn on the frames the scheduler presents
 * while the game thread is parked in a lockstep wait (netgame.c). Who we
 * are waiting for comes from the client's view of the host: before the
 * start, who has finished loading; in a match, whose input for the frame
 * the host still lacks (none: the delay is between us and the host). */
static Gfx *drawWait(Gfx *gdl, const NetgameWait *w)
{
    const s32 W = viGetX(), H = viGetY();
    const int secs = w->ms / 1000;
    const int dots = (int)((X.now / 400000ull) % 4);
    char title[64], line[96];
    const char *who = NULL;
    int i, lagging = 0, y;

    if (w->kind == NGW_STALL) {
        for (i = 0; i < w->numPlayers && i < NET_MAX_PLAYERS; i++) {
            if ((X.hud.discMask & (1 << i)) || i == w->localSlot) continue;
            if (X.st.slotLatest[i] <= w->frame) {   /* inNext: records < it are in */
                lagging++;
                who = w->names[i];
            }
        }
        if (lagging == 1 && who && who[0]) {
            netStrFmt(title, sizeof(title), "WAITING FOR %s", who);
        } else if (lagging == 0 && w->localSlot >= 0 && w->localSlot < NET_MAX_PLAYERS &&
                   X.st.slotLatest[w->localSlot] <= w->frame) {
            netStrCopy(title, sizeof(title), "YOUR CONNECTION IS STALLING");
        } else if (lagging == 0) {
            netStrCopy(title, sizeof(title), "WAITING FOR THE HOST");
        } else {
            netStrCopy(title, sizeof(title), "WAITING FOR PLAYERS");
        }
    } else {
        netStrCopy(title, sizeof(title), "WAITING FOR PLAYERS");
    }
    netStrFmt(title + strlen(title), (int)(sizeof(title) - strlen(title)), "%.*s", dots, "...");

    gdl = beginDl(gdl);
    gdl = fillRect(gdl, 0, 0, W, H, 0, 0, 0, 255);
    gdl = fillRect(gdl, W / 2 - 120, H / 2 - 62, W / 2 + 120, H / 2 + 62, 5, 17, 13, 235);
    gdl = fillRect(gdl, W / 2 - 112, H / 2 - 40, W / 2 + 112, H / 2 - 39, 56, 135, 73, 195);
    gdl = microcode_constructor(gdl);
    gdl = text(gdl, 0, W / 2 - textW(0, title) / 2, H / 2 - 55, title, INK_TITLE);

    y = H / 2 - 32;
    for (i = 0; i < w->numPlayers && i < NET_MAX_PLAYERS; i++) {
        const char *state;
        u32 ink = INK_ROW;
        if (X.hud.discMask & (1 << i)) {
            state = "LEFT";
            ink = INK_DIM;
        } else if (w->kind == NGW_START) {
            if (!(X.st.waitMask & (1 << i))) {
                state = "LEFT";
                ink = INK_DIM;
            } else if (X.st.loadedMask & (1 << i)) {
                state = "READY";
                ink = INK_GOOD;
            } else {
                state = "LOADING";
                ink = INK_WARN;
            }
        } else if (X.st.slotLatest[i] > w->frame) {
            state = "OK";
            ink = INK_GOOD;
        } else {
            state = i == w->localSlot ? "SENDING" : "LAGGING";
            ink = INK_WARN;
        }
        netStrFmt(line, sizeof(line), "%d  %s%s", i + 1, w->names[i], i == w->localSlot ? " (YOU)" : "");
        gdl = textFit(gdl, 0, W / 2 - 104, y, 150, line, i == w->localSlot ? INK_SEL : INK_ROW, 0);
        gdl = textFit(gdl, 0, W / 2 + 104, y, 60, state, ink, 1);
        y += UI_LINE;
    }

    netStrFmt(line, sizeof(line), "%d SECOND%s", secs, secs == 1 ? "" : "S");
    gdl = textFit(gdl, 1, W / 2 - 104, H / 2 + 34, 100, line, INK_META, 0);
    gdl = textFit(gdl, 1, W / 2 + 104, H / 2 + 34, 140, "SHIFT+F9 LEAVES", INK_META, 1);
    if (w->kind == NGW_START && secs >= 20) {
        gdl = textFit(gdl, 1, W / 2 - 104, H / 2 + 47, 208, "A slow PC may still be loading the stage", INK_DIM, 0);
    } else if (w->kind == NGW_STALL && secs >= 10) {
        gdl = textFit(gdl, 1, W / 2 - 104, H / 2 + 47, 208, "Gives up after 60 seconds", INK_DIM, 0);
    }
    return gdl;
}

/* Close the display list built at s_buf: NULL if nothing was appended,
 * else its START (fast3d runs it from there). */
static Gfx *finishDl(Gfx *gdl)
{
    if (gdl == s_buf) return NULL;
    endDl(gdl);
    return s_buf;
}

Gfx *netuiEmit(void)
{
    int open, sc, justOpened, waitShown = 0;
    UiEvent e;
    NetgameWait wait;
    Gfx *gdl = s_buf;

    open = SDL_AtomicGet(&s_open);
    /* Fully idle (never used online, panel closed): append nothing. */
    if (!open && !netRuntimeRunning()) {
        s_wasOpen = 0;
        return NULL;
    }
    uiInit();
    gather();
    update();
    open = SDL_AtomicGet(&s_open);   /* update() may open / close it */
    netgameGetWait(&wait);
    if (wait.kind != NGW_NONE && wait.ms >= 300) {
        gdl = drawWait(gdl, &wait);
        waitShown = 1;
    }

    justOpened = open && !s_wasOpen;
    if (!open && s_wasOpen && s_prefDirty) {
        s_prefDirty = 0;
        configSave();   /* Net.Character, once per visit rather than per click */
    }
    s_wasOpen = open;
    if (!open) {
        SDL_AtomicSet(&s_textWanted, 0);
        evClear();
        return finishDl(waitShown ? gdl : drawHud(gdl));
    }
    inputSuspendForOverlay();   /* the cursor is free while the panel is up */
    if (justOpened) {
        evClear();
        selectFirst();
        if (SDL_AtomicSet(&s_reqHome, 0) && !X.hud.inMatch) s_page = PG_HOME;
        /* Opening online play starts networking, so the home page can show
         * who is online (the service connection drops when it closes). */
        if (X.service[0] && !X.running) ensureRuntime();
        if (SDL_AtomicSet(&s_cliOpen, 0) && SDL_AtomicSet(&s_cliQuick, 0)) {
            if (netgameCfgServer()[0]) {
                s_intent = IN_QUICK;   /* netgameInit already started connecting */
                s_intentUs = X.now;
                s_intentErrSeq = X.st.errorSeq;
            } else {
                setStatus("--net-quick needs a server: --net-server HOST or Settings", INK_WARN, 8);
            }
        }
    }

    sc = screenNow();
    buildRows(sc);
    handlePad(sc, justOpened);
    while (evPop(&e)) {
        sc = screenNow();
        buildRows(sc);
        switch (e.kind) {
        case EV_KEY:   handleKey(sc, e.a, e.b); break;
        case EV_TEXT:  typeText(e.text); break;
        case EV_CLICK: handleClick(sc, e.a, e.b, e.c); break;
        case EV_WHEEL: moveSel(e.a > 0 ? -1 : 1); break;
        default: break;
        }
        gather();
    }
    if (!SDL_AtomicGet(&s_open)) {   /* closed by an action this frame */
        s_wasOpen = 0;
        if (s_prefDirty) {
            s_prefDirty = 0;
            configSave();
        }
        SDL_AtomicSet(&s_textWanted, 0);
        return finishDl(waitShown ? gdl : drawHud(gdl));
    }
    sc = screenNow();
    buildRows(sc);
    {
        int sel = resolveSel();
        SDL_AtomicSet(&s_textWanted, sel >= 0 && s_rows[sel].kind == RK_TEXT);
    }
    return finishDl(drawPanel(gdl, sc));
}
