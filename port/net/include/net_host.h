/*
 * net_host.h -- lobby host + lockstep input relay (D413).
 *
 * One implementation, two deployments:
 *   - direct host (serverMode = 0): runs inside a player's game; exactly one
 *     lobby, joiners land in it straight from the handshake (LAN / IP play);
 *   - matchmaking server (serverMode = 1): runs in tools_pc/netplay's
 *     ge007-netserver; any number of lobbies, list / create / join-by-code /
 *     quick match.
 *
 * The host never simulates the game. During a match it only gathers each
 * player's controller records, assembles per-frame bundles (all players'
 * records for frame f) and relays them; every peer simulates from the same
 * bundles (deterministic lockstep, docs/dev/NETPLAY-PLAN.md).
 */
#ifndef GE_NET_HOST_H
#define GE_NET_HOST_H

#include <stdint.h>

#include "net_proto.h"
#include "net_sock.h"

#ifdef __cplusplus
extern "C" {
#endif

enum NetLogLevel { NETLOG_DEBUG = 0, NETLOG_INFO, NETLOG_WARN, NETLOG_ERROR };

typedef void (*NetLogFn)(void *ctx, int level, const char *msg);

typedef struct NetHostConfig {
    int serverMode;
    int maxSessions;        /* 0 = default (direct 16, server 512) */
    int maxLobbies;         /* server only; 0 = default 128 */
    int maxPerIp;           /* server only; 0 = default 8 */
    char buildId[NET_BUILDID_MAX];          /* direct: joiners must match */
    char lobbyName[NET_LOBBY_NAME_MAX];     /* direct mode lobby name */
    int maxPlayers;                          /* direct mode, 2..4 */
    NetLogFn log;
    void *logCtx;
} NetHostConfig;

typedef struct NetHost NetHost;

NetHost *netHostCreate(const NetHostConfig *cfg, NetTransport t);
void netHostDestroy(NetHost *h);

/* Feed one received datagram. */
void netHostOnPacket(NetHost *h, const NetAddr *from, const uint8_t *data, int len, uint64_t nowUs);
/* Timers: timeouts, assembly, retransmits, lobby broadcasts. Call >= 200 Hz. */
void netHostTick(NetHost *h, uint64_t nowUs);
/* Drain the transport (recv loop) then tick. */
void netHostPump(NetHost *h, uint64_t nowUs);

typedef struct NetHostStats {
    int sessions;
    int lobbies;
    int matches;
    uint32_t packetsIn;
    uint32_t packetsOut;
} NetHostStats;
void netHostGetStats(NetHost *h, NetHostStats *out);

/* Direct mode (a player's own game hosting) -- the online service (D414):
 * the lobby code the service assigned, public / quick-match flags, and
 * quick-match auto-start; and a snapshot of the lobby + match for the
 * service's lobby list and status page. Net thread (the host's owner) only. */
typedef struct NetHostLobbyInfo {
    int valid;
    NetLobbyState lobby;
    int inMatch;
    int matchGo;                /* start barrier passed */
    uint32_t matchFrame;        /* frames assembled so far */
    uint32_t matchElapsedSec;   /* since MATCH_START */
    uint8_t matchStage;         /* the stage actually played (random resolved) */
} NetHostLobbyInfo;
/* code: NULL = leave as is; "" = none (yet) -- an online lobby has no code
 * until the service assigns one, and the locally made one means nothing. */
void netHostSetDirectInfo(NetHost *h, const char *code, uint8_t lobbyFlags, int autostart);
int netHostGetDirectLobby(NetHost *h, NetHostLobbyInfo *out);
/* A quick-match game's rules (D416: from the searcher's preferences --
 * ndpPrefsToRules): settings (normalised, autostart) and size. Ignored once
 * a match is under way; never lowers the size below the players present. */
void netHostSetQuickRules(NetHost *h, const NetSettings *rules, int maxPlayers);

#ifdef __cplusplus
}
#endif

#endif /* GE_NET_HOST_H */
