/*
 * net_dirproto.h -- the online directory protocol (D410): messages between
 * the game and the central matchmaking service (tools_pc/netplay/cloudflare,
 * a Cloudflare Worker + Durable Object). Mirrored byte for byte by
 * tools_pc/netplay/cloudflare/src/protocol.js -- change both together.
 *
 * The service is a directory and rendezvous point only: it lists lobbies,
 * hands out 6-letter codes, matches quick-match players, and introduces a
 * joiner to a host (each side's public + LAN addresses) so they can connect
 * directly over UDP. Gameplay never passes through it.
 *
 * Transport: one message per binary WebSocket frame (wss://SERVICE/api/v1/ws),
 * or -- where WebSockets are unavailable -- a batch per HTTPS POST to
 * SERVICE/api/v1/poll, each message prefixed by a u16 length (both ways).
 *
 * Encoding: little-endian (net_wire.h); strings are u8 length + bytes;
 * an address is u32 IPv4 (host order) + u16 port.
 */
#ifndef GE_NET_DIRPROTO_H
#define GE_NET_DIRPROTO_H

#include <stdint.h>

#include "net_proto.h"
#include "net_sock.h"
#include "net_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NDP_VERSION        3   /* 2: QUICK excludeId, WELCOME / LISTED searching (D411)
                                 * 3: quick-match preferences; lobby weapons / length (D412) */
#define NDP_ANY            0xFF   /* a quick-match preference that takes anything */
#define NDP_MAX_MSG        2048   /* service rejects larger messages */
#define NDP_TOKEN_MAX      33     /* 32 hex chars + NUL */
#define NDP_COUNTRY_MAX    3
#define NDP_MOTD_MAX       96
#define NDP_LIST_MAX       32

/* client -> service */
enum NdpClientMsg {
    NDP_HELLO = 1,    /* first on every connection / POST batch */
    NDP_LIST,         /* -> NDP_LISTED */
    NDP_HOST,         /* register / refresh my lobby -> NDP_HOSTED */
    NDP_UNHOST,
    NDP_JOIN,         /* by id or code -> NDP_JOININFO | NDP_ERROR */
    NDP_QUICK,        /* -> NDP_JOININFO | NDP_QUICKHOST | NDP_ERROR */
    NDP_RESULT,       /* finished match, for the status page */
    NDP_POLL          /* HTTP transport: pending NDP_JOINREQ for my lobby */
};

/* service -> client */
enum NdpServiceMsg {
    NDP_WELCOME = 101,
    NDP_LISTED,
    NDP_HOSTED,
    NDP_JOININFO = 105,
    NDP_ERROR,
    NDP_QUICKHOST,    /* nobody to quick-match with: host a quick lobby */
    NDP_JOINREQ       /* to a host: someone is joining, punch towards them */
};

/* NDP_ERROR codes */
enum NdpErrorCode {
    NDPE_BAD_REQUEST = 1,
    NDPE_VERSION,       /* different game build / protocol */
    NDPE_NOT_FOUND,
    NDPE_FULL,
    NDPE_IN_MATCH,
    NDPE_RATE_LIMIT,
    NDPE_BUSY,
    NDPE_FORBIDDEN
};

/* lobby flags (NdpHost.flags / NdpListEntry.flags) */
#define NDPF_PUBLIC  0x01
#define NDPF_QUICK   0x02

/* lobby state */
enum { NDPS_WAITING = 0, NDPS_STARTING, NDPS_PLAYING };

typedef struct NdpHello {
    uint8_t version;
    char build[NET_BUILDID_MAX];
    char name[NET_NAME_MAX];
} NdpHello;

typedef struct NdpPlayer {
    char name[NET_NAME_MAX];
    uint8_t character;
    uint8_t team;
    uint8_t ready;
} NdpPlayer;

/* What a searcher wants from a quick-match game (D412). Every field is
 * NDP_ANY or a value: scenario NG_SCENARIO_*, stage 1..11 (a specific map --
 * the random-stage games match only "any"), weapons 0..13, length 0..7,
 * players 2..4 (the game's size). ndpNormalizePrefs makes a set consistent
 * with GoldenEye's rules (team modes have fixed sizes, small maps cap the
 * size, YOLT fixes the length, the Golden Gun its weapons). */
typedef struct NdpPrefs {
    uint8_t scenario;
    uint8_t stage;
    uint8_t weapons;
    uint8_t length;
    uint8_t players;
} NdpPrefs;

typedef struct NdpHost {
    uint32_t lobbyId;               /* 0 = new */
    char token[NDP_TOKEN_MAX];      /* empty for new */
    char code[NET_CODE_LEN + 1];    /* empty = assign one */
    char name[NET_LOBBY_NAME_MAX];
    uint8_t flags;                  /* NDPF_* */
    uint8_t maxPlayers;
    uint8_t state;                  /* NDPS_* */
    uint8_t numPlayers;
    uint8_t scenario;
    uint8_t stage;
    uint8_t weapons;                /* the game's rules, for quick-match matching (v3) */
    uint8_t length;
    uint16_t matchSec;
    uint8_t ncand;
    NetAddr cand[NET_MAX_CANDS];
    uint8_t nplayers;
    NdpPlayer players[NET_MAX_PLAYERS];
} NdpHost;

typedef struct NdpUnhost {
    uint32_t lobbyId;
    char token[NDP_TOKEN_MAX];
} NdpUnhost;

typedef struct NdpJoin {
    uint32_t lobbyId;               /* 0 = by code */
    char code[NET_CODE_LEN + 1];
    uint32_t nonce;
    uint8_t ncand;
    NetAddr cand[NET_MAX_CANDS];
} NdpJoin;

typedef struct NdpQuick {
    uint32_t nonce;
    uint32_t lobbyId;               /* the asker's own quick lobby, 0 = none:
                                     * only OLDER lobbies are then offered */
    uint32_t excludeId;             /* a lobby we just failed to reach (D411):
                                     * not offered again, counted against it */
    NdpPrefs prefs;                 /* what kind of game (v3) */
    uint8_t ncand;
    NetAddr cand[NET_MAX_CANDS];
} NdpQuick;

typedef struct NdpResultPlayer {
    char name[NET_NAME_MAX];
    uint8_t character;
    uint8_t team;
    int16_t kills;
    int16_t deaths;
} NdpResultPlayer;

typedef struct NdpResult {
    uint32_t lobbyId;
    char token[NDP_TOKEN_MAX];
    uint16_t durationSec;
    uint8_t scenario;
    uint8_t stage;
    uint8_t n;
    NdpResultPlayer players[NET_MAX_PLAYERS];
} NdpResult;

typedef struct NdpPoll {
    uint32_t lobbyId;
    char token[NDP_TOKEN_MAX];
} NdpPoll;

typedef struct NdpWelcome {
    uint16_t online, lobbies, matches;
    uint16_t searching;             /* players waiting in open quick games */
    char motd[NDP_MOTD_MAX];
} NdpWelcome;

typedef struct NdpListEntry {
    uint32_t id;
    char name[NET_LOBBY_NAME_MAX];
    char hostName[NET_NAME_MAX];
    char code[NET_CODE_LEN + 1];    /* public lobbies only */
    uint8_t flags, state, numPlayers, maxPlayers, scenario, stage, weapons, length;
    char country[NDP_COUNTRY_MAX];
} NdpListEntry;

typedef struct NdpListed {
    uint16_t online, lobbies, matches, searching;
    uint8_t n;
    NdpListEntry e[NDP_LIST_MAX];
} NdpListed;

typedef struct NdpHosted {
    uint32_t lobbyId;
    char token[NDP_TOKEN_MAX];
    char code[NET_CODE_LEN + 1];
} NdpHosted;

typedef struct NdpJoinInfo {
    uint32_t nonce;
    uint32_t lobbyId;
    char name[NET_LOBBY_NAME_MAX];
    char hostName[NET_NAME_MAX];
    uint8_t ncand;
    NetAddr cand[NET_MAX_CANDS];
} NdpJoinInfo;

typedef struct NdpError {
    uint8_t reqType;
    uint32_t nonce;
    uint8_t code;
    char text[NET_CHAT_MAX + 1];
} NdpError;

typedef struct NdpJoinReq {
    uint32_t lobbyId;
    char name[NET_NAME_MAX];
    uint8_t ncand;
    NetAddr cand[NET_MAX_CANDS];
} NdpJoinReq;

/* Encoders write [u8 type][payload]; decoders read the payload after the
 * type byte the caller already consumed. Decoders return 0 or -1 (bad /
 * truncated / out-of-range -- the message must then be dropped). */
void ndpEncHello(NetW *w, const NdpHello *m);
void ndpEncList(NetW *w);
void ndpEncHost(NetW *w, const NdpHost *m);
void ndpEncUnhost(NetW *w, const NdpUnhost *m);
void ndpEncJoin(NetW *w, const NdpJoin *m);
void ndpEncQuick(NetW *w, const NdpQuick *m);
void ndpEncResult(NetW *w, const NdpResult *m);
void ndpEncPoll(NetW *w, const NdpPoll *m);

int ndpDecWelcome(NetR *r, NdpWelcome *m);
int ndpDecListed(NetR *r, NdpListed *m);
int ndpDecHosted(NetR *r, NdpHosted *m);
int ndpDecJoinInfo(NetR *r, NdpJoinInfo *m);
int ndpDecError(NetR *r, NdpError *m);
int ndpDecQuickHost(NetR *r, uint32_t *nonce);
int ndpDecJoinReq(NetR *r, NdpJoinReq *m);

/* The service side of the codec, used by the selftest's fake service. */
void ndpEncWelcome(NetW *w, const NdpWelcome *m);
void ndpEncListed(NetW *w, const NdpListed *m);
void ndpEncHosted(NetW *w, const NdpHosted *m);
void ndpEncJoinInfo(NetW *w, const NdpJoinInfo *m);
void ndpEncError(NetW *w, const NdpError *m);
void ndpEncQuickHost(NetW *w, uint32_t nonce);
void ndpEncJoinReq(NetW *w, const NdpJoinReq *m);
int ndpDecHello(NetR *r, NdpHello *m);
int ndpDecHost(NetR *r, NdpHost *m);
int ndpDecJoin(NetR *r, NdpJoin *m);
int ndpDecQuick(NetR *r, NdpQuick *m);
int ndpDecResult(NetR *r, NdpResult *m);

/* Candidate addresses worth publishing: not 0.0.0.0, not port 0. */
int ndpCandValid(const NetAddr *a);
/* An address it is sane to send game packets to (D412): not this-network,
 * link-local, multicast, reserved or broadcast; port 1024 or above (the game
 * uses 27007 or an ephemeral port). Loopback is allowed (local play). The
 * service already filters what it hands out; this is the client's own
 * check, in case it is talking to something else. */
int ndpCandSendable(const NetAddr *a);

/* Quick-match preferences (D412). */
void ndpPrefsAny(NdpPrefs *p);
/* Out-of-range fields -> any; then GoldenEye's rules: a team mode fixes the
 * game size (2v2 / 3v1: 4, 2v1: 3), a small map caps it (or is dropped when
 * a team mode needs more), YOLT fixes the length (-> any), the Golden Gun
 * its weapons (-> any), Flag Tag allows lengths up to 20 minutes, "last one
 * standing" picks YOLT when no mode was chosen (else it is dropped).
 * Idempotent; the service applies the same rules. */
void ndpNormalizePrefs(NdpPrefs *p);
/* 1 if a game with these rules satisfies the preferences. */
int ndpPrefsMatch(const NdpPrefs *p, int scenario, int stage, int weapons, int length, int maxPlayers);
/* The rules a searcher hosts when nobody fits: "any" fields take the N64
 * defaults (Normal, a random stage, the default weapons, 10 minutes), then
 * ngNormalizeSettings; *maxPlayers = the game's size. NS_AUTOSTART set. */
void ndpPrefsToRules(const NdpPrefs *p, NetSettings *s, int *maxPlayers);
/* Short label for the UI, e.g. "Any game" or "Golden Gun, Facility". */
void ndpPrefsLabel(const NdpPrefs *p, char *out, int n);

#ifdef __cplusplus
}
#endif

#endif /* GE_NET_DIRPROTO_H */
