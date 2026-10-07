/*
 * net_wire.h -- bounds-checked little-endian (de)serialization (D413).
 *
 * Every byte that crosses the network goes through these helpers; nothing is
 * ever memcpy'd as a struct, so layout/endianness/padding never matter and a
 * truncated or malicious packet can only set the reader's `err` flag.
 * Floats are carried as their raw IEEE-754 bit pattern so every peer applies
 * bit-identical values (lockstep requirement).
 */
#ifndef GE_NET_WIRE_H
#define GE_NET_WIRE_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NetW {
    uint8_t *buf;
    int cap;
    int len;
    int overflow;
} NetW;

typedef struct NetR {
    const uint8_t *buf;
    int len;
    int pos;
    int err;
} NetR;

static inline void nwInit(NetW *w, void *buf, int cap)
{
    w->buf = (uint8_t *)buf;
    w->cap = cap;
    w->len = 0;
    w->overflow = 0;
}

static inline int nwRoom(const NetW *w) { return w->cap - w->len; }

static inline void nwU8(NetW *w, uint8_t v)
{
    if (w->len + 1 > w->cap) { w->overflow = 1; return; }
    w->buf[w->len++] = v;
}

static inline void nwU16(NetW *w, uint16_t v)
{
    if (w->len + 2 > w->cap) { w->overflow = 1; return; }
    w->buf[w->len++] = (uint8_t)(v & 0xFF);
    w->buf[w->len++] = (uint8_t)(v >> 8);
}

static inline void nwU32(NetW *w, uint32_t v)
{
    if (w->len + 4 > w->cap) { w->overflow = 1; return; }
    w->buf[w->len++] = (uint8_t)(v & 0xFF);
    w->buf[w->len++] = (uint8_t)((v >> 8) & 0xFF);
    w->buf[w->len++] = (uint8_t)((v >> 16) & 0xFF);
    w->buf[w->len++] = (uint8_t)(v >> 24);
}

static inline void nwU64(NetW *w, uint64_t v)
{
    nwU32(w, (uint32_t)(v & 0xFFFFFFFFu));
    nwU32(w, (uint32_t)(v >> 32));
}

static inline void nwF32(NetW *w, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    nwU32(w, bits);
}

static inline void nwBytes(NetW *w, const void *p, int n)
{
    if (n < 0 || w->len + n > w->cap) { w->overflow = 1; return; }
    if (n) memcpy(w->buf + w->len, p, (size_t)n);
    w->len += n;
}

/* u8 length prefix + bytes (no NUL), truncated to maxLen. */
static inline void nwStr(NetW *w, const char *s, int maxLen)
{
    int n = 0;
    if (s) {
        while (n < maxLen && n < 255 && s[n]) n++;
    }
    nwU8(w, (uint8_t)n);
    nwBytes(w, s, n);
}

static inline void nrInit(NetR *r, const void *buf, int len)
{
    r->buf = (const uint8_t *)buf;
    r->len = len;
    r->pos = 0;
    r->err = 0;
}

static inline int nrLeft(const NetR *r) { return r->len - r->pos; }

static inline uint8_t nrU8(NetR *r)
{
    if (r->pos + 1 > r->len) { r->err = 1; return 0; }
    return r->buf[r->pos++];
}

static inline uint16_t nrU16(NetR *r)
{
    uint16_t v;
    if (r->pos + 2 > r->len) { r->err = 1; return 0; }
    v = (uint16_t)(r->buf[r->pos] | ((uint16_t)r->buf[r->pos + 1] << 8));
    r->pos += 2;
    return v;
}

static inline uint32_t nrU32(NetR *r)
{
    uint32_t v;
    if (r->pos + 4 > r->len) { r->err = 1; return 0; }
    v = (uint32_t)r->buf[r->pos] | ((uint32_t)r->buf[r->pos + 1] << 8) |
        ((uint32_t)r->buf[r->pos + 2] << 16) | ((uint32_t)r->buf[r->pos + 3] << 24);
    r->pos += 4;
    return v;
}

static inline uint64_t nrU64(NetR *r)
{
    uint64_t lo = nrU32(r);
    uint64_t hi = nrU32(r);
    return lo | (hi << 32);
}

static inline float nrF32(NetR *r)
{
    uint32_t bits = nrU32(r);
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static inline void nrBytes(NetR *r, void *out, int n)
{
    if (n < 0 || r->pos + n > r->len) { r->err = 1; if (n > 0) memset(out, 0, (size_t)n); return; }
    if (n) memcpy(out, r->buf + r->pos, (size_t)n);
    r->pos += n;
}

static inline void nrSkip(NetR *r, int n)
{
    if (n < 0 || r->pos + n > r->len) { r->err = 1; return; }
    r->pos += n;
}

/* Reads a u8-prefixed string into out[cap] (always NUL-terminated). Bytes
 * beyond cap-1 are consumed and dropped. */
static inline void nrStr(NetR *r, char *out, int cap)
{
    int n = nrU8(r);
    int i;
    if (r->err || r->pos + n > r->len) { r->err = 1; if (cap > 0) out[0] = 0; return; }
    for (i = 0; i < n; i++) {
        if (i < cap - 1) out[i] = (char)r->buf[r->pos + i];
    }
    if (cap > 0) out[(n < cap - 1) ? n : cap - 1] = 0;
    r->pos += n;
}

#ifdef __cplusplus
}
#endif

#endif /* GE_NET_WIRE_H */
