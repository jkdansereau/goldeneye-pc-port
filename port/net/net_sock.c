/*
 * net_sock.c -- UDP sockets + addresses for the netplay core (D409).
 */
#include "net_sock.h"
#include "net_plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
/* inet_pton/getaddrinfo need the Vista+ API surface (MinGW gates them). */
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
typedef SOCKET net_fd_t;
#define NET_BADFD INVALID_SOCKET
#define net_closesock closesocket
#define net_lasterr() WSAGetLastError()
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int net_fd_t;
#define NET_BADFD (-1)
#define net_closesock close
#define net_lasterr() errno
#endif

struct NetUdp {
    net_fd_t fd;
    uint16_t port;
};

static int s_sockRefs = 0;

int netSockStartup(void)
{
#if defined(_WIN32)
    if (s_sockRefs++ == 0) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            s_sockRefs = 0;
            return -1;
        }
    }
#else
    s_sockRefs++;
#endif
    return 0;
}

void netSockCleanup(void)
{
    if (s_sockRefs <= 0) return;
#if defined(_WIN32)
    if (--s_sockRefs == 0) {
        WSACleanup();
    }
#else
    s_sockRefs--;
#endif
}

static void fillSockaddr(struct sockaddr_in *sa, const NetAddr *a)
{
    memset(sa, 0, sizeof(*sa));
    sa->sin_family = AF_INET;
    sa->sin_addr.s_addr = htonl(a->ip);
    sa->sin_port = htons(a->port);
}

NetUdp *netUdpOpen(uint16_t port, int allowBroadcast, char *err, int errLen)
{
    NetUdp *u;
    net_fd_t fd;
    struct sockaddr_in sa;
    int one = 1;
    int bufsz = 256 * 1024;

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == NET_BADFD) {
        netStrFmt(err, errLen, "socket() failed (%d)", (int)net_lasterr());
        return NULL;
    }

#if defined(_WIN32)
    {
        u_long nb = 1;
        DWORD bytes = 0;
        BOOL off = FALSE;
        ioctlsocket(fd, FIONBIO, &nb);
        /* Windows reports ICMP port-unreachable for a previous send as an
         * error on the NEXT recvfrom; for a server that is just "a peer went
         * away" and must not stall the receive loop. */
        WSAIoctl(fd, SIO_UDP_CONNRESET, &off, sizeof(off), NULL, 0, &bytes, NULL, NULL);
    }
#else
    {
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
#endif
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const char *)&bufsz, sizeof(bufsz));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char *)&bufsz, sizeof(bufsz));
    if (allowBroadcast) {
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof(one));
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        netStrFmt(err, errLen, "cannot bind UDP port %u (%d) -- in use?", (unsigned)port, (int)net_lasterr());
        net_closesock(fd);
        return NULL;
    }

    u = (NetUdp *)calloc(1, sizeof(*u));
    if (!u) {
        net_closesock(fd);
        netStrFmt(err, errLen, "out of memory");
        return NULL;
    }
    u->fd = fd;
    {
        struct sockaddr_in got;
        socklen_t gl = (socklen_t)sizeof(got);
        memset(&got, 0, sizeof(got));
        if (getsockname(fd, (struct sockaddr *)&got, &gl) == 0) {
            u->port = ntohs(got.sin_port);
        } else {
            u->port = port;
        }
    }
    return u;
}

void netUdpClose(NetUdp *u)
{
    if (!u) return;
    if (u->fd != NET_BADFD) net_closesock(u->fd);
    free(u);
}

uint16_t netUdpLocalPort(NetUdp *u)
{
    return u ? u->port : 0;
}

int netUdpSend(NetUdp *u, const NetAddr *to, const void *data, int len)
{
    struct sockaddr_in sa;
    int r;
    if (!u || !to || len <= 0) return -1;
    fillSockaddr(&sa, to);
    r = (int)sendto(u->fd, (const char *)data, len, 0, (struct sockaddr *)&sa, sizeof(sa));
    if (r < 0) {
        int e = net_lasterr();
#if defined(_WIN32)
        if (e == WSAEWOULDBLOCK || e == WSAENOBUFS || e == WSAEHOSTUNREACH || e == WSAENETUNREACH) return 0;
#else
        if (e == EAGAIN || e == EWOULDBLOCK || e == ENOBUFS || e == EHOSTUNREACH || e == ENETUNREACH ||
            e == ECONNREFUSED) return 0;
#endif
        return -1;
    }
    return 0;
}

int netUdpRecv(NetUdp *u, NetAddr *from, void *buf, int cap)
{
    int tries;
    if (!u) return -1;
    /* Loop past benign per-datagram errors (oversized / ICMP resets) so a
     * 0 return really means "drained". Bounded so a flood can't pin us. */
    for (tries = 0; tries < 64; tries++) {
        struct sockaddr_in sa;
        socklen_t sl = (socklen_t)sizeof(sa);
        int r = (int)recvfrom(u->fd, (char *)buf, cap, 0, (struct sockaddr *)&sa, &sl);
        if (r >= 0) {
            if (sa.sin_family != AF_INET) continue;
            if (from) {
                from->ip = ntohl(sa.sin_addr.s_addr);
                from->port = ntohs(sa.sin_port);
            }
            if (r == 0) continue;   /* empty datagram: ignore */
            return r;
        } else {
            int e = net_lasterr();
#if defined(_WIN32)
            if (e == WSAEWOULDBLOCK) return 0;
            if (e == WSAEMSGSIZE || e == WSAECONNRESET || e == WSAENETRESET) continue;
#else
            if (e == EAGAIN || e == EWOULDBLOCK) return 0;
            if (e == EINTR || e == ECONNREFUSED || e == EMSGSIZE) continue;
#endif
            return -1;
        }
    }
    return 0;
}

int netUdpWait(NetUdp **socks, int n, uint32_t timeoutUs)
{
    fd_set rs;
    struct timeval tv;
    int i;
    int any = 0;
#if !defined(_WIN32)
    int maxfd = -1;
#endif
    FD_ZERO(&rs);
    for (i = 0; i < n; i++) {
        if (!socks[i]) continue;
        FD_SET(socks[i]->fd, &rs);
        any = 1;
#if !defined(_WIN32)
        if (socks[i]->fd > maxfd) maxfd = socks[i]->fd;
#endif
    }
    if (!any) {
        netSleepUs(timeoutUs);
        return 0;
    }
    tv.tv_sec = (long)(timeoutUs / 1000000u);
    tv.tv_usec = (long)(timeoutUs % 1000000u);
#if defined(_WIN32)
    return select(0, &rs, NULL, NULL, &tv);
#else
    return select(maxfd + 1, &rs, NULL, NULL, &tv);
#endif
}

NetAddr netAddrMake(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port)
{
    NetAddr r;
    r.ip = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
    r.port = port;
    return r;
}

int netAddrResolve(const char *str, uint16_t defaultPort, NetAddr *out)
{
    char host[256];
    const char *colon;
    long port = defaultPort;
    int hl;
    struct in_addr ia;

    if (!str || !out) return -1;
    while (*str == ' ' || *str == '\t') str++;
    if (!*str) return -1;

    colon = strrchr(str, ':');
    if (colon) {
        char *end = NULL;
        hl = (int)(colon - str);
        port = strtol(colon + 1, &end, 10);
        if (!end || end == colon + 1 || port <= 0 || port > 65535) return -1;
    } else {
        hl = (int)strlen(str);
    }
    if (hl <= 0 || hl >= (int)sizeof(host)) return -1;
    memcpy(host, str, (size_t)hl);
    host[hl] = 0;
    while (hl > 0 && (host[hl - 1] == ' ' || host[hl - 1] == '\t')) host[--hl] = 0;

    if (inet_pton(AF_INET, host, &ia) == 1) {
        out->ip = ntohl(ia.s_addr);
        out->port = (uint16_t)port;
        return 0;
    }
    {
        struct addrinfo hints, *res = NULL, *it;
        int rc;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        rc = getaddrinfo(host, NULL, &hints, &res);
        if (rc != 0 || !res) return -1;
        for (it = res; it; it = it->ai_next) {
            if (it->ai_family == AF_INET && it->ai_addr) {
                const struct sockaddr_in *sin = (const struct sockaddr_in *)it->ai_addr;
                out->ip = ntohl(sin->sin_addr.s_addr);
                out->port = (uint16_t)port;
                freeaddrinfo(res);
                return 0;
            }
        }
        freeaddrinfo(res);
    }
    return -1;
}

void netAddrFormat(const NetAddr *a, char *buf, int n)
{
    if (!a) {
        netStrCopy(buf, n, "?");
        return;
    }
    netStrFmt(buf, n, "%u.%u.%u.%u:%u",
              (unsigned)((a->ip >> 24) & 0xFF), (unsigned)((a->ip >> 16) & 0xFF),
              (unsigned)((a->ip >> 8) & 0xFF), (unsigned)(a->ip & 0xFF), (unsigned)a->port);
}

int netAddrEq(const NetAddr *a, const NetAddr *b)
{
    return a->ip == b->ip && a->port == b->port;
}

int netLocalIPv4(char *buf, int n)
{
    /* connect() on a UDP socket only selects a route -- nothing is sent. The
     * target is a documentation address (TEST-NET-1); any non-local address
     * resolves to the default-route interface, which is the one to share. */
    net_fd_t fd;
    struct sockaddr_in sa, got;
    socklen_t gl = (socklen_t)sizeof(got);
    NetAddr probe = netAddrMake(192, 0, 2, 1, 9);
    int ok = -1;
    if (n > 0) buf[0] = 0;
    if (netSockStartup() != 0) return -1;
    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd != NET_BADFD) {
        fillSockaddr(&sa, &probe);
        memset(&got, 0, sizeof(got));
        if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0 &&
            getsockname(fd, (struct sockaddr *)&got, &gl) == 0 && got.sin_addr.s_addr != 0) {
            uint32_t ip = ntohl(got.sin_addr.s_addr);
            netStrFmt(buf, n, "%u.%u.%u.%u", (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
                      (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF));
            ok = 0;
        }
        net_closesock(fd);
    }
    netSockCleanup();
    return ok;
}

static int udpTransportSend(void *ctx, const NetAddr *to, const void *data, int len)
{
    return netUdpSend((NetUdp *)ctx, to, data, len);
}

static int udpTransportRecv(void *ctx, NetAddr *from, void *buf, int cap)
{
    return netUdpRecv((NetUdp *)ctx, from, buf, cap);
}

NetTransport netUdpTransport(NetUdp *u)
{
    NetTransport t;
    t.ctx = u;
    t.send = udpTransportSend;
    t.recv = udpTransportRecv;
    return t;
}
