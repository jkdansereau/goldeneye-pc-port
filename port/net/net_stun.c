/*
 * net_stun.c -- minimal STUN Binding client (D410). See net_stun.h.
 */
#include "net_stun.h"
#include "net_plat.h"

#include <string.h>

#define STUN_MAGIC          0x2112A442u
#define STUN_BINDING_REQ    0x0001u
#define STUN_BINDING_OK     0x0101u
#define ATTR_MAPPED_ADDR    0x0001u
#define ATTR_XOR_MAPPED     0x0020u

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int netStunBuildRequest(uint8_t *out, int cap, NetStunTx *tx)
{
    if (!out || !tx || cap < NET_STUN_REQUEST_LEN) return -1;
    netRandomBytes(tx->id, sizeof(tx->id));
    out[0] = (uint8_t)(STUN_BINDING_REQ >> 8);
    out[1] = (uint8_t)(STUN_BINDING_REQ & 0xFF);
    out[2] = 0;   /* no attributes */
    out[3] = 0;
    out[4] = (uint8_t)((STUN_MAGIC >> 24) & 0xFFu);
    out[5] = (uint8_t)((STUN_MAGIC >> 16) & 0xFFu);
    out[6] = (uint8_t)((STUN_MAGIC >> 8) & 0xFFu);
    out[7] = (uint8_t)(STUN_MAGIC & 0xFFu);
    memcpy(out + 8, tx->id, sizeof(tx->id));
    return NET_STUN_REQUEST_LEN;
}

int netStunIsMessage(const uint8_t *p, int len)
{
    return p && len >= 20 && (p[0] & 0xC0) == 0 && be32(p + 4) == STUN_MAGIC;
}

int netStunParseResponse(const uint8_t *p, int len, const NetStunTx *tx, NetAddr *mapped)
{
    int bodyLen, off, haveMapped = 0;
    NetAddr plain;
    if (!netStunIsMessage(p, len) || !tx || !mapped) return -1;
    if (be16(p) != STUN_BINDING_OK) return -1;
    if (memcmp(p + 8, tx->id, sizeof(tx->id)) != 0) return -1;
    bodyLen = be16(p + 2);
    if ((bodyLen & 3) != 0 || 20 + bodyLen > len) return -1;
    memset(&plain, 0, sizeof(plain));
    for (off = 20; off + 4 <= 20 + bodyLen;) {
        uint16_t type = be16(p + off);
        int alen = be16(p + off + 2);
        const uint8_t *v = p + off + 4;
        if (off + 4 + alen > 20 + bodyLen) return -1;
        if ((type == ATTR_XOR_MAPPED || type == ATTR_MAPPED_ADDR) && alen >= 8 && v[1] == 0x01 /* IPv4 */) {
            uint16_t port = be16(v + 2);
            uint32_t ip = be32(v + 4);
            if (type == ATTR_XOR_MAPPED) {
                mapped->port = (uint16_t)(port ^ (uint16_t)(STUN_MAGIC >> 16));
                mapped->ip = ip ^ STUN_MAGIC;
                return 0;   /* the XOR form wins (NATs rewrite the plain one) */
            }
            plain.port = port;
            plain.ip = ip;
            haveMapped = 1;
        }
        off += 4 + ((alen + 3) & ~3);   /* attributes are padded to 4 bytes */
    }
    if (haveMapped) {
        *mapped = plain;
        return 0;
    }
    return -1;
}
