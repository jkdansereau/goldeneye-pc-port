/*
 * net_runtime.h -- the game's background network thread (D413, D414).
 *
 * Owns the UDP sockets, one NetClient, and (when this PC hosts) one direct
 * NetHost, and pumps them ~1000 times a second so the protocol keeps running
 * while the game thread is busy (stage loads take seconds). Nothing here is
 * started until the player opens online play: no sockets, no traffic.
 *
 * Online play (D414) goes through the directory service (net_dir.h) for
 * finding games only; the match itself is peer to peer with a player's game
 * as the host. The runtime learns each socket's public address from a STUN
 * server (net_stun.h), publishes it, and hole-punches towards joiners.
 */
#ifndef GE_NET_RUNTIME_H
#define GE_NET_RUNTIME_H

#include <stdint.h>

#include "net_client.h"
#include "net_dirproto.h"
#include "net_host.h"
#include "net_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the thread + client socket (idempotent). 0 on success. */
int netRuntimeStart(const char *playerName, const char *buildId, NetLogFn log, void *logCtx);
/* Disconnect, stop hosting, join the thread, close sockets. */
void netRuntimeStop(void);
/* atexit variant: stop the thread and tell peers goodbye (DISCONNECT from
 * the client and the host), but free nothing -- the game thread may still
 * be parked inside a client call while the process exits. */
void netRuntimeShutdownForExit(void);
int netRuntimeRunning(void);

/* The single client (valid while running). */
NetClient *netRuntimeClient(void);

/* Host a direct lobby on `port` and connect our own client to it. */
int netRuntimeHost(uint16_t port, const char *lobbyName, int maxPlayers, char *err, int errLen);
int netRuntimeIsHosting(void);
uint16_t netRuntimeHostPort(void);

/* Resolve `addr` (DNS allowed) on the net thread, then connect. */
void netRuntimeConnect(const char *addr, uint16_t defaultPort, int serverMode);

/* Leave everything: disconnect the client and stop hosting. */
void netRuntimeLeave(void);

/* LAN discovery: broadcast a query, collect replies for a few seconds. */
typedef struct NetLanGame {
    char addr[32];
    char name[NET_LOBBY_NAME_MAX];
    char leader[NET_NAME_MAX];
    uint8_t numPlayers;
    uint8_t maxPlayers;
    uint8_t state;
    uint8_t compatible;   /* same build id */
} NetLanGame;
void netRuntimeLanScan(uint16_t port);
int netRuntimeLanResults(NetLanGame *out, int max);

/* Our player name (client + directory). */
void netRuntimeSetName(const char *name);

/* ---- online service (D414) ---- */

/* Point the directory client at a service ("https://NAME.workers.dev"; an
 * empty URL = no online service) and start it. Cheap: it connects only while
 * something needs it. */
void netRuntimeSetService(const char *url, int forcePoll);
/* STUN server used to learn our public address: "host[:port]" (empty =
 * stun.cloudflare.com:3478), or "off" (offer only the LAN address). */
void netRuntimeSetStun(const char *server);

/* Host a lobby registered with the service: public (listed, joinable by
 * code) or private (code only); quick = a quick-match lobby that starts by
 * itself when everyone is ready. Tries `port` first, then any free port. */
int netRuntimeHostOnline(uint16_t port, const char *lobbyName, int maxPlayers, int isPublic, int quick, char *err,
                         int errLen);
int netRuntimeIsHostingOnline(void);

/* Join a listed lobby (by id) or one by code (lobbyId 0); or quick match
 * (joins an open quick lobby, or hosts one if there is none). Progress via
 * netRuntimeOnlineBusy; the outcome shows up in the client's state (it
 * connects, or fails with a message). */
void netRuntimeJoinOnline(uint32_t lobbyId, const char *code);
/* prefs: what kind of game (D416: mode / map / weapons / length / size, each
 * NDP_ANY or a value); NULL = any. When nothing fits, this PC hosts a quick
 * game with those preferences as its rules. */
void netRuntimeQuickOnline(const NdpPrefs *prefs);
/* 1 while an online join / quick match is still talking to the service;
 * `text` = what it is doing (or the last thing it did). */
int netRuntimeOnlineBusy(char *text, int n);

/* Host: a finished match of our online lobby, for the status page. */
void netRuntimeReportMatch(const NdpResult *r);

#ifdef __cplusplus
}
#endif

#endif /* GE_NET_RUNTIME_H */
