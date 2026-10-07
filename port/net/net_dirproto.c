/*
 * net_dirproto.c -- online directory protocol codec (D414). See
 * net_dirproto.h; mirrored by tools_pc/netplay/cloudflare/src/protocol.js.
 */
#include "net_dirproto.h"
#include "net_gamedata.h"
#include "net_plat.h"

#include <string.h>

/* ---- shared pieces ---- */

static void encCands(NetW *w, const NetAddr *cand, uint8_t n)
{
    int i;
    if (n > NET_MAX_CANDS) n = NET_MAX_CANDS;
    nwU8(w, n);
    for (i = 0; i < n; i++) {
        nwU32(w, cand[i].ip);
        nwU16(w, cand[i].port);
    }
}

static int decCands(NetR *r, NetAddr *cand, uint8_t *n)
{
    int i, k = 0;
    int count = nrU8(r);
    if (count > NET_MAX_CANDS) {
        r->err = 1;
        return -1;
    }
    for (i = 0; i < count; i++) {
        NetAddr a;
        a.ip = nrU32(r);
        a.port = nrU16(r);
        if (ndpCandValid(&a)) cand[k++] = a;   /* drop junk, keep the rest */
    }
    *n = (uint8_t)k;
    return r->err ? -1 : 0;
}

static void decName(NetR *r, char *out, int cap, const char *fallback)
{
    nrStr(r, out, cap);
    netSanitizeText(out, cap, fallback);
}

/* ---- quick-match preferences (D416) ---- */

void ndpPrefsAny(NdpPrefs *p)
{
    p->scenario = p->stage = p->weapons = p->length = p->players = NDP_ANY;
}

void ndpNormalizePrefs(NdpPrefs *p)
{
    if (p->scenario != NDP_ANY && p->scenario >= NG_NUM_SCENARIOS) p->scenario = NDP_ANY;
    if (p->stage != NDP_ANY && (p->stage == NG_STAGE_RANDOM || p->stage >= NG_NUM_STAGES)) p->stage = NDP_ANY;
    if (p->weapons != NDP_ANY && p->weapons >= NG_NUM_WEAPONSETS) p->weapons = NDP_ANY;
    if (p->length != NDP_ANY && p->length >= NG_NUM_LENGTHS) p->length = NDP_ANY;
    if (p->players != NDP_ANY && (p->players < 2 || p->players > NET_MAX_PLAYERS)) p->players = NDP_ANY;
    /* "last one standing" is You Only Live Twice's length: it picks YOLT
     * when no mode was chosen, and means nothing for any other mode */
    if (p->length == NG_LENGTH_LAST) {
        if (p->scenario == NDP_ANY) p->scenario = NG_SCENARIO_YOLT;
        p->length = NDP_ANY;
    }
    switch (p->scenario) {
    case NG_SCENARIO_YOLT:
        p->length = NDP_ANY;
        break;
    case NG_SCENARIO_MWTGG:
        p->weapons = NDP_ANY;
        break;
    case NG_SCENARIO_TLD:
        if (p->length != NDP_ANY && p->length > 3) p->length = NDP_ANY;
        break;
    default:
        break;
    }
    if (p->scenario != NDP_ANY && ngScenarioIsTeam(p->scenario)) {
        p->players = (uint8_t)ngScenarioMinPlayers(p->scenario);   /* team sizes are fixed */
        if (p->stage != NDP_ANY && ngStageMaxPlayers(p->stage) < p->players) p->stage = NDP_ANY;
    } else if (p->stage != NDP_ANY && p->players != NDP_ANY && ngStageMaxPlayers(p->stage) < p->players) {
        p->players = (uint8_t)ngStageMaxPlayers(p->stage);
    }
}

int ndpPrefsMatch(const NdpPrefs *p, int scenario, int stage, int weapons, int length, int maxPlayers)
{
    return (p->scenario == NDP_ANY || p->scenario == scenario) && (p->stage == NDP_ANY || p->stage == stage) &&
           (p->weapons == NDP_ANY || p->weapons == weapons) && (p->length == NDP_ANY || p->length == length) &&
           (p->players == NDP_ANY || p->players == maxPlayers);
}

void ndpPrefsToRules(const NdpPrefs *pin, NetSettings *s, int *maxPlayers)
{
    NdpPrefs p = *pin;
    int maxp;
    ndpNormalizePrefs(&p);
    ngDefaultSettings(s);
    s->scenario = p.scenario != NDP_ANY ? p.scenario : NG_SCENARIO_NORMAL;
    s->stage = p.stage != NDP_ANY ? p.stage : NG_STAGE_RANDOM;
    if (p.weapons != NDP_ANY) s->weapons = p.weapons;
    if (p.length != NDP_ANY) s->length = p.length;
    s->flags |= NS_AUTOSTART;
    ngNormalizeSettings(s);   /* YOLT: last one standing; Golden Gun: its weapons */
    maxp = ngScenarioMaxPlayers(s->scenario);
    if (s->stage != NG_STAGE_RANDOM && ngStageMaxPlayers(s->stage) < maxp) maxp = ngStageMaxPlayers(s->stage);
    if (p.players != NDP_ANY && p.players < maxp) maxp = p.players;
    if (maxp < ngScenarioMinPlayers(s->scenario)) maxp = ngScenarioMinPlayers(s->scenario);
    *maxPlayers = maxp;
}

/* Append ", part" (or "part" first). Never trusts the formatter's return
 * value for the offset: vsnprintf may report the untruncated length, or -1
 * on older C runtimes. */
static void appendPart(char *out, int n, int *len, const char *part)
{
    int k;
    if (*len < 0 || *len >= n - 1) return;
    k = netStrFmt(out + *len, n - *len, "%s%s", *len ? ", " : "", part);
    if (k < 0 || k >= n - *len) {
        *len = n - 1;
    } else {
        *len += k;
    }
}

void ndpPrefsLabel(const NdpPrefs *pin, char *out, int n)
{
    static const char *const kShort[NG_NUM_SCENARIOS] = {
        "Normal", "YOLT", "Flag Tag", "Golden Gun", "Licence to Kill", "2 vs 2", "3 vs 1", "2 vs 1",
    };
    NdpPrefs p = *pin;
    char num[16];
    int len = 0;
    if (!out || n <= 0) return;
    ndpNormalizePrefs(&p);
    out[0] = 0;
    if (p.scenario != NDP_ANY) appendPart(out, n, &len, kShort[p.scenario]);
    if (p.stage != NDP_ANY) appendPart(out, n, &len, ngStageName(p.stage));
    if (p.weapons != NDP_ANY) appendPart(out, n, &len, ngWeaponsName(p.weapons));
    if (p.length != NDP_ANY) appendPart(out, n, &len, ngLengthName(p.length));
    if (p.players != NDP_ANY && !(p.scenario != NDP_ANY && ngScenarioIsTeam(p.scenario))) {
        netStrFmt(num, sizeof(num), "%d players", (int)p.players);
        appendPart(out, n, &len, num);
    }
    if (!out[0]) netStrCopy(out, n, "Any game");
}

int ndpCandSendable(const NetAddr *a)
{
    uint32_t top;
    if (!ndpCandValid(a) || a->port < 1024) return 0;
    top = a->ip >> 24;
    if (top == 0 || top >= 224) return 0;                          /* this-network, multicast, reserved, broadcast */
    if ((a->ip & 0xFFFF0000u) == 0xA9FE0000u) return 0;            /* 169.254/16 link-local */
    return 1;
}

int ndpCandValid(const NetAddr *a)
{
    return a && a->ip != 0 && a->ip != 0xFFFFFFFFu && a->port != 0;
}

/* ---- client -> service ---- */

void ndpEncHello(NetW *w, const NdpHello *m)
{
    nwU8(w, NDP_HELLO);
    nwU8(w, m->version);
    nwStr(w, m->build, NET_BUILDID_MAX - 1);
    nwStr(w, m->name, NET_NAME_MAX - 1);
}

void ndpEncList(NetW *w)
{
    nwU8(w, NDP_LIST);
}

void ndpEncHost(NetW *w, const NdpHost *m)
{
    int i, n = m->nplayers > NET_MAX_PLAYERS ? NET_MAX_PLAYERS : m->nplayers;
    nwU8(w, NDP_HOST);
    nwU32(w, m->lobbyId);
    nwStr(w, m->token, NDP_TOKEN_MAX - 1);
    nwStr(w, m->code, NET_CODE_LEN);
    nwStr(w, m->name, NET_LOBBY_NAME_MAX - 1);
    nwU8(w, m->flags);
    nwU8(w, m->maxPlayers);
    nwU8(w, m->state);
    nwU8(w, m->numPlayers);
    nwU8(w, m->scenario);
    nwU8(w, m->stage);
    nwU8(w, m->weapons);
    nwU8(w, m->length);
    nwU16(w, m->matchSec);
    encCands(w, m->cand, m->ncand);
    nwU8(w, (uint8_t)n);
    for (i = 0; i < n; i++) {
        nwStr(w, m->players[i].name, NET_NAME_MAX - 1);
        nwU8(w, m->players[i].character);
        nwU8(w, m->players[i].team);
        nwU8(w, m->players[i].ready);
    }
}

void ndpEncUnhost(NetW *w, const NdpUnhost *m)
{
    nwU8(w, NDP_UNHOST);
    nwU32(w, m->lobbyId);
    nwStr(w, m->token, NDP_TOKEN_MAX - 1);
}

void ndpEncJoin(NetW *w, const NdpJoin *m)
{
    nwU8(w, NDP_JOIN);
    nwU32(w, m->lobbyId);
    nwStr(w, m->code, NET_CODE_LEN);
    nwU32(w, m->nonce);
    encCands(w, m->cand, m->ncand);
}

void ndpEncQuick(NetW *w, const NdpQuick *m)
{
    nwU8(w, NDP_QUICK);
    nwU32(w, m->nonce);
    nwU32(w, m->lobbyId);
    nwU32(w, m->excludeId);
    nwU8(w, m->prefs.scenario);
    nwU8(w, m->prefs.stage);
    nwU8(w, m->prefs.weapons);
    nwU8(w, m->prefs.length);
    nwU8(w, m->prefs.players);
    encCands(w, m->cand, m->ncand);
}

void ndpEncResult(NetW *w, const NdpResult *m)
{
    int i, n = m->n > NET_MAX_PLAYERS ? NET_MAX_PLAYERS : m->n;
    nwU8(w, NDP_RESULT);
    nwU32(w, m->lobbyId);
    nwStr(w, m->token, NDP_TOKEN_MAX - 1);
    nwU16(w, m->durationSec);
    nwU8(w, m->scenario);
    nwU8(w, m->stage);
    nwU8(w, (uint8_t)n);
    for (i = 0; i < n; i++) {
        nwStr(w, m->players[i].name, NET_NAME_MAX - 1);
        nwU8(w, m->players[i].character);
        nwU8(w, m->players[i].team);
        nwU16(w, (uint16_t)m->players[i].kills);
        nwU16(w, (uint16_t)m->players[i].deaths);
    }
}

void ndpEncPoll(NetW *w, const NdpPoll *m)
{
    nwU8(w, NDP_POLL);
    nwU32(w, m->lobbyId);
    nwStr(w, m->token, NDP_TOKEN_MAX - 1);
}

int ndpDecHello(NetR *r, NdpHello *m)
{
    memset(m, 0, sizeof(*m));
    m->version = nrU8(r);
    nrStr(r, m->build, sizeof(m->build));
    decName(r, m->name, sizeof(m->name), "Agent");
    return r->err ? -1 : 0;
}

int ndpDecHost(NetR *r, NdpHost *m)
{
    int i;
    memset(m, 0, sizeof(*m));
    m->lobbyId = nrU32(r);
    nrStr(r, m->token, sizeof(m->token));
    nrStr(r, m->code, sizeof(m->code));
    decName(r, m->name, sizeof(m->name), "Lobby");
    m->flags = nrU8(r);
    m->maxPlayers = nrU8(r);
    m->state = nrU8(r);
    m->numPlayers = nrU8(r);
    m->scenario = nrU8(r);
    m->stage = nrU8(r);
    m->weapons = nrU8(r);
    m->length = nrU8(r);
    m->matchSec = nrU16(r);
    if (decCands(r, m->cand, &m->ncand) != 0) return -1;
    m->nplayers = nrU8(r);
    if (m->nplayers > NET_MAX_PLAYERS) return -1;
    for (i = 0; i < m->nplayers; i++) {
        decName(r, m->players[i].name, sizeof(m->players[i].name), "?");
        m->players[i].character = nrU8(r);
        m->players[i].team = nrU8(r);
        m->players[i].ready = nrU8(r);
    }
    return r->err ? -1 : 0;
}

int ndpDecJoin(NetR *r, NdpJoin *m)
{
    memset(m, 0, sizeof(*m));
    m->lobbyId = nrU32(r);
    nrStr(r, m->code, sizeof(m->code));
    m->nonce = nrU32(r);
    if (decCands(r, m->cand, &m->ncand) != 0) return -1;
    return r->err ? -1 : 0;
}

int ndpDecQuick(NetR *r, NdpQuick *m)
{
    memset(m, 0, sizeof(*m));
    m->nonce = nrU32(r);
    m->lobbyId = nrU32(r);
    m->excludeId = nrU32(r);
    m->prefs.scenario = nrU8(r);
    m->prefs.stage = nrU8(r);
    m->prefs.weapons = nrU8(r);
    m->prefs.length = nrU8(r);
    m->prefs.players = nrU8(r);
    if (decCands(r, m->cand, &m->ncand) != 0) return -1;
    return r->err ? -1 : 0;
}

int ndpDecResult(NetR *r, NdpResult *m)
{
    int i;
    memset(m, 0, sizeof(*m));
    m->lobbyId = nrU32(r);
    nrStr(r, m->token, sizeof(m->token));
    m->durationSec = nrU16(r);
    m->scenario = nrU8(r);
    m->stage = nrU8(r);
    m->n = nrU8(r);
    if (m->n > NET_MAX_PLAYERS) return -1;
    for (i = 0; i < m->n; i++) {
        decName(r, m->players[i].name, sizeof(m->players[i].name), "?");
        m->players[i].character = nrU8(r);
        m->players[i].team = nrU8(r);
        m->players[i].kills = (int16_t)nrU16(r);
        m->players[i].deaths = (int16_t)nrU16(r);
    }
    return r->err ? -1 : 0;
}

/* ---- service -> client ---- */

void ndpEncWelcome(NetW *w, const NdpWelcome *m)
{
    nwU8(w, NDP_WELCOME);
    nwU16(w, m->online);
    nwU16(w, m->lobbies);
    nwU16(w, m->matches);
    nwU16(w, m->searching);
    nwStr(w, m->motd, NDP_MOTD_MAX - 1);
}

int ndpDecWelcome(NetR *r, NdpWelcome *m)
{
    memset(m, 0, sizeof(*m));
    m->online = nrU16(r);
    m->lobbies = nrU16(r);
    m->matches = nrU16(r);
    m->searching = nrU16(r);
    decName(r, m->motd, sizeof(m->motd), "");
    return r->err ? -1 : 0;
}

void ndpEncListed(NetW *w, const NdpListed *m)
{
    int i, n = m->n > NDP_LIST_MAX ? NDP_LIST_MAX : m->n;
    nwU8(w, NDP_LISTED);
    nwU16(w, m->online);
    nwU16(w, m->lobbies);
    nwU16(w, m->matches);
    nwU16(w, m->searching);
    nwU8(w, (uint8_t)n);
    for (i = 0; i < n; i++) {
        const NdpListEntry *e = &m->e[i];
        nwU32(w, e->id);
        nwStr(w, e->name, NET_LOBBY_NAME_MAX - 1);
        nwStr(w, e->hostName, NET_NAME_MAX - 1);
        nwStr(w, e->code, NET_CODE_LEN);
        nwU8(w, e->flags);
        nwU8(w, e->state);
        nwU8(w, e->numPlayers);
        nwU8(w, e->maxPlayers);
        nwU8(w, e->scenario);
        nwU8(w, e->stage);
        nwU8(w, e->weapons);
        nwU8(w, e->length);
        nwStr(w, e->country, NDP_COUNTRY_MAX - 1);
    }
}

int ndpDecListed(NetR *r, NdpListed *m)
{
    int i;
    memset(m, 0, sizeof(*m));
    m->online = nrU16(r);
    m->lobbies = nrU16(r);
    m->matches = nrU16(r);
    m->searching = nrU16(r);
    m->n = nrU8(r);
    if (m->n > NDP_LIST_MAX) return -1;
    for (i = 0; i < m->n; i++) {
        NdpListEntry *e = &m->e[i];
        e->id = nrU32(r);
        decName(r, e->name, sizeof(e->name), "Lobby");
        decName(r, e->hostName, sizeof(e->hostName), "?");
        nrStr(r, e->code, sizeof(e->code));
        netSanitizeText(e->code, sizeof(e->code), "");
        e->flags = nrU8(r);
        e->state = nrU8(r);
        e->numPlayers = nrU8(r);
        e->maxPlayers = nrU8(r);
        e->scenario = nrU8(r);
        e->stage = nrU8(r);
        e->weapons = nrU8(r);
        e->length = nrU8(r);
        nrStr(r, e->country, sizeof(e->country));
        netSanitizeText(e->country, sizeof(e->country), "");
    }
    return r->err ? -1 : 0;
}

void ndpEncHosted(NetW *w, const NdpHosted *m)
{
    nwU8(w, NDP_HOSTED);
    nwU32(w, m->lobbyId);
    nwStr(w, m->token, NDP_TOKEN_MAX - 1);
    nwStr(w, m->code, NET_CODE_LEN);
}

int ndpDecHosted(NetR *r, NdpHosted *m)
{
    memset(m, 0, sizeof(*m));
    m->lobbyId = nrU32(r);
    nrStr(r, m->token, sizeof(m->token));
    nrStr(r, m->code, sizeof(m->code));
    netSanitizeText(m->code, sizeof(m->code), "");
    return r->err ? -1 : 0;
}

void ndpEncJoinInfo(NetW *w, const NdpJoinInfo *m)
{
    nwU8(w, NDP_JOININFO);
    nwU32(w, m->nonce);
    nwU32(w, m->lobbyId);
    nwStr(w, m->name, NET_LOBBY_NAME_MAX - 1);
    nwStr(w, m->hostName, NET_NAME_MAX - 1);
    encCands(w, m->cand, m->ncand);
}

int ndpDecJoinInfo(NetR *r, NdpJoinInfo *m)
{
    memset(m, 0, sizeof(*m));
    m->nonce = nrU32(r);
    m->lobbyId = nrU32(r);
    decName(r, m->name, sizeof(m->name), "Lobby");
    decName(r, m->hostName, sizeof(m->hostName), "?");
    if (decCands(r, m->cand, &m->ncand) != 0) return -1;
    return r->err ? -1 : 0;
}

void ndpEncError(NetW *w, const NdpError *m)
{
    nwU8(w, NDP_ERROR);
    nwU8(w, m->reqType);
    nwU32(w, m->nonce);
    nwU8(w, m->code);
    nwStr(w, m->text, NET_CHAT_MAX);
}

int ndpDecError(NetR *r, NdpError *m)
{
    memset(m, 0, sizeof(*m));
    m->reqType = nrU8(r);
    m->nonce = nrU32(r);
    m->code = nrU8(r);
    decName(r, m->text, sizeof(m->text), "The online service refused the request");
    return r->err ? -1 : 0;
}

void ndpEncQuickHost(NetW *w, uint32_t nonce)
{
    nwU8(w, NDP_QUICKHOST);
    nwU32(w, nonce);
}

int ndpDecQuickHost(NetR *r, uint32_t *nonce)
{
    *nonce = nrU32(r);
    return r->err ? -1 : 0;
}

void ndpEncJoinReq(NetW *w, const NdpJoinReq *m)
{
    nwU8(w, NDP_JOINREQ);
    nwU32(w, m->lobbyId);
    nwStr(w, m->name, NET_NAME_MAX - 1);
    encCands(w, m->cand, m->ncand);
}

int ndpDecJoinReq(NetR *r, NdpJoinReq *m)
{
    memset(m, 0, sizeof(*m));
    m->lobbyId = nrU32(r);
    decName(r, m->name, sizeof(m->name), "?");
    if (decCands(r, m->cand, &m->ncand) != 0) return -1;
    return r->err ? -1 : 0;
}
