/*
 * netdir_test.c -- drives the online directory client (port/net/net_dir.c)
 * against a live service, for the end-to-end test in
 * tools_pc/netplay/cloudflare/test/integration.mjs (D414). One role per run:
 *
 *   netdir_test URL host  [--poll]   register a lobby, print HOSTED <id> <code>,
 *                                    wait for a joiner (JOINREQ ...), play a
 *                                    match (state PLAYING -> WAITING: the
 *                                    service only takes results for matches
 *                                    it saw, D416), report its result,
 *                                    unregister, print DONE
 *   netdir_test URL join CODE [--poll]   print JOININFO ... or ERROR ...
 *   netdir_test URL quick [--poll]       print QUICKHOST or JOININFO ...
 *   netdir_test URL list  [--poll]       print LISTED <n> then one line each
 *
 * Exit 0 on success, 1 on timeout / failure. Lines are flushed immediately.
 */
#include "net_dir.h"
#include "net_plat.h"
#include "net_sock.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void logFn(void *ctx, int level, const char *msg)
{
    (void)ctx;
    if (level >= NETLOG_WARN || getenv("NETDIR_VERBOSE")) fprintf(stderr, "[netdir] %s\n", msg);
}

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static void addrStr(const NetAddr *a, char *out, int n)
{
    netAddrFormat(a, out, n);
}

/* Wait until pred() or timeout; returns 1 if pred became true. */
typedef int (*Pred)(NetDirStatus *st, void *arg);
static int waitFor(Pred pred, void *arg, int timeoutMs, NetDirStatus *st)
{
    uint64_t end = netTimeUs() + (uint64_t)timeoutMs * 1000ull;
    while (netTimeUs() < end) {
        netDirGetStatus(st);
        if (pred(st, arg)) return 1;
        netSleepUs(10000);
    }
    netDirGetStatus(st);
    return 0;
}

static int predHosted(NetDirStatus *st, void *arg) { (void)arg; return st->hostedSeq > 0 && st->hosted.code[0]; }
static int predSeqAbove(NetDirStatus *st, void *arg)
{
    const uint32_t *base = (const uint32_t *)arg;   /* [joinInfoSeq, failSeq, quickHostSeq, listSeq] */
    return st->joinInfoSeq > base[0] || st->failSeq > base[1] || st->quickHostSeq > base[2] || st->listSeq > base[3];
}

int main(int argc, char **argv)
{
    NetDirConfig cfg;
    NetDirStatus st;
    NetAddr cands[2];
    const char *role;
    int i, poll = 0, rc = 1;
    if (argc < 3) {
        fprintf(stderr, "usage: %s URL host|join CODE|quick|list [--poll]\n", argv[0]);
        return 2;
    }
    for (i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--poll")) poll = 1;
    }
    role = argv[2];
    if (netSockStartup() != 0) return 1;
    memset(&cfg, 0, sizeof(cfg));
    netStrCopy(cfg.url, sizeof(cfg.url), argv[1]);
    netStrCopy(cfg.build, sizeof(cfg.build), "netdir-test-build|ntsc-final|x86_64-windows|p2|64bit");
    netStrCopy(cfg.name, sizeof(cfg.name), !strcmp(role, "host") ? "HostBond" : "JoinerAlec");
    cfg.forcePoll = poll;
    cfg.log = logFn;
    if (netDirStart(&cfg) != 0) {
        say("FAIL start");
        return 1;
    }
    cands[0] = netAddrMake(203, 0, 113, 5, 51000);
    cands[1] = netAddrMake(192, 168, 1, 20, 27007);

    if (!strcmp(role, "host")) {
        NdpHost h;
        NdpJoinReq jr;
        uint64_t end;
        memset(&h, 0, sizeof(h));
        netStrCopy(h.name, sizeof(h.name), "Selftest lobby");
        h.flags = NDPF_PUBLIC;
        h.maxPlayers = 4;
        h.numPlayers = 1;
        h.stage = 7;
        h.ncand = 2;
        h.cand[0] = cands[0];
        h.cand[1] = cands[1];
        h.nplayers = 1;
        netStrCopy(h.players[0].name, NET_NAME_MAX, "HostBond");
        netDirHost(&h);
        if (!waitFor(predHosted, NULL, 15000, &st)) {
            say("FAIL not hosted: %s", st.lastError);
            goto out;
        }
        say("HOSTED %u %s %s", (unsigned)st.hosted.lobbyId, st.hosted.code, st.polling ? "poll" : "ws");
        end = netTimeUs() + 30000000ull;
        while (netTimeUs() < end) {
            if (netDirTakeJoinReq(&jr)) {
                char a[32];
                addrStr(&jr.cand[0], a, sizeof(a));
                say("JOINREQ %s %d %s", jr.name, jr.ncand, jr.ncand ? a : "-");
                break;
            }
            netSleepUs(10000);
        }
        if (netTimeUs() >= end) {
            say("FAIL no joiner");
            goto out;
        }
        /* the match: the service sees it start and end */
        h.numPlayers = 2;
        h.nplayers = 2;
        netStrCopy(h.players[1].name, NET_NAME_MAX, "JoinerAlec");
        h.state = NDPS_PLAYING;
        netDirHost(&h);
        netSleepUs(poll ? 2500000 : 1500000);   /* a changed record goes out at the next round */
        h.state = NDPS_WAITING;
        netDirHost(&h);
        netSleepUs(poll ? 2500000 : 1500000);
        {
            NdpResult r;
            memset(&r, 0, sizeof(r));
            r.durationSec = 125;
            r.stage = 7;
            r.n = 2;
            netStrCopy(r.players[0].name, NET_NAME_MAX, "HostBond");
            r.players[0].kills = 5;
            r.players[0].deaths = 1;
            netStrCopy(r.players[1].name, NET_NAME_MAX, "JoinerAlec");
            r.players[1].character = 2;
            r.players[1].kills = 1;
            r.players[1].deaths = 5;
            netDirReportResult(&r);
        }
        netSleepUs(poll ? 3000000 : 500000);   /* let the result + unhost go out */
        netDirUnhost();
        netSleepUs(poll ? 3000000 : 500000);
        say("DONE");
        rc = 0;
    } else {
        uint32_t base[4];
        uint32_t nonce = 0x5EED0000u | (uint32_t)(netRandom32() & 0xFFFF);
        netDirGetStatus(&st);
        base[0] = st.joinInfoSeq;
        base[1] = st.failSeq;
        base[2] = st.quickHostSeq;
        base[3] = st.listSeq;
        if (!strcmp(role, "join") && argc >= 4) {
            netDirJoin(0, argv[3], nonce, cands, 2);
        } else if (!strcmp(role, "quick")) {
            netDirQuick(nonce, 0, NULL, cands, 2);
        } else if (!strcmp(role, "list")) {
            netDirRequestList();
        } else {
            say("FAIL bad role");
            goto out;
        }
        if (!waitFor(predSeqAbove, base, 15000, &st)) {
            say("FAIL no reply: %s", st.lastError);
            goto out;
        }
        if (st.joinInfoSeq > base[0]) {
            char a[32];
            addrStr(&st.joinInfo.cand[0], a, sizeof(a));
            say("JOININFO %u %s %d %s %s", (unsigned)st.joinInfo.lobbyId, st.joinInfo.hostName, st.joinInfo.ncand,
                st.joinInfo.ncand ? a : "-", st.joinInfo.nonce == nonce ? "nonce-ok" : "nonce-BAD");
            rc = st.joinInfo.nonce == nonce ? 0 : 1;
        } else if (st.failSeq > base[1]) {
            say("ERROR %d %s", st.fail.code, st.fail.text);
            rc = 0;
        } else if (st.quickHostSeq > base[2]) {
            say("QUICKHOST %s", st.quickHostNonce == nonce ? "nonce-ok" : "nonce-BAD");
            rc = st.quickHostNonce == nonce ? 0 : 1;
        } else {
            say("LISTED %d online=%u", st.list.n, (unsigned)st.list.online);
            for (i = 0; i < st.list.n; i++) {
                say("ENTRY %u %s %s %s %d/%d", (unsigned)st.list.e[i].id, st.list.e[i].code, st.list.e[i].hostName,
                    st.list.e[i].country[0] ? st.list.e[i].country : "-", st.list.e[i].numPlayers,
                    st.list.e[i].maxPlayers);
            }
            rc = 0;
        }
    }
out:
    netDirStop();
    netSockCleanup();
    return rc;
}
