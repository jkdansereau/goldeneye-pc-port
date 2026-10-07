/*
 * net_session.c -- connection + reliable channel (D409). See net_session.h.
 */
#include "net_session.h"
#include "net_plat.h"

#include <stdlib.h>
#include <string.h>

#define PING_INTERVAL_US      500000u
#define KEEPALIVE_US          250000u
#define RTO_MIN_US            60000u
#define RTO_MAX_US            1000000u
#define SESSION_HDR_BYTES     (NET_HEADER_SIZE + 2 + 1)
#define MSG_HDR_UNREL         3
#define MSG_HDR_REL           5

static int seqLess(uint16_t a, uint16_t b)
{
    return (int16_t)(uint16_t)(a - b) < 0;
}

void netConnInit(NetConn *c, const NetAddr *addr, uint32_t connId, uint64_t nowUs)
{
    memset(c, 0, sizeof(*c));
    c->addr = *addr;
    c->connId = connId;
    c->createdUs = nowUs;
    c->lastRecvUs = nowUs;
    c->lastSendUs = 0;
    c->lastPingUs = 0;
    c->rttMs = 100.0f;
    c->rttVarMs = 50.0f;
}

void netConnFree(NetConn *c)
{
    int i;
    for (i = 0; i < NET_REL_WINDOW; i++) {
        if (c->sendq[i].used) free(c->sendq[i].data);
        if (c->recvq[i].used) free(c->recvq[i].data);
        c->sendq[i].used = 0;
        c->recvq[i].used = 0;
        c->sendq[i].data = NULL;
        c->recvq[i].data = NULL;
    }
}

int netConnSendReliable(NetConn *c, uint8_t type, const void *data, int len)
{
    NetRelMsg *m;
    if (len < 0 || len > NET_REL_MAXMSG) return -1;
    if ((uint16_t)(c->sendSeq - c->sendAcked) >= NET_REL_WINDOW) {
        c->failed = 1;
        return -1;
    }
    m = &c->sendq[c->sendSeq % NET_REL_WINDOW];
    if (m->used) {   /* cannot happen while the window check holds */
        c->failed = 1;
        return -1;
    }
    m->data = (uint8_t *)malloc(len > 0 ? (size_t)len : 1);
    if (!m->data) {
        c->failed = 1;
        return -1;
    }
    if (len) memcpy(m->data, data, (size_t)len);
    m->len = (uint16_t)len;
    m->seq = c->sendSeq;
    m->type = type;
    m->used = 1;
    m->sends = 0;
    m->lastSendUs = 0;
    c->sendSeq++;
    return 0;
}

int netConnPendingReliable(const NetConn *c)
{
    return (int)(uint16_t)(c->sendSeq - c->sendAcked);
}

static void rttSample(NetConn *c, float ms)
{
    if (ms < 0.0f) return;
    if (ms > 5000.0f) ms = 5000.0f;
    if (c->rttSamples == 0) {
        c->rttMs = ms;
        c->rttVarMs = ms * 0.5f;
    } else {
        float d = c->rttMs - ms;
        if (d < 0) d = -d;
        c->rttVarMs = 0.75f * c->rttVarMs + 0.25f * d;
        c->rttMs = 0.875f * c->rttMs + 0.125f * ms;
    }
    c->rttSamples++;
}

uint64_t netConnRtoUs(const NetConn *c)
{
    float ms = c->rttMs * 1.5f + 2.0f * c->rttVarMs;
    uint64_t us = (uint64_t)(ms * 1000.0f);
    if (us < RTO_MIN_US) us = RTO_MIN_US;
    if (us > RTO_MAX_US) us = RTO_MAX_US;
    return us;
}

int netConnReceive(NetConn *c, NetR *r, uint64_t nowUs, NetMsgFn fn, void *ctx)
{
    uint16_t ack = nrU16(r);
    int count = nrU8(r);
    int i;

    if (r->err) return -1;
    c->lastRecvUs = nowUs;
    c->pktsIn++;

    /* Cumulative ack: peer holds every seq < ack. Accept only if it lies in
     * [sendAcked, sendSeq]. */
    if (!seqLess(ack, c->sendAcked) && !seqLess(c->sendSeq, ack)) {
        while (c->sendAcked != ack) {
            NetRelMsg *m = &c->sendq[c->sendAcked % NET_REL_WINDOW];
            if (m->used && m->seq == c->sendAcked) {
                free(m->data);
                m->data = NULL;
                m->used = 0;
            }
            c->sendAcked++;
        }
    }

    for (i = 0; i < count; i++) {
        uint8_t type = nrU8(r);
        int len = nrU16(r);
        uint16_t seq = 0;
        NetR sub;
        if (NM_IS_RELIABLE(type)) seq = nrU16(r);
        if (r->err || len > nrLeft(r)) return -1;
        nrInit(&sub, r->buf + r->pos, len);
        nrSkip(r, len);

        if (type == NM_PING) {
            uint32_t echo = nrU32(&sub);
            if (!sub.err) {
                c->pongPending = 1;
                c->pongEcho = echo;
            }
            continue;
        }
        if (type == NM_PONG) {
            uint32_t echo = nrU32(&sub);
            if (!sub.err) {
                uint32_t now32 = (uint32_t)nowUs;
                rttSample(c, (float)(uint32_t)(now32 - echo) / 1000.0f);
            }
            continue;
        }
        if (!NM_IS_RELIABLE(type)) {
            if (fn) fn(ctx, c, type, &sub);
            continue;
        }

        c->ackDirty = 1;
        {
            int16_t d = (int16_t)(uint16_t)(seq - c->recvNext);
            if (d < 0 || d >= NET_REL_WINDOW) {
                continue;   /* duplicate, or beyond our window (will be resent) */
            }
            if (d == 0) {
                if (fn) fn(ctx, c, type, &sub);
                c->recvNext++;
                for (;;) {
                    NetRelMsg *b = &c->recvq[c->recvNext % NET_REL_WINDOW];
                    NetR br;
                    if (!b->used || b->seq != c->recvNext) break;
                    nrInit(&br, b->data, b->len);
                    if (fn) fn(ctx, c, b->type, &br);
                    free(b->data);
                    b->data = NULL;
                    b->used = 0;
                    c->recvNext++;
                }
            } else {
                NetRelMsg *b = &c->recvq[seq % NET_REL_WINDOW];
                if (!b->used) {
                    b->data = (uint8_t *)malloc(len > 0 ? (size_t)len : 1);
                    if (b->data) {
                        if (len) memcpy(b->data, sub.buf, (size_t)len);
                        b->len = (uint16_t)len;
                        b->seq = seq;
                        b->type = type;
                        b->used = 1;
                    }
                }
            }
        }
    }
    return 0;
}

/* Packet under construction. */
typedef struct PktBuild {
    uint8_t buf[NET_MAX_PACKET];
    NetW w;
    int countPos;
    int count;
} PktBuild;

static void pktBegin(PktBuild *p, const NetConn *c)
{
    nwInit(&p->w, p->buf, sizeof(p->buf));
    netWriteHeader(&p->w, NP_SESSION, c->connId);
    nwU16(&p->w, c->recvNext);
    p->countPos = p->w.len;
    nwU8(&p->w, 0);
    p->count = 0;
}

static int pktFits(const PktBuild *p, int bytes)
{
    return p->count < 255 && nwRoom(&p->w) >= bytes;
}

static void pktSend(PktBuild *p, NetConn *c, NetTransport *t, uint64_t nowUs)
{
    p->buf[p->countPos] = (uint8_t)p->count;
    netFinishHeader(&p->w);
    if (!p->w.overflow) {
        t->send(t->ctx, &c->addr, p->buf, p->w.len);
        c->pktsOut++;
        c->lastSendUs = nowUs;
        c->ackDirty = 0;
    }
}

void netConnTransmit(NetConn *c, NetTransport *t, uint64_t nowUs,
                     const NetUnrel *unrel, int nUnrel, int forceAck)
{
    PktBuild pkt;
    int ui = 0;
    uint16_t rs = c->sendAcked;
    uint64_t rto = netConnRtoUs(c);
    int wantPing = (c->lastPingUs == 0 || nowUs - c->lastPingUs >= PING_INTERVAL_US);
    int wantPong = c->pongPending;
    int guard;

    for (guard = 0; guard < 64; guard++) {
        int progressed = 0;
        int pending = 0;
        pktBegin(&pkt, c);

        if (wantPong && pktFits(&pkt, MSG_HDR_UNREL + 4)) {
            nwU8(&pkt.w, NM_PONG);
            nwU16(&pkt.w, 4);
            nwU32(&pkt.w, c->pongEcho);
            pkt.count++;
            wantPong = 0;
            c->pongPending = 0;
            progressed = 1;
        }
        if (wantPing && pktFits(&pkt, MSG_HDR_UNREL + 4)) {
            nwU8(&pkt.w, NM_PING);
            nwU16(&pkt.w, 4);
            nwU32(&pkt.w, (uint32_t)nowUs);
            pkt.count++;
            wantPing = 0;
            c->lastPingUs = nowUs;
            progressed = 1;
        }

        while (ui < nUnrel) {
            const NetUnrel *u = &unrel[ui];
            if (u->len < 0 || u->len > NET_MAX_PACKET - SESSION_HDR_BYTES - MSG_HDR_UNREL) {
                ui++;   /* cannot ever fit: drop */
                continue;
            }
            if (!pktFits(&pkt, MSG_HDR_UNREL + u->len)) break;
            nwU8(&pkt.w, u->type);
            nwU16(&pkt.w, (uint16_t)u->len);
            nwBytes(&pkt.w, u->data, u->len);
            pkt.count++;
            ui++;
            progressed = 1;
        }

        while (rs != c->sendSeq) {
            NetRelMsg *m = &c->sendq[rs % NET_REL_WINDOW];
            int due;
            if (!m->used || m->seq != rs) {
                rs++;
                continue;
            }
            {
                uint64_t backoff = rto;
                int k;
                for (k = 1; k < m->sends && backoff < RTO_MAX_US; k++) backoff *= 2;
                if (backoff > RTO_MAX_US) backoff = RTO_MAX_US;
                due = (m->sends == 0) || (nowUs - m->lastSendUs >= backoff);
            }
            if (!due) {
                rs++;
                continue;
            }
            if (!pktFits(&pkt, MSG_HDR_REL + m->len)) {
                pending = 1;
                break;
            }
            nwU8(&pkt.w, m->type);
            nwU16(&pkt.w, m->len);
            nwU16(&pkt.w, m->seq);
            nwBytes(&pkt.w, m->data, m->len);
            pkt.count++;
            if (m->sends > 0) c->resends++;
            if (m->sends < 0xFFFF) m->sends++;
            m->lastSendUs = nowUs;
            rs++;
            progressed = 1;
        }

        if (pkt.count > 0 || forceAck || c->ackDirty ||
            (c->lastSendUs == 0 || nowUs - c->lastSendUs >= KEEPALIVE_US)) {
            pktSend(&pkt, c, t, nowUs);
            forceAck = 0;
        }
        if (!pending && ui >= nUnrel && !wantPing && !wantPong) break;
        if (!progressed && !pending) break;
    }
}
