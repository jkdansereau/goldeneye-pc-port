/*
 * net_sock.h -- UDP sockets + addresses for the netplay core (D409).
 *
 * IPv4 only in v1. Addresses are kept in HOST byte order inside NetAddr.
 * Every protocol object (host / client) talks to the network through a
 * NetTransport so the selftest can substitute a simulated lossy network.
 */
#ifndef GE_NET_SOCK_H
#define GE_NET_SOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NetAddr {
    uint32_t ip;    /* host byte order, e.g. 127.0.0.1 == 0x7F000001 */
    uint16_t port;  /* host byte order */
} NetAddr;

/* Process-wide socket init (WSAStartup on Windows). Ref-counted. */
int netSockStartup(void);
void netSockCleanup(void);

typedef struct NetUdp NetUdp;

/* Open a non-blocking UDP socket bound to INADDR_ANY:port (0 = ephemeral).
 * Returns NULL on failure with a reason in err. */
NetUdp *netUdpOpen(uint16_t port, int allowBroadcast, char *err, int errLen);
void netUdpClose(NetUdp *u);
uint16_t netUdpLocalPort(NetUdp *u);

/* 0 on success (or a benign drop), -1 on a hard error. */
int netUdpSend(NetUdp *u, const NetAddr *to, const void *data, int len);
/* >0: datagram length; 0: nothing pending; -1: error. Oversized datagrams
 * are truncated and reported as 0 (dropped). */
int netUdpRecv(NetUdp *u, NetAddr *from, void *buf, int cap);
/* Wait until any socket is readable or the timeout elapses. Returns >0 if
 * something is readable, 0 on timeout, -1 on error. NULL entries ignored. */
int netUdpWait(NetUdp **socks, int n, uint32_t timeoutUs);

/* Parse "host", "host:port", "1.2.3.4:5" (DNS allowed -- BLOCKING, call it
 * off the game thread). Returns 0 on success. */
int netAddrResolve(const char *str, uint16_t defaultPort, NetAddr *out);
void netAddrFormat(const NetAddr *a, char *buf, int n);
int netAddrEq(const NetAddr *a, const NetAddr *b);
NetAddr netAddrMake(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port);

/* This PC's IPv4 address on its default-route interface ("192.168.1.20"),
 * for showing to the people who will join. Sends nothing. 0 on success. */
int netLocalIPv4(char *buf, int n);

/* Transport indirection. send returns 0/-1, recv as netUdpRecv. */
typedef struct NetTransport {
    void *ctx;
    int (*send)(void *ctx, const NetAddr *to, const void *data, int len);
    int (*recv)(void *ctx, NetAddr *from, void *buf, int cap);
} NetTransport;

NetTransport netUdpTransport(NetUdp *u);

#ifdef __cplusplus
}
#endif

#endif /* GE_NET_SOCK_H */
