/*
 * conv_d88.c -- C port of tools_pc/d88_emit.py + d88_propdefs.py: stage setup
 * files (Usetup*Z solo, Ump_setup*Z multiplayer) -> PC layout, appended to
 * the pccg.bin / manifest.csv that conv_d69.c just wrote (the Python
 * `--regen` behaviour: any existing setup rows are dropped and rebuilt).
 * Format rationale lives in the Python scripts' comments (D88, D122-D132).
 */
#include "convert.h"
#include "cvbytes.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================ propdefs */
static const uint8_t PD_N64W[49] = {
    [1] = 64, [2] = 2, [3] = 32, [4] = 33, [5] = 32, [6] = 59, [7] = 33, [8] = 34, [9] = 7,
    [10] = 64, [11] = 149, [12] = 32, [13] = 54, [14] = 3, [17] = 32, [18] = 3, [19] = 4,
    [20] = 45, [21] = 34, [22] = 4, [23] = 4, [24] = 1, [25] = 2, [26] = 2, [27] = 2, [28] = 2,
    [29] = 2, [30] = 4, [31] = 1, [32] = 4, [33] = 5, [34] = 3, [35] = 4, [36] = 32, [37] = 10,
    [38] = 4, [39] = 44, [40] = 45, [42] = 32, [43] = 32, [44] = 5, [45] = 56, [46] = 7,
    [47] = 37, [48] = 1,
};
static const uint16_t PD_PCB[49] = {
    [1] = 296, [2] = 8, [3] = 144, [5] = 144, [12] = 144, [17] = 144, [36] = 144, [42] = 144,
    [43] = 144, [4] = 152, [6] = 272, [7] = 152, [8] = 160, [9] = 32, [10] = 288, [11] = 664,
    [13] = 248, [14] = 32, [18] = 12, [19] = 32, [20] = 200, [21] = 152, [22] = 24, [23] = 16,
    [24] = 4, [25] = 8, [26] = 8, [27] = 8, [28] = 8, [29] = 8, [30] = 24, [31] = 4, [32] = 24,
    [33] = 24, [34] = 12, [35] = 24, [37] = 48, [38] = 32, [39] = 208, [40] = 208, [44] = 40,
    [45] = 248, [46] = 28, [47] = 168, [48] = 4,
};
#define OBJ_N64_WORDS 32
#define OBJ_PC_BYTES 144

static void bsw(uint8_t *o, const uint8_t *w) { o[0] = w[3]; o[1] = w[2]; o[2] = w[1]; o[3] = w[0]; }
static void hdrw(uint8_t *o, const uint8_t *w) { o[0] = w[1]; o[1] = w[0]; o[2] = w[2]; o[3] = w[3]; }
static void hhw(uint8_t *o, const uint8_t *w) { o[0] = w[1]; o[1] = w[0]; o[2] = w[3]; o[3] = w[2]; }
static void cpw(uint8_t *o, const uint8_t *w) { memcpy(o, w, 4); }

static void obj_prefix(uint8_t *out, const uint8_t *src, uint32_t so)
{
    uint32_t pc = 0;
    for (int i = 0; i < OBJ_N64_WORDS; i++) {
        const uint8_t *w = src + so + 4 * i;
        if (i == 4 || i == 5 || i == 26 || i == 27) { pc = (pc + 7) & ~7u; pc += 8; }
        else if (i == 0) { hdrw(out + pc, w); pc += 4; }
        else if (i == 1) { hhw(out + pc, w); pc += 4; }
        else if (i == 30 || i == 31) { cpw(out + pc, w); pc += 4; }
        else { bsw(out + pc, w); pc += 4; }
    }
}

static int in_set(const int *s, int n, int v) { for (int i = 0; i < n; i++) if (s[i] == v) return 1; return 0; }

/* out must be zeroed and PD_PCB[t] bytes long. Returns 0 on success. */
static int convert_record(const uint8_t *src, uint32_t so, int t, uint8_t *out)
{
    int n64w = PD_N64W[t];
    uint32_t pcb = PD_PCB[t];
    const uint8_t *s = src + so;

    switch (t) {
    case 3: case 5: case 12: case 17: case 36: case 42: case 43:
        obj_prefix(out, src, so);
        return 0;
    case 1: {
        uint32_t pc = OBJ_PC_BYTES;
        obj_prefix(out, src, so);
        for (int i = OBJ_N64_WORDS; i < n64w; i++) {
            const uint8_t *w = s + 4 * i;
            if (i == 50 || i == 51 || i == 61 || i == 62) { pc = (pc + 7) & ~7u; pc += 8; }
            else if (i == 38) { hhw(out + pc, w); pc += 4; }
            else if (i == 47) { out[pc] = w[0]; out[pc + 1] = w[1]; out[pc + 2] = w[3]; out[pc + 3] = w[2]; pc += 4; }
            else if (i == 49) { hdrw(out + pc, w); pc += 4; }
            else { bsw(out + pc, w); pc += 4; }
        }
        return pc <= pcb ? 0 : -1;
    }
    case 4: case 21: case 7: {
        uint32_t pc = OBJ_PC_BYTES;
        obj_prefix(out, src, so);
        for (int i = OBJ_N64_WORDS; i < n64w; i++) { bsw(out + pc, s + 4 * i); pc += 4; }
        return 0;
    }
    case 8: {
        const uint8_t *w = s + 4 * 32;
        obj_prefix(out, src, so);
        out[144] = w[0]; out[145] = w[1]; out[146] = w[3]; out[147] = w[2];
        return 0;
    }
    case 10: case 11: {
        int nmon = t == 10 ? 1 : 4;
        uint32_t tail_start = OBJ_N64_WORDS + 29 * nmon, pc = OBJ_PC_BYTES + 128 * nmon;
        obj_prefix(out, src, so);
        for (uint32_t i = tail_start; i < (uint32_t)n64w; i++) {
            if (t == 11) cpw(out + pc, s + 4 * i); else bsw(out + pc, s + 4 * i);
            pc += 4;
        }
        return 0;
    }
    case 6: {
        uint32_t pc = 212;
        obj_prefix(out, src, so);
        bsw(out + 144, s + 4 * 32);
        for (int i = 49; i < n64w; i++) { bsw(out + pc, s + 4 * i); pc += 4; }
        return 0;
    }
    case 22:
        hdrw(out, s);
        hhw(out + 4, s + 4);
        return 0;
    case 37:
        hdrw(out, s);
        for (int i = 1; i < 9; i++) bsw(out + 4 * i, s + 4 * i);
        return 0;
    case 14: case 19: case 38: case 44: {
        int nidx = t == 44 ? 3 : 2;
        hdrw(out, s);
        for (int k = 0; k < nidx; k++) bsw(out + 8 + 8 * k, s + 4 * (k + 1));
        return 0;
    }
    case 18:
        hdrw(out, s);
        bsw(out + 4, s + 4);
        hdrw(out + 8, s + 8);
        return 0;
    case 9:
        hdrw(out, s);
        for (int i = 1; i < 6; i++) hhw(out + 4 * i, s + 4 * i);
        return 0;
    case 47: case 39: case 40: case 45: case 13: case 20: {
        static const int P39[] = {41, 43}, P40[] = {43, 44}, P45[] = {32}, P13[] = {49, 50, 51};
        const int *ptr = NULL; int nptr = 0, hh_lo = 0, hh_hi = -1, hh1 = -1, id32 = 0;
        uint32_t pc = OBJ_PC_BYTES;
        switch (t) {
        case 39: ptr = P39; nptr = 2; hh1 = 33; id32 = 1; break;
        case 40: ptr = P40; nptr = 2; hh1 = 33; id32 = 1; break;
        case 45: ptr = P45; nptr = 1; break;
        case 13: ptr = P13; nptr = 3; break;
        case 20: hh_lo = 32; hh_hi = 44; break;
        default: break;
        }
        obj_prefix(out, src, so);
        for (int i = OBJ_N64_WORDS; i < n64w; i++) {
            const uint8_t *w = s + 4 * i;
            if (id32 && i == 32) { pc = (pc + 7) & ~7u; bsw(out + pc, w); pc += 8; }
            else if (ptr && in_set(ptr, nptr, i)) { pc = (pc + 7) & ~7u; pc += 8; }
            else if (i == hh1 || (i >= hh_lo && i <= hh_hi)) { hhw(out + pc, w); pc += 4; }
            else { bsw(out + pc, w); pc += 4; }
        }
        return pc <= pcb ? 0 : -1;
    }
    case 30: case 32: case 33: case 35:
        hdrw(out, s);
        for (int i = 1; i < n64w - 1; i++) bsw(out + 4 * i, s + 4 * i);
        return 0;
    default:
        hdrw(out, s);
        for (int i = 1; i < n64w; i++) bsw(out + 4 * i, s + 4 * i);
        return 0;
    }
}

/* Convert src[start:end]; appends to *res. Returns 0, or -1 + message. */
static int convert_stream(const uint8_t *src, uint32_t start, uint32_t end, CvBuf *res, char *msg, size_t mcap)
{
    uint32_t o = start;
    int n = 0;
    while (o < end) {
        int t;
        uint8_t *rec;
        if (o + 4 > end) { snprintf(msg, mcap, "propDefs: truncated header at %#x", o); return -1; }
        t = src[o + 3];
        if (t > 48 || !PD_N64W[t]) { snprintf(msg, mcap, "propDefs: unknown record type %d at %#x (rec #%d)", t, o, n); return -1; }
        if (o + 4u * PD_N64W[t] > end) { snprintf(msg, mcap, "propDefs: record type %d at %#x overruns region end", t, o); return -1; }
        rec = calloc(PD_PCB[t], 1);
        if (convert_record(src, o, t, rec)) { free(rec); snprintf(msg, mcap, "propDefs: type %d layout overflow", t); return -1; }
        cv_buf_put(res, rec, PD_PCB[t]);
        free(rec);
        o += 4u * PD_N64W[t];
        n++;
        if (t == 48) break;
    }
    if (o != end) {
        uint32_t pad = end - o;
        if (o > end || pad >= 16) { snprintf(msg, mcap, "propDefs: walk ended at %#x, region end %#x (pad=%d)", o, end, (int)pad); return -1; }
        for (uint32_t i = o; i < end; i++)
            if (src[i]) { snprintf(msg, mcap, "propDefs: walk ended at %#x, region end %#x (pad=%u)", o, end, pad); return -1; }
        cv_buf_put(res, src + o, pad);
    }
    return 0;
}

/* =================================================================== setup */
enum { K_HEADER, K_GROW, K_INTRO, K_PROPDEFS, K_LEAF_S32, K_LEAF_CSTR, K_LEAF_AI, K_GAP };

static const uint8_t INTRO_SZ[10] = {12, 16, 16, 32, 8, 8, 40, 12, 8, 4};

typedef struct { uint32_t start; int64_t end; int kind; int tbl; int seq; } Reg;
typedef struct { uint32_t *p; int n, cap; } U32V;
typedef struct { const char *fn; int oldsz, newsz; int hidx; } Grow;

static void u32v_push(U32V *v, uint32_t x)
{
    if (v->n == v->cap) { v->cap = v->cap ? v->cap * 2 : 64; v->p = realloc(v->p, (size_t)v->cap * 4); }
    v->p[v->n++] = x;
}

static int cmp_reg(const void *a, const void *b)
{
    const Reg *x = a, *y = b;
    if (x->start != y->start) return x->start < y->start ? -1 : 1;
    return x->seq - y->seq;
}

/* field order in the header (FIELD_NAMES) */
enum { F_PATHWAYPOINTS, F_WAYPOINTGROUPS, F_INTRO, F_PROPDEFS, F_PATROLPATHS, F_AILISTS,
       F_PADS, F_BOUNDPADS, F_PADNAMES, F_BOUNDPADNAMES };

typedef struct {
    CvCtx *c; const char *name; const uint8_t *src; uint32_t D;
} S;

static int rd_ok(const S *s, uint32_t o, uint32_t n) { return (uint64_t)o + n <= s->D; }
static uint32_t B32(const S *s, uint32_t o) { return rd_ok(s, o, 4) ? cv_be32(s->src, o) : 0; }

/* walk_fixed: returns 0 ok (starts filled, *term = terminator start or -1 if off==0), -1 error */
static int walk_fixed(S *s, uint32_t off, uint32_t recsize, uint32_t term_off, int s32neg, U32V *starts, int64_t *term)
{
    uint32_t o = off;
    *term = -1;
    if (off == 0) return 0;
    for (int guard = 0; guard < 100000; guard++) {
        uint32_t v;
        int isterm;
        if (!rd_ok(s, o + term_off, 4)) { cv_err(s->c, "%s: table walk past EOF at %#x", s->name, o); return -1; }
        v = cv_be32(s->src, o + term_off);
        isterm = s32neg ? ((int32_t)v < 0) : (v == 0);
        if (isterm) { *term = o; return 0; }
        u32v_push(starts, o);
        o += recsize;
    }
    cv_err(s->c, "%s: runaway walk_fixed at %#x", s->name, off);
    return -1;
}

typedef struct { uint32_t start, end; int kind; } Leaf;

static int convert_usetup(CvCtx *c, const char *name, const uint8_t *src, uint32_t D, CvBuf *res)
{
    S s = {c, name, src, D};
    uint32_t H[10];
    U32V st[10];                 /* by field index; only the 8 growing tables are used */
    int64_t term[10];
    Grow GT[8] = {
        {"pads", 44, 56, F_PADS}, {"boundpads", 68, 80, F_BOUNDPADS},
        {"waypointgroups", 12, 24, F_WAYPOINTGROUPS}, {"pathwaypoints", 16, 24, F_PATHWAYPOINTS},
        {"patrolpaths", 8, 16, F_PATROLPATHS}, {"ailists", 8, 16, F_AILISTS},
        {"padnames", 4, 8, F_PADNAMES}, {"boundpadnames", 4, 8, F_BOUNDPADNAMES},
    };
    static const char *FN[10] = {"pathwaypoints", "waypointgroups", "intro", "propDefs", "patrolpaths",
                                 "ailists", "pads", "boundpads", "padnames", "boundpadnames"};
    Reg *regs = NULL, *tiled = NULL;
    int nreg = 0, ntiled = 0, rc = -1;
    Leaf *leaves = NULL; int nleaves = 0, capleaves = 0;
    int32_t *leafidx = NULL;
    U32V ai = {0};
    uint32_t *delta_at = NULL;
    uint8_t *out = NULL;
    CvBuf pdpc = {0};
    int have_pd = 0;
    uint32_t intro_end, cum, total_new;
    U32V intro_o = {0}; int intro_t[5000]; int nintro = 0;
    int seq = 0;

    memset(st, 0, sizeof st);
    if (D < 0x28) { cv_err(c, "%s: too small (%u)", name, D); return -1; }
    for (int i = 0; i < 10; i++) H[i] = cv_be32(src, 4 * i);
    for (int i = 0; i < 10; i++)
        if (H[i] != 0 && H[i] >= D) { cv_err(c, "%s: header field %s offset %#x >= filesize %#x", name, FN[i], H[i], D); return -1; }

    if (walk_fixed(&s, H[F_PATHWAYPOINTS], 16, 0, 1, &st[F_PATHWAYPOINTS], &term[F_PATHWAYPOINTS])) goto done;
    if (walk_fixed(&s, H[F_WAYPOINTGROUPS], 12, 0, 0, &st[F_WAYPOINTGROUPS], &term[F_WAYPOINTGROUPS])) goto done;
    if (walk_fixed(&s, H[F_PATROLPATHS], 8, 0, 0, &st[F_PATROLPATHS], &term[F_PATROLPATHS])) goto done;
    if (walk_fixed(&s, H[F_AILISTS], 8, 0, 0, &st[F_AILISTS], &term[F_AILISTS])) goto done;
    if (walk_fixed(&s, H[F_PADS], 44, 0x24, 0, &st[F_PADS], &term[F_PADS])) goto done;
    if (walk_fixed(&s, H[F_BOUNDPADS], 68, 0x24, 0, &st[F_BOUNDPADS], &term[F_BOUNDPADS])) goto done;
    if (walk_fixed(&s, H[F_PADNAMES], 4, 0, 0, &st[F_PADNAMES], &term[F_PADNAMES])) goto done;
    if (walk_fixed(&s, H[F_BOUNDPADNAMES], 4, 0, 0, &st[F_BOUNDPADNAMES], &term[F_BOUNDPADNAMES])) goto done;

    /* intro: type-tag walk */
    intro_end = H[F_INTRO];
    if (H[F_INTRO]) {
        uint32_t o = H[F_INTRO];
        int done_walk = 0;
        for (int guard = 0; guard < 5000; guard++) {
            int32_t t;
            if (!rd_ok(&s, o, 4)) { cv_err(c, "%s: intro walk past EOF", name); goto done; }
            t = (int32_t)cv_be32(src, o);
            if (t < 0 || t > 9) { cv_err(c, "%s: unknown intro type %d at %#x", name, t, o); goto done; }
            u32v_push(&intro_o, o); intro_t[nintro++] = t;
            o += INTRO_SZ[t];
            if (t == 9) { done_walk = 1; break; }
        }
        if (!done_walk) { cv_err(c, "%s: intro walk runaway", name); goto done; }
        intro_end = o;
    }

    /* leaves */
    leafidx = malloc(((size_t)D + 1) * sizeof(int32_t));
    memset(leafidx, 0xFF, ((size_t)D + 1) * sizeof(int32_t));
#define ADD_LEAF(startv, kindv) do { \
        uint32_t st_ = (startv); int k_ = (kindv); \
        if (st_ != 0 && !(st_ <= D && leafidx[st_] >= 0)) { \
            int64_t e_ = -1; \
            if (k_ == K_LEAF_S32) { \
                uint32_t o_ = st_; \
                for (int g_ = 0; g_ < 50000; g_++) { \
                    if (!rd_ok(&s, o_, 4)) break; \
                    if ((int32_t)cv_be32(src, o_) < 0) { e_ = (int64_t)o_ + 4; break; } \
                    o_ += 4; } \
                if (e_ < 0) cv_err(c, "%s: runaway s32 array at %#x", name, st_); \
            } else { \
                const uint8_t *z_ = st_ < D ? memchr(src + st_, 0, D - st_) : NULL; \
                if (!z_) cv_err(c, "%s: unterminated string at %#x", name, st_); \
                else e_ = (z_ - src) + 1; \
            } \
            if (e_ >= 0) { \
                if (nleaves == capleaves) { capleaves = capleaves ? capleaves * 2 : 256; leaves = realloc(leaves, (size_t)capleaves * sizeof *leaves); } \
                leaves[nleaves].start = st_; leaves[nleaves].end = (uint32_t)e_; leaves[nleaves].kind = k_; \
                leafidx[st_] = nleaves++; } } } while (0)

    for (int i = 0; i < st[F_PATHWAYPOINTS].n; i++) ADD_LEAF(B32(&s, st[F_PATHWAYPOINTS].p[i] + 4), K_LEAF_S32);
    for (int i = 0; i < st[F_WAYPOINTGROUPS].n; i++) {
        ADD_LEAF(B32(&s, st[F_WAYPOINTGROUPS].p[i]), K_LEAF_S32);
        ADD_LEAF(B32(&s, st[F_WAYPOINTGROUPS].p[i] + 4), K_LEAF_S32);
    }
    for (int i = 0; i < st[F_PATROLPATHS].n; i++) ADD_LEAF(B32(&s, st[F_PATROLPATHS].p[i]), K_LEAF_S32);
    for (int i = 0; i < st[F_PADS].n; i++) ADD_LEAF(B32(&s, st[F_PADS].p[i] + 0x24), K_LEAF_CSTR);
    for (int i = 0; i < st[F_BOUNDPADS].n; i++) ADD_LEAF(B32(&s, st[F_BOUNDPADS].p[i] + 0x24), K_LEAF_CSTR);
    for (int i = 0; i < st[F_PADNAMES].n; i++) ADD_LEAF(B32(&s, st[F_PADNAMES].p[i]), K_LEAF_CSTR);
    for (int i = 0; i < st[F_BOUNDPADNAMES].n; i++) ADD_LEAF(B32(&s, st[F_BOUNDPADNAMES].p[i]), K_LEAF_CSTR);
    if (c->nerrors) goto done;   /* python records these errors and continues; any error fails the run */

    for (int i = 0; i < st[F_AILISTS].n; i++) {
        uint32_t a = B32(&s, st[F_AILISTS].p[i]);
        int dup = 0;
        if (!a || (a <= D && leafidx[a] >= 0)) continue;
        for (int k = 0; k < ai.n; k++) if (ai.p[k] == a) dup = 1;
        if (!dup) u32v_push(&ai, a);
    }

    /* regions */
    regs = malloc(sizeof(Reg) * (size_t)(20 + nleaves + ai.n));
    regs[nreg++] = (Reg){0, 0x28, K_HEADER, 0, seq++};
    for (int g = 0; g < 8; g++) {
        int h = GT[g].hidx;
        if (term[h] < 0) continue;
        regs[nreg++] = (Reg){H[h], term[h] + GT[g].oldsz, K_GROW, g, seq++};
    }
    if (H[F_INTRO]) regs[nreg++] = (Reg){H[F_INTRO], intro_end, K_INTRO, 0, seq++};
    if (H[F_PROPDEFS]) regs[nreg++] = (Reg){H[F_PROPDEFS], -1, K_PROPDEFS, 0, seq++};
    for (int i = 0; i < nleaves; i++) regs[nreg++] = (Reg){leaves[i].start, leaves[i].end, leaves[i].kind, 0, seq++};
    for (int i = 0; i < ai.n; i++) regs[nreg++] = (Reg){ai.p[i], -1, K_LEAF_AI, 0, seq++};
    qsort(regs, (size_t)nreg, sizeof *regs, cmp_reg);
    for (int i = 0; i < nreg; i++)
        if (regs[i].end < 0) regs[i].end = i + 1 < nreg ? regs[i + 1].start : D;

    /* tiling (small gaps become opaque passthrough) */
    tiled = malloc(sizeof(Reg) * (size_t)(2 * nreg + 2));
    {
        int64_t cursor = 0;
        for (int i = 0; i < nreg; i++) {
            if (regs[i].start < cursor) { cv_err(c, "%s: region overlap before kind %d@%#x (cursor was %#llx)", name, regs[i].kind, regs[i].start, (long long)cursor); goto done; }
            if (regs[i].start > cursor) tiled[ntiled++] = (Reg){(uint32_t)cursor, regs[i].start, K_GAP, 0, 0};
            if (regs[i].end < regs[i].start) { cv_err(c, "%s: region kind %d@%#x has negative length", name, regs[i].kind, regs[i].start); goto done; }
            tiled[ntiled++] = regs[i];
            cursor = regs[i].end;
        }
        if (cursor < D) tiled[ntiled++] = (Reg){(uint32_t)cursor, D, K_GAP, 0, 0};
        else if (cursor > D) { cv_err(c, "%s: regions overrun EOF (%#llx > %#x)", name, (long long)cursor, D); goto done; }
    }

    /* propDefs polymorphic stream (D88.4) */
    if (H[F_PROPDEFS]) {
        int64_t pd_end = -1;
        char msg[160];
        for (int i = 0; i < ntiled; i++) if (tiled[i].kind == K_PROPDEFS) { pd_end = tiled[i].end; break; }
        if (convert_stream(src, H[F_PROPDEFS], (uint32_t)pd_end, &pdpc, msg, sizeof msg)) { cv_err(c, "%s: %s", name, msg); goto done; }
        have_pd = 1;
    }

    /* pass 1: cumulative delta at each region start */
    delta_at = calloc((size_t)D + 2, sizeof(uint32_t));
    cum = 0x28;
    for (int i = 0; i < ntiled; i++) {
        Reg *r = &tiled[i];
        if (r->kind == K_HEADER) { delta_at[r->start] = 0; continue; }
        delta_at[r->start] = cum;
        if (r->kind == K_GROW) {
            int h = GT[r->tbl].hidx;
            cum += (uint32_t)(st[h].n + 1) * (uint32_t)(GT[r->tbl].newsz - GT[r->tbl].oldsz);
        } else if (r->kind == K_PROPDEFS && have_pd) {
            cum += (uint32_t)pdpc.len - (uint32_t)(r->end - r->start);
        }
    }
#define RELOC(off) ((off) == 0 ? 0u : (off) + (((off) <= D) ? delta_at[(off)] : 0u))

    total_new = D + cum;
    out = calloc((size_t)total_new + 64, 1);

    for (int i = 0; i < 10; i++) { cv_le32(out, 8 * i, RELOC(H[i])); }

    for (int g = 0; g < 8; g++) {
        int h = GT[g].hidx;
        uint32_t base, newsz = (uint32_t)GT[g].newsz;
        if (term[h] < 0) continue;
        base = RELOC(H[h]);
        for (int idx = 0; idx <= st[h].n; idx++) {
            uint32_t so = idx < st[h].n ? st[h].p[idx] : (uint32_t)term[h], d = base + (uint32_t)idx * newsz;
            if (so + (uint32_t)GT[g].oldsz > D) { cv_err(c, "%s: %s record past EOF", name, GT[g].fn); goto done; }
            if (h == F_PADS || h == F_BOUNDPADS) {
                for (int k = 0; k < 9; k++) cv_le32(out, d + 4 * k, cv_be32(src, so + 4 * k));
                cv_le32(out, d + 0x28, RELOC(cv_be32(src, so + 0x24)));
                if (h == F_BOUNDPADS)
                    for (int k = 0; k < 6; k++) cv_le32(out, d + 0x38 + 4 * k, cv_be32(src, so + 0x2c + 4 * k));
            } else if (h == F_WAYPOINTGROUPS) {
                uint32_t nb = cv_be32(src, so), wpv = cv_be32(src, so + 4), dist = cv_be32(src, so + 8);
                cv_le32(out, d, nb ? RELOC(nb) : 0);
                cv_le32(out, d + 8, wpv ? RELOC(wpv) : 0);
                cv_le32(out, d + 16, dist);
            } else if (h == F_PATHWAYPOINTS) {
                uint32_t padID = cv_be32(src, so), nb = cv_be32(src, so + 4), gn = cv_be32(src, so + 8), dist = cv_be32(src, so + 12);
                cv_le32(out, d, padID);
                cv_le32(out, d + 8, nb ? RELOC(nb) : 0);
                cv_le32(out, d + 16, gn);
                cv_le32(out, d + 20, dist);
            } else if (h == F_PATROLPATHS) {
                uint32_t wpv = cv_be32(src, so);
                cv_le32(out, d, wpv ? RELOC(wpv) : 0);
                out[d + 8] = src[so + 4];
                out[d + 9] = src[so + 5];
                cv_le16(out, d + 10, cv_bu16(src, so + 6));
            } else if (h == F_AILISTS) {
                uint32_t al = cv_be32(src, so), id = cv_be32(src, so + 4);
                cv_le32(out, d, al ? RELOC(al) : 0);
                cv_le32(out, d + 8, id);
            } else { /* padnames / boundpadnames */
                uint32_t p = cv_be32(src, so);
                cv_le32(out, d, p ? RELOC(p) : 0);
            }
        }
    }

    /* intro (same record size, no growth) */
    if (H[F_INTRO]) {
        uint32_t base_delta = delta_at[H[F_INTRO]];
        for (int i = 0; i < nintro; i++) {
            uint32_t o = intro_o.p[i], d = o + base_delta;
            int t = intro_t[i];
            if (t == 6) {
                for (int k = 0; k < 10; k++) {
                    if (k == 7) { cv_le16(out, d + 4 * k, cv_bu16(src, o + 4 * k)); cv_le16(out, d + 4 * k + 2, cv_bu16(src, o + 4 * k + 2)); }
                    else cv_le32(out, d + 4 * k, cv_be32(src, o + 4 * k));
                }
            } else {
                for (int k = 0; k < INTRO_SZ[t] / 4; k++) cv_le32(out, d + 4 * k, cv_be32(src, o + 4 * k));
            }
        }
    }

    for (int i = 0; i < nleaves; i++) {
        uint32_t start = leaves[i].start, end = leaves[i].end, d = RELOC(start);
        if (leaves[i].kind == K_LEAF_S32) {
            for (uint32_t o = start; o < end; o += 4) cv_le32(out, d + (o - start), cv_be32(src, o));
        } else {
            memcpy(out + d, src + start, end - start);
        }
    }
    for (int i = 0; i < ntiled; i++) {
        Reg *r = &tiled[i];
        uint32_t d = RELOC(r->start);
        if (r->kind == K_PROPDEFS) { if (have_pd) memcpy(out + d, pdpc.p, pdpc.len); }
        else if (r->kind == K_LEAF_AI || r->kind == K_GAP) memcpy(out + d, src + r->start, (size_t)(r->end - r->start));
    }

    if (cv_buf_put(res, out, total_new)) goto done;
    rc = 0;
done:
    for (int i = 0; i < 10; i++) free(st[i].p);
    free(regs); free(tiled); free(leaves); free(leafidx); free(ai.p); free(delta_at); free(out);
    free(intro_o.p); cv_buf_free(&pdpc);
    return rc;
}

/* ==================================================================== main */
static int is_setup_name(const char *n)
{
    size_t len = strlen(n);
    return (!strncmp(n, "Usetup", 6) || !strncmp(n, "Ump_setup", 9)) && len && n[len - 1] == 'Z';
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    uint8_t *p;
    long sz;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    p = malloc((size_t)sz + 1);
    if (!p || fread(p, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(p); return NULL; }
    fclose(f);
    *n = (size_t)sz;
    return p;
}

int cv_run_d88(CvCtx *c, const char *out_dir)
{
    char binp[1024], manp[1024];
    uint8_t *base, *man;
    size_t nbase = 0, nman = 0;
    CvSidecar sc;
    const char **names;
    int nnames = 0, n_ok = 0, kept, rc = 0;

    snprintf(binp, sizeof binp, "%s/pccg.bin", out_dir);
    snprintf(manp, sizeof manp, "%s/manifest.csv", out_dir);
    memset(&sc, 0, sizeof sc);
    base = slurp(binp, &nbase);
    man = slurp(manp, &nman);

    if (base && man) {            /* --regen: keep the d69 rows, drop old setup rows */
        char *line;
        man[nman] = 0;
        line = strtok((char *)man, "\r\n");   /* header */
        while ((line = strtok(NULL, "\r\n"))) {
            char *c1 = strchr(line, ','), *c2;
            unsigned long off, sz;
            if (!c1) continue;
            *c1++ = 0;
            c2 = strchr(c1, ',');
            if (!c2) continue;
            *c2++ = 0;
            off = strtoul(c1, NULL, 10); sz = strtoul(c2, NULL, 10);
            if (is_setup_name(line)) continue;
            if (off + sz <= nbase) cv_sc_emit(&sc, line, base + off, sz);
        }
    }
    kept = sc.n;
    printf("--regen: kept %d d69 rows; sidecar rebuilt to %zu bytes\n", kept, sc.img.len);
    free(base); free(man);

    names = malloc(sizeof(char *) * (size_t)cv_table_count);
    for (int i = 0; i < cv_table_count; i++) if (is_setup_name(cv_table_names[i])) names[nnames++] = cv_table_names[i];
    qsort(names, (size_t)nnames, sizeof *names, cmp_str);
    {   /* unique */
        int k = 0;
        for (int i = 0; i < nnames; i++) if (!i || strcmp(names[i], names[i - 1])) names[k++] = names[i];
        nnames = k;
    }

    for (int i = 0; i < nnames; i++) {
        const CvFileRow *row = cv_find_row(c->rd, names[i]);
        CvBuf dec = {0}, out = {0}, comp = {0};
        if (!row || row->size == 0) continue;
        if ((size_t)row->off + row->size > c->rom_size) { cv_err(c, "%s: past end of ROM", names[i]); continue; }
        if (row->size < 2 || c->rom[row->off] != 0x11 || c->rom[row->off + 1] != 0x72) { cv_err(c, "%s: bad RZ magic", names[i]); continue; }
        if (cv_rz_inflate(c->rom + row->off, row->size, &dec)) { cv_err(c, "%s: decompress failed", names[i]); cv_buf_free(&dec); continue; }
        if (convert_usetup(c, names[i], dec.p, (uint32_t)dec.len, &out) == 0) {
            if (cv_rz_deflate(out.p, out.len, &comp) == 0) { cv_sc_emit(&sc, names[i], comp.p, comp.len); n_ok++; }
            else cv_err(c, "%s: deflate failed", names[i]);
        }
        cv_buf_free(&dec); cv_buf_free(&out); cv_buf_free(&comp);
    }
    printf("converted: %d/%d setup*Z files (solo + MP)\n", n_ok, nnames);
    free(names);

    if (c->nerrors) rc = -1;
    else if (cv_sc_write(&sc, out_dir, "pccg.bin")) { fprintf(stderr, "cannot write %s\n", out_dir); rc = -1; }
    else printf("wrote %d sidecars to %s (%zu bytes total)\n", sc.n, binp, sc.img.len);
    cv_sc_free(&sc);
    return rc;
}
