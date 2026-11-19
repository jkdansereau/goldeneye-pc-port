/*
 * conv_d69.c -- C port of tools_pc/d69_emit.py: stage bg seg and
 * Tbg_*_stanZ files -> PC layout -> pccg.bin + manifest.csv. The Python
 * script's comments carry the format rationale (D69/D78/D80/D81/D88.5); this
 * file mirrors its control flow so the outputs diff byte-identical.
 */
#include "convert.h"
#include "cvbytes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

typedef struct { uint32_t start, size; int is_record; } Region;

/* ------------------------------------------------------------ bg .seg --- */
static int convert_seg(CvCtx *c, const char *name, const uint8_t *src, size_t D, CvBuf *res)
{
    uint32_t h0, h[5], h1, h2, h3, h4, room_end, nrooms;
    uint32_t o, n_portals = 0, portal_table_end, portal_delta;
    uint32_t *targets = NULL, ntargets = 0, captargets = 0;
    uint32_t *alt = NULL, nalt = 0;
    Region *reg = NULL;
    int nreg = 0, rc = -1;
    uint8_t *out = NULL;
    const uint32_t ALT = 100;

    if (D < 0x40) { cv_err(c, "%s: too small (%zu)", name, D); return -1; }
    h0 = cv_be32(src, 0);
    for (int i = 0; i < 5; i++) h[i] = cv_be32(src, 4 * i) & CV_MASK;
    h1 = h[1]; h2 = h[2]; h3 = h[3]; h4 = h[4];
    if (h0 != 0) cv_err(c, "%s: header word0 != 0 (%#x)", name, h0);
    if (h4 != 0) {
        cv_err(c, "%s: header word4 != 0 (%#x) -- f32 array path not implemented (spec D80); needs manual review", name, h4);
        return -1;
    }
    if (h2 == 0 || h2 <= h1) {
        cv_err(c, "%s: portal offset %#x <= room offset %#x", name, h2, h1);
        return -1;
    }
    room_end = (h3 && h3 > h1 && h3 < h2) ? h3 : h2;
    if ((room_end - h1) % 24 != 0) {
        cv_err(c, "%s: room table extent %#x not a multiple of 24", name, room_end - h1);
        return -1;
    }
    nrooms = (room_end - h1) / 24; (void)nrooms;

    /* portal table: count records before the terminator */
    o = h2;
    for (;;) {
        uint32_t v;
        if (!(o + 8 <= D)) {
            cv_err(c, "%s: portal table terminator not found before EOF", name);
            return -1;
        }
        v = cv_be32(src, o) & CV_MASK;
        if (v == 0) break;
        if (ntargets == captargets) {
            captargets = captargets ? captargets * 2 : 256;
            targets = (uint32_t *)realloc(targets, captargets * sizeof *targets);
        }
        targets[ntargets++] = v;
        n_portals++;
        o += 8;
    }
    portal_table_end = o + 8;
    portal_delta = 8 * (n_portals + 1);

    /* envdata ALT targets share the portal point-data offset space */
    if (h3 && room_end == h3) {
        uint32_t o2 = h3;
        while (o2 < h2) {
            uint8_t etype = src[o2];
            int32_t data = (int32_t)cv_be32(src, o2 + 4);
            if (etype == ALT && data) {
                if (ntargets + nalt >= captargets) {
                    captargets = captargets ? captargets * 2 : 256;
                    targets = (uint32_t *)realloc(targets, captargets * sizeof *targets);
                }
                targets[ntargets + nalt++] = (uint32_t)data & CV_MASK;
            }
            o2 += 8;
            if (etype == 0) break;
        }
    }
    (void)alt;
    ntargets += nalt;
    qsort(targets, ntargets, sizeof *targets, cmp_u32);
    {   /* dedupe */
        uint32_t k = 0;
        for (uint32_t i = 0; i < ntargets; i++)
            if (i == 0 || targets[i] != targets[i - 1]) targets[k++] = targets[i];
        ntargets = k;
    }

    reg = (Region *)malloc((ntargets * 2 + 2) * sizeof *reg);
    {
        uint32_t cursor = portal_table_end;
        for (uint32_t i = 0; i < ntargets; i++) {
            uint32_t t = targets[i], numPoints, recsize;
            if (t < cursor) {
                cv_err(c, "%s: portal target %#x overlaps previous region (cursor %#x)", name, t, cursor);
                goto done;
            }
            if (t > cursor) { reg[nreg].start = cursor; reg[nreg].size = t - cursor; reg[nreg].is_record = 0; nreg++; }
            if (t >= D) { cv_err(c, "%s: portal target %#x past EOF", name, t); goto done; }
            numPoints = src[t];
            if (numPoints == 0 || numPoints > 20) {
                cv_err(c, "%s: portal target %#x numPoints=%u implausible", name, t, numPoints);
                goto done;
            }
            recsize = 4 + 12 * numPoints;
            if (t + recsize > D) {
                cv_err(c, "%s: portal target %#x size %#x overruns EOF", name, t, recsize);
                goto done;
            }
            reg[nreg].start = t; reg[nreg].size = recsize; reg[nreg].is_record = 1; nreg++;
            cursor = t + recsize;
        }
        if (cursor < D) { reg[nreg].start = cursor; reg[nreg].size = (uint32_t)D - cursor; reg[nreg].is_record = 0; nreg++; }
    }

    out = (uint8_t *)calloc(D + portal_delta, 1);
    cv_le32(out, 0, h0);
    cv_le32(out, 4, 0x0F000000u | h1);
    cv_le32(out, 8, 0x0F000000u | h2);
    cv_le32(out, 12, h3 ? (0x0F000000u | h3) : 0);
    cv_le32(out, 16, 0);
    memcpy(out + 20, src + 20, 0x40 - 20);

    /* room table: three delta-shifted offsets, three plain bswap32 floats */
    for (uint32_t o2 = h1; o2 < room_end; o2 += 24) {
        for (int w = 0; w < 3; w++) {
            uint32_t v = cv_be32(src, o2 + 4 * w) & CV_MASK;
            uint32_t nv = v ? (0x0F000000u | ((v + portal_delta) & CV_MASK)) : 0;
            cv_le32(out, o2 + 4 * w, nv);
        }
        for (int w = 0; w < 3; w++) {
            size_t fo = o2 + 12 + 4 * w;
            cv_le32(out, fo, cv_be32(src, fo));
        }
    }

    /* envdata table */
    if (h3 && room_end == h3) {
        uint32_t o2 = h3;
        while (o2 < h2) {
            uint8_t etype = src[o2];
            int32_t data;
            out[o2] = etype;
            memcpy(out + o2 + 1, src + o2 + 1, 3);
            data = (int32_t)cv_be32(src, o2 + 4);
            if (etype == ALT) data = data ? (int32_t)((uint32_t)data + portal_delta) : data;
            cv_le32(out, o2 + 4, (uint32_t)data);
            o2 += 8;
            if (etype == 0) break;
        }
    }

    /* portal table: 8B N64 records -> 16B PC records, growing forward */
    {
        uint32_t o2 = h2, pc_pos = h2;
        for (uint32_t idx = 0; idx <= n_portals; idx++) {
            uint32_t off_val = cv_be32(src, o2) & CV_MASK;
            cv_le32(out, pc_pos, off_val ? (0x0F000000u | ((off_val + portal_delta) & CV_MASK)) : 0);
            cv_le32(out, pc_pos + 4, 0);
            out[pc_pos + 8] = src[o2 + 4];
            out[pc_pos + 9] = src[o2 + 5];
            out[pc_pos + 10] = src[o2 + 6];
            out[pc_pos + 11] = src[o2 + 7];
            cv_le32(out, pc_pos + 12, 0);
            o2 += 8;
            pc_pos += 16;
        }
        if (o2 != portal_table_end || pc_pos != h2 + 16 * (n_portals + 1)) {
            cv_err(c, "%s: portal table accounting mismatch", name);
            goto done;
        }
    }

    /* portal point-data blob, relocated by +portal_delta */
    for (int i = 0; i < nreg; i++) {
        uint32_t start = reg[i].start, size = reg[i].size, dst = start + portal_delta;
        if (!reg[i].is_record) { memcpy(out + dst, src + start, size); continue; }
        out[dst] = src[start];
        memcpy(out + dst + 1, src + start + 1, 3);
        for (uint32_t k = 0; k < (size - 4) / 4; k++)
            cv_le32(out, dst + 4 + 4 * k, cv_be32(src, start + 4 + 4 * k));
    }

    if (cv_buf_put(res, out, D + portal_delta)) goto done;
    rc = 0;
done:
    free(targets); free(reg); free(out);
    return rc;
}

/* ---------------------------------------------------------- stan file --- */
static const uint32_t TILESIZES[12] = {0x20, 0x20, 0x20, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0};

static int convert_stan(CvCtx *c, const char *name, const uint8_t *src, size_t D, CvBuf *res)
{
    uint32_t stanfile, N, old_array_end, array_delta, new_tiledata_start;
    size_t o, src_o, dst_o, p;
    uint8_t *out = NULL;
    int rc = -1, found;

    if (D < 8) { cv_err(c, "%s: too small (%zu)", name, D); return -1; }
    stanfile = cv_be32(src, 0);

    /* room-offset array: u32 entries from file offset 4, NULL terminated */
    o = 4; N = 0; found = 0;
    while (o + 4 <= D) {
        uint32_t v = cv_be32(src, o);
        o += 4;
        if (v == 0) { found = 1; break; }
        N++;
    }
    if (!found) { cv_err(c, "%s: room-offset array terminator not found", name); return -1; }
    old_array_end = (uint32_t)o;
    array_delta = 4 * (N + 2);
    new_tiledata_start = old_array_end + array_delta;

    out = (uint8_t *)calloc(D + array_delta, 1);
    cv_le32(out, 0, stanfile);
    p = 8;
    for (uint32_t i = 0; i <= N; i++) {
        uint32_t v = cv_be32(src, 4 + 4 * i);
        cv_le32(out, p, v ? v + array_delta : 0);
        p += 8;
    }

    src_o = old_array_end;
    dst_o = new_tiledata_start;
    found = 0;
    while (src_o + 4 <= D) {
        uint32_t midhalf, tailhalf, pointCount, sz;
        if (!cv_be32(src, src_o)) { found = 1; break; }
        if (src_o + 8 > D) break;
        out[dst_o] = src[src_o + 1];   /* D88.5: idHi bytes swapped, idLo + room stay */
        out[dst_o + 1] = src[src_o];
        out[dst_o + 2] = src[src_o + 2];
        out[dst_o + 3] = src[src_o + 3];
        midhalf = cv_bu16(src, src_o + 4);
        tailhalf = cv_bu16(src, src_o + 6);
        cv_le16(out, dst_o + 4, midhalf);
        cv_le16(out, dst_o + 6, tailhalf);
        pointCount = (tailhalf >> 12) & 0xF;
        sz = TILESIZES[pointCount];
        if (sz == 0) { cv_err(c, "%s: bad pointCount %u at src %#zx", name, pointCount, src_o); goto done; }
        if (src_o + sz > D) { cv_err(c, "%s: tile at %#zx size %#x overruns EOF %#zx", name, src_o, sz, D); goto done; }
        for (uint32_t i = 0; i < pointCount; i++) {
            size_t po = src_o + 8 + 8 * i, qo = dst_o + 8 + 8 * i;
            for (int k = 0; k < 4; k++) cv_le16(out, qo + 2 * k, cv_bu16(src, po + 2 * k));
        }
        src_o += sz;
        dst_o += sz;
    }
    if (!found) { cv_err(c, "%s: ran off EOF without an all-zero EOF sentinel", name); goto done; }

    if (src_o + 4 < D) memcpy(out + dst_o + 4, src + src_o + 4, D - (src_o + 4));
    if (cv_buf_put(res, out, D + array_delta)) goto done;
    rc = 0;
done:
    free(out);
    return rc;
}

/* -------------------------------------------------------------- main ---- */
int cv_run_d69(CvCtx *c, const char *out_dir)
{
    CvSidecar sc;
    int n_seg = 0, n_stan = 0, rc = 0;
    memset(&sc, 0, sizeof sc);

    for (int i = 0; i < cv_table_count; i++) {
        const char *name = cv_table_names[i];
        size_t len = strlen(name);
        const CvFileRow *row;
        CvBuf out = {0};

        if (!strncmp(name, "bg/", 3) && len > 4 && !strcmp(name + len - 4, ".seg")) {
            char base[128];
            const char *slash = strrchr(name, '/');
            snprintf(base, sizeof base, "%.*s", (int)strlen(slash + 1) - 4, slash + 1);
            row = cv_find_row(c->rd, base);
            if (!row || row->size == 0) continue;     /* unused in this ROM build */
            if ((size_t)row->off + row->size > c->rom_size) { cv_err(c, "%s: past end of ROM", name); continue; }
            if (convert_seg(c, name, c->rom + row->off, row->size, &out) == 0) {
                cv_sc_emit(&sc, name, out.p, out.len);
                n_seg++;
            }
        } else if ((!strncmp(name, "Tbg_", 4) || !strcmp(name, "TbgxZ")) && name[len - 1] == 'Z') {
            CvBuf dec = {0}, comp = {0};
            row = cv_find_row(c->rd, name);
            if (!row || row->size == 0) continue;
            if ((size_t)row->off + row->size > c->rom_size) { cv_err(c, "%s: past end of ROM", name); continue; }
            if (row->size < 2 || c->rom[row->off] != 0x11 || c->rom[row->off + 1] != 0x72) {
                cv_err(c, "%s: bad RZ magic", name);
                continue;
            }
            if (cv_rz_inflate(c->rom + row->off, row->size, &dec)) {
                cv_err(c, "%s: decompress failed", name);
                cv_buf_free(&dec);
                continue;
            }
            if (convert_stan(c, name, dec.p, dec.len, &out) == 0) {
                if (cv_rz_deflate(out.p, out.len, &comp) == 0) {
                    cv_sc_emit(&sc, name, comp.p, comp.len);
                    n_stan++;
                } else cv_err(c, "%s: deflate failed", name);
            }
            cv_buf_free(&dec); cv_buf_free(&comp);
        }
        cv_buf_free(&out);
    }

    printf("converted: %d bg/*.seg + %d Tbg_*_stanZ files\n", n_seg, n_stan);
    if (c->nerrors) rc = -1;
    else if (cv_sc_write(&sc, out_dir, "pccg.bin")) { fprintf(stderr, "cannot write %s\n", out_dir); rc = -1; }
    else printf("wrote %d sidecars to %s/pccg.bin (%zu bytes total)\n", sc.n, out_dir, sc.img.len);
    cv_sc_free(&sc);
    return rc;
}
