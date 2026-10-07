/*
 * net_proto.c -- wire codecs for the netplay protocol (D409).
 */
#include "net_proto.h"

#include <string.h>

/* Wire-only bits in the record's leading flag byte. */
#define WIRE_STICKS   0x08
#define WIRE_ACTIONS  0x10
#define WIRE_SEMANTIC (NIR_LOOK | NIR_CROSS | NIR_PDTURN)

void netWriteHeader(NetW *w, uint8_t type, uint32_t connId)
{
    nwU32(w, NET_MAGIC);
    nwU8(w, NET_PROTO_VERSION);
    nwU8(w, type);
    nwU16(w, 0);          /* patched by netFinishHeader */
    nwU32(w, connId);
}

void netFinishHeader(NetW *w)
{
    int payload;
    if (w->overflow || w->len < NET_HEADER_SIZE) return;
    payload = w->len - NET_HEADER_SIZE;
    w->buf[6] = (uint8_t)(payload & 0xFF);
    w->buf[7] = (uint8_t)(payload >> 8);
}

int netReadHeader(NetR *r, NetHeader *h)
{
    uint32_t magic = nrU32(r);
    h->version = nrU8(r);
    h->type = nrU8(r);
    h->length = nrU16(r);
    h->conn_id = nrU32(r);
    if (r->err || magic != NET_MAGIC) return -1;
    if (h->version != NET_PROTO_VERSION) return -2;
    if ((int)h->length != nrLeft(r)) return -1;
    return 0;
}

void netEncInputRec(NetW *w, const NetInputRec *rec)
{
    uint8_t wf = (uint8_t)(rec->flags & WIRE_SEMANTIC);
    if (rec->stick_x || rec->stick_y) wf |= WIRE_STICKS;
    if (rec->actions) wf |= WIRE_ACTIONS;
    nwU8(w, wf);
    nwU16(w, rec->buttons);
    if (wf & WIRE_STICKS) {
        nwU8(w, (uint8_t)rec->stick_x);
        nwU8(w, (uint8_t)rec->stick_y);
    }
    if (wf & WIRE_ACTIONS) nwU8(w, rec->actions);
    if (wf & NIR_LOOK) {
        nwF32(w, rec->look_dtheta);
        nwF32(w, rec->look_dverta);
    }
    if (wf & NIR_CROSS) {
        nwF32(w, rec->cross_x);
        nwF32(w, rec->cross_y);
        nwF32(w, rec->gun_az);
        nwF32(w, rec->gun_turn);
    }
    if (wf & NIR_PDTURN) {
        nwF32(w, rec->pdturn_x);
        nwF32(w, rec->pdturn_y);
    }
}

/* D412: every float a peer sends ends up in game state on every PC. The
 * game wraps angles with `while (theta >= 360.0f) theta -= 360.0f`
 * (bondview2.c), which never ends for infinity or 1e30 -- one crafted packet
 * would hang every player's game. So: non-finite -> 0, magnitudes beyond any
 * real input clamped. Every PC (the sender included) plays the host's
 * re-encoded copy of what it decoded here, so this cannot desync. Limits,
 * all far beyond real input: ten turns per frame (the wrap loop stays
 * short); the crosshair's +-5.16 screen edge (input.c
 * GEPD_CROSSHAIR_LIMIT); the gun pose that follows from it plus the FOV
 * term; PD turn is +-1. */
#define NIR_LIM_LOOK   3600.0f
#define NIR_LIM_CROSS  8.0f
#define NIR_LIM_GUN    16.0f
#define NIR_LIM_PDTURN 4.0f

/* Read a float; infinity / NaN -> 0 by exponent bits (immune to a future
 * -ffast-math, which may fold isfinite() to true), then clamp to +-lim. */
static float nrF32Sane(NetR *r, float lim)
{
    uint32_t bits = nrU32(r);
    float v;
    if ((bits & 0x7F800000u) == 0x7F800000u) return 0.0f;
    memcpy(&v, &bits, sizeof(v));
    if (v > lim) return lim;
    if (v < -lim) return -lim;
    return v;
}

int netDecInputRec(NetR *r, NetInputRec *rec)
{
    uint8_t wf = nrU8(r);
    memset(rec, 0, sizeof(*rec));
    if (wf & ~(WIRE_SEMANTIC | WIRE_STICKS | WIRE_ACTIONS)) {
        r->err = 1;
        return -1;
    }
    rec->flags = (uint8_t)(wf & WIRE_SEMANTIC);
    rec->buttons = nrU16(r);
    if (wf & WIRE_STICKS) {
        rec->stick_x = (int8_t)nrU8(r);
        rec->stick_y = (int8_t)nrU8(r);
    }
    if (wf & WIRE_ACTIONS) rec->actions = nrU8(r);
    if (wf & NIR_LOOK) {
        rec->look_dtheta = nrF32Sane(r, NIR_LIM_LOOK);
        rec->look_dverta = nrF32Sane(r, NIR_LIM_LOOK);
    }
    if (wf & NIR_CROSS) {
        rec->cross_x = nrF32Sane(r, NIR_LIM_CROSS);
        rec->cross_y = nrF32Sane(r, NIR_LIM_CROSS);
        rec->gun_az = nrF32Sane(r, NIR_LIM_GUN);
        rec->gun_turn = nrF32Sane(r, NIR_LIM_GUN);
    }
    if (wf & NIR_PDTURN) {
        rec->pdturn_x = nrF32Sane(r, NIR_LIM_PDTURN);
        rec->pdturn_y = nrF32Sane(r, NIR_LIM_PDTURN);
    }
    return r->err ? -1 : 0;
}

void netEncBundle(NetW *w, const NetBundle *b)
{
    int s;
    nwU32(w, b->frame);
    nwU8(w, b->present);
    nwU8(w, b->disc);
    nwU8(w, b->flags);
    for (s = 0; s < NET_MAX_PLAYERS; s++) {
        if (b->present & (1u << s)) netEncInputRec(w, &b->rec[s]);
    }
}

int netDecBundle(NetR *r, NetBundle *b)
{
    int s;
    memset(b, 0, sizeof(*b));
    b->frame = nrU32(r);
    b->present = nrU8(r);
    b->disc = nrU8(r);
    b->flags = nrU8(r);
    if ((b->present | b->disc) & ~((1u << NET_MAX_PLAYERS) - 1)) {
        r->err = 1;
        return -1;
    }
    for (s = 0; s < NET_MAX_PLAYERS; s++) {
        if (b->present & (1u << s)) {
            if (netDecInputRec(r, &b->rec[s]) != 0) return -1;
        }
    }
    return r->err ? -1 : 0;
}

void netEncPlayerInfo(NetW *w, const NetPlayerInfo *p)
{
    nwU8(w, p->used);
    nwU8(w, p->ready);
    nwU8(w, p->connected);
    nwU8(w, p->character);
    nwU8(w, p->handicap);
    nwU8(w, p->control);
    nwU8(w, p->team);
    nwU16(w, p->ping);
    nwStr(w, p->name, NET_NAME_MAX - 1);
}

int netDecPlayerInfo(NetR *r, NetPlayerInfo *p)
{
    memset(p, 0, sizeof(*p));
    p->used = nrU8(r) ? 1 : 0;
    p->ready = nrU8(r) ? 1 : 0;
    p->connected = nrU8(r) ? 1 : 0;
    p->character = nrU8(r);
    p->handicap = nrU8(r);
    p->control = nrU8(r);
    p->team = nrU8(r);
    p->ping = nrU16(r);
    nrStr(r, p->name, NET_NAME_MAX);
    netSanitizeText(p->name, NET_NAME_MAX, "Player");
    return r->err ? -1 : 0;
}

void netEncSettings(NetW *w, const NetSettings *s)
{
    nwU8(w, s->scenario);
    nwU8(w, s->stage);
    nwU8(w, s->length);
    nwU8(w, s->weapons);
    nwU8(w, s->aimsight);
    nwU8(w, s->delay);
    nwU8(w, s->flags);
    nwU16(w, s->options);
}

int netDecSettings(NetR *r, NetSettings *s)
{
    memset(s, 0, sizeof(*s));
    s->scenario = nrU8(r);
    s->stage = nrU8(r);
    s->length = nrU8(r);
    s->weapons = nrU8(r);
    s->aimsight = nrU8(r);
    s->delay = nrU8(r);
    s->flags = nrU8(r);
    s->options = nrU16(r);
    return r->err ? -1 : 0;
}

void netEncLobby(NetW *w, const NetLobbyState *l)
{
    int i;
    nwU32(w, l->revision);
    nwU32(w, l->lobby_id);
    nwStr(w, l->code, NET_CODE_LEN);
    nwStr(w, l->name, NET_LOBBY_NAME_MAX - 1);
    nwU8(w, l->flags);
    nwU8(w, l->state);
    nwU8(w, l->max_players);
    nwU8(w, l->num_players);
    nwU8(w, l->leader);
    nwU8(w, l->countdown);
    for (i = 0; i < NET_MAX_PLAYERS; i++) netEncPlayerInfo(w, &l->players[i]);
    netEncSettings(w, &l->settings);
}

int netDecLobby(NetR *r, NetLobbyState *l)
{
    int i;
    memset(l, 0, sizeof(*l));
    l->revision = nrU32(r);
    l->lobby_id = nrU32(r);
    nrStr(r, l->code, NET_CODE_LEN + 1);
    nrStr(r, l->name, NET_LOBBY_NAME_MAX);
    netSanitizeText(l->name, NET_LOBBY_NAME_MAX, "Lobby");
    l->flags = nrU8(r);
    l->state = nrU8(r);
    l->max_players = nrU8(r);
    l->num_players = nrU8(r);
    l->leader = nrU8(r);
    l->countdown = nrU8(r);
    for (i = 0; i < NET_MAX_PLAYERS; i++) {
        if (netDecPlayerInfo(r, &l->players[i]) != 0) return -1;
    }
    if (netDecSettings(r, &l->settings) != 0) return -1;
    if (l->max_players < 2 || l->max_players > NET_MAX_PLAYERS || l->num_players > NET_MAX_PLAYERS ||
        l->leader >= NET_MAX_PLAYERS) {
        r->err = 1;
    }
    return r->err ? -1 : 0;
}

void netEncMatchStart(NetW *w, const NetMatchStart *m)
{
    int i;
    nwU32(w, m->match_id);
    nwU64(w, m->seed_random);
    nwU64(w, m->seed_chrobj);
    nwU8(w, m->num_players);
    nwU8(w, m->delay);
    nwU8(w, m->stage);
    netEncSettings(w, &m->settings);
    for (i = 0; i < NET_MAX_PLAYERS; i++) netEncPlayerInfo(w, &m->players[i]);
}

int netDecMatchStart(NetR *r, NetMatchStart *m)
{
    int i;
    memset(m, 0, sizeof(*m));
    m->match_id = nrU32(r);
    m->seed_random = nrU64(r);
    m->seed_chrobj = nrU64(r);
    m->num_players = nrU8(r);
    m->delay = nrU8(r);
    m->stage = nrU8(r);
    if (netDecSettings(r, &m->settings) != 0) return -1;
    for (i = 0; i < NET_MAX_PLAYERS; i++) {
        if (netDecPlayerInfo(r, &m->players[i]) != 0) return -1;
    }
    if (m->num_players < 1 || m->num_players > NET_MAX_PLAYERS || m->delay > NET_MAX_DELAY) {
        r->err = 1;
    }
    return r->err ? -1 : 0;
}

void netEncListEntry(NetW *w, const NetListEntry *e)
{
    nwU32(w, e->lobby_id);
    nwStr(w, e->name, NET_LOBBY_NAME_MAX - 1);
    nwStr(w, e->leader, NET_NAME_MAX - 1);
    nwStr(w, e->code, NET_CODE_LEN);
    nwU8(w, e->num_players);
    nwU8(w, e->max_players);
    nwU8(w, e->state);
    nwU8(w, e->flags);
    nwU8(w, e->scenario);
    nwU8(w, e->stage);
}

int netDecListEntry(NetR *r, NetListEntry *e)
{
    memset(e, 0, sizeof(*e));
    e->lobby_id = nrU32(r);
    nrStr(r, e->name, NET_LOBBY_NAME_MAX);
    netSanitizeText(e->name, NET_LOBBY_NAME_MAX, "Lobby");
    nrStr(r, e->leader, NET_NAME_MAX);
    netSanitizeText(e->leader, NET_NAME_MAX, "?");
    nrStr(r, e->code, NET_CODE_LEN + 1);
    e->num_players = nrU8(r);
    e->max_players = nrU8(r);
    e->state = nrU8(r);
    e->flags = nrU8(r);
    e->scenario = nrU8(r);
    e->stage = nrU8(r);
    return r->err ? -1 : 0;
}

void netSanitizeText(char *s, int cap, const char *fallback)
{
    int i, n, start = 0, end;
    if (!s || cap <= 0) return;
    s[cap - 1] = 0;
    n = (int)strlen(s);
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c > 0x7E) s[i] = '?';
    }
    while (start < n && s[start] == ' ') start++;
    end = n;
    while (end > start && s[end - 1] == ' ') end--;
    if (start > 0 || end < n) {
        memmove(s, s + start, (size_t)(end - start));
        s[end - start] = 0;
    }
    if (!s[0] && fallback) {
        int k = 0;
        for (; k < cap - 1 && fallback[k]; k++) s[k] = fallback[k];
        s[k] = 0;
    }
}

int netInputRecEq(const NetInputRec *a, const NetInputRec *b)
{
    if (a->buttons != b->buttons || a->stick_x != b->stick_x || a->stick_y != b->stick_y ||
        a->flags != b->flags || a->actions != b->actions) {
        return 0;
    }
    /* Compare float BITS (NaN-safe, -0.0 distinct) -- the game sees bits. */
    if ((a->flags & NIR_LOOK) && (memcmp(&a->look_dtheta, &b->look_dtheta, 4) || memcmp(&a->look_dverta, &b->look_dverta, 4))) return 0;
    if ((a->flags & NIR_CROSS) && (memcmp(&a->cross_x, &b->cross_x, 4) || memcmp(&a->cross_y, &b->cross_y, 4) ||
                                   memcmp(&a->gun_az, &b->gun_az, 4) || memcmp(&a->gun_turn, &b->gun_turn, 4))) return 0;
    if ((a->flags & NIR_PDTURN) && (memcmp(&a->pdturn_x, &b->pdturn_x, 4) || memcmp(&a->pdturn_y, &b->pdturn_y, 4))) return 0;
    return 1;
}

const char *netRejectReasonText(int reason)
{
    switch (reason) {
    case NR_FULL:        return "the lobby is full";
    case NR_PROTOCOL:    return "incompatible netplay protocol version";
    case NR_BUILD:       return "different game build (both players need the same version)";
    case NR_IN_MATCH:    return "a match is already in progress";
    case NR_SERVER_FULL: return "the server is full";
    case NR_BAD_REQUEST: return "bad request";
    case NR_RATE_LIMIT:  return "too many connection attempts, try again shortly";
    default:             return "refused";
    }
}
