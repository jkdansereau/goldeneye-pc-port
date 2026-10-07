/*
 * net_client.h -- netplay client: connection, lobby replica, matchmaking
 * requests and the lockstep consumer (D409).
 *
 * Thread model: one NetClient is driven by the net thread (netClientPump /
 * OnPacket / Tick) and used concurrently by the UI and the game thread
 * through the request / status / match functions below. Every public
 * function takes the client's internal lock; none of them blocks.
 */
#ifndef GE_NET_CLIENT_H
#define GE_NET_CLIENT_H

#include <stdint.h>

#include "net_host.h"
#include "net_proto.h"
#include "net_sock.h"

#ifdef __cplusplus
extern "C" {
#endif

enum NetClientState {
    NCS_IDLE = 0,
    NCS_CONNECTING,
    NCS_CONNECTED,   /* session up, no lobby (matchmaking server) */
    NCS_LOBBY,
    NCS_FAILED
};

enum NetMatchPhase {
    NMP_NONE = 0,
    NMP_STARTING,    /* MATCH_START received; the game must load the stage */
    NMP_LOADED,      /* stage loaded, waiting for the start barrier */
    NMP_RUNNING,
    NMP_OVER
};

typedef struct NetClientConfig {
    char name[NET_NAME_MAX];
    char buildId[NET_BUILDID_MAX];
    NetLogFn log;
    void *logCtx;
} NetClientConfig;

#define NET_NOTICE_RING 6

typedef struct NetClientStatus {
    int state;
    int serverMode;
    int slot;            /* -1 when not in a lobby */
    int isLeader;
    int lobbyValid;
    NetLobbyState lobby;
    int listCount;
    uint32_t listSeq;    /* bumps on every LIST received */
    NetListEntry list[NET_LIST_MAX];
    char lastError[96];
    uint32_t errorSeq;
    char notice[NET_NOTICE_RING][96];
    uint32_t noticeSeq;  /* total notices received; newest = notice[(seq-1) % RING] */
    float rttMs;
    char hostAddr[32];
    /* match */
    int matchPhase;
    uint32_t matchId;
    int matchSlot;
    int matchPlayers;
    int matchDelay;
    int desync;
    uint32_t desyncFrame;
    uint8_t discMask;
    uint32_t bundlesAhead;   /* received but not yet consumed */
    /* waiting screen (D410) */
    int matchGo;             /* start barrier passed */
    uint8_t loadedMask;      /* before GO: players that finished loading */
    uint8_t waitMask;        /* ...out of these (connected players) */
    uint32_t consumed;       /* frames the game has run */
    uint32_t slotLatest[NET_MAX_PLAYERS];   /* host's newest input frame per player */
} NetClientStatus;

typedef struct NetClient NetClient;

NetClient *netClientCreate(const NetClientConfig *cfg, NetTransport t);
void netClientDestroy(NetClient *c);
void netClientSetName(NetClient *c, const char *name);

/* Begin connecting (state -> CONNECTING). serverMode selects JOIN mode. */
int netClientConnect(NetClient *c, const NetAddr *host, int serverMode);
/* Same, trying up to NET_MAX_CANDS addresses of one host at once (online
 * play: its public STUN mapping and LAN address); the first to answer wins.
 * maxTries = JOIN rounds before giving up (0 = default), 500 ms apart. */
int netClientConnectMulti(NetClient *c, const NetAddr *cands, int n, int serverMode, int maxTries);
/* Graceful disconnect (state -> IDLE). Safe in any state. */
void netClientDisconnect(NetClient *c);
/* Drop any connection and enter FAILED with `message` as the error. */
void netClientFail(NetClient *c, const char *message);

void netClientOnPacket(NetClient *c, const NetAddr *from, const uint8_t *data, int len, uint64_t nowUs);
void netClientTick(NetClient *c, uint64_t nowUs);
void netClientPump(NetClient *c, uint64_t nowUs);

/* Matchmaking-server requests (state CONNECTED). */
void netClientRequestList(NetClient *c);
void netClientCreateLobby(NetClient *c, const char *name, int isPublic, int maxPlayers, int autostart);
void netClientJoinLobby(NetClient *c, uint32_t lobbyId, const char *code);
void netClientQuickMatch(NetClient *c);
void netClientLeaveLobby(NetClient *c);

/* Lobby actions (state LOBBY). */
void netClientSetPlayer(NetClient *c, int character, int handicap, int control, int team, int ready);
void netClientSetSettings(NetClient *c, const NetSettings *s);
void netClientStartMatch(NetClient *c);
void netClientAbortMatch(NetClient *c);
void netClientChat(NetClient *c, const char *text);

void netClientGetStatus(NetClient *c, NetClientStatus *out);

/* ---- match API (game thread) ---- */

/* Returns 1 exactly once per match when a MATCH_START is waiting. */
int netClientMatchTake(NetClient *c, NetMatchStart *out, int *slot);
/* The stage is loaded; frame 0 is next. */
void netClientMatchLoaded(NetClient *c);
/* 1 once the host released the start barrier. */
int netClientMatchIsGo(NetClient *c);
/* Local record for `frame` (frames are submitted in increasing order). */
void netClientMatchSubmit(NetClient *c, uint32_t frame, const NetInputRec *rec);
/* 1: bundle copied; 0: not yet available; -1: the match is over for us
 * (aborted at/after this frame, host lost, or kicked) -- leave the stage. */
int netClientMatchGetBundle(NetClient *c, uint32_t frame, NetBundle *out);
/* The game is done with frames < frame (ring space can be reused). */
void netClientMatchConsumed(NetClient *c, uint32_t frame);
/* Timesync: microseconds to sleep before running `frame` (0..4000). */
uint32_t netClientMatchAdvise(NetClient *c, uint32_t frame);
void netClientMatchHash(NetClient *c, uint32_t frame, uint32_t hash);
/* The game left the stage on its own (natural end, or after an abort). */
void netClientMatchFinished(NetClient *c);
/* The local player quit the match. */
void netClientMatchLeave(NetClient *c);

#ifdef __cplusplus
}
#endif

#endif /* GE_NET_CLIENT_H */
