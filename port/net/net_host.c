/*
 * net_host.c -- lobby host + lockstep input relay (D409). See net_host.h and
 * docs/dev/NETPLAY-PLAN.md §7 for the protocol.
 */
#include "net_host.h"
#include "net_gamedata.h"
#include "net_plat.h"
#include "net_session.h"
#include "net_wire.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- tunables ---- */
#define SESSION_TIMEOUT_US        15000000ull   /* lobby / lobbyless */
#define MATCH_TIMEOUT_US           8000000ull   /* silent client in a match */
#define INPUT_STALL_TIMEOUT_US    15000000ull   /* connected but sending no input */
#define LOAD_TIMEOUT_US           90000000ull   /* MATCH_START -> everyone loaded */
#define END_TIMEOUT_US            30000000ull   /* abort -> everyone reported end */
#define LOBBY_BROADCAST_MIN_US      100000ull
#define PING_REFRESH_US            2000000ull
#define FRAMES_RESEND_US             12000ull
#define FRAMES_IDLE_US               50000ull
/* Quick-match lobbies (autostart) count down once everyone present is
 * ready (D411): long enough for more searching players to drop in while the
 * game is small, at once when it is full. */
#define AUTOSTART_SECONDS_2               12
#define AUTOSTART_SECONDS_3               8
#define AUTOSTART_SECONDS_FULL            3
#define HASH_RING                        64
#define HASH_INTERVAL                    30   /* must match net_client.c */
#define MAX_BUNDLES_PER_MSG              24
#define MATCH_WAIT_US               250000ull   /* start-barrier status cadence */

enum { NME_FINISHED = 1, NME_ABORTED = 2, NME_LEFT = 3 };
enum { NF_NO_LOBBY = 1, NF_FULL, NF_IN_MATCH, NF_BUILD, NF_NOT_LEADER, NF_INVALID, NF_SERVER_FULL, NF_NOT_READY };

typedef struct HMatch {
    uint32_t id;
    uint8_t n;
    uint8_t delay;
    uint8_t loaded;     /* mask */
    uint8_t ended;      /* mask: reported MATCH_END / left */
    uint8_t go;
    uint8_t stage;      /* resolved (random already picked) */
    uint8_t aborted;
    uint8_t desyncReported;
    uint32_t abortFrame;
    uint64_t startUs;
    uint64_t abortUs;
    uint64_t lastWaitUs;                /* last NM_MATCH_WAIT broadcast */
    int sess[NET_MAX_PLAYERS];          /* -1 = gone */
    uint32_t inNext[NET_MAX_PLAYERS];   /* all records < inNext received */
    uint32_t discFrom[NET_MAX_PLAYERS]; /* neutral from this frame; UINT32_MAX = active */
    uint64_t inProgressUs[NET_MAX_PLAYERS];
    uint8_t inHave[NET_MAX_PLAYERS][NET_RING];
    NetInputRec in[NET_MAX_PLAYERS][NET_RING];
    uint32_t next;                      /* next frame to assemble */
    NetBundle out[NET_RING];
    uint32_t cliAck[NET_MAX_PLAYERS];
    uint64_t lastFramesUs[NET_MAX_PLAYERS];
    uint32_t sentHi[NET_MAX_PLAYERS];
    uint32_t hashFrame[HASH_RING];
    uint32_t hashVal[HASH_RING][NET_MAX_PLAYERS];
    uint8_t hashHave[HASH_RING];
} HMatch;

typedef struct HSession {
    int used;
    int dead;
    NetConn conn;
    uint32_t joinNonce;
    char name[NET_NAME_MAX];
    char buildId[NET_BUILDID_MAX];
    int lobby;      /* -1 */
    int slot;       /* -1 */
    uint64_t lastListUs;
} HSession;

typedef struct HLobby {
    int used;
    NetLobbyState st;
    int member[NET_MAX_PLAYERS];
    char buildId[NET_BUILDID_MAX];
    uint64_t lastBroadcastUs;
    uint64_t lastPingRefreshUs;
    uint64_t countdownStartUs;
    int dirty;
    HMatch *match;
} HLobby;

struct NetHost {
    NetHostConfig cfg;
    NetTransport t;
    HSession *sess;
    int nSess;
    HLobby *lobby;
    int nLobby;
    uint32_t nextLobbyId;
    uint32_t packetsIn, packetsOut;
    uint64_t now;
};

/* ------------------------------------------------------------------------ */

static void hlog(NetHost *h, int level, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    if (!h->cfg.log) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    h->cfg.log(h->cfg.logCtx, level, buf);
}

static int countingSend(void *ctx, const NetAddr *to, const void *data, int len)
{
    NetHost *h = (NetHost *)ctx;
    h->packetsOut++;
    return h->t.send(h->t.ctx, to, data, len);
}

static NetTransport hostTransport(NetHost *h)
{
    NetTransport t;
    t.ctx = h;
    t.send = countingSend;
    t.recv = NULL;
    return t;
}

static void sendRaw(NetHost *h, const NetAddr *to, const uint8_t *buf, int len)
{
    h->packetsOut++;
    h->t.send(h->t.ctx, to, buf, len);
}

static void sendRel(NetHost *h, int si, uint8_t type, const NetW *w)
{
    HSession *s = &h->sess[si];
    if (!s->used || s->dead || w->overflow) return;
    if (netConnSendReliable(&s->conn, type, w->buf, w->len) != 0) {
        hlog(h, NETLOG_WARN, "session %s: reliable window full, dropping", s->name);
        s->dead = 1;
    }
}

static void sendNotice(NetHost *h, int si, const char *text)
{
    uint8_t b[NET_CHAT_MAX + 8];
    NetW w;
    nwInit(&w, b, sizeof(b));
    nwStr(&w, text, NET_CHAT_MAX);
    sendRel(h, si, NM_NOTICE, &w);
}

static void sendFail(NetHost *h, int si, int reason, const char *text)
{
    uint8_t b[NET_CHAT_MAX + 8];
    NetW w;
    nwInit(&w, b, sizeof(b));
    nwU8(&w, (uint8_t)reason);
    nwStr(&w, text, NET_CHAT_MAX);
    sendRel(h, si, NM_FAIL, &w);
}

static void lobbyNotice(NetHost *h, HLobby *L, const char *text)
{
    int i;
    for (i = 0; i < NET_MAX_PLAYERS; i++) {
        if (L->member[i] >= 0) sendNotice(h, L->member[i], text);
    }
}

/* ------------------------------------------------------------------------ */
/* Lobbies                                                                   */
/* ------------------------------------------------------------------------ */

static void makeCode(NetHost *h, char *out)
{
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    int tries, i, j;
    for (tries = 0; tries < 100; tries++) {
        int clash = 0;
        uint8_t rnd[NET_CODE_LEN];
        netRandomBytes(rnd, sizeof(rnd));
        for (i = 0; i < NET_CODE_LEN; i++) out[i] = alphabet[rnd[i] % 32];
        out[NET_CODE_LEN] = 0;
        for (j = 0; j < h->nLobby; j++) {
            if (h->lobby[j].used && strcmp(h->lobby[j].st.code, out) == 0) clash = 1;
        }
        if (!clash) return;
    }
}

static int lobbyCreate(NetHost *h, const char *name, int flags, int maxPlayers, const char *buildId)
{
    int i;
    for (i = 0; i < h->nLobby; i++) {
        if (!h->lobby[i].used) {
            HLobby *L = &h->lobby[i];
            int k;
            memset(L, 0, sizeof(*L));
            L->used = 1;
            for (k = 0; k < NET_MAX_PLAYERS; k++) L->member[k] = -1;
            L->st.lobby_id = ++h->nextLobbyId;
            if (L->st.lobby_id == 0) L->st.lobby_id = ++h->nextLobbyId;
            makeCode(h, L->st.code);
            netStrCopy(L->st.name, NET_LOBBY_NAME_MAX, name);
            netSanitizeText(L->st.name, NET_LOBBY_NAME_MAX, "Lobby");
            L->st.flags = (uint8_t)flags;
            L->st.state = NLS_WAITING;
            L->st.max_players = (uint8_t)((maxPlayers < 2) ? 2 : (maxPlayers > NET_MAX_PLAYERS ? NET_MAX_PLAYERS : maxPlayers));
            ngDefaultSettings(&L->st.settings);
            if (flags & NL_QUICK) {   /* matchmaking: standard rules, random stage */
                L->st.settings.flags |= NS_AUTOSTART;
                L->st.settings.stage = NG_STAGE_RANDOM;
            }
            netStrCopy(L->buildId, NET_BUILDID_MAX, buildId);
            L->dirty = 1;
            return i;
        }
    }
    return -1;
}

static void lobbyFree(NetHost *h, int li)
{
    HLobby *L = &h->lobby[li];
    if (L->match) free(L->match);
    memset(L, 0, sizeof(*L));
    hlog(h, NETLOG_DEBUG, "lobby %d freed", li);
}

static void lobbyFixLeader(HLobby *L)
{
    int i;
    L->st.leader = 0;
    for (i = 0; i < L->st.num_players; i++) {
        if (L->member[i] >= 0) {
            L->st.leader = (uint8_t)i;
            return;
        }
    }
}

static int lobbyMemberCount(const HLobby *L)
{
    int i, n = 0;
    for (i = 0; i < NET_MAX_PLAYERS; i++) {
        if (L->member[i] >= 0) n++;
    }
    return n;
}

/* Add session si to lobby li. Returns slot or -1. */
static int lobbyJoin(NetHost *h, int li, int si)
{
    HLobby *L = &h->lobby[li];
    HSession *s = &h->sess[si];
    int slot, k, used[NG_NUM_CHARACTERS];
    NetPlayerInfo *p;
    if (L->st.state != NLS_WAITING) return -1;
    if (L->st.num_players >= L->st.max_players) return -1;
    slot = L->st.num_players++;
    L->member[slot] = si;
    p = &L->st.players[slot];
    memset(p, 0, sizeof(*p));
    p->used = 1;
    p->connected = 1;
    netStrCopy(p->name, NET_NAME_MAX, s->name);
    p->handicap = NG_DEFAULT_HANDICAP;
    p->control = 0;
    /* First free character, like the N64 char-select default (player i = i). */
    memset(used, 0, sizeof(used));
    for (k = 0; k < NET_MAX_PLAYERS; k++) {
        if (k != slot && L->st.players[k].used && L->st.players[k].character < NG_NUM_CHARACTERS) {
            used[L->st.players[k].character] = 1;
        }
    }
    p->character = 0;
    for (k = 0; k < 12; k++) {
        if (!used[(slot + k) % 12]) {
            p->character = (uint8_t)((slot + k) % 12);
            break;
        }
    }
    if (ngScenarioIsTeam(L->st.settings.scenario)) {
        ngDefaultTeams(L->st.settings.scenario, L->st.players, L->st.num_players);
    }
    s->lobby = li;
    s->slot = slot;
    lobbyFixLeader(L);
    L->dirty = 1;
    L->st.countdown = 0;
    L->countdownStartUs = 0;
    {
        char msg[64];
        netStrFmt(msg, sizeof(msg), "%s joined", s->name);
        lobbyNotice(h, L, msg);
    }
    return slot;
}

/* Remove a player between matches (compacts slots). */
static void lobbyRemoveSlot(NetHost *h, int li, int slot)
{
    HLobby *L = &h->lobby[li];
    int i;
    if (slot < 0 || slot >= L->st.num_players) return;
    for (i = slot; i < L->st.num_players - 1; i++) {
        L->st.players[i] = L->st.players[i + 1];
        L->member[i] = L->member[i + 1];
        if (L->member[i] >= 0) h->sess[L->member[i]].slot = i;
    }
    L->st.num_players--;
    memset(&L->st.players[L->st.num_players], 0, sizeof(NetPlayerInfo));
    L->member[L->st.num_players] = -1;
    if (ngScenarioIsTeam(L->st.settings.scenario)) {
        ngDefaultTeams(L->st.settings.scenario, L->st.players, L->st.num_players);
    }
    lobbyFixLeader(L);
    L->st.countdown = 0;
    L->countdownStartUs = 0;
    L->dirty = 1;
}

static void matchDisconnectSlot(NetHost *h, HLobby *L, int slot, const char *why);

/* Session leaves its lobby (any state). */
static void sessionLeaveLobby(NetHost *h, int si, const char *why)
{
    HSession *s = &h->sess[si];
    int li = s->lobby;
    HLobby *L;
    char msg[96];
    if (li < 0) return;
    L = &h->lobby[li];
    if (L->st.state == NLS_IN_MATCH && L->match && s->slot >= 0) {
        L->member[s->slot] = -1;
        L->st.players[s->slot].connected = 0;
        L->st.players[s->slot].ready = 0;
        matchDisconnectSlot(h, L, s->slot, why);
        lobbyFixLeader(L);
        L->dirty = 1;
    } else {
        lobbyRemoveSlot(h, li, s->slot);
    }
    netStrFmt(msg, sizeof(msg), "%s left (%s)", s->name, why);
    lobbyNotice(h, L, msg);
    s->lobby = -1;
    s->slot = -1;
    if (h->cfg.serverMode && lobbyMemberCount(L) == 0) {
        lobbyFree(h, li);
    }
}

/* ------------------------------------------------------------------------ */
/* Sessions                                                                  */
/* ------------------------------------------------------------------------ */

static int sessionFindByConn(NetHost *h, uint32_t connId, const NetAddr *from)
{
    int i;
    for (i = 0; i < h->nSess; i++) {
        HSession *s = &h->sess[i];
        if (s->used && s->conn.connId == connId && netAddrEq(&s->conn.addr, from)) return i;
    }
    return -1;
}

static int sessionFindByAddr(NetHost *h, const NetAddr *from)
{
    int i;
    for (i = 0; i < h->nSess; i++) {
        if (h->sess[i].used && netAddrEq(&h->sess[i].conn.addr, from)) return i;
    }
    return -1;
}

static void sessionRemove(NetHost *h, int si, const char *why)
{
    HSession *s = &h->sess[si];
    if (!s->used) return;
    hlog(h, NETLOG_INFO, "session %s removed: %s", s->name, why);
    sessionLeaveLobby(h, si, why);
    netConnFree(&s->conn);
    memset(s, 0, sizeof(*s));
    s->lobby = -1;
    s->slot = -1;
}

static uint32_t newConnId(NetHost *h)
{
    for (;;) {
        uint32_t id = netRandom32();
        int i, clash = 0;
        if (id == 0) continue;
        for (i = 0; i < h->nSess; i++) {
            if (h->sess[i].used && h->sess[i].conn.connId == id) clash = 1;
        }
        if (!clash) return id;
    }
}

/* ------------------------------------------------------------------------ */
/* Lobby state broadcast                                                     */
/* ------------------------------------------------------------------------ */

static void lobbyBroadcast(NetHost *h, int li)
{
    HLobby *L = &h->lobby[li];
    int i;
    L->st.revision++;
    for (i = 0; i < NET_MAX_PLAYERS; i++) {
        int si = L->member[i];
        uint8_t b[700];
        NetW w;
        if (si < 0) continue;
        nwInit(&w, b, sizeof(b));
        netEncLobby(&w, &L->st);
        nwU8(&w, (uint8_t)i);
        sendRel(h, si, NM_LOBBY_STATE, &w);
    }
    L->dirty = 0;
    L->lastBroadcastUs = h->now;
}

/* ------------------------------------------------------------------------ */
/* Match                                                                     */
/* ------------------------------------------------------------------------ */

static int autoDelay(NetHost *h, HLobby *L)
{
    float maxRtt = 0.0f;
    int i, d;
    for (i = 0; i < L->st.num_players; i++) {
        int si = L->member[i];
        if (si >= 0 && h->sess[si].conn.rttMs > maxRtt) maxRtt = h->sess[si].conn.rttMs;
    }
    d = (int)(maxRtt / 16.6667f + 0.999f) + 2;
    if (d < 2) d = 2;
    if (d > NET_MAX_DELAY) d = NET_MAX_DELAY;
    return d;
}

static void matchStart(NetHost *h, int li)
{
    HLobby *L = &h->lobby[li];
    HMatch *m;
    NetMatchStart ms;
    int i, f;
    uint8_t buf[700];

    m = (HMatch *)calloc(1, sizeof(*m));
    if (!m) {
        lobbyNotice(h, L, "Server out of memory; match not started");
        return;
    }
    memset(&ms, 0, sizeof(ms));
    m->id = netRandom32() | 1u;
    m->n = L->st.num_players;
    m->delay = L->st.settings.delay ? L->st.settings.delay : (uint8_t)autoDelay(h, L);
    m->startUs = h->now;
    for (i = 0; i < NET_MAX_PLAYERS; i++) {
        m->sess[i] = (i < m->n) ? L->member[i] : -1;
        m->discFrom[i] = 0xFFFFFFFFu;
        m->inProgressUs[i] = h->now;
    }
    /* Frames 0..delay-1 are neutral for everyone by definition (nobody could
     * have sampled them `delay` frames earlier). They enter the contiguous
     * range directly; inHave only ever marks received-but-not-yet-contiguous
     * frames, so it must stay clear here (a stale bit would alias frame
     * f + NET_RING later). */
    for (i = 0; i < m->n; i++) {
        for (f = 0; f < m->delay; f++) {
            memset(&m->in[i][f % NET_RING], 0, sizeof(NetInputRec));
        }
        m->inNext[i] = m->delay;
    }

    ms.match_id = m->id;
    ms.seed_random = netRandom64();
    ms.seed_chrobj = netRandom64();
    ms.num_players = m->n;
    ms.delay = m->delay;
    ms.stage = (uint8_t)ngResolveStage(L->st.settings.stage, m->n, netRandom32());
    m->stage = ms.stage;
    ms.settings = L->st.settings;
    for (i = 0; i < NET_MAX_PLAYERS; i++) ms.players[i] = L->st.players[i];

    L->match = m;
    L->st.state = NLS_IN_MATCH;
    L->st.countdown = 0;
    L->countdownStartUs = 0;
    L->dirty = 1;

    for (i = 0; i < m->n; i++) {
        NetW w;
        if (m->sess[i] < 0) continue;
        nwInit(&w, buf, sizeof(buf));
        netEncMatchStart(&w, &ms);
        nwU8(&w, (uint8_t)i);
        sendRel(h, m->sess[i], NM_MATCH_START, &w);
    }
    hlog(h, NETLOG_INFO, "lobby %u: match %08x started (%d players, delay %d, stage %d)",
         L->st.lobby_id, m->id, m->n, m->delay, ms.stage);
}

static int matchSlotActiveAt(const HMatch *m, int s, uint32_t f)
{
    return s < m->n && f < m->discFrom[s];
}

static void matchDisconnectSlot(NetHost *h, HLobby *L, int slot, const char *why)
{
    HMatch *m = L->match;
    uint32_t from;
    if (!m || slot < 0 || slot >= m->n) return;
    if (m->discFrom[slot] != 0xFFFFFFFFu) return;
    from = m->inNext[slot];
    if (from < m->next) from = m->next;
    m->discFrom[slot] = from;
    m->sess[slot] = -1;
    m->ended |= (uint8_t)(1u << slot);
    hlog(h, NETLOG_INFO, "match %08x: slot %d disconnected from frame %u (%s)", m->id, slot, from, why);
}

static void matchAssemble(HLobby *L)
{
    HMatch *m = L->match;
    if (!m || m->aborted) return;
    for (;;) {
        uint32_t f = m->next;
        uint32_t minAck = 0xFFFFFFFFu;
        int s, ready = 1, active = 0;
        NetBundle *b;
        for (s = 0; s < m->n; s++) {
            if (!matchSlotActiveAt(m, s, f)) continue;
            active = 1;
            if (m->inNext[s] <= f) {
                ready = 0;
                break;
            }
        }
        /* D412: once every slot has finished or left, there is nothing left
         * to assemble -- and no consumer holds the ring back (minAck below),
         * so without this the loop never ended: the host's net thread spun
         * forever at the end of every match in which all players finished
         * (found by netfuzz; slots never become active again). */
        if (!ready || !active) break;
        /* Only peers still consuming bundles hold the ring back; a slot that
         * finished / left keeps its session but no longer acks. */
        for (s = 0; s < m->n; s++) {
            if (m->sess[s] >= 0 && m->discFrom[s] == 0xFFFFFFFFu && m->cliAck[s] < minAck) minAck = m->cliAck[s];
        }
        if (minAck != 0xFFFFFFFFu && f - minAck >= NET_RING - 1) break;   /* would overwrite unacked */
        b = &m->out[f % NET_RING];
        memset(b, 0, sizeof(*b));
        b->frame = f;
        for (s = 0; s < m->n; s++) {
            if (matchSlotActiveAt(m, s, f)) {
                b->present |= (uint8_t)(1u << s);
                b->rec[s] = m->in[s][f % NET_RING];
            } else {
                b->disc |= (uint8_t)(1u << s);
            }
        }
        m->next = f + 1;
    }
}

static void matchFinish(NetHost *h, int li, const char *why)
{
    HLobby *L = &h->lobby[li];
    int i;
    hlog(h, NETLOG_INFO, "lobby %u: match over (%s)", L->st.lobby_id, why);
    if (L->match) {
        free(L->match);
        L->match = NULL;
    }
    L->st.state = NLS_WAITING;
    /* Drop players who disconnected during the match; reset ready flags. */
    for (i = L->st.num_players - 1; i >= 0; i--) {
        if (L->member[i] < 0) lobbyRemoveSlot(h, li, i);
    }
    for (i = 0; i < L->st.num_players; i++) L->st.players[i].ready = 0;
    L->dirty = 1;
    if (h->cfg.serverMode && lobbyMemberCount(L) == 0) lobbyFree(h, li);
}

static void matchAbort(NetHost *h, int li, const char *why)
{
    HLobby *L = &h->lobby[li];
    HMatch *m = L->match;
    int i;
    uint8_t b[64];
    if (!m || m->aborted) return;
    m->aborted = 1;
    m->abortFrame = m->next;
    m->abortUs = h->now;
    for (i = 0; i < m->n; i++) {
        NetW w;
        if (m->sess[i] < 0) continue;
        nwInit(&w, b, sizeof(b));
        nwU32(&w, m->id);
        nwU8(&w, NME_ABORTED);
        nwU32(&w, m->abortFrame);
        sendRel(h, m->sess[i], NM_MATCH_END, &w);
    }
    {
        char msg[96];
        netStrFmt(msg, sizeof(msg), "Match aborted: %s", why);
        lobbyNotice(h, L, msg);
    }
    hlog(h, NETLOG_INFO, "match %08x aborted at frame %u: %s", m->id, m->abortFrame, why);
}

static void matchSendFrames(NetHost *h, HLobby *L, int slot)
{
    HMatch *m = L->match;
    int si = m->sess[slot];
    uint8_t buf[NET_MAX_PACKET];
    NetW w;
    uint32_t from, to, f;
    int countPos, count = 0, s;
    NetUnrel u;
    NetTransport t;
    if (si < 0 || !m->go || m->discFrom[slot] != 0xFFFFFFFFu) return;
    from = m->cliAck[slot];
    to = m->next;
    if (from > to) from = to;
    if (from == to && h->now - m->lastFramesUs[slot] < FRAMES_IDLE_US) return;
    if (from < to && m->sentHi[slot] >= to && h->now - m->lastFramesUs[slot] < FRAMES_RESEND_US) return;

    nwInit(&w, buf, NET_MAX_PACKET - 40);
    nwU32(&w, m->id);
    nwU32(&w, m->inNext[slot]);
    nwU32(&w, m->next);
    nwU8(&w, m->n);
    for (s = 0; s < m->n; s++) {
        uint32_t latest = (m->discFrom[s] != 0xFFFFFFFFu) ? 0xFFFFFFFFu : m->inNext[s];
        nwU32(&w, latest);
    }
    {
        uint8_t disc = 0;
        for (s = 0; s < m->n; s++) {
            if (m->discFrom[s] != 0xFFFFFFFFu) disc |= (uint8_t)(1u << s);
        }
        nwU8(&w, disc);
    }
    nwU32(&w, from);
    countPos = w.len;
    nwU8(&w, 0);
    for (f = from; f < to && count < MAX_BUNDLES_PER_MSG; f++) {
        int before = w.len;
        netEncBundle(&w, &m->out[f % NET_RING]);
        if (w.overflow) {
            w.overflow = 0;
            w.len = before;
            break;
        }
        count++;
    }
    w.buf[countPos] = (uint8_t)count;
    u.type = NM_FRAMES;
    u.data = buf;
    u.len = w.len;
    t = hostTransport(h);
    netConnTransmit(&h->sess[si].conn, &t, h->now, &u, 1, 0);
    m->lastFramesUs[slot] = h->now;
    if (from + (uint32_t)count > m->sentHi[slot]) m->sentHi[slot] = from + (uint32_t)count;
    if (count == (int)(to - from)) m->sentHi[slot] = to;
}

static void matchOnInputs(NetHost *h, HLobby *L, int slot, NetR *r)
{
    HMatch *m = L->match;
    uint32_t mid = nrU32(r);
    uint32_t ack = nrU32(r);
    uint32_t first = nrU32(r);
    int count = nrU8(r);
    int i;
    if (r->err || !m || mid != m->id || slot < 0 || slot >= m->n) return;
    if (m->discFrom[slot] != 0xFFFFFFFFu) return;
    if (ack > m->cliAck[slot] && ack <= m->next) m->cliAck[slot] = ack;
    for (i = 0; i < count; i++) {
        NetInputRec rec;
        uint32_t f = first + (uint32_t)i;
        if (netDecInputRec(r, &rec) != 0) return;
        if (f < m->inNext[slot]) continue;                 /* duplicate */
        if (f >= m->next + NET_RING - 1) break;            /* would overwrite needed slot */
        m->in[slot][f % NET_RING] = rec;
        m->inHave[slot][f % NET_RING] = 1;
    }
    /* advance contiguous */
    while (m->inNext[slot] < m->next + NET_RING - 1 && m->inHave[slot][m->inNext[slot] % NET_RING]) {
        m->inHave[slot][m->inNext[slot] % NET_RING] = 0;   /* consumed into contiguous range */
        m->inNext[slot]++;
        m->inProgressUs[slot] = h->now;
    }
    matchAssemble(L);
}

static void matchOnHash(NetHost *h, HLobby *L, int slot, NetR *r)
{
    HMatch *m = L->match;
    uint32_t mid = nrU32(r);
    uint32_t frame = nrU32(r);
    uint32_t hash = nrU32(r);
    int idx, s, want = 0, have, first = -1, bad = 0;
    if (r->err || !m || mid != m->id || slot < 0 || slot >= m->n) return;
    if (frame % HASH_INTERVAL) return;
    idx = (int)((frame / HASH_INTERVAL) % HASH_RING);
    if (m->hashFrame[idx] != frame || !m->hashHave[idx]) {
        m->hashFrame[idx] = frame;
        m->hashHave[idx] = 0;
    }
    m->hashVal[idx][slot] = hash;
    m->hashHave[idx] |= (uint8_t)(1u << slot);
    for (s = 0; s < m->n; s++) {
        if (m->sess[s] >= 0 && matchSlotActiveAt(m, s, frame)) want |= 1 << s;
    }
    have = m->hashHave[idx];
    if ((have & want) != want) return;
    for (s = 0; s < m->n; s++) {
        if (!(want & (1 << s))) continue;
        if (first < 0) first = s;
        else if (m->hashVal[idx][s] != m->hashVal[idx][first]) bad |= 1 << s;
    }
    if (bad && !m->desyncReported) {
        uint8_t b[32];
        m->desyncReported = 1;
        for (s = 0; s < m->n; s++) {
            NetW w;
            if (m->sess[s] < 0) continue;
            nwInit(&w, b, sizeof(b));
            nwU32(&w, m->id);
            nwU32(&w, frame);
            nwU8(&w, (uint8_t)bad);
            sendRel(h, m->sess[s], NM_DESYNC, &w);
        }
        hlog(h, NETLOG_WARN, "match %08x: DESYNC at frame %u (mask %02x)", m->id, frame, bad);
    }
    m->hashHave[idx] = 0xFF;   /* compared; ignore late duplicates */
    m->hashFrame[idx] = frame;
}

static void matchTick(NetHost *h, int li)
{
    HLobby *L = &h->lobby[li];
    HMatch *m = L->match;
    int s, connected = 0;
    if (!m) return;

    for (s = 0; s < m->n; s++) {
        if (m->sess[s] >= 0) connected++;
    }
    if (connected == 0) {
        matchFinish(h, li, "everyone left");
        return;
    }

    /* Start barrier. */
    if (!m->go) {
        uint8_t want = 0;
        for (s = 0; s < m->n; s++) {
            if (m->sess[s] >= 0) want |= (uint8_t)(1u << s);
        }
        if ((m->loaded & want) == want || h->now - m->startUs > LOAD_TIMEOUT_US) {
            uint8_t b[16];
            for (s = 0; s < m->n; s++) {
                if (m->sess[s] >= 0 && !(m->loaded & (1u << s))) {
                    int si = m->sess[s];
                    matchDisconnectSlot(h, L, s, "did not finish loading");
                    sendFail(h, si, NF_INVALID, "Timed out loading the stage");
                }
            }
            m->go = 1;
            for (s = 0; s < m->n; s++) {
                NetW w;
                if (m->sess[s] < 0) continue;
                nwInit(&w, b, sizeof(b));
                nwU32(&w, m->id);
                sendRel(h, m->sess[s], NM_MATCH_GO, &w);
                m->inProgressUs[s] = h->now;
            }
            hlog(h, NETLOG_INFO, "match %08x: GO", m->id);
            return;
        }
        /* Still loading: tell everyone who is ready (the waiting screen). */
        if (h->now - m->lastWaitUs >= MATCH_WAIT_US) {
            uint8_t wb[8];
            NetW w;
            NetUnrel u;
            NetTransport t = hostTransport(h);
            m->lastWaitUs = h->now;
            nwInit(&w, wb, sizeof(wb));
            nwU32(&w, m->id);
            nwU8(&w, m->loaded);
            nwU8(&w, want);
            u.type = NM_MATCH_WAIT;
            u.data = wb;
            u.len = w.len;
            for (s = 0; s < m->n; s++) {
                if (m->sess[s] >= 0) netConnTransmit(&h->sess[m->sess[s]].conn, &t, h->now, &u, 1, 0);
            }
        }
        return;
    }

    /* A connected peer whose game stopped producing input stalls everyone:
     * cut it over to neutral input after a generous grace period. */
    if (!m->aborted) {
        for (s = 0; s < m->n; s++) {
            if (m->sess[s] < 0 || m->discFrom[s] != 0xFFFFFFFFu) continue;
            if (m->inNext[s] <= m->next && h->now - m->inProgressUs[s] > INPUT_STALL_TIMEOUT_US) {
                int si = m->sess[s];
                char msg[80];
                matchDisconnectSlot(h, L, s, "stopped sending input");
                L->st.players[s].connected = 0;
                L->dirty = 1;
                netStrFmt(msg, sizeof(msg), "%s stopped responding", h->sess[si].name);
                lobbyNotice(h, L, msg);
                sendFail(h, si, NF_INVALID, "Your game stopped responding; you were removed from the match");
            }
        }
        matchAssemble(L);
    }

    for (s = 0; s < m->n; s++) matchSendFrames(h, L, s);

    if (m->aborted && h->now - m->abortUs > END_TIMEOUT_US) {
        matchFinish(h, li, "abort timeout");
        return;
    }
    {
        uint8_t want = 0;
        for (s = 0; s < m->n; s++) {
            if (m->sess[s] >= 0) want |= (uint8_t)(1u << s);
        }
        if ((m->ended & want) == want) matchFinish(h, li, "all players finished");
    }
}

/* ------------------------------------------------------------------------ */
/* Message handling                                                          */
/* ------------------------------------------------------------------------ */

typedef struct MsgCtx {
    NetHost *h;
    int si;
} MsgCtx;

static int isLeader(NetHost *h, int si)
{
    HSession *s = &h->sess[si];
    if (s->lobby < 0) return 0;
    return h->lobby[s->lobby].st.leader == s->slot && s->slot >= 0;
}

static void sendJoined(NetHost *h, int si)
{
    HSession *s = &h->sess[si];
    HLobby *L = &h->lobby[s->lobby];
    uint8_t b[32];
    NetW w;
    nwInit(&w, b, sizeof(b));
    nwU32(&w, L->st.lobby_id);
    nwStr(&w, L->st.code, NET_CODE_LEN);
    nwU8(&w, (uint8_t)s->slot);
    sendRel(h, si, NM_JOINED, &w);
}

static void handleList(NetHost *h, int si)
{
    HSession *s = &h->sess[si];
    uint8_t b[NET_REL_MAXMSG];
    NetW w;
    int i, count = 0, countPos;
    if (h->now - s->lastListUs < 500000ull && s->lastListUs) return;   /* rate limit */
    s->lastListUs = h->now;
    nwInit(&w, b, sizeof(b));
    countPos = w.len;
    nwU8(&w, 0);
    for (i = 0; i < h->nLobby && count < NET_LIST_MAX; i++) {
        HLobby *L = &h->lobby[i];
        NetListEntry e;
        int before;
        if (!L->used || !(L->st.flags & NL_PUBLIC)) continue;
        if (strcmp(L->buildId, s->buildId) != 0) continue;
        memset(&e, 0, sizeof(e));
        e.lobby_id = L->st.lobby_id;
        netStrCopy(e.name, NET_LOBBY_NAME_MAX, L->st.name);
        if (L->st.num_players > 0) netStrCopy(e.leader, NET_NAME_MAX, L->st.players[L->st.leader].name);
        netStrCopy(e.code, NET_CODE_LEN + 1, L->st.code);
        e.num_players = L->st.num_players;
        e.max_players = L->st.max_players;
        e.state = L->st.state;
        e.flags = L->st.flags;
        e.scenario = L->st.settings.scenario;
        e.stage = L->st.settings.stage;
        before = w.len;
        netEncListEntry(&w, &e);
        if (w.overflow) {
            w.overflow = 0;
            w.len = before;
            break;
        }
        count++;
    }
    b[countPos] = (uint8_t)count;
    sendRel(h, si, NM_LIST, &w);
}

static int findLobbyByIdOrCode(NetHost *h, uint32_t id, const char *code)
{
    int i;
    for (i = 0; i < h->nLobby; i++) {
        HLobby *L = &h->lobby[i];
        if (!L->used) continue;
        if (id && L->st.lobby_id == id) return i;
        if (!id && code[0]) {
            /* case-insensitive code match */
            int k, eq = 1;
            for (k = 0; k < NET_CODE_LEN; k++) {
                char a = code[k], c = L->st.code[k];
                if (a >= 'a' && a <= 'z') a = (char)(a - 32);
                if (a != c) { eq = 0; break; }
            }
            if (eq && code[NET_CODE_LEN] == 0) return i;
        }
    }
    return -1;
}

static void tryJoinLobby(NetHost *h, int si, int li)
{
    HSession *s = &h->sess[si];
    HLobby *L;
    if (li < 0) {
        sendFail(h, si, NF_NO_LOBBY, "No such lobby");
        return;
    }
    L = &h->lobby[li];
    if (strcmp(L->buildId, s->buildId) != 0) {
        sendFail(h, si, NF_BUILD, "That lobby runs a different game build");
        return;
    }
    if (L->st.state != NLS_WAITING) {
        sendFail(h, si, NF_IN_MATCH, "That lobby is in a match");
        return;
    }
    if (L->st.num_players >= L->st.max_players) {
        sendFail(h, si, NF_FULL, "That lobby is full");
        return;
    }
    if (lobbyJoin(h, li, si) < 0) {
        sendFail(h, si, NF_FULL, "Could not join");
        return;
    }
    sendJoined(h, si);
}

static void handleMsg(void *vctx, NetConn *c, uint8_t type, NetR *r)
{
    MsgCtx *ctx = (MsgCtx *)vctx;
    NetHost *h = ctx->h;
    int si = ctx->si;
    HSession *s = &h->sess[si];
    HLobby *L = (s->lobby >= 0) ? &h->lobby[s->lobby] : NULL;
    (void)c;
    if (!s->used || s->dead) return;

    switch (type) {
    case NM_INPUTS:
        if (L && L->match) matchOnInputs(h, L, s->slot, r);
        break;

    case NM_PLAYER_SET:
        if (L && L->st.state == NLS_WAITING && s->slot >= 0) {
            NetPlayerInfo *p = &L->st.players[s->slot];
            uint8_t ch = nrU8(r), hc = nrU8(r), ct = nrU8(r), tm = nrU8(r), rd = nrU8(r);
            if (r->err) break;
            if (ch < NG_NUM_CHARACTERS) p->character = ch;
            if (hc < NG_NUM_HANDICAPS) p->handicap = hc;
            if (ct < NG_NUM_CONTROLS) p->control = ct;
            /* Quick-match team games keep the host's balanced teams (D412):
             * random players don't get to stack one side. */
            if (tm <= 1 && !((L->st.flags & NL_QUICK) && ngScenarioIsTeam(L->st.settings.scenario))) p->team = tm;
            p->ready = rd ? 1 : 0;
            L->dirty = 1;
        }
        break;

    case NM_SETTINGS_SET:
        /* Quick-match lobbies keep standard rules (D411): whoever happens to
         * be the leader of a random game does not get to change them. */
        if (L && L->st.state == NLS_WAITING && isLeader(h, si) && !(L->st.flags & NL_QUICK)) {
            NetSettings ns;
            int oldScenario = L->st.settings.scenario;
            if (netDecSettings(r, &ns) != 0) break;
            /* Server lobbies keep their autostart policy. */
            ns.flags = (uint8_t)((ns.flags & ~NS_AUTOSTART) | (L->st.settings.flags & NS_AUTOSTART));
            ngNormalizeSettings(&ns);
            L->st.settings = ns;
            if (ns.scenario != oldScenario && ngScenarioIsTeam(ns.scenario)) {
                ngDefaultTeams(ns.scenario, L->st.players, L->st.num_players);
            }
            L->dirty = 1;
        }
        break;

    case NM_START_REQ:
        if (!L || L->st.state != NLS_WAITING) break;
        if (!isLeader(h, si)) {
            sendFail(h, si, NF_NOT_LEADER, "Only the lobby leader can start the match");
            break;
        }
        {
            char why[96];
            int i, allReady = 1;
            for (i = 0; i < L->st.num_players; i++) {
                if (i != L->st.leader && !L->st.players[i].ready) allReady = 0;
            }
            if (!allReady) {
                sendFail(h, si, NF_NOT_READY, "Not everyone is ready");
                break;
            }
            if (L->st.num_players < 2) {
                sendFail(h, si, NF_INVALID, "Need at least 2 players");
                break;
            }
            if (ngValidateMatch(&L->st.settings, L->st.players, L->st.num_players, why, sizeof(why)) != 0) {
                sendFail(h, si, NF_INVALID, why);
                break;
            }
            L->st.players[L->st.leader].ready = 1;
            matchStart(h, s->lobby);
        }
        break;

    case NM_ABORT_REQ:
        if (L && L->match && isLeader(h, si)) matchAbort(h, s->lobby, "ended by the lobby leader");
        break;

    case NM_MATCH_LOADED:
        if (L && L->match && s->slot >= 0 && s->slot < L->match->n) {
            uint32_t mid = nrU32(r);
            if (!r->err && mid == L->match->id) L->match->loaded |= (uint8_t)(1u << s->slot);
        }
        break;

    case NM_MATCH_END:
        if (L && L->match && s->slot >= 0 && s->slot < L->match->n) {
            uint32_t mid = nrU32(r);
            if (!r->err && mid == L->match->id) {
                HMatch *m = L->match;
                /* A peer that has left the stage produces no more input;
                 * the rest must not wait on it (e.g. a desynced peer that
                 * finished early). */
                matchDisconnectSlot(h, L, s->slot, "finished");
                m->sess[s->slot] = si;   /* still connected to the lobby */
                m->ended |= (uint8_t)(1u << s->slot);
            }
        }
        break;

    case NM_MATCH_LEAVE:
        if (L && L->match && s->slot >= 0 && s->slot < L->match->n) {
            uint32_t mid = nrU32(r);
            if (!r->err && mid == L->match->id) {
                char msg[64];
                matchDisconnectSlot(h, L, s->slot, "left the match");
                L->match->sess[s->slot] = si;
                L->match->ended |= (uint8_t)(1u << s->slot);
                L->st.players[s->slot].connected = 0;
                L->dirty = 1;
                netStrFmt(msg, sizeof(msg), "%s left the match", s->name);
                lobbyNotice(h, L, msg);
            }
        }
        break;

    case NM_HASH:
        if (L && L->match) matchOnHash(h, L, s->slot, r);
        break;

    case NM_CHAT:
        if (L) {
            char text[NET_CHAT_MAX + 1];
            uint8_t b[NET_CHAT_MAX + NET_NAME_MAX + 8];
            int i;
            nrStr(r, text, sizeof(text));
            if (r->err) break;
            netSanitizeText(text, sizeof(text), "");
            if (!text[0]) break;
            for (i = 0; i < NET_MAX_PLAYERS; i++) {
                NetW w;
                if (L->member[i] < 0) continue;
                nwInit(&w, b, sizeof(b));
                nwU8(&w, (uint8_t)s->slot);
                nwStr(&w, s->name, NET_NAME_MAX - 1);
                nwStr(&w, text, NET_CHAT_MAX);
                sendRel(h, L->member[i], NM_CHAT, &w);
            }
        }
        break;

    case NM_LIST_REQ:
        if (h->cfg.serverMode) handleList(h, si);
        break;

    case NM_CREATE:
        if (h->cfg.serverMode && s->lobby < 0) {
            char name[NET_LOBBY_NAME_MAX];
            uint8_t pub, maxp, autostart;
            int li;
            nrStr(r, name, sizeof(name));
            pub = nrU8(r);
            maxp = nrU8(r);
            autostart = nrU8(r);
            if (r->err) break;
            netSanitizeText(name, sizeof(name), "Lobby");
            li = lobbyCreate(h, name, NL_SERVER | (pub ? NL_PUBLIC : 0), maxp, s->buildId);
            if (li < 0) {
                sendFail(h, si, NF_SERVER_FULL, "The server has no free lobbies");
                break;
            }
            if (autostart) h->lobby[li].st.settings.flags |= NS_AUTOSTART;
            if (lobbyJoin(h, li, si) < 0) {
                lobbyFree(h, li);
                sendFail(h, si, NF_INVALID, "Could not create lobby");
                break;
            }
            sendJoined(h, si);
        }
        break;

    case NM_JOIN_CODE:
        if (h->cfg.serverMode && s->lobby < 0) {
            uint32_t id = nrU32(r);
            char code[NET_CODE_LEN + 2];
            nrStr(r, code, sizeof(code));
            if (r->err) break;
            tryJoinLobby(h, si, findLobbyByIdOrCode(h, id, code));
        }
        break;

    case NM_QUICK:
        if (h->cfg.serverMode && s->lobby < 0) {
            int i, best = -1;
            for (i = 0; i < h->nLobby; i++) {
                HLobby *Q = &h->lobby[i];
                if (!Q->used || !(Q->st.flags & NL_PUBLIC) || Q->st.state != NLS_WAITING) continue;
                if (Q->st.num_players >= Q->st.max_players) continue;
                if (strcmp(Q->buildId, s->buildId) != 0) continue;
                if (best < 0 || Q->st.num_players > h->lobby[best].st.num_players ||
                    ((Q->st.flags & NL_QUICK) && !(h->lobby[best].st.flags & NL_QUICK))) {
                    best = i;
                }
            }
            if (best < 0) {
                best = lobbyCreate(h, "Quick Match", NL_SERVER | NL_PUBLIC | NL_QUICK, NET_MAX_PLAYERS, s->buildId);
                if (best < 0) {
                    sendFail(h, si, NF_SERVER_FULL, "The server has no free lobbies");
                    break;
                }
                h->lobby[best].st.settings.stage = NG_STAGE_RANDOM;
            }
            tryJoinLobby(h, si, best);
        }
        break;

    case NM_LEAVE_LOBBY:
        if (s->lobby >= 0) {
            sessionLeaveLobby(h, si, "left");
            if (!h->cfg.serverMode) s->dead = 1;   /* direct host: leaving == disconnect */
        }
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* Packets                                                                   */
/* ------------------------------------------------------------------------ */

static int ipSessionCount(NetHost *h, uint32_t ip)
{
    int i, n = 0;
    for (i = 0; i < h->nSess; i++) {
        if (h->sess[i].used && h->sess[i].conn.addr.ip == ip) n++;
    }
    return n;
}

static void sendReject(NetHost *h, const NetAddr *to, uint32_t nonce, int reason)
{
    uint8_t b[NET_HEADER_SIZE + 8];
    NetW w;
    nwInit(&w, b, sizeof(b));
    netWriteHeader(&w, NP_JOIN_REJECT, 0);
    nwU32(&w, nonce);
    nwU8(&w, (uint8_t)reason);
    netFinishHeader(&w);
    sendRaw(h, to, b, w.len);
}

static void sendAccept(NetHost *h, int si)
{
    HSession *s = &h->sess[si];
    uint8_t b[NET_HEADER_SIZE + 16];
    NetW w;
    nwInit(&w, b, sizeof(b));
    netWriteHeader(&w, NP_JOIN_ACCEPT, 0);
    nwU32(&w, s->joinNonce);
    nwU32(&w, s->conn.connId);
    nwU8(&w, (uint8_t)(s->lobby >= 0 ? 1 : 0));
    nwU8(&w, (uint8_t)(h->cfg.serverMode ? 1 : 0));
    netFinishHeader(&w);
    sendRaw(h, &s->conn.addr, b, w.len);
}

static void handleJoin(NetHost *h, const NetAddr *from, NetR *r, int datagramLen)
{
    uint32_t nonce = nrU32(r);
    uint8_t mode = nrU8(r);
    char name[NET_NAME_MAX];
    char build[NET_BUILDID_MAX];
    int si, i;

    nrStr(r, name, sizeof(name));
    nrStr(r, build, sizeof(build));
    if (r->err) return;
    if (datagramLen < NET_JOIN_PAD) return;   /* anti-amplification: ignore short JOINs */
    netSanitizeText(name, sizeof(name), "Player");

    /* Retransmitted JOIN for an existing session: resend the accept. */
    si = sessionFindByAddr(h, from);
    if (si >= 0) {
        if (h->sess[si].joinNonce == nonce) {
            sendAccept(h, si);
            return;
        }
        sessionRemove(h, si, "rejoined");
    }
    /* The same attempt arriving by a second path: an online joiner sends its
     * JOIN to every address the directory published for us (public + LAN,
     * D410), and on one LAN -- or behind a router that loops our public
     * address back -- more than one gets here, each from a different source
     * address. One session per attempt: the first path wins, and its ACCEPT
     * tells the joiner which of our addresses to use. */
    for (i = 0; i < h->nSess; i++) {
        if (h->sess[i].used && h->sess[i].joinNonce == nonce && !strcmp(h->sess[i].name, name)) return;
    }

    if (mode == NJ_DIRECT && h->cfg.serverMode) {
        sendReject(h, from, nonce, NR_BAD_REQUEST);
        return;
    }
    if (!h->cfg.serverMode) {
        HLobby *L = &h->lobby[0];
        if (strcmp(build, h->cfg.buildId) != 0) {
            sendReject(h, from, nonce, NR_BUILD);
            return;
        }
        if (L->st.state != NLS_WAITING) {
            sendReject(h, from, nonce, NR_IN_MATCH);
            return;
        }
        if (L->st.num_players >= L->st.max_players) {
            sendReject(h, from, nonce, NR_FULL);
            return;
        }
    } else if (ipSessionCount(h, from->ip) >= h->cfg.maxPerIp) {
        sendReject(h, from, nonce, NR_RATE_LIMIT);
        return;
    }

    si = -1;
    for (i = 0; i < h->nSess; i++) {
        if (!h->sess[i].used) { si = i; break; }
    }
    if (si < 0) {
        sendReject(h, from, nonce, NR_SERVER_FULL);
        return;
    }
    {
        HSession *s = &h->sess[si];
        memset(s, 0, sizeof(*s));
        s->used = 1;
        s->lobby = -1;
        s->slot = -1;
        s->joinNonce = nonce;
        netStrCopy(s->name, NET_NAME_MAX, name);
        netStrCopy(s->buildId, NET_BUILDID_MAX, build);
        netConnInit(&s->conn, from, newConnId(h), h->now);
    }
    if (!h->cfg.serverMode) {
        if (lobbyJoin(h, 0, si) < 0) {
            sendReject(h, from, nonce, NR_FULL);
            netConnFree(&h->sess[si].conn);
            h->sess[si].used = 0;
            return;
        }
    }
    {
        char a[32];
        netAddrFormat(from, a, sizeof(a));
        hlog(h, NETLOG_INFO, "%s connected from %s", name, a);
    }
    sendAccept(h, si);
}

static void handleQuery(NetHost *h, const NetAddr *from, NetR *r, int datagramLen)
{
    uint32_t nonce = nrU32(r);
    uint8_t b[NET_HEADER_SIZE + 128];
    NetW w;
    HLobby *L;
    if (r->err || datagramLen < 128 || h->cfg.serverMode) return;   /* reply is smaller: no amplification */
    L = &h->lobby[0];
    nwInit(&w, b, sizeof(b));
    netWriteHeader(&w, NP_QUERY_REPLY, 0);
    nwU32(&w, nonce);
    nwStr(&w, L->st.name, NET_LOBBY_NAME_MAX - 1);
    nwStr(&w, L->st.num_players ? L->st.players[L->st.leader].name : "", NET_NAME_MAX - 1);
    nwU8(&w, L->st.num_players);
    nwU8(&w, L->st.max_players);
    nwU8(&w, L->st.state);
    nwStr(&w, h->cfg.buildId, 40);
    netFinishHeader(&w);
    if (!w.overflow) sendRaw(h, from, b, w.len);
}

void netHostOnPacket(NetHost *h, const NetAddr *from, const uint8_t *data, int len, uint64_t nowUs)
{
    NetR r;
    NetHeader hd;
    h->now = nowUs;
    h->packetsIn++;
    nrInit(&r, data, len);
    if (netReadHeader(&r, &hd) != 0) return;

    switch (hd.type) {
    case NP_JOIN:
        handleJoin(h, from, &r, len);
        break;
    case NP_QUERY:
        handleQuery(h, from, &r, len);
        break;
    case NP_SESSION: {
        int si = sessionFindByConn(h, hd.conn_id, from);
        MsgCtx ctx;
        if (si < 0) return;
        ctx.h = h;
        ctx.si = si;
        if (netConnReceive(&h->sess[si].conn, &r, nowUs, handleMsg, &ctx) != 0) return;
        if (h->sess[si].dead) sessionRemove(h, si, "closed");
        break;
    }
    case NP_DISCONNECT: {
        int si = sessionFindByConn(h, hd.conn_id, from);
        if (si >= 0) sessionRemove(h, si, "disconnected");
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* Tick                                                                      */
/* ------------------------------------------------------------------------ */

static void lobbyTick(NetHost *h, int li)
{
    HLobby *L = &h->lobby[li];
    int i;

    if (L->match) {
        matchTick(h, li);
        if (!L->used) return;
    }

    if (h->now - L->lastPingRefreshUs >= PING_REFRESH_US) {
        L->lastPingRefreshUs = h->now;
        for (i = 0; i < L->st.num_players; i++) {
            int si = L->member[i];
            uint16_t p = (si >= 0) ? (uint16_t)(h->sess[si].conn.rttMs + 0.5f) : 0;
            if (L->st.players[i].ping != p) {
                L->st.players[i].ping = p;
                L->dirty = 1;
            }
        }
    }

    /* Autostart (quick-match lobbies). */
    if (L->st.state == NLS_WAITING && (L->st.settings.flags & NS_AUTOSTART)) {
        int all = L->st.num_players >= 2;
        char why[96];
        for (i = 0; i < L->st.num_players; i++) {
            if (!L->st.players[i].ready) all = 0;
        }
        if (all && ngValidateMatch(&L->st.settings, L->st.players, L->st.num_players, why, sizeof(why)) != 0) all = 0;
        if (all) {
            uint64_t el;
            int left, secs;
            secs = L->st.num_players >= L->st.max_players ? AUTOSTART_SECONDS_FULL
                 : L->st.num_players >= 3                 ? AUTOSTART_SECONDS_3
                                                          : AUTOSTART_SECONDS_2;
            if (!L->countdownStartUs) L->countdownStartUs = h->now;
            el = h->now - L->countdownStartUs;
            left = secs - (int)(el / 1000000ull);
            if (left <= 0) {
                matchStart(h, li);
            } else if (L->st.countdown != (uint8_t)left) {
                L->st.countdown = (uint8_t)left;
                L->dirty = 1;
            }
        } else if (L->countdownStartUs) {
            L->countdownStartUs = 0;
            L->st.countdown = 0;
            L->dirty = 1;
        }
    }

    if (L->dirty && h->now - L->lastBroadcastUs >= LOBBY_BROADCAST_MIN_US) {
        lobbyBroadcast(h, li);
    }
}

void netHostTick(NetHost *h, uint64_t nowUs)
{
    int i;
    NetTransport t = hostTransport(h);
    h->now = nowUs;

    /* Timeouts. */
    for (i = 0; i < h->nSess; i++) {
        HSession *s = &h->sess[i];
        uint64_t limit;
        if (!s->used) continue;
        limit = (s->lobby >= 0 && h->lobby[s->lobby].match) ? MATCH_TIMEOUT_US : SESSION_TIMEOUT_US;
        if (s->dead || s->conn.failed) {
            sessionRemove(h, i, "connection failed");
        } else if (nowUs - s->conn.lastRecvUs > limit) {
            sessionRemove(h, i, "timed out");
        }
    }

    for (i = 0; i < h->nLobby; i++) {
        if (h->lobby[i].used) lobbyTick(h, i);
    }

    /* Flush reliable traffic / acks / pings for every session. FRAMES were
     * already transmitted by matchSendFrames (which also carries acks). */
    for (i = 0; i < h->nSess; i++) {
        HSession *s = &h->sess[i];
        if (!s->used || s->dead) continue;
        netConnTransmit(&s->conn, &t, nowUs, NULL, 0, 0);
    }
}

void netHostPump(NetHost *h, uint64_t nowUs)
{
    uint8_t buf[NET_MAX_PACKET + 64];
    int guard;
    for (guard = 0; guard < 4096; guard++) {
        NetAddr from;
        int n = h->t.recv(h->t.ctx, &from, buf, sizeof(buf));
        if (n <= 0) break;
        if (n > NET_MAX_PACKET) continue;
        netHostOnPacket(h, &from, buf, n, nowUs);
    }
    netHostTick(h, nowUs);
}

NetHost *netHostCreate(const NetHostConfig *cfg, NetTransport t)
{
    NetHost *h = (NetHost *)calloc(1, sizeof(*h));
    int i;
    if (!h) return NULL;
    h->cfg = *cfg;
    h->t = t;
    if (h->cfg.maxSessions <= 0) h->cfg.maxSessions = h->cfg.serverMode ? 512 : 16;
    if (h->cfg.maxLobbies <= 0) h->cfg.maxLobbies = h->cfg.serverMode ? 128 : 1;
    if (h->cfg.maxPerIp <= 0) h->cfg.maxPerIp = 8;
    if (!h->cfg.serverMode) h->cfg.maxLobbies = 1;
    h->nSess = h->cfg.maxSessions;
    h->nLobby = h->cfg.maxLobbies;
    h->sess = (HSession *)calloc((size_t)h->nSess, sizeof(HSession));
    h->lobby = (HLobby *)calloc((size_t)h->nLobby, sizeof(HLobby));
    if (!h->sess || !h->lobby) {
        free(h->sess);
        free(h->lobby);
        free(h);
        return NULL;
    }
    for (i = 0; i < h->nSess; i++) {
        h->sess[i].lobby = -1;
        h->sess[i].slot = -1;
    }
    h->nextLobbyId = netRandom32() & 0x00FFFFFFu;
    if (!h->cfg.serverMode) {
        int maxp = h->cfg.maxPlayers ? h->cfg.maxPlayers : NET_MAX_PLAYERS;
        if (lobbyCreate(h, h->cfg.lobbyName[0] ? h->cfg.lobbyName : "GoldenEye", NL_PUBLIC, maxp, h->cfg.buildId) != 0) {
            netHostDestroy(h);
            return NULL;
        }
    }
    return h;
}

void netHostDestroy(NetHost *h)
{
    int i;
    if (!h) return;
    for (i = 0; i < h->nSess; i++) {
        if (h->sess[i].used) {
            /* Best-effort goodbye so clients don't wait for a timeout. */
            uint8_t b[NET_HEADER_SIZE];
            NetW w;
            nwInit(&w, b, sizeof(b));
            netWriteHeader(&w, NP_DISCONNECT, h->sess[i].conn.connId);
            netFinishHeader(&w);
            sendRaw(h, &h->sess[i].conn.addr, b, w.len);
            netConnFree(&h->sess[i].conn);
        }
    }
    for (i = 0; i < h->nLobby; i++) {
        if (h->lobby[i].match) free(h->lobby[i].match);
    }
    free(h->sess);
    free(h->lobby);
    free(h);
}

void netHostGetStats(NetHost *h, NetHostStats *out)
{
    int i;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < h->nSess; i++) {
        if (h->sess[i].used) out->sessions++;
    }
    for (i = 0; i < h->nLobby; i++) {
        if (h->lobby[i].used) {
            out->lobbies++;
            if (h->lobby[i].match) out->matches++;
        }
    }
    out->packetsIn = h->packetsIn;
    out->packetsOut = h->packetsOut;
}

/* ------------------------------------------------------------------------ */
/* Direct-mode lobby: online-service integration (D410, net_dir.h)           */
/* ------------------------------------------------------------------------ */

void netHostSetDirectInfo(NetHost *h, const char *code, uint8_t lobbyFlags, int autostart)
{
    HLobby *L;
    if (!h || h->cfg.serverMode || h->nLobby < 1 || !h->lobby[0].used) return;
    L = &h->lobby[0];
    if (code) netStrCopy(L->st.code, (int)sizeof(L->st.code), code);
    if ((lobbyFlags & NL_QUICK) && !(L->st.flags & NL_QUICK)) {
        /* becoming a quick-match game: standard rules, random stage (D411) */
        ngDefaultSettings(&L->st.settings);
        L->st.settings.stage = NG_STAGE_RANDOM;
    }
    L->st.flags = (uint8_t)((lobbyFlags & (NL_PUBLIC | NL_QUICK | NL_ONLINE)) | (L->st.flags & NL_SERVER));
    if (autostart) {
        L->st.settings.flags |= NS_AUTOSTART;
    } else {
        L->st.settings.flags &= (uint8_t)~NS_AUTOSTART;
    }
    L->dirty = 1;
}

void netHostSetQuickRules(NetHost *h, const NetSettings *rules, int maxPlayers)
{
    HLobby *L;
    if (!h || !rules || h->cfg.serverMode || h->nLobby < 1 || !h->lobby[0].used) return;
    L = &h->lobby[0];
    if (L->st.state != NLS_WAITING) return;   /* never mid-match */
    L->st.settings = *rules;
    L->st.settings.flags |= NS_AUTOSTART;
    ngNormalizeSettings(&L->st.settings);
    if (maxPlayers > NET_MAX_PLAYERS) maxPlayers = NET_MAX_PLAYERS;
    if (maxPlayers < 2) maxPlayers = 2;
    if (maxPlayers < L->st.num_players) maxPlayers = L->st.num_players;   /* nobody is pushed out */
    L->st.max_players = (uint8_t)maxPlayers;
    if (ngScenarioIsTeam(L->st.settings.scenario)) {
        ngDefaultTeams(L->st.settings.scenario, L->st.players, L->st.num_players);
    }
    L->st.countdown = 0;
    L->countdownStartUs = 0;
    L->dirty = 1;
}

int netHostGetDirectLobby(NetHost *h, NetHostLobbyInfo *out)
{
    HLobby *L;
    memset(out, 0, sizeof(*out));
    if (!h || h->cfg.serverMode || h->nLobby < 1 || !h->lobby[0].used) return 0;
    L = &h->lobby[0];
    out->valid = 1;
    out->lobby = L->st;
    if (L->match) {
        out->inMatch = 1;
        out->matchGo = L->match->go;
        out->matchFrame = L->match->next;
        out->matchElapsedSec = (uint32_t)((h->now - L->match->startUs) / 1000000ull);
        out->matchStage = L->match->stage;
    }
    return 1;
}
