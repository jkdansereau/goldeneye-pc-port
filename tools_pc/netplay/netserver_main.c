/*
 * ge007-netserver -- GoldenEye 007 PC port matchmaking + relay server (D409).
 *
 * Hosts any number of online lobbies: players connect out to this server (no
 * port forwarding needed on their side), list public lobbies, create one
 * (public or private with a 6-character join code), or quick-match. During a
 * match the server only relays controller input -- it never runs the game, so
 * a small VPS handles many matches. See docs/netplay.md.
 *
 *   ge007-netserver [--port N] [--max-sessions N] [--max-lobbies N]
 *                   [--max-per-ip N] [--stats SECONDS] [-v]
 */
#include "net_gamedata.h"
#include "net_host.h"
#include "net_plat.h"
#include "net_proto.h"
#include "net_sock.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t g_quit = 0;
static int g_verbose = 0;

static void onSignal(int sig)
{
    (void)sig;
    g_quit = 1;
}

static void logFn(void *ctx, int level, const char *msg)
{
    static const char *const names[] = { "debug", "info", "warn", "error" };
    char ts[32];
    time_t t = time(NULL);
    struct tm *tmv = localtime(&t);
    (void)ctx;
    if (level == NETLOG_DEBUG && !g_verbose) return;
    if (tmv) strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tmv);
    else netStrCopy(ts, sizeof(ts), "?");
    printf("%s [%s] %s\n", ts, names[(level >= 0 && level <= 3) ? level : 1], msg);
    fflush(stdout);
}

static void usage(const char *argv0)
{
    printf("GoldenEye 007 PC port -- matchmaking / relay server (protocol v%d)\n\n"
           "usage: %s [options]\n"
           "  --port N          UDP port to listen on (default %d)\n"
           "  --max-sessions N  concurrent connections (default 512)\n"
           "  --max-lobbies N   concurrent lobbies (default 128)\n"
           "  --max-per-ip N    connections per IP address (default 8)\n"
           "  --stats SECONDS   print a status line every N seconds (default 60, 0 = off)\n"
           "  -v                verbose (debug) logging\n\n"
           "Players set this server's address in the game's Online menu (Settings)\n"
           "or in ge007.ini: [Net] Server = host:port\n",
           NET_PROTO_VERSION, argv0, NET_DEFAULT_SERVER_PORT);
}

static int argInt(int argc, char **argv, int *i, int lo, int hi, int *out)
{
    char *end = NULL;
    long v;
    if (*i + 1 >= argc) return -1;
    v = strtol(argv[*i + 1], &end, 10);
    if (!end || *end || v < lo || v > hi) return -1;
    *out = (int)v;
    (*i)++;
    return 0;
}

int main(int argc, char **argv)
{
    NetHostConfig hc;
    NetUdp *sock;
    NetHost *host;
    char err[160];
    int port = NET_DEFAULT_SERVER_PORT;
    int statsSec = 60;
    uint64_t lastStats;
    int i;

    memset(&hc, 0, sizeof(hc));
    hc.serverMode = 1;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        int bad = 0;
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(a, "-v")) {
            g_verbose = 1;
        } else if (!strcmp(a, "--port")) {
            bad = argInt(argc, argv, &i, 1, 65535, &port);
        } else if (!strcmp(a, "--max-sessions")) {
            bad = argInt(argc, argv, &i, 2, 65536, &hc.maxSessions);
        } else if (!strcmp(a, "--max-lobbies")) {
            bad = argInt(argc, argv, &i, 1, 16384, &hc.maxLobbies);
        } else if (!strcmp(a, "--max-per-ip")) {
            bad = argInt(argc, argv, &i, 1, 1024, &hc.maxPerIp);
        } else if (!strcmp(a, "--stats")) {
            bad = argInt(argc, argv, &i, 0, 86400, &statsSec);
        } else {
            bad = 1;
        }
        if (bad) {
            fprintf(stderr, "bad argument: %s\n\n", a);
            usage(argv[0]);
            return 2;
        }
    }

    if (netSockStartup() != 0) {
        fprintf(stderr, "socket startup failed\n");
        return 1;
    }
    sock = netUdpOpen((uint16_t)port, 0, err, sizeof(err));
    if (!sock) {
        fprintf(stderr, "%s\n", err);
        netSockCleanup();
        return 1;
    }
    hc.log = logFn;
    host = netHostCreate(&hc, netUdpTransport(sock));
    if (!host) {
        fprintf(stderr, "could not create the server\n");
        netUdpClose(sock);
        netSockCleanup();
        return 1;
    }

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    {
        char line[96];
        netStrFmt(line, sizeof(line), "ge007-netserver listening on UDP %d (protocol v%d)", port, NET_PROTO_VERSION);
        logFn(NULL, NETLOG_INFO, line);
    }

    lastStats = netTimeUs();
    while (!g_quit) {
        NetUdp *socks[1];
        uint64_t now;
        socks[0] = sock;
        netUdpWait(socks, 1, 1000);
        now = netTimeUs();
        netHostPump(host, now);
        if (statsSec > 0 && now - lastStats >= (uint64_t)statsSec * 1000000ull) {
            NetHostStats st;
            char line[160];
            lastStats = now;
            netHostGetStats(host, &st);
            netStrFmt(line, sizeof(line), "status: %d connections, %d lobbies, %d matches running, %u pkts in / %u out",
                      st.sessions, st.lobbies, st.matches, (unsigned)st.packetsIn, (unsigned)st.packetsOut);
            logFn(NULL, NETLOG_INFO, line);
        }
    }

    logFn(NULL, NETLOG_INFO, "shutting down");
    netHostDestroy(host);   /* tells every client goodbye */
    netUdpClose(sock);
    netSockCleanup();
    return 0;
}
