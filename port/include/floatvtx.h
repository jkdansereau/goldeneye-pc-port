#ifndef PORT_FLOATVTX_H
#define PORT_FLOATVTX_H

/* D245 (M-201): port-only full-precision vertex load for the sky/water fans.
 *
 * On N64, sky.c builds these triangles directly as RDP edge/texture
 * coefficients with 32-bit S/T, so a horizon-scale quad (the IsWater ocean
 * spans ~300,000 S10.5 units, ~9,400 texels) is exact. Routed through an
 * ordinary gSPVertex, both its texture coords (s16 tc: +-1,024 texels) and its
 * positions (s16 ob, divided by a fan-wide wScale to fit camera-space w up to
 * ~2e5) had to be quantised -- visible as water/sky jitter that slides with
 * the view. G_FLOATVTX_EXT hands fast3d the clip-space position and S/T as
 * floats instead; fast3d already stores every loaded vertex as floats.
 *
 * w0 = G_FLOATVTX_EXT << 24 | dest_index << 16 | count, w1 = PortFloatVtx *.
 * s/t are in Vtx.tc units (S10.5, before gSPTexture scaling). */
#define G_FLOATVTX_EXT 0x47

typedef struct PortFloatVtx {
    float x, y, z, w;   /* clip space */
    float s, t;         /* tc units */
    unsigned char r, g, b, a;
} PortFloatVtx;

#define gSPFloatVertexExt(pkt, ptr, n, v0)                                        \
    do {                                                                          \
        Gfx *_g = (Gfx *)(pkt);                                                   \
        _g->words.w0 = ((uintptr_t)G_FLOATVTX_EXT << 24) |                        \
                       (((uintptr_t)(v0) & 0xff) << 16) | ((uintptr_t)(n) & 0xffff); \
        _g->words.w1 = GBI_W1(ptr);                                               \
    } while (0)

#endif
