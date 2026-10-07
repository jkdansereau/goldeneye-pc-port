/*
 * net_runtime.c -- the game's background network thread (D413, D414).
 *
 * Only the net thread touches the host object and the sockets' receive side.
 * Requests from other threads (host / connect / leave / LAN scan / online
 * join) are posted under R.mx and executed by the net thread on its next
 * iteration. The client object is internally locked and may be used from
 * any thread; so is the directory client (net_dir.c, its own thread).
 *
 * Online play through the directory service (D414) is peer to peer: a host is
 * a player's own game. The net thread learns each socket's public address
 * from a STUN server, publishes it (with the LAN address) through the
 * directory, and when the directory reports a joiner, sends "punch" packets
 * at them so the host's NAT lets their JOIN in.
 */
#include "net_runtime.h"
#include "net_dir.h"
#include "net_dirproto.h"
#include "net_plat.h"
#include "net_sock.h"
#include "net_stun.h"
#include "net_wire.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LAN_MAX 16

/* online (D414) */
#define PUNCH_MAX                8
#define PUNCH_INTERVAL_US   100000ull
#define PUNCH_DURATION_US  6000000ull
#define STUN_RETRY_US       500000ull
#define STUN_TRIES               6
#define STUN_REFRESH_US   25000000ull   /* keeps the host's NAT mapping alive */
#define STUN_FRESH_US     60000000ull   /* a joiner reuses a mapping this young */
#define STUN_DNS_TTL_US  600000000ull
#define STUN_WAIT_US       2500000ull   /* first registration waits this long for STUN */
#define ASK_TIMEOUT_US    20000000ull
#define HOST_PUSH_US        500000ull
#define JOIN_TRIES_ONLINE       30      /* 15 s of JOIN rounds: room for the punch */
#define JOIN_TRIES_QUICK        16      /* 8 s: a punch that works works in 1-3 s;
                                         * matchmaking then tries another game */
#define QUICK_MAX_FAILS          3      /* unreachable games before hosting one */
#define QUICK_REASK_US     5000000ull   /* a lone quick host looks for an older lobby */

enum { STUN_IDLE = 0, STUN_PENDING, STUN_DONE, STUN_FAILED };
enum { OJ_NONE = 0, OJ_STUN, OJ_ASK, OJ_CONNECT };
enum { OJK_JOIN = 1, OJK_QUICK = 2 };

typedef struct RtStun {
    int state;
    NetStunTx tx;
    uint8_t req[NET_STUN_REQUEST_LEN];
    uint64_t startUs, sentUs, doneUs;
    int tries;
    NetAddr mapped;
} RtStun;

static struct {
    int running;
    volatile int quit;
    NetThread *thread;
    NetMutex *mx;
    NetUdp *csock;
    NetUdp *hsock;
    NetClient *client;
    NetHost *host;
    uint16_t hostPort;
    char buildId[NET_BUILDID_MAX];
    char name[NET_NAME_MAX];
    NetLogFn log;
    void *logCtx;

    /* requests (under mx) */
    NetHost *pendingHost;
    NetUdp *pendingHsock;
    int pendingOnline, pendingPublic, pendingQuick;
    int stopHostReq;
    int leaveReq;
    int connectReq;
    char connectAddr[128];
    uint16_t connectPort;
    int connectServer;
    int lanReq;
    uint16_t lanPort;
    int ojReq;                         /* OJK_* */
    uint32_t ojReqLobby;
    char ojReqCode[NET_CODE_LEN + 1];
    char ojText[96];                   /* what the online join is doing (UI) */
    int ojBusy;

    /* LAN results (under mx) */
    NetLanGame lan[LAN_MAX];
    int lanCount;
    uint32_t lanNonce;

    /* online service (net thread unless noted) */
    char serviceUrl[NET_DIR_URL_MAX];  /* (mx) */
    int forcePoll;                     /* (mx) */
    int onlineHost;                    /* (mx for reads by the API) */
    int onlinePublic, onlineQuick;
    int registeredOnce;
    uint32_t hostedSeqSeen;
    uint64_t lastHostPushUs;
    char stunCfg[128];                 /* (mx) "host[:port]", or "off" */
    int stunCfgChanged;                /* (mx) */
    NetAddr stunServer;
    int stunServerOk;
    uint64_t stunResolvedUs;
    RtStun stunC, stunH;
    struct {
        NetAddr to;
        uint64_t next, until;
    } punch[PUNCH_MAX];
    int ojPhase, ojKind;
    int ojMerge;                       /* a lone quick host re-asking (silent) */
    int ojHostPending;                 /* quick match hosts: busy until we connect */
    int ojFails;                       /* quick: games we could not reach this search */
    NdpPrefs qprefs;                   /* quick: what kind of game (D416) */
    NetSettings quickRules;            /* quick: the rules we host with when nobody fits */
    int quickMax;
    uint32_t ojExclude;                /* quick: ...the last one, told to the service */
    uint32_t ojTarget;                 /* quick: the game we are connecting to */
    uint64_t qmLastUs;
    uint32_t ojLobby, ojNonce;
    char ojCode[NET_CODE_LEN + 1];
    uint64_t ojStartUs;
    uint32_t ojSeqJoin, ojSeqFail, ojSeqQuick;
} R;

static void rlog(int level, const char *msg)
{
    if (R.log) R.log(R.logCtx, level, msg);
}

static void rlogf(int level, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    if (!R.log) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    R.log(R.logCtx, level, buf);
}

static void setOjText(const char *text, int busy)
{
    netMutexLock(R.mx);
    netStrCopy(R.ojText, sizeof(R.ojText), text);
    R.ojBusy = busy;
    netMutexUnlock(R.mx);
}

/* ------------------------------------------------------------------------ */
/* STUN (net thread)                                                         */
/* ------------------------------------------------------------------------ */

/* Microseconds from `then` to `now`, never "negative": a timestamp taken by
 * a packet handler inside this tick's pump is LATER than the tick's `now`, and
 * the plain unsigned `now - then` wraps to "ages ago" (D415: the host's STUN
 * answer then looked stale at once, was re-asked every tick, and the first
 * registration -- which waits for it -- never happened). */
static uint64_t since(uint64_t now, uint64_t then)
{
    return now > then ? now - then : 0;
}

/* The DNS lookup blocks: only ever done outside a running match (the
 * address is cached for ten minutes and reused past that while playing). */
static int stunServerReady(uint64_t now, int mayResolve)
{
    char cfg[128];
    netMutexLock(R.mx);
    netStrCopy(cfg, sizeof(cfg), R.stunCfg);
    if (R.stunCfgChanged) {
        R.stunCfgChanged = 0;
        R.stunServerOk = 0;
    }
    netMutexUnlock(R.mx);
    if (!strcmp(cfg, "off")) return 0;
    if (R.stunServerOk && (now - R.stunResolvedUs < STUN_DNS_TTL_US || !mayResolve)) return 1;
    if (!mayResolve) return R.stunServerOk;
    if (netAddrResolve(cfg, NET_STUN_DEFAULT_PORT, &R.stunServer) == 0) {
        R.stunServerOk = 1;
        R.stunResolvedUs = now;
        return 1;
    }
    return R.stunServerOk;   /* keep a stale address rather than none */
}

static void stunStart(RtStun *s, uint64_t now)
{
    s->state = STUN_PENDING;
    s->tries = 0;
    s->startUs = now;
    s->sentUs = 0;
    netStunBuildRequest(s->req, sizeof(s->req), &s->tx);
}

static void stunTick(RtStun *s, NetUdp *sock, uint64_t now, int mayResolve, const char *what)
{
    if (s->state != STUN_PENDING || !sock) return;
    if (s->sentUs && now - s->sentUs < STUN_RETRY_US) return;
    if (s->tries >= STUN_TRIES || !stunServerReady(now, mayResolve)) {
        char cfg[128];
        s->state = STUN_FAILED;
        s->doneUs = now;
        netMutexLock(R.mx);
        netStrCopy(cfg, sizeof(cfg), R.stunCfg);
        netMutexUnlock(R.mx);
        if (strcmp(cfg, "off") != 0) {
            rlogf(NETLOG_WARN, "STUN (%s): no answer from %s -- only the LAN address can be offered", what, cfg);
        }
        return;
    }
    netUdpSend(sock, &R.stunServer, s->req, NET_STUN_REQUEST_LEN);
    s->sentUs = now;
    s->tries++;
}

static void stunOnPacket(RtStun *s, const uint8_t *p, int n, const char *what)
{
    NetAddr a;
    if (s->state != STUN_PENDING) return;
    if (netStunParseResponse(p, n, &s->tx, &a) == 0) {
        char t[32];
        int changed = (a.ip != s->mapped.ip || a.port != s->mapped.port);
        s->mapped = a;
        s->state = STUN_DONE;
        s->doneUs = netTimeUs();
        if (changed) {
            netAddrFormat(&a, t, sizeof(t));
            rlogf(NETLOG_INFO, "STUN (%s): public address %s", what, t);
        }
    }
}

static int localCand(uint16_t port, NetAddr *out)
{
    char ip[32];
    if (netLocalIPv4(ip, sizeof(ip)) != 0) return 0;
    return netAddrResolve(ip, port, out) == 0 && ndpCandValid(out);
}

/* Public (STUN) first, then the LAN address if different. */
static int buildCands(const RtStun *s, uint16_t port, NetAddr *out)
{
    int n = 0;
    NetAddr lan;
    if (s->state == STUN_DONE && ndpCandValid(&s->mapped)) out[n++] = s->mapped;
    if (localCand(port, &lan) && !(n && lan.ip == out[0].ip && lan.port == out[0].port)) out[n++] = lan;
    return n;
}

/* ------------------------------------------------------------------------ */
/* Host socket transport: STUN replies to the host socket are ours           */
/* ------------------------------------------------------------------------ */

static int rtHostSend(void *ctx, const NetAddr *to, const void *data, int len)
{
    return netUdpSend((NetUdp *)ctx, to, data, len);
}

static int rtHostRecv(void *ctx, NetAddr *from, void *buf, int cap)
{
    for (;;) {
        int n = netUdpRecv((NetUdp *)ctx, from, buf, cap);
        if (n > 0 && netStunIsMessage((const uint8_t *)buf, n)) {
            stunOnPacket(&R.stunH, (const uint8_t *)buf, n, "host");
            continue;
        }
        return n;
    }
}

static NetTransport rtHostTransport(NetUdp *sock)
{
    NetTransport t;
    t.ctx = sock;
    t.send = rtHostSend;
    t.recv = rtHostRecv;
    return t;
}

/* ------------------------------------------------------------------------ */
/* Hole punching (net thread)                                                */
/* ------------------------------------------------------------------------ */

static void punchAdd(const NetAddr *to, uint64_t now)
{
    int i, slot = -1;
    if (!ndpCandSendable(to)) return;
    for (i = 0; i < PUNCH_MAX; i++) {
        if (netAddrEq(&R.punch[i].to, to)) {
            slot = i;
            break;
        }
        if (slot < 0 && now >= R.punch[i].until) slot = i;
    }
    if (slot < 0) slot = 0;
    R.punch[slot].to = *to;
    R.punch[slot].next = now;
    R.punch[slot].until = now + PUNCH_DURATION_US;
}

static void punchTick(uint64_t now)
{
    uint8_t b[NET_HEADER_SIZE];
    NetW w;
    int i;
    if (!R.hsock) return;
    nwInit(&w, b, sizeof(b));   /* header only: opens our NAT towards them */
    netWriteHeader(&w, NP_PUNCH, 0);
    netFinishHeader(&w);
    for (i = 0; i < PUNCH_MAX; i++) {
        if (now >= R.punch[i].until || now < R.punch[i].next) continue;
        netUdpSend(R.hsock, &R.punch[i].to, b, w.len);
        R.punch[i].next = now + PUNCH_INTERVAL_US;
    }
}

/* ------------------------------------------------------------------------ */
/* Hosting                                                                   */
/* ------------------------------------------------------------------------ */

static void stopHostNow(void)
{
    NetHost *h;
    NetUdp *s;
    int wasOnline;
    netMutexLock(R.mx);
    h = R.host;
    s = R.hsock;
    wasOnline = R.onlineHost;
    R.host = NULL;
    R.hsock = NULL;
    R.hostPort = 0;
    R.onlineHost = 0;
    netMutexUnlock(R.mx);
    if (h) netHostDestroy(h);   /* DISCONNECT to every joined player, via s */
    if (s) netUdpClose(s);
    if (wasOnline) netDirUnhost();
    memset(&R.stunH, 0, sizeof(R.stunH));
    memset(R.punch, 0, sizeof(R.punch));
}

/* Open the socket + host and hand them to the net thread (any thread). */
static int hostBegin(uint16_t port, const char *lobbyName, int maxPlayers, int online, int isPublic, int quick,
                     char *err, int errLen)
{
    NetHostConfig hc;
    NetUdp *sock;
    NetHost *host;
    if (!R.running) {
        netStrCopy(err, errLen, "netplay not started");
        return -1;
    }
    sock = netUdpOpen(port, 0, err, errLen);
    if (!sock && online && port != 0) {
        /* Online the port need not be well known (the service publishes
         * it): another program -- or a second copy -- may hold it. */
        sock = netUdpOpen(0, 0, err, errLen);
    }
    if (!sock) return -1;
    memset(&hc, 0, sizeof(hc));
    hc.serverMode = 0;
    hc.maxPlayers = maxPlayers;
    netStrCopy(hc.buildId, NET_BUILDID_MAX, R.buildId);
    netStrCopy(hc.lobbyName, NET_LOBBY_NAME_MAX, lobbyName);
    hc.log = R.log;
    hc.logCtx = R.logCtx;
    host = netHostCreate(&hc, rtHostTransport(sock));
    if (!host) {
        netUdpClose(sock);
        netStrCopy(err, errLen, "could not create host");
        return -1;
    }
    netMutexLock(R.mx);
    if (R.pendingHost) netHostDestroy(R.pendingHost);
    if (R.pendingHsock) netUdpClose(R.pendingHsock);
    R.pendingHost = host;
    R.pendingHsock = sock;
    R.pendingOnline = online;
    R.pendingPublic = isPublic;
    R.pendingQuick = quick;
    /* Our own player joins over loopback, like everyone else. */
    netStrCopy(R.connectAddr, sizeof(R.connectAddr), "127.0.0.1");
    R.connectPort = netUdpLocalPort(sock);
    R.connectServer = 0;
    R.connectReq = 1;
    R.ojReq = 0;
    netMutexUnlock(R.mx);
    return 0;
}

/* Push the lobby to the directory, refresh STUN, punch towards joiners. */
static void onlineHostTick(uint64_t now)
{
    NetHostLobbyInfo li;
    NdpJoinReq jr;
    NetDirStatus ds;
    NdpHost rec;
    int i;
    if (!R.onlineHost || !R.host) return;

    netHostGetDirectLobby(R.host, &li);
    if (li.valid && !(li.lobby.flags & NL_ONLINE)) {
        netHostSetDirectInfo(R.host, NULL,
                             (uint8_t)(NL_ONLINE | (R.onlinePublic ? NL_PUBLIC : 0) | (R.onlineQuick ? NL_QUICK : 0)),
                             R.onlineQuick);
        if (R.onlineQuick) netHostSetQuickRules(R.host, &R.quickRules, R.quickMax);
    }
    if (R.stunH.state == STUN_IDLE ||
        (R.stunH.state != STUN_PENDING && !li.inMatch && since(now, R.stunH.doneUs) > STUN_REFRESH_US)) {
        stunStart(&R.stunH, now);
    }
    stunTick(&R.stunH, R.hsock, now, !li.inMatch, "host");

    while (netDirTakeJoinReq(&jr)) {
        for (i = 0; i < jr.ncand; i++) punchAdd(&jr.cand[i], now);
    }

    /* The first registration waits briefly for the public address. */
    if (!R.registeredOnce && R.stunH.state == STUN_PENDING && now - R.stunH.startUs < STUN_WAIT_US) return;
    if (now - R.lastHostPushUs < HOST_PUSH_US || !li.valid) return;
    R.lastHostPushUs = now;

    memset(&rec, 0, sizeof(rec));
    netStrCopy(rec.name, sizeof(rec.name), li.lobby.name);
    rec.flags = (uint8_t)((R.onlinePublic ? NDPF_PUBLIC : 0) | (R.onlineQuick ? NDPF_QUICK : 0));
    rec.maxPlayers = li.lobby.max_players;
    rec.numPlayers = li.lobby.num_players;
    rec.state = li.inMatch ? (li.matchGo ? NDPS_PLAYING : NDPS_STARTING) : NDPS_WAITING;
    rec.scenario = li.lobby.settings.scenario;
    rec.stage = li.inMatch ? li.matchStage : li.lobby.settings.stage;
    rec.weapons = li.lobby.settings.weapons;
    rec.length = li.lobby.settings.length;
    rec.matchSec = (uint16_t)(li.inMatch && li.matchGo ? (li.matchFrame / 60 > 65535 ? 65535 : li.matchFrame / 60) : 0);
    rec.ncand = (uint8_t)buildCands(&R.stunH, R.hostPort, rec.cand);
    for (i = 0; i < NET_MAX_PLAYERS; i++) {   /* slots are sparse */
        const NetPlayerInfo *p = &li.lobby.players[i];
        NdpPlayer *o;
        if (!p->used) continue;
        o = &rec.players[rec.nplayers++];
        netStrCopy(o->name, sizeof(o->name), p->name);
        o->character = p->character;
        o->team = p->team;
        o->ready = p->ready;
    }
    if (rec.ncand == 0) return;   /* no usable address at all (offline?) */
    netDirHost(&rec);
    R.registeredOnce = 1;

    netDirGetStatus(&ds);
    if (ds.hostedSeq != R.hostedSeqSeen && ds.hosted.code[0]) {
        R.hostedSeqSeen = ds.hostedSeq;
        netHostSetDirectInfo(R.host, ds.hosted.code,
                             (uint8_t)(NL_ONLINE | (R.onlinePublic ? NL_PUBLIC : 0) | (R.onlineQuick ? NL_QUICK : 0)),
                             R.onlineQuick);
    }
}

/* ------------------------------------------------------------------------ */
/* Online join / quick match (net thread)                                    */
/* ------------------------------------------------------------------------ */

/* An attempt ends. A merge attempt (below) ends silently: the player is
 * sitting in their own quick lobby and nothing they asked for failed. */
static void ojFail(const char *text)
{
    R.ojPhase = OJ_NONE;
    if (R.ojMerge) {
        rlogf(NETLOG_DEBUG, "quick match: re-ask ended (%s)", text);
        return;
    }
    setOjText(text, 0);
    netClientFail(R.client, text);
}

static void ojBegin(int kind, uint32_t lobby, const char *code, uint64_t now, int merge)
{
    R.ojKind = kind;
    R.ojLobby = lobby;
    netStrCopy(R.ojCode, sizeof(R.ojCode), code);
    R.ojNonce = netRandom32() | 1u;
    R.ojStartUs = now;
    R.ojPhase = OJ_STUN;
    R.ojMerge = merge;
    if (!(R.stunC.state == STUN_DONE && since(now, R.stunC.doneUs) < STUN_FRESH_US)) stunStart(&R.stunC, now);
    if (!merge && R.ojFails == 0) setOjText("Finding your public address...", 1);
}

/* Someone the service sent our way is still being punched towards: a
 * joiner is on the way in. */
static int punchActive(uint64_t now)
{
    int i;
    for (i = 0; i < PUNCH_MAX; i++) {
        if (now < R.punch[i].until) return 1;
    }
    return 0;
}

/* Still alone in our own quick lobby, outside a match? */
static int quickHostAlone(void)
{
    NetHostLobbyInfo li;
    if (!R.host || !R.onlineQuick || !netHostGetDirectLobby(R.host, &li)) return 0;
    return !li.inMatch && li.lobby.num_players <= 1;
}

/* Quick match found nobody (or nobody reachable): host the quick game the
 * next players are sent to. Busy until our own client connects to it. */
static void quickHostOwn(const char *text)
{
    char err[128];
    R.ojPhase = OJ_NONE;
    ndpPrefsToRules(&R.qprefs, &R.quickRules, &R.quickMax);   /* applied at adoption */
    setOjText(text, 1);
    if (hostBegin(0, "Quick Match", NET_MAX_PLAYERS, 1, 1, 1, err, sizeof(err)) != 0) {
        ojFail(err);
    } else {
        R.ojHostPending = 1;
    }
}

static void onlineJoinTick(uint64_t now)
{
    NetDirStatus ds;
    int req;
    uint32_t reqLobby = 0;
    char reqCode[NET_CODE_LEN + 1] = { 0 };

    netMutexLock(R.mx);
    req = R.ojReq;
    if (req) {
        reqLobby = R.ojReqLobby;
        netStrCopy(reqCode, sizeof(reqCode), R.ojReqCode);
        R.ojReq = 0;
    }
    netMutexUnlock(R.mx);

    if (req) {
        R.ojFails = 0;
        R.ojExclude = 0;
        ojBegin(req, reqLobby, reqCode, now, 0);
    } else if (R.ojPhase == OJ_NONE && R.hostedSeqSeen && since(now, R.qmLastUs) >= QUICK_REASK_US && quickHostAlone() &&
               !punchActive(now)) {   /* not while a joiner is on the way in */
        /* Two players who press Quick Match at the same moment are both told
         * to host. Waiting alone, ask again now and then: the service offers
         * only quick lobbies OLDER than ours (net_dir.c sends our lobby id),
         * so the newer host moves into the older lobby, never both ways. */
        R.qmLastUs = now;
        R.ojFails = 0;   /* keep ojExclude: still a game we could not reach */
        ojBegin(OJK_QUICK, 0, "", now, 1);
    }

    if (R.ojPhase == OJ_STUN) {
        NetAddr cands[NET_MAX_CANDS];
        int n;
        stunTick(&R.stunC, R.csock, now, 1, "client");
        if (R.stunC.state == STUN_PENDING && now - R.ojStartUs < STUN_WAIT_US) return;
        n = buildCands(&R.stunC, netUdpLocalPort(R.csock), cands);
        netDirGetStatus(&ds);
        if (ds.state == NDS_UNAVAILABLE) {
            ojFail(ds.lastError[0] ? ds.lastError : "No online service configured");
            return;
        }
        R.ojSeqJoin = ds.joinInfoSeq;
        R.ojSeqFail = ds.failSeq;
        R.ojSeqQuick = ds.quickHostSeq;
        if (R.ojKind == OJK_QUICK) {
            netDirQuick(R.ojNonce, R.ojExclude, &R.qprefs, cands, n);
            if (!R.ojMerge && R.ojFails == 0) setOjText("Looking for a game...", 1);
        } else {
            netDirJoin(R.ojLobby, R.ojCode, R.ojNonce, cands, n);
            setOjText("Asking the online service...", 1);
        }
        R.ojPhase = OJ_ASK;
        return;
    }

    if (R.ojPhase == OJ_ASK) {
        netDirGetStatus(&ds);
        if (ds.joinInfoSeq != R.ojSeqJoin && ds.joinInfo.nonce == R.ojNonce) {
            char t[96];
            {
                /* Only sane addresses get game packets (D416): the service
                 * filters too, but this check does not depend on it. */
                int k, kept = 0;
                for (k = 0; k < ds.joinInfo.ncand && k < NET_MAX_CANDS; k++) {
                    if (ndpCandSendable(&ds.joinInfo.cand[k])) ds.joinInfo.cand[kept++] = ds.joinInfo.cand[k];
                }
                ds.joinInfo.ncand = (uint8_t)kept;
            }
            if (ds.joinInfo.ncand == 0) {
                ojFail("The host published no usable address");
                return;
            }
            if (R.ojMerge) {
                if (!quickHostAlone() || punchActive(now)) {   /* someone joined / is joining us: stay */
                    R.ojPhase = OJ_NONE;
                    return;
                }
                rlogf(NETLOG_INFO, "quick match: moving into %s's older quick lobby", ds.joinInfo.hostName);
                R.ojMerge = 0;   /* from here on, an ordinary quick match */
            }
            if (R.ojKind == OJK_QUICK) {
                /* Matchmaking: watch the connection; a game we cannot reach
                 * is not an error, just a reason to try another one. */
                netClientConnectMulti(R.client, ds.joinInfo.cand, ds.joinInfo.ncand, 0, JOIN_TRIES_QUICK);
                stopHostNow();   /* if we were a lone quick host (after the client let go) */
                R.ojTarget = ds.joinInfo.lobbyId;
                R.ojPhase = OJ_CONNECT;
                netStrFmt(t, sizeof(t), "Found a game -- joining %s...", ds.joinInfo.hostName);
                setOjText(t, 1);
            } else {
                netClientConnectMulti(R.client, ds.joinInfo.cand, ds.joinInfo.ncand, 0, JOIN_TRIES_ONLINE);
                netStrFmt(t, sizeof(t), "Connecting to %s...", ds.joinInfo.hostName);
                R.ojPhase = OJ_NONE;
                setOjText(t, 0);
            }
            rlogf(NETLOG_INFO, "online: joining %s's game (%d address%s)", ds.joinInfo.hostName,
                  ds.joinInfo.ncand, ds.joinInfo.ncand == 1 ? "" : "es");
        } else if (ds.quickHostSeq != R.ojSeqQuick && ds.quickHostNonce == R.ojNonce) {
            R.ojPhase = OJ_NONE;
            if (R.ojMerge) return;   /* nothing older: keep waiting in ours */
            quickHostOwn(R.ojFails ? "No reachable game -- starting one..." : "No open game -- starting one...");
        } else if (ds.failSeq != R.ojSeqFail && ds.fail.nonce == R.ojNonce) {
            ojFail(ds.fail.text);
        } else if (ds.state == NDS_UNAVAILABLE) {
            ojFail(ds.lastError[0] ? ds.lastError : "No online service configured");
        } else if (now - R.ojStartUs > ASK_TIMEOUT_US) {
            ojFail(ds.lastError[0] ? ds.lastError : "The online service did not answer");
        }
        return;
    }

    if (R.ojPhase == OJ_CONNECT) {
        NetClientStatus st;
        netClientGetStatus(R.client, &st);
        if (st.state == NCS_LOBBY) {
            R.ojPhase = OJ_NONE;
            R.ojFails = 0;
            setOjText("", 0);
        } else if (st.state == NCS_FAILED) {
            /* Could not reach it (a strict router on one side), or it filled
             * up / started meanwhile. Try another game; the service hears
             * which one failed. After a few, host one: others may reach us. */
            R.ojFails++;
            rlogf(NETLOG_INFO, "quick match: could not join (%s) -- %s", st.lastError,
                  R.ojFails < QUICK_MAX_FAILS ? "trying another game" : "hosting instead");
            if (R.ojFails < QUICK_MAX_FAILS) {
                R.ojExclude = R.ojTarget;
                setOjText("Could not reach that game -- trying another...", 1);
                ojBegin(OJK_QUICK, 0, "", now, 0);
            } else {
                quickHostOwn("No reachable game -- starting one...");
            }
        } else if (st.state != NCS_CONNECTING) {
            R.ojPhase = OJ_NONE;   /* left / cancelled meanwhile */
            setOjText("", 0);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* LAN discovery                                                             */
/* ------------------------------------------------------------------------ */

static void handleQueryReply(const NetAddr *from, NetR *r)
{
    uint32_t nonce = nrU32(r);
    NetLanGame g;
    char build[NET_BUILDID_MAX];
    int i;
    memset(&g, 0, sizeof(g));
    nrStr(r, g.name, sizeof(g.name));
    nrStr(r, g.leader, sizeof(g.leader));
    g.numPlayers = nrU8(r);
    g.maxPlayers = nrU8(r);
    g.state = nrU8(r);
    nrStr(r, build, sizeof(build));
    if (r->err) return;
    netSanitizeText(g.name, sizeof(g.name), "Lobby");
    netSanitizeText(g.leader, sizeof(g.leader), "?");
    g.compatible = (strncmp(build, R.buildId, 40) == 0);
    netAddrFormat(from, g.addr, sizeof(g.addr));
    netMutexLock(R.mx);
    if (nonce == R.lanNonce) {
        for (i = 0; i < R.lanCount; i++) {
            if (strcmp(R.lan[i].addr, g.addr) == 0) break;
        }
        if (i < LAN_MAX) {
            R.lan[i] = g;
            if (i == R.lanCount) R.lanCount++;
        }
    }
    netMutexUnlock(R.mx);
}

static void sendLanQuery(uint16_t port)
{
    uint8_t b[128];
    NetW w;
    NetAddr bc = netAddrMake(255, 255, 255, 255, port);
    NetAddr lo = netAddrMake(127, 0, 0, 1, port);
    nwInit(&w, b, sizeof(b));
    netWriteHeader(&w, NP_QUERY, 0);
    nwU32(&w, R.lanNonce);
    while (w.len < (int)sizeof(b)) nwU8(&w, 0);
    netFinishHeader(&w);
    netUdpSend(R.csock, &bc, b, w.len);
    netUdpSend(R.csock, &lo, b, w.len);
}

/* ------------------------------------------------------------------------ */
/* The thread                                                                */
/* ------------------------------------------------------------------------ */

static void netThreadMain(void *arg)
{
    uint8_t buf[NET_MAX_PACKET + 64];
    (void)arg;
    while (!R.quit) {
        NetUdp *socks[2];
        uint64_t now;
        int doConnect = 0, doLeave = 0, doStopHost = 0, doLan = 0, connectServer = 0;
        char addr[128];
        uint16_t port = 0, lanPort = 0;

        socks[0] = R.csock;
        socks[1] = R.hsock;
        netUdpWait(socks, 2, 1000);

        /* Take requests. */
        netMutexLock(R.mx);
        if (R.pendingHost) {
            NetHost *nh = R.pendingHost;
            NetUdp *ns = R.pendingHsock;
            int online = R.pendingOnline, pub = R.pendingPublic, quick = R.pendingQuick;
            R.pendingHost = NULL;
            R.pendingHsock = NULL;
            netMutexUnlock(R.mx);
            stopHostNow();
            netMutexLock(R.mx);
            R.host = nh;
            R.hsock = ns;
            R.hostPort = netUdpLocalPort(ns);
            R.onlineHost = online;
            netMutexUnlock(R.mx);
            R.onlinePublic = pub;
            R.onlineQuick = quick;
            R.registeredOnce = 0;
            R.hostedSeqSeen = 0;
            R.lastHostPushUs = 0;
            R.qmLastUs = netTimeUs();
            if (online) {
                netHostSetDirectInfo(R.host, "", (uint8_t)(NL_ONLINE | (pub ? NL_PUBLIC : 0) | (quick ? NL_QUICK : 0)),
                                     quick);
                if (quick) netHostSetQuickRules(R.host, &R.quickRules, R.quickMax);
                rlogf(NETLOG_INFO, "online: hosting a %s game on UDP %u", quick ? "quick-match" : pub ? "public" : "private",
                      (unsigned)R.hostPort);
            }
            netMutexLock(R.mx);
        }
        doStopHost = R.stopHostReq;
        R.stopHostReq = 0;
        doLeave = R.leaveReq;
        R.leaveReq = 0;
        if (R.connectReq) {
            doConnect = 1;
            R.connectReq = 0;
            memcpy(addr, R.connectAddr, sizeof(addr));
            port = R.connectPort;
            connectServer = R.connectServer;
        }
        if (R.lanReq) {
            doLan = 1;
            lanPort = R.lanPort;
            R.lanReq = 0;
        }
        netMutexUnlock(R.mx);

        if (doLeave) {
            netClientDisconnect(R.client);
            stopHostNow();
            R.ojPhase = OJ_NONE;
            R.ojHostPending = 0;
            setOjText("", 0);
        }
        if (doStopHost) stopHostNow();
        if (doConnect) {
            NetAddr a;
            if (netAddrResolve(addr, port, &a) == 0) {
                netClientConnect(R.client, &a, connectServer);
            } else {
                char msg[160];
                netStrFmt(msg, sizeof(msg), "Cannot resolve address '%s'", addr);
                netClientFail(R.client, msg);
            }
        }
        if (doConnect && R.ojHostPending) {
            R.ojHostPending = 0;
            setOjText("Waiting for players...", 0);
        }
        if (doLan) sendLanQuery(lanPort);

        now = netTimeUs();
        if (R.host) netHostPump(R.host, now);

        {
            int guard;
            for (guard = 0; guard < 1024; guard++) {
                NetAddr from;
                int n = netUdpRecv(R.csock, &from, buf, sizeof(buf));
                if (n <= 0) break;
                if (n > NET_MAX_PACKET) continue;
                if (netStunIsMessage(buf, n)) {
                    stunOnPacket(&R.stunC, buf, n, "client");
                    continue;
                }
                {
                    NetR r;
                    NetHeader hd;
                    nrInit(&r, buf, n);
                    if (netReadHeader(&r, &hd) == 0 && hd.type == NP_QUERY_REPLY) {
                        handleQueryReply(&from, &r);
                        continue;
                    }
                }
                netClientOnPacket(R.client, &from, buf, n, now);
            }
        }
        netClientTick(R.client, now);

        onlineHostTick(now);
        onlineJoinTick(now);
        punchTick(now);
    }
}

/* ------------------------------------------------------------------------ */
/* API                                                                       */
/* ------------------------------------------------------------------------ */

int netRuntimeStart(const char *playerName, const char *buildId, NetLogFn log, void *logCtx)
{
    NetClientConfig cc;
    char err[128];
    if (R.running) {
        netRuntimeSetName(playerName);
        return 0;
    }
    memset(&R, 0, sizeof(R));
    if (netSockStartup() != 0) return -1;
    R.log = log;
    R.logCtx = logCtx;
    netStrCopy(R.buildId, NET_BUILDID_MAX, buildId);
    netStrCopy(R.name, NET_NAME_MAX, playerName);
    netStrFmt(R.stunCfg, sizeof(R.stunCfg), "%s:%d", NET_STUN_DEFAULT_HOST, NET_STUN_DEFAULT_PORT);
    R.mx = netMutexCreate();
    R.csock = netUdpOpen(0, 1, err, sizeof(err));
    if (!R.mx || !R.csock) {
        rlog(NETLOG_ERROR, err);
        if (R.csock) netUdpClose(R.csock);
        if (R.mx) netMutexDestroy(R.mx);
        netSockCleanup();
        memset(&R, 0, sizeof(R));
        return -1;
    }
    memset(&cc, 0, sizeof(cc));
    netStrCopy(cc.name, NET_NAME_MAX, playerName);
    netStrCopy(cc.buildId, NET_BUILDID_MAX, buildId);
    cc.log = log;
    cc.logCtx = logCtx;
    R.client = netClientCreate(&cc, netUdpTransport(R.csock));
    if (!R.client) {
        netUdpClose(R.csock);
        netMutexDestroy(R.mx);
        netSockCleanup();
        memset(&R, 0, sizeof(R));
        return -1;
    }
    R.quit = 0;
    R.thread = netThreadStart(netThreadMain, NULL);
    if (!R.thread) {
        netClientDestroy(R.client);
        netUdpClose(R.csock);
        netMutexDestroy(R.mx);
        netSockCleanup();
        memset(&R, 0, sizeof(R));
        return -1;
    }
    R.running = 1;
    rlog(NETLOG_INFO, "netplay runtime started");
    return 0;
}

void netRuntimeStop(void)
{
    if (!R.running) return;
    R.quit = 1;
    netThreadJoin(R.thread);
    R.thread = NULL;
    netClientDestroy(R.client);
    R.client = NULL;
    if (R.pendingHost) netHostDestroy(R.pendingHost);
    if (R.pendingHsock) netUdpClose(R.pendingHsock);
    stopHostNow();   /* queues the UNHOST... */
    netDirStop();    /* ...which goes out as the directory client winds down */
    netUdpClose(R.csock);
    netMutexDestroy(R.mx);
    netSockCleanup();
    rlog(NETLOG_INFO, "netplay runtime stopped");
    memset(&R, 0, sizeof(R));
}

void netRuntimeShutdownForExit(void)
{
    if (!R.running) return;
    R.quit = 1;
    netThreadJoin(R.thread);
    R.thread = NULL;
    netDirShutdownForExit();         /* never waits; the service drops our lobby
                                      * when the connection closes (or on TTL) */
    netClientDisconnect(R.client);   /* sends DISCONNECT x3 */
    if (R.host) {
        netHostDestroy(R.host);      /* DISCONNECT to every joined player */
        R.host = NULL;
    }
    R.running = 0;
}

int netRuntimeRunning(void)
{
    return R.running;
}

NetClient *netRuntimeClient(void)
{
    return R.client;
}

void netRuntimeSetName(const char *name)
{
    if (!R.running) return;
    netMutexLock(R.mx);
    netStrCopy(R.name, NET_NAME_MAX, name);
    netMutexUnlock(R.mx);
    if (R.client) netClientSetName(R.client, name);
    netDirSetName(name);
}

int netRuntimeHost(uint16_t port, const char *lobbyName, int maxPlayers, char *err, int errLen)
{
    return hostBegin(port, lobbyName, maxPlayers, 0, 0, 0, err, errLen);
}

int netRuntimeIsHosting(void)
{
    int r;
    if (!R.running) return 0;
    netMutexLock(R.mx);
    r = (R.host != NULL || R.pendingHost != NULL);
    netMutexUnlock(R.mx);
    return r;
}

uint16_t netRuntimeHostPort(void)
{
    uint16_t p;
    if (!R.running) return 0;
    netMutexLock(R.mx);
    p = R.hostPort;
    netMutexUnlock(R.mx);
    return p;
}

void netRuntimeConnect(const char *addr, uint16_t defaultPort, int serverMode)
{
    if (!R.running) return;
    netMutexLock(R.mx);
    netStrCopy(R.connectAddr, sizeof(R.connectAddr), addr);
    R.connectPort = defaultPort;
    R.connectServer = serverMode;
    R.connectReq = 1;
    R.ojReq = 0;
    netMutexUnlock(R.mx);
}

void netRuntimeLeave(void)
{
    if (!R.running) return;
    netMutexLock(R.mx);
    R.leaveReq = 1;
    R.connectReq = 0;
    R.ojReq = 0;
    if (R.pendingHost) {
        netHostDestroy(R.pendingHost);
        R.pendingHost = NULL;
    }
    if (R.pendingHsock) {
        netUdpClose(R.pendingHsock);
        R.pendingHsock = NULL;
    }
    netMutexUnlock(R.mx);
}

void netRuntimeLanScan(uint16_t port)
{
    if (!R.running) return;
    netMutexLock(R.mx);
    R.lanNonce = netRandom32();
    R.lanCount = 0;
    R.lanPort = port;
    R.lanReq = 1;
    netMutexUnlock(R.mx);
}

int netRuntimeLanResults(NetLanGame *out, int max)
{
    int n;
    if (!R.running) return 0;
    netMutexLock(R.mx);
    n = R.lanCount < max ? R.lanCount : max;
    if (n > 0) memcpy(out, R.lan, sizeof(NetLanGame) * (size_t)n);
    netMutexUnlock(R.mx);
    return n;
}

/* ---- online service (D414) ---- */

void netRuntimeSetService(const char *url, int forcePoll)
{
    NetDirConfig dc;
    if (!R.running) return;
    memset(&dc, 0, sizeof(dc));
    netMutexLock(R.mx);
    netStrCopy(R.serviceUrl, sizeof(R.serviceUrl), url ? url : "");
    R.forcePoll = forcePoll;
    netStrCopy(dc.url, sizeof(dc.url), R.serviceUrl);
    netStrCopy(dc.build, sizeof(dc.build), R.buildId);
    netStrCopy(dc.name, sizeof(dc.name), R.name);
    netMutexUnlock(R.mx);
    {
        /* trailing slashes would double up in "/api/v1/..." */
        int n = (int)strlen(dc.url);
        while (n > 0 && dc.url[n - 1] == '/') dc.url[--n] = 0;
    }
    dc.forcePoll = forcePoll;
    dc.log = R.log;
    dc.logCtx = R.logCtx;
    netDirStart(&dc);
}

void netRuntimeSetStun(const char *server)
{
    if (!R.running) return;
    netMutexLock(R.mx);
    if (server && server[0]) {
        netStrCopy(R.stunCfg, sizeof(R.stunCfg), server);
    } else {
        netStrFmt(R.stunCfg, sizeof(R.stunCfg), "%s:%d", NET_STUN_DEFAULT_HOST, NET_STUN_DEFAULT_PORT);
    }
    R.stunCfgChanged = 1;
    netMutexUnlock(R.mx);
}

int netRuntimeHostOnline(uint16_t port, const char *lobbyName, int maxPlayers, int isPublic, int quick, char *err,
                         int errLen)
{
    return hostBegin(port, lobbyName, maxPlayers, 1, isPublic, quick, err, errLen);
}

int netRuntimeIsHostingOnline(void)
{
    int r;
    if (!R.running) return 0;
    netMutexLock(R.mx);
    r = R.onlineHost || (R.pendingHost != NULL && R.pendingOnline);
    netMutexUnlock(R.mx);
    return r;
}

void netRuntimeJoinOnline(uint32_t lobbyId, const char *code)
{
    if (!R.running) return;
    netMutexLock(R.mx);
    R.ojReq = OJK_JOIN;
    R.ojReqLobby = lobbyId;
    netStrCopy(R.ojReqCode, sizeof(R.ojReqCode), code ? code : "");
    R.ojBusy = 1;
    netStrCopy(R.ojText, sizeof(R.ojText), "Joining...");
    R.connectReq = 0;
    netMutexUnlock(R.mx);
}

void netRuntimeQuickOnline(const NdpPrefs *prefs)
{
    if (!R.running) return;
    netMutexLock(R.mx);
    if (prefs) {
        R.qprefs = *prefs;
        ndpNormalizePrefs(&R.qprefs);
    } else {
        ndpPrefsAny(&R.qprefs);
    }
    ndpPrefsToRules(&R.qprefs, &R.quickRules, &R.quickMax);
    R.ojReq = OJK_QUICK;
    R.ojBusy = 1;
    netStrCopy(R.ojText, sizeof(R.ojText), "Looking for a game...");
    R.connectReq = 0;
    netMutexUnlock(R.mx);
}

int netRuntimeOnlineBusy(char *text, int n)
{
    int busy;
    if (!R.running) {
        if (n > 0) text[0] = 0;
        return 0;
    }
    netMutexLock(R.mx);
    busy = R.ojBusy || R.ojReq != 0;
    netStrCopy(text, n, R.ojText);
    netMutexUnlock(R.mx);
    return busy;
}

void netRuntimeReportMatch(const NdpResult *r)
{
    if (!R.running || !netRuntimeIsHostingOnline()) return;
    netDirReportResult(r);
}
