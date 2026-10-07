/*
 * net_dir.c -- online directory service client (D410). See net_dir.h and
 * net_dirproto.h (wire format), tools_pc/netplay/cloudflare (the service).
 */
#include "net_dir.h"
#include "net_http.h"
#include "net_plat.h"
#include "net_wire.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUTQ              32
#define JOINREQ_Q          8
#define CONNECT_TIMEOUT_MS 10000
#define HOST_REFRESH_US    30000000ull   /* WebSocket: full record every 30 s */
#define POLL_HOST_US        2000000ull   /* HTTPS polling host: check for joiners */
#define POLL_REFRESH_US    10000000ull   /* ...and refresh the record */
#define PING_US            15000000ull
#define QUEUE_MAX_AGE_US   30000000ull
#define IDLE_GRACE_US      60000000ull   /* stay connected this long after the last use */
#define WS_FAILS_TO_POLL   3             /* proxies that eat WebSockets: fall back */

typedef struct OutMsg {
    uint8_t *data;
    int len;
    uint64_t at;
} OutMsg;

static struct {
    int running;
    volatile int quit;
    NetThread *thread;
    NetMutex *mx;
    NetDirConfig cfg;

    OutMsg out[OUTQ];
    int outHead, outTail;

    int hostWanted;
    NdpHost host;
    int hostDirty;
    int unhostPending;
    NdpUnhost unhost;

    NdpJoinReq jr[JOINREQ_Q];
    int jrHead, jrTail;

    NetDirStatus st;
    uint64_t keepUntilUs;
} D;

static void dlog(int level, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    if (!D.cfg.log) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    D.cfg.log(D.cfg.logCtx, level, buf);
}

/* ---- queue (caller holds D.mx) ---- */

static void enqueueLocked(const NetW *w)
{
    int next = (D.outTail + 1) % OUTQ;
    uint8_t *copy;
    if (w->overflow || w->len <= 0) return;
    if (next == D.outHead) {   /* full: drop the oldest */
        free(D.out[D.outHead].data);
        D.outHead = (D.outHead + 1) % OUTQ;
    }
    copy = (uint8_t *)malloc((size_t)w->len);
    if (!copy) return;
    memcpy(copy, w->buf, (size_t)w->len);
    D.out[D.outTail].data = copy;
    D.out[D.outTail].len = w->len;
    D.out[D.outTail].at = netTimeUs();
    D.outTail = next;
}

static int popLocked(OutMsg *m)
{
    uint64_t now = netTimeUs();
    while (D.outHead != D.outTail) {
        *m = D.out[D.outHead];
        D.outHead = (D.outHead + 1) % OUTQ;
        if (now - m->at <= QUEUE_MAX_AGE_US) return 1;
        free(m->data);   /* stale: the user has moved on */
    }
    return 0;
}

static void clearQueueLocked(void)
{
    OutMsg m;
    while (D.outHead != D.outTail) {
        m = D.out[D.outHead];
        D.outHead = (D.outHead + 1) % OUTQ;
        free(m.data);
    }
}

static void setErrorLocked(const char *text)
{
    netStrCopy(D.st.lastError, sizeof(D.st.lastError), text);
    D.st.errorSeq++;
}

/* ---- incoming ---- */

static void handleMessage(const uint8_t *p, int n)
{
    NetR r;
    uint8_t type;
    if (n < 1) return;
    type = p[0];
    nrInit(&r, p + 1, n - 1);
    switch (type) {
    case NDP_WELCOME: {
        NdpWelcome m;
        if (ndpDecWelcome(&r, &m) != 0) break;
        netMutexLock(D.mx);
        D.st.info = m;
        D.st.infoSeq++;
        netMutexUnlock(D.mx);
        break;
    }
    case NDP_LISTED: {
        static NdpListed m;   /* thread-only scratch: big */
        if (ndpDecListed(&r, &m) != 0) break;
        netMutexLock(D.mx);
        D.st.list = m;
        D.st.listSeq++;
        D.st.info.online = m.online;
        D.st.info.lobbies = m.lobbies;
        D.st.info.matches = m.matches;
        D.st.info.searching = m.searching;
        D.st.infoSeq++;
        netMutexUnlock(D.mx);
        break;
    }
    case NDP_HOSTED: {
        NdpHosted m;
        if (ndpDecHosted(&r, &m) != 0) break;
        netMutexLock(D.mx);
        if (D.hostWanted) {
            int fresh = (D.host.lobbyId != m.lobbyId) || strcmp(D.st.hosted.code, m.code) != 0;
            D.host.lobbyId = m.lobbyId;
            netStrCopy(D.host.token, sizeof(D.host.token), m.token);
            netStrCopy(D.host.code, sizeof(D.host.code), m.code);
            D.st.hosted = m;
            if (fresh) {
                D.st.hostedSeq++;
                dlog(NETLOG_INFO, "online: lobby registered, code %s", m.code);
            }
        }
        netMutexUnlock(D.mx);
        break;
    }
    case NDP_JOININFO: {
        NdpJoinInfo m;
        if (ndpDecJoinInfo(&r, &m) != 0) break;
        netMutexLock(D.mx);
        D.st.joinInfo = m;
        D.st.joinInfoSeq++;
        netMutexUnlock(D.mx);
        break;
    }
    case NDP_QUICKHOST: {
        uint32_t nonce;
        if (ndpDecQuickHost(&r, &nonce) != 0) break;
        netMutexLock(D.mx);
        D.st.quickHostNonce = nonce;
        D.st.quickHostSeq++;
        netMutexUnlock(D.mx);
        break;
    }
    case NDP_JOINREQ: {
        NdpJoinReq m;
        if (ndpDecJoinReq(&r, &m) != 0) break;
        netMutexLock(D.mx);
        if (D.hostWanted && m.lobbyId == D.host.lobbyId) {
            int next = (D.jrTail + 1) % JOINREQ_Q;
            if (next == D.jrHead) D.jrHead = (D.jrHead + 1) % JOINREQ_Q;
            D.jr[D.jrTail] = m;
            D.jrTail = next;
        }
        netMutexUnlock(D.mx);
        dlog(NETLOG_INFO, "online: %s is joining", m.name);
        break;
    }
    case NDP_ERROR: {
        NdpError m;
        if (ndpDecError(&r, &m) != 0) break;
        netMutexLock(D.mx);
        D.st.fail = m;
        D.st.failSeq++;
        setErrorLocked(m.text);
        if (m.reqType == NDP_HOST && m.code == NDPE_FORBIDDEN && D.hostWanted) {
            /* our id was taken over (service restart race): register anew */
            D.host.lobbyId = 0;
            D.host.token[0] = 0;
            D.hostDirty = 1;
        }
        if (m.reqType == NDP_HELLO && m.code == NDPE_VERSION) D.st.state = NDS_UNAVAILABLE;
        netMutexUnlock(D.mx);
        dlog(NETLOG_WARN, "online service: %s", m.text);
        break;
    }
    default:
        break;
    }
}

/* ---- connection ---- */

static int needed(uint64_t now)
{
    int need;
    netMutexLock(D.mx);
    need = D.hostWanted || D.unhostPending || D.outHead != D.outTail || now < D.keepUntilUs;
    netMutexUnlock(D.mx);
    return need;
}

static void setState(int s)
{
    netMutexLock(D.mx);
    if (D.st.state != NDS_UNAVAILABLE) D.st.state = s;
    netMutexUnlock(D.mx);
}

static void encodeHello(NetW *w)
{
    NdpHello h;
    memset(&h, 0, sizeof(h));
    h.version = NDP_VERSION;
    netMutexLock(D.mx);
    netStrCopy(h.build, sizeof(h.build), D.cfg.build);
    netStrCopy(h.name, sizeof(h.name), D.cfg.name);
    netMutexUnlock(D.mx);
    ndpEncHello(w, &h);
}

/* What is due right now for our lobby registration: an UNHOST and/or a
 * HOST refresh, each as its own message. */
typedef struct HostOut {
    uint8_t unhost[64];
    int unhostLen;
    uint8_t host[NDP_MAX_MSG];
    int hostLen;
} HostOut;

static void takeHostingLocked(HostOut *o, uint64_t now, uint64_t *lastHostUs, uint64_t refreshUs)
{
    NetW w;
    o->unhostLen = o->hostLen = 0;
    if (D.unhostPending) {
        nwInit(&w, o->unhost, sizeof(o->unhost));
        ndpEncUnhost(&w, &D.unhost);
        if (!w.overflow) o->unhostLen = w.len;
        D.unhostPending = 0;
    }
    if (D.hostWanted && (D.hostDirty || now - *lastHostUs >= refreshUs)) {
        nwInit(&w, o->host, sizeof(o->host));
        ndpEncHost(&w, &D.host);
        if (!w.overflow) o->hostLen = w.len;
        D.hostDirty = 0;
        *lastHostUs = now;
    }
}

static void wsUrl(char *out, int n)
{
    char base[NET_DIR_URL_MAX];
    netMutexLock(D.mx);
    netStrCopy(base, sizeof(base), D.cfg.url);
    netMutexUnlock(D.mx);
    if (!strncmp(base, "https://", 8)) {
        netStrFmt(out, n, "wss://%s/api/v1/ws", base + 8);
    } else if (!strncmp(base, "http://", 7)) {
        netStrFmt(out, n, "ws://%s/api/v1/ws", base + 7);
    } else {
        netStrFmt(out, n, "%s/api/v1/ws", base);
    }
}

/* One HTTPS polling round: [HELLO][queued...][HOST?][POLL?] -> replies. */
static int pollRound(uint64_t now, uint64_t *lastHostUs, uint64_t *lastPollUs)
{
    uint8_t body[8192], resp[16384];
    int bodyLen = 0, respLen = 0, status, msgs = 0, pos;
    char url[NET_DIR_URL_MAX + 16], err[128];
    uint8_t one[NDP_MAX_MSG];
    NetW w;
    OutMsg m;

#define PUT_FRAME(src, len_)                                         \
    do {                                                             \
        if (bodyLen + 2 + (len_) <= (int)sizeof(body)) {            \
            body[bodyLen] = (uint8_t)((len_) & 0xFF);                \
            body[bodyLen + 1] = (uint8_t)(((len_) >> 8) & 0xFF);     \
            memcpy(body + bodyLen + 2, (src), (size_t)(len_));      \
            bodyLen += 2 + (len_);                                   \
            msgs++;                                                  \
        }                                                            \
    } while (0)

    nwInit(&w, one, sizeof(one));
    encodeHello(&w);
    PUT_FRAME(one, w.len);
    msgs = 0;
    netMutexLock(D.mx);
    while (msgs < 12 && popLocked(&m)) {
        PUT_FRAME(m.data, m.len);
        free(m.data);
    }
    {
        static HostOut ho;   /* thread-only */
        takeHostingLocked(&ho, now, lastHostUs, POLL_REFRESH_US);
        if (ho.unhostLen) PUT_FRAME(ho.unhost, ho.unhostLen);
        if (ho.hostLen) PUT_FRAME(ho.host, ho.hostLen);
    }
    if (D.hostWanted && D.host.lobbyId && now - *lastPollUs >= POLL_HOST_US) {
        NdpPoll p;
        p.lobbyId = D.host.lobbyId;
        netStrCopy(p.token, sizeof(p.token), D.host.token);
        nwInit(&w, one, sizeof(one));
        ndpEncPoll(&w, &p);
        PUT_FRAME(one, w.len);
        *lastPollUs = now;
    }
    netMutexUnlock(D.mx);
#undef PUT_FRAME
    if (msgs == 0) return 0;   /* nothing but HELLO: skip the round trip */

    netMutexLock(D.mx);
    netStrFmt(url, sizeof(url), "%s/api/v1/poll", D.cfg.url);
    netMutexUnlock(D.mx);
    status = netHttpPost(url, body, bodyLen, resp, sizeof(resp), &respLen, CONNECT_TIMEOUT_MS, err, sizeof(err));
    if (status != 200) {
        if (status > 0) netStrFmt(err, sizeof(err), "online service error (HTTP %d)", status);
        netMutexLock(D.mx);
        setErrorLocked(err);
        netMutexUnlock(D.mx);
        return -1;
    }
    for (pos = 0; pos + 2 <= respLen;) {
        int len = resp[pos] | (resp[pos + 1] << 8);
        if (len < 1 || pos + 2 + len > respLen) break;
        handleMessage(resp + pos + 2, len);
        pos += 2 + len;
    }
    return 1;
}

static void dirThread(void *arg)
{
    NetWs *ws = NULL;
    uint64_t retryAt = 0, lastHostUs = 0, lastPollUs = 0, lastPingUs = 0, idleSince = 0;
    int backoffS = 1, wsFails = 0;
    int usePoll = D.cfg.forcePoll || !netWsAvailable();
    uint8_t buf[NET_WS_MAX_MSG];
    (void)arg;
    netMutexLock(D.mx);
    D.st.polling = usePoll;
    netMutexUnlock(D.mx);

    while (!D.quit) {
        uint64_t now = netTimeUs();
        int unavailable;
        netMutexLock(D.mx);
        unavailable = D.st.state == NDS_UNAVAILABLE;
        netMutexUnlock(D.mx);
        if (unavailable) {
            if (ws) {
                netWsClose(ws);
                ws = NULL;
            }
            netSleepUs(100000);
            continue;
        }

        if (!needed(now)) {
            if (!idleSince) idleSince = now;
            if (now - idleSince > 2000000ull) {   /* brief gaps keep the link */
                if (ws) {
                    netWsClose(ws);
                    ws = NULL;
                    dlog(NETLOG_INFO, "online service: idle, disconnected");
                }
                setState(NDS_OFF);
            }
            netSleepUs(50000);
            continue;
        }
        idleSince = 0;

        if (usePoll) {
            int r;
            if (now < retryAt) {
                netSleepUs(50000);
                continue;
            }
            r = pollRound(now, &lastHostUs, &lastPollUs);
            if (r < 0) {
                setState(NDS_RETRY);
                retryAt = now + (uint64_t)backoffS * 1000000ull;
                if (backoffS < 30) backoffS *= 2;
            } else {
                if (r > 0) {
                    backoffS = 1;
                    setState(NDS_ONLINE);
                }
            }
            netSleepUs(100000);
            continue;
        }

        if (!ws) {
            char url[NET_DIR_URL_MAX + 16], err[128];
            if (now < retryAt) {
                netSleepUs(50000);
                continue;
            }
            setState(NDS_CONNECTING);
            wsUrl(url, sizeof(url));
            ws = netWsConnect(url, CONNECT_TIMEOUT_MS, err, sizeof(err));
            if (!ws) {
                netMutexLock(D.mx);
                setErrorLocked(err);
                netMutexUnlock(D.mx);
                dlog(NETLOG_WARN, "online service: %s", err);
                setState(NDS_RETRY);
                retryAt = netTimeUs() + (uint64_t)backoffS * 1000000ull;
                if (backoffS < 30) backoffS *= 2;
                if (++wsFails >= WS_FAILS_TO_POLL && netHttpAvailable()) {
                    usePoll = 1;   /* perhaps a proxy that does HTTPS but not WebSockets */
                    netMutexLock(D.mx);
                    D.st.polling = 1;
                    netMutexUnlock(D.mx);
                    dlog(NETLOG_INFO, "online service: falling back to HTTPS polling");
                }
                continue;
            }
            backoffS = 1;
            wsFails = 0;
            lastPingUs = netTimeUs();
            {
                uint8_t hb[NDP_MAX_MSG];
                NetW w;
                nwInit(&w, hb, sizeof(hb));
                encodeHello(&w);
                netWsSend(ws, w.buf, w.len);
            }
            netMutexLock(D.mx);
            D.hostDirty = D.hostWanted;   /* re-register after every reconnect */
            netMutexUnlock(D.mx);
            setState(NDS_ONLINE);
            dlog(NETLOG_INFO, "online service: connected");
        }

        /* send */
        {
            static HostOut ho;   /* thread-only */
            OutMsg m;
            int ok = 1;
            for (;;) {
                int got;
                netMutexLock(D.mx);
                got = popLocked(&m);
                netMutexUnlock(D.mx);
                if (!got) break;
                if (netWsSend(ws, m.data, m.len) != 0) ok = 0;
                free(m.data);
                if (!ok) break;
            }
            netMutexLock(D.mx);
            takeHostingLocked(&ho, now, &lastHostUs, HOST_REFRESH_US);
            netMutexUnlock(D.mx);
            if (ok && ho.unhostLen) ok = netWsSend(ws, ho.unhost, ho.unhostLen) == 0;
            if (ok && ho.hostLen) ok = netWsSend(ws, ho.host, ho.hostLen) == 0;
            if (ok && now - lastPingUs >= PING_US) {
                ok = netWsSendText(ws, "ping") == 0;
                lastPingUs = now;
            }
            if (!ok) {
                netWsClose(ws);
                ws = NULL;
                setState(NDS_RETRY);
                retryAt = netTimeUs() + 1000000ull;
                continue;
            }
        }

        /* receive */
        {
            int n = netWsRecv(ws, buf, sizeof(buf), 50);
            if (n < 0) {
                netWsClose(ws);
                ws = NULL;
                setState(NDS_RETRY);
                dlog(NETLOG_WARN, "online service: connection lost");
                retryAt = netTimeUs() + 1000000ull;
                continue;
            }
            while (n > 0) {
                handleMessage(buf, n);
                n = netWsRecv(ws, buf, sizeof(buf), 0);
                if (n < 0) break;
            }
        }
    }
    if (ws) netWsClose(ws);   /* the service drops our lobby on the close */
    if (usePoll) {
        int pending;
        netMutexLock(D.mx);
        pending = D.unhostPending;
        netMutexUnlock(D.mx);
        /* Polling has no connection to close: send the UNHOST, or the lobby
         * stays listed until the service's TTL. Best effort. */
        if (pending) pollRound(netTimeUs(), &lastHostUs, &lastPollUs);
    }
}

/* ------------------------------------------------------------------------ */
/* API                                                                       */
/* ------------------------------------------------------------------------ */

/* https://anything, or http:// to this machine only (the local mock). The
 * player's name and addresses never travel unencrypted to a remote host. */
static int urlAcceptable(const char *url)
{
    static const char *const local[] = { "http://127.0.0.1", "http://localhost", "http://[::1]" };
    size_t i, n;
    if (!strncmp(url, "https://", 8)) return url[8] != 0;
    for (i = 0; i < sizeof(local) / sizeof(local[0]); i++) {
        n = strlen(local[i]);
        if (!strncmp(url, local[i], n) && (url[n] == 0 || url[n] == ':' || url[n] == '/')) return 1;
    }
    return 0;
}

int netDirStart(const NetDirConfig *cfg)
{
    if (D.running) {
        if (strcmp(D.cfg.url, cfg->url) == 0) {
            netMutexLock(D.mx);
            netStrCopy(D.cfg.name, sizeof(D.cfg.name), cfg->name);
            netStrCopy(D.cfg.build, sizeof(D.cfg.build), cfg->build);
            netMutexUnlock(D.mx);
            return 0;
        }
        netDirStop();
    }
    memset(&D, 0, sizeof(D));
    D.cfg = *cfg;
    if (!cfg->url[0]) {
        D.st.state = NDS_UNAVAILABLE;
        netStrCopy(D.st.lastError, sizeof(D.st.lastError), "No online service configured");
    } else if (!urlAcceptable(cfg->url)) {
        D.st.state = NDS_UNAVAILABLE;
        netStrCopy(D.st.lastError, sizeof(D.st.lastError), "The online service address must start with https://");
        dlog(NETLOG_WARN, "online service: refusing a non-https address");
    }
    D.mx = netMutexCreate();
    if (!D.mx) return -1;
    D.quit = 0;
    D.thread = netThreadStart(dirThread, NULL);
    if (!D.thread) {
        netMutexDestroy(D.mx);
        memset(&D, 0, sizeof(D));
        return -1;
    }
    D.running = 1;
    return 0;
}

void netDirStop(void)
{
    if (!D.running) return;
    D.quit = 1;
    netThreadJoin(D.thread);
    clearQueueLocked();
    netMutexDestroy(D.mx);
    memset(&D, 0, sizeof(D));
}

void netDirShutdownForExit(void)
{
    /* Ask the thread to stop but don't wait: it may be inside a blocking
     * connect (up to CONNECT_TIMEOUT_MS). The process is exiting; the OS
     * closes the connection and the service drops our lobby on the close. */
    if (D.running) D.quit = 1;
}

int netDirRunning(void)
{
    return D.running;
}

void netDirSetName(const char *name)
{
    if (!D.running) return;
    netMutexLock(D.mx);
    netStrCopy(D.cfg.name, sizeof(D.cfg.name), name);
    netMutexUnlock(D.mx);
}

void netDirKeepAlive(int seconds)
{
    uint64_t until;
    if (!D.running) return;
    until = netTimeUs() + (uint64_t)(seconds > 0 ? seconds : 0) * 1000000ull;
    netMutexLock(D.mx);
    if (until > D.keepUntilUs) D.keepUntilUs = until;
    netMutexUnlock(D.mx);
}

static void queueSimple(void (*enc)(NetW *))
{
    uint8_t b[16];
    NetW w;
    nwInit(&w, b, sizeof(b));
    enc(&w);
    netMutexLock(D.mx);
    enqueueLocked(&w);
    if (D.keepUntilUs < netTimeUs() + IDLE_GRACE_US) D.keepUntilUs = netTimeUs() + IDLE_GRACE_US;
    netMutexUnlock(D.mx);
}

void netDirRequestList(void)
{
    if (!D.running) return;
    queueSimple(ndpEncList);
}

void netDirHost(const NdpHost *rec)
{
    if (!D.running) return;
    netMutexLock(D.mx);
    {
        uint32_t id = D.host.lobbyId;
        char token[NDP_TOKEN_MAX], code[NET_CODE_LEN + 1];
        int was = D.hostWanted;
        netStrCopy(token, sizeof(token), D.host.token);
        netStrCopy(code, sizeof(code), D.host.code);
        /* only flag a send when something the service shows changed */
        if (!was || memcmp(&D.host.name, &rec->name, sizeof(rec->name)) != 0 || D.host.flags != rec->flags ||
            D.host.maxPlayers != rec->maxPlayers || D.host.state != rec->state ||
            D.host.numPlayers != rec->numPlayers || D.host.scenario != rec->scenario ||
            D.host.stage != rec->stage || D.host.ncand != rec->ncand ||
            memcmp(D.host.cand, rec->cand, sizeof(rec->cand)) != 0 || D.host.nplayers != rec->nplayers ||
            memcmp(D.host.players, rec->players, sizeof(rec->players)) != 0) {
            D.hostDirty = 1;
        }
        D.host = *rec;
        if (was) {   /* keep the identity the service gave us */
            D.host.lobbyId = id;
            netStrCopy(D.host.token, sizeof(D.host.token), token);
            netStrCopy(D.host.code, sizeof(D.host.code), code);
        } else {
            memset(&D.st.hosted, 0, sizeof(D.st.hosted));
        }
        D.hostWanted = 1;
        D.st.hosting = 1;
    }
    netMutexUnlock(D.mx);
}

void netDirUnhost(void)
{
    if (!D.running) return;
    netMutexLock(D.mx);
    if (D.hostWanted && D.host.lobbyId) {
        D.unhost.lobbyId = D.host.lobbyId;
        netStrCopy(D.unhost.token, sizeof(D.unhost.token), D.host.token);
        D.unhostPending = 1;
    }
    D.hostWanted = 0;
    D.hostDirty = 0;
    D.st.hosting = 0;
    memset(&D.host, 0, sizeof(D.host));
    memset(&D.st.hosted, 0, sizeof(D.st.hosted));
    D.jrHead = D.jrTail = 0;
    netMutexUnlock(D.mx);
}

void netDirJoin(uint32_t lobbyId, const char *code, uint32_t nonce, const NetAddr *cands, int n)
{
    uint8_t b[96];
    NetW w;
    NdpJoin j;
    int i;
    if (!D.running) return;
    memset(&j, 0, sizeof(j));
    j.lobbyId = lobbyId;
    netStrCopy(j.code, sizeof(j.code), code ? code : "");
    j.nonce = nonce;
    for (i = 0; i < n && i < NET_MAX_CANDS; i++) j.cand[j.ncand++] = cands[i];
    nwInit(&w, b, sizeof(b));
    ndpEncJoin(&w, &j);
    netMutexLock(D.mx);
    enqueueLocked(&w);
    if (D.keepUntilUs < netTimeUs() + IDLE_GRACE_US) D.keepUntilUs = netTimeUs() + IDLE_GRACE_US;
    netMutexUnlock(D.mx);
}

void netDirQuick(uint32_t nonce, uint32_t excludeId, const NdpPrefs *prefs, const NetAddr *cands, int n)
{
    uint8_t b[64];
    NetW w;
    NdpQuick q;
    int i;
    if (!D.running) return;
    memset(&q, 0, sizeof(q));
    q.nonce = nonce;
    q.excludeId = excludeId;
    if (prefs) {
        q.prefs = *prefs;
        ndpNormalizePrefs(&q.prefs);
    } else {
        ndpPrefsAny(&q.prefs);
    }
    for (i = 0; i < n && i < NET_MAX_CANDS; i++) q.cand[q.ncand++] = cands[i];
    netMutexLock(D.mx);
    /* A quick lobby of our own asking again: the service offers only older
     * ones, so two lone quick hosts merge one way (D410). */
    if (D.hostWanted && (D.host.flags & NDPF_QUICK)) q.lobbyId = D.host.lobbyId;
    nwInit(&w, b, sizeof(b));
    ndpEncQuick(&w, &q);
    enqueueLocked(&w);
    if (D.keepUntilUs < netTimeUs() + IDLE_GRACE_US) D.keepUntilUs = netTimeUs() + IDLE_GRACE_US;
    netMutexUnlock(D.mx);
}

void netDirReportResult(const NdpResult *r)
{
    uint8_t b[256];
    NetW w;
    NdpResult m;
    if (!D.running) return;
    netMutexLock(D.mx);
    if (!D.hostWanted || !D.host.lobbyId) {
        netMutexUnlock(D.mx);
        return;
    }
    m = *r;
    m.lobbyId = D.host.lobbyId;
    netStrCopy(m.token, sizeof(m.token), D.host.token);
    nwInit(&w, b, sizeof(b));
    ndpEncResult(&w, &m);
    enqueueLocked(&w);
    netMutexUnlock(D.mx);
}

int netDirTakeJoinReq(NdpJoinReq *out)
{
    int got = 0;
    if (!D.running) return 0;
    netMutexLock(D.mx);
    if (D.jrHead != D.jrTail) {
        *out = D.jr[D.jrHead];
        D.jrHead = (D.jrHead + 1) % JOINREQ_Q;
        got = 1;
    }
    netMutexUnlock(D.mx);
    return got;
}

void netDirGetStatus(NetDirStatus *out)
{
    if (!D.running) {
        memset(out, 0, sizeof(*out));
        out->state = NDS_UNAVAILABLE;
        return;
    }
    netMutexLock(D.mx);
    *out = D.st;
    netMutexUnlock(D.mx);
}
