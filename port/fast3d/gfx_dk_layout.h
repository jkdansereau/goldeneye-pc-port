#ifndef GFX_DK_LAYOUT_H
#define GFX_DK_LAYOUT_H

/* D973: deko3d backend, the parts shared by the shader generator (host tool
 * tools_pc/dkshgen) and the backend (gfx_deko3d.cpp): vertex attribute
 * locations, the vertex layout of a combiner program (same order and sizes as
 * gfx_opengl_create_and_load_new_shader), the uniform blocks, the combiner
 * parameters the uber-shader reads, and the shader pack format.
 * Pure C, no deko3d types. */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "gfx_cc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Vertex attribute locations; varyings use the same numbers (deko3d binds
 * stages separately, so outputs and inputs meet by location). */
enum {
    DK_ATTR_POS = 0,
    DK_ATTR_TC0 = 1,
    DK_ATTR_TC1 = 2,
    DK_ATTR_CLS0 = 3,
    DK_ATTR_CLT0 = 4,
    DK_ATTR_CLS1 = 5,
    DK_ATTR_CLT1 = 6,
    DK_ATTR_FOG = 7,
    DK_ATTR_GRAY = 8,
    DK_ATTR_IN1 = 9, /* .. IN7 = 15 */
    DK_ATTR_COUNT = 16,
    DK_VAR_FOGW = 16  /* varying only: fog * w, perspective-interpolated (per-pixel fog) */
};

/* The vertex layout of a program: count attributes, loc/size/offset in floats. */
struct DkLayout {
    uint8_t num_floats;
    uint8_t count;
    uint8_t loc[16];
    uint8_t size[16];
    uint8_t offset[16];
};

static inline void dk_layout_add(struct DkLayout* out, uint8_t loc, uint8_t size) {
    out->loc[out->count] = loc;
    out->size[out->count] = size;
    out->offset[out->count] = out->num_floats;
    out->num_floats = (uint8_t)(out->num_floats + size);
    out->count++;
}

/* Same order and sizes as GL: pos 4; per used texture tc 2 then clamp S/T 1
 * each (when clamped); fog 4; grayscale 4; inputs 3 or 4 (opt_alpha). */
static inline void dk_layout_from_features(const struct CCFeatures* f, struct DkLayout* out) {
    memset(out, 0, sizeof(*out));
    dk_layout_add(out, DK_ATTR_POS, 4);
    for (int i = 0; i < 2; i++) {
        if (f->used_textures[i]) {
            dk_layout_add(out, (uint8_t)(DK_ATTR_TC0 + i), 2);
            for (int j = 0; j < 2; j++) {
                if (f->clamp[i][j]) {
                    dk_layout_add(out, (uint8_t)(DK_ATTR_CLS0 + 2 * i + j), 1);
                }
            }
        }
    }
    if (f->opt_fog) {
        dk_layout_add(out, DK_ATTR_FOG, 4);
    }
    if (f->opt_grayscale) {
        dk_layout_add(out, DK_ATTR_GRAY, 4);
    }
    for (int i = 0; i < f->num_inputs; i++) {
        dk_layout_add(out, (uint8_t)(DK_ATTR_IN1 + i), f->opt_alpha ? 4 : 3);
    }
}

/* Uniform block `Draw` (std140, binding 0, fragment stage): what GL sets with
 * glUniform at program load (frame_count, noise_scale, three-point flags),
 * plus the deko3d-only flip of a unit bound to a framebuffer image. */
struct DkDrawUniforms {
    int32_t frame_count;
    float noise_scale;
    int32_t three_point0;
    int32_t three_point1;
    int32_t flip_v0;   /* unit samples a rendered image (stored top-down): t' = 1 - t */
    int32_t flip_v1;
    int32_t fog_pixel; /* Video.FogPixel: fragment fog from vFogW (fog * w), else the per-vertex vFog.a */
    int32_t pad;
};

/* Vertex-stage data for GE_MESHGPU.  The baked position is model-space xyz
 * plus the index of the entry's live matrix.  This is deliberately a separate
 * UBO from Draw: Draw is fragment-only and changes for every classic batch. */
struct DkMeshUniforms {
    float mtx[32][16];
    float fog_color[4];
    float fog_mul, fog_off;
    int32_t fog_from_z;
    int32_t pad0;
    float lod_mask[8][4];
    float env_in[8][4];     /* live env colour per input (GE_MESHGPU) */
    float env_mask[8][4];
};

/* Uniform block `Comb` (std140, binding 1, fragment stage) of the uber-shader.
 * sel[c][k]: cycle c, channel k (0 rgb, 1 alpha), four 8-bit sources a, b, c, d
 * (SHADER_* of gfx_cc.h) at bits 0, 8, 16, 24. flags: DK_CF_* below. pad:
 * color_alpha_same per cycle (bits 0, 1; informational). */
struct DkCombinerParams {
    uint32_t sel[2][2];
    uint32_t flags;
    uint32_t pad;
};

enum {
    DK_CF_ALPHA = 1u << 0,
    DK_CF_FOG = 1u << 1,
    DK_CF_TEXEDGE = 1u << 2,
    DK_CF_NOISE = 1u << 3,
    DK_CF_2CYC = 1u << 4,
    DK_CF_ATHRESH = 1u << 5,
    DK_CF_INVISIBLE = 1u << 6,
    DK_CF_GRAYSCALE = 1u << 7,
    DK_CF_BLUR = 1u << 8,
    DK_CF_TEX0 = 1u << 9,
    DK_CF_TEX1 = 1u << 10,
    DK_CF_CLAMP_S0 = 1u << 11,
    DK_CF_CLAMP_T0 = 1u << 12,
    DK_CF_CLAMP_S1 = 1u << 13,
    DK_CF_CLAMP_T1 = 1u << 14,
    DK_CF_NUM_INPUTS_SHIFT = 16, /* 3 bits */
    DK_CF_DO_SHIFT = 20          /* 12 bits: (c*2+k)*3 + {single, multiply, mix} */
};

static inline void dk_combiner_params(const struct CCFeatures* f, struct DkCombinerParams* out) {
    memset(out, 0, sizeof(*out));
    for (int c = 0; c < 2; c++) {
        for (int k = 0; k < 2; k++) {
            uint32_t v = 0;
            for (int i = 0; i < 4; i++) {
                v |= (uint32_t)(f->c[c][k][i] & 0xff) << (i * 8);
            }
            out->sel[c][k] = v;
            const int b = DK_CF_DO_SHIFT + (c * 2 + k) * 3;
            if (f->do_single[c][k]) out->flags |= 1u << b;
            if (f->do_multiply[c][k]) out->flags |= 1u << (b + 1);
            if (f->do_mix[c][k]) out->flags |= 1u << (b + 2);
        }
        if (f->color_alpha_same[c]) out->pad |= 1u << c;
    }
    if (f->opt_alpha) out->flags |= DK_CF_ALPHA;
    if (f->opt_fog) out->flags |= DK_CF_FOG;
    if (f->opt_texture_edge) out->flags |= DK_CF_TEXEDGE;
    if (f->opt_noise) out->flags |= DK_CF_NOISE;
    if (f->opt_2cyc) out->flags |= DK_CF_2CYC;
    if (f->opt_alpha_threshold) out->flags |= DK_CF_ATHRESH;
    if (f->opt_invisible) out->flags |= DK_CF_INVISIBLE;
    if (f->opt_grayscale) out->flags |= DK_CF_GRAYSCALE;
    if (f->opt_blur) out->flags |= DK_CF_BLUR;
    if (f->used_textures[0]) out->flags |= DK_CF_TEX0;
    if (f->used_textures[1]) out->flags |= DK_CF_TEX1;
    if (f->clamp[0][0]) out->flags |= DK_CF_CLAMP_S0;
    if (f->clamp[0][1]) out->flags |= DK_CF_CLAMP_T0;
    if (f->clamp[1][0]) out->flags |= DK_CF_CLAMP_S1;
    if (f->clamp[1][1]) out->flags |= DK_CF_CLAMP_T1;
    out->flags |= ((uint32_t)f->num_inputs & 7u) << DK_CF_NUM_INPUTS_SHIFT;
}

/* Source i (0 a, 1 b, 2 c, 3 d) of cycle c, channel k. */
static inline uint8_t dk_combiner_sel(const struct DkCombinerParams* p, int c, int k, int i) {
    return (uint8_t)((p->sel[c][k] >> (i * 8)) & 0xff);
}

/* Shader pack (ge007_dksh.bin, little endian): "DKSHPACK", u32 count, u32
 * reserved, then count entries sorted by (id0, id1), then the DKSH blobs
 * (each at a 256-byte aligned offset; len 0 = absent). Named entries have
 * id0 = 0 and id1 < 16 (a combiner's id1 is never that small with id0 = 0). */
#define DK_PACK_MAGIC "DKSHPACK"
struct DkPackEntry {
    uint64_t id0;
    uint32_t id1;
    uint32_t vert_off;
    uint32_t vert_len;
    uint32_t frag_off;
    uint32_t frag_len;
    uint32_t mesh_vert_off; /* GE_MESHGPU vertex twin (absent for named entries) */
    uint32_t mesh_vert_len;
};
enum {
    DK_PACK_UBER = 1,       /* uber vert + frag */
    DK_PACK_FULLSCREEN = 2, /* fullscreen-triangle vert (frag unused) */
    DK_PACK_FXAA = 3,       /* fxaa frag (vert unused) */
    DK_PACK_SKYPLUS = 4,    /* GE+ sky frag (vert unused) */
    DK_PACK_NAMED_MAX = 16
};

#ifdef __cplusplus
}
#endif

#endif
