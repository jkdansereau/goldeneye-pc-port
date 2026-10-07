/*
 * net_stun.h -- minimal STUN (RFC 5389) Binding client for NAT traversal (D410).
 *
 * Online play through the directory service is peer-to-peer: the host is a
 * player's own game. To be reachable through home routers, each side asks a
 * public STUN server which public address/port its UDP socket appears as
 * (the "mapped address"), publishes it via the directory, and both sides then
 * send to each other's mapped address at the same time ("hole punching").
 *
 * Only the Binding request/response is implemented -- no authentication,
 * no TURN. Messages are sent from the game's own UDP sockets so the mapping
 * learned is the one the game traffic will use.
 */
#ifndef GE_NET_STUN_H
#define GE_NET_STUN_H

#include <stdint.h>

#include "net_sock.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cloudflare's public STUN service: free and unlimited (no account). */
#define NET_STUN_DEFAULT_HOST "stun.cloudflare.com"
#define NET_STUN_DEFAULT_PORT 3478

#define NET_STUN_REQUEST_LEN 20

typedef struct NetStunTx {
    uint8_t id[12];   /* transaction id */
} NetStunTx;

/* A Binding request with a fresh random transaction id. Returns its length
 * (NET_STUN_REQUEST_LEN), or -1 if cap is too small. */
int netStunBuildRequest(uint8_t *out, int cap, NetStunTx *tx);

/* 1 if the datagram looks like a STUN message (top bits 00, magic cookie),
 * so a socket shared with the game protocol can route it. */
int netStunIsMessage(const uint8_t *p, int len);

/* Parse a Binding success response to `tx`. 0 = `mapped` filled from
 * XOR-MAPPED-ADDRESS (or MAPPED-ADDRESS); -1 = not ours / malformed / not
 * IPv4. */
int netStunParseResponse(const uint8_t *p, int len, const NetStunTx *tx, NetAddr *mapped);

#ifdef __cplusplus
}
#endif

#endif /* GE_NET_STUN_H */
