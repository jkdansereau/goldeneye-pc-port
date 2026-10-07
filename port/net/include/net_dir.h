/*
 * net_dir.h -- client for the online directory service (D414).
 *
 * Owns one connection to the service (tools_pc/netplay/cloudflare) on its own
 * thread: a WebSocket when the platform has one (net_http.h), otherwise
 * HTTPS polling. It connects only while something needs it -- the Online
 * menu is open (netDirKeepAlive), this PC hosts an online lobby, or a
 * request is pending -- and drops the connection when idle. Reconnects with
 * back-off; a hosted lobby is re-registered after every reconnect.
 *
 * Every function is thread-safe and non-blocking (requests are queued).
 */
#ifndef GE_NET_DIR_H
#define GE_NET_DIR_H

#include <stdint.h>

#include "net_dirproto.h"
#include "net_host.h"
#include "net_sock.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NET_DIR_URL_MAX 200

enum NetDirState {
    NDS_OFF = 0,        /* not needed right now */
    NDS_CONNECTING,
    NDS_ONLINE,
    NDS_RETRY,          /* lost / failed; reconnecting with back-off */
    NDS_UNAVAILABLE     /* no service configured, or it refused this version */
};

typedef struct NetDirConfig {
    char url[NET_DIR_URL_MAX];       /* https://SERVICE (no trailing path) */
    char build[NET_BUILDID_MAX];
    char name[NET_NAME_MAX];
    int forcePoll;                   /* HTTPS polling even if WebSockets work */
    NetLogFn log;
    void *logCtx;
} NetDirConfig;

typedef struct NetDirStatus {
    int state;
    int polling;                     /* HTTPS polling instead of a WebSocket */
    char lastError[96];
    uint32_t errorSeq;
    NdpWelcome info;                 /* players online etc. */
    uint32_t infoSeq;
    NdpListed list;
    uint32_t listSeq;
    int hosting;                     /* a lobby registration is wanted */
    NdpHosted hosted;                /* id / code of our registered lobby */
    uint32_t hostedSeq;
    NdpJoinInfo joinInfo;            /* reply to netDirJoin / netDirQuick */
    uint32_t joinInfoSeq;
    uint32_t quickHostNonce;         /* reply to netDirQuick: host one */
    uint32_t quickHostSeq;
    NdpError fail;                   /* last NDP_ERROR */
    uint32_t failSeq;
} NetDirStatus;

/* Idempotent; a new URL restarts. The URL must be https:// -- plain http://
 * only to this machine (local tests); anything else leaves the service
 * UNAVAILABLE with the reason in lastError (D416). */
int netDirStart(const NetDirConfig *cfg);
void netDirStop(void);
/* atexit: stop without waiting or freeing (see net_runtime.h). */
void netDirShutdownForExit(void);
int netDirRunning(void);
void netDirSetName(const char *name);

/* Stay connected for at least `seconds` from now (the Online menu calls this
 * while it is open). */
void netDirKeepAlive(int seconds);

void netDirRequestList(void);
/* Register / refresh this PC's lobby (the full record; resent after every
 * reconnect and every 30 s, and immediately when it changes). lobbyId /
 * token / code are filled in from the service's NDP_HOSTED reply. */
void netDirHost(const NdpHost *rec);
void netDirUnhost(void);
void netDirJoin(uint32_t lobbyId, const char *code, uint32_t nonce, const NetAddr *cands, int n);
/* Quick match. excludeId: a game we were just sent to and could not reach
 * (0 = none) -- not offered again, and counted against it (D415). prefs:
 * what kind of game (D416; NULL = any). */
void netDirQuick(uint32_t nonce, uint32_t excludeId, const NdpPrefs *prefs, const NetAddr *cands, int n);
/* A finished match of our hosted lobby (id / token filled in here). */
void netDirReportResult(const NdpResult *r);

/* Host side: someone the service sent our way (punch towards them). */
int netDirTakeJoinReq(NdpJoinReq *out);

void netDirGetStatus(NetDirStatus *out);

#ifdef __cplusplus
}
#endif

#endif /* GE_NET_DIR_H */
