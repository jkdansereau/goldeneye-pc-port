/*
 * netonline_test.c -- drives the game's network runtime (port/net/net_runtime.c)
 * through the online flows, for tools_pc/netplay/cloudflare/test/online.mjs
 * (D410): the directory service (the local mock), a STUN server (a local
 * fake), hole punching and the direct UDP connection, exactly as the game
 * uses them. One player per process, like the game (the runtime is a
 * singleton):
 *
 *   netonline_test URL STUN NAME host [--private] [--poll] [--no-report]
 *       host an online lobby; print CODE <code> once it is registered, then
 *       JOINED <name> when someone joins; report a result (unless
 *       --no-report: keeps a live service's public page clean), print DONE
 *   netonline_test URL STUN NAME join CODE [--poll]
 *       join by code; print INLOBBY <players> <host address> or FAILED <why>
 *   netonline_test URL STUN NAME quick [--poll] [--mode N] [--stage N]
 *                  [--weapons N] [--length N] [--players N]
 *       quick match with those preferences (default any): QUICKHOST <code>
 *       <scenario> <stage> <max> (then JOINED / DONE, as a host) or
 *       INLOBBY ... (joined someone's quick lobby)
 *
 * Exit 0 on success. NETONLINE_VERBOSE=1 logs the runtime's progress.
 */
#include "net_client.h"
#include "net_plat.h"
#include "net_runtime.h"
#include "net_sock.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUILD "netonline-test-build|ntsc-final|x86_64-windows|p2|64bit"

static void logFn(void *ctx, int level, const char *msg)
{
    if (level >= NETLOG_WARN || getenv("NETONLINE_VERBOSE")) fprintf(stderr, "[%s] %s\n", (const char *)ctx, msg);
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

static const char *otherPlayer(const NetClientStatus *st)
{
    int i;
    for (i = 0; i < NET_MAX_PLAYERS; i++) {
        if (st->lobby.players[i].used && i != st->slot) return st->lobby.players[i].name;
    }
    return "?";
}

int main(int argc, char **argv)
{
    const char *url, *stun, *name, *role, *code = "";
    int i, poll = 0, priv = 0, report = 1, rc = 1, quick, shownCode = 0;
    NdpPrefs prefs;
    char text[96] = "", lastText[96] = "", err[128];
    uint64_t end;
    if (argc < 5) {
        fprintf(stderr, "usage: %s URL STUN NAME host|join CODE|quick [--private] [--poll]\n", argv[0]);
        return 2;
    }
    url = argv[1];
    stun = argv[2];
    name = argv[3];
    role = argv[4];
    ndpPrefsAny(&prefs);
    for (i = 5; i < argc; i++) {
        if (i + 1 < argc && !strcmp(argv[i], "--mode")) {
            prefs.scenario = (uint8_t)atoi(argv[++i]);
        } else if (i + 1 < argc && !strcmp(argv[i], "--stage")) {
            prefs.stage = (uint8_t)atoi(argv[++i]);
        } else if (i + 1 < argc && !strcmp(argv[i], "--weapons")) {
            prefs.weapons = (uint8_t)atoi(argv[++i]);
        } else if (i + 1 < argc && !strcmp(argv[i], "--length")) {
            prefs.length = (uint8_t)atoi(argv[++i]);
        } else if (i + 1 < argc && !strcmp(argv[i], "--players")) {
            prefs.players = (uint8_t)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--poll")) {
            poll = 1;
        } else if (!strcmp(argv[i], "--private")) {
            priv = 1;
        } else if (!strcmp(argv[i], "--no-report")) {
            report = 0;
        } else {
            code = argv[i];
        }
    }
    quick = !strcmp(role, "quick");
    if (netRuntimeStart(name, BUILD, logFn, (void *)name) != 0) {
        say("FAILED runtime start");
        return 1;
    }
    netRuntimeSetService(url, poll);
    netRuntimeSetStun(stun);

    if (!strcmp(role, "host")) {
        if (netRuntimeHostOnline(0, "Online test", 4, !priv, 0, err, sizeof(err)) != 0) {
            say("FAILED host: %s", err);
            goto out;
        }
    } else if (!strcmp(role, "join")) {
        netRuntimeJoinOnline(0, code);
    } else if (quick) {
        netRuntimeQuickOnline(&prefs);
    } else {
        say("FAILED bad role");
        goto out;
    }

    end = netTimeUs() + 40000000ull;
    while (netTimeUs() < end) {
        NetClientStatus st;
        /* hosting first: a quick host moving into another lobby connects
         * the client before it stops hosting, so a status read after this
         * never shows the old lobby as someone else's */
        int hosting = netRuntimeIsHostingOnline();
        int busy = netRuntimeOnlineBusy(text, sizeof(text));
        if (strcmp(text, lastText) != 0) {
            fprintf(stderr, "[%s] step: %s\n", name, text);
            netStrCopy(lastText, sizeof(lastText), text);
        }
        netClientGetStatus(netRuntimeClient(), &st);
        if (st.state == NCS_FAILED && !busy) {   /* busy: matchmaking is trying another game */
            say("FAILED %s", st.lastError);
            goto out;
        }
        if (hosting) {
            if (!shownCode && st.state == NCS_LOBBY && st.lobbyValid && st.lobby.code[0] &&
                (st.lobby.flags & NL_ONLINE)) {
                say("%s %s %s %d %d %d %d", quick ? "QUICKHOST" : "CODE", st.lobby.code,
                    (st.lobby.flags & NL_PUBLIC) ? "public" : "private", (int)st.lobby.settings.scenario,
                    (int)st.lobby.settings.stage, (int)st.lobby.settings.weapons, (int)st.lobby.max_players);
                shownCode = 1;
            }
            if (shownCode && st.lobbyValid && st.lobby.num_players >= 2) {
                NdpResult r;
                say("JOINED %s", otherPlayer(&st));
                memset(&r, 0, sizeof(r));
                r.durationSec = 300;
                r.stage = 7;
                r.n = 2;
                netStrCopy(r.players[0].name, NET_NAME_MAX, name);
                r.players[0].kills = 3;
                r.players[0].deaths = 2;
                netStrCopy(r.players[1].name, NET_NAME_MAX, otherPlayer(&st));
                r.players[1].kills = 2;
                r.players[1].deaths = 3;
                if (report) netRuntimeReportMatch(&r);
                netSleepUs(poll ? 2500000 : 1000000);   /* let it go out */
                say("DONE");
                rc = 0;
                goto out;
            }
        } else if (st.state == NCS_LOBBY && st.lobbyValid) {
            say("INLOBBY %d %s", st.lobby.num_players, st.hostAddr);
            netSleepUs(3000000);   /* stay while the host notices us */
            rc = 0;
            goto out;
        }
        netSleepUs(10000);
    }
    say("TIMEOUT %s", text);
out:
    netRuntimeStop();
    return rc;
}
