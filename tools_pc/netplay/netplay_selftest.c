/*
 * netplay_selftest.c -- protocol + lockstep tests for the netplay core (D413).
 *
 * Runs without the game or a ROM. A host (direct or matchmaking-server mode)
 * and N clients talk through a simulated network with latency, jitter, loss,
 * duplication and reordering, on a virtual clock. Each client runs a toy
 * deterministic "game" that consumes frame bundles exactly like the real
 * playback hook does (port/src/netgame.c) and folds them into a state value.
 *
 * Asserted:
 *   - every client sees byte-identical bundles, so toy states agree on every
 *     frame (lockstep holds under loss / reorder / jitter / clock drift);
 *   - no false DESYNC; an injected divergence IS reported;
 *   - a vanished client is cut over to neutral input at the same frame on
 *     every remaining peer and the match keeps running;
 *   - a leader abort ends the match at the same frame everywhere;
 *   - matchmaking: create + join by code, lobby list, quick match, the start
 *     rules (ready / validation), autostart;
 *   - a short real-UDP loopback match (socket layer smoke test).
 *
 * Build: see tools_pc/netplay/CMakeLists.txt (or build.bat for MSVC).
 * Usage: netplay_selftest [-v]   exit code 0 = all passed.
 */
#include "net_client.h"
#include "net_dirproto.h"
#include "net_gamedata.h"
#include "net_host.h"
#include "net_plat.h"
#include "net_proto.h"
#include "net_sock.h"
#include "net_stun.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static int g_verbose = 0;
static int g_failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        g_failures++; \
        printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

static void logFn(void *ctx, int level, const char *msg)
{
    (void)ctx;
    if (g_verbose || level >= NETLOG_WARN) printf("    [%s] %s\n", (const char *)(ctx ? ctx : "net"), msg);
}

/* ------------------------------------------------------------------------ */
/* Deterministic test RNG (xorshift64*)                                      */
/* ------------------------------------------------------------------------ */

typedef struct { uint64_t s; } Rng;
static uint32_t rngNext(Rng *r)
{
    r->s ^= r->s >> 12;
    r->s ^= r->s << 25;
    r->s ^= r->s >> 27;
    return (uint32_t)((r->s * 2685821657736338717ull) >> 32);
}
static double rngUnit(Rng *r) { return (double)rngNext(r) / 4294967296.0; }

/* ------------------------------------------------------------------------ */
/* Simulated network                                                         */
/* ------------------------------------------------------------------------ */

#define SIM_MAX_PKTS 20000
#define SIM_MAX_EP   8

typedef struct SimPkt {
    NetAddr from, to;
    uint64_t deliverAt;
    int len;
    uint8_t data[NET_MAX_PACKET];
} SimPkt;

typedef struct SimNet {
    uint64_t now;
    Rng rng;
    double loss, dup;
    uint32_t latMinUs, latMaxUs;   /* one-way */
    SimPkt *pkts;
    int npkts;
    NetAddr ep[SIM_MAX_EP];
    int blackhole[SIM_MAX_EP];     /* endpoint vanished */
    int nep;
    uint32_t sent, dropped;
    /* Extra routes: what endpoint srcIdx sends to `dst` reaches `dstReal`
     * and appears to come from `srcAlias` (a NAT that loops a public address
     * back, two machines on one LAN, ...). */
    struct {
        int srcIdx;
        NetAddr dst, dstReal, srcAlias;
    } path[4];
    int npath;
} SimNet;

typedef struct SimEp {
    SimNet *net;
    int idx;
} SimEp;

static int simIndex(SimNet *n, const NetAddr *a)
{
    int i;
    for (i = 0; i < n->nep; i++) {
        if (netAddrEq(&n->ep[i], a)) return i;
    }
    return -1;
}

static void simQueue(SimNet *n, const NetAddr *from, const NetAddr *to, const void *data, int len)
{
    SimPkt *p;
    uint32_t lat;
    if (n->npkts >= SIM_MAX_PKTS || len > NET_MAX_PACKET) {
        n->dropped++;
        return;
    }
    p = &n->pkts[n->npkts++];
    p->from = *from;
    p->to = *to;
    lat = n->latMinUs + (n->latMaxUs > n->latMinUs ? rngNext(&n->rng) % (n->latMaxUs - n->latMinUs + 1) : 0);
    p->deliverAt = n->now + lat;   /* random latency => natural reordering */
    p->len = len;
    memcpy(p->data, data, (size_t)len);
}

static int simSend(void *ctx, const NetAddr *to, const void *data, int len)
{
    SimEp *e = (SimEp *)ctx;
    SimNet *n = e->net;
    NetAddr from = n->ep[e->idx], dest = *to;
    int k;
    n->sent++;
    if (n->blackhole[e->idx]) return 0;
    if (rngUnit(&n->rng) < n->loss) {
        n->dropped++;
        return 0;
    }
    for (k = 0; k < n->npath; k++) {
        if (n->path[k].srcIdx == e->idx && netAddrEq(&n->path[k].dst, to)) {
            from = n->path[k].srcAlias;
            dest = n->path[k].dstReal;
            break;
        }
    }
    simQueue(n, &from, &dest, data, len);
    if (rngUnit(&n->rng) < n->dup) simQueue(n, &from, &dest, data, len);
    return 0;
}

static int simRecv(void *ctx, NetAddr *from, void *buf, int cap)
{
    SimEp *e = (SimEp *)ctx;
    SimNet *n = e->net;
    int i, best = -1;
    for (i = 0; i < n->npkts; i++) {
        SimPkt *p = &n->pkts[i];
        if (p->deliverAt > n->now || !netAddrEq(&p->to, &n->ep[e->idx])) continue;
        if (best < 0 || p->deliverAt < n->pkts[best].deliverAt) best = i;
    }
    if (best < 0) return 0;
    {
        SimPkt *p = &n->pkts[best];
        int len = p->len;
        int dead = n->blackhole[e->idx];
        if (from) *from = p->from;
        if (len > cap) len = cap;
        memcpy(buf, p->data, (size_t)len);
        n->pkts[best] = n->pkts[--n->npkts];
        if (dead) return simRecv(ctx, from, buf, cap);   /* vanished: drop inbound */
        return len;
    }
}

static NetTransport simTransport(SimEp *e)
{
    NetTransport t;
    t.ctx = e;
    t.send = simSend;
    t.recv = simRecv;
    return t;
}

static void simInit(SimNet *n, uint64_t seed, double loss, double dup, uint32_t latMin, uint32_t latMax)
{
    memset(n, 0, sizeof(*n));
    n->rng.s = seed ? seed : 1;
    n->loss = loss;
    n->dup = dup;
    n->latMinUs = latMin;
    n->latMaxUs = latMax;
    n->now = 1000;
    n->pkts = (SimPkt *)calloc(SIM_MAX_PKTS, sizeof(SimPkt));
}

static void simFree(SimNet *n)
{
    free(n->pkts);
}

/* Two-way alternate route between endpoints a and b: a reaches b at
 * bAlias, and b sees it coming from aAlias (and vice versa). */
static void simAddPath(SimNet *n, int a, NetAddr aAlias, int b, NetAddr bAlias)
{
    n->path[n->npath].srcIdx = a;
    n->path[n->npath].dst = bAlias;
    n->path[n->npath].dstReal = n->ep[b];
    n->path[n->npath].srcAlias = aAlias;
    n->npath++;
    n->path[n->npath].srcIdx = b;
    n->path[n->npath].dst = aAlias;
    n->path[n->npath].dstReal = n->ep[a];
    n->path[n->npath].srcAlias = bAlias;
    n->npath++;
}

static int simAddEndpoint(SimNet *n, NetAddr a, SimEp *e)
{
    n->ep[n->nep] = a;
    e->net = n;
    e->idx = n->nep;
    return n->nep++;
}

/* ------------------------------------------------------------------------ */
/* Toy game client                                                           */
/* ------------------------------------------------------------------------ */

#define MAX_TEST_FRAMES 8000

typedef struct ToyClient {
    char label[16];
    SimEp ep;
    NetClient *c;
    int vanished;
    /* match */
    int inMatch;
    int loaded;
    uint64_t loadDoneAt;
    uint32_t frame;          /* next frame to run */
    int submittedFor;        /* frame index whose local input was submitted */
    uint64_t nextFrameAt;
    double period;
    uint64_t state;
    uint64_t states[MAX_TEST_FRAMES];
    uint8_t haveState[MAX_TEST_FRAMES];
    uint8_t discAt[MAX_TEST_FRAMES];
    int ended;               /* match ended for us (got -1) */
    uint32_t endFrame;
    int finishReported;
    Rng inRng;
    uint32_t stallFrames;
    int injectDesyncAt;      /* frame, or -1 */
    int matchDelay;
    int slot;
} ToyClient;

static uint64_t mix64(uint64_t h, uint64_t v)
{
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h *= 0xBF58476D1CE4E5B9ull;
    return h ^ (h >> 31);
}

static uint64_t foldBundle(uint64_t s, const NetBundle *b)
{
    int i;
    s = mix64(s, b->frame);
    s = mix64(s, ((uint64_t)b->present << 16) | ((uint64_t)b->disc << 8) | b->flags);
    for (i = 0; i < NET_MAX_PLAYERS; i++) {
        const NetInputRec *r = &b->rec[i];
        uint32_t f[8];
        if (!(b->present & (1u << i))) continue;
        s = mix64(s, ((uint64_t)r->buttons << 32) | ((uint64_t)(uint8_t)r->stick_x << 24) |
                     ((uint64_t)(uint8_t)r->stick_y << 16) | ((uint64_t)r->flags << 8) | r->actions);
        memcpy(&f[0], &r->look_dtheta, 4);
        memcpy(&f[1], &r->look_dverta, 4);
        memcpy(&f[2], &r->cross_x, 4);
        memcpy(&f[3], &r->cross_y, 4);
        memcpy(&f[4], &r->pdturn_x, 4);
        memcpy(&f[5], &r->pdturn_y, 4);
        memcpy(&f[6], &r->gun_az, 4);
        memcpy(&f[7], &r->gun_turn, 4);
        if (r->flags & NIR_LOOK) s = mix64(s, ((uint64_t)f[0] << 32) | f[1]);
        if (r->flags & NIR_CROSS) {
            s = mix64(s, ((uint64_t)f[2] << 32) | f[3]);
            s = mix64(s, ((uint64_t)f[6] << 32) | f[7]);
        }
        if (r->flags & NIR_PDTURN) s = mix64(s, ((uint64_t)f[4] << 32) | f[5]);
    }
    return s;
}

static void makeInput(Rng *r, NetInputRec *rec)
{
    memset(rec, 0, sizeof(*rec));
    rec->buttons = (uint16_t)(rngNext(r) & 0xF03F);
    rec->stick_x = (int8_t)((int)(rngNext(r) % 161) - 80);
    rec->stick_y = (int8_t)((int)(rngNext(r) % 161) - 80);
    if (rngNext(r) & 1) {
        rec->flags |= NIR_LOOK;
        rec->look_dtheta = (float)((int)(rngNext(r) % 2001) - 1000) / 37.0f;
        rec->look_dverta = (float)((int)(rngNext(r) % 2001) - 1000) / 91.0f;
    }
    if ((rngNext(r) % 4) == 0) {
        rec->flags |= NIR_CROSS;
        rec->cross_x = (float)rngUnit(r) * 2.0f - 1.0f;
        rec->cross_y = (float)rngUnit(r) * 2.0f - 1.0f;
        rec->gun_az = (float)rngUnit(r) * 6.0f - 3.0f;
        rec->gun_turn = (float)rngUnit(r) * 6.0f - 3.0f;
    }
    if ((rngNext(r) % 8) == 0) {
        rec->flags |= NIR_PDTURN;
        rec->pdturn_x = (float)rngUnit(r);
        rec->pdturn_y = -(float)rngUnit(r);
    }
    if ((rngNext(r) % 16) == 0) rec->actions = (uint8_t)(1u << (rngNext(r) % 5));
}

static void toyInit(ToyClient *t, SimNet *n, int idx, const char *label, uint64_t seed)
{
    NetClientConfig cc;
    memset(t, 0, sizeof(*t));
    netStrCopy(t->label, sizeof(t->label), label);
    simAddEndpoint(n, netAddrMake(10, 0, 0, (uint8_t)(10 + idx), (uint16_t)(40000 + idx)), &t->ep);
    memset(&cc, 0, sizeof(cc));
    netStrCopy(cc.name, NET_NAME_MAX, label);
    netStrCopy(cc.buildId, NET_BUILDID_MAX, "selftest-build-1");
    cc.log = logFn;
    cc.logCtx = t->label;
    t->c = netClientCreate(&cc, simTransport(&t->ep));
    t->inRng.s = seed;
    t->period = 16666.67;
    t->injectDesyncAt = -1;
    t->submittedFor = -1;
}

static void toyFree(ToyClient *t)
{
    netClientDestroy(t->c);
}

/* One game-loop opportunity for a toy client at sim time n->now. */
static void toyStep(ToyClient *t, SimNet *n, Rng *jit)
{
    NetMatchStart ms;
    int slot;
    if (t->vanished) return;

    if (!t->inMatch) {
        if (netClientMatchTake(t->c, &ms, &slot)) {
            t->inMatch = 1;
            t->loaded = 0;
            t->loadDoneAt = n->now + 300000 + rngNext(jit) % 1500000;   /* 0.3..1.8 s "stage load" */
            t->frame = 0;
            t->state = 0x1234567890ABCDEFull ^ ms.seed_random;
            t->ended = 0;
            t->finishReported = 0;
            t->matchDelay = ms.delay;
            t->slot = slot;
            memset(t->haveState, 0, sizeof(t->haveState));
            memset(t->discAt, 0, sizeof(t->discAt));
            t->submittedFor = -1;
        }
        return;
    }
    if (t->ended) {
        if (!t->finishReported) {
            netClientMatchFinished(t->c);
            t->finishReported = 1;
        }
        return;
    }
    if (!t->loaded) {
        if (n->now >= t->loadDoneAt) {
            netClientMatchLoaded(t->c);
            t->loaded = 1;
        }
        return;
    }
    if (t->frame == 0 && !netClientMatchIsGo(t->c)) {
        t->nextFrameAt = n->now;
        return;
    }
    if (n->now < t->nextFrameAt) return;

    /* Like the playback hook: sample local input for frame + D once, then
     * wait for this frame's bundle. */
    if (t->submittedFor != (int)t->frame) {
        NetInputRec rec;
        uint32_t adv = netClientMatchAdvise(t->c, t->frame);
        makeInput(&t->inRng, &rec);
        netClientMatchSubmit(t->c, t->frame + (uint32_t)t->matchDelay, &rec);
        t->submittedFor = (int)t->frame;
        t->nextFrameAt += adv;
        if (n->now < t->nextFrameAt) return;
    }
    {
        NetBundle b;
        int r = netClientMatchGetBundle(t->c, t->frame, &b);
        if (r == 0) {
            t->stallFrames++;
            return;
        }
        if (r < 0) {
            t->ended = 1;
            t->endFrame = t->frame;
            return;
        }
        t->state = foldBundle(t->state, &b);
        if ((int)t->frame == t->injectDesyncAt) t->state ^= 0xDEADBEEFull;
        if (t->frame < MAX_TEST_FRAMES) {
            t->states[t->frame] = t->state;
            t->haveState[t->frame] = 1;
            t->discAt[t->frame] = b.disc;
        }
        if ((t->frame % 30) == 0) netClientMatchHash(t->c, t->frame, (uint32_t)(t->state ^ (t->state >> 32)));
        netClientMatchConsumed(t->c, t->frame + 1);
        t->frame++;
        /* Per-client clock drift + render jitter. */
        t->nextFrameAt += (uint64_t)(t->period + (double)(rngNext(jit) % 2000) - 1000.0);
    }
}

/* ------------------------------------------------------------------------ */
/* Scenario driver                                                           */
/* ------------------------------------------------------------------------ */

typedef struct World {
    SimNet net;
    SimEp hostEp;
    NetHost *host;
    NetAddr hostAddr;
    ToyClient cl[SIM_MAX_EP];
    int ncl;
    Rng jit;
} World;

static void worldInit(World *w, int serverMode, int nclients, uint64_t seed,
                      double loss, double dup, uint32_t latMin, uint32_t latMax)
{
    NetHostConfig hc;
    int i;
    memset(w, 0, sizeof(*w));
    simInit(&w->net, seed, loss, dup, latMin, latMax);
    w->jit.s = seed * 31 + 7;
    w->hostAddr = netAddrMake(10, 0, 0, 1, (uint16_t)(serverMode ? NET_DEFAULT_SERVER_PORT : NET_DEFAULT_HOST_PORT));
    simAddEndpoint(&w->net, w->hostAddr, &w->hostEp);
    memset(&hc, 0, sizeof(hc));
    hc.serverMode = serverMode;
    hc.maxPlayers = 4;
    netStrCopy(hc.buildId, NET_BUILDID_MAX, "selftest-build-1");
    netStrCopy(hc.lobbyName, NET_LOBBY_NAME_MAX, "Selftest");
    hc.log = logFn;
    hc.logCtx = (void *)"host";
    w->host = netHostCreate(&hc, simTransport(&w->hostEp));
    w->ncl = nclients;
    for (i = 0; i < nclients; i++) {
        char label[16];
        netStrFmt(label, sizeof(label), "P%d", i + 1);
        toyInit(&w->cl[i], &w->net, i, label, seed * 1000 + (uint64_t)i + 1);
        /* +/-0.3 % clock drift per peer */
        w->cl[i].period *= 1.0 + ((double)((int)(rngNext(&w->jit) % 61) - 30) / 10000.0);
    }
}

static void worldFree(World *w)
{
    int i;
    for (i = 0; i < w->ncl; i++) toyFree(&w->cl[i]);
    netHostDestroy(w->host);
    simFree(&w->net);
}

static void worldStep(World *w, uint64_t dtUs)
{
    int i;
    w->net.now += dtUs;
    netHostPump(w->host, w->net.now);
    for (i = 0; i < w->ncl; i++) {
        if (w->cl[i].vanished) continue;
        netClientPump(w->cl[i].c, w->net.now);
        toyStep(&w->cl[i], &w->net, &w->jit);
    }
}

/* Run until pred(w) or timeout (sim us). Returns 1 if pred became true. */
typedef int (*WorldPred)(World *w, void *arg);
static int worldRunUntil(World *w, WorldPred pred, void *arg, uint64_t timeoutUs)
{
    uint64_t end = w->net.now + timeoutUs;
    while (w->net.now < end) {
        worldStep(w, 250);
        if (pred(w, arg)) return 1;
    }
    return 0;
}

static int predAllInLobby(World *w, void *arg)
{
    int i, want = (int)(intptr_t)arg;
    for (i = 0; i < w->ncl; i++) {
        NetClientStatus st;
        if (w->cl[i].vanished) continue;
        netClientGetStatus(w->cl[i].c, &st);
        if (st.state != NCS_LOBBY || !st.lobbyValid || st.lobby.num_players != want) return 0;
    }
    return 1;
}

static int predAllConnected(World *w, void *arg)
{
    int i;
    (void)arg;
    for (i = 0; i < w->ncl; i++) {
        NetClientStatus st;
        netClientGetStatus(w->cl[i].c, &st);
        if (st.state != NCS_CONNECTED) return 0;
    }
    return 1;
}

static int predFrames(World *w, void *arg)
{
    uint32_t want = (uint32_t)(uintptr_t)arg;
    int i;
    for (i = 0; i < w->ncl; i++) {
        if (w->cl[i].vanished || w->cl[i].ended) continue;
        if (w->cl[i].frame < want) return 0;
    }
    return 1;
}

static int predAllEnded(World *w, void *arg)
{
    int i;
    (void)arg;
    for (i = 0; i < w->ncl; i++) {
        if (w->cl[i].vanished) continue;
        if (!w->cl[i].ended) return 0;
    }
    return 1;
}

static int predLobbyWaitingAgain(World *w, void *arg)
{
    int i;
    (void)arg;
    for (i = 0; i < w->ncl; i++) {
        NetClientStatus st;
        if (w->cl[i].vanished) continue;
        netClientGetStatus(w->cl[i].c, &st);
        if (!st.lobbyValid || st.lobby.state != NLS_WAITING) return 0;
    }
    return 1;
}

/* Compare toy states of all live clients over [0, upto). */
static int compareStates(World *w, uint32_t upto, const char *what)
{
    uint32_t f;
    int i, mismatches = 0, compared = 0;
    for (f = 0; f < upto && f < MAX_TEST_FRAMES; f++) {
        int ref = -1;
        for (i = 0; i < w->ncl; i++) {
            if (!w->cl[i].haveState[f]) continue;
            if (ref < 0) {
                ref = i;
                continue;
            }
            compared++;
            if (w->cl[i].states[f] != w->cl[ref].states[f]) {
                if (mismatches < 3) {
                    printf("  [%s] frame %u: %s state %016llx != %s state %016llx\n", what, f,
                           w->cl[i].label, (unsigned long long)w->cl[i].states[f],
                           w->cl[ref].label, (unsigned long long)w->cl[ref].states[f]);
                }
                mismatches++;
            }
        }
    }
    if (g_verbose) printf("  [%s] compared %d frame-pairs, %d mismatches\n", what, compared, mismatches);
    return mismatches;
}

static int anyDesync(World *w)
{
    int i, n = 0;
    for (i = 0; i < w->ncl; i++) {
        NetClientStatus st;
        if (w->cl[i].vanished) continue;
        netClientGetStatus(w->cl[i].c, &st);
        if (st.desync) n++;
    }
    return n;
}

static int predFirstInLobby(World *w, void *arg)
{
    NetClientStatus st;
    (void)arg;
    netClientGetStatus(w->cl[0].c, &st);
    return st.state == NCS_LOBBY && st.lobbyValid && st.lobby.num_players == 1;
}

static int predFirstInLobbyAny(World *w, void *arg)
{
    NetClientStatus st;
    (void)arg;
    netClientGetStatus(w->cl[0].c, &st);
    return st.state == NCS_LOBBY && st.lobbyValid;
}

/* P1 joins first (so it is the lobby leader), then everyone else. */
static int connectAllDirect(World *w)
{
    int i;
    netClientConnect(w->cl[0].c, &w->hostAddr, 0);
    if (!worldRunUntil(w, predFirstInLobby, NULL, 20000000)) return 0;
    for (i = 1; i < w->ncl; i++) netClientConnect(w->cl[i].c, &w->hostAddr, 0);
    return worldRunUntil(w, predAllInLobby, (void *)(intptr_t)w->ncl, 30000000);
}

static void allReadyAndStart(World *w, int leader)
{
    int i;
    for (i = 0; i < w->ncl; i++) {
        NetClientStatus st;
        if (w->cl[i].vanished) continue;
        netClientGetStatus(w->cl[i].c, &st);
        if (st.slot >= 0) {
            const NetPlayerInfo *p = &st.lobby.players[st.slot];
            netClientSetPlayer(w->cl[i].c, p->character, p->handicap, p->control, p->team, 1);
        }
    }
    {
        uint64_t end = w->net.now + 2000000;
        while (w->net.now < end) worldStep(w, 250);   /* let PLAYER_SETs land */
    }
    netClientStartMatch(w->cl[leader].c);
}

static void printNetStats(World *w, const char *tag)
{
    int i;
    printf("  %s: sim %.1fs, %u pkts sent, %u dropped;", tag, (double)w->net.now / 1e6, w->net.sent, w->net.dropped);
    for (i = 0; i < w->ncl; i++) {
        printf(" %s f=%u stall=%u", w->cl[i].label, w->cl[i].frame, w->cl[i].stallFrames);
    }
    printf("\n");
}

/* ------------------------------------------------------------------------ */
/* Tests                                                                     */
/* ------------------------------------------------------------------------ */

static void testCodecs(void)
{
    uint8_t buf[512];
    NetW w;
    NetR r;
    NetBundle b, b2;
    NetLobbyState l, l2;
    int i;
    printf("codecs\n");
    memset(&b, 0, sizeof(b));
    b.frame = 123456;
    b.present = 0x0B;
    b.disc = 0x04;
    b.flags = NB_ABORT;
    for (i = 0; i < 4; i++) {
        Rng rg;
        rg.s = 99 + (uint64_t)i;
        makeInput(&rg, &b.rec[i]);
    }
    memset(&b.rec[2], 0, sizeof(b.rec[2]));   /* not present: must stay zero */
    nwInit(&w, buf, sizeof(buf));
    netEncBundle(&w, &b);
    CHECK(!w.overflow, "bundle encode overflow");
    nrInit(&r, buf, w.len);
    CHECK(netDecBundle(&r, &b2) == 0 && nrLeft(&r) == 0, "bundle decode");
    CHECK(b2.frame == b.frame && b2.present == b.present && b2.disc == b.disc && b2.flags == b.flags, "bundle header");
    for (i = 0; i < 4; i++) {
        if (b.present & (1u << i)) CHECK(netInputRecEq(&b.rec[i], &b2.rec[i]), "record %d roundtrip", i);
    }
    /* truncation is an error, never a crash */
    for (i = 0; i < w.len; i++) {
        nrInit(&r, buf, i);
        CHECK(netDecBundle(&r, &b2) != 0, "truncated bundle (%d bytes) accepted", i);
    }

    /* Hostile floats (D416): the game wraps angles with a while loop, so an
     * infinite or huge turn would hang every PC. Decoded values are finite
     * and bounded; ordinary values pass through unchanged. */
    {
        NetInputRec h, h2;
        const float inf = (float)HUGE_VAL;
        const float nan = inf - inf;
        memset(&h, 0, sizeof(h));
        h.flags = NIR_LOOK | NIR_CROSS | NIR_PDTURN;
        h.look_dtheta = inf; h.look_dverta = -1e30f;
        h.cross_x = nan; h.cross_y = -inf;
        h.gun_az = 1e9f; h.gun_turn = -3.0f;
        h.pdturn_x = 0.5f; h.pdturn_y = 1e20f;
        nwInit(&w, buf, sizeof(buf));
        netEncInputRec(&w, &h);
        nrInit(&r, buf, w.len);
        CHECK(netDecInputRec(&r, &h2) == 0, "hostile record decodes");
        CHECK(h2.look_dtheta == 0.0f && h2.look_dverta == -3600.0f, "look: inf -> 0, -1e30 clamped (%g %g)",
              h2.look_dtheta, h2.look_dverta);
        CHECK(h2.cross_x == 0.0f && h2.cross_y == 0.0f, "crosshair: NaN / -inf -> 0");
        CHECK(h2.gun_az == 16.0f && h2.gun_turn == -3.0f, "gun pose clamped, ordinary kept");
        CHECK(h2.pdturn_x == 0.5f && h2.pdturn_y == 4.0f, "PD turn clamped, ordinary kept");
        /* decoding is idempotent: the host re-encodes what it decoded */
        nwInit(&w, buf, sizeof(buf));
        netEncInputRec(&w, &h2);
        nrInit(&r, buf, w.len);
        CHECK(netDecInputRec(&r, &h) == 0 && netInputRecEq(&h, &h2), "sanitised record round trips unchanged");
    }

    memset(&l, 0, sizeof(l));
    l.revision = 7;
    l.lobby_id = 42;
    netStrCopy(l.code, sizeof(l.code), "ABC234");
    netStrCopy(l.name, sizeof(l.name), "Test\x01Lobby");
    l.max_players = 4;
    l.num_players = 2;
    l.players[0].used = 1;
    netStrCopy(l.players[0].name, NET_NAME_MAX, "Leigh");
    l.players[1].used = 1;
    l.players[1].character = 63;
    netStrCopy(l.players[1].name, NET_NAME_MAX, "   ");
    ngDefaultSettings(&l.settings);
    nwInit(&w, buf, sizeof(buf));
    netEncLobby(&w, &l);
    nrInit(&r, buf, w.len);
    CHECK(netDecLobby(&r, &l2) == 0, "lobby decode");
    CHECK(strcmp(l2.name, "Test?Lobby") == 0, "lobby name sanitized: '%s'", l2.name);
    CHECK(strcmp(l2.players[1].name, "Player") == 0, "blank name -> fallback: '%s'", l2.players[1].name);
    CHECK(l2.players[1].character == 63 && strcmp(l2.code, "ABC234") == 0, "lobby fields");

    /* settings normalization mirrors front.c */
    {
        NetSettings s;
        ngDefaultSettings(&s);
        s.scenario = NG_SCENARIO_YOLT;
        s.length = 2;
        ngNormalizeSettings(&s);
        CHECK(s.length == NG_LENGTH_LAST, "YOLT forces last-alive");
        s.scenario = NG_SCENARIO_MWTGG;
        s.weapons = 3;
        ngNormalizeSettings(&s);
        CHECK(s.weapons == NG_WEAPONS_GOLDEN && s.length == NG_LENGTH_10MIN, "MWTGG forces golden gun / caps length");
        s.scenario = NG_SCENARIO_TLD;
        s.length = 5;
        ngNormalizeSettings(&s);
        CHECK(s.length == NG_LENGTH_10MIN, "flag tag caps length to time limits");
    }
    {
        NetSettings s;
        NetPlayerInfo p[4];
        char why[96];
        memset(p, 0, sizeof(p));
        ngDefaultSettings(&s);
        s.stage = 11;   /* Egyptian: 2 players max */
        CHECK(ngValidateMatch(&s, p, 3, why, sizeof(why)) != 0, "Egyptian with 3 players rejected");
        s.stage = 0;
        s.scenario = NG_SCENARIO_2V2;
        CHECK(ngValidateMatch(&s, p, 3, why, sizeof(why)) != 0, "2v2 with 3 players rejected");
        ngDefaultTeams(s.scenario, p, 4);
        CHECK(ngValidateMatch(&s, p, 4, why, sizeof(why)) == 0, "2v2 default teams valid (%s)", why);
        p[0].team = 1;
        CHECK(ngValidateMatch(&s, p, 4, why, sizeof(why)) != 0, "3-1 split rejected for 2v2");
        for (i = 0; i < 200; i++) {
            int st = ngResolveStage(0, 3, (uint32_t)i * 2654435761u);
            CHECK(st >= 1 && st < NG_NUM_STAGES && ngStageMaxPlayers(st) >= 3, "random stage %d fits 3 players", st);
        }
    }
}

/* Full lockstep match through a direct host. */
static void testDirectMatch(const char *name, int nclients, double loss, double dup,
                            uint32_t latMin, uint32_t latMax, uint32_t frames, uint64_t seed)
{
    World w;
    int ok, i;
    printf("%s\n", name);
    worldInit(&w, 0, nclients, seed, loss, dup, latMin, latMax);
    ok = connectAllDirect(&w);
    CHECK(ok, "clients did not all reach the lobby");
    if (!ok) { worldFree(&w); return; }
    {
        NetClientStatus st;
        netClientGetStatus(w.cl[0].c, &st);
        CHECK(st.isLeader && st.slot == 0, "first joiner is leader");
        netClientGetStatus(w.cl[1].c, &st);
        CHECK(!st.isLeader, "second joiner is not leader");
    }
    allReadyAndStart(&w, 0);
    ok = worldRunUntil(&w, predFrames, (void *)(uintptr_t)frames, (uint64_t)frames * 40000 + 30000000);
    CHECK(ok, "match did not reach frame %u", frames);
    printNetStats(&w, "stats");
    CHECK(compareStates(&w, frames, name) == 0, "lockstep states diverged");
    CHECK(anyDesync(&w) == 0, "false desync reported");
    for (i = 0; i < nclients; i++) {
        CHECK(w.cl[i].matchDelay >= 2 && w.cl[i].matchDelay <= NET_MAX_DELAY, "delay %d out of range", w.cl[i].matchDelay);
    }
    worldFree(&w);
}

static void testDesyncDetected(void)
{
    World w;
    int i, ok;
    printf("desync detection\n");
    worldInit(&w, 0, 3, 77, 0.01, 0.0, 15000, 40000);
    ok = connectAllDirect(&w);
    CHECK(ok, "lobby");
    (void)i;
    w.cl[2].injectDesyncAt = 400;
    allReadyAndStart(&w, 0);
    ok = worldRunUntil(&w, predFrames, (void *)(uintptr_t)700, 60000000);
    CHECK(ok, "frames");
    {
        uint64_t end = w.net.now + 3000000;
        while (w.net.now < end) worldStep(&w, 250);
    }
    CHECK(anyDesync(&w) == 3, "desync reported to %d/3 clients", anyDesync(&w));
    worldFree(&w);
}

static void testDisconnect(void)
{
    World w;
    int i, ok;
    uint32_t f, discFrame = 0;
    printf("disconnect mid-match\n");
    worldInit(&w, 0, 3, 1234, 0.02, 0.01, 20000, 50000);
    ok = connectAllDirect(&w);
    CHECK(ok, "lobby");
    (void)i;
    allReadyAndStart(&w, 0);
    ok = worldRunUntil(&w, predFrames, (void *)(uintptr_t)300, 60000000);
    CHECK(ok, "reach 300");
    /* P3 vanishes without a goodbye (crash / cable pulled). */
    w.cl[2].vanished = 1;
    w.net.blackhole[w.cl[2].ep.idx] = 1;
    ok = worldRunUntil(&w, predFrames, (void *)(uintptr_t)1500, 120000000);
    CHECK(ok, "match stalled after a client vanished");
    printNetStats(&w, "stats");
    CHECK(compareStates(&w, 1500, "disconnect") == 0, "survivors diverged");
    {
        /* Lobby slots follow JOIN arrival order, not client index. */
        uint8_t bit = (uint8_t)(1u << w.cl[2].slot);
        for (f = 0; f < 1500; f++) {
            if (w.cl[0].haveState[f] && (w.cl[0].discAt[f] & bit)) {
                discFrame = f;
                break;
            }
        }
        CHECK(discFrame > 0, "vanished client's slot %d never marked disconnected", w.cl[2].slot);
        if (discFrame) {
            CHECK(w.cl[1].discAt[discFrame] & bit, "P2 saw the disconnect at a different frame");
            CHECK(!(w.cl[1].discAt[discFrame - 1] & bit), "disconnect frame not identical");
            if (g_verbose) printf("  slot %d neutral from frame %u\n", w.cl[2].slot, discFrame);
        }
    }
    worldFree(&w);
}

static void testAbort(void)
{
    World w;
    int i, ok;
    printf("leader abort\n");
    worldInit(&w, 0, 4, 555, 0.02, 0.0, 10000, 60000);
    ok = connectAllDirect(&w);
    CHECK(ok, "lobby");
    allReadyAndStart(&w, 0);
    ok = worldRunUntil(&w, predFrames, (void *)(uintptr_t)200, 60000000);
    CHECK(ok, "reach 200");
    netClientAbortMatch(w.cl[0].c);
    ok = worldRunUntil(&w, predAllEnded, NULL, 30000000);
    CHECK(ok, "not everyone left after abort");
    for (i = 1; i < 4; i++) {
        CHECK(w.cl[i].endFrame == w.cl[0].endFrame, "%s ended at %u, P1 at %u", w.cl[i].label, w.cl[i].endFrame, w.cl[0].endFrame);
    }
    CHECK(compareStates(&w, w.cl[0].endFrame, "abort") == 0, "diverged before abort");
    ok = worldRunUntil(&w, predLobbyWaitingAgain, NULL, 40000000);
    CHECK(ok, "lobby did not return to waiting after the match");
    /* A second match in the same lobby works. */
    for (i = 0; i < 4; i++) {
        w.cl[i].inMatch = 0;
        w.cl[i].ended = 0;
    }
    allReadyAndStart(&w, 0);
    ok = worldRunUntil(&w, predFrames, (void *)(uintptr_t)300, 60000000);
    CHECK(ok, "second match did not run");
    CHECK(compareStates(&w, 300, "rematch") == 0, "rematch diverged");
    worldFree(&w);
}

/* The ordinary end of a match (D416): every player's game leaves the stage
 * after the results screen and reports MATCH_END (or one quits with
 * MATCH_LEAVE). The host must close the match and put the lobby back to
 * waiting -- netfuzz found its net thread spinning forever in matchAssemble
 * here instead (no slot left active, so nothing ended the loop). */
static void testMatchEnds(void)
{
    World w;
    int i, ok, k;
    printf("match ends normally\n");
    for (k = 0; k < 2; k++) {
        worldInit(&w, 0, 3, 777 + (uint64_t)k, 0.01, 0.0, 5000, 30000);
        ok = connectAllDirect(&w);
        CHECK(ok, "lobby");
        if (!ok) {
            worldFree(&w);
            continue;
        }
        allReadyAndStart(&w, 0);
        ok = worldRunUntil(&w, predFrames, (void *)(uintptr_t)240, 60000000);
        CHECK(ok, "reach 240");
        /* results screens of different lengths; in the second round one
         * player quits the match instead */
        for (i = 0; i < w.ncl; i++) {
            uint64_t end = w.net.now + 300000;
            if (k == 1 && i == 1) netClientMatchLeave(w.cl[i].c);
            w.cl[i].ended = 1;
            w.cl[i].endFrame = w.cl[i].frame;
            while (w.net.now < end) worldStep(&w, 250);
        }
        ok = worldRunUntil(&w, predLobbyWaitingAgain, NULL, 10000000);
        CHECK(ok, "the lobby did not return to waiting after everyone finished (round %d)", k + 1);
        {
            NetHostStats hs;
            netHostGetStats(w.host, &hs);
            CHECK(hs.matches == 0, "the host still has %d match(es) running", hs.matches);
        }
        worldFree(&w);
    }
}

static int predListed(World *w, void *arg)
{
    NetClientStatus st;
    (void)arg;
    netClientGetStatus(w->cl[2].c, &st);
    return st.listCount > 0;
}

static void testMatchmaking(void)
{
    World w;
    int i, ok;
    NetClientStatus st;
    char code[NET_CODE_LEN + 1];
    printf("matchmaking server\n");
    worldInit(&w, 1, 4, 4242, 0.03, 0.01, 20000, 70000);
    for (i = 0; i < 4; i++) netClientConnect(w.cl[i].c, &w.hostAddr, 1);
    ok = worldRunUntil(&w, predAllConnected, NULL, 20000000);
    CHECK(ok, "server sessions");

    /* P1 creates a private lobby; P2 joins with the code. */
    netClientCreateLobby(w.cl[0].c, "Leigh's game", 0, 4, 0);
    {
        uint64_t end = w.net.now + 3000000;
        while (w.net.now < end) worldStep(&w, 250);
    }
    netClientGetStatus(w.cl[0].c, &st);
    CHECK(st.state == NCS_LOBBY && st.lobbyValid, "create lobby");
    netStrCopy(code, sizeof(code), st.lobby.code);
    CHECK(strlen(code) == NET_CODE_LEN, "lobby code '%s'", code);
    {
        char lower[NET_CODE_LEN + 1];
        int k;
        for (k = 0; k <= NET_CODE_LEN; k++) lower[k] = (code[k] >= 'A' && code[k] <= 'Z') ? (char)(code[k] + 32) : code[k];
        netClientJoinLobby(w.cl[1].c, 0, lower);   /* codes are case-insensitive */
    }
    /* P3 lists: the private lobby must NOT be listed. */
    netClientRequestList(w.cl[2].c);
    {
        uint64_t end = w.net.now + 3000000;
        while (w.net.now < end) worldStep(&w, 250);
    }
    netClientGetStatus(w.cl[1].c, &st);
    CHECK(st.state == NCS_LOBBY && st.lobby.num_players == 2, "join by code (%d players)", st.lobby.num_players);
    netClientGetStatus(w.cl[2].c, &st);
    CHECK(st.listCount == 0, "private lobby was listed");

    /* P3 + P4 quick match: they make / share a public quick lobby. */
    netClientQuickMatch(w.cl[2].c);
    {
        uint64_t end = w.net.now + 2000000;
        while (w.net.now < end) worldStep(&w, 250);
    }
    netClientQuickMatch(w.cl[3].c);
    {
        uint64_t end = w.net.now + 3000000;
        while (w.net.now < end) worldStep(&w, 250);
    }
    {
        NetClientStatus s3, s4;
        netClientGetStatus(w.cl[2].c, &s3);
        netClientGetStatus(w.cl[3].c, &s4);
        CHECK(s3.state == NCS_LOBBY && s4.state == NCS_LOBBY && s3.lobby.lobby_id == s4.lobby.lobby_id,
              "quick match paired P3+P4");
        CHECK(s3.lobby.flags & NL_QUICK, "quick lobby flagged");
    }

    /* Lobby rules: non-leader can't start; start with an unready player fails. */
    netClientStartMatch(w.cl[1].c);
    netClientStartMatch(w.cl[0].c);
    {
        uint64_t end = w.net.now + 2000000;
        while (w.net.now < end) worldStep(&w, 250);
    }
    netClientGetStatus(w.cl[0].c, &st);
    CHECK(st.lobby.state == NLS_WAITING, "match started with an unready player");
    CHECK(st.lastError[0] != 0, "no error for unready start");

    /* Settings: leader picks Egyptian + 2v2 -> invalid for 2 players; then fix. */
    {
        NetSettings s = st.lobby.settings;
        s.scenario = NG_SCENARIO_2V2;
        netClientSetSettings(w.cl[0].c, &s);
    }
    netClientSetPlayer(w.cl[1].c, 1, 5, 0, 0, 1);
    {
        uint64_t end = w.net.now + 2000000;
        while (w.net.now < end) worldStep(&w, 250);
    }
    netClientStartMatch(w.cl[0].c);
    {
        uint64_t end = w.net.now + 2000000;
        while (w.net.now < end) worldStep(&w, 250);
    }
    netClientGetStatus(w.cl[0].c, &st);
    CHECK(st.lobby.state == NLS_WAITING, "2v2 started with 2 players");
    {
        NetSettings s = st.lobby.settings;
        s.scenario = NG_SCENARIO_NORMAL;
        s.stage = 11;   /* Egyptian, 2 max: fine for 2 */
        netClientSetSettings(w.cl[0].c, &s);
    }
    {
        uint64_t end = w.net.now + 2000000;
        while (w.net.now < end) worldStep(&w, 250);
    }
    netClientStartMatch(w.cl[0].c);

    /* Quick lobby autostarts once both are ready. */
    netClientSetPlayer(w.cl[2].c, 2, 5, 0, 0, 1);
    netClientSetPlayer(w.cl[3].c, 3, 5, 1, 0, 1);
    ok = worldRunUntil(&w, predFrames, (void *)(uintptr_t)600, 60000000);
    CHECK(ok, "both server matches should run to frame 600");
    printNetStats(&w, "stats");
    /* Two independent matches: compare within each pair. */
    {
        uint32_t f;
        int bad = 0;
        for (f = 0; f < 600; f++) {
            if (w.cl[0].haveState[f] && w.cl[1].haveState[f] && w.cl[0].states[f] != w.cl[1].states[f]) bad++;
            if (w.cl[2].haveState[f] && w.cl[3].haveState[f] && w.cl[2].states[f] != w.cl[3].states[f]) bad++;
        }
        CHECK(bad == 0, "server-hosted matches diverged (%d)", bad);
    }
    CHECK(anyDesync(&w) == 0, "false desync on server");
    {
        NetHostStats hs;
        netHostGetStats(w.host, &hs);
        CHECK(hs.lobbies == 2 && hs.matches == 2, "server tracks 2 lobbies / 2 matches (%d/%d)", hs.lobbies, hs.matches);
    }
    (void)predListed;
    worldFree(&w);
}

static void testListing(void)
{
    World w;
    int ok;
    NetClientStatus st;
    printf("lobby listing\n");
    worldInit(&w, 1, 3, 99, 0.0, 0.0, 5000, 10000);
    netClientConnect(w.cl[0].c, &w.hostAddr, 1);
    netClientConnect(w.cl[1].c, &w.hostAddr, 1);
    netClientConnect(w.cl[2].c, &w.hostAddr, 1);
    ok = worldRunUntil(&w, predAllConnected, NULL, 10000000);
    CHECK(ok, "connect");
    netClientCreateLobby(w.cl[0].c, "Public A", 1, 4, 0);
    netClientCreateLobby(w.cl[1].c, "Private B", 0, 2, 0);
    {
        uint64_t end = w.net.now + 1000000;
        while (w.net.now < end) worldStep(&w, 250);
    }
    netClientRequestList(w.cl[2].c);
    ok = worldRunUntil(&w, predListed, NULL, 5000000);
    CHECK(ok, "no list");
    netClientGetStatus(w.cl[2].c, &st);
    CHECK(st.listCount == 1 && strcmp(st.list[0].name, "Public A") == 0, "list shows only the public lobby");
    if (st.listCount == 1) {
        netClientJoinLobby(w.cl[2].c, st.list[0].lobby_id, NULL);
        {
            uint64_t end = w.net.now + 2000000;
            while (w.net.now < end) worldStep(&w, 250);
        }
        netClientGetStatus(w.cl[2].c, &st);
        CHECK(st.state == NCS_LOBBY && st.lobby.num_players == 2, "join from list");
    }
    worldFree(&w);
}

static void testBuildMismatch(void)
{
    World w;
    NetClientStatus st;
    printf("build mismatch rejected\n");
    worldInit(&w, 0, 1, 5, 0.0, 0.0, 1000, 2000);
    netClientDestroy(w.cl[0].c);
    {
        NetClientConfig cc;
        memset(&cc, 0, sizeof(cc));
        netStrCopy(cc.name, NET_NAME_MAX, "Other");
        netStrCopy(cc.buildId, NET_BUILDID_MAX, "a-different-build");
        cc.log = logFn;
        cc.logCtx = (void *)"P1";
        w.cl[0].c = netClientCreate(&cc, simTransport(&w.cl[0].ep));
    }
    netClientConnect(w.cl[0].c, &w.hostAddr, 0);
    {
        uint64_t end = w.net.now + 2000000;
        while (w.net.now < end) worldStep(&w, 250);
    }
    netClientGetStatus(w.cl[0].c, &st);
    CHECK(st.state == NCS_FAILED && strstr(st.lastError, "build") != NULL, "mismatched build accepted (%s)", st.lastError);
    worldFree(&w);
}

/* ------------------------------------------------------------------------ */
/* Real UDP loopback smoke test                                              */
/* ------------------------------------------------------------------------ */

/* STUN Binding (D414): RFC 5769 §2.2's sample IPv4 response (XOR-MAPPED-
 * ADDRESS 192.0.2.1:32853, plus SOFTWARE / MESSAGE-INTEGRITY / FINGERPRINT
 * attributes the parser must step over), our request format, rejection of
 * other transactions and truncation, and the plain MAPPED-ADDRESS form. */
static void testStun(void)
{
    static const uint8_t vec[] = {
        0x01, 0x01, 0x00, 0x3c, 0x21, 0x12, 0xa4, 0x42,
        0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae,
        0x80, 0x22, 0x00, 0x0b, 0x74, 0x65, 0x73, 0x74, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x20,
        0x00, 0x20, 0x00, 0x08, 0x00, 0x01, 0xa1, 0x47, 0xe1, 0x12, 0xa6, 0x43,
        0x00, 0x08, 0x00, 0x14, 0x2b, 0x91, 0xf5, 0x99, 0xfd, 0x9e, 0x90, 0xc3, 0x8c, 0x74, 0x89, 0xf9,
        0x2a, 0xf9, 0xba, 0x53, 0xf0, 0x6b, 0xe7, 0xd7,
        0x80, 0x28, 0x00, 0x04, 0xc0, 0x7d, 0x4c, 0x96,
    };
    NetStunTx tx, other;
    NetAddr a;
    uint8_t req[64], plain[32];
    int n;
    printf("STUN codec\n");
    memcpy(tx.id, vec + 8, 12);
    CHECK(sizeof(vec) == 80, "vector length %d", (int)sizeof(vec));
    CHECK(netStunIsMessage(vec, (int)sizeof(vec)), "vector is a STUN message");
    memset(&a, 0, sizeof(a));
    CHECK(netStunParseResponse(vec, (int)sizeof(vec), &tx, &a) == 0 && a.ip == 0xC0000201u && a.port == 32853,
          "RFC 5769 mapped address %08x:%u", (unsigned)a.ip, (unsigned)a.port);
    memcpy(other.id, vec + 8, 12);
    other.id[0] ^= 1;
    CHECK(netStunParseResponse(vec, (int)sizeof(vec), &other, &a) != 0, "other transaction rejected");
    CHECK(netStunParseResponse(vec, 60, &tx, &a) != 0, "truncated rejected");
    CHECK(!netStunIsMessage((const uint8_t *)"GE7N....................", 24), "game packet is not STUN");

    n = netStunBuildRequest(req, (int)sizeof(req), &other);
    CHECK(n == 20 && req[0] == 0x00 && req[1] == 0x01 && req[2] == 0 && req[3] == 0 &&
          req[4] == 0x21 && req[5] == 0x12 && req[6] == 0xa4 && req[7] == 0x42 &&
          memcmp(req + 8, other.id, 12) == 0, "binding request format");
    CHECK(netStunBuildRequest(req, 10, &other) < 0, "small buffer refused");

    /* MAPPED-ADDRESS only (old servers): 203.0.113.7:4500 */
    memset(plain, 0, sizeof(plain));
    plain[0] = 0x01; plain[1] = 0x01; plain[3] = 12;
    plain[4] = 0x21; plain[5] = 0x12; plain[6] = 0xa4; plain[7] = 0x42;
    memcpy(plain + 8, tx.id, 12);
    plain[20] = 0x00; plain[21] = 0x01; plain[23] = 8;
    plain[25] = 0x01; plain[26] = 0x11; plain[27] = 0x94;
    plain[28] = 203; plain[29] = 0; plain[30] = 113; plain[31] = 7;
    CHECK(netStunParseResponse(plain, 32, &tx, &a) == 0 && a.ip == netAddrMake(203, 0, 113, 7, 0).ip &&
          a.port == 4500, "MAPPED-ADDRESS fallback");
}

/* Directory protocol (D414): every message survives encode -> decode, and a
 * truncated / over-long one is refused rather than half-read. The JS side
 * (cloudflare/src/protocol.js) is tested against the same layouts. */
static void testDirProto(void)
{
    uint8_t buf[NDP_MAX_MSG];
    NetW w;
    NetR r;
    printf("directory protocol codec\n");
    {
        NdpHost h, h2;
        memset(&h, 0, sizeof(h));
        h.lobbyId = 0x01020304;
        netStrCopy(h.token, sizeof(h.token), "0123456789abcdef0123456789abcdef");
        netStrCopy(h.code, sizeof(h.code), "ABC234");
        netStrCopy(h.name, sizeof(h.name), "Bond's game");
        h.flags = NDPF_PUBLIC | NDPF_QUICK;
        h.maxPlayers = 4; h.state = NDPS_PLAYING; h.numPlayers = 2;
        h.scenario = 5; h.stage = 7; h.weapons = 13; h.length = 4; h.matchSec = 321;
        h.ncand = 2;
        h.cand[0] = netAddrMake(203, 0, 113, 9, 51000);
        h.cand[1] = netAddrMake(192, 168, 1, 20, 27007);
        h.nplayers = 2;
        netStrCopy(h.players[0].name, NET_NAME_MAX, "Bond");
        h.players[0].character = 63; h.players[0].team = 1; h.players[0].ready = 1;
        netStrCopy(h.players[1].name, NET_NAME_MAX, "Natalya");
        nwInit(&w, buf, sizeof(buf));
        ndpEncHost(&w, &h);
        CHECK(!w.overflow && buf[0] == NDP_HOST, "host encode");
        nrInit(&r, buf + 1, w.len - 1);
        CHECK(ndpDecHost(&r, &h2) == 0 && h2.lobbyId == h.lobbyId && !strcmp(h2.token, h.token) &&
              !strcmp(h2.code, "ABC234") && !strcmp(h2.name, "Bond's game") && h2.flags == h.flags &&
              h2.state == NDPS_PLAYING && h2.matchSec == 321 && h2.ncand == 2 && h2.weapons == 13 && h2.length == 4 &&
              netAddrEq(&h2.cand[1], &h.cand[1]) && h2.nplayers == 2 && h2.players[0].character == 63 &&
              h2.players[0].team == 1 && !strcmp(h2.players[1].name, "Natalya"), "host round trip");
        nrInit(&r, buf + 1, w.len - 6);
        CHECK(ndpDecHost(&r, &h2) != 0, "truncated host refused");
    }
    {
        NdpListed l, l2;
        memset(&l, 0, sizeof(l));
        l.online = 17; l.lobbies = 3; l.matches = 1; l.n = 2;
        l.e[0].id = 7; netStrCopy(l.e[0].name, NET_LOBBY_NAME_MAX, "Quick Match");
        netStrCopy(l.e[0].hostName, NET_NAME_MAX, "Trev"); netStrCopy(l.e[0].code, sizeof(l.e[0].code), "XYZ789");
        l.e[0].flags = NDPF_PUBLIC | NDPF_QUICK; l.e[0].numPlayers = 3; l.e[0].maxPlayers = 4;
        netStrCopy(l.e[0].country, NDP_COUNTRY_MAX, "GB");
        l.e[1].id = 9; l.e[1].state = NDPS_PLAYING; l.e[1].stage = 11; l.e[1].weapons = 12; l.e[1].length = 6;
        nwInit(&w, buf, sizeof(buf));
        ndpEncListed(&w, &l);
        nrInit(&r, buf + 1, w.len - 1);
        CHECK(buf[0] == NDP_LISTED && ndpDecListed(&r, &l2) == 0 && l2.online == 17 && l2.n == 2 &&
              l2.e[0].id == 7 && !strcmp(l2.e[0].code, "XYZ789") && !strcmp(l2.e[0].country, "GB") &&
              l2.e[0].numPlayers == 3 && l2.e[1].state == NDPS_PLAYING && l2.e[1].stage == 11 &&
              l2.e[1].weapons == 12 && l2.e[1].length == 6 &&
              !strcmp(l2.e[1].name, "Lobby"), "listed round trip (+ empty name fallback)");
        buf[1 + 8] = NDP_LIST_MAX + 1;   /* entry count past the cap (after 4 x u16 counts) */
        nrInit(&r, buf + 1, w.len - 1);
        CHECK(ndpDecListed(&r, &l2) != 0, "over-long list refused");
    }
    {
        NdpJoin j, j2;
        NdpJoinInfo ji, ji2;
        NdpJoinReq jr, jr2;
        NdpError e, e2;
        NdpResult res, res2;
        NdpHello he, he2;
        uint32_t nonce = 0;
        memset(&j, 0, sizeof(j));
        netStrCopy(j.code, sizeof(j.code), "QWE987");
        j.nonce = 0xCAFEF00D; j.ncand = 3;
        j.cand[0] = netAddrMake(198, 51, 100, 4, 6000);
        j.cand[1] = netAddrMake(0, 0, 0, 0, 1);       /* junk: dropped on decode */
        j.cand[2] = netAddrMake(10, 1, 1, 2, 40001);
        nwInit(&w, buf, sizeof(buf));
        ndpEncJoin(&w, &j);
        nrInit(&r, buf + 1, w.len - 1);
        CHECK(ndpDecJoin(&r, &j2) == 0 && j2.nonce == j.nonce && !strcmp(j2.code, "QWE987") && j2.ncand == 2 &&
              netAddrEq(&j2.cand[1], &j.cand[2]), "join round trip (junk candidate dropped)");
        {
            NdpQuick q, q2;
            memset(&q, 0, sizeof(q));
            q.nonce = 0x11223344; q.lobbyId = 0x0A0B0C0D; q.excludeId = 0x01020304; q.ncand = 1;
            q.prefs.scenario = 3; q.prefs.stage = 7; q.prefs.weapons = NDP_ANY; q.prefs.length = 4; q.prefs.players = 2;
            q.cand[0] = netAddrMake(1, 2, 3, 4, 5);
            nwInit(&w, buf, sizeof(buf));
            ndpEncQuick(&w, &q);
            nrInit(&r, buf + 1, w.len - 1);
            CHECK(ndpDecQuick(&r, &q2) == 0 && q2.nonce == q.nonce && q2.lobbyId == q.lobbyId &&
                  q2.excludeId == q.excludeId && q2.ncand == 1 && q2.prefs.scenario == 3 && q2.prefs.stage == 7 &&
                  q2.prefs.weapons == NDP_ANY && q2.prefs.length == 4 && q2.prefs.players == 2 &&
                  netAddrEq(&q2.cand[0], &q.cand[0]), "quick round trip (with the asker's own lobby)");
        }

        memset(&ji, 0, sizeof(ji));
        ji.nonce = 5; ji.lobbyId = 77; netStrCopy(ji.name, NET_LOBBY_NAME_MAX, "Lobby\x01X");
        netStrCopy(ji.hostName, NET_NAME_MAX, "Host"); ji.ncand = 1; ji.cand[0] = netAddrMake(1, 2, 3, 4, 5);
        nwInit(&w, buf, sizeof(buf));
        ndpEncJoinInfo(&w, &ji);
        nrInit(&r, buf + 1, w.len - 1);
        CHECK(ndpDecJoinInfo(&r, &ji2) == 0 && ji2.lobbyId == 77 && ji2.ncand == 1 &&
              !strcmp(ji2.name, "Lobby?X"), "joininfo round trip (control char sanitised)");

        memset(&jr, 0, sizeof(jr));
        jr.lobbyId = 77; netStrCopy(jr.name, NET_NAME_MAX, "Joiner"); jr.ncand = 1;
        jr.cand[0] = netAddrMake(9, 8, 7, 6, 5432);
        nwInit(&w, buf, sizeof(buf));
        ndpEncJoinReq(&w, &jr);
        nrInit(&r, buf + 1, w.len - 1);
        CHECK(ndpDecJoinReq(&r, &jr2) == 0 && jr2.ncand == 1 && jr2.cand[0].port == 5432, "joinreq round trip");

        memset(&e, 0, sizeof(e));
        e.reqType = NDP_JOIN; e.nonce = 99; e.code = NDPE_FULL; netStrCopy(e.text, sizeof(e.text), "That lobby is full");
        nwInit(&w, buf, sizeof(buf));
        ndpEncError(&w, &e);
        nrInit(&r, buf + 1, w.len - 1);
        CHECK(ndpDecError(&r, &e2) == 0 && e2.nonce == 99 && e2.code == NDPE_FULL && !strcmp(e2.text, e.text),
              "error round trip");

        memset(&res, 0, sizeof(res));
        res.lobbyId = 3; res.durationSec = 600; res.scenario = 2; res.stage = 4; res.n = 2;
        netStrCopy(res.players[0].name, NET_NAME_MAX, "A"); res.players[0].kills = 12; res.players[0].deaths = -1;
        nwInit(&w, buf, sizeof(buf));
        ndpEncResult(&w, &res);
        nrInit(&r, buf + 1, w.len - 1);
        CHECK(ndpDecResult(&r, &res2) == 0 && res2.durationSec == 600 && res2.n == 2 &&
              res2.players[0].kills == 12 && res2.players[0].deaths == -1, "result round trip");

        memset(&he, 0, sizeof(he));
        he.version = NDP_VERSION; netStrCopy(he.build, NET_BUILDID_MAX, "abc|ntsc-final|x86_64-windows|p2|64bit");
        netStrCopy(he.name, NET_NAME_MAX, "Agent");
        nwInit(&w, buf, sizeof(buf));
        ndpEncHello(&w, &he);
        nrInit(&r, buf + 1, w.len - 1);
        CHECK(ndpDecHello(&r, &he2) == 0 && he2.version == NDP_VERSION && !strcmp(he2.build, he.build),
              "hello round trip");

        nwInit(&w, buf, sizeof(buf));
        ndpEncQuickHost(&w, 0x11223344);
        nrInit(&r, buf + 1, w.len - 1);
        CHECK(ndpDecQuickHost(&r, &nonce) == 0 && nonce == 0x11223344, "quickhost round trip");
    }
}

/* Cross-language check, direction service -> game: these hex strings are
 * what tools_pc/netplay/cloudflare/src/protocol.js encodes (pinned by its
 * protocol.test.js); the C decoders must read them back exactly. */
static int hexToBytes(const char *hex, uint8_t *out, int cap)
{
    int n = 0;
    while (hex[0] && hex[1] && n < cap) {
        unsigned v = 0;
        int k;
        for (k = 0; k < 2; k++) {
            char c = hex[k];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else return -1;
        }
        out[n++] = (uint8_t)v;
        hex += 2;
    }
    return n;
}

static void testDirProtoJsVectors(void)
{
    uint8_t b[512];
    NetR r;
    int n;
    printf("directory protocol: service messages encoded by the JS service\n");
    {
        NdpListed l;
        n = hexToBytes("66110003000100020001070000000b517569636b204d6174636804547265760658595a3738390300030401020d04024742", b, sizeof(b));
        nrInit(&r, b + 1, n - 1);
        CHECK(b[0] == NDP_LISTED && ndpDecListed(&r, &l) == 0 && l.online == 17 && l.lobbies == 3 && l.matches == 1 &&
              l.searching == 2 && l.e[0].weapons == 13 && l.e[0].length == 4 &&
              l.n == 1 && l.e[0].id == 7 && !strcmp(l.e[0].name, "Quick Match") && !strcmp(l.e[0].hostName, "Trev") &&
              !strcmp(l.e[0].code, "XYZ789") && l.e[0].flags == 3 && l.e[0].numPlayers == 3 && l.e[0].maxPlayers == 4 &&
              l.e[0].scenario == 1 && l.e[0].stage == 2 && !strcmp(l.e[0].country, "GB"), "JS listed");
    }
    {
        NdpHosted h;
        n = hexToBytes("670d0c0b0a20666666666666666666666666666666666666666666666666666666666666666606484a4b323334", b, sizeof(b));
        nrInit(&r, b + 1, n - 1);
        CHECK(b[0] == NDP_HOSTED && ndpDecHosted(&r, &h) == 0 && h.lobbyId == 0x0a0b0c0d && strlen(h.token) == 32 &&
              h.token[31] == 'f' && !strcmp(h.code, "HJK234"), "JS hosted");
    }
    {
        NdpJoinInfo j;
        n = hexToBytes("69050000004d000000054c6f62627904486f737401040302010500", b, sizeof(b));
        nrInit(&r, b + 1, n - 1);
        CHECK(b[0] == NDP_JOININFO && ndpDecJoinInfo(&r, &j) == 0 && j.nonce == 5 && j.lobbyId == 77 &&
              !strcmp(j.name, "Lobby") && !strcmp(j.hostName, "Host") && j.ncand == 1 && j.cand[0].ip == 0x01020304u &&
              j.cand[0].port == 5, "JS joininfo");
    }
    {
        NdpJoinReq q;
        n = hexToBytes("6c4d000000064a6f696e657201060708093815", b, sizeof(b));
        nrInit(&r, b + 1, n - 1);
        CHECK(b[0] == NDP_JOINREQ && ndpDecJoinReq(&r, &q) == 0 && q.lobbyId == 77 && !strcmp(q.name, "Joiner") &&
              q.ncand == 1 && q.cand[0].ip == 0x09080706u && q.cand[0].port == 5432, "JS joinreq");
    }
    {
        NdpError e;
        n = hexToBytes("6a05630000000411546861742067616d652069732066756c6c", b, sizeof(b));
        nrInit(&r, b + 1, n - 1);
        CHECK(b[0] == NDP_ERROR && ndpDecError(&r, &e) == 0 && e.reqType == NDP_JOIN && e.nonce == 99 &&
              e.code == NDPE_FULL && !strcmp(e.text, "That game is full"), "JS error");
    }
    {
        uint32_t nonce = 0;
        n = hexToBytes("6b44332211", b, sizeof(b));
        nrInit(&r, b + 1, n - 1);
        CHECK(b[0] == NDP_QUICKHOST && ndpDecQuickHost(&r, &nonce) == 0 && nonce == 0x11223344u, "JS quickhost");
    }
    {
        NdpWelcome wl;
        n = hexToBytes("650c00040002000300024869", b, sizeof(b));
        nrInit(&r, b + 1, n - 1);
        CHECK(b[0] == NDP_WELCOME && ndpDecWelcome(&r, &wl) == 0 && wl.online == 12 && wl.lobbies == 4 &&
              wl.matches == 2 && wl.searching == 3 && !strcmp(wl.motd, "Hi"), "JS welcome");
    }
}

/* Direction game -> service: `netplay_selftest --dirproto-vectors` prints
 * these; saved as cloudflare/test/fixtures/c-vectors.txt they are decoded by
 * the JS tests. */
static void printHex(const char *name, const uint8_t *p, int n)
{
    int i;
    printf("%s ", name);
    for (i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
}

static int dirProtoVectors(void)
{
    uint8_t buf[NDP_MAX_MSG];
    NetW w;
    NdpHost h;
    NdpJoin j;
    NdpQuick q;
    NdpResult res;
    NdpHello he;
    NdpPoll po;
    NdpUnhost un;

    memset(&h, 0, sizeof(h));
    h.lobbyId = 0x01020304;
    netStrCopy(h.token, sizeof(h.token), "0123456789abcdef0123456789abcdef");
    netStrCopy(h.code, sizeof(h.code), "ABC234");
    netStrCopy(h.name, sizeof(h.name), "Bond's game");
    h.flags = NDPF_PUBLIC | NDPF_QUICK;
    h.maxPlayers = 4; h.state = NDPS_PLAYING; h.numPlayers = 2; h.scenario = 5; h.stage = 7; h.matchSec = 321;
    h.weapons = 13; h.length = 4;
    h.ncand = 2;
    h.cand[0] = netAddrMake(203, 0, 113, 9, 51000);
    h.cand[1] = netAddrMake(192, 168, 1, 20, 27007);
    h.nplayers = 2;
    netStrCopy(h.players[0].name, NET_NAME_MAX, "Bond");
    h.players[0].character = 63; h.players[0].team = 1; h.players[0].ready = 1;
    netStrCopy(h.players[1].name, NET_NAME_MAX, "Natalya");
    nwInit(&w, buf, sizeof(buf));
    ndpEncHost(&w, &h);
    printHex("host", buf, w.len);

    memset(&j, 0, sizeof(j));
    netStrCopy(j.code, sizeof(j.code), "QWE987");
    j.nonce = 0xCAFEF00D; j.ncand = 3;
    j.cand[0] = netAddrMake(198, 51, 100, 4, 6000);
    j.cand[1] = netAddrMake(0, 0, 0, 0, 1);
    j.cand[2] = netAddrMake(10, 1, 1, 2, 40001);
    nwInit(&w, buf, sizeof(buf));
    ndpEncJoin(&w, &j);
    printHex("join", buf, w.len);

    memset(&q, 0, sizeof(q));
    q.nonce = 0x11223344; q.lobbyId = 0x0A0B0C0D; q.excludeId = 0x01020304; q.ncand = 1;
    q.prefs.scenario = 3; q.prefs.stage = 7; q.prefs.weapons = NDP_ANY; q.prefs.length = 4; q.prefs.players = 2;
    q.cand[0] = netAddrMake(1, 2, 3, 4, 5);
    nwInit(&w, buf, sizeof(buf));
    ndpEncQuick(&w, &q);
    printHex("quick", buf, w.len);

    memset(&res, 0, sizeof(res));
    res.lobbyId = 3; netStrCopy(res.token, sizeof(res.token), "tok"); res.durationSec = 600; res.scenario = 2;
    res.stage = 4; res.n = 2;
    netStrCopy(res.players[0].name, NET_NAME_MAX, "A"); res.players[0].kills = 12; res.players[0].deaths = -1;
    netStrCopy(res.players[1].name, NET_NAME_MAX, "B"); res.players[1].kills = 1; res.players[1].deaths = 12;
    nwInit(&w, buf, sizeof(buf));
    ndpEncResult(&w, &res);
    printHex("result", buf, w.len);

    memset(&he, 0, sizeof(he));
    he.version = NDP_VERSION;
    netStrCopy(he.build, NET_BUILDID_MAX, "abc|ntsc-final|x86_64-windows|p2|64bit");
    netStrCopy(he.name, NET_NAME_MAX, "Agent");
    nwInit(&w, buf, sizeof(buf));
    ndpEncHello(&w, &he);
    printHex("hello", buf, w.len);

    memset(&po, 0, sizeof(po));
    po.lobbyId = 77; netStrCopy(po.token, sizeof(po.token), "t");
    nwInit(&w, buf, sizeof(buf));
    ndpEncPoll(&w, &po);
    printHex("poll", buf, w.len);

    memset(&un, 0, sizeof(un));
    un.lobbyId = 77; netStrCopy(un.token, sizeof(un.token), "t");
    nwInit(&w, buf, sizeof(buf));
    ndpEncUnhost(&w, &un);
    printHex("unhost", buf, w.len);

    /* Preference parity (D416): the same inputs through ndpNormalizePrefs;
     * the JS tests run them through gamedata.js normalizePrefs and compare.
     * Deterministic pseudo-random inputs, including out-of-range values. */
    {
        static const uint8_t pick[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 99, NDP_ANY };
        uint32_t x = 0x2545F491u;
        int k;
        for (k = 0; k < 64; k++) {
            NdpPrefs in, out;
            uint8_t v[5];
            int j;
            for (j = 0; j < 5; j++) {
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                v[j] = pick[x % (uint32_t)sizeof(pick)];
            }
            in.scenario = v[0]; in.stage = v[1]; in.weapons = v[2]; in.length = v[3]; in.players = v[4];
            out = in;
            ndpNormalizePrefs(&out);
            printf("prefs %d %d %d %d %d -> %d %d %d %d %d\n", in.scenario, in.stage, in.weapons, in.length, in.players,
                   out.scenario, out.stage, out.weapons, out.length, out.players);
        }
    }
    return 0;
}

/* Quick-match preferences (D416): GoldenEye's rules applied to what a
 * searcher asks for, matching, and the rules a searcher hosts with. */
static void testQuickPrefs(void)
{
    NdpPrefs p;
    NetSettings st;
    int maxp;
    char label[96];
    printf("quick-match preferences\n");
    ndpPrefsAny(&p);
    ndpNormalizePrefs(&p);
    CHECK(p.scenario == NDP_ANY && p.stage == NDP_ANY && p.players == NDP_ANY, "any stays any");
    ndpPrefsToRules(&p, &st, &maxp);
    CHECK(st.scenario == NG_SCENARIO_NORMAL && st.stage == NG_STAGE_RANDOM && maxp == 4 && (st.flags & NS_AUTOSTART),
          "any hosts Normal on a random map for 4");

    ndpPrefsAny(&p);
    p.scenario = NG_SCENARIO_2V2; p.players = 2; p.stage = 11;   /* Egyptian holds 2: can't be 2 vs 2 */
    ndpNormalizePrefs(&p);
    CHECK(p.players == 4 && p.stage == NDP_ANY, "2 vs 2 is 4 players; a 2-player map is dropped (%d, %d)", p.players, p.stage);
    ndpPrefsToRules(&p, &st, &maxp);
    CHECK(st.scenario == NG_SCENARIO_2V2 && maxp == 4, "2 vs 2 hosts for exactly 4");

    ndpPrefsAny(&p);
    p.scenario = NG_SCENARIO_2V1; p.stage = 9;   /* Archives holds 3: fine */
    ndpNormalizePrefs(&p);
    ndpPrefsToRules(&p, &st, &maxp);
    CHECK(p.players == 3 && p.stage == 9 && maxp == 3, "2 vs 1 on Archives: 3 players");

    ndpPrefsAny(&p);
    p.length = 7;
    ndpNormalizePrefs(&p);
    CHECK(p.scenario == NG_SCENARIO_YOLT && p.length == NDP_ANY, "last one standing, no mode: YOLT (%d)", p.scenario);
    ndpPrefsAny(&p);
    p.scenario = NG_SCENARIO_MWTGG; p.length = 7;
    ndpNormalizePrefs(&p);
    CHECK(p.scenario == NG_SCENARIO_MWTGG && p.length == NDP_ANY, "a chosen mode wins over last one standing");
    ndpPrefsAny(&p);
    p.scenario = NG_SCENARIO_MWTGG; p.weapons = 1;
    ndpNormalizePrefs(&p);
    ndpPrefsToRules(&p, &st, &maxp);
    CHECK(p.weapons == NDP_ANY && st.weapons == NG_WEAPONS_GOLDEN, "Golden Gun: its own weapons");

    ndpPrefsAny(&p);
    p.scenario = NG_SCENARIO_YOLT; p.length = 2;
    ndpNormalizePrefs(&p);
    ndpPrefsToRules(&p, &st, &maxp);
    CHECK(p.length == NDP_ANY && st.length == NG_LENGTH_LAST, "YOLT: last one standing");

    ndpPrefsAny(&p);
    p.scenario = NG_SCENARIO_TLD; p.length = 5;
    ndpNormalizePrefs(&p);
    CHECK(p.length == NDP_ANY, "Flag Tag lengths stop at 20 minutes");

    ndpPrefsAny(&p);
    p.stage = 11; p.players = 4;
    ndpNormalizePrefs(&p);
    ndpPrefsToRules(&p, &st, &maxp);
    CHECK(p.players == 2 && maxp == 2 && st.stage == 11, "Egyptian caps the size at 2");

    ndpPrefsAny(&p);
    p.scenario = 99; p.stage = 0; p.weapons = 200; p.length = 9; p.players = 7;
    ndpNormalizePrefs(&p);
    CHECK(p.scenario == NDP_ANY && p.stage == NDP_ANY && p.weapons == NDP_ANY && p.length == NDP_ANY &&
          p.players == NDP_ANY, "garbage becomes any (random map is not a preference)");

    ndpPrefsAny(&p);
    p.scenario = NG_SCENARIO_MWTGG; p.stage = 7;
    ndpNormalizePrefs(&p);
    CHECK(ndpPrefsMatch(&p, NG_SCENARIO_MWTGG, 7, 13, 2, 4) && !ndpPrefsMatch(&p, NG_SCENARIO_NORMAL, 7, 13, 2, 4) &&
          !ndpPrefsMatch(&p, NG_SCENARIO_MWTGG, 0, 13, 2, 4), "matching: every field that is not any must agree");
    ndpPrefsLabel(&p, label, sizeof(label));
    CHECK(!strcmp(label, "Golden Gun, Facility"), "label '%s'", label);
    ndpPrefsAny(&p);
    ndpPrefsLabel(&p, label, sizeof(label));
    CHECK(!strcmp(label, "Any game"), "label '%s'", label);
    {
        char tiny[6];
        p.scenario = NG_SCENARIO_LTK; p.stage = 10; p.weapons = 8; p.length = 3; p.players = 3;
        ndpPrefsLabel(&p, tiny, sizeof(tiny));   /* truncation never overruns */
        CHECK(strlen(tiny) < sizeof(tiny), "label truncates safely");
    }
}

/* Online join (D414): a client given several candidate addresses for one
 * host (a dead "public" one first, the real one second) connects through
 * whichever answers and keeps talking to it afterwards. */
static void testMultiCandidateJoin(void)
{
    World w;
    NetAddr cands[2];
    NetClientStatus st;
    char a[32];
    printf("multi-address join (online P2P)\n");
    worldInit(&w, 0, 2, 4242, 0.02, 0.0, 5000, 30000);
    cands[0] = netAddrMake(203, 0, 113, 50, 61000);   /* nobody there */
    cands[1] = w.hostAddr;
    netClientConnectMulti(w.cl[0].c, cands, 2, 0, 24);
    CHECK(worldRunUntil(&w, predFirstInLobby, NULL, 20000000), "joined through the second candidate");
    netClientGetStatus(w.cl[0].c, &st);
    netAddrFormat(&w.hostAddr, a, sizeof(a));
    CHECK(!strcmp(st.hostAddr, a), "locked to the answering address (%s vs %s)", st.hostAddr, a);
    netClientConnect(w.cl[1].c, &w.hostAddr, 0);
    CHECK(worldRunUntil(&w, predAllInLobby, (void *)(intptr_t)2, 20000000), "second player joined");
    worldFree(&w);

    /* Both addresses reach the host (joiner on the host's LAN, or a router
     * that loops the public address back): the JOIN arrives twice, from two
     * source addresses. Still one session, one player. */
    worldInit(&w, 0, 2, 4545, 0.0, 0.0, 5000, 20000);
    {
        NetAddr hostPublic = netAddrMake(198, 51, 100, 9, 27100);
        NetAddr joinerPublic = netAddrMake(198, 51, 100, 77, 40001);
        uint64_t end;
        simAddPath(&w.net, w.cl[0].ep.idx, joinerPublic, w.hostEp.idx, hostPublic);
        cands[0] = hostPublic;
        cands[1] = w.hostAddr;
        int most = 0;
        netClientConnectMulti(w.cl[0].c, cands, 2, 0, 24);
        CHECK(worldRunUntil(&w, predFirstInLobbyAny, NULL, 20000000), "joined with both addresses live");
        end = w.net.now + 3000000;   /* any duplicate JOIN has landed by now */
        while (w.net.now < end) {
            worldStep(&w, 1000);
            netClientGetStatus(w.cl[0].c, &st);
            if (st.lobbyValid && st.lobby.num_players > most) most = st.lobby.num_players;
        }
        CHECK(st.state == NCS_LOBBY && most == 1,
              "one player for one join attempt (state %d, up to %d players)", st.state, most);
        netClientConnect(w.cl[1].c, &w.hostAddr, 0);
        CHECK(worldRunUntil(&w, predAllInLobby, (void *)(intptr_t)2, 20000000), "then a second player joins");
    }
    worldFree(&w);

    /* Every candidate dead: a clear failure, not a hang. */
    worldInit(&w, 0, 1, 4343, 0.0, 0.0, 5000, 10000);
    cands[0] = netAddrMake(203, 0, 113, 51, 61000);
    cands[1] = netAddrMake(198, 51, 100, 52, 62000);
    netClientConnectMulti(w.cl[0].c, cands, 2, 0, 6);
    {
        uint64_t end = w.net.now + 6000000;
        while (w.net.now < end) worldStep(&w, 1000);
    }
    netClientGetStatus(w.cl[0].c, &st);
    CHECK(st.state == NCS_FAILED && strstr(st.lastError, "tried 2 addresses") != NULL,
          "unreachable host reported: state %d '%s'", st.state, st.lastError);
    worldFree(&w);
}

static int predSomeoneLoadedOthersNot(World *w, void *arg)
{
    (void)arg;
    return w->cl[0].loaded && !w->cl[1].loaded && w->cl[0].inMatch;
}

/* Waiting screen data (D414): between MATCH_START and GO the host reports
 * who has finished loading. */
static void testWaitStatus(void)
{
    World w;
    NetClientStatus st;
    int got = 0;
    printf("start-barrier status (waiting screen)\n");
    worldInit(&w, 0, 2, 5151, 0.0, 0.0, 2000, 6000);
    CHECK(connectAllDirect(&w), "lobby");
    allReadyAndStart(&w, 0);
    /* Make P2 a slow loader so there is a window to observe. */
    {
        uint64_t end = w.net.now + 3000000;
        while (w.net.now < end && !w.cl[1].inMatch) worldStep(&w, 250);
        w.cl[1].loadDoneAt = w.net.now + 4000000;
    }
    if (worldRunUntil(&w, predSomeoneLoadedOthersNot, NULL, 4000000)) {
        uint64_t end = w.net.now + 1000000;
        while (w.net.now < end) {
            worldStep(&w, 250);
            netClientGetStatus(w.cl[0].c, &st);
            if (!st.matchGo && st.waitMask == 0x03 && st.loadedMask == (1u << w.cl[0].slot)) got = 1;
        }
    }
    CHECK(got, "P1 saw itself loaded and P2 still loading");
    CHECK(worldRunUntil(&w, predFrames, (void *)(uintptr_t)60, 20000000), "match ran after the slow load");
    netClientGetStatus(w.cl[0].c, &st);
    CHECK(st.matchGo && st.consumed >= 60, "go + frames consumed (%u)", (unsigned)st.consumed);
    worldFree(&w);
}

/* netLocalIPv4: a dotted quad when there is a default route; -1 (never a
 * crash, never traffic) on a machine without one -- both are acceptable. */
static void testLocalAddress(void)
{
    char ip[32];
    int a, b, c, d, r;
    printf("local address\n");
    r = netLocalIPv4(ip, sizeof(ip));
    if (r == 0) {
        CHECK(sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d) == 4 && a > 0 && a < 256, "dotted quad: '%s'", ip);
        printf("  default-route IPv4: %s\n", ip);
    } else {
        CHECK(ip[0] == 0, "empty on failure");
        printf("  no default route (offline) -- ok\n");
    }
}

static void testRealUdp(void)
{
    NetUdp *hs, *cs1, *cs2;
    NetHost *host;
    NetClient *c1, *c2;
    NetHostConfig hc;
    NetClientConfig cc;
    char err[128];
    NetAddr ha;
    uint64_t t0, now, lastStartTry = 0;
    uint32_t f1 = 0, f2 = 0;
    uint64_t s1 = 0, s2 = 0;
    int started = 0, loaded1 = 0, loaded2 = 0, sub1 = -1, sub2 = -1, mism = 0;
    Rng r1, r2;
    printf("real UDP loopback\n");
    if (netSockStartup() != 0) {
        CHECK(0, "socket startup");
        return;
    }
    hs = netUdpOpen(0, 0, err, sizeof(err));
    cs1 = netUdpOpen(0, 1, err, sizeof(err));
    cs2 = netUdpOpen(0, 0, err, sizeof(err));
    CHECK(hs && cs1 && cs2, "open sockets: %s", err);
    if (!hs || !cs1 || !cs2) return;
    memset(&hc, 0, sizeof(hc));
    netStrCopy(hc.buildId, NET_BUILDID_MAX, "udp-test");
    hc.maxPlayers = 2;
    hc.log = logFn;
    hc.logCtx = (void *)"udp-host";
    host = netHostCreate(&hc, netUdpTransport(hs));
    memset(&cc, 0, sizeof(cc));
    netStrCopy(cc.buildId, NET_BUILDID_MAX, "udp-test");
    cc.log = logFn;
    netStrCopy(cc.name, NET_NAME_MAX, "Alpha");
    cc.logCtx = (void *)"udp-1";
    c1 = netClientCreate(&cc, netUdpTransport(cs1));
    netStrCopy(cc.name, NET_NAME_MAX, "Bravo");
    cc.logCtx = (void *)"udp-2";
    c2 = netClientCreate(&cc, netUdpTransport(cs2));
    ha = netAddrMake(127, 0, 0, 1, netUdpLocalPort(hs));
    netClientConnect(c1, &ha, 0);
    netClientConnect(c2, &ha, 0);
    r1.s = 11;
    r2.s = 22;
    t0 = netTimeUs();
    now = t0;
    while (now - t0 < 12000000ull && (f1 < 240 || f2 < 240)) {
        NetUdp *socks[3];
        NetClientStatus st1, st2;
        socks[0] = hs;
        socks[1] = cs1;
        socks[2] = cs2;
        netUdpWait(socks, 3, 1000);
        now = netTimeUs();
        netHostPump(host, now);
        netClientPump(c1, now);
        netClientPump(c2, now);
        netClientGetStatus(c1, &st1);
        netClientGetStatus(c2, &st2);
        if (!started && st1.lobbyValid && st2.lobbyValid && st1.lobby.num_players == 2 && st2.lobby.num_players == 2) {
            netClientSetPlayer(c2, 1, 5, 0, 0, 1);
            started = 1;
            lastStartTry = now;
        }
        /* Start once P2's ready flag is visible (both are reliable messages
         * from different peers, so they can land in either order). */
        if (started && st1.lobbyValid && st1.lobby.state == NLS_WAITING && st1.matchPhase == NMP_NONE &&
            st1.lobby.players[1].ready && now - lastStartTry >= 250000ull) {
            netClientStartMatch(c1);
            lastStartTry = now;
        }
        {
            NetMatchStart ms;
            int slot;
            if (netClientMatchTake(c1, &ms, &slot)) { netClientMatchLoaded(c1); loaded1 = ms.delay; }
            if (netClientMatchTake(c2, &ms, &slot)) { netClientMatchLoaded(c2); loaded2 = ms.delay; }
        }
        if (loaded1 && netClientMatchIsGo(c1)) {
            NetBundle b;
            if (sub1 != (int)f1) {
                NetInputRec rec;
                makeInput(&r1, &rec);
                netClientMatchSubmit(c1, f1 + (uint32_t)loaded1, &rec);
                sub1 = (int)f1;
            }
            if (netClientMatchGetBundle(c1, f1, &b) == 1) {
                s1 = foldBundle(s1, &b);
                netClientMatchConsumed(c1, f1 + 1);
                f1++;
            }
        }
        if (loaded2 && netClientMatchIsGo(c2)) {
            NetBundle b;
            if (sub2 != (int)f2) {
                NetInputRec rec;
                makeInput(&r2, &rec);
                netClientMatchSubmit(c2, f2 + (uint32_t)loaded2, &rec);
                sub2 = (int)f2;
            }
            if (netClientMatchGetBundle(c2, f2, &b) == 1) {
                s2 = foldBundle(s2, &b);
                netClientMatchConsumed(c2, f2 + 1);
                f2++;
            }
        }
        if (f1 == f2 && f1 > 0 && s1 != s2) mism++;
    }
    printf("  frames %u / %u in %.2fs real time\n", f1, f2, (double)(now - t0) / 1e6);
    CHECK(f1 >= 240 && f2 >= 240, "real UDP match did not progress (%u/%u)", f1, f2);
    CHECK(mism == 0, "real UDP states diverged");
    netClientDestroy(c1);
    netClientDestroy(c2);
    netHostDestroy(host);
    netUdpClose(hs);
    netUdpClose(cs1);
    netUdpClose(cs2);
    netSockCleanup();
}

/* Two real UDP clients quick-match on a running ge007-netserver and play a
 * short match through it:  netplay_selftest --live-server 127.0.0.1:27008 */
static int liveServerTest(const char *addrStr)
{
    NetUdp *cs[2];
    NetClient *c[2];
    NetClientConfig cc;
    NetAddr sa;
    char err[128];
    uint64_t t0, now, lastTry = 0;
    uint32_t f[2] = { 0, 0 };
    uint64_t st[2] = { 0, 0 };
    int delay[2] = { 0, 0 }, sub[2] = { -1, -1 }, k, quickSent = 0, readySent = 0, mism = 0;
    Rng rr[2];
    printf("live server %s\n", addrStr);
    if (netSockStartup() != 0 || netAddrResolve(addrStr, NET_DEFAULT_SERVER_PORT, &sa) != 0) {
        printf("  cannot resolve %s\n", addrStr);
        return 1;
    }
    memset(&cc, 0, sizeof(cc));
    netStrCopy(cc.buildId, NET_BUILDID_MAX, "live-selftest");
    cc.log = logFn;
    for (k = 0; k < 2; k++) {
        cs[k] = netUdpOpen(0, 0, err, sizeof(err));
        if (!cs[k]) {
            printf("  %s\n", err);
            return 1;
        }
        netStrFmt(cc.name, NET_NAME_MAX, "Live%d", k + 1);
        cc.logCtx = (void *)(k ? "live-2" : "live-1");
        c[k] = netClientCreate(&cc, netUdpTransport(cs[k]));
        netClientConnect(c[k], &sa, 1);
        rr[k].s = 100 + (uint64_t)k;
    }
    t0 = now = netTimeUs();
    while (now - t0 < 30000000ull && (f[0] < 300 || f[1] < 300)) {
        NetClientStatus s[2];
        netUdpWait(cs, 2, 1000);
        now = netTimeUs();
        for (k = 0; k < 2; k++) {
            netClientPump(c[k], now);
            netClientGetStatus(c[k], &s[k]);
            if (s[k].state == NCS_FAILED) {
                printf("  client %d failed: %s\n", k + 1, s[k].lastError);
                return 1;
            }
        }
        if (!quickSent && s[0].state == NCS_CONNECTED && s[1].state == NCS_CONNECTED) {
            netClientQuickMatch(c[0]);
            lastTry = now;
            quickSent = 1;
        }
        if (quickSent == 1 && s[0].state == NCS_LOBBY && now - lastTry > 300000ull) {
            netClientQuickMatch(c[1]);   /* after P1, so both land in the same lobby */
            quickSent = 2;
        }
        if (!readySent && s[0].lobbyValid && s[1].lobbyValid && s[0].lobby.num_players == 2) {
            for (k = 0; k < 2; k++) netClientSetPlayer(c[k], k, 5, 0, 0, 1);   /* quick lobbies autostart */
            readySent = 1;
        }
        for (k = 0; k < 2; k++) {
            NetMatchStart ms;
            int slot;
            NetBundle b;
            if (netClientMatchTake(c[k], &ms, &slot)) {
                netClientMatchLoaded(c[k]);
                delay[k] = ms.delay;
            }
            if (!delay[k] || !netClientMatchIsGo(c[k])) continue;
            if (sub[k] != (int)f[k]) {
                NetInputRec rec;
                makeInput(&rr[k], &rec);
                netClientMatchSubmit(c[k], f[k] + (uint32_t)delay[k], &rec);
                sub[k] = (int)f[k];
            }
            if (netClientMatchGetBundle(c[k], f[k], &b) == 1) {
                st[k] = foldBundle(st[k], &b);
                if ((f[k] % 30) == 0) netClientMatchHash(c[k], f[k], (uint32_t)st[k]);
                netClientMatchConsumed(c[k], f[k] + 1);
                f[k]++;
            }
        }
        if (f[0] == f[1] && f[0] > 0 && st[0] != st[1]) mism++;
    }
    printf("  frames %u / %u, %s\n", f[0], f[1], (f[0] >= 300 && f[1] >= 300 && !mism) ? "OK" : "FAILED");
    for (k = 0; k < 2; k++) {
        netClientMatchFinished(c[k]);
        netClientPump(c[k], netTimeUs());
        netClientDestroy(c[k]);
        netUdpClose(cs[k]);
    }
    netSockCleanup();
    return (f[0] >= 300 && f[1] >= 300 && !mism) ? 0 : 1;
}

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) g_verbose = 1;
        if (strcmp(argv[i], "--live-server") == 0 && i + 1 < argc) return liveServerTest(argv[i + 1]);
        if (strcmp(argv[i], "--dirproto-vectors") == 0) return dirProtoVectors();
    }
    printf("GoldenEye netplay selftest (protocol v%d)\n", NET_PROTO_VERSION);
    testCodecs();
    testDirectMatch("2 players, LAN (no loss)", 2, 0.0, 0.0, 300, 900, 1800, 1);
    testDirectMatch("4 players, internet (3% loss, dup, 20-90 ms)", 4, 0.03, 0.02, 20000, 90000, 3600, 2);
    testDirectMatch("3 players, bad link (12% loss, 60-200 ms)", 3, 0.12, 0.03, 60000, 200000, 1200, 3);
    testDesyncDetected();
    testDisconnect();
    testAbort();
    testMatchEnds();
    testListing();
    testMatchmaking();
    testBuildMismatch();
    testStun();
    testDirProto();
    testDirProtoJsVectors();
    testMultiCandidateJoin();
    testQuickPrefs();
    testWaitStatus();
    testLocalAddress();
    testRealUdp();
    if (g_failures) {
        printf("\n%d CHECK(S) FAILED\n", g_failures);
        return 1;
    }
    printf("\nALL TESTS PASSED\n");
    return 0;
}
