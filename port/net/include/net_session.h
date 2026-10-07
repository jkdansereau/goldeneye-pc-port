/*
 * net_session.h -- one peer connection: keepalive, RTT, and a reliable,
 * ordered message channel multiplexed with unreliable messages (D409).
 *
 * SESSION packet payload:
 *   u16 ack      -- receiver's next expected reliable seq (cumulative ack)
 *   u8  count    -- messages that follow
 *   per message: u8 type, u16 len, [u16 seq if NM_IS_RELIABLE(type)], bytes
 *
 * Reliable messages are retransmitted after ~RTO until acked and delivered
 * strictly in order; unreliable ones are delivered on arrival or never.
 */
#ifndef GE_NET_SESSION_H
#define GE_NET_SESSION_H

#include <stdint.h>

#include "net_proto.h"
#include "net_sock.h"
#include "net_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NET_REL_WINDOW 64
#define NET_REL_MAXMSG 1100

typedef struct NetRelMsg {
    uint8_t *data;
    uint16_t len;
    uint16_t seq;
    uint8_t type;
    uint8_t used;
    uint16_t sends;
    uint64_t lastSendUs;
} NetRelMsg;

typedef struct NetConn {
    NetAddr addr;
    uint32_t connId;
    uint64_t createdUs;
    uint64_t lastRecvUs;
    uint64_t lastSendUs;
    uint64_t lastPingUs;
    float rttMs;
    float rttVarMs;
    int rttSamples;

    uint16_t sendSeq;     /* next seq to assign */
    uint16_t sendAcked;   /* peer has every seq < sendAcked */
    NetRelMsg sendq[NET_REL_WINDOW];

    uint16_t recvNext;    /* next in-order seq we expect */
    NetRelMsg recvq[NET_REL_WINDOW];
    int ackDirty;

    int pongPending;      /* a PING arrived; answer on next transmit */
    uint32_t pongEcho;

    uint32_t pktsIn, pktsOut, resends;
    int failed;           /* reliable window overflowed: peer unresponsive */
} NetConn;

typedef struct NetUnrel {
    uint8_t type;
    const uint8_t *data;
    int len;
} NetUnrel;

/* Message sink: called for every delivered message (in order for reliable). */
typedef void (*NetMsgFn)(void *ctx, NetConn *c, uint8_t type, NetR *payload);

void netConnInit(NetConn *c, const NetAddr *addr, uint32_t connId, uint64_t nowUs);
void netConnFree(NetConn *c);

/* Queue a reliable message (copied). -1 if the window is full (the conn is
 * then flagged `failed`, which owners treat as a disconnect). */
int netConnSendReliable(NetConn *c, uint8_t type, const void *data, int len);
int netConnPendingReliable(const NetConn *c);

/* Parse a SESSION payload (after the packet header) and deliver messages. */
int netConnReceive(NetConn *c, NetR *payload, uint64_t nowUs, NetMsgFn fn, void *ctx);

/* Send everything due: the given unreliable messages, reliable messages whose
 * retransmit timer expired, a ping if due, and an ack if owed (or forceAck).
 * Splits across packets as needed. */
void netConnTransmit(NetConn *c, NetTransport *t, uint64_t nowUs,
                     const NetUnrel *unrel, int nUnrel, int forceAck);

/* Current retransmission timeout in microseconds. */
uint64_t netConnRtoUs(const NetConn *c);

#ifdef __cplusplus
}
#endif

#endif /* GE_NET_SESSION_H */
