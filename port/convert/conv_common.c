/* conv_common.c -- shared plumbing for the C asset converters. */
#include "convert.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* ------------------------------------------------------------- CvBuf ---- */
void cv_buf_free(CvBuf *b) { free(b->p); b->p = NULL; b->len = b->cap = 0; }

static int cv_buf_reserve(CvBuf *b, size_t extra)
{
    size_t need = b->len + extra;
    if (need <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 4096;
    while (cap < need) cap *= 2;
    uint8_t *p = (uint8_t *)realloc(b->p, cap);
    if (!p) return -1;
    b->p = p; b->cap = cap;
    return 0;
}

int cv_buf_put(CvBuf *b, const void *d, size_t n)
{
    if (cv_buf_reserve(b, n)) return -1;
    if (n) memcpy(b->p + b->len, d, n);
    b->len += n;
    return 0;
}

int cv_buf_zero(CvBuf *b, size_t n)
{
    if (cv_buf_reserve(b, n)) return -1;
    memset(b->p + b->len, 0, n);
    b->len += n;
    return 0;
}

/* --------------------------------------------------------- CvSidecar ---- */
void cv_sc_free(CvSidecar *s)
{
    for (int i = 0; i < s->n; i++) free(s->ent[i].name);
    free(s->ent);
    cv_buf_free(&s->img);
    memset(s, 0, sizeof *s);
}

int cv_sc_emit(CvSidecar *s, const char *name, const void *d, size_t n)
{
    size_t start = (s->img.len + 15) & ~(size_t)15;
    if (cv_buf_zero(&s->img, start - s->img.len)) return -1;
    if (s->n == s->cap) {
        int cap = s->cap ? s->cap * 2 : 128;
        CvEntry *e = (CvEntry *)realloc(s->ent, (size_t)cap * sizeof *e);
        if (!e) return -1;
        s->ent = e; s->cap = cap;
    }
    s->ent[s->n].name = strdup(name);
    s->ent[s->n].off = (uint32_t)s->img.len;
    s->ent[s->n].size = (uint32_t)n;
    s->n++;
    return cv_buf_put(&s->img, d, n);
}

int cv_sc_write(const CvSidecar *s, const char *dir, const char *binname)
{
    char path[1024];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, binname);
    f = fopen(path, "wb");
    if (!f) return -1;
    if (s->img.len && fwrite(s->img.p, 1, s->img.len, f) != s->img.len) { fclose(f); return -1; }
    fclose(f);
    snprintf(path, sizeof path, "%s/manifest.csv", dir);
    f = fopen(path, "wb");   /* binary: Python csv writes \r\n regardless of OS */
    if (!f) return -1;
    fputs("name,offset,size\r\n", f);
    for (int i = 0; i < s->n; i++)
        fprintf(f, "%s,%u,%u\r\n", s->ent[i].name, s->ent[i].off, s->ent[i].size);
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------- misc ----- */
void cv_err(CvCtx *c, const char *fmt, ...)
{
    va_list ap;
    if (c->nerrors < 64) {
        va_start(ap, fmt);
        vsnprintf(c->errors[c->nerrors], sizeof c->errors[0], fmt, ap);
        va_end(ap);
    }
    c->nerrors++;
}

const CvFileRow *cv_find_row(const CvRegionData *rd, const char *base)
{
    for (int i = rd->nfiles - 1; i >= 0; i--)
        if (!strcmp(rd->files[i].base, base)) return &rd->files[i];
    return NULL;
}

int cv_rz_inflate(const uint8_t *comp, size_t n, CvBuf *out)
{
    z_stream z;
    uint8_t tmp[65536];
    int r;
    if (n < 2 || comp[0] != 0x11 || comp[1] != 0x72) return -2;
    memset(&z, 0, sizeof z);
    if (inflateInit2(&z, -15) != Z_OK) return -1;
    z.next_in = (Bytef *)(comp + 2);
    z.avail_in = (uInt)(n - 2);
    do {
        z.next_out = tmp; z.avail_out = sizeof tmp;
        r = inflate(&z, Z_NO_FLUSH);
        if (r != Z_OK && r != Z_STREAM_END) { inflateEnd(&z); return -1; }
        if (cv_buf_put(out, tmp, sizeof tmp - z.avail_out)) { inflateEnd(&z); return -1; }
    } while (r != Z_STREAM_END);
    inflateEnd(&z);
    return 0;
}

int cv_rz_deflate(const uint8_t *raw, size_t n, CvBuf *out)
{
    z_stream z;
    uint8_t tmp[65536];
    int r;
    memset(&z, 0, sizeof z);
    /* == Python zlib.compressobj(6, DEFLATED, -15) (memLevel 8, default strategy) */
    if (deflateInit2(&z, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return -1;
    if (cv_buf_put(out, "\x11\x72", 2)) { deflateEnd(&z); return -1; }
    z.next_in = (Bytef *)raw;
    z.avail_in = (uInt)n;
    do {
        z.next_out = tmp; z.avail_out = sizeof tmp;
        r = deflate(&z, Z_FINISH);
        if (r != Z_OK && r != Z_STREAM_END && r != Z_BUF_ERROR) { deflateEnd(&z); return -1; }
        if (cv_buf_put(out, tmp, sizeof tmp - z.avail_out)) { deflateEnd(&z); return -1; }
    } while (r != Z_STREAM_END);
    deflateEnd(&z);
    return 0;
}
