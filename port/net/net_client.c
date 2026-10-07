/*
 * net_client.c -- netplay client (D409). See net_client.h.
 */
#include "net_client.h"
#include "net_gamedata.h"
#include "net_plat.h"
#include "net_session.h"
#include "net_wire.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JOIN_RESEND_US         500000ull
#define JOIN_MAX_TRIES         12
#define LOBBY_TIMEOUT_US     15000000ull
#define MATCH_TIMEOUT_US      8000000ull
#define INPUTS_RESEND_US        12000ull
#define INPUTS_ACK_US            2000ull
#define MAX_RECS_PER_MSG           32
#define HASH_INTERVAL              30   /* must match net_host.c */

enum { NME_FINISHED = 1, NME_ABORTED = 2, NME_LEFT = 3 };

typedef struct CMatch {
    int phase;
    int taken;
    int go;
    int over;            /* host lost / kicked: GetBundle returns -1 */
    uint32_t id;
    int slot;
    NetMatchStart ms;
    uint8_t n, delay;
    NetInputRec local[NET_RING];
    uint32_t localNext;  /* frames < localNext submitted */
    uint32_t hostInAck;  /* host holds our records < hostInAck */
    NetBundle bundles[NET_RING];
    uint8_t bHave[NET_RING];
    uint32_t bNext;      /* all bundles < bNext received */
    uint32_t consumed;
    uint32_t hostNext;
    uint32_t latest[NET_MAX_PLAYERS];
    uint8_t disc;
    uint8_t loadedMask;  /* NM_MATCH_WAIT: who finished loading */
    uint8_t waitMask;    /* ...out of these */
    int abortPending;
    uint32_t abortFrame;
    int desync;
    uint32_t desyncFrame;
    int ackDirty;
    int newLocal;
    uint64_t lastInputsUs;
} CMatch;

struct NetClient {
    NetClientConfig cfg;
    NetTransport t;
    NetMutex *mx;
    uint64_t now;

    int state;
    int serverMode;
    NetAddr host;
    NetAddr cand[NET_MAX_CANDS];   /* while CONNECTING: every address to try */
    int ncand;
    int joinMaxTries;
    uint32_t nonce;
    uint64_t lastJoinUs;
    int joinTries;
    int connValid;
    NetConn conn;

    int slot;
    int lobbyValid;
    NetLobbyState lobby;
    NetListEntry list[NET_LIST_MAX];
    int listCount;
    uint32_t listSeq;
    char lastError[96];
    uint32_t errorSeq;
    char notice[NET_NOTICE_RING][96];
    uint32_t noticeSeq;

    CMatch m;
};

static void clog_(NetClient *c, int level, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    if (!c->cfg.log) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    c->cfg.log(c->cfg.logCtx, level, buf);
}

static void setError(NetClient *c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->lastError, sizeof(c->lastError), fmt, ap);
    va_end(ap);
    c->lastError[sizeof(c->lastError) - 1] = 0;
    c->errorSeq++;
    clog_(c, NETLOG_WARN, "%s", c->lastError);
}

static void pushNotice(NetClient *c, const char *text)
{
    netStrCopy(c->notice[c->noticeSeq % NET_NOTICE_RING], 96, text);
    c->noticeSeq++;
    clog_(c, NETLOG_INFO, "notice: %s", text);
}

static void sendRel(NetClient *c, uint8_t type, const NetW *w)
{
    if (!c->connValid || w->overflow) return;
    if (netConnSendReliable(&c->conn, type, w->buf, w->len) != 0) {
        setError(c, "Connection congested");
    }
}

static void matchReset(NetClient *c)
{
    memset(&c->m, 0, sizeof(c->m));
}

/* ------------------------------------------------------------------------ */

static void sendJoin(NetClient *c)
{
    uint8_t b[NET_JOIN_PAD + 16];
    NetW w;
    nwInit(&w, b, sizeof(b));
    netWriteHeader(&w, NP_JOIN, 0);
    nwU32(&w, c->nonce);
    nwU8(&w, (uint8_t)(c->serverMode ? NJ_SERVER : NJ_DIRECT));
    nwStr(&w, c->cfg.name, NET_NAME_MAX - 1);
    nwStr(&w, c->cfg.buildId, NET_BUILDID_MAX - 1);
    while (w.len < NET_JOIN_PAD) nwU8(&w, 0);   /* anti-amplification padding */
    netFinishHeader(&w);
    if (c->ncand > 1) {
        /* Online (directory) join: the host may answer on any of its
         * addresses (public via STUN, LAN); the first ACCEPT picks one. */
        int i;
        for (i = 0; i < c->ncand; i++) c->t.send(c->t.ctx, &c->cand[i], b, w.len);
    } else {
        c->t.send(c->t.ctx, &c->host, b, w.len);
    }
    c->lastJoinUs = c->now;
    c->joinTries++;
}

static void sendDisconnect(NetClient *c)
{
    uint8_t b[NET_HEADER_SIZE];
    NetW w;
    int i;
    if (!c->connValid) return;
    nwInit(&w, b, sizeof(b));
    netWriteHeader(&w, NP_DISCONNECT, c->conn.connId);
    netFinishHeader(&w);
    for (i = 0; i < 3; i++) c->t.send(c->t.ctx, &c->host, b, w.len);
}

static void dropConnection(NetClient *c)
{
    if (c->connValid) netConnFree(&c->conn);
    c->connValid = 0;
    c->lobbyValid = 0;
    c->slot = -1;
}

/* ------------------------------------------------------------------------ */
/* Match frames                                                              */
/* ------------------------------------------------------------------------ */

static void onFrames(NetClient *c, NetR *r)
{
    CMatch *m = &c->m;
    uint32_t mid = nrU32(r);
    uint32_t inputAck = nrU32(r);
    uint32_t hostNext = nrU32(r);
    int n = nrU8(r);
    int i, count;
    uint32_t first;
    if (r->err || n > NET_MAX_PLAYERS) return;
    if (m->phase == NMP_NONE || m->phase == NMP_OVER || mid != m->id) {
        for (i = 0; i < n; i++) (void)nrU32(r);
        return;
    }
    for (i = 0; i < n; i++) m->latest[i] = nrU32(r);
    m->disc = nrU8(r);
    first = nrU32(r);
    count = nrU8(r);
    if (r->err) return;
    if (inputAck > m->hostInAck && inputAck <= m->localNext) m->hostInAck = inputAck;
    m->hostNext = hostNext;
    for (i = 0; i < count; i++) {
        NetBundle b;
        uint32_t f = first + (uint32_t)i;
        if (netDecBundle(r, &b) != 0) return;
        if (b.frame != f) return;
        if (f < m->bNext || f >= m->consumed + NET_RING) continue;
        m->bundles[f % NET_RING] = b;
        m->bHave[f % NET_RING] = 1;
    }
    {
        uint32_t before = m->bNext;
        while (m->bNext < m->consumed + NET_RING && m->bHave[m->bNext % NET_RING] &&
               m->bundles[m->bNext % NET_RING].frame == m->bNext) {
            m->bNext++;
        }
        if (m->bNext != before) m->ackDirty = 1;
    }
}

static void sendInputs(NetClient *c)
{
    CMatch *m = &c->m;
    uint8_t buf[NET_MAX_PACKET];
    NetW w;
    NetUnrel u;
    uint32_t f;
    int countPos, count = 0;
    nwInit(&w, buf, NET_MAX_PACKET - 40);
    nwU32(&w, m->id);
    nwU32(&w, m->bNext);
    nwU32(&w, m->hostInAck);
    countPos = w.len;
    nwU8(&w, 0);
    for (f = m->hostInAck; f < m->localNext && count < MAX_RECS_PER_MSG; f++) {
        int before = w.len;
        netEncInputRec(&w, &m->local[f % NET_RING]);
        if (w.overflow) {
            w.overflow = 0;
            w.len = before;
            break;
        }
        count++;
    }
    buf[countPos] = (uint8_t)count;
    u.type = NM_INPUTS;
    u.data = buf;
    u.len = w.len;
    netConnTransmit(&c->conn, &c->t, c->now, &u, 1, 0);
    m->lastInputsUs = c->now;
    m->ackDirty = 0;
    m->newLocal = 0;
}

/* ------------------------------------------------------------------------ */
/* Session messages                                                          */
/* ------------------------------------------------------------------------ */

static void onMsg(void *ctx, NetConn *conn, uint8_t type, NetR *r)
{
    NetClient *c = (NetClient *)ctx;
    (void)conn;
    switch (type) {
    case NM_FRAMES:
        onFrames(c, r);
        break;

    case NM_MATCH_WAIT: {
        uint32_t mid = nrU32(r);
        uint8_t loaded = nrU8(r), want = nrU8(r);
        if (r->err || mid != c->m.id || c->m.go) break;
        c->m.loadedMask = loaded;
        c->m.waitMask = want;
        break;
    }

    case NM_LOBBY_STATE: {
        NetLobbyState l;
        int yours;
        if (netDecLobby(r, &l) != 0) break;
        yours = nrU8(r);
        if (r->err) break;
        c->lobby = l;
        c->lobbyValid = 1;
        c->slot = (yours < NET_MAX_PLAYERS) ? yours : -1;
        c->state = NCS_LOBBY;
        break;
    }

    case NM_JOINED: {
        uint32_t id = nrU32(r);
        char code[NET_CODE_LEN + 2];
        int slot;
        nrStr(r, code, sizeof(code));
        slot = nrU8(r);
        if (r->err) break;
        (void)id;
        c->state = NCS_LOBBY;
        c->slot = (slot < NET_MAX_PLAYERS) ? slot : -1;
        break;
    }

    case NM_FAIL: {
        int reason = nrU8(r);
        char text[NET_CHAT_MAX + 1];
        nrStr(r, text, sizeof(text));
        if (r->err) break;
        netSanitizeText(text, sizeof(text), "Request failed");
        (void)reason;
        setError(c, "%s", text);
        break;
    }

    case NM_LIST: {
        int n = nrU8(r), i;
        NetListEntry tmp[NET_LIST_MAX];
        if (r->err || n > NET_LIST_MAX) break;
        for (i = 0; i < n; i++) {
            if (netDecListEntry(r, &tmp[i]) != 0) return;
        }
        memcpy(c->list, tmp, sizeof(NetListEntry) * (size_t)n);
        c->listCount = n;
        c->listSeq++;
        break;
    }

    case NM_NOTICE: {
        char text[NET_CHAT_MAX + 1];
        nrStr(r, text, sizeof(text));
        if (r->err) break;
        netSanitizeText(text, sizeof(text), "");
        if (text[0]) pushNotice(c, text);
        break;
    }

    case NM_CHAT: {
        char name[NET_NAME_MAX], text[NET_CHAT_MAX + 1], line[128];
        (void)nrU8(r);
        nrStr(r, name, sizeof(name));
        nrStr(r, text, sizeof(text));
        if (r->err) break;
        netSanitizeText(name, sizeof(name), "?");
        netSanitizeText(text, sizeof(text), "");
        netStrFmt(line, sizeof(line), "%s: %s", name, text);
        pushNotice(c, line);
        break;
    }

    case NM_MATCH_START: {
        NetMatchStart ms;
        int yours;
        if (netDecMatchStart(r, &ms) != 0) break;
        yours = nrU8(r);
        if (r->err || yours >= ms.num_players) break;
        if (c->m.phase != NMP_NONE && c->m.phase != NMP_OVER) break;
        matchReset(c);
        c->m.phase = NMP_STARTING;
        c->m.id = ms.match_id;
        c->m.slot = yours;
        c->m.ms = ms;
        c->m.n = ms.num_players;
        c->m.delay = ms.delay;
        c->m.localNext = ms.delay;
        c->m.hostInAck = ms.delay;
        clog_(c, NETLOG_INFO, "match %08x starting: slot %d of %d, delay %d", ms.match_id, yours, ms.num_players, ms.delay);
        break;
    }

    case NM_MATCH_GO: {
        uint32_t mid = nrU32(r);
        if (!r->err && mid == c->m.id && c->m.phase != NMP_NONE && c->m.phase != NMP_OVER) {
            c->m.go = 1;
            if (c->m.phase == NMP_LOADED) c->m.phase = NMP_RUNNING;
        }
        break;
    }

    case NM_MATCH_END: {
        uint32_t mid = nrU32(r);
        int reason = nrU8(r);
        uint32_t frame = nrU32(r);
        if (r->err || mid != c->m.id) break;
        if (reason == NME_ABORTED) {
            c->m.abortPending = 1;
            c->m.abortFrame = frame;
        }
        break;
    }

    case NM_DESYNC: {
        uint32_t mid = nrU32(r);
        uint32_t frame = nrU32(r);
        int mask = nrU8(r);
        if (r->err || mid != c->m.id) break;
        if (!c->m.desync) {
            char line[96];
            c->m.desync = 1;
            c->m.desyncFrame = frame;
            netStrFmt(line, sizeof(line), "DESYNC detected at frame %u (players %02x)", frame, mask);
            pushNotice(c, line);
        }
        break;
    }

    default:
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* Packets / tick                                                            */
/* ------------------------------------------------------------------------ */

static void onPacketLocked(NetClient *c, const NetAddr *from, const uint8_t *data, int len)
{
    NetR r;
    NetHeader hd;
    int rc;
    int fromHost;
    nrInit(&r, data, len);
    rc = netReadHeader(&r, &hd);
    fromHost = netAddrEq(from, &c->host);
    if (!fromHost && c->state == NCS_CONNECTING) {
        int i;
        for (i = 0; i < c->ncand; i++) {
            if (netAddrEq(from, &c->cand[i])) fromHost = 1;
        }
    }
    if (rc == -2 && c->state == NCS_CONNECTING && fromHost) {
        setError(c, "The host uses a different netplay protocol version");
        c->state = NCS_FAILED;
        return;
    }
    if (rc != 0 || !fromHost) return;

    switch (hd.type) {
    case NP_JOIN_ACCEPT: {
        uint32_t nonce = nrU32(&r);
        uint32_t connId = nrU32(&r);
        int inLobby = nrU8(&r);
        int serverMode = nrU8(&r);
        if (r.err || c->state != NCS_CONNECTING || nonce != c->nonce || connId == 0) return;
        c->host = *from;   /* the candidate that answered */
        c->ncand = 0;
        netConnInit(&c->conn, &c->host, connId, c->now);
        c->connValid = 1;
        c->serverMode = serverMode;
        c->state = inLobby ? NCS_LOBBY : NCS_CONNECTED;
        c->slot = -1;
        clog_(c, NETLOG_INFO, "connected (conn %08x, %s)", connId, serverMode ? "server" : "direct host");
        break;
    }
    case NP_JOIN_REJECT: {
        uint32_t nonce = nrU32(&r);
        int reason = nrU8(&r);
        if (r.err || c->state != NCS_CONNECTING || nonce != c->nonce) return;
        setError(c, "Connection refused: %s", netRejectReasonText(reason));
        c->state = NCS_FAILED;
        break;
    }
    case NP_SESSION:
        if (!c->connValid || hd.conn_id != c->conn.connId) return;
        netConnReceive(&c->conn, &r, c->now, onMsg, c);
        break;
    case NP_DISCONNECT:
        if (!c->connValid || hd.conn_id != c->conn.connId) return;
        setError(c, "The host closed the connection");
        if (c->m.phase != NMP_NONE && c->m.phase != NMP_OVER) c->m.over = 1;
        dropConnection(c);
        c->state = NCS_FAILED;
        break;
    default:
        break;
    }
}

void netClientOnPacket(NetClient *c, const NetAddr *from, const uint8_t *data, int len, uint64_t nowUs)
{
    netMutexLock(c->mx);
    c->now = nowUs;
    onPacketLocked(c, from, data, len);
    netMutexUnlock(c->mx);
}

static void tickLocked(NetClient *c)
{
    CMatch *m = &c->m;
    if (c->state == NCS_CONNECTING) {
        if (c->now - c->lastJoinUs >= JOIN_RESEND_US) {
            if (c->joinTries >= (c->joinMaxTries > 0 ? c->joinMaxTries : JOIN_MAX_TRIES)) {
                char a[32];
                netAddrFormat(&c->host, a, sizeof(a));
                if (c->ncand > 1) {
                    setError(c, "No response from the host (tried %d addresses)", c->ncand);
                } else {
                    setError(c, "No response from %s", a);
                }
                c->state = NCS_FAILED;
                return;
            }
            sendJoin(c);
        }
        return;
    }
    if (!c->connValid) return;

    {
        int inMatch = (m->phase == NMP_RUNNING || m->phase == NMP_LOADED || m->phase == NMP_STARTING);
        uint64_t limit = inMatch ? MATCH_TIMEOUT_US : LOBBY_TIMEOUT_US;
        if (c->now - c->conn.lastRecvUs > limit || c->conn.failed) {
            setError(c, "Connection to the host was lost");
            if (inMatch) m->over = 1;
            dropConnection(c);
            c->state = NCS_FAILED;
            return;
        }
    }

    if (m->go && (m->phase == NMP_RUNNING || m->phase == NMP_LOADED)) {
        int pending = m->localNext > m->hostInAck;
        if ((m->newLocal && pending) ||
            (m->ackDirty && c->now - m->lastInputsUs >= INPUTS_ACK_US) ||
            c->now - m->lastInputsUs >= INPUTS_RESEND_US) {
            sendInputs(c);
            return;   /* sendInputs already flushed the connection */
        }
    }
    netConnTransmit(&c->conn, &c->t, c->now, NULL, 0, 0);
}

void netClientTick(NetClient *c, uint64_t nowUs)
{
    netMutexLock(c->mx);
    c->now = nowUs;
    tickLocked(c);
    netMutexUnlock(c->mx);
}

void netClientPump(NetClient *c, uint64_t nowUs)
{
    uint8_t buf[NET_MAX_PACKET + 64];
    int guard;
    netMutexLock(c->mx);
    c->now = nowUs;
    for (guard = 0; guard < 1024; guard++) {
        NetAddr from;
        int n = c->t.recv(c->t.ctx, &from, buf, sizeof(buf));
        if (n <= 0) break;
        if (n > NET_MAX_PACKET) continue;
        onPacketLocked(c, &from, buf, n);
    }
    tickLocked(c);
    netMutexUnlock(c->mx);
}

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

NetClient *netClientCreate(const NetClientConfig *cfg, NetTransport t)
{
    NetClient *c = (NetClient *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->cfg = *cfg;
    netSanitizeText(c->cfg.name, NET_NAME_MAX, "Player");
    c->t = t;
    c->mx = netMutexCreate();
    if (!c->mx) {
        free(c);
        return NULL;
    }
    c->slot = -1;
    return c;
}

void netClientDestroy(NetClient *c)
{
    if (!c) return;
    netMutexLock(c->mx);
    sendDisconnect(c);
    dropConnection(c);
    netMutexUnlock(c->mx);
    netMutexDestroy(c->mx);
    free(c);
}

void netClientSetName(NetClient *c, const char *name)
{
    netMutexLock(c->mx);
    netStrCopy(c->cfg.name, NET_NAME_MAX, name);
    netSanitizeText(c->cfg.name, NET_NAME_MAX, "Player");
    netMutexUnlock(c->mx);
}

int netClientConnectMulti(NetClient *c, const NetAddr *cands, int n, int serverMode, int maxTries)
{
    int i;
    if (!cands || n < 1) return -1;
    if (n > NET_MAX_CANDS) n = NET_MAX_CANDS;
    netMutexLock(c->mx);
    sendDisconnect(c);
    dropConnection(c);
    matchReset(c);
    c->host = cands[0];
    c->ncand = (n > 1) ? n : 0;
    for (i = 0; i < n; i++) c->cand[i] = cands[i];
    c->joinMaxTries = maxTries;
    c->serverMode = serverMode;
    c->state = NCS_CONNECTING;
    c->nonce = netRandom32() | 1u;
    c->joinTries = 0;
    c->listCount = 0;
    c->lastError[0] = 0;
    /* The first JOIN goes out on the next tick, in the pump's clock domain
     * (the selftest drives a virtual clock; never mix in wall time here). */
    c->lastJoinUs = c->now - JOIN_RESEND_US;
    netMutexUnlock(c->mx);
    return 0;
}

int netClientConnect(NetClient *c, const NetAddr *host, int serverMode)
{
    return netClientConnectMulti(c, host, 1, serverMode, 0);
}

void netClientDisconnect(NetClient *c)
{
    netMutexLock(c->mx);
    sendDisconnect(c);
    dropConnection(c);
    matchReset(c);
    c->state = NCS_IDLE;
    netMutexUnlock(c->mx);
}

void netClientFail(NetClient *c, const char *message)
{
    netMutexLock(c->mx);
    sendDisconnect(c);
    dropConnection(c);
    if (c->m.phase != NMP_NONE && c->m.phase != NMP_OVER) c->m.over = 1;
    setError(c, "%s", message ? message : "failed");
    c->state = NCS_FAILED;
    netMutexUnlock(c->mx);
}

/* ------------------------------------------------------------------------ */
/* Requests                                                                  */
/* ------------------------------------------------------------------------ */

static void simpleRel(NetClient *c, uint8_t type)
{
    uint8_t b[4];
    NetW w;
    nwInit(&w, b, sizeof(b));
    sendRel(c, type, &w);
}

void netClientRequestList(NetClient *c)
{
    netMutexLock(c->mx);
    if (c->state == NCS_CONNECTED) simpleRel(c, NM_LIST_REQ);
    netMutexUnlock(c->mx);
}

void netClientCreateLobby(NetClient *c, const char *name, int isPublic, int maxPlayers, int autostart)
{
    uint8_t b[64];
    NetW w;
    netMutexLock(c->mx);
    if (c->state == NCS_CONNECTED) {
        nwInit(&w, b, sizeof(b));
        nwStr(&w, name, NET_LOBBY_NAME_MAX - 1);
        nwU8(&w, (uint8_t)(isPublic ? 1 : 0));
        nwU8(&w, (uint8_t)maxPlayers);
        nwU8(&w, (uint8_t)(autostart ? 1 : 0));
        sendRel(c, NM_CREATE, &w);
    }
    netMutexUnlock(c->mx);
}

void netClientJoinLobby(NetClient *c, uint32_t lobbyId, const char *code)
{
    uint8_t b[32];
    NetW w;
    netMutexLock(c->mx);
    if (c->state == NCS_CONNECTED) {
        nwInit(&w, b, sizeof(b));
        nwU32(&w, lobbyId);
        nwStr(&w, code ? code : "", NET_CODE_LEN);
        sendRel(c, NM_JOIN_CODE, &w);
    }
    netMutexUnlock(c->mx);
}

void netClientQuickMatch(NetClient *c)
{
    netMutexLock(c->mx);
    if (c->state == NCS_CONNECTED) simpleRel(c, NM_QUICK);
    netMutexUnlock(c->mx);
}

void netClientLeaveLobby(NetClient *c)
{
    netMutexLock(c->mx);
    if (c->state == NCS_LOBBY) {
        if (c->serverMode) {
            simpleRel(c, NM_LEAVE_LOBBY);
            c->lobbyValid = 0;
            c->slot = -1;
            c->state = NCS_CONNECTED;
        } else {
            /* A direct host has exactly one lobby: leaving it is leaving. */
            sendDisconnect(c);
            dropConnection(c);
            matchReset(c);
            c->state = NCS_IDLE;
        }
    }
    netMutexUnlock(c->mx);
}

void netClientSetPlayer(NetClient *c, int character, int handicap, int control, int team, int ready)
{
    uint8_t b[8];
    NetW w;
    netMutexLock(c->mx);
    if (c->state == NCS_LOBBY) {
        nwInit(&w, b, sizeof(b));
        nwU8(&w, (uint8_t)character);
        nwU8(&w, (uint8_t)handicap);
        nwU8(&w, (uint8_t)control);
        nwU8(&w, (uint8_t)team);
        nwU8(&w, (uint8_t)(ready ? 1 : 0));
        sendRel(c, NM_PLAYER_SET, &w);
        /* Optimistic local update so the UI doesn't flicker. */
        if (c->lobbyValid && c->slot >= 0) {
            NetPlayerInfo *p = &c->lobby.players[c->slot];
            p->character = (uint8_t)character;
            p->handicap = (uint8_t)handicap;
            p->control = (uint8_t)control;
            p->team = (uint8_t)team;
            p->ready = (uint8_t)(ready ? 1 : 0);
        }
    }
    netMutexUnlock(c->mx);
}

void netClientSetSettings(NetClient *c, const NetSettings *s)
{
    uint8_t b[16];
    NetW w;
    netMutexLock(c->mx);
    if (c->state == NCS_LOBBY) {
        nwInit(&w, b, sizeof(b));
        netEncSettings(&w, s);
        sendRel(c, NM_SETTINGS_SET, &w);
        if (c->lobbyValid) {
            c->lobby.settings = *s;
            ngNormalizeSettings(&c->lobby.settings);
        }
    }
    netMutexUnlock(c->mx);
}

void netClientStartMatch(NetClient *c)
{
    netMutexLock(c->mx);
    if (c->state == NCS_LOBBY) simpleRel(c, NM_START_REQ);
    netMutexUnlock(c->mx);
}

void netClientAbortMatch(NetClient *c)
{
    netMutexLock(c->mx);
    if (c->state == NCS_LOBBY) simpleRel(c, NM_ABORT_REQ);
    netMutexUnlock(c->mx);
}

void netClientChat(NetClient *c, const char *text)
{
    uint8_t b[NET_CHAT_MAX + 4];
    NetW w;
    netMutexLock(c->mx);
    if (c->state == NCS_LOBBY && text && text[0]) {
        nwInit(&w, b, sizeof(b));
        nwStr(&w, text, NET_CHAT_MAX);
        sendRel(c, NM_CHAT, &w);
    }
    netMutexUnlock(c->mx);
}

void netClientGetStatus(NetClient *c, NetClientStatus *out)
{
    netMutexLock(c->mx);
    memset(out, 0, sizeof(*out));
    out->state = c->state;
    out->serverMode = c->serverMode;
    out->slot = c->slot;
    out->lobbyValid = c->lobbyValid;
    if (c->lobbyValid) {
        out->lobby = c->lobby;
        out->isLeader = (c->slot >= 0 && c->lobby.leader == c->slot);
    }
    out->listCount = c->listCount;
    out->listSeq = c->listSeq;
    memcpy(out->list, c->list, sizeof(out->list));
    memcpy(out->lastError, c->lastError, sizeof(out->lastError));
    out->errorSeq = c->errorSeq;
    memcpy(out->notice, c->notice, sizeof(out->notice));
    out->noticeSeq = c->noticeSeq;
    out->rttMs = c->connValid ? c->conn.rttMs : 0.0f;
    netAddrFormat(&c->host, out->hostAddr, sizeof(out->hostAddr));
    out->matchPhase = c->m.phase;
    out->matchId = c->m.id;
    out->matchSlot = c->m.slot;
    out->matchPlayers = c->m.n;
    out->matchDelay = c->m.delay;
    out->desync = c->m.desync;
    out->desyncFrame = c->m.desyncFrame;
    out->discMask = c->m.disc;
    out->bundlesAhead = (c->m.bNext > c->m.consumed) ? c->m.bNext - c->m.consumed : 0;
    out->matchGo = c->m.go;
    out->loadedMask = c->m.loadedMask;
    out->waitMask = c->m.waitMask;
    out->consumed = c->m.consumed;
    memcpy(out->slotLatest, c->m.latest, sizeof(out->slotLatest));
    netMutexUnlock(c->mx);
}

/* ------------------------------------------------------------------------ */
/* Match API                                                                 */
/* ------------------------------------------------------------------------ */

int netClientMatchTake(NetClient *c, NetMatchStart *out, int *slot)
{
    int r = 0;
    netMutexLock(c->mx);
    if (c->m.phase == NMP_STARTING && !c->m.taken) {
        c->m.taken = 1;
        if (out) *out = c->m.ms;
        if (slot) *slot = c->m.slot;
        r = 1;
    }
    netMutexUnlock(c->mx);
    return r;
}

void netClientMatchLoaded(NetClient *c)
{
    uint8_t b[8];
    NetW w;
    netMutexLock(c->mx);
    if (c->m.phase == NMP_STARTING) {
        c->m.phase = c->m.go ? NMP_RUNNING : NMP_LOADED;
        nwInit(&w, b, sizeof(b));
        nwU32(&w, c->m.id);
        sendRel(c, NM_MATCH_LOADED, &w);
    }
    netMutexUnlock(c->mx);
}

int netClientMatchIsGo(NetClient *c)
{
    int r;
    netMutexLock(c->mx);
    r = c->m.go;
    if (r && c->m.phase == NMP_LOADED) c->m.phase = NMP_RUNNING;
    netMutexUnlock(c->mx);
    return r;
}

void netClientMatchSubmit(NetClient *c, uint32_t frame, const NetInputRec *rec)
{
    netMutexLock(c->mx);
    if (c->m.phase == NMP_RUNNING && frame == c->m.localNext &&
        frame - c->m.hostInAck < NET_RING - 1) {
        c->m.local[frame % NET_RING] = *rec;
        c->m.localNext = frame + 1;
        c->m.newLocal = 1;
    }
    netMutexUnlock(c->mx);
}

int netClientMatchGetBundle(NetClient *c, uint32_t frame, NetBundle *out)
{
    int r = 0;
    netMutexLock(c->mx);
    if (c->m.phase == NMP_NONE || c->m.phase == NMP_OVER || c->m.over) {
        r = -1;
    } else if (c->m.abortPending && frame >= c->m.abortFrame) {
        r = -1;
    } else if (frame < c->m.bNext && frame >= c->m.consumed &&
               c->m.bundles[frame % NET_RING].frame == frame) {
        *out = c->m.bundles[frame % NET_RING];
        r = 1;
    }
    netMutexUnlock(c->mx);
    return r;
}

void netClientMatchConsumed(NetClient *c, uint32_t frame)
{
    netMutexLock(c->mx);
    while (c->m.consumed < frame && c->m.consumed < c->m.bNext) {
        c->m.bHave[c->m.consumed % NET_RING] = 0;
        c->m.consumed++;
    }
    netMutexUnlock(c->mx);
}

uint32_t netClientMatchAdvise(NetClient *c, uint32_t frame)
{
    uint32_t sleepUs = 0;
    netMutexLock(c->mx);
    if (c->m.phase == NMP_RUNNING && c->m.n > 1) {
        uint32_t slowest = 0xFFFFFFFFu;
        int s;
        for (s = 0; s < c->m.n; s++) {
            if (s == c->m.slot || c->m.latest[s] == 0xFFFFFFFFu || (c->m.disc & (1u << s))) continue;
            if (c->m.latest[s] < slowest) slowest = c->m.latest[s];
        }
        if (slowest != 0xFFFFFFFFu && slowest != 0) {
            /* How far our input production runs ahead of the slowest peer's,
             * corrected for the age of the host's report (half an RTT). */
            float rttFrames = c->connValid ? c->conn.rttMs / 16.6667f : 0.0f;
            int adv = (int)(c->m.localNext - slowest) - (int)(rttFrames * 0.5f + 0.5f);
            (void)frame;
            if (adv > 1) {
                int ms = adv - 1;
                if (ms > 4) ms = 4;
                sleepUs = (uint32_t)ms * 1000u;
            }
        }
    }
    netMutexUnlock(c->mx);
    return sleepUs;
}

void netClientMatchHash(NetClient *c, uint32_t frame, uint32_t hash)
{
    uint8_t b[16];
    NetW w;
    netMutexLock(c->mx);
    if (c->m.phase == NMP_RUNNING && (frame % HASH_INTERVAL) == 0) {
        nwInit(&w, b, sizeof(b));
        nwU32(&w, c->m.id);
        nwU32(&w, frame);
        nwU32(&w, hash);
        sendRel(c, NM_HASH, &w);
    }
    netMutexUnlock(c->mx);
}

static void endMatch(NetClient *c, uint8_t type, int reason)
{
    uint8_t b[8];
    NetW w;
    if (c->m.phase == NMP_NONE || c->m.phase == NMP_OVER) return;
    nwInit(&w, b, sizeof(b));
    nwU32(&w, c->m.id);
    if (type == NM_MATCH_END) nwU8(&w, (uint8_t)reason);
    sendRel(c, type, &w);
    c->m.phase = NMP_OVER;
}

void netClientMatchFinished(NetClient *c)
{
    netMutexLock(c->mx);
    endMatch(c, NM_MATCH_END, NME_FINISHED);
    netMutexUnlock(c->mx);
}

void netClientMatchLeave(NetClient *c)
{
    netMutexLock(c->mx);
    endMatch(c, NM_MATCH_LEAVE, NME_LEFT);
    netMutexUnlock(c->mx);
}
