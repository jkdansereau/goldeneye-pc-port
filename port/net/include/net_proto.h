/*
 * net_proto.h -- GoldenEye netplay wire protocol (D409).
 *
 * See docs/dev/NETPLAY-PLAN.md §7 for the message table. Everything here is
 * game-independent: the host/server never runs the game, it only relays
 * controller records, so the server and selftest build without the decomp.
 */
#ifndef GE_NET_PROTO_H
#define GE_NET_PROTO_H

#include <stdint.h>

#include "net_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NET_PROTO_VERSION        2   /* 2: NP_PUNCH, NM_MATCH_WAIT (D410) */
#define NET_MAGIC                0x4E374547u   /* bytes 'G','E','7','N' on the wire */
#define NET_MAX_PACKET           1200
#define NET_HEADER_SIZE          12
#define NET_MAX_PLAYERS          4
#define NET_NAME_MAX             16            /* incl. NUL: 15 visible chars */
#define NET_BUILDID_MAX          64
#define NET_LOBBY_NAME_MAX       24
#define NET_CODE_LEN             6
#define NET_CHAT_MAX             80
#define NET_JOIN_PAD             256           /* JOIN is never smaller than a reply */
#define NET_DEFAULT_HOST_PORT    27007
#define NET_DEFAULT_SERVER_PORT  27008
#define NET_RING                 512           /* frames of input/bundle history */
#define NET_MAX_DELAY            12
#define NET_LIST_MAX             16

/* ---- packet types (header byte 5) ---- */
enum NetPacketType {
    NP_JOIN = 1,
    NP_JOIN_ACCEPT,
    NP_JOIN_REJECT,
    NP_SESSION,
    NP_QUERY,
    NP_QUERY_REPLY,
    NP_DISCONNECT,
    NP_PUNCH        /* NAT hole punch: opens the sender's NAT, ignored on receipt */
};

/* Addresses a peer can be reached at (public via STUN, LAN): online play
 * through the directory service tries every candidate (net_dir.h). */
#define NET_MAX_CANDS 4

/* JOIN modes */
enum NetJoinMode {
    NJ_DIRECT = 0,  /* join the host's single lobby (direct / LAN host) */
    NJ_SERVER = 1   /* lobbyless session on a matchmaking server */
};

enum NetRejectReason {
    NR_NONE = 0,
    NR_FULL,
    NR_PROTOCOL,
    NR_BUILD,
    NR_IN_MATCH,
    NR_SERVER_FULL,
    NR_BAD_REQUEST,
    NR_RATE_LIMIT
};

/* ---- session message types. < 32 unreliable, >= 32 reliable ---- */
enum NetMsgType {
    NM_PING = 1,
    NM_PONG,
    NM_INPUTS,
    NM_FRAMES,
    NM_MATCH_WAIT,   /* H->C during the start barrier: who has loaded */

    NM_LOBBY_STATE = 32,
    NM_PLAYER_SET,
    NM_SETTINGS_SET,
    NM_START_REQ,
    NM_ABORT_REQ,
    NM_MATCH_START,
    NM_MATCH_LOADED,
    NM_MATCH_GO,
    NM_MATCH_END,
    NM_MATCH_LEAVE,
    NM_HASH,
    NM_DESYNC,
    NM_LIST_REQ,
    NM_LIST,
    NM_CREATE,
    NM_JOIN_CODE,
    NM_QUICK,
    NM_JOINED,
    NM_FAIL,
    NM_LEAVE_LOBBY,
    NM_CHAT,
    NM_NOTICE,
    NM_KICK
};
#define NM_IS_RELIABLE(t) ((t) >= 32)

/* ---- controller record ---- */
#define NIR_LOOK    0x01   /* look_dtheta/look_dverta present                 */
#define NIR_CROSS   0x02   /* absolute crosshair + gun pose present (GEPD aim) */
#define NIR_PDTURN  0x04   /* pdturn_x/pdturn_y present                       */
#define NIR_STICKS  0x08   /* wire-only: stick bytes present                  */

#define NIA_USE          0x01   /* dedicated use (no reload fallback) */
#define NIA_RELOAD       0x02   /* dedicated reload                   */
#define NIA_GADGET       0x04   /* cycle owned gadget                 */
#define NIA_CROUCH_DOWN  0x08   /* free crouch held                   */
#define NIA_CROUCH_UP    0x10   /* free crouch released               */

typedef struct NetInputRec {
    uint16_t buttons;     /* N64 pad: the game reads these via joy.c */
    int8_t stick_x;
    int8_t stick_y;
    uint8_t flags;        /* NIR_* */
    uint8_t actions;      /* NIA_* */
    float look_dtheta;    /* added to player->vv_theta              */
    float look_dverta;    /* subtracted from player->vv_verta       */
    float cross_x;        /* player->crosshair_x_pos                */
    float cross_y;        /* player->crosshair_y_pos                */
    float gun_az;         /* player->gun_azimuth_angle              */
    float gun_turn;       /* player->gun_azimuth_turning            */
    float pdturn_x;       /* PD mouse-aim turn (gunfire.c hook)     */
    float pdturn_y;
} NetInputRec;

/* ---- frame bundle (all players' records for one frame) ---- */
#define NB_ABORT  0x01   /* every peer leaves the stage after this frame */

typedef struct NetBundle {
    uint32_t frame;
    uint8_t present;   /* slots that have a record */
    uint8_t disc;      /* slots disconnected from this frame on (neutral) */
    uint8_t flags;
    NetInputRec rec[NET_MAX_PLAYERS];
} NetBundle;

/* ---- lobby ---- */
typedef struct NetPlayerInfo {
    uint8_t used;
    uint8_t ready;
    uint8_t connected;
    uint8_t character;   /* mp_chr_setup[] index, 0..63            */
    uint8_t handicap;    /* MP_handicap_table[] index, 0..10        */
    uint8_t control;     /* controlstyle 0..3 (1.1 Honey..1.4)      */
    uint8_t team;        /* 0/1 (team scenarios)                    */
    uint16_t ping;       /* ms, as measured by the host             */
    char name[NET_NAME_MAX];
} NetPlayerInfo;

#define NS_AUTOSTART 0x01   /* server auto-starts when everyone is ready */

typedef struct NetSettings {
    uint8_t scenario;    /* MPSCENARIOS 0..7                         */
    uint8_t stage;       /* MP_STAGE_SELECTED 0..11 (0 = random)     */
    uint8_t length;      /* GAMELENGTH 0..7                          */
    uint8_t weapons;     /* mp_weapon_set 0..13                      */
    uint8_t aimsight;    /* aim_sight_adjustment 0..3                */
    uint8_t delay;       /* input delay frames, 0 = auto             */
    uint8_t flags;       /* NS_*                                     */
    uint16_t options;    /* save_data.options bits (OPTION_*)        */
} NetSettings;

#define NL_PUBLIC  0x01
#define NL_SERVER  0x02   /* hosted by a matchmaking server */
#define NL_QUICK   0x04   /* quick-match lobby */
#define NL_ONLINE  0x08   /* player-hosted, registered with the online service (D410) */

enum NetLobbyRunState {
    NLS_WAITING = 0,
    NLS_STARTING,
    NLS_IN_MATCH
};

typedef struct NetLobbyState {
    uint32_t revision;
    uint32_t lobby_id;
    char code[NET_CODE_LEN + 1];
    char name[NET_LOBBY_NAME_MAX];
    uint8_t flags;
    uint8_t state;
    uint8_t max_players;
    uint8_t num_players;
    uint8_t leader;       /* slot index */
    uint8_t countdown;    /* autostart seconds left, 0 = none */
    NetPlayerInfo players[NET_MAX_PLAYERS];
    NetSettings settings;
} NetLobbyState;

typedef struct NetMatchStart {
    uint32_t match_id;
    uint64_t seed_random;
    uint64_t seed_chrobj;
    uint8_t num_players;
    uint8_t delay;
    uint8_t stage;         /* resolved MP_STAGE index (never random) */
    NetSettings settings;
    NetPlayerInfo players[NET_MAX_PLAYERS];
} NetMatchStart;

typedef struct NetListEntry {
    uint32_t lobby_id;
    char name[NET_LOBBY_NAME_MAX];
    char leader[NET_NAME_MAX];
    char code[NET_CODE_LEN + 1];   /* empty for private lobbies */
    uint8_t num_players;
    uint8_t max_players;
    uint8_t state;
    uint8_t flags;
    uint8_t scenario;
    uint8_t stage;
} NetListEntry;

/* ---- header ---- */
typedef struct NetHeader {
    uint8_t version;
    uint8_t type;
    uint16_t length;      /* payload bytes after the header */
    uint32_t conn_id;
} NetHeader;

void netWriteHeader(NetW *w, uint8_t type, uint32_t connId);
/* Patch the payload length once the payload is written. */
void netFinishHeader(NetW *w);
/* 0 on success; rejects bad magic / version / length. */
int netReadHeader(NetR *r, NetHeader *h);

/* ---- record / bundle / lobby codecs ---- */
void netEncInputRec(NetW *w, const NetInputRec *rec);
int netDecInputRec(NetR *r, NetInputRec *rec);
void netEncBundle(NetW *w, const NetBundle *b);
int netDecBundle(NetR *r, NetBundle *b);
void netEncPlayerInfo(NetW *w, const NetPlayerInfo *p);
int netDecPlayerInfo(NetR *r, NetPlayerInfo *p);
void netEncSettings(NetW *w, const NetSettings *s);
int netDecSettings(NetR *r, NetSettings *s);
void netEncLobby(NetW *w, const NetLobbyState *l);
int netDecLobby(NetR *r, NetLobbyState *l);
void netEncMatchStart(NetW *w, const NetMatchStart *m);
int netDecMatchStart(NetR *r, NetMatchStart *m);
void netEncListEntry(NetW *w, const NetListEntry *e);
int netDecListEntry(NetR *r, NetListEntry *e);

/* Replace anything that is not printable ASCII (GE's fonts cover 0x20..0x7E)
 * and trim; an empty result becomes `fallback`. */
void netSanitizeText(char *s, int cap, const char *fallback);

/* Records compare equal iff every field the game consumes matches. */
int netInputRecEq(const NetInputRec *a, const NetInputRec *b);

const char *netRejectReasonText(int reason);

#ifdef __cplusplus
}
#endif

#endif /* GE_NET_PROTO_H */
