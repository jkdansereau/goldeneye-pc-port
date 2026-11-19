/*
 * conv_d43.c -- C port of tools_pc/d43_emit.py: every N64 model file ->
 * PC layout (48B nodes, u64 pointers, 16B GDL slots) -> RZ sidecar ->
 * pcmodels.bin + manifest.csv. Control flow mirrors the Python script so the
 * outputs diff byte-identical; see its docstring and comments (D43..D120,
 * D303) for the format rationale.
 *
 * Not ported: the buffer-fit cross-checks the Python script prints after the
 * conversion (chain / stage-bank analysis). They gate converter *changes*, not
 * the output; run `python tools_pc/d43_emit.py --check-only` for them.
 */
#include "convert.h"
#include "cvbytes.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- vectors -- */
#define VEC(T) struct { T *p; int n, cap; }
#define VEC_PUSH(v, x) do { \
    if ((v).n == (v).cap) { (v).cap = (v).cap ? (v).cap * 2 : 64; \
        (v).p = realloc((v).p, (size_t)(v).cap * sizeof *(v).p); } \
    (v).p[(v).n++] = (x); } while (0)
#define VEC_FREE(v) do { free((v).p); (v).p = NULL; (v).n = (v).cap = 0; } while (0)

typedef struct { uint32_t old, size, nw; } Region;
typedef struct { uint32_t off; int op; } Placed;
typedef struct { uint32_t off, nv; } VtxReg;
typedef struct { uint32_t op, data, parent, next, prev, child; int used; } Node;

static const int N64_REC[25] = {0, 0x1C, 0x1C, 0x1C, 0x14, 0, 0, 0, 0x10, 0x24, 0x1C, 0, 0x28,
                                0x20, 0, 0x1C, 0, 0x20, 0x08, 0, 0, 0x14, 0x10, 0x02, 0x20};
static const int PC_REC[25] = {0, 24, 40, 40, 40, 0, 0, 0, 24, 48, 28, 0, 48,
                               48, 0, 28, 0, 0, 16, 0, 0, 20, 32, 2, 64};
#define PC_NODE 48u

/* ----------------------------------------------------------- file state -- */
typedef struct {
    CvCtx *c;
    const char *name;
    const uint8_t *src;
    uint32_t D;
    int NS, NT;
    int bad;                 /* out-of-range read / structural error */

    Node *nodes;             /* indexed by N64 offset, .used marks presence */
    uint32_t R0;

    VEC(uint32_t) seq;
    VEC(Placed) placed;
    VEC(Region) regions;     /* (old, size, new) */
    VEC(VtxReg) vtx;
    VEC(uint32_t) zero_vtx;
    VEC(uint32_t) all_gdls;  /* unvisited (sorted) + visit order */
    VEC(Region) blobs;

    int32_t *node_newoff;    /* by old offset, -1 = unset */
    int32_t *rec_newoff;
    int32_t *gdl_newoff;
    uint8_t *is_coll;        /* op24 collision-vertex array at old offset */
    uint16_t *pointusage;    /* op24 PointUsage array at old offset -> numVertices */

    Region *rold;            /* regions sorted by old */
    int *rnew_idx;           /* indices of rold sorted by (new, index) */
    uint8_t *buf;
    uint32_t D_PC;
} F;

#define FAIL(f, ...) do { cv_err((f)->c, __VA_ARGS__); } while (0)

static uint32_t rd32(F *f, uint32_t o)
{
    if ((size_t)o + 4 > f->D) { f->bad = 1; return 0; }
    return cv_be32(f->src, o);
}
static uint32_t rd32o(F *f, uint32_t o) { return rd32(f, o) & CV_MASK; }
static uint32_t rdu16(F *f, uint32_t o)
{
    if ((size_t)o + 2 > f->D) { f->bad = 1; return 0; }
    return cv_bu16(f->src, o);
}
static int32_t rds16(F *f, uint32_t o) { return (int16_t)rdu16(f, o); }

/* ------------------------------------------------------- node map + sim -- */
static int build_nodes(F *f)
{
    uint32_t R0 = 4u * f->NS + 12u * f->NT;
    VEC(uint32_t) stack = {0};
    int count = 0;
    f->R0 = R0;
    VEC_PUSH(stack, R0);
    while (stack.n) {
        uint32_t o = stack.p[--stack.n];
        Node *n;
        if (o >= f->D || f->nodes[o].used) continue;
        n = &f->nodes[o];
        n->used = 1;
        n->op = rdu16(f, o) & 0xff;
        n->data = rd32o(f, o + 4);
        n->parent = rd32o(f, o + 8);
        n->next = rd32o(f, o + 0xC);
        n->prev = rd32o(f, o + 0x10);
        n->child = rd32o(f, o + 0x14);
        count++;
        if (n->op == 8) {
            uint32_t q = rd32o(f, n->data + 8);
            if (q) VEC_PUSH(stack, q);
        } else if (n->op == 18) {
            uint32_t q = rd32o(f, n->data + 0);
            if (q) VEC_PUSH(stack, q);
        } else if (n->child) {
            VEC_PUSH(stack, n->child);
        }
        if (n->next) VEC_PUSH(stack, n->next);
    }
    VEC_FREE(stack);
    return count;
}

static Node *nd(F *f, uint32_t o)
{
    if (o >= f->D || !f->nodes[o].used) { f->bad = 1; return NULL; }
    return &f->nodes[o];
}

static void gdl_of(F *f, const Node *n, uint32_t *p, uint32_t *s)
{
    *p = *s = 0;
    if (n->op == 4 || n->op == 24) { *p = rd32o(f, n->data); *s = rd32o(f, n->data + 4); }
    else if (n->op == 22) { *p = rd32o(f, n->data + 8); }
}

/* EXACT modelIterateDisplayLists GDL visit order (mutable walk). */
static void visit_seq(F *f)
{
    uint32_t node = f->R0, prev_gdl = 0;
    int have_prev = 0;
    uint32_t prev_node = 0;
    int guard = 0;
    while (node && guard < 200000 && !f->bad) {
        Node *n = nd(f, node);
        uint32_t gdl = 0;
        if (!n) return;
        guard++;
        if (n->op == 4 || n->op == 22 || n->op == 24) {
            uint32_t p, s;
            gdl_of(f, n, &p, &s);
            if (!have_prev || node != prev_node) gdl = p;
            else if (s && s != prev_gdl) gdl = s;
        } else if (n->op == 8) {
            n->child = rd32o(f, n->data + 8);
        } else if (n->op == 18) {
            n->child = rd32o(f, n->data + 0);
        } else if (n->op == 9) {
            uint32_t node1 = rd32o(f, n->data + 0x18), node2 = rd32o(f, n->data + 0x1C);
            if (node1) {
                uint32_t loop = node1;
                Node *ln;
                n->child = node1;
                if (!nd(f, node1)) return;
                f->nodes[node1].prev = 0;
                while ((ln = nd(f, loop)) && ln->next && ln->next != node2) loop = ln->next;
                if (!ln) return;
                ln->next = node2;
                if (node2) {
                    if (!nd(f, node2)) return;
                    f->nodes[node2].prev = loop;
                    loop = node2;
                    while ((ln = nd(f, loop)) && ln->next && ln->next != node1) loop = ln->next;
                    if (!ln) return;
                    ln->next = 0;
                }
            } else {
                n->child = node2;
                if (node2) { if (!nd(f, node2)) return; f->nodes[node2].prev = 0; }
            }
        }
        if (gdl) {
            VEC_PUSH(f->seq, gdl);
            prev_node = node;
            have_prev = 1;
            prev_gdl = gdl;
            continue;
        }
        have_prev = 0;
        if (n->child) {
            node = n->child;
        } else {
            while (node) {
                Node *cur = nd(f, node);
                if (!cur) return;
                if (cur->next) { node = cur->next; break; }
                node = cur->parent;
            }
        }
    }
}

/* d43_convert walk(): next-first DFS stack (node placement order). */
static void placement_order(F *f)
{
    uint8_t *seen = calloc(f->D + 1, 1);
    VEC(uint32_t) stack = {0};
    VEC_PUSH(stack, f->R0);
    while (stack.n) {
        uint32_t o = stack.p[--stack.n];
        Node *n;
        Placed pl;
        if (o >= f->D || seen[o]) continue;
        seen[o] = 1;
        n = nd(f, o);
        if (!n) break;
        pl.off = o; pl.op = (int)n->op;
        VEC_PUSH(f->placed, pl);
        if (n->op == 8) {
            uint32_t a = rd32o(f, n->data + 8);
            if (a) VEC_PUSH(stack, a);
        } else if (n->op == 18) {
            uint32_t q = rd32o(f, n->data + 0);
            if (q) VEC_PUSH(stack, q);
        } else if (n->child) {
            VEC_PUSH(stack, n->child);
        }
        if (n->next) VEC_PUSH(stack, n->next);
    }
    VEC_FREE(stack);
    free(seen);
}

static uint32_t gdl_end(F *f, uint32_t g)
{
    uint32_t o = g;
    while ((size_t)o + 8 <= f->D) {
        if ((cv_be32(f->src, o) >> 24) == 0xB8) return o + 8;
        o += 8;
    }
    return f->D;
}

/* ---------------------------------------------------------------- layout -- */
static uint32_t add_region(F *f, uint32_t old, uint32_t sz, uint32_t cur)
{
    Region r; r.old = old; r.size = sz; r.nw = cur;
    VEC_PUSH(f->regions, r);
    return cur + sz;
}

static int cmp_region_old(const void *a, const void *b)
{
    const Region *x = a, *y = b;
    if (x->old != y->old) return x->old < y->old ? -1 : 1;
    if (x->size != y->size) return x->size < y->size ? -1 : 1;
    if (x->nw != y->nw) return x->nw < y->nw ? -1 : 1;
    return 0;
}
static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static int in_vec_u32(const uint32_t *p, int n, uint32_t v)
{
    for (int i = 0; i < n; i++) if (p[i] == v) return 1;
    return 0;
}

/* remap: N64 offset -> PC offset, -1 = not in the region map. */
static int64_t remap(F *f, uint32_t off)
{
    int lo = 0, hi = f->regions.n, i;
    if (off == 0) return 0;
    while (lo < hi) {            /* bisect_right on rold[].old */
        int mid = (lo + hi) / 2;
        if (off < f->rold[mid].old) hi = mid; else lo = mid + 1;
    }
    i = lo - 1;
    if (i < 0) return -1;
    if (f->rold[i].old <= off && off < f->rold[i].old + f->rold[i].size)
        return (int64_t)f->rold[i].nw + (off - f->rold[i].old);
    return -1;
}

static const Region *g_cmp_base;
static int cmp_new_idx(const void *a, const void *b)
{
    int ia = *(const int *)a, ib = *(const int *)b;
    uint32_t x = g_cmp_base[ia].nw, y = g_cmp_base[ib].nw;
    if (x != y) return x < y ? -1 : 1;
    return ia - ib;   /* stable on ties */
}

/* ------------------------------------------------------------ put helpers -- */
static void put_u16(F *f, uint32_t o, uint32_t v) { cv_le16(f->buf, o, v); }
static void put_u32(F *f, uint32_t o, uint32_t v) { cv_le32(f->buf, o, v); }
static void put_f32(F *f, uint32_t o, uint32_t doff)
{
    const uint8_t *s = f->src + doff;
    if (doff + 4 > f->D) { f->bad = 1; return; }
    f->buf[o] = s[3]; f->buf[o + 1] = s[2]; f->buf[o + 2] = s[1]; f->buf[o + 3] = s[0];
}
static void put_ptr(F *f, uint32_t o, uint32_t old_off)
{
    int64_t n;
    if (!old_off) return;
    n = remap(f, old_off);
    if (n < 0) { FAIL(f, "%s: ptr %#x not in region map", f->name, old_off); n = 0; }
    put_u32(f, o, 0x05000000u | ((uint32_t)n & CV_MASK));
    put_u32(f, o + 4, 0);
}
static void put_gdlptr(F *f, uint32_t o, uint32_t g)
{
    if (!g) return;
    if (g >= f->D || f->gdl_newoff[g] < 0) { f->bad = 1; return; }
    put_u32(f, o, 0x05000000u | ((uint32_t)f->gdl_newoff[g] & CV_MASK));
}

/* ------------------------------------------------------------ the layout -- */
static void layout(F *f)
{
    const uint8_t *src = f->src;
    uint32_t D = f->D, dstpos = 0;
    int NS = f->NS, NT = f->NT;

    for (int i = 0; i < NS; i++) dstpos = add_region(f, 4u * i, 8, dstpos);
    for (int i = 0; i < NT; i++) dstpos = add_region(f, 4u * NS + 12u * i, 12, dstpos);

    for (int pi = 0; pi < f->placed.n; pi++) {
        uint32_t no = f->placed.p[pi].off, data = rd32o(f, no + 4);
        int op = f->placed.p[pi].op;
        dstpos = add_region(f, no, PC_NODE, dstpos);
        f->node_newoff[no] = (int32_t)(dstpos - PC_NODE);
        if (op > 24 || !N64_REC[op]) { FAIL(f, "%s: unknown opcode %d at %#x", f->name, op, no); f->bad = 1; return; }
        if (!PC_REC[op]) { FAIL(f, "%s: no PC record size for opcode %d", f->name, op); f->bad = 1; return; }
        if (data >= D) { f->bad = 1; return; }
        f->rec_newoff[data] = (int32_t)dstpos;
        dstpos = add_region(f, data, (uint32_t)PC_REC[op], dstpos);
        if (op == 4) {
            uint32_t nv = rdu16(f, data + 0x10), vo = rd32o(f, data + 0xC);
            if (vo) {
                if (!nv) VEC_PUSH(f->zero_vtx, vo);
                else {
                    VtxReg v = {vo, nv};
                    VEC_PUSH(f->vtx, v);
                    dstpos = add_region(f, vo, 16 * nv, dstpos);
                }
            }
        } else if (op == 24) {
            int32_t nv = rds16(f, data + 0xC), ncv = rds16(f, data + 0xE);
            uint32_t vo = rd32o(f, data + 8), cvo = rd32o(f, data + 0x10), puo = rd32o(f, data + 0x14);
            if (nv && vo) {
                VtxReg v = {vo, (uint32_t)nv};
                VEC_PUSH(f->vtx, v);
                dstpos = add_region(f, vo, 16u * (uint32_t)nv, dstpos);
            }
            if (ncv && cvo) {
                VtxReg v = {cvo, (uint32_t)ncv};
                if (cvo < D) f->is_coll[cvo] = 1;
                VEC_PUSH(f->vtx, v);
                dstpos = add_region(f, cvo, 16u * (uint32_t)ncv, dstpos);
            }
            if (nv && puo) {
                dstpos = add_region(f, puo, 2u * (uint32_t)nv, dstpos);
                if (puo < D) f->pointusage[puo] = (uint16_t)nv;
            }
        } else if (op == 22) {
            /* D303: count = number of star-flash ARMS (quads), 4 vertices each */
            int32_t nv = (int32_t)rd32(f, data);
            uint32_t vo = rd32o(f, data + 4);
            if (nv && vo) {
                VtxReg v = {vo, 4u * (uint32_t)nv};
                VEC_PUSH(f->vtx, v);
                dstpos = add_region(f, vo, 16u * 4u * (uint32_t)nv, dstpos);
            }
        }
    }

    /* record-referenced GDLs */
    {
        VEC(uint32_t) ref = {0}, unv = {0};
        uint8_t *visited = calloc(D + 1, 1);
        uint8_t *isobj = calloc(D + 1, 1);
        uint8_t *isregold = calloc(D + 1, 1);
        for (int pi = 0; pi < f->placed.n; pi++) {
            uint32_t no = f->placed.p[pi].off, data = rd32o(f, no + 4);
            int op = f->placed.p[pi].op;
            if (op == 4 || op == 24) {
                for (uint32_t off = 0; off <= 4; off += 4) {
                    uint32_t q = rd32o(f, data + off);
                    if (q && !in_vec_u32(ref.p, ref.n, q)) VEC_PUSH(ref, q);
                }
            } else if (op == 22) {
                uint32_t q = rd32o(f, data + 8);
                if (q && !in_vec_u32(ref.p, ref.n, q)) VEC_PUSH(ref, q);
            }
        }
        for (int i = 0; i < f->seq.n; i++) if (f->seq.p[i] < D) visited[f->seq.p[i]] = 1;
        for (int i = 0; i < ref.n; i++) {
            uint32_t g = ref.p[i];
            if (g >= D || !visited[g]) VEC_PUSH(unv, g);
        }
        if (unv.n) qsort(unv.p, (size_t)unv.n, sizeof(uint32_t), cmp_u32);
        for (int i = 0; i < unv.n; i++) VEC_PUSH(f->all_gdls, unv.p[i]);
        for (int i = 0; i < f->seq.n; i++) VEC_PUSH(f->all_gdls, f->seq.p[i]);

        /* "next object" boundaries for zero-length vertex arrays */
        for (int i = 0; i < f->placed.n; i++) isobj[f->placed.p[i].off] = 1;
        for (uint32_t i = 0; i < D; i++) if (f->rec_newoff[i] >= 0) isobj[i] = 1;
        for (int i = 0; i < f->vtx.n; i++) if (f->vtx.p[i].off < D) isobj[f->vtx.p[i].off] = 1;
        for (int i = 0; i < f->all_gdls.n; i++) if (f->all_gdls.p[i] < D) isobj[f->all_gdls.p[i]] = 1;
        for (int i = 0; i < f->zero_vtx.n; i++) {
            uint32_t vo = f->zero_vtx.p[i], nxt = D, sz;
            for (uint32_t o = vo + 1; o < D; o++) if (isobj[o]) { nxt = o; break; }
            sz = nxt - vo;
            if (vo >= D || sz == 0 || sz % 16) {
                FAIL(f, "%s: zero-vtx array %#x bad size %#x", f->name, vo, sz);
            } else {
                VtxReg v = {vo, sz / 16};
                VEC_PUSH(f->vtx, v);
                dstpos = add_region(f, vo, sz, dstpos);
            }
        }

        /* embedded image blobs: G_SETTIMG seg-5 targets */
        {
            VEC(uint32_t) bt = {0};
            for (int i = 0; i < f->all_gdls.n; i++) {
                uint32_t o = f->all_gdls.p[i];
                while ((size_t)o + 8 <= D) {
                    uint32_t w0 = cv_be32(src, o), c = w0 >> 24;
                    if (c == 0xB8) break;
                    if (c == 0xFD && (cv_be32(src, o + 4) >> 24) == 5) VEC_PUSH(bt, cv_be32(src, o + 4) & CV_MASK);
                    o += 8;
                }
            }
            for (int i = 0; i < f->regions.n; i++) if (f->regions.p[i].old < D) isregold[f->regions.p[i].old] = 1;
            if (bt.n) qsort(bt.p, (size_t)bt.n, sizeof(uint32_t), cmp_u32);
            {
                VEC(Region) merged = {0};   /* reuse: old=start, size=end */
                for (int i = 0; i < bt.n; i++) {
                    uint32_t t, end;
                    if (i && bt.p[i] == bt.p[i - 1]) continue;
                    t = bt.p[i];
                    end = D;
                    for (uint32_t o = t + 1; o < D; o++) if (isregold[o]) { end = o; break; }
                    if (merged.n && t <= merged.p[merged.n - 1].size) {
                        if (end > merged.p[merged.n - 1].size) merged.p[merged.n - 1].size = end;
                    } else {
                        Region r; r.old = t; r.size = end; r.nw = 0;
                        VEC_PUSH(merged, r);
                    }
                }
                for (int i = 0; i < merged.n; i++) {
                    uint32_t t = merged.p[i].old, e = merged.p[i].size;
                    Region r; r.old = t; r.size = e - t; r.nw = dstpos;
                    dstpos = add_region(f, t, e - t, dstpos);
                    VEC_PUSH(f->blobs, r);
                }
                VEC_FREE(merged);
            }
            VEC_FREE(bt);
        }
        VEC_FREE(ref); VEC_FREE(unv);
        free(visited); free(isobj); free(isregold);
    }

    /* GDLs last, tight-packed 16B slots: unvisited first, then visit order */
    for (int i = 0; i < f->all_gdls.n; i++) {
        uint32_t g = f->all_gdls.p[i], end, nslots;
        if (g >= D) { f->bad = 1; return; }
        end = gdl_end(f, g);
        nslots = (end - g) / 8;
        if ((end - g) % 8) FAIL(f, "%s: GDL %#x not 8B aligned end", f->name, g);
        f->gdl_newoff[g] = (int32_t)dstpos;
        {
            Region r; r.old = g; r.size = 16 * nslots; r.nw = dstpos;
            VEC_PUSH(f->regions, r);
        }
        dstpos += 16 * nslots;
    }
    f->D_PC = dstpos;
}

/* ------------------------------------------------------------------ emit -- */
static void emit_node_records(F *f)
{
    for (int pi = 0; pi < f->placed.n; pi++) {
        uint32_t no = f->placed.p[pi].off, d, r, npos = (uint32_t)f->node_newoff[no];
        int op = f->placed.p[pi].op;
        put_u16(f, npos, rdu16(f, no));
        put_ptr(f, npos + 8, rd32o(f, no + 4));
        put_ptr(f, npos + 16, rd32o(f, no + 8));
        put_ptr(f, npos + 24, rd32o(f, no + 0xC));
        put_ptr(f, npos + 32, rd32o(f, no + 0x10));
        put_ptr(f, npos + 40, rd32o(f, no + 0x14));

        d = rd32o(f, no + 4);
        r = (uint32_t)f->rec_newoff[d];
        switch (op) {
        case 1:
            put_u16(f, r, rdu16(f, d)); put_u16(f, r + 2, rdu16(f, d + 2));
            put_ptr(f, r + 8, rd32o(f, d + 4));
            put_u16(f, r + 16, rdu16(f, d + 8)); put_u16(f, r + 18, rdu16(f, d + 0xA));
            put_u16(f, r + 20, rdu16(f, d + 0xC));
            break;
        case 2: case 3:
            for (int i = 0; i < 3; i++) put_f32(f, r + 4 * i, d + 4 * i);
            put_u16(f, r + 12, rdu16(f, d + 0xC));
            for (int i = 0; i < 3; i++) put_u16(f, r + 14 + 2 * i, rdu16(f, d + 0xE + 2 * i));
            put_ptr(f, r + 24, rd32o(f, d + 0x14));
            put_f32(f, r + 32, d + 0x18);
            break;
        case 4: {
            uint32_t p = rd32o(f, d), s = rd32o(f, d + 4);
            put_gdlptr(f, r, p);
            put_gdlptr(f, r + 8, s);
            put_ptr(f, r + 24, rd32o(f, d + 0xC));
            put_u16(f, r + 32, rdu16(f, d + 0x10));
            f->buf[r + 34] = f->src[d + 0x12];
            break;
        }
        case 8:
            put_f32(f, r, d); put_f32(f, r + 4, d + 4);
            put_ptr(f, r + 8, rd32o(f, d + 8));
            put_u16(f, r + 16, rdu16(f, d + 0xC));
            break;
        case 9:
            for (int i = 0; i < 3; i++) put_f32(f, r + 4 * i, d + 4 * i);
            for (int i = 0; i < 3; i++) put_f32(f, r + 12 + 4 * i, d + 0xC + 4 * i);
            put_ptr(f, r + 24, rd32o(f, d + 0x18));
            put_ptr(f, r + 32, rd32o(f, d + 0x1C));
            put_u16(f, r + 42, rdu16(f, d + 0x22));
            break;
        case 10:
            put_u32(f, r, rd32(f, d));
            for (int i = 0; i < 6; i++) put_f32(f, r + 4 + 4 * i, d + 4 + 4 * i);
            break;
        case 12:
            for (int i = 0; i < 3; i++) put_f32(f, r + 4 * i, d + 4 * i);
            for (int i = 0; i < 3; i++) put_f32(f, r + 12 + 4 * i, d + 0xC + 4 * i);
            put_ptr(f, r + 24, rd32o(f, d + 0x18));
            put_f32(f, r + 32, d + 0x1C);
            put_u16(f, r + 36, rdu16(f, d + 0x20));
            break;
        case 13:
            for (int i = 0; i < 2; i++) put_f32(f, r + 4 * i, d + 4 * i);
            for (int i = 0; i < 2; i++) put_f32(f, r + 8 + 4 * i, d + 8 + 4 * i);
            put_ptr(f, r + 16, rd32o(f, d + 0x10));
            put_ptr(f, r + 24, rd32o(f, d + 0x14));
            put_f32(f, r + 32, d + 0x18);
            break;
        case 15:
            for (int i = 0; i < 3; i++) put_f32(f, r + 4 * i, d + 4 * i);
            for (int i = 0; i < 3; i++) put_f32(f, r + 12 + 4 * i, d + 0xC + 4 * i);
            put_f32(f, r + 24, d + 0x18);
            break;
        case 18:
            put_ptr(f, r, rd32o(f, d));
            put_u16(f, r + 8, rdu16(f, d + 4));
            break;
        case 21:
            for (int i = 0; i < 3; i++) put_f32(f, r + 4 * i, d + 4 * i);
            put_u16(f, r + 12, rdu16(f, d + 0xC)); put_u16(f, r + 14, rdu16(f, d + 0xE));
            put_f32(f, r + 16, d + 0x10);
            break;
        case 22: {
            put_u32(f, r, rd32(f, d));
            put_ptr(f, r + 8, rd32o(f, d + 4));
            put_gdlptr(f, r + 16, rd32o(f, d + 8));
            break;
        }
        case 23:
            put_u16(f, r, rdu16(f, d));
            break;
        case 24: {
            uint32_t p = rd32o(f, d), s = rd32o(f, d + 4);
            put_gdlptr(f, r, p);
            put_gdlptr(f, r + 8, s);
            put_ptr(f, r + 16, rd32o(f, d + 8));
            put_u16(f, r + 24, rdu16(f, d + 0xC)); put_u16(f, r + 26, rdu16(f, d + 0xE));
            put_ptr(f, r + 32, rd32o(f, d + 0x10));
            put_ptr(f, r + 40, rd32o(f, d + 0x14));
            put_u16(f, r + 48, rdu16(f, d + 0x18)); put_u16(f, r + 50, rdu16(f, d + 0x1A));
            break;
        }
        default: break;
        }
    }
}

static void emit_vertices(F *f)
{
    for (int k = 0; k < f->vtx.n; k++) {
        uint32_t vo = f->vtx.p[k].off, nv = f->vtx.p[k].nv;
        int64_t npos = remap(f, vo);
        if (npos < 0) { f->bad = 1; return; }
        if (vo < f->D && f->is_coll[vo]) {
            for (uint32_t i = 0; i < nv; i++) {
                uint32_t o = vo + 16 * i, q = (uint32_t)npos + 16 * i, v;
                for (int w = 0; w < 4; w++) put_u16(f, q + 2 * w, rdu16(f, o + 2 * w));
                v = rd32(f, o + 8);
                if (v == 0) {
                } else if ((v >> 24) == 5) {
                    int64_t n = remap(f, v & CV_MASK);
                    if (n < 0) { FAIL(f, "%s: LinkedTo %#x not in map", f->name, v); n = 0; }
                    put_u32(f, q + 8, 0x05000000u | ((uint32_t)n & CV_MASK));
                } else {
                    FAIL(f, "%s: LinkedTo %#x neither null nor seg-5", f->name, v);
                }
                put_u16(f, q + 12, rdu16(f, o + 0xC)); put_u16(f, q + 14, rdu16(f, o + 0xE));
            }
        } else {
            for (uint32_t i = 0; i < nv; i++) {
                uint32_t o = vo + 16 * i, q = (uint32_t)npos + 16 * i;
                for (int w = 0; w < 6; w++) put_u16(f, q + 2 * w, rdu16(f, o + 2 * w));
                if (o + 0x10 > f->D) { f->bad = 1; return; }
                memcpy(f->buf + q + 12, f->src + o + 0xC, 4);
            }
        }
    }
    /* D120: PointUsage[] s16 chain, plain byteswap */
    for (uint32_t puo = 0; puo < f->D; puo++) {
        uint32_t punv = f->pointusage[puo];
        int64_t pn;
        if (!punv) continue;
        pn = remap(f, puo);
        if (pn < 0) { FAIL(f, "%s: PointUsage %#x not in region map", f->name, puo); continue; }
        for (uint32_t k = 0; k < punv; k++) put_u16(f, (uint32_t)pn + 2 * k, rdu16(f, puo + 2 * k));
    }
}

static void emit_gdls(F *f)
{
    for (int i = 0; i < f->all_gdls.n; i++) {
        uint32_t g = f->all_gdls.p[i], end = gdl_end(f, g), npos = (uint32_t)f->gdl_newoff[g];
        uint32_t j = 0, o = g;
        while ((size_t)o + 8 <= end) {
            uint32_t w0 = cv_be32(f->src, o), w1 = cv_be32(f->src, o + 4), q = npos + 16 * j, c = w0 >> 24;
            if ((c == 0x04 || c == 0xFD) && (w1 >> 24) == 5) {
                int64_t n = remap(f, w1 & CV_MASK);
                if (n < 0) { FAIL(f, "%s: GDL %#x op%#x w1 %#x not in map", f->name, g, c, w1); n = 0; }
                w1 = 0x05000000u | ((uint32_t)n & CV_MASK);
            }
            put_u32(f, q, w0); put_u32(f, q + 4, 0);
            put_u32(f, q + 8, w1); put_u32(f, q + 12, 0);
            if (c == 0xB8) break;
            j++; o += 8;
        }
    }
    for (int i = 0; i < f->blobs.n; i++)
        memcpy(f->buf + f->blobs.p[i].nw, f->src + f->blobs.p[i].old, f->blobs.p[i].size);
}

/* ------------------------------------------------------------ validation -- */
static uint32_t lle32(const uint8_t *b, uint32_t o)
{
    return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}
static uint64_t lle64(const uint8_t *b, uint32_t o)
{
    return (uint64_t)lle32(b, o) | ((uint64_t)lle32(b, o + 4) << 32);
}

static int pc_ptr_off(int op, uint32_t fld)
{
    switch (op) {
    case 1: if (fld == 4) return 8; break;
    case 2: case 3: if (fld == 0x14) return 24; break;
    case 8: if (fld == 8) return 8; break;
    case 9: if (fld == 0x18) return 24; if (fld == 0x1C) return 32; break;
    case 12: if (fld == 0x18) return 24; break;
    case 13: if (fld == 0x10) return 16; if (fld == 0x14) return 24; break;
    case 18: if (fld == 0) return 0; break;
    case 24: if (fld == 8) return 16; if (fld == 0x10) return 32; if (fld == 0x14) return 40; break;
    case 4: if (fld == 0xC) return 24; break;
    case 22: if (fld == 4) return 8; break;
    }
    return -1;
}

static void rt_err(F *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void rt_err(F *f, const char *fmt, ...)
{
    char tmp[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    cv_err(f->c, "%s: RT %s", f->name, tmp);
}

static void validate(F *f)
{
    const uint8_t *buf = f->buf;
    int *ord;
    uint32_t pos = 0;

    for (int i = 0; i < f->blobs.n; i++)
        if (memcmp(buf + f->blobs.p[i].nw, f->src + f->blobs.p[i].old, f->blobs.p[i].size))
            rt_err(f, "blob %#x+%#x: pixel bytes differ at new %#x", f->blobs.p[i].old, f->blobs.p[i].size, f->blobs.p[i].nw);

    /* tiling: regions cover [0, D_PC) exactly */
    ord = f->rnew_idx;
    for (int i = 0; i < f->regions.n; i++) {
        const Region *r = &f->rold[ord[i]];
        if (r->nw != pos || r->nw + r->size > f->D_PC) {
            rt_err(f, "tiling gap/overlap at %#x (region %#x->%#x)", pos, r->old, r->nw);
            return;
        }
        pos += r->size;
    }
    if (pos != f->D_PC) { rt_err(f, "tiling ends at %#x != D_PC %#x", pos, f->D_PC); return; }

    for (int pi = 0; pi < f->placed.n; pi++) {
        uint32_t no = f->placed.p[pi].off, npos = (uint32_t)f->node_newoff[no];
        static const uint32_t FLD[5][2] = {{4, 8}, {8, 16}, {0xC, 24}, {0x10, 32}, {0x14, 40}};
        if (rdu16(f, no) != (lle32(buf, npos) & 0xFFFF)) rt_err(f, "node %#x opcode mismatch", no);
        for (int k = 0; k < 5; k++) {
            uint32_t old = rd32o(f, no + FLD[k][0]);
            uint64_t v = lle64(buf, npos + FLD[k][1]);
            if (old == 0) {
                if (v != 0) rt_err(f, "node %#x+%#x: expected null", no, FLD[k][0]);
            } else {
                int64_t want = remap(f, old);
                if ((v >> 24) != 5 || (int64_t)(v & CV_MASK) != want)
                    rt_err(f, "node %#x+%#x: %#llx != 0x05|%lld", no, FLD[k][0], (unsigned long long)v, (long long)want);
            }
        }
    }

    for (int pi = 0; pi < f->placed.n; pi++) {
        uint32_t no = f->placed.p[pi].off, d = rd32o(f, no + 4), r = (uint32_t)f->rec_newoff[d];
        int op = f->placed.p[pi].op;
        static const uint32_t FLDS[] = {0, 4, 8, 0xC, 0x10, 0x14, 0x18, 0x1C};
        for (size_t k = 0; k < sizeof FLDS / sizeof FLDS[0]; k++) {
            uint32_t fld = FLDS[k];
            int po = pc_ptr_off(op, fld);
            uint32_t old;
            uint64_t v;
            int64_t want;
            if (po < 0) continue;
            old = rd32o(f, d + fld);
            v = lle64(buf, r + (uint32_t)po);
            if (old == 0) {
                if (v != 0) rt_err(f, "rec%d %#x+%#x: expected null", op, d, fld);
            } else {
                want = remap(f, old);
                if ((v >> 24) != 5 || (int64_t)(v & CV_MASK) != want) rt_err(f, "rec%d %#x+%#x: %#llx", op, d, fld, (unsigned long long)v);
            }
        }
    }

    for (int i = 0; i < f->all_gdls.n; i++) {
        uint32_t g = f->all_gdls.p[i], end = gdl_end(f, g), npos = (uint32_t)f->gdl_newoff[g], j = 0, o = g;
        while ((size_t)o + 8 <= end) {
            uint32_t w0 = cv_be32(f->src, o), w1 = cv_be32(f->src, o + 4), q = npos + 16 * j, c = w0 >> 24, w1p;
            if (lle32(buf, q) != w0) rt_err(f, "gdl %#x slot %u: w0 mismatch", g, j);
            if (lle32(buf, q + 4) != 0) rt_err(f, "gdl %#x slot %u: pad[4..8) nonzero", g, j);
            w1p = lle32(buf, q + 8);
            if ((c == 0x04 || c == 0xFD) && (w1 >> 24) == 5) {
                int64_t want = remap(f, w1 & CV_MASK);
                if (want < 0 || w1p != (0x05000000u | (uint32_t)want)) rt_err(f, "gdl %#x slot %u: w1 %#x != 0x05|%lld", g, j, w1p, (long long)want);
            } else if (w1p != w1) {
                rt_err(f, "gdl %#x slot %u: w1 passthrough mismatch", g, j);
            }
            if (lle32(buf, q + 12) != 0) rt_err(f, "gdl %#x slot %u: pad[12..16) nonzero", g, j);
            if (c == 0xB8) break;
            j++; o += 8;
        }
    }

    for (uint32_t puo = 0; puo < f->D; puo++) {
        uint32_t punv = f->pointusage[puo];
        int64_t pn;
        int allzero = 1;
        if (!punv) continue;
        pn = remap(f, puo);
        if (pn < 0) { rt_err(f, "PointUsage %#x not in region map", puo); continue; }
        for (uint32_t k = 0; k < punv; k++) {
            int32_t want = rds16(f, puo + 2 * k);
            int32_t got = (int16_t)(lle32(buf, (uint32_t)pn + 2 * k) & 0xFFFF);
            if (got != want) rt_err(f, "PointUsage %#x[%u]: %d != %d", puo, k, got, want);
            if (got != 0) allzero = 0;
            if (got >= (int32_t)punv) rt_err(f, "PointUsage %#x[%u] = %d >= numVertices %u", puo, k, got, punv);
        }
        if (allzero && punv > 1) rt_err(f, "PointUsage %#x: all %u entries zero (emit gap?)", puo, punv);
    }
}

/* ----------------------------------------------------------- per file ---- */
static int is_model_name(const char *n)
{
    size_t len = strlen(n);
    return len && strchr("CGP", n[0]) && n[len - 1] == 'Z' && !strstr(n, "_stan");
}

/* candidates (name, name[1:], name[:-1], name[1:-1]) */
static int candidate(const char *name, int k, char *out, size_t cap)
{
    size_t len = strlen(name);
    const char *s = name;
    size_t n = len;
    if (k == 1 || k == 3) { s++; n--; }
    if (k == 2 || k == 3) n--;
    if ((k == 3 && len < 2) || ((k == 1 || k == 2) && len < 1) || n >= cap) return 0;
    memcpy(out, s, n);
    out[n] = 0;
    return 1;
}

static int process(CvCtx *c, const char *name, CvBuf *comp_out)
{
    char key[160];
    const CvFileRow *row = NULL;
    CvBuf dec = {0};
    F f;
    int hk = -1, rc = -1, errs0 = c->nerrors;
    int ns = 0, nt = 0;

    memset(&f, 0, sizeof f);
    for (int k = 0; k < 4 && !row; k++)
        if (candidate(name, k, key, sizeof key)) row = cv_find_row(c->rd, key);
    if (!row) { cv_err(c, "%s: no filelist row", name); return -1; }
    /* key now holds the matching candidate? recompute precisely */
    for (int k = 0; k < 4; k++)
        if (candidate(name, k, key, sizeof key) && cv_find_row(c->rd, key)) break;

    if ((size_t)row->off + row->size > c->rom_size) { cv_err(c, "%s: file past end of ROM", name); return -1; }
    if (cv_rz_inflate(c->rom + row->off, row->size, &dec)) { cv_err(c, "%s: decompress failed", name); cv_buf_free(&dec); return -1; }

    for (int k = 0; k < 4 && hk < 0; k++) {
        char cand[160];
        if (!candidate(key, k, cand, sizeof cand)) continue;
        for (int i = 0; i < cv_nsnt_count; i++)
            if (!strcmp(cv_nsnt[i].name, cand)) { hk = i; break; }
    }
    if (hk < 0) { cv_err(c, "%s: no header data", name); cv_buf_free(&dec); return -1; }
    ns = cv_nsnt[hk].ns; nt = cv_nsnt[hk].nt;

    f.c = c; f.name = name; f.src = dec.p; f.D = (uint32_t)dec.len; f.NS = ns; f.NT = nt;
    f.nodes = calloc(f.D + 1, sizeof(Node));
    f.node_newoff = malloc((f.D + 1) * sizeof(int32_t));
    f.rec_newoff = malloc((f.D + 1) * sizeof(int32_t));
    f.gdl_newoff = malloc((f.D + 1) * sizeof(int32_t));
    memset(f.node_newoff, 0xFF, (f.D + 1) * sizeof(int32_t));
    memset(f.rec_newoff, 0xFF, (f.D + 1) * sizeof(int32_t));
    memset(f.gdl_newoff, 0xFF, (f.D + 1) * sizeof(int32_t));
    f.is_coll = calloc(f.D + 1, 1);
    f.pointusage = calloc(f.D + 1, sizeof(uint16_t));

    if (!build_nodes(&f)) { cv_err(c, "%s: empty tree", name); goto done; }
    visit_seq(&f);
    placement_order(&f);
    if (f.bad) { cv_err(c, "%s: malformed node graph", name); goto done; }

    layout(&f);
    if (f.bad || c->nerrors != errs0) { if (f.bad) cv_err(c, "%s: layout failed", name); goto done; }

    /* sorted-by-old copy of the regions + index of it sorted by new offset */
    qsort(f.regions.p, (size_t)f.regions.n, sizeof(Region), cmp_region_old);
    f.rold = f.regions.p;
    f.rnew_idx = malloc((size_t)f.regions.n * sizeof(int));
    for (int i = 0; i < f.regions.n; i++) f.rnew_idx[i] = i;
    g_cmp_base = f.rold;
    qsort(f.rnew_idx, (size_t)f.regions.n, sizeof(int), cmp_new_idx);

    f.buf = calloc(f.D_PC + 16, 1);
    /* switches + texconfigs */
    for (int i = 0; i < ns; i++) put_ptr(&f, 8u * i, rd32o(&f, 4u * i));
    for (int i = 0; i < nt; i++) {
        uint32_t doff = 4u * ns + 12u * i, npos = 8u * ns + 12u * i, tid = rd32(&f, doff);
        put_u32(&f, npos, tid);
        if (doff + 12 > f.D) { f.bad = 1; break; }
        memcpy(f.buf + npos + 4, f.src + doff + 4, 8);
        if ((tid >> 24) == 5 && remap(&f, tid & CV_MASK) < 0)
            cv_err(c, "%s: texconfig seg-5 target %#x not in a blob region", name, tid & CV_MASK);
    }
    emit_node_records(&f);
    emit_vertices(&f);
    emit_gdls(&f);
    if (f.bad) { cv_err(c, "%s: out-of-range read during emit", name); goto done; }

    validate(&f);
    if (c->nerrors != errs0) goto done;

    if (cv_rz_deflate(f.buf, f.D_PC, comp_out)) { cv_err(c, "%s: deflate failed", name); goto done; }
    rc = 0;
done:
    VEC_FREE(f.seq); VEC_FREE(f.placed); VEC_FREE(f.regions); VEC_FREE(f.vtx);
    VEC_FREE(f.zero_vtx); VEC_FREE(f.all_gdls); VEC_FREE(f.blobs);
    free(f.nodes); free(f.node_newoff); free(f.rec_newoff); free(f.gdl_newoff);
    free(f.is_coll); free(f.pointusage); free(f.rnew_idx); free(f.buf);
    cv_buf_free(&dec);
    return rc;
}

int cv_run_d43(CvCtx *c, const char *out_dir)
{
    CvSidecar sc;
    int nmodels = 0;
    memset(&sc, 0, sizeof sc);
    for (int i = 0; i < cv_table_count; i++) {
        CvBuf comp = {0};
        if (!is_model_name(cv_table_names[i])) continue;
        nmodels++;
        if (process(c, cv_table_names[i], &comp) == 0)
            cv_sc_emit(&sc, cv_table_names[i], comp.p, comp.len);
        cv_buf_free(&comp);
    }
    printf("files converted: %d / %d\n", sc.n, nmodels);
    if (c->nerrors) { cv_sc_free(&sc); return -1; }
    /* pcmodels.bin is padded to a 16-byte multiple (python wrote the tail pad) */
    cv_buf_zero(&sc.img, (16 - (sc.img.len % 16)) % 16);
    if (cv_sc_write(&sc, out_dir, "pcmodels.bin")) { fprintf(stderr, "cannot write %s\n", out_dir); cv_sc_free(&sc); return -1; }
    printf("wrote %d sidecars to %s/pcmodels.bin\n", sc.n, out_dir);
    printf("total reservation needed: %#zx bytes (romSize + this must fit the extended VirtualAlloc)\n", sc.img.len);
    cv_sc_free(&sc);
    return 0;
}
