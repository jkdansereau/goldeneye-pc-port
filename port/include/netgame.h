/*
 * netgame.h -- online multiplayer: game-side glue (D413, D414).
 *
 * Design: docs/dev/NETPLAY-PLAN.md. Every peer runs the unmodified N64
 * multiplayer simulation for all players; only controller input travels.
 * This module turns the game into a lockstep peer:
 *   - starts a match through the front end's own MENU_RUN_STAGE route,
 *   - at stage load writes the agreed deterministic state (seeds, MP setup,
 *     RAM-only save slot, cheats off, frame counters),
 *   - feeds the controllers through joy.c's playback hook (one sample per
 *     frame, from the network bundle),
 *   - switches osGetCount() to the frame-locked virtual clock,
 *   - pins every port knob that changes which game code runs,
 *   - re-applies each player's port-side input (mouse look, crosshair,
 *     dedicated use / reload / gadget, free crouch) identically on every peer,
 *   - tells fast3d to present only the local player's view.
 * Inert (zero cost, zero traffic) until the player opens online play.
 */
#ifndef PORT_NETGAME_H
#define PORT_NETGAME_H

#ifdef __cplusplus
extern "C" {
#endif

/* main.c: register [Net] config, parse --net-* flags. No network activity. */
void netgameInit(void);

/* Game thread, once per frame (libultra.c, after each gfxFrameMsgQ receive). */
void netgameGameTick(void);
/* Game thread, src/boss.c, top of the per-stage loop (before any stage PRNG). */
void netgameOnStageLoad(int stage);

/* 1 from the match's stage load until it ends: every port knob that alters
 * which game code runs must return its N64-faithful value while this holds. */
int netgameSimPinned(void);
/* 1 while the netplay match owns controller input (libultra.c SI shim must
 * hand the game neutral pads; input.c is driven by netgame instead). */
int netgameOwnsInput(void);

/* PD mouse-aim hook (input.c portMouseAimPdGetTurn): the CURRENT player's
 * networked turn for this frame. */
int netgamePdTurn(float *tx, float *ty);

/* fast3d presentation: rect (logical 320x240 space, top-left origin) of the
 * local player's viewport, or 0 when the normal full-canvas view applies. */
int netgameLocalView(float *x, float *y, float *w, float *h);

/* Status for the overlay / window title. */
typedef struct NetgameHud {
    int inMatch;          /* from MATCH_START until the stage is left */
    int starting;         /* ...and still loading / waiting for the menus */
    int frame;
    int delay;
    int pingMs;
    int waiting;          /* currently stalled waiting for a peer */
    int desync;
    int discMask;
    int localSlot;
    int numPlayers;
} NetgameHud;
void netgameGetHud(NetgameHud *out);

/* User actions (any thread; executed on the game thread). */
void netgameRequestLeaveMatch(void);

/* Waiting screen (D414). While the game thread is parked waiting for the
 * other players -- the start barrier after loading, or a peer's input that
 * is late -- no frames are drawn. The scheduler thread asks
 * netgameWantWaitFrame() at every retrace and, when it says so, presents a
 * frame of its own (libultra.c), on which netui draws the waiting screen
 * from netgameGetWait(). */
enum { NGW_NONE = 0, NGW_START, NGW_STALL };
typedef struct NetgameWait {
    int kind;             /* NGW_* */
    int ms;               /* how long so far */
    unsigned frame;       /* the frame waited for (NGW_STALL) */
    int localSlot;
    int numPlayers;
    char names[4][16];
} NetgameWait;
void netgameGetWait(NetgameWait *out);
int netgameWantWaitFrame(void);

/* 1 while online play is doing something (connecting, in a lobby or a match,
 * looking for a game): the front end must not drift into its attract loop. */
int netgameOnlineActive(void);

/* Build id used for matching peers (same build, same region, same protocol). */
const char *netgameBuildId(void);

/* 1 on an interactive front-end screen with no menu switch in flight -- where
 * a match can start (game thread state; the overlay reads it for display). */
int netgameAtFrontEnd(void);

/* [Net] config + runtime start, for the online overlay (netui.c). Setters
 * sanitize; the caller persists with configSave(). */
const char *netgameCfgName(void);
void netgameCfgSetName(const char *name);
const char *netgameCfgServer(void);
void netgameCfgSetServer(const char *server);
int netgameCfgHostPort(void);
void netgameCfgSetHostPort(int port);
int netgameCfgShowStats(void);
void netgameCfgSetShowStats(int on);
int netgameCfgCharacter(void);
void netgameCfgSetCharacter(int character);
/* Starts the runtime and points it at the online service + STUN server. */
int netgameStartRuntime(void);

/* D414 online service: the URL in effect ("" = none), and the [Net] Service
 * setting behind it ("" = this build's default, "off" = none). */
const char *netgameServiceUrl(void);
const char *netgameCfgService(void);
void netgameCfgSetService(const char *url);

/* D416 quick-match preferences ([Net] QuickMode / QuickStage / QuickWeapons /
 * QuickLength / QuickPlayers), as five bytes in that order -- the layout of
 * NdpPrefs; 255 = any. Set normalises (GoldenEye's rules); caller saves. */
void netgameCfgQuickPrefs(unsigned char out[5]);
void netgameCfgSetQuickPrefs(const unsigned char in[5]);

#ifdef __cplusplus
}
#endif

#endif /* PORT_NETGAME_H */
