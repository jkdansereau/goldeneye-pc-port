/*
 * netfuzz.c -- robustness fuzzer for the netplay stack (D416).
 *
 * Online play means running code against strangers: a quick-match joiner
 * processes whatever a random host sends, a host whatever random joiners
 * send, and both whatever the directory service -- or anything posing as
 * it -- returns. This drives the real port/net code with:
 *
 *   decoders   every wire decoder (game protocol, directory protocol, STUN)
 *              on random bytes and on valid encodings, mutated;
 *   prefs      quick-match preference rules: normalisation is idempotent, a
 *              searcher's own game matches its preferences and is a legal
 *              GoldenEye setup, labels never overrun;
 *   evil host  a real NetClient joins an "evil host" that completes the
 *              handshake and then sends hostile but correctly framed session
 *              messages (lobby states, match starts, frames, ...), so they
 *              pass the reliable channel and reach every handler, while the
 *              client is driven like the game (takes matches, loads,
 *              submits input, runs frames);
 *   evil joiner a real NetHost (direct, online-quick or server mode) with two
 *              well-behaved clients playing, plus an "evil joiner" doing the
 *              same from the other side, raw garbage from anywhere, and an
 *              on-path attacker tampering with and replaying real packets.
 *
 * Oracles: AddressSanitizer (configure with -DNETPLAY_SANITIZE=ON) for memory
 * errors; checks on everything the game would consume -- input floats finite
 * and bounded (and unchanged by the host's re-encode, or peers desync),
 * counts, slots and ids in range, strings terminated; and that the fuzzing
 * reached the deep states at all (matches started, frames run).
 *
 * Deterministic for a seed: the stack's own randomness is seeded too
 * (netRandomTestSeed).
 *
 *   netfuzz [--iters N] [--seed S] [-v]      exit 0 = no violation
 */
#include "net_client.h"
#include "net_dirproto.h"
#include "net_gamedata.h"
#include "net_host.h"
#include "net_plat.h"
#include "net_proto.h"
#include "net_session.h"
#include "net_sock.h"
#include "net_stun.h"
#include "net_wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUILD_ID "netfuzz-build"

static int g_viol;
static int g_verbose;
static const char *g_ctx = "";

#define FCHECK(cond, ...) do { \
    if (!(cond)) { \
        if (++g_viol <= 30) { \
            printf("  VIOLATION [%s] %s:%d: ", g_ctx, __FILE__, __LINE__); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } \
} while (0)

static void logFn(void *ctx, int level, const char *msg)
{
    if (g_verbose) printf("    [%s] %d %s\n", ctx ? (const char *)ctx : "net", level, msg);
}

/* ======================================================================== */
/* Randomness and mutation                                                   */
/* ======================================================================== */

static uint64_t g_rs = 1;

static uint32_t rnd(void)
{
    g_rs ^= g_rs >> 12;
    g_rs ^= g_rs << 25;
    g_rs ^= g_rs >> 27;
    return (uint32_t)((g_rs * 2685821657736338717ull) >> 32);
}
static uint32_t rndN(uint32_t n) { return n ? rnd() % n : 0; }
static int chance(int pct) { return (int)rndN(100) < pct; }

static const uint32_t kInt[] = {
    0, 1, 2, 3, 4, 5, 7, 8, 11, 12, 13, 15, 16, 30, 31, 32, 33, 63, 64, 100, 127, 128, 200, 254, 255,
    256, 511, 512, 513, 1023, 1024, 1199, 1200, 2047, 2048, 0x7FFF, 0x8000, 0xFFFE, 0xFFFF, 0x10000,
    0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFEu, 0xFFFFFFFFu,
};
#define NKINT ((uint32_t)(sizeof(kInt) / sizeof(kInt[0])))

/* float bit patterns: +-inf, NaNs, +-FLT_MAX, denormal, -0, 1e9, 1e30,
 * 3600.5, 360 */
static const uint32_t kFlt[] = {
    0x7F800000u, 0xFF800000u, 0x7FC00000u, 0xFFFFFFFFu, 0x7F800001u, 0x7F7FFFFFu, 0xFF7FFFFFu,
    0x00000001u, 0x80000000u, 0x4E6E6B28u, 0x7149F2CAu, 0x45610800u, 0x43B40000u,
};
#define NKFLT ((uint32_t)(sizeof(kFlt) / sizeof(kFlt[0])))

static uint32_t rndInteresting(void) { return chance(80) ? kInt[rndN(NKINT)] : rnd(); }

static void rndBytes(void *p, size_t n)
{
    uint8_t *b = (uint8_t *)p;
    while (n--) *b++ = (uint8_t)rnd();
}

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static void be16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

/* 1..4 random edits of buf[0..*len), never past cap. */
static void mutate(uint8_t *buf, int *len, int cap)
{
    int k, edits = 1 + (int)rndN(4);
    for (k = 0; k < edits; k++) {
        int n = *len;
        switch (rndN(11)) {
        case 0:
            if (n) buf[rndN((uint32_t)n)] ^= (uint8_t)(1u << rndN(8));
            break;
        case 1:
            if (n) buf[rndN((uint32_t)n)] = (uint8_t)rnd();
            break;
        case 2:
            if (n) buf[rndN((uint32_t)n)] = (uint8_t)rndInteresting();
            break;
        case 3:
            if (n >= 2) put16(buf + rndN((uint32_t)n - 1), rndInteresting());
            break;
        case 4:
            if (n >= 4) put32(buf + rndN((uint32_t)n - 3), chance(50) ? rndInteresting() : kFlt[rndN(NKFLT)]);
            break;
        case 5:   /* truncate */
            if (n) *len = (int)rndN((uint32_t)n + 1);
            break;
        case 6: { /* append */
            int add = 1 + (int)rndN(24);
            while (add-- > 0 && *len < cap) buf[(*len)++] = (uint8_t)rnd();
            break;
        }
        case 7:   /* insert a byte */
            if (n < cap) {
                int p = (int)rndN((uint32_t)n + 1);
                memmove(buf + p + 1, buf + p, (size_t)(n - p));
                buf[p] = (uint8_t)rndInteresting();
                (*len)++;
            }
            break;
        case 8:   /* delete a range */
            if (n) {
                int p = (int)rndN((uint32_t)n), m = 1 + (int)rndN(8);
                if (m > n - p) m = n - p;
                memmove(buf + p, buf + p + m, (size_t)(n - p - m));
                *len -= m;
            }
            break;
        case 9:   /* copy a chunk elsewhere */
            if (n >= 2) {
                int a = (int)rndN((uint32_t)n), b = (int)rndN((uint32_t)n), m = 1 + (int)rndN(8);
                if (m > n - a) m = n - a;
                if (m > n - b) m = n - b;
                memmove(buf + b, buf + a, (size_t)m);
            }
            break;
        default:  /* nudge a byte by -2..+2 */
            if (n) {
                int p = (int)rndN((uint32_t)n);
                buf[p] = (uint8_t)(buf[p] + (int)rndN(5) - 2);
            }
            break;
        }
    }
}

/* A string for an encoder: printable, any byte, or code-like; always
 * terminated within cap. */
static void rndStr(char *s, int cap)
{
    int n = (int)rndN((uint32_t)cap), i, kind = (int)rndN(3);
    for (i = 0; i < n; i++) {
        if (kind == 0) s[i] = (char)(0x20 + rndN(95));
        else if (kind == 1) s[i] = (char)(1 + rndN(255));
        else s[i] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"[rndN(32)];
    }
    s[n] = 0;
}

static NetAddr rndAddr(void)
{
    NetAddr a;
    a.ip = chance(50) ? rnd() : rndInteresting();
    a.port = (uint16_t)(chance(50) ? rnd() : rndInteresting());
    return a;
}

static float rndFloat(void)
{
    uint32_t b;
    float f;
    switch (rndN(4)) {
    case 0: b = kFlt[rndN(NKFLT)]; break;
    case 1: b = rnd(); break;
    default: return (float)((int)rndN(20001) - 10000) / 100.0f;
    }
    memcpy(&f, &b, sizeof(f));
    return f;
}

/* ======================================================================== */
/* Oracles                                                                   */
/* ======================================================================== */

static int strOk(const char *s, int cap) { return memchr(s, 0, (size_t)cap) != NULL; }

static int printable(const char *s, int cap)
{
    int i;
    for (i = 0; i < cap && s[i]; i++) {
        if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] > 0x7E) return 0;
    }
    return i < cap;
}

static int fIn(float f, float lim)
{
    uint32_t b;
    memcpy(&b, &f, sizeof(b));
    return (b & 0x7F800000u) != 0x7F800000u && f <= lim && f >= -lim;
}

/* What every PC's game applies: finite, within what real input produces. */
static void checkRec(const NetInputRec *r)
{
    FCHECK(!(r->flags & ~(NIR_LOOK | NIR_CROSS | NIR_PDTURN)), "record flags %02x", r->flags);
    FCHECK(fIn(r->look_dtheta, 3600.0f) && fIn(r->look_dverta, 3600.0f), "look %g %g", r->look_dtheta, r->look_dverta);
    FCHECK(fIn(r->cross_x, 8.0f) && fIn(r->cross_y, 8.0f), "crosshair %g %g", r->cross_x, r->cross_y);
    FCHECK(fIn(r->gun_az, 16.0f) && fIn(r->gun_turn, 16.0f), "gun pose %g %g", r->gun_az, r->gun_turn);
    FCHECK(fIn(r->pdturn_x, 4.0f) && fIn(r->pdturn_y, 4.0f), "pd turn %g %g", r->pdturn_x, r->pdturn_y);
}

/* The host decodes a record and re-encodes it into bundles that every peer
 * decodes: if decoding changed a decoded record again, peers would desync. */
static void checkReencode(const NetInputRec *a)
{
    uint8_t b[64];
    NetW w;
    NetR r;
    NetInputRec c;
    nwInit(&w, b, sizeof(b));
    netEncInputRec(&w, a);
    nrInit(&r, b, w.len);
    FCHECK(netDecInputRec(&r, &c) == 0 && netInputRecEq(a, &c), "a decoded record changes when re-encoded");
}

static void checkLobby(const NetLobbyState *l, int decoded)
{
    int i;
    FCHECK(l->max_players >= 2 && l->max_players <= NET_MAX_PLAYERS && l->num_players <= NET_MAX_PLAYERS &&
           l->leader < NET_MAX_PLAYERS, "lobby %d/%d leader %d", l->num_players, l->max_players, l->leader);
    FCHECK(strOk(l->code, NET_CODE_LEN + 1) && strOk(l->name, NET_LOBBY_NAME_MAX), "lobby strings");
    for (i = 0; i < NET_MAX_PLAYERS; i++) {
        FCHECK(strOk(l->players[i].name, NET_NAME_MAX), "player %d name", i);
        if (decoded) FCHECK(printable(l->players[i].name, NET_NAME_MAX) && l->players[i].name[0], "player %d name not sanitised", i);
    }
    if (decoded) FCHECK(printable(l->name, NET_LOBBY_NAME_MAX) && l->name[0], "lobby name not sanitised");
}

static void checkClient(NetClient *c)
{
    static NetClientStatus st;
    int i;
    netClientGetStatus(c, &st);
    FCHECK(st.slot >= -1 && st.slot < NET_MAX_PLAYERS, "client slot %d", st.slot);
    if (st.lobbyValid) checkLobby(&st.lobby, 0);
    FCHECK(st.listCount >= 0 && st.listCount <= NET_LIST_MAX, "list count %d", st.listCount);
    for (i = 0; i < st.listCount && i < NET_LIST_MAX; i++) {
        FCHECK(strOk(st.list[i].name, NET_LOBBY_NAME_MAX) && strOk(st.list[i].leader, NET_NAME_MAX) &&
               strOk(st.list[i].code, NET_CODE_LEN + 1), "list entry %d strings", i);
    }
    FCHECK(strOk(st.lastError, (int)sizeof(st.lastError)), "last error unterminated");
    for (i = 0; i < NET_NOTICE_RING; i++) FCHECK(strOk(st.notice[i], (int)sizeof(st.notice[i])), "notice unterminated");
    FCHECK(st.matchPlayers >= 0 && st.matchPlayers <= NET_MAX_PLAYERS && st.matchDelay >= 0 &&
           st.matchDelay <= NET_MAX_DELAY, "match %d players, delay %d", st.matchPlayers, st.matchDelay);
    if (st.matchPhase != NMP_NONE) {
        FCHECK(st.matchSlot >= 0 && st.matchSlot < (st.matchPlayers ? st.matchPlayers : 1), "match slot %d of %d",
               st.matchSlot, st.matchPlayers);
    }
}

/* ======================================================================== */
/* Random message contents                                                   */
/* ======================================================================== */

static void rndRec(NetInputRec *r)
{
    memset(r, 0, sizeof(*r));
    r->flags = (uint8_t)(rnd() & (NIR_LOOK | NIR_CROSS | NIR_PDTURN));
    r->buttons = (uint16_t)rnd();
    if (chance(70)) {
        r->stick_x = (int8_t)rnd();
        r->stick_y = (int8_t)rnd();
    }
    if (chance(20)) r->actions = (uint8_t)rnd();
    r->look_dtheta = rndFloat();
    r->look_dverta = rndFloat();
    r->cross_x = rndFloat();
    r->cross_y = rndFloat();
    r->gun_az = rndFloat();
    r->gun_turn = rndFloat();
    r->pdturn_x = rndFloat();
    r->pdturn_y = rndFloat();
}

static void rndPlayer(NetPlayerInfo *p)
{
    rndBytes(p, sizeof(*p));
    rndStr(p->name, NET_NAME_MAX);
    if (chance(70)) {
        p->character = (uint8_t)rndN(NG_NUM_CHARACTERS + 1);
        p->handicap = (uint8_t)rndN(NG_NUM_HANDICAPS + 1);
        p->control = (uint8_t)rndN(NG_NUM_CONTROLS + 1);
        p->team = (uint8_t)rndN(3);
    }
}

/* Mostly in range, sometimes anything. */
static void rndSettings(NetSettings *s)
{
    rndBytes(s, sizeof(*s));
    if (chance(70)) {
        s->scenario = (uint8_t)rndN(NG_NUM_SCENARIOS + 1);
        s->stage = (uint8_t)rndN(NG_NUM_STAGES + 1);
        s->length = (uint8_t)rndN(NG_NUM_LENGTHS + 1);
        s->weapons = (uint8_t)rndN(NG_NUM_WEAPONSETS + 1);
        s->aimsight = (uint8_t)rndN(NG_NUM_AIMSIGHT + 1);
        s->delay = (uint8_t)rndN(NET_MAX_DELAY + 2);
    }
}

static void rndLobby(NetLobbyState *l)
{
    int i;
    rndBytes(l, sizeof(*l));
    rndStr(l->code, NET_CODE_LEN + 1);
    rndStr(l->name, NET_LOBBY_NAME_MAX);
    if (chance(70)) {
        l->max_players = (uint8_t)(2 + rndN(3));
        l->num_players = (uint8_t)rndN(l->max_players + 1u);
        l->leader = (uint8_t)rndN(NET_MAX_PLAYERS);
        l->state = (uint8_t)rndN(4);
    }
    for (i = 0; i < NET_MAX_PLAYERS; i++) rndPlayer(&l->players[i]);
    rndSettings(&l->settings);
}

static void rndMatchStart(NetMatchStart *m)
{
    int i;
    rndBytes(m, sizeof(*m));
    if (chance(70)) {
        m->num_players = (uint8_t)(1 + rndN(NET_MAX_PLAYERS));
        m->delay = (uint8_t)rndN(NET_MAX_DELAY + 1);
        m->stage = (uint8_t)rndN(NG_NUM_STAGES + 1);
    }
    rndSettings(&m->settings);
    for (i = 0; i < NET_MAX_PLAYERS; i++) rndPlayer(&m->players[i]);
}

static void rndListEntry(NetListEntry *e)
{
    rndBytes(e, sizeof(*e));
    rndStr(e->name, NET_LOBBY_NAME_MAX);
    rndStr(e->leader, NET_NAME_MAX);
    rndStr(e->code, NET_CODE_LEN + 1);
}

static void rndCands(uint8_t *n, NetAddr *cand)
{
    int i;
    *n = (uint8_t)rndN(NET_MAX_CANDS + 1);
    for (i = 0; i < NET_MAX_CANDS; i++) cand[i] = rndAddr();
}

static void rndPrefs(NdpPrefs *p)
{
    static const uint8_t pick[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 99, NDP_ANY, NDP_ANY, NDP_ANY };
    uint8_t *f[5];
    int i;
    f[0] = &p->scenario; f[1] = &p->stage; f[2] = &p->weapons; f[3] = &p->length; f[4] = &p->players;
    for (i = 0; i < 5; i++) *f[i] = chance(90) ? pick[rndN((uint32_t)sizeof(pick))] : (uint8_t)rnd();
}

/* ======================================================================== */
/* 1. Decoders                                                               */
/* ======================================================================== */

enum {
    D_HEADER, D_INPUT, D_BUNDLE, D_PLAYER, D_SETTINGS, D_LOBBY, D_MSTART, D_LISTENT,
    D_WELCOME, D_LISTED, D_HOSTED, D_JOININFO, D_ERROR, D_QUICKHOST, D_JOINREQ,
    D_HELLO, D_HOST, D_JOIN, D_QUICK, D_RESULT, D_STUN, D_COUNT
};
static const char *const kDecName[D_COUNT] = {
    "game header", "input record", "bundle", "player info", "settings", "lobby state", "match start",
    "list entry", "ndp welcome", "ndp listed", "ndp hosted", "ndp joininfo", "ndp error", "ndp quickhost",
    "ndp joinreq", "ndp hello", "ndp host", "ndp join", "ndp quick", "ndp result", "stun response",
};
static uint32_t g_decTried[D_COUNT], g_decOk[D_COUNT];

/* A random but well-formed encoding for decoder `which`. *skip = bytes the
 * caller's dispatcher consumes first (the directory protocol's type byte). */
static int encodeValid(int which, uint8_t *buf, int cap, int *skip)
{
    NetW w;
    *skip = 0;
    nwInit(&w, buf, cap);
    switch (which) {
    case D_HEADER: {
        int i, n = (int)rndN(64);
        netWriteHeader(&w, (uint8_t)rndN(10), rnd());
        for (i = 0; i < n; i++) nwU8(&w, (uint8_t)rnd());
        netFinishHeader(&w);
        break;
    }
    case D_INPUT: {
        NetInputRec r;
        rndRec(&r);
        netEncInputRec(&w, &r);
        break;
    }
    case D_BUNDLE: {
        NetBundle b;
        int i;
        memset(&b, 0, sizeof(b));
        b.frame = rnd();
        b.present = (uint8_t)(rnd() & 0xF);
        b.disc = (uint8_t)rnd();
        b.flags = (uint8_t)rnd();
        for (i = 0; i < NET_MAX_PLAYERS; i++) rndRec(&b.rec[i]);
        netEncBundle(&w, &b);
        break;
    }
    case D_PLAYER: {
        NetPlayerInfo p;
        rndPlayer(&p);
        netEncPlayerInfo(&w, &p);
        break;
    }
    case D_SETTINGS: {
        NetSettings s;
        rndSettings(&s);
        netEncSettings(&w, &s);
        break;
    }
    case D_LOBBY: {
        NetLobbyState l;
        rndLobby(&l);
        netEncLobby(&w, &l);
        break;
    }
    case D_MSTART: {
        NetMatchStart m;
        rndMatchStart(&m);
        netEncMatchStart(&w, &m);
        break;
    }
    case D_LISTENT: {
        NetListEntry e;
        rndListEntry(&e);
        netEncListEntry(&w, &e);
        break;
    }
    case D_WELCOME: {
        NdpWelcome m;
        rndBytes(&m, sizeof(m));
        rndStr(m.motd, NDP_MOTD_MAX);
        ndpEncWelcome(&w, &m);
        *skip = 1;
        break;
    }
    case D_LISTED: {
        static NdpListed m;
        int i;
        rndBytes(&m, sizeof(m));
        m.n = (uint8_t)rndN(NDP_LIST_MAX + 1);
        for (i = 0; i < NDP_LIST_MAX; i++) {
            rndStr(m.e[i].name, NET_LOBBY_NAME_MAX);
            rndStr(m.e[i].hostName, NET_NAME_MAX);
            rndStr(m.e[i].code, NET_CODE_LEN + 1);
            rndStr(m.e[i].country, NDP_COUNTRY_MAX);
        }
        ndpEncListed(&w, &m);
        *skip = 1;
        break;
    }
    case D_HOSTED: {
        NdpHosted m;
        rndBytes(&m, sizeof(m));
        rndStr(m.token, NDP_TOKEN_MAX);
        rndStr(m.code, NET_CODE_LEN + 1);
        ndpEncHosted(&w, &m);
        *skip = 1;
        break;
    }
    case D_JOININFO: {
        NdpJoinInfo m;
        rndBytes(&m, sizeof(m));
        rndStr(m.name, NET_LOBBY_NAME_MAX);
        rndStr(m.hostName, NET_NAME_MAX);
        rndCands(&m.ncand, m.cand);
        ndpEncJoinInfo(&w, &m);
        *skip = 1;
        break;
    }
    case D_ERROR: {
        NdpError m;
        rndBytes(&m, sizeof(m));
        rndStr(m.text, NET_CHAT_MAX + 1);
        ndpEncError(&w, &m);
        *skip = 1;
        break;
    }
    case D_QUICKHOST:
        ndpEncQuickHost(&w, rnd());
        *skip = 1;
        break;
    case D_JOINREQ: {
        NdpJoinReq m;
        rndBytes(&m, sizeof(m));
        rndStr(m.name, NET_NAME_MAX);
        rndCands(&m.ncand, m.cand);
        ndpEncJoinReq(&w, &m);
        *skip = 1;
        break;
    }
    case D_HELLO: {
        NdpHello m;
        rndBytes(&m, sizeof(m));
        if (chance(70)) m.version = NDP_VERSION;
        rndStr(m.build, NET_BUILDID_MAX);
        rndStr(m.name, NET_NAME_MAX);
        ndpEncHello(&w, &m);
        *skip = 1;
        break;
    }
    case D_HOST: {
        NdpHost m;
        int i;
        rndBytes(&m, sizeof(m));
        rndStr(m.token, NDP_TOKEN_MAX);
        rndStr(m.code, NET_CODE_LEN + 1);
        rndStr(m.name, NET_LOBBY_NAME_MAX);
        rndCands(&m.ncand, m.cand);
        m.nplayers = (uint8_t)rndN(NET_MAX_PLAYERS + 1);
        for (i = 0; i < NET_MAX_PLAYERS; i++) rndStr(m.players[i].name, NET_NAME_MAX);
        if (chance(70)) {
            m.maxPlayers = (uint8_t)(2 + rndN(3));
            m.numPlayers = (uint8_t)rndN(m.maxPlayers + 1u);
        }
        ndpEncHost(&w, &m);
        *skip = 1;
        break;
    }
    case D_JOIN: {
        NdpJoin m;
        rndBytes(&m, sizeof(m));
        rndStr(m.code, NET_CODE_LEN + 1);
        rndCands(&m.ncand, m.cand);
        ndpEncJoin(&w, &m);
        *skip = 1;
        break;
    }
    case D_QUICK: {
        NdpQuick m;
        rndBytes(&m, sizeof(m));
        rndPrefs(&m.prefs);
        rndCands(&m.ncand, m.cand);
        ndpEncQuick(&w, &m);
        *skip = 1;
        break;
    }
    case D_RESULT: {
        NdpResult m;
        int i;
        rndBytes(&m, sizeof(m));
        rndStr(m.token, NDP_TOKEN_MAX);
        m.n = (uint8_t)rndN(NET_MAX_PLAYERS + 1);
        for (i = 0; i < NET_MAX_PLAYERS; i++) rndStr(m.players[i].name, NET_NAME_MAX);
        ndpEncResult(&w, &m);
        *skip = 1;
        break;
    }
    case D_STUN: {
        /* Binding success response; attributes: (XOR-)MAPPED-ADDRESS in
         * every family / length, SOFTWARE, MESSAGE-INTEGRITY, FINGERPRINT,
         * unknown -- STUN is big-endian. */
        static const uint16_t types[] = { 0x0020, 0x0001, 0x8022, 0x8028, 0x0008, 0x1234 };
        int len = 20, k, na = (int)rndN(5);
        memset(buf, 0, 256);
        buf[0] = 0x01;
        buf[1] = 0x01;
        buf[4] = 0x21; buf[5] = 0x12; buf[6] = 0xA4; buf[7] = 0x42;
        rndBytes(buf + 8, 12);
        for (k = 0; k < na; k++) {
            uint16_t t = types[rndN(6)];
            int addr = (t == 0x0020 || t == 0x0001);
            int vlen = addr ? (chance(80) ? 8 : (chance(50) ? 20 : (int)rndN(24))) : (int)rndN(20);
            be16(buf + len, t);
            be16(buf + len + 2, (uint32_t)vlen);
            rndBytes(buf + len + 4, (size_t)vlen);
            if (addr && vlen >= 2) {
                buf[len + 4] = 0;
                buf[len + 5] = (uint8_t)(chance(80) ? 1 : (chance(50) ? 2 : rnd()));
            }
            len += 4 + ((vlen + 3) & ~3);
        }
        be16(buf + 2, (uint32_t)(len - 20));
        return len;
    }
    default:
        break;
    }
    return w.len;
}

static void decodeCheck(int which, const uint8_t *p, int n)
{
    NetR r;
    int ok = 0, i;
    nrInit(&r, p, n);
    g_decTried[which]++;
    switch (which) {
    case D_HEADER: {
        NetHeader h;
        if (netReadHeader(&r, &h) == 0) {
            ok = 1;
            FCHECK((int)h.length == nrLeft(&r), "header length %d, %d left", h.length, nrLeft(&r));
        }
        break;
    }
    case D_INPUT: {
        NetInputRec a;
        if (netDecInputRec(&r, &a) == 0) {
            ok = 1;
            checkRec(&a);
            checkReencode(&a);
        }
        break;
    }
    case D_BUNDLE: {
        NetBundle b;
        if (netDecBundle(&r, &b) == 0) {
            ok = 1;
            for (i = 0; i < NET_MAX_PLAYERS; i++) {
                if (b.present & (1u << i)) {
                    checkRec(&b.rec[i]);
                    checkReencode(&b.rec[i]);
                }
            }
        }
        break;
    }
    case D_PLAYER: {
        NetPlayerInfo pl;
        if (netDecPlayerInfo(&r, &pl) == 0) {
            ok = 1;
            FCHECK(printable(pl.name, NET_NAME_MAX) && pl.name[0], "player name not sanitised");
        }
        break;
    }
    case D_SETTINGS: {
        NetSettings s, t;
        if (netDecSettings(&r, &s) == 0) {
            ok = 1;
            ngNormalizeSettings(&s);
            t = s;
            ngNormalizeSettings(&t);
            FCHECK(!memcmp(&s, &t, sizeof(s)), "settings normalisation not idempotent");
            FCHECK(s.scenario < NG_NUM_SCENARIOS && s.stage < NG_NUM_STAGES && s.length < NG_NUM_LENGTHS &&
                   s.weapons < NG_NUM_WEAPONSETS && s.aimsight < NG_NUM_AIMSIGHT && s.delay <= NET_MAX_DELAY,
                   "normalised settings out of range");
        }
        break;
    }
    case D_LOBBY: {
        NetLobbyState l;
        if (netDecLobby(&r, &l) == 0) {
            ok = 1;
            checkLobby(&l, 1);
        }
        break;
    }
    case D_MSTART: {
        NetMatchStart m;
        if (netDecMatchStart(&r, &m) == 0) {
            ok = 1;
            FCHECK(m.num_players >= 1 && m.num_players <= NET_MAX_PLAYERS && m.delay <= NET_MAX_DELAY,
                   "match start %d players delay %d", m.num_players, m.delay);
            for (i = 0; i < NET_MAX_PLAYERS; i++) FCHECK(printable(m.players[i].name, NET_NAME_MAX), "match player name");
        }
        break;
    }
    case D_LISTENT: {
        NetListEntry e;
        if (netDecListEntry(&r, &e) == 0) {
            ok = 1;
            FCHECK(printable(e.name, NET_LOBBY_NAME_MAX) && printable(e.leader, NET_NAME_MAX) &&
                   strOk(e.code, NET_CODE_LEN + 1), "list entry strings");
        }
        break;
    }
    case D_WELCOME: {
        NdpWelcome m;
        if (ndpDecWelcome(&r, &m) == 0) {
            ok = 1;
            FCHECK(strOk(m.motd, NDP_MOTD_MAX), "motd");
        }
        break;
    }
    case D_LISTED: {
        static NdpListed m;
        if (ndpDecListed(&r, &m) == 0) {
            ok = 1;
            FCHECK(m.n <= NDP_LIST_MAX, "listed n %d", m.n);
            for (i = 0; i < m.n && i < NDP_LIST_MAX; i++) {
                FCHECK(strOk(m.e[i].name, NET_LOBBY_NAME_MAX) && strOk(m.e[i].hostName, NET_NAME_MAX) &&
                       strOk(m.e[i].code, NET_CODE_LEN + 1) && strOk(m.e[i].country, NDP_COUNTRY_MAX), "listed strings");
            }
        }
        break;
    }
    case D_HOSTED: {
        NdpHosted m;
        if (ndpDecHosted(&r, &m) == 0) {
            ok = 1;
            FCHECK(strOk(m.token, NDP_TOKEN_MAX) && strOk(m.code, NET_CODE_LEN + 1), "hosted strings");
        }
        break;
    }
    case D_JOININFO: {
        NdpJoinInfo m;
        if (ndpDecJoinInfo(&r, &m) == 0) {
            ok = 1;
            FCHECK(m.ncand <= NET_MAX_CANDS && strOk(m.name, NET_LOBBY_NAME_MAX) && strOk(m.hostName, NET_NAME_MAX),
                   "joininfo %d cands", m.ncand);
        }
        break;
    }
    case D_ERROR: {
        NdpError m;
        if (ndpDecError(&r, &m) == 0) {
            ok = 1;
            FCHECK(strOk(m.text, NET_CHAT_MAX + 1), "error text");
        }
        break;
    }
    case D_QUICKHOST: {
        uint32_t nonce;
        if (ndpDecQuickHost(&r, &nonce) == 0) ok = 1;
        break;
    }
    case D_JOINREQ: {
        NdpJoinReq m;
        if (ndpDecJoinReq(&r, &m) == 0) {
            ok = 1;
            FCHECK(m.ncand <= NET_MAX_CANDS && strOk(m.name, NET_NAME_MAX), "joinreq %d cands", m.ncand);
        }
        break;
    }
    case D_HELLO: {
        NdpHello m;
        if (ndpDecHello(&r, &m) == 0) {
            ok = 1;
            FCHECK(strOk(m.build, NET_BUILDID_MAX) && strOk(m.name, NET_NAME_MAX), "hello strings");
        }
        break;
    }
    case D_HOST: {
        NdpHost m;
        if (ndpDecHost(&r, &m) == 0) {
            ok = 1;
            FCHECK(m.ncand <= NET_MAX_CANDS && m.nplayers <= NET_MAX_PLAYERS, "host %d cands %d players", m.ncand, m.nplayers);
            FCHECK(strOk(m.token, NDP_TOKEN_MAX) && strOk(m.code, NET_CODE_LEN + 1) && strOk(m.name, NET_LOBBY_NAME_MAX),
                   "host strings");
            for (i = 0; i < m.nplayers && i < NET_MAX_PLAYERS; i++) FCHECK(strOk(m.players[i].name, NET_NAME_MAX), "host player");
        }
        break;
    }
    case D_JOIN: {
        NdpJoin m;
        if (ndpDecJoin(&r, &m) == 0) {
            ok = 1;
            FCHECK(m.ncand <= NET_MAX_CANDS && strOk(m.code, NET_CODE_LEN + 1), "join %d cands", m.ncand);
        }
        break;
    }
    case D_QUICK: {
        NdpQuick m;
        if (ndpDecQuick(&r, &m) == 0) {
            ok = 1;
            FCHECK(m.ncand <= NET_MAX_CANDS, "quick %d cands", m.ncand);
        }
        break;
    }
    case D_RESULT: {
        NdpResult m;
        if (ndpDecResult(&r, &m) == 0) {
            ok = 1;
            FCHECK(m.n <= NET_MAX_PLAYERS && strOk(m.token, NDP_TOKEN_MAX), "result %d players", m.n);
            for (i = 0; i < m.n && i < NET_MAX_PLAYERS; i++) FCHECK(strOk(m.players[i].name, NET_NAME_MAX), "result player");
        }
        break;
    }
    case D_STUN: {
        NetStunTx tx;
        NetAddr a;
        if (n >= 20 && chance(85)) memcpy(tx.id, p + 8, 12);
        else rndBytes(tx.id, sizeof(tx.id));
        (void)netStunIsMessage(p, n);
        if (netStunParseResponse(p, n, &tx, &a) == 0) ok = 1;
        break;
    }
    default:
        break;
    }
    if (ok) g_decOk[which]++;
}

static void fuzzDecoders(int iters)
{
    static uint8_t buf[4096];
    int i, which;
    g_ctx = "decoders";
    for (i = 0; i < iters; i++) {
        int n, skip = 0;
        which = (int)rndN(D_COUNT);
        if (chance(15)) {
            n = (int)rndN(400);
            rndBytes(buf, (size_t)n);
        } else {
            n = encodeValid(which, buf, (int)sizeof(buf), &skip);
            if (chance(85)) {
                int m = n - skip;
                mutate(buf + skip, &m, (int)sizeof(buf) - skip);
                n = skip + m;
            }
        }
        decodeCheck(which, buf + skip, n - skip);
    }
    printf("  decoders: %d inputs\n", iters);
    for (which = 0; which < D_COUNT; which++) {
        printf("    %-15s %7u tried %7u accepted\n", kDecName[which], g_decTried[which], g_decOk[which]);
        FCHECK(g_decOk[which] > 0, "%s never accepted anything: the fuzzing is not reaching it", kDecName[which]);
    }
}

/* ======================================================================== */
/* 2. Quick-match preferences                                                */
/* ======================================================================== */

static void fuzzPrefs(int iters)
{
    int i;
    g_ctx = "prefs";
    for (i = 0; i < iters; i++) {
        NdpPrefs p, q;
        NetSettings rules;
        NetPlayerInfo pl[NET_MAX_PLAYERS];
        char why[96], label[128];
        int maxp, cap, k, stage;
        rndPrefs(&p);
        ndpNormalizePrefs(&p);
        q = p;
        ndpNormalizePrefs(&q);
        FCHECK(!memcmp(&p, &q, sizeof(p)), "prefs normalisation not idempotent");
        ndpPrefsToRules(&p, &rules, &maxp);
        FCHECK(maxp >= 2 && maxp <= NET_MAX_PLAYERS, "quick game size %d", maxp);
        /* the searcher's own game must match its preferences (the service
         * offers it to others by the same rule) ... */
        FCHECK(ndpPrefsMatch(&p, rules.scenario, rules.stage, rules.weapons, rules.length, maxp),
               "a searcher's game (%d %d %d %d, %d) does not match its own preferences (%d %d %d %d %d)",
               rules.scenario, rules.stage, rules.weapons, rules.length, maxp,
               p.scenario, p.stage, p.weapons, p.length, p.players);
        /* ... and a full game with those rules must be a legal GoldenEye setup */
        memset(pl, 0, sizeof(pl));
        for (k = 0; k < NET_MAX_PLAYERS; k++) pl[k].handicap = 5;
        ngDefaultTeams(rules.scenario, pl, maxp);
        stage = ngResolveStage(rules.stage, maxp, rnd());
        {
            NetSettings s = rules;
            s.stage = (uint8_t)stage;
            FCHECK(ngValidateMatch(&s, pl, maxp, why, sizeof(why)) == 0, "quick rules are not a legal game: %s", why);
            /* a quick game can start before it is full: 2 players (or the
             * fixed team size) must be legal too */
            if (!ngScenarioIsTeam(rules.scenario)) {
                FCHECK(ngValidateMatch(&s, pl, 2, why, sizeof(why)) == 0, "a 2-player quick game is illegal: %s", why);
            }
        }
        /* labels: any size, never past the buffer */
        cap = 1 + (int)rndN(100);
        memset(label, 0x5A, sizeof(label));
        ndpPrefsLabel(&p, label, cap);
        FCHECK(memchr(label, 0, (size_t)cap) != NULL, "label unterminated (cap %d)", cap);
        for (k = cap; k < (int)sizeof(label); k++) {
            if (label[k] != 0x5A) {
                FCHECK(0, "label wrote past its buffer (cap %d)", cap);
                break;
            }
        }
    }
    printf("  prefs: %d preference sets\n", iters);
}

/* ======================================================================== */
/* In-memory network                                                         */
/* ======================================================================== */

#define QMAX 4096

typedef struct QPkt {
    NetAddr from, to;
    int len;
    uint8_t data[NET_MAX_PACKET];
} QPkt;

static QPkt *g_qbuf[2];
static int g_qcount[2];
static int g_qcur;      /* sends land in g_qbuf[g_qcur] */
static uint64_t g_now;
static int g_tamper;    /* % of delivered packets an on-path attacker mutates */
static QPkt g_replay[32];
static int g_nreplay;
static uint64_t g_pkts;    /* datagrams sent, all episodes */

static void qPush(const NetAddr *from, const NetAddr *to, const void *data, int len)
{
    QPkt *p;
    if (len < 0 || len > NET_MAX_PACKET || g_qcount[g_qcur] >= QMAX) return;
    p = &g_qbuf[g_qcur][g_qcount[g_qcur]++];
    g_pkts++;
    p->from = *from;
    p->to = *to;
    p->len = len;
    memcpy(p->data, data, (size_t)len);
}

static int qSend(void *ctx, const NetAddr *to, const void *data, int len)
{
    qPush((const NetAddr *)ctx, to, data, len);
    return len;
}

static int qRecv(void *ctx, NetAddr *from, void *buf, int cap)
{
    (void)ctx;
    (void)from;
    (void)buf;
    (void)cap;
    return 0;   /* everything is delivered through *OnPacket */
}

static NetTransport qTransport(NetAddr *self)
{
    NetTransport t;
    t.ctx = self;
    t.send = qSend;
    t.recv = qRecv;
    return t;
}

/* ---- evil peers ---- */

typedef struct Evil {
    NetAddr self, peer;
    int role;                /* 0 = joiner (talks to a real host), 1 = host (to a real client) */
    int serverMode;
    int connValid;
    NetConn conn;
    uint32_t joinNonce;
    uint64_t lastJoinUs;
    int rate;                /* % per step: a crafted message */
    /* learned from the peer */
    uint32_t matchId, lobbyId;
    char code[NET_CODE_LEN + 2];
    int n;
    uint32_t frame;          /* joiner: next input frame it sends; host: next bundle frame */
    uint32_t peerAck;
    int loaded;              /* host: the client loaded our match (then GO + frames) */
    int goSent;
} Evil;

static void evilOnMsg(void *ctx, NetConn *c, uint8_t type, NetR *r)
{
    Evil *e = (Evil *)ctx;
    (void)c;
    if (e->role == 0) {
        switch (type) {
        case NM_JOINED:
            e->lobbyId = nrU32(r);
            nrStr(r, e->code, sizeof(e->code));
            break;
        case NM_LOBBY_STATE: {
            NetLobbyState l;
            if (netDecLobby(r, &l) == 0) {
                e->lobbyId = l.lobby_id;
                netStrCopy(e->code, sizeof(e->code), l.code);
            }
            break;
        }
        case NM_MATCH_START: {
            NetMatchStart ms;
            if (netDecMatchStart(r, &ms) == 0) {
                e->matchId = ms.match_id;
                e->n = ms.num_players;
                e->frame = ms.delay;
            }
            break;
        }
        case NM_LIST: {
            int k = nrU8(r);
            NetListEntry le;
            if (k > 0 && k <= NET_LIST_MAX && netDecListEntry(r, &le) == 0) {
                e->lobbyId = le.lobby_id;
                netStrCopy(e->code, sizeof(e->code), le.code);
            }
            break;
        }
        case NM_FRAMES:
            (void)nrU32(r);
            e->peerAck = nrU32(r);
            break;
        default:
            break;
        }
    } else {
        if (type == NM_INPUTS) {
            uint32_t mid = nrU32(r);
            uint32_t ack = nrU32(r);   /* the client's bundle ack */
            if (!r->err && mid == e->matchId) e->peerAck = ack;
        } else if (type == NM_MATCH_LOADED) {
            if (nrU32(r) == e->matchId && !r->err) e->loaded = 1;
        }
    }
}

static void evilInit(Evil *e, NetAddr self, NetAddr peer, int role, int serverMode)
{
    memset(e, 0, sizeof(*e));
    e->self = self;
    e->peer = peer;
    e->role = role;
    e->serverMode = serverMode;
    e->rate = 5 + (int)rndN(55);
}

static void evilFree(Evil *e)
{
    if (e->connValid) netConnFree(&e->conn);
    e->connValid = 0;
}

static void evilConnect(Evil *e, uint32_t connId)
{
    evilFree(e);
    netConnInit(&e->conn, &e->peer, connId, g_now);
    e->connValid = 1;
    e->matchId = 0;
    e->frame = 0;
    e->n = 0;
    e->peerAck = 0;
    e->loaded = 0;
    e->goSent = 0;
}

/* Joiner: a JOIN, mostly well-formed and padded (short ones test the
 * anti-amplification rule). */
static void evilSendJoin(Evil *e)
{
    uint8_t b[NET_JOIN_PAD + 64];
    char name[NET_NAME_MAX];
    NetW w;
    int pad = chance(95) ? NET_JOIN_PAD : (int)rndN(NET_JOIN_PAD);
    nwInit(&w, b, sizeof(b));
    netWriteHeader(&w, NP_JOIN, chance(90) ? 0 : rnd());
    e->joinNonce = rnd() | 1u;
    nwU32(&w, e->joinNonce);
    nwU8(&w, (uint8_t)(chance(90) ? (e->serverMode ? NJ_SERVER : NJ_DIRECT) : rnd()));
    rndStr(name, sizeof(name));
    nwStr(&w, name, NET_NAME_MAX - 1);
    nwStr(&w, chance(92) ? BUILD_ID : "another-build", NET_BUILDID_MAX - 1);
    while (w.len < pad) nwU8(&w, (uint8_t)(chance(95) ? 0 : rnd()));
    netFinishHeader(&w);
    qPush(&e->self, &e->peer, b, w.len);
    e->lastJoinUs = g_now;
}

/* Host: answer a JOIN -- mostly an ACCEPT, sometimes a REJECT, a wrong nonce
 * or connection id 0. */
static void evilAnswerJoin(Evil *e)
{
    uint8_t b[64];
    NetW w;
    uint32_t connId;
    nwInit(&w, b, sizeof(b));
    if (chance(6)) {
        netWriteHeader(&w, NP_JOIN_REJECT, 0);
        nwU32(&w, chance(80) ? e->joinNonce : rnd());
        nwU8(&w, (uint8_t)rnd());
        netFinishHeader(&w);
        qPush(&e->self, &e->peer, b, w.len);
        return;
    }
    connId = chance(96) ? (rnd() | 1u) : 0;
    netWriteHeader(&w, NP_JOIN_ACCEPT, chance(90) ? 0 : rnd());
    nwU32(&w, chance(95) ? e->joinNonce : rnd());
    nwU32(&w, connId);
    nwU8(&w, (uint8_t)(rnd() & 1));
    nwU8(&w, (uint8_t)(chance(90) ? (rnd() & 1) : rnd()));
    netFinishHeader(&w);
    qPush(&e->self, &e->peer, b, w.len);
    if (connId) evilConnect(e, connId);
}

static void evilOnPacket(Evil *e, const QPkt *p)
{
    NetR r;
    NetHeader hd;
    nrInit(&r, p->data, p->len);
    if (netReadHeader(&r, &hd) != 0) return;
    switch (hd.type) {
    case NP_JOIN_ACCEPT:
        if (e->role == 0) {
            uint32_t nonce = nrU32(&r), connId = nrU32(&r);
            if (!r.err && nonce == e->joinNonce && connId) evilConnect(e, connId);
        }
        break;
    case NP_JOIN:
        if (e->role == 1) {
            e->joinNonce = nrU32(&r);
            if (!r.err) evilAnswerJoin(e);
        }
        break;
    case NP_SESSION:
        if (e->connValid && hd.conn_id == e->conn.connId) netConnReceive(&e->conn, &r, g_now, evilOnMsg, e);
        break;
    case NP_DISCONNECT:
        if (e->connValid && hd.conn_id == e->conn.connId) evilFree(e);
        break;
    default:
        break;
    }
}

/* Joiner -> host: every request a client can make, with hostile values. */
static int craftClientMsg(Evil *e, uint8_t *type, uint8_t *b, int cap)
{
    NetW w;
    nwInit(&w, b, cap);
    switch (rndN(17)) {
    case 0:
    case 1:
        nwU8(&w, (uint8_t)(chance(70) ? rndN(NG_NUM_CHARACTERS + 2) : rnd()));
        nwU8(&w, (uint8_t)(chance(70) ? rndN(NG_NUM_HANDICAPS + 2) : rnd()));
        nwU8(&w, (uint8_t)(chance(70) ? rndN(NG_NUM_CONTROLS + 2) : rnd()));
        nwU8(&w, (uint8_t)(chance(70) ? rndN(3) : rnd()));
        nwU8(&w, (uint8_t)(chance(70) ? 1 : rnd()));
        *type = NM_PLAYER_SET;
        break;
    case 2: {
        NetSettings s;
        rndSettings(&s);
        netEncSettings(&w, &s);
        *type = NM_SETTINGS_SET;
        break;
    }
    case 3:
        *type = NM_START_REQ;
        break;
    case 4:
        *type = NM_ABORT_REQ;
        break;
    case 5:
        nwU32(&w, chance(85) ? e->matchId : rnd());
        *type = NM_MATCH_LOADED;
        break;
    case 6:
        nwU32(&w, chance(85) ? e->matchId : rnd());
        nwU8(&w, (uint8_t)rnd());
        *type = NM_MATCH_END;
        break;
    case 7:
        nwU32(&w, chance(85) ? e->matchId : rnd());
        *type = NM_MATCH_LEAVE;
        break;
    case 8:
        nwU32(&w, chance(85) ? e->matchId : rnd());
        nwU32(&w, chance(70) ? 30u * rndN(200) : rnd());
        nwU32(&w, rnd());
        *type = NM_HASH;
        break;
    case 9: {
        char t[NET_CHAT_MAX + 1];
        rndStr(t, sizeof(t));
        nwStr(&w, t, NET_CHAT_MAX);
        *type = NM_CHAT;
        break;
    }
    case 10:
        *type = NM_LIST_REQ;
        break;
    case 11: {
        char nm[NET_LOBBY_NAME_MAX];
        rndStr(nm, sizeof(nm));
        nwStr(&w, nm, NET_LOBBY_NAME_MAX - 1);
        nwU8(&w, (uint8_t)rnd());
        nwU8(&w, (uint8_t)(chance(60) ? rndN(6) : rnd()));
        nwU8(&w, (uint8_t)rnd());
        *type = NM_CREATE;
        break;
    }
    case 12: {
        char code[NET_CODE_LEN + 2];
        nwU32(&w, chance(40) ? e->lobbyId : (chance(50) ? 0 : rnd()));
        if (chance(50) && e->code[0]) netStrCopy(code, sizeof(code), e->code);
        else rndStr(code, sizeof(code));
        nwStr(&w, code, NET_CODE_LEN + 1);
        *type = NM_JOIN_CODE;
        break;
    }
    case 13:
        *type = NM_QUICK;
        break;
    case 14:
        if (!chance(20)) return -1;   /* leaving drops the session: not too often */
        *type = NM_LEAVE_LOBBY;
        break;
    default: {
        int k, n = (int)rndN(64);
        for (k = 0; k < n; k++) nwU8(&w, (uint8_t)rnd());
        *type = (uint8_t)(32 + rndN(224));
        break;
    }
    }
    if (chance(10)) {
        int k, n = 1 + (int)rndN(16);
        for (k = 0; k < n; k++) nwU8(&w, (uint8_t)rnd());   /* trailing junk */
    }
    return w.len;
}

/* Host -> client: everything a host sends, with hostile values. */
static int craftHostMsg(Evil *e, uint8_t *type, uint8_t *b, int cap)
{
    NetW w;
    nwInit(&w, b, cap);
    switch (rndN(13)) {
    case 0:
    case 1: {
        NetLobbyState l;
        rndLobby(&l);
        if (chance(60)) l.lobby_id = e->lobbyId ? e->lobbyId : (e->lobbyId = rnd());
        netEncLobby(&w, &l);
        nwU8(&w, (uint8_t)(chance(80) ? rndN(NET_MAX_PLAYERS) : rnd()));
        *type = NM_LOBBY_STATE;
        break;
    }
    case 2: {
        char code[NET_CODE_LEN + 2];
        nwU32(&w, rnd());
        rndStr(code, sizeof(code));
        nwStr(&w, code, NET_CODE_LEN + 1);
        nwU8(&w, (uint8_t)(chance(80) ? rndN(NET_MAX_PLAYERS) : rnd()));
        *type = NM_JOINED;
        break;
    }
    case 3: {
        char t[NET_CHAT_MAX + 1];
        nwU8(&w, (uint8_t)rnd());
        rndStr(t, sizeof(t));
        nwStr(&w, t, NET_CHAT_MAX);
        *type = NM_FAIL;
        break;
    }
    case 4: {
        int k, n = (int)rndN(NET_LIST_MAX + 3);
        nwU8(&w, (uint8_t)n);
        for (k = 0; k < n; k++) {
            NetListEntry le;
            rndListEntry(&le);
            netEncListEntry(&w, &le);
        }
        *type = NM_LIST;
        break;
    }
    case 5: {
        char t[NET_CHAT_MAX + 1];
        rndStr(t, sizeof(t));
        nwStr(&w, t, NET_CHAT_MAX);
        *type = NM_NOTICE;
        break;
    }
    case 6: {
        char nm[NET_NAME_MAX], t[NET_CHAT_MAX + 1];
        nwU8(&w, (uint8_t)rnd());
        rndStr(nm, sizeof(nm));
        rndStr(t, sizeof(t));
        nwStr(&w, nm, NET_NAME_MAX - 1);
        nwStr(&w, t, NET_CHAT_MAX);
        *type = NM_CHAT;
        break;
    }
    case 7:
    case 8: {
        NetMatchStart ms;
        if (e->loaded && !chance(10)) {   /* keep the client's match going */
            nwU32(&w, e->matchId);
            *type = NM_MATCH_GO;
            break;
        }
        rndMatchStart(&ms);
        if (chance(30) && e->matchId) ms.match_id = e->matchId;
        e->matchId = ms.match_id;
        e->n = ms.num_players;
        e->frame = 0;
        e->peerAck = 0;
        e->loaded = 0;
        e->goSent = 0;
        netEncMatchStart(&w, &ms);
        nwU8(&w, (uint8_t)(chance(85) ? rndN(ms.num_players ? ms.num_players : 1u) : rnd()));
        *type = NM_MATCH_START;
        break;
    }
    case 9:
        nwU32(&w, chance(85) ? e->matchId : rnd());
        *type = NM_MATCH_GO;
        break;
    case 10:
        if (e->loaded && chance(85)) {   /* mostly let a running match run */
            nwU32(&w, e->matchId);
            *type = NM_MATCH_GO;
            break;
        }
        nwU32(&w, chance(85) ? e->matchId : rnd());
        nwU8(&w, (uint8_t)(chance(50) ? 2 : rnd()));   /* 2 = aborted */
        nwU32(&w, chance(50) ? e->frame + rndN(50) : rndInteresting());
        *type = NM_MATCH_END;
        break;
    case 11:
        nwU32(&w, chance(85) ? e->matchId : rnd());
        nwU32(&w, rnd());
        nwU8(&w, (uint8_t)rnd());
        *type = NM_DESYNC;
        break;
    default: {
        int k, n = (int)rndN(64);
        for (k = 0; k < n; k++) nwU8(&w, (uint8_t)rnd());
        *type = (uint8_t)(32 + rndN(224));
        break;
    }
    }
    return w.len;
}

/* Joiner -> host input records (unreliable), hostile floats included. */
static int craftInputs(Evil *e, uint8_t *b, int cap)
{
    NetW w;
    int i, count, countPos;
    uint32_t first;
    nwInit(&w, b, cap);
    nwU32(&w, chance(90) ? e->matchId : rnd());
    nwU32(&w, chance(70) ? e->peerAck : rndInteresting());
    first = chance(80) ? e->frame : (chance(50) ? e->frame + rndN(600) : rndInteresting());
    nwU32(&w, first);
    countPos = w.len;
    nwU8(&w, 0);
    count = (int)rndN(chance(10) ? 255 : 16);
    for (i = 0; i < count; i++) {
        NetInputRec rec;
        int before = w.len;
        rndRec(&rec);
        netEncInputRec(&w, &rec);
        if (w.overflow) {
            w.overflow = 0;
            w.len = before;
            break;
        }
    }
    b[countPos] = (uint8_t)i;
    if (first == e->frame) e->frame += (uint32_t)i;
    return w.len;
}

/* Host -> client frame bundles (unreliable), hostile floats included. */
static int craftFrames(Evil *e, uint8_t *b, int cap)
{
    NetW w;
    int i, n, count, countPos, want;
    uint32_t first;
    nwInit(&w, b, cap);
    nwU32(&w, chance(90) ? e->matchId : rnd());
    nwU32(&w, chance(70) ? e->peerAck : rndInteresting());          /* input ack */
    nwU32(&w, chance(70) ? e->frame + 10 : rndInteresting());      /* host next */
    n = chance(90) ? e->n : (int)rndN(7);
    nwU8(&w, (uint8_t)n);
    for (i = 0; i < n; i++) nwU32(&w, chance(80) ? e->frame + rndN(20) : rndInteresting());
    nwU8(&w, (uint8_t)(chance(80) ? 0 : rnd()));
    if (chance(80)) {
        /* in step with the client: resend from its ack when far ahead */
        first = (e->frame > e->peerAck + 32) ? e->peerAck : e->frame;
    } else {
        first = chance(50) ? e->frame + rndN(1000) : rndInteresting();
    }
    nwU32(&w, first);
    countPos = w.len;
    nwU8(&w, 0);
    want = (int)rndN(8);
    for (count = 0; count < want; count++) {
        NetBundle bd;
        int s, before = w.len, np = (e->n > 0 && e->n <= NET_MAX_PLAYERS) ? e->n : NET_MAX_PLAYERS;
        memset(&bd, 0, sizeof(bd));
        bd.frame = chance(95) ? first + (uint32_t)count : rnd();
        bd.present = (uint8_t)(chance(80) ? ((1u << np) - 1u) : (rnd() & 0xF));
        bd.disc = (uint8_t)(chance(90) ? 0 : rnd());
        bd.flags = (uint8_t)(chance(95) ? 0 : rnd());
        for (s = 0; s < NET_MAX_PLAYERS; s++) rndRec(&bd.rec[s]);
        netEncBundle(&w, &bd);
        if (w.overflow) {
            w.overflow = 0;
            w.len = before;
            break;
        }
    }
    b[countPos] = (uint8_t)count;
    if (first <= e->frame && first + (uint32_t)count > e->frame) e->frame = first + (uint32_t)count;
    return w.len;
}

/* Raw datagrams: noise, a valid header over noise, or a SESSION payload
 * aimed at the session layer's parser (acks, counts, lengths, seqs). */
static void sendGarbage(const NetAddr *from, const NetAddr *to, uint32_t connId, uint16_t seqHint)
{
    uint8_t b[NET_MAX_PACKET];
    NetW w;
    int n, k = (int)rndN(5);
    nwInit(&w, b, sizeof(b));
    if (k == 0) {
        n = (int)rndN(sizeof(b) + 1);
        rndBytes(b, (size_t)n);
    } else if (k == 1) {
        int i, m = (int)rndN(200);
        netWriteHeader(&w, (uint8_t)rndN(12), chance(50) ? connId : rnd());
        for (i = 0; i < m; i++) nwU8(&w, (uint8_t)rnd());
        netFinishHeader(&w);
        n = w.len;
    } else {
        int i, cnt = (int)rndN(8);
        netWriteHeader(&w, NP_SESSION, chance(85) ? connId : rnd());
        nwU16(&w, (uint16_t)(chance(50) ? seqHint : rndInteresting()));
        nwU8(&w, (uint8_t)(chance(80) ? cnt : rnd()));
        for (i = 0; i < cnt; i++) {
            uint8_t t = (uint8_t)(chance(50) ? rndN(64) : rnd());
            int len = (int)(chance(80) ? rndN(40) : (rndInteresting() & 0xFFFF));
            int j, real = len < 60 ? len : (int)rndN(60);
            nwU8(&w, t);
            nwU16(&w, (uint16_t)len);
            if (NM_IS_RELIABLE(t)) nwU16(&w, (uint16_t)(chance(60) ? seqHint + rndN(70) : rndInteresting()));
            for (j = 0; j < real; j++) nwU8(&w, (uint8_t)rnd());
        }
        netFinishHeader(&w);
        n = w.len;
        if (chance(30)) mutate(b, &n, (int)sizeof(b));
    }
    qPush(from, to, b, n);
}

static void evilStep(Evil *e)
{
    uint8_t b[NET_REL_MAXMSG];
    NetTransport t = qTransport(&e->self);
    if (!e->connValid) {
        if (e->role == 0 && (!e->lastJoinUs || g_now - e->lastJoinUs > 250000)) evilSendJoin(e);
        if (chance(1)) sendGarbage(&e->self, &e->peer, rnd(), (uint16_t)rnd());
        return;
    }
    if (e->role == 1 && e->loaded && !e->goSent && netConnPendingReliable(&e->conn) < 24) {
        NetW w;
        nwInit(&w, b, sizeof(b));
        nwU32(&w, e->matchId);
        (void)netConnSendReliable(&e->conn, NM_MATCH_GO, b, w.len);
        e->goSent = 1;
    }
    if (chance(e->rate) && netConnPendingReliable(&e->conn) < 24) {
        uint8_t type = 0;
        int n = (e->role == 0) ? craftClientMsg(e, &type, b, (int)sizeof(b)) : craftHostMsg(e, &type, b, (int)sizeof(b));
        if (n >= 0) {
            if (chance(25)) mutate(b, &n, (int)sizeof(b));
            if (NM_IS_RELIABLE(type)) (void)netConnSendReliable(&e->conn, type, b, n);
        }
    }
    if (chance(e->rate)) {
        NetUnrel u;
        int n;
        if (e->role == 0) {
            n = craftInputs(e, b, (int)sizeof(b));
            u.type = NM_INPUTS;
        } else if (chance(85)) {
            n = craftFrames(e, b, (int)sizeof(b));
            u.type = NM_FRAMES;
        } else {
            NetW w;
            nwInit(&w, b, sizeof(b));
            nwU32(&w, e->matchId);
            nwU8(&w, (uint8_t)rnd());
            nwU8(&w, (uint8_t)rnd());
            n = w.len;
            u.type = NM_MATCH_WAIT;
        }
        if (chance(8)) u.type = (uint8_t)(chance(80) ? rndN(32) : rnd());
        if (chance(20)) mutate(b, &n, 1000);
        u.data = b;
        u.len = n;
        netConnTransmit(&e->conn, &t, g_now, &u, 1, 0);
    } else {
        netConnTransmit(&e->conn, &t, g_now, NULL, 0, 0);
    }
    if (e->conn.failed) {
        evilFree(e);
        return;
    }
    if (chance(2)) sendGarbage(&e->self, &e->peer, e->conn.connId, e->conn.sendSeq);
}

/* ---- game-like driving of a real client ---- */

typedef struct Toy {
    int inMatch, loaded, delay;
    uint64_t loadAt;
    uint32_t frame;
    int submitted;
} Toy;

static uint32_t g_matchesTaken, g_framesRun, g_lobbiesSeen;

static void toyStep(Toy *t, NetClient *c)
{
    NetMatchStart ms;
    NetBundle b;
    int slot, r;
    if (!t->inMatch) {
        if (netClientMatchTake(c, &ms, &slot)) {
            FCHECK(ms.num_players >= 1 && ms.num_players <= NET_MAX_PLAYERS && ms.delay <= NET_MAX_DELAY &&
                   slot >= 0 && slot < ms.num_players, "match handed to the game: %d players, delay %d, slot %d",
                   ms.num_players, ms.delay, slot);
            memset(t, 0, sizeof(*t));
            t->inMatch = 1;
            t->delay = ms.delay;
            t->loadAt = g_now + rndN(400000);
            t->submitted = -1;
            g_matchesTaken++;
        }
        return;
    }
    if (!t->loaded) {
        if (g_now >= t->loadAt) {
            netClientMatchLoaded(c);
            t->loaded = 1;
        }
        return;
    }
    if (!netClientMatchIsGo(c)) {
        if (netClientMatchGetBundle(c, t->frame, &b) < 0) {
            netClientMatchFinished(c);
            t->inMatch = 0;
        }
        return;
    }
    if (t->submitted != (int)t->frame) {
        NetInputRec rec;
        rndRec(&rec);   /* even a peer's own input may be hostile: the host sanitises it */
        (void)netClientMatchAdvise(c, t->frame);
        netClientMatchSubmit(c, t->frame + (uint32_t)t->delay, &rec);
        t->submitted = (int)t->frame;
    }
    r = netClientMatchGetBundle(c, t->frame, &b);
    if (r < 0) {
        netClientMatchFinished(c);
        t->inMatch = 0;
        return;
    }
    if (r == 1) {
        int s;
        FCHECK(b.frame == t->frame, "bundle for frame %u came out as %u", t->frame, b.frame);
        for (s = 0; s < NET_MAX_PLAYERS; s++) {
            if (b.present & (1u << s)) checkRec(&b.rec[s]);
        }
        if (t->frame % 30 == 0) netClientMatchHash(c, t->frame, rnd());
        netClientMatchConsumed(c, t->frame + 1);
        t->frame++;
        g_framesRun++;
        if (chance(1) && chance(5)) {
            netClientMatchLeave(c);
            t->inMatch = 0;
        }
    }
}

/* What a player clicks in the lobby screens, including nonsense. */
static void uiPoke(NetClient *c)
{
    switch (rndN(9)) {
    case 0:
        netClientSetPlayer(c, (int)rndN(70), (int)rndN(12), (int)rndN(5), (int)rndN(3), (int)(rnd() & 1));
        break;
    case 1: {
        NetSettings s;
        rndSettings(&s);
        netClientSetSettings(c, &s);
        break;
    }
    case 2:
        netClientStartMatch(c);
        break;
    case 3: {
        char t[NET_CHAT_MAX + 1];
        rndStr(t, sizeof(t));
        netClientChat(c, t);
        break;
    }
    case 4:
        netClientRequestList(c);
        break;
    case 5:
        netClientAbortMatch(c);
        break;
    case 6:
        if (chance(20)) netClientLeaveLobby(c);
        break;
    case 7:
        netClientQuickMatch(c);
        break;
    default: {
        char code[NET_CODE_LEN + 2];
        rndStr(code, sizeof(code));
        netClientJoinLobby(c, chance(50) ? 0 : rnd(), code);
        break;
    }
    }
}

/* ======================================================================== */
/* 3. Evil host vs. a real client                                            */
/* ======================================================================== */

static NetClient *newClient(NetAddr *addr, const char *label)
{
    NetClientConfig cc;
    memset(&cc, 0, sizeof(cc));
    netStrCopy(cc.name, NET_NAME_MAX, label);
    netStrCopy(cc.buildId, NET_BUILDID_MAX, BUILD_ID);
    cc.log = logFn;
    cc.logCtx = (void *)label;
    return netClientCreate(&cc, qTransport(addr));
}

static void stepClock(void)
{
    g_now += 500 + rndN(4000);
    if (chance(1) && chance(5)) g_now += 1000000 + rndN(11000000);   /* a stall: timeouts fire */
}

static void episodeEvilHost(int steps)
{
    static NetAddr caddr, eaddr;
    NetClient *c;
    Evil ev;
    Toy toy;
    int i, k;
    caddr = netAddrMake(10, 1, 0, 2, 40001);
    eaddr = netAddrMake(10, 1, 0, 1, 27007);
    g_ctx = "evil host";
    c = newClient(&caddr, "victim");
    evilInit(&ev, eaddr, caddr, 1, 0);
    memset(&toy, 0, sizeof(toy));
    if (chance(80)) {
        netClientConnect(c, &eaddr, (int)(rnd() & 1));
    } else {
        NetAddr cands[NET_MAX_CANDS];
        int n = 1 + (int)rndN(NET_MAX_CANDS);
        for (k = 0; k < n; k++) cands[k] = rndAddr();
        cands[rndN((uint32_t)n)] = eaddr;
        netClientConnectMulti(c, cands, n, (int)(rnd() & 1), 0);
    }
    for (i = 0; i < steps; i++) {
        int src = g_qcur;
        stepClock();
        g_qcur ^= 1;
        g_qcount[g_qcur] = 0;
        for (k = 0; k < g_qcount[src]; k++) {
            const QPkt *p = &g_qbuf[src][k];
            if (netAddrEq(&p->to, &caddr)) netClientOnPacket(c, &p->from, p->data, p->len, g_now);
            else if (netAddrEq(&p->to, &eaddr)) evilOnPacket(&ev, p);
        }
        g_qcount[src] = 0;
        netClientTick(c, g_now);
        toyStep(&toy, c);
        evilStep(&ev);
        if (chance(3)) uiPoke(c);
        if (chance(1)) {   /* garbage from strangers: must be ignored */
            NetAddr stranger = rndAddr();
            sendGarbage(&stranger, &caddr, rnd(), (uint16_t)rnd());
        }
        if ((i & 15) == 0) {
            NetClientStatus st;
            checkClient(c);
            netClientGetStatus(c, &st);
            if (st.lobbyValid) g_lobbiesSeen++;
            if ((st.state == NCS_FAILED || st.state == NCS_IDLE) && chance(25)) {
                evilFree(&ev);
                memset(&toy, 0, sizeof(toy));
                netClientConnect(c, &eaddr, (int)(rnd() & 1));
            }
        }
    }
    netClientDestroy(c);
    evilFree(&ev);
    g_qcount[0] = g_qcount[1] = 0;
}

/* ======================================================================== */
/* 4. Evil joiner (and on-path attacker) vs. a real host + real players      */
/* ======================================================================== */

static uint32_t g_hostMatches;

static void legitLobby(NetClient *c, int idx, int serverMode, const NetAddr *host)
{
    static NetClientStatus st;
    netClientGetStatus(c, &st);
    if (st.state == NCS_CONNECTED && serverMode && chance(3)) {
        if (idx == 0) netClientCreateLobby(c, "Fuzz", 1, 2 + (int)rndN(3), (int)(rnd() & 1));
        else netClientQuickMatch(c);
    }
    if (st.state == NCS_LOBBY && st.lobbyValid && st.lobby.state == NLS_WAITING && st.slot >= 0) {
        const NetPlayerInfo *p = &st.lobby.players[st.slot];
        if (!p->ready && chance(5)) netClientSetPlayer(c, p->character, p->handicap, p->control, p->team, 1);
        if (st.isLeader && chance(2)) netClientStartMatch(c);
        if (st.isLeader && chance(1)) {
            NetSettings s = st.lobby.settings;
            s.stage = (uint8_t)rndN(NG_NUM_STAGES);
            if (chance(30)) s.scenario = (uint8_t)rndN(NG_NUM_SCENARIOS);
            netClientSetSettings(c, &s);
        }
    }
    if ((st.state == NCS_FAILED || st.state == NCS_IDLE) && chance(3)) netClientConnect(c, host, serverMode);
}

static void deliverToHostWorld(const QPkt *p, NetHost *h, const NetAddr *haddr, NetClient **lc,
                               const NetAddr *laddr, int nl, Evil *ev)
{
    int i;
    if (netAddrEq(&p->to, haddr)) {
        netHostOnPacket(h, &p->from, p->data, p->len, g_now);
        return;
    }
    for (i = 0; i < nl; i++) {
        if (netAddrEq(&p->to, &laddr[i])) {
            netClientOnPacket(lc[i], &p->from, p->data, p->len, g_now);
            return;
        }
    }
    if (netAddrEq(&p->to, &ev->self)) evilOnPacket(ev, p);
}

static void episodeEvilJoiner(int steps)
{
    static NetAddr haddr, laddr[2], eaddr;
    const int serverMode = chance(35);
    NetHostConfig hc;
    NetHost *h;
    NetClient *lc[2];
    Toy toy[2];
    Evil ev;
    int i, k;
    haddr = netAddrMake(10, 2, 0, 1, (uint16_t)(serverMode ? NET_DEFAULT_SERVER_PORT : NET_DEFAULT_HOST_PORT));
    laddr[0] = netAddrMake(10, 2, 0, 2, 40001);
    laddr[1] = netAddrMake(10, 2, 0, 3, 40002);
    eaddr = netAddrMake(10, 2, 0, 66, 40066);
    g_ctx = serverMode ? "evil joiner / server" : "evil joiner / host";
    g_tamper = chance(50) ? 0 : 1 + (int)rndN(4);
    g_nreplay = 0;

    memset(&hc, 0, sizeof(hc));
    hc.serverMode = serverMode;
    hc.maxPlayers = 2 + (int)rndN(3);
    netStrCopy(hc.buildId, NET_BUILDID_MAX, BUILD_ID);
    netStrCopy(hc.lobbyName, NET_LOBBY_NAME_MAX, "Fuzz host");
    hc.log = logFn;
    hc.logCtx = (void *)"host";
    h = netHostCreate(&hc, qTransport(&haddr));
    if (!serverMode && chance(50)) {
        /* an online quick game (D415 / D416): locked rules, balanced teams */
        NdpPrefs p;
        NetSettings rules;
        int maxp;
        rndPrefs(&p);
        ndpNormalizePrefs(&p);
        ndpPrefsToRules(&p, &rules, &maxp);
        netHostSetDirectInfo(h, chance(50) ? "ABCDEF" : "", (uint8_t)(NL_ONLINE | NL_PUBLIC | NL_QUICK), 1);
        netHostSetQuickRules(h, &rules, maxp);
        g_ctx = "evil joiner / quick host";
    }
    for (k = 0; k < 2; k++) {
        lc[k] = newClient(&laddr[k], k ? "legit2" : "legit1");
        memset(&toy[k], 0, sizeof(toy[k]));
        netClientConnect(lc[k], &haddr, serverMode);
    }
    evilInit(&ev, eaddr, haddr, 0, serverMode);

    for (i = 0; i < steps; i++) {
        int src = g_qcur;
        stepClock();
        g_qcur ^= 1;
        g_qcount[g_qcur] = 0;
        for (k = 0; k < g_qcount[src]; k++) {
            const QPkt *p = &g_qbuf[src][k];
            if (g_tamper && chance(g_tamper)) {           /* on-path: tampered copy */
                QPkt t = *p;
                mutate(t.data, &t.len, NET_MAX_PACKET);
                deliverToHostWorld(&t, h, &haddr, lc, laddr, 2, &ev);
            }
            if (g_tamper && chance(2)) {                  /* remembered for a replay */
                g_replay[g_nreplay < 32 ? g_nreplay++ : (int)rndN(32)] = *p;
            }
            deliverToHostWorld(p, h, &haddr, lc, laddr, 2, &ev);
        }
        g_qcount[src] = 0;
        if (g_nreplay && g_tamper && chance(3)) deliverToHostWorld(&g_replay[rndN((uint32_t)g_nreplay)], h, &haddr, lc, laddr, 2, &ev);
        netHostTick(h, g_now);
        for (k = 0; k < 2; k++) {
            netClientTick(lc[k], g_now);
            legitLobby(lc[k], k, serverMode, &haddr);
            toyStep(&toy[k], lc[k]);
        }
        evilStep(&ev);
        if (chance(1)) {
            NetAddr stranger = rndAddr();
            sendGarbage(&stranger, &haddr, rnd(), (uint16_t)rnd());
        }
        if ((i & 31) == 0) {
            NetHostStats hs;
            netHostGetStats(h, &hs);
            FCHECK(hs.sessions >= 0 && hs.sessions <= (serverMode ? 512 : 16) && hs.lobbies >= 0 &&
                   hs.lobbies <= (serverMode ? 128 : 1) && hs.matches >= 0 && hs.matches <= hs.lobbies,
                   "host stats: %d sessions, %d lobbies, %d matches", hs.sessions, hs.lobbies, hs.matches);
            if (hs.matches > 0) g_hostMatches++;
            if (!serverMode) {
                NetHostLobbyInfo li;
                if (netHostGetDirectLobby(h, &li) && li.valid) checkLobby(&li.lobby, 0);
            }
            for (k = 0; k < 2; k++) checkClient(lc[k]);
        }
    }
    for (k = 0; k < 2; k++) netClientDestroy(lc[k]);
    netHostDestroy(h);
    evilFree(&ev);
    g_qcount[0] = g_qcount[1] = 0;
    g_tamper = 0;
}

/* ======================================================================== */

int main(int argc, char **argv)
{
    int iters = 100, i;
    uint64_t seed = 1;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--iters") && i + 1 < argc) iters = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-v")) g_verbose = 1;
    }
    if (iters < 1) iters = 1;
    setvbuf(stdout, NULL, _IONBF, 0);   /* progress survives a hang or a sanitizer abort */
    g_rs = seed * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull;
    netRandomTestSeed(seed * 0xBF58476D1CE4E5B9ull + 1);
    g_qbuf[0] = (QPkt *)calloc(QMAX, sizeof(QPkt));
    g_qbuf[1] = (QPkt *)calloc(QMAX, sizeof(QPkt));
    if (!g_qbuf[0] || !g_qbuf[1]) return 2;
    g_now = 1000000;

    printf("GoldenEye netplay fuzzer: seed %llu, %d iterations\n", (unsigned long long)seed, iters);
    fuzzDecoders(iters * 400);
    fuzzPrefs(iters * 100);
    for (i = 0; i < iters; i++) {
        uint64_t t0 = netTimeUs(), p0 = g_pkts;
        episodeEvilHost(1500 + (int)rndN(3000));
        t0 = netTimeUs() - t0;
        if (t0 > 2000000 || g_verbose) {
            printf("    evil host episode %d: %.1f s, %llu packets\n", i, (double)t0 / 1e6,
                   (unsigned long long)(g_pkts - p0));
        }
    }
    printf("  evil host: %d episodes; the victim took %u matches, ran %u frames, saw a lobby %u times\n", iters,
           g_matchesTaken, g_framesRun, g_lobbiesSeen);
    g_ctx = "evil host";
    FCHECK(g_matchesTaken > 0 && g_framesRun > 0, "the evil host never got the client into a running match");
    {
        uint32_t frames = g_framesRun;
        for (i = 0; i < iters; i++) {
            uint64_t t0 = netTimeUs(), p0 = g_pkts;
            episodeEvilJoiner(1500 + (int)rndN(3000));
            t0 = netTimeUs() - t0;
            if (t0 > 2000000 || g_verbose) {
                printf("    evil joiner episode %d (%s): %.1f s, %llu packets\n", i, g_ctx, (double)t0 / 1e6,
                       (unsigned long long)(g_pkts - p0));
            }
        }
        printf("  evil joiner: %d episodes; matches running at %u checks, legit players ran %u frames\n", iters,
               g_hostMatches, g_framesRun - frames);
        g_ctx = "evil joiner";
        FCHECK(g_hostMatches > 0 && g_framesRun > frames, "the legit players never played under attack");
    }
    free(g_qbuf[0]);
    free(g_qbuf[1]);
    if (g_viol) {
        printf("\n%d VIOLATION(S)\n", g_viol);
        return 1;
    }
    printf("\nNO VIOLATIONS\n");
    return 0;
}
