#define NOMINMAX

#include "floatvtx.h" /* D245 */
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdio>

#include <map>
#include <set>
#include <unordered_map>
#include <vector>
#include <list>
#include <stack>
#include <string>
#include <iostream>
#include <memory>
#include <chrono>
#include <limits>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>
#include "gbiex.h" /* GE's G_TRI4 + PD extension opcodes (see header) */

#include "platform.h"

#include "gfx_pc.h"
#include "gfx_cc.h"
#include "gfx_window_manager_api.h"
#include "gfx_rendering_api.h"
#include "gfx_screen_config.h"

uintptr_t gfxFramebuffer;

#define ALIGN(x, a) (((x) + (a - 1)) & ~(a - 1))

#define SUPPORT_CHECK(x) assert(x)

// SCALE_M_N: upscale/downscale M-bit integer to N-bit
#define SCALE_5_8(VAL_) (((VAL_)*0xFF) / 0x1F)
#define SCALE_8_5(VAL_) ((((VAL_) + 4) * 0x1F) / 0xFF)
#define SCALE_4_8(VAL_) ((VAL_)*0x11)
#define SCALE_8_4(VAL_) ((VAL_) / 0x11)
#define SCALE_3_8(VAL_) ((VAL_)*0x24)
/* D266: RDP IA4 is I2:A2 (2-bit intensity, 2-bit alpha), not the I3:A1 the
 * PD-derived importer assumed. */
#define SCALE_2_8(VAL_) ((VAL_)*0x55)
#define SCALE_8_3(VAL_) ((VAL_) / 0x24)

// SCREEN_WIDTH and SCREEN_HEIGHT are defined in the headerfile
#define HALF_SCREEN_WIDTH (SCREEN_WIDTH / 2.f)
#define HALF_SCREEN_HEIGHT (SCREEN_HEIGHT / 2.f)

#define RATIO_X (gfx_current_dimensions.width / (float)SCREEN_WIDTH)
#define RATIO_Y (gfx_current_dimensions.height / (float)SCREEN_HEIGHT)

#define MAX_BUFFERED 256
#define MAX_LIGHTS 4
#define MAX_VERTICES 128
#define MAX_VERTEX_COLORS 64

#define TEXTURE_CACHE_MAX_SIZE 1024

#define C0(pos, width) ((cmd->words.w0 >> (pos)) & ((1U << width) - 1))
#define C1(pos, width) ((cmd->words.w1 >> (pos)) & ((1U << width) - 1))

struct RGBA {
    uint8_t r, g, b, a;
};

struct NormalColor {
    union {
        struct { uint8_t r, g, b, a; };
        struct { int8_t x, y, z, w; };
    };
};

struct LoadedVertex {
    float x, y, z, w;
    float u, v;
    struct RGBA color;
    uint8_t fog;
    uint8_t clip_rej;
};

static struct {
    TextureCacheMap map;
    std::list<TextureCacheMapIter> lru;
    std::vector<uint32_t> free_texture_ids;
} gfx_texture_cache;

struct ColorCombiner {
    uint64_t shader_id0;
    uint32_t shader_id1;
    bool used_textures[2];
    struct ShaderProgram* prg[16];
    uint8_t shader_input_mapping[2][7];
};

static std::map<ColorCombinerKey, struct ColorCombiner> color_combiner_pool;
static std::map<ColorCombinerKey, struct ColorCombiner>::iterator prev_combiner = color_combiner_pool.end();

static uint8_t* tex_upload_buffer = nullptr;

static struct RSP {
    float modelview_matrix_stack[11][4][4];
    uint8_t modelview_matrix_stack_size;

    float MP_matrix[4][4];
    float P_matrix[4][4];

    Light_t lookat[2];
    bool lookat_enabled;

    Light_t current_lights[MAX_LIGHTS + 1];
    float current_lights_coeffs[MAX_LIGHTS][3];
    float current_lookat_coeffs[2][3]; // lookat_x, lookat_y
    uint8_t current_num_lights;        // includes ambient light
    bool lights_changed;

    uint32_t geometry_mode;
    int16_t fog_mul, fog_offset;

    uint32_t extra_geometry_mode;

    uint32_t aspect_mode;
    float aspect_ofs;
    float aspect_scale;

    struct {
        // U0.16
        uint16_t s, t;
    } texture_scaling_factor;

    struct LoadedVertex loaded_vertices[MAX_VERTICES + 4];

    const struct NormalColor *vertex_colors; //[MAX_VERTEX_COLORS];
} rsp;

struct RawTexMetadata {
    uint16_t width, height;
    float h_byte_scale = 1, v_pixel_scale = 1;
};

struct LoadedTexture {
    const uint8_t* addr;
    uint32_t orig_size_bytes;
    uint32_t full_size_bytes; // full_image_line_size_bytes * height
    uint32_t size_bytes; // line_size_bytes * height
    uint32_t full_image_line_size_bytes;
    uint32_t line_size_bytes;
    uint32_t tex_flags;
    struct RawTexMetadata raw_tex_metadata;
    /* D229: G_IM_FMT_* of the gDPSetTextureImage active when this slot was
     * last written by a load command (0xFF = never / unknown). Used to tell
     * a CI8 index stream apart from real RGBA16 pixels when a later tile
     * re-declares the same TMEM in another format -- see import_texture. */
    uint8_t src_fmt = 0xFF;
};

static struct RDP {
    uint16_t palette[256];
    const uint8_t* palette_addrs[2];
    uint32_t palette_fmt;
    uint32_t palette_hash; /* D217: FNV-1a of palette[], refreshed in gfx_dp_load_tlut */
    struct {
        const uint8_t* addr;
        uint8_t fmt; /* D229: was dropped before; needed to track CI sources */
        uint8_t siz;
        uint32_t width;
        uint32_t tex_flags;
        struct RawTexMetadata raw_tex_metadata;
    } texture_to_load;
    struct {
        uint8_t fmt;
        uint8_t siz;
        uint8_t cms, cmt;
        uint8_t masks, maskt; /* RC3: N64 tile mask; wrap period = 1<<mask */
        uint8_t shifts, shiftt;
        uint16_t uls, ult, lrs, lrt; // U10.2
        uint16_t width, height;      // in texels
        uint16_t tmem;               // 0-511, in 64-bit word units
        uint32_t line_size_bytes;
        uint8_t palette;
    } texture_tile[8];
    LoadedTexture loaded_texture[512]; // for each tmem location
    bool textures_changed[2];

    uint8_t first_tile_index;
    uint8_t tex_min_lod;
    uint8_t tex_max_lod;

    uint32_t other_mode_l, other_mode_h;
    uint64_t combine_mode;
    bool grayscale;
    bool tex_lod;
    bool tex_detail;

    uint8_t prim_lod_fraction;
    struct RGBA env_color, prim_color, fog_color, fill_color, grayscale_color;
    struct XYWidthHeight viewport, scissor;
    bool viewport_or_scissor_changed;
    void* z_buf_address;
    void* color_image_address;

    int16_t subpixel_ofs_x;
    int16_t subpixel_ofs_y;
} rdp;

static struct RenderingState {
    uint8_t depth_mode;
    bool alpha_blend;
    bool modulate;
    struct XYWidthHeight viewport, scissor;
    struct ShaderProgram* shader_program;
    TextureCacheNode* textures[SHADER_MAX_TEXTURES];
} rendering_state;

struct GfxDimensions gfx_current_window_dimensions;
int32_t gfx_current_window_position_x;
int32_t gfx_current_window_position_y;
struct GfxDimensions gfx_current_dimensions;
static struct GfxDimensions gfx_prev_dimensions;
struct XYWidthHeight gfx_current_game_window_viewport;
struct XYWidthHeight gfx_current_native_viewport;
float gfx_current_native_aspect = 4.f / 3.f;
bool gfx_framebuffers_enabled = true;
bool gfx_detail_textures_enabled = true;

static bool game_renders_to_framebuffer;
static int game_framebuffer;
static int game_framebuffer_msaa_resolved;

/* Safe-area (TV-overscan) crop. GE's own N64 game code insets its normal
 * single-player "Full" gameplay viewport a fixed margin from the true VI
 * framebuffer edges (src/fr.h: VIEWPORT_HEIGHT_DEFAULT_NTSC=220 of a
 * 240-line frame -- ~8% top+bottom; PAL's equivalent is already full-height,
 * no margin) and separately fills that margin with black rectangles
 * (src/fr.c viSetupScreensForNumPlayers). On a real CRT that margin falls
 * in the invisible overscan region; this port displays the full VI frame,
 * so the margin shows up as literal top/bottom black bars.
 *
 * g_gpSafeTop/g_gpSafeHeight cache the most recently set SP viewport's raw
 * (pre window-scale) Y bounds, in the same native-VI-Y units gfx_pc.cpp
 * uses elsewhere (see gfx_calc_and_set_viewport). gfx_adjust_viewport_or_
 * scissor remaps every screen-space Y (the 3D viewport itself AND RDP
 * fill-rect/texture-rect draws, which deliberately bypass the current SP
 * viewport and use the full VI canvas -- see gfx_draw_rectangle's
 * default_viewport) against this cached region instead of the full VI
 * canvas, so it fills the window edge to edge. Self-gating: front-end/menus
 * and PAL "Full" never inset their viewport (top=0, full height), so this
 * is a no-op there; -1 sentinel means "no viewport captured yet",
 * passthrough (identical to original behavior). */
static float g_gpSafeTop = 0.0f;
static float g_gpSafeHeight = -1.0f;
static bool g_safe_area_crop_enabled = true;

extern "C" void gfx_set_safe_area_crop(int on) {
    g_safe_area_crop_enabled = !!on;
}

uint32_t gfx_msaa_level = 1;

static bool dropped_frame;

static float buf_vbo[MAX_BUFFERED * (32 * 3)]; // 3 vertices in a triangle and 32 floats per vtx
static size_t buf_vbo_len;
static size_t buf_vbo_num_tris;

static struct GfxWindowManagerAPI* gfx_wapi;
static struct GfxRenderingAPI* gfx_rapi;

static uintptr_t segmentPointers[16];

struct FBInfo {
    uint32_t orig_width, orig_height;
    uint32_t applied_width, applied_height;
    bool upscale, autoresize;
};

static bool fbActive = 0;
static std::map<int, FBInfo>::iterator active_fb;
static std::map<int, FBInfo> framebuffers;

static constexpr float clampf(const float x, const float min, const float max) {
    return (x < min) ? min : (x > max) ? max : x;
}

/* #92 perf probe (GE_PERFSTAT=1): per-frame counters, see gfx_run. */
static uint64_t s_perf_batches = 0, s_perf_tris = 0;

static void gfx_flush(void) {
    if (buf_vbo_len > 0) {
        s_perf_batches++;
        s_perf_tris += buf_vbo_num_tris;
        gfx_rapi->draw_triangles(buf_vbo, buf_vbo_len, buf_vbo_num_tris);
        buf_vbo_len = 0;
        buf_vbo_num_tris = 0;
    }
}

static struct ShaderProgram* gfx_lookup_or_create_shader_program(uint64_t shader_id0, uint32_t shader_id1) {
    struct ShaderProgram* prg = gfx_rapi->lookup_shader(shader_id0, shader_id1);
    if (prg == NULL) {
        gfx_rapi->unload_shader(rendering_state.shader_program);
        prg = gfx_rapi->create_and_load_new_shader(shader_id0, shader_id1);
        rendering_state.shader_program = prg;
    }
    return prg;
}

static const char* ccmux_to_string(uint32_t ccmux) {
    static const char* const tbl[] = {
        "G_CCMUX_COMBINED",
        "G_CCMUX_TEXEL0",
        "G_CCMUX_TEXEL1",
        "G_CCMUX_PRIMITIVE",
        "G_CCMUX_SHADE",
        "G_CCMUX_ENVIRONMENT",
        "G_CCMUX_1",
        "G_CCMUX_COMBINED_ALPHA",
        "G_CCMUX_TEXEL0_ALPHA",
        "G_CCMUX_TEXEL1_ALPHA",
        "G_CCMUX_PRIMITIVE_ALPHA",
        "G_CCMUX_SHADE_ALPHA",
        "G_CCMUX_ENV_ALPHA",
        "G_CCMUX_LOD_FRACTION",
        "G_CCMUX_PRIM_LOD_FRAC",
        "G_CCMUX_K5",
    };
    if (ccmux > 15) {
        return "G_CCMUX_0";

    } else {
        return tbl[ccmux];
    }
}

static const char* acmux_to_string(uint32_t acmux) {
    static const char* const tbl[] = {
        "G_ACMUX_COMBINED or G_ACMUX_LOD_FRACTION",
        "G_ACMUX_TEXEL0",
        "G_ACMUX_TEXEL1",
        "G_ACMUX_PRIMITIVE",
        "G_ACMUX_SHADE",
        "G_ACMUX_ENVIRONMENT",
        "G_ACMUX_1 or G_ACMUX_PRIM_LOD_FRAC",
        "G_ACMUX_0",
    };
    return tbl[acmux];
}

/* forward decl for the env-gated D75D probe (defined further down) */
static bool d75d_env_active(void);

static void gfx_generate_cc(struct ColorCombiner* comb, const ColorCombinerKey& key) {
    bool is_2cyc = (key.options & (uint64_t)SHADER_OPT_2CYC) != 0;

    uint8_t c[2][2][4] = { { { 0 } } };
    uint64_t shader_id0 = 0;
    uint32_t shader_id1 = key.options;
    uint8_t shader_input_mapping[2][7] = { { 0 } };
    bool used_textures[2] = { false, false };
    for (int i = 0; i < 2 && (i == 0 || is_2cyc); i++) {
        uint32_t rgb_a = (key.combine_mode >> (i * 28)) & 0xf;
        uint32_t rgb_b = (key.combine_mode >> (i * 28 + 4)) & 0xf;
        uint32_t rgb_c = (key.combine_mode >> (i * 28 + 8)) & 0x1f;
        uint32_t rgb_d = (key.combine_mode >> (i * 28 + 13)) & 7;
        uint32_t alpha_a = (key.combine_mode >> (i * 28 + 16)) & 7;
        uint32_t alpha_b = (key.combine_mode >> (i * 28 + 16 + 3)) & 7;
        uint32_t alpha_c = (key.combine_mode >> (i * 28 + 16 + 6)) & 7;
        uint32_t alpha_d = (key.combine_mode >> (i * 28 + 16 + 9)) & 7;

        if (rgb_a >= 8) {
            rgb_a = G_CCMUX_0;
        }
        if (rgb_b >= 8) {
            rgb_b = G_CCMUX_0;
        }
        if (rgb_c >= 16) {
            rgb_c = G_CCMUX_0;
        }
        if (rgb_d == 7) {
            rgb_d = G_CCMUX_0;
        }

        if (rgb_a == rgb_b || rgb_c == G_CCMUX_0) {
            // Normalize
            rgb_a = G_CCMUX_0;
            rgb_b = G_CCMUX_0;
            rgb_c = G_CCMUX_0;
        }
        if (alpha_a == alpha_b || alpha_c == G_ACMUX_0) {
            // Normalize
            alpha_a = G_ACMUX_0;
            alpha_b = G_ACMUX_0;
            alpha_c = G_ACMUX_0;
        }
        if (i == 1) {
            if (rgb_a != G_CCMUX_COMBINED && rgb_b != G_CCMUX_COMBINED && rgb_c != G_CCMUX_COMBINED &&
                rgb_d != G_CCMUX_COMBINED) {
                // First cycle RGB not used, so clear it away
                c[0][0][0] = c[0][0][1] = c[0][0][2] = c[0][0][3] = G_CCMUX_0;
            }
            if (rgb_c != G_CCMUX_COMBINED_ALPHA && alpha_a != G_ACMUX_COMBINED && alpha_b != G_ACMUX_COMBINED &&
                alpha_d != G_ACMUX_COMBINED) {
                // First cycle ALPHA not used, so clear it away
                c[0][1][0] = c[0][1][1] = c[0][1][2] = c[0][1][3] = G_ACMUX_0;
            }
        }

        c[i][0][0] = rgb_a;
        c[i][0][1] = rgb_b;
        c[i][0][2] = rgb_c;
        c[i][0][3] = rgb_d;
        c[i][1][0] = alpha_a;
        c[i][1][1] = alpha_b;
        c[i][1][2] = alpha_c;
        c[i][1][3] = alpha_d;
    }
    if (!is_2cyc) {
        for (int i = 0; i < 2; i++) {
            for (int k = 0; k < 4; k++) {
                c[1][i][k] = i == 0 ? G_CCMUX_0 : G_ACMUX_0;
            }
        }
    }
    {
        uint8_t input_number[32] = { 0 };
        int next_input_number = SHADER_INPUT_1;
        for (int i = 0; i < 2 && (i == 0 || is_2cyc); i++) {
            for (int j = 0; j < 4; j++) {
                uint32_t val = 0;
                switch (c[i][0][j]) {
                    case G_CCMUX_0:
                        val = SHADER_0;
                        break;
                    case G_CCMUX_1:
                        val = SHADER_1;
                        break;
                    case G_CCMUX_TEXEL0:
                        val = SHADER_TEXEL0;
                        used_textures[0] = true;
                        break;
                    case G_CCMUX_TEXEL1:
                        val = SHADER_TEXEL1;
                        used_textures[1] = true;
                        break;
                    case G_CCMUX_TEXEL0_ALPHA:
                        val = SHADER_TEXEL0A;
                        used_textures[0] = true;
                        break;
                    case G_CCMUX_TEXEL1_ALPHA:
                        val = SHADER_TEXEL1A;
                        used_textures[1] = true;
                        break;
                    case G_CCMUX_NOISE:
                        val = SHADER_NOISE;
                        break;
                    case G_CCMUX_PRIMITIVE:
                    case G_CCMUX_PRIMITIVE_ALPHA:
                    case G_CCMUX_PRIM_LOD_FRAC:
                    case G_CCMUX_SHADE:
                    case G_CCMUX_SHADE_ALPHA:
                    case G_CCMUX_ENVIRONMENT:
                    case G_CCMUX_ENV_ALPHA:
                    case G_CCMUX_LOD_FRACTION:
                        if (input_number[c[i][0][j]] == 0) {
                            shader_input_mapping[0][next_input_number - 1] = c[i][0][j];
                            input_number[c[i][0][j]] = next_input_number++;
                        }
                        val = input_number[c[i][0][j]];
                        break;
                    case G_CCMUX_COMBINED:
                        val = SHADER_COMBINED;
                        break;
                    default:
                        sysLogPrintf(LOG_WARNING, "Unsupported ccmux: %d", c[i][0][j]);
                        break;
                }
                shader_id0 |= (uint64_t)val << (i * 32 + j * 4);
            }
        }
    }
    /* TEMP D75D: one-shot decode of the logo model's combine mode (env-gated via d75d_lo) */
    {
        static int d75d_cc_once = 0;
        if (!d75d_cc_once && d75d_env_active() && key.combine_mode == 0x009ffe4f19ffe4f1ULL) {
            d75d_cc_once = 1;
            fprintf(stderr, "D75DCC: comb=0x%016llx is_2cyc=%d c0=(rgb a=%u b=%u c=%u d=%u)(al a=%u b=%u c=%u d=%u) utex0=%d\n",
                (unsigned long long)key.combine_mode, (int)is_2cyc,
                c[0][0][0], c[0][0][1], c[0][0][2], c[0][0][3],
                c[0][1][0], c[0][1][1], c[0][1][2], c[0][1][3], (int)used_textures[0]);
        }
    }
    {
        uint8_t input_number[16] = { 0 };
        int next_input_number = SHADER_INPUT_1;
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 4; j++) {
                uint32_t val = 0;
                switch (c[i][1][j]) {
                    case G_ACMUX_0:
                        val = SHADER_0;
                        break;
                    case G_ACMUX_TEXEL0:
                        val = SHADER_TEXEL0;
                        used_textures[0] = true;
                        break;
                    case G_ACMUX_TEXEL1:
                        val = SHADER_TEXEL1;
                        used_textures[1] = true;
                        break;
                    case G_ACMUX_LOD_FRACTION:
                        // case G_ACMUX_COMBINED: same numerical value
                        if (j != 2) {
                            val = SHADER_COMBINED;
                            break;
                        }
                        c[i][1][j] = G_CCMUX_LOD_FRACTION;
                        [[fallthrough]]; // for G_ACMUX_LOD_FRACTION
                    case G_ACMUX_1:
                        // case G_ACMUX_PRIM_LOD_FRAC: same numerical value
                        if (j != 2) {
                            val = SHADER_1;
                            break;
                        }
                        [[fallthrough]]; // for G_ACMUX_PRIM_LOD_FRAC
                    case G_ACMUX_PRIMITIVE:
                    case G_ACMUX_SHADE:
                    case G_ACMUX_ENVIRONMENT:
                        if (input_number[c[i][1][j]] == 0) {
                            shader_input_mapping[1][next_input_number - 1] = c[i][1][j];
                            input_number[c[i][1][j]] = next_input_number++;
                        }
                        val = input_number[c[i][1][j]];
                        break;
                }
                shader_id0 |= (uint64_t)val << (i * 32 + 16 + j * 4);
            }
        }
    }
    comb->shader_id0 = shader_id0;
    comb->shader_id1 = shader_id1;
    comb->used_textures[0] = used_textures[0];
    comb->used_textures[1] = used_textures[1];
    // comb->prg = gfx_lookup_or_create_shader_program(shader_id0, shader_id1);
    memcpy(comb->shader_input_mapping, shader_input_mapping, sizeof(shader_input_mapping));
}

static struct ColorCombiner* gfx_lookup_or_create_color_combiner(const ColorCombinerKey& key) {
    if (prev_combiner != color_combiner_pool.end() && prev_combiner->first == key) {
        return &prev_combiner->second;
    }

    prev_combiner = color_combiner_pool.find(key);
    if (prev_combiner != color_combiner_pool.end()) {
        return &prev_combiner->second;
    }
    gfx_flush();
    prev_combiner = color_combiner_pool.insert(std::make_pair(key, ColorCombiner())).first;
    gfx_generate_cc(&prev_combiner->second, key);
    return &prev_combiner->second;
}

void gfx_texture_cache_clear() {
    gfx_flush();
    for (const auto& entry : gfx_texture_cache.map) {
        gfx_texture_cache.free_texture_ids.push_back(entry.second.texture_id);
    }
    gfx_texture_cache.map.clear();
    gfx_texture_cache.lru.clear();
    rdp.textures_changed[0] = rdp.textures_changed[1] = true;
    memset(rendering_state.textures, 0, sizeof(rendering_state.textures));
}

/* D235: entry count for the stage-transition probe (boss.c). */
extern "C" int gfx_texture_cache_count(void) {
    return (int)gfx_texture_cache.map.size();
}

extern "C" u8 *g_VtxBuffers[3]; /* dyn.c per-frame pool (dynamic-texture cache key) */

static bool gfx_texture_cache_lookup(int i, const TextureCacheKey& key) {
    TextureCacheMap::iterator it = gfx_texture_cache.map.find(key);
    TextureCacheNode** n = &rendering_state.textures[i];

    if (it != gfx_texture_cache.map.end()) {
        gfx_rapi->select_texture(i, it->second.texture_id, it->second.linear_filter);
        *n = &*it;
        gfx_texture_cache.lru.splice(gfx_texture_cache.lru.end(), gfx_texture_cache.lru,
                                     it->second.lru_location); // move to back
        return true;
    }

    if (gfx_texture_cache.map.size() >= TEXTURE_CACHE_MAX_SIZE) {
        // Remove the texture that was least recently used
        it = gfx_texture_cache.lru.front().it;
        gfx_texture_cache.free_texture_ids.push_back(it->second.texture_id);
        gfx_texture_cache.map.erase(it);
        gfx_texture_cache.lru.pop_front();
    }

    uint32_t texture_id;
    if (!gfx_texture_cache.free_texture_ids.empty()) {
        texture_id = gfx_texture_cache.free_texture_ids.back();
        gfx_texture_cache.free_texture_ids.pop_back();
    } else {
        texture_id = gfx_rapi->new_texture();
    }

    it = gfx_texture_cache.map.insert(std::make_pair(key, TextureCacheValue())).first;
    TextureCacheNode* node = &*it;
    node->second.texture_id = texture_id;
    node->second.lru_location = gfx_texture_cache.lru.insert(gfx_texture_cache.lru.end(), { it });

    gfx_rapi->select_texture(i, texture_id, false);
    gfx_rapi->set_sampler_parameters(i, false, 0, 0, rdp.tex_lod);
    *n = node;
    return false;
}

void gfx_texture_cache_delete(const uint8_t* orig_addr) {
    gfx_flush();

    for (int i = 0; i < 2; ++i) {
        if (rendering_state.textures[i] && rendering_state.textures[i]->first.texture_addr == orig_addr) {
            rdp.textures_changed[i] = true;
            rendering_state.textures[i] = nullptr;
        }
    }

    while (gfx_texture_cache.map.bucket_count() > 0) {
        TextureCacheKey key = { orig_addr, { 0 }, 0, 0, 0 }; // bucket index only depends on the address
        size_t bucket = gfx_texture_cache.map.bucket(key);
        bool again = false;
        for (auto it = gfx_texture_cache.map.begin(bucket); it != gfx_texture_cache.map.end(bucket); ++it) {
            if (it->first.texture_addr == orig_addr) {
                gfx_texture_cache.lru.erase(it->second.lru_location);
                gfx_texture_cache.free_texture_ids.push_back(it->second.texture_id);
                gfx_texture_cache.map.erase(it->first);
                again = true;
                break;
            }
        }
        if (!again) {
            break;
        }
    }
}

void gfx_texture_cache_delete_range(const uint8_t* start, const uint8_t* end) {
    gfx_flush();

    for (int i = 0; i < 2; ++i) {
        if (rendering_state.textures[i]
                && rendering_state.textures[i]->first.texture_addr >= start
                && rendering_state.textures[i]->first.texture_addr < end) {
            rdp.textures_changed[i] = true;
            rendering_state.textures[i] = nullptr;
        }
    }

    for (auto it = gfx_texture_cache.map.begin(); it != gfx_texture_cache.map.end(); ) {
        if (it->first.texture_addr >= start && it->first.texture_addr < end) {
            gfx_texture_cache.lru.erase(it->second.lru_location);
            gfx_texture_cache.free_texture_ids.push_back(it->second.texture_id);
            it = gfx_texture_cache.map.erase(it);
        } else {
            ++it;
        }
    }
}

// D71 (docs/internals.md): texture sources arrive in two byte conventions.
// Raw N64 big-endian byte streams: ROM cart map (0x10xxxxxx), model-sidecar
// blobs (cart extension 0x10Cxxxxx), KSEG0 mirror (0x80xxxxxx) and V1
// dram/BSS/heap buffers (0x70xxxxxx, e.g. tex.c texture pool, rle_expand_8bit
// output). C-compiled u32 arrays in the exe image (.data/.rodata,
// 0x140xxxxxx on MinGW x64) instead store each N64 texel pair as a
// little-endian u32 — e.g. the rarewarelogo.c RGBA16 images — so the N64 byte
// order is recovered by bswap32 of every u32. Without this, the logo's gold
// texels (0xED0F...) decode from the swapped pairs (0x4FCC/0xCC4F) as bright
// green/pink — the garbled Rareware-logo pixels.
static bool gfx_tex_source_is_c_array(const uint8_t* addr) {
    const uintptr_t a = (uintptr_t)addr;
    if (a >= 0x10000000u && a < 0x20000000u) return false; // cart map + sidecar
    if (a >= 0x70000000u && a < 0x90000000u) return false; // V1 dram + KSEG0 mirror
    return true; // exe image: C-compiled array
}

static std::map<const uint8_t*, std::vector<uint8_t> > s_c_array_tex_norms;

// Returns a pointer to the source in N64 byte order (the original pointer for
// raw-stream sources, a stable per-source bswapped copy for C arrays).
// extent is the full image size incl. padded rows (ci8 reads up to it).
static const uint8_t* gfx_tex_normalize_source(const uint8_t* addr, uint32_t extent) {
    if (!gfx_tex_source_is_c_array(addr)) return addr;
    auto it = s_c_array_tex_norms.find(addr);
    if (it != s_c_array_tex_norms.end()) return it->second.data();

    const uint32_t n = (extent + 3u) & ~3u;
    std::vector<uint8_t> buf(n);
    const uint32_t* src = (const uint32_t*)addr;
    uint32_t* dst = (uint32_t*)buf.data();
    for (uint32_t i = 0; i < n / 4; i++)
        dst[i] = PD_BE32(src[i]);
    {
        static int ge_d71log = -1;
        if (ge_d71log < 0) ge_d71log = getenv("GE_D71LOG") != NULL;
        if (ge_d71log)
            fprintf(stderr, "[D71] normalized C-array texture source %p (%u bytes)\n",
                    (const void*)addr, extent);
    }
    return s_c_array_tex_norms.emplace(addr, std::move(buf)).first->second.data();
}

static void import_texture_rgba16(int tile, const LoadedTexture& loaded_texture, bool gen_mipmaps) {
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    const uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;
    // SUPPORT_CHECK(full_image_line_size_bytes == line_size_bytes);
    // TODO: this trips in some places with a garbage size in full_image_line_size_bytes
    // probably wherever framebuffer effects are used

    uint8_t *dest = tex_upload_buffer;
    for (uint32_t i = 0; i < size_bytes / 2; i++, dest += 4) {
        const uint16_t col16 = (addr[2 * i] << 8) | addr[2 * i + 1];
        const uint8_t a = col16 & 1;
        const uint8_t r = col16 >> 11;
        const uint8_t g = (col16 >> 6) & 0x1f;
        const uint8_t b = (col16 >> 1) & 0x1f;
        dest[0] = SCALE_5_8(r);
        dest[1] = SCALE_5_8(g);
        dest[2] = SCALE_5_8(b);
        dest[3] = a ? 255 : 0;
    }

    const uint32_t width = rdp.texture_tile[tile].line_size_bytes / 2;
    const uint32_t height = size_bytes / rdp.texture_tile[tile].line_size_bytes;

#ifdef PORT
    /* D252 diag (temporary): compare the line_size_bytes-derived width/height
     * against the SETTILESIZE-derived rdp.texture_tile[tile].width/height, and
     * dump the raw texel data once per distinct (addr,size) pair so a fire
     * tile can be checked against the source bytes by hand. Remove once
     * D252 is resolved. */
    static int s_d252 = -1;
    if (s_d252 < 0) s_d252 = getenv("GE_D252") != NULL;
    if (s_d252) {
        static std::set<std::pair<const void*, uint32_t>> s_d252_seen;
        if (s_d252_seen.emplace((const void*)addr, size_bytes).second) {
            const auto& t = rdp.texture_tile[tile];
            fprintf(stderr,
                "[D252] tile=%d addr=%p size_bytes=%u line_size_bytes=%u "
                "computed(w=%u h=%u) settilesize(w=%u h=%u) uls=%u ult=%u lrs=%u lrt=%u "
                "masks=%u maskt=%u tmem=%u\n",
                tile, (const void*)addr, size_bytes, line_size_bytes, width, height,
                t.width, t.height, t.uls, t.ult, t.lrs, t.lrt, t.masks, t.maskt, t.tmem);
            char rawpath[256];
            snprintf(rawpath, sizeof(rawpath), "scratch/d252_raw_%p_%u.bin", (const void*)addr, size_bytes);
            FILE* rf = fopen(rawpath, "wb");
            if (rf) { fwrite(addr, 1, size_bytes, rf); fclose(rf); }
        }
    }
#endif

	gfx_rapi->upload_texture(tex_upload_buffer, width, height, gen_mipmaps);
    // DumpTexture(loaded_texture.otr_path, rgba32_buf, width, height);
}

static void import_texture_rgba32(int tile, const LoadedTexture& loaded_texture, bool gen_mipmaps) {
    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    const uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;
    SUPPORT_CHECK(full_image_line_size_bytes == line_size_bytes);

    uint32_t *dest = (uint32_t *)tex_upload_buffer;
    const uint32_t *src = (const uint32_t *)addr;
    for (uint32_t i = 0; i < size_bytes; i += 4, ++dest, ++src) {
        *dest = PD_BE32(*src);
    }

    const uint32_t width = rdp.texture_tile[tile].line_size_bytes / 2;
    const uint32_t height = (size_bytes / 2) / rdp.texture_tile[tile].line_size_bytes;
	gfx_rapi->upload_texture(tex_upload_buffer, width, height, gen_mipmaps);
    // DumpTexture(loaded_texture.otr_path, addr, width, height);
}

static void import_texture_ia4(int tile, const LoadedTexture& loaded_texture, bool gen_mipmaps) {
    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    const uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;
    SUPPORT_CHECK(full_image_line_size_bytes == line_size_bytes);

    uint8_t *dest = tex_upload_buffer;
    for (uint32_t i = 0; i < size_bytes * 2; i++, dest += 4) {
        const uint8_t byte = addr[i / 2];
        const uint8_t part = (byte >> (4 - (i % 2) * 4)) & 0xf;
        /* D266: I2:A2 -- the hardware IA4 layout. The old I3:A1 read made the
         * A2=1 canopy pixels fully opaque and the A2=2 edge dither invisible
         * (D236/D265 tree "wall"). */
        const uint8_t intensity = part >> 2;
        const uint8_t alpha = part & 3;
        const uint8_t c = SCALE_2_8(intensity);
        dest[0] = c;
        dest[1] = c;
        dest[2] = c;
        dest[3] = SCALE_2_8(alpha);
    }

    const uint32_t width = rdp.texture_tile[tile].line_size_bytes * 2;
    const uint32_t height = size_bytes / rdp.texture_tile[tile].line_size_bytes;

	gfx_rapi->upload_texture(tex_upload_buffer, width, height, gen_mipmaps);
    // DumpTexture(loaded_texture.otr_path, rgba32_buf, width, height);
}

static void import_texture_ia8(int tile, const LoadedTexture& loaded_texture, bool gen_mipmaps) {
    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    const uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;
    SUPPORT_CHECK(full_image_line_size_bytes == line_size_bytes);

    uint8_t *dest = tex_upload_buffer;
    for (uint32_t i = 0; i < size_bytes; i++, dest += 4) {
        const uint8_t intensity = SCALE_4_8(addr[i] >> 4);
        const uint8_t alpha = SCALE_4_8(addr[i] & 0xf);
        dest[0] = intensity;
        dest[1] = intensity;
        dest[2] = intensity;
        dest[3] = alpha;
    }

    const uint32_t width = rdp.texture_tile[tile].line_size_bytes;
    const uint32_t height = size_bytes / rdp.texture_tile[tile].line_size_bytes;

	gfx_rapi->upload_texture(tex_upload_buffer, width, height, gen_mipmaps);
    // DumpTexture(loaded_texture.otr_path, rgba32_buf, width, height);
}

static void import_texture_ia16(int tile, const LoadedTexture& loaded_texture, bool gen_mipmaps) {
    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    const uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;
    SUPPORT_CHECK(full_image_line_size_bytes == line_size_bytes);

    uint8_t *dest = tex_upload_buffer;
    for (uint32_t i = 0; i < size_bytes / 2; i++, dest += 4) {
        const uint8_t intensity = addr[2 * i];
        const uint8_t alpha = addr[2 * i + 1];
        dest[0] = intensity;
        dest[1] = intensity;
        dest[2] = intensity;
        dest[3] = alpha;
    }

    const uint32_t width = rdp.texture_tile[tile].line_size_bytes / 2;
    const uint32_t height = size_bytes / rdp.texture_tile[tile].line_size_bytes;

	gfx_rapi->upload_texture(tex_upload_buffer, width, height, gen_mipmaps);
    // DumpTexture(loaded_texture.otr_path, rgba32_buf, width, height);
}

static void import_texture_i4(int tile, const LoadedTexture& loaded_texture, bool gen_mipmaps) {
    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    const uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;
    SUPPORT_CHECK(full_image_line_size_bytes == line_size_bytes);

    uint8_t *dest = tex_upload_buffer;
    for (uint32_t i = 0; i < size_bytes * 2; i++, dest += 4) {
        const uint8_t byte = addr[i / 2];
        const uint8_t part = (byte >> (4 - (i % 2) * 4)) & 0xf;
        const uint8_t intensity = SCALE_4_8(part);
        dest[0] = intensity;
        dest[1] = intensity;
        dest[2] = intensity;
        dest[3] = intensity;
    }

    const uint32_t width = rdp.texture_tile[tile].line_size_bytes * 2;
    const uint32_t height = size_bytes / rdp.texture_tile[tile].line_size_bytes;

	gfx_rapi->upload_texture(tex_upload_buffer, width, height, gen_mipmaps);
    // DumpTexture(loaded_texture.otr_path, rgba32_buf, width, height);
}

static void import_texture_i8(int tile, const LoadedTexture& loaded_texture, bool gen_mipmaps) {
    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;
    SUPPORT_CHECK(full_image_line_size_bytes == line_size_bytes);

    uint8_t *dest = tex_upload_buffer;
    for (uint32_t i = 0; i < size_bytes; i++, dest += 4) {
        const uint8_t intensity = addr[i];
        dest[0] = intensity;
        dest[1] = intensity;
        dest[2] = intensity;
        dest[3] = intensity;
    }

    const uint32_t width = rdp.texture_tile[tile].line_size_bytes;
    const uint32_t height = size_bytes / rdp.texture_tile[tile].line_size_bytes;

	gfx_rapi->upload_texture(tex_upload_buffer, width, height, gen_mipmaps);
    // DumpTexture(loaded_texture.otr_path, rgba32_buf, width, height);
}

static inline void palette_to_rgba32(const uint16_t palentry, uint8_t *rgba32_buf) {
    if (rdp.palette_fmt == G_TT_IA16) {
        /* D228: intensity/alpha were swapped here relative to the (correct)
         * direct IA16 decode in import_texture_ia16() a few lines above --
         * that one reads addr[2*i]=intensity, addr[2*i+1]=alpha, i.e.
         * intensity is the first (high, after gfx_dp_load_tlut's PD_BE16
         * swap) byte. This path had them backwards: a dim, fully-opaque
         * palette entry (e.g. intensity=0x58, alpha=0xff) decoded as a
         * bright, mostly-transparent one (intensity=0xff, alpha=0x58) --
         * the "white missing-texture patches" on AK47/NPC weapon models. */
        const uint8_t intensity = palentry >> 8;
        const uint8_t alpha = palentry & 0xff;
        rgba32_buf[0] = intensity;
        rgba32_buf[1] = intensity;
        rgba32_buf[2] = intensity;
        rgba32_buf[3] = alpha;
    } else {
        // assume G_TT_RGBA16
        const uint8_t a = palentry & 1;
        const uint8_t r = palentry >> 11;
        const uint8_t g = (palentry >> 6) & 0x1f;
        const uint8_t b = (palentry >> 1) & 0x1f;
        rgba32_buf[0] = SCALE_5_8(r);
        rgba32_buf[1] = SCALE_5_8(g);
        rgba32_buf[2] = SCALE_5_8(b);
        rgba32_buf[3] = a ? 255 : 0;
    }
}

static void import_texture_ci4(int tile, const LoadedTexture& loaded_texture, bool gen_mipmaps) {
	const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    const uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;
    const uint32_t pal_idx = rdp.texture_tile[tile].palette; // 0-15
    const uint16_t* palette = (const uint16_t *)(rdp.palette + pal_idx * 16); // 16 pixel entries, 16 bits each
    SUPPORT_CHECK(full_image_line_size_bytes == line_size_bytes);

    for (uint32_t i = 0; i < size_bytes * 2; i++) {
        const uint8_t byte = addr[i / 2];
        const uint8_t idx = (byte >> (4 - (i % 2) * 4)) & 0xf;
        palette_to_rgba32(palette[idx], tex_upload_buffer +4 * i);
    }

    uint32_t result_line_size = rdp.texture_tile[tile].line_size_bytes;
    if (metadata->h_byte_scale != 1) {
        result_line_size *= metadata->h_byte_scale;
    }

    const uint32_t width = result_line_size * 2;
    const uint32_t height = size_bytes / result_line_size;

	gfx_rapi->upload_texture(tex_upload_buffer, width, height, gen_mipmaps);
}

static void import_texture_ci8(int tile, const LoadedTexture& loaded_texture, bool gen_mipmaps) {
	const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    const uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;

    for (uint32_t i = 0, j = 0; i < size_bytes; j += full_image_line_size_bytes - line_size_bytes) {
        for (uint32_t k = 0; k < line_size_bytes; i++, k++, j++) {
            const uint8_t idx = addr[j];
            palette_to_rgba32(rdp.palette[idx], tex_upload_buffer + 4 * i);
        }
    }

    uint32_t result_line_size = rdp.texture_tile[tile].line_size_bytes;
    if (metadata->h_byte_scale != 1) {
        result_line_size *= metadata->h_byte_scale;
    }

    const uint32_t width = result_line_size;
    const uint32_t height = size_bytes / result_line_size;

	gfx_rapi->upload_texture(tex_upload_buffer, width, height, gen_mipmaps);
}

/* RC2 mip-contamination clamp (see import_texture). Default on;
 * Video.FixMipTextures = 0 restores the raw over-tall upload. */
bool g_fix_mip_textures = true;

/* D74 sub-tile UV pre-wrap (see gfx_sp_tri). Opt-in (never-run path); default
 * off. Video.WrapFix = 1. */
bool g_wrap_fix = false;

/* D236 pass 26: for a GE TEXTURETYPE_DETAIL binding, sample the BASE image
 * (tile 1) rather than the detail texture sitting at TMEM 0. See
 * gfx_lod_tile_offset. Video.DetailBaseTile. */
bool g_detail_base_tile = false;

/* D183 source-pitch de-stride (see import_texture). Default on;
 * GE_TEXPITCH=0 restores the old flat read for A/B. */
static bool gfx_tex_pitch_fix(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* e = getenv("GE_TEXPITCH");
        cached = (e && e[0] == '0') ? 0 : 1;
    }
    return cached != 0;
}

static void import_texture(int i, int tile, bool importReplacement) {
    LoadedTexture& loaded_texture = rdp.loaded_texture[rdp.texture_tile[tile].tmem];
    const uint8_t fmt = rdp.texture_tile[tile].fmt;
    const uint8_t siz = rdp.texture_tile[tile].siz;
    const uint32_t tex_flags = loaded_texture.tex_flags;
    const uint8_t palette_index = rdp.texture_tile[tile].palette;

    // D74: only fall back when the tmem slot was never written by a load
    // command. The old `rdp.tex_lod && tile >= first+detail` branch also
    // overwrote valid gDPLoadBlock data with line*tile.height, which (a) dropped
    // mip chains and (b) truncated sub-tiled textures to the sub-tile's row
    // count (e.g. the Rare-logo D_02005FF0 20x3 tile uploaded as 32x3).
    if (!loaded_texture.addr) {
        // set up miplevel 0; also acts as a catch-all for when .addr is NULL because my texture loader sucks
        loaded_texture.addr = rdp.texture_to_load.addr;
        loaded_texture.line_size_bytes = rdp.texture_tile[tile].line_size_bytes;
        loaded_texture.full_image_line_size_bytes = rdp.texture_tile[tile].line_size_bytes;
        loaded_texture.full_size_bytes = loaded_texture.full_image_line_size_bytes * rdp.texture_tile[tile].height;
        loaded_texture.size_bytes = loaded_texture.line_size_bytes * rdp.texture_tile[tile].height;
        if (siz == G_IM_SIZ_32b) {
            // HACK: fixup 32-bit LODed texture height
            loaded_texture.size_bytes <<= 1;
            loaded_texture.full_size_bytes <<= 1;
        }
        loaded_texture.orig_size_bytes = loaded_texture.size_bytes;
    }

    /* RC2 (docs/dev/TEXTURE-GLITCH-ANALYSIS.md): GE's texGetDepthAndSize() sums the
     * base level + every LOD mip into one gDPLoadBlock, so the block byte count
     * (-> upload height = size_bytes / row) runs ~1.3x taller than the base
     * image and the mip bytes render as garbage rows below it ("interlaced"
     * textures on the menu / Depot). Only when LOD is active (rdp.tex_lod) AND
     * the load is a plain full-width block (not a windowed gfx_dp_load_tile,
     * which legitimately has extra rows -- the D74 sub-tile / Rare-logo case):
     * clip to the SETTILESIZE base-tile height. GL then builds correct mips
     * from a correct base image. */
    if (g_fix_mip_textures && rdp.tex_lod &&
        loaded_texture.line_size_bytes == loaded_texture.full_image_line_size_bytes) {
        const uint32_t row = rdp.texture_tile[tile].line_size_bytes;
        const uint32_t tile_h =
            ((uint32_t)(rdp.texture_tile[tile].lrt - rdp.texture_tile[tile].ult) >> 2) + 1;
        if (row && tile_h > 1) {
            uint32_t base_bytes = row * tile_h;
            if (siz == G_IM_SIZ_32b)
                base_bytes <<= 1; /* mirrors the 32b height fixup above */
            if (base_bytes < loaded_texture.size_bytes) {
                loaded_texture.size_bytes = base_bytes;
                if (loaded_texture.full_size_bytes > base_bytes)
                    loaded_texture.full_size_bytes = base_bytes;
                loaded_texture.orig_size_bytes = base_bytes;
            }
        }
    }

    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* orig_addr = loaded_texture.addr;
    SUPPORT_CHECK(orig_addr);

    TextureCacheKey key;
    if (fmt == G_IM_FMT_CI) {
        key = { orig_addr, { rdp.palette_addrs[0], rdp.palette_addrs[1] }, fmt, siz, palette_index,
                loaded_texture.size_bytes, rdp.palette_hash }; // D217: key on palette content
    } else {
        key = { orig_addr, {}, fmt, siz, palette_index, loaded_texture.size_bytes, 0u };
    }

    /* Intro blood (M-201): textures the game regenerates IN PLACE in the
     * per-frame dynamic pool (dynAllocate, [g_VtxBuffers[0], g_VtxBuffers[2]))
     * -- e.g. the gun-barrel / death blood-drip image rebuilt by
     * die_blood_image_routine -- reuse the same addresses frame after frame,
     * so an address-only key returns a stale GL texture from an earlier frame.
     * Key those on a content hash (FNV-1a) instead; static textures keep the
     * free address key. GE_DYNTEXHASH_OFF=1 disables for A/B. */
    {
        static int dynhash_off = -1;
        if (dynhash_off < 0) dynhash_off = getenv("GE_DYNTEXHASH_OFF") != NULL;
        /* Game code often reaches this memory through OS_K0_TO_PHYSICAL,
         * which fast3d resolves into the byte-identical KSEG0 mirror at
         * 0x80000000 (port/src/dram.c V2) -- normalise to the V1 view the
         * dyn pool pointers use before the range test. */
        const uint8_t* v1addr = orig_addr;
        if ((uintptr_t)v1addr >= 0x80000000UL && (uintptr_t)v1addr < 0x80800000UL) {
            v1addr -= 0x10000000UL;
        }
        if (!dynhash_off && g_VtxBuffers[0] && v1addr >= g_VtxBuffers[0] && v1addr < g_VtxBuffers[2]) {
            uint32_t h = 2166136261u;
            const uint32_t n = loaded_texture.size_bytes;
            for (uint32_t b = 0; b < n; b++) {
                h = (h ^ orig_addr[b]) * 16777619u;
            }
            key.palette_hash = h ? h : 1u;
        }
    }

    if (gfx_texture_cache_lookup(i, key)) {
        return;
    }

    // D71: importers read raw N64 byte streams; normalize C-array sources.
    const uint8_t* saved_addr = loaded_texture.addr;
    const uint32_t saved_full_line = loaded_texture.full_image_line_size_bytes;
    loaded_texture.addr =
        gfx_tex_normalize_source(orig_addr,
                                 loaded_texture.full_size_bytes > loaded_texture.size_bytes
                                     ? loaded_texture.full_size_bytes
                                     : loaded_texture.size_bytes);

    /* D183: honour the source row pitch. gfx_dp_load_tile can load a
     * sub-rectangle of a wider texture image: successive texel rows then sit
     * full_image_line_size_bytes apart in RAM while only line_size_bytes of
     * each row belong to the tile. Every import_texture_* except the CI8 one
     * reads the source flat (they only assert line == full -- and the asserts
     * compile out in the release build), so row r starts r*(full-line) bytes
     * early -> a progressive diagonal shear that reads as grey static /
     * "comb interlacing" (D176(b) Surface cliff walls, D182(2) file-select
     * spiral). Compact the strided rows into a contiguous scratch buffer once
     * here, so every importer sees line == full and no importer needs to know
     * about pitch. No-op when the load was already row-packed (gDPLoadBlock,
     * and any full-width gDPLoadTile), which is the overwhelming majority --
     * hence golden-safe. */
    std::vector<uint8_t> destride_buf;
    if (gfx_tex_pitch_fix()) {
        const uint32_t src_line = loaded_texture.line_size_bytes;
        const uint32_t src_full = loaded_texture.full_image_line_size_bytes;
        if (src_line && src_full > src_line && loaded_texture.size_bytes > src_line) {
            const uint32_t rows = loaded_texture.size_bytes / src_line;
            destride_buf.resize((size_t)rows * src_line);
            for (uint32_t r = 0; r < rows; r++) {
                memcpy(&destride_buf[(size_t)r * src_line],
                       loaded_texture.addr + (size_t)r * src_full, src_line);
            }
            loaded_texture.addr = destride_buf.data();
            loaded_texture.full_image_line_size_bytes = src_line;
        }
    }

    /* GE_DTEX: dump the load parameters for the first N textures of a frame so
     * RC2 (mip-chain contamination -> over-tall upload) can be told apart from a
     * decode/row-swap bug. tile_h = base-tile height from SETTILESIZE; if the
     * computed upload height is much larger, the excess rows are LOD mip data.
     * docs/dev/TEXTURE-GLITCH-ANALYSIS.md sec 6b.
     * D250: import_texture() runs on every texture bind -- an uncached
     * getenv() here measured at ~50% of the hot render thread's total CPU
     * time on a texture-heavy level (Dam), via a live WPR/xperf profile.
     * Cache like every other env-gated probe in this codebase. */
    static int ge_dtex = -1;
    if (ge_dtex < 0) ge_dtex = getenv("GE_DTEX") != NULL;
    if (ge_dtex) {
        static int dtexCount = 0;
        if (dtexCount < 64) {
            const uint32_t row = rdp.texture_tile[tile].line_size_bytes;
            const uint32_t up_h = row ? (loaded_texture.size_bytes / row) : 0;
            const uint32_t tile_h =
                ((rdp.texture_tile[tile].lrt - rdp.texture_tile[tile].ult) >> 2) + 1;
            const uint32_t tile_w =
                ((rdp.texture_tile[tile].lrs - rdp.texture_tile[tile].uls) >> 2) + 1;
            sysLogPrintf(LOG_NOTE,
                "GE_DTEX[%d] addr=%p fmt=%u siz=%u lod=%d  tile=%ux%u  row=%u "
                "line=%u full=%u size=%u -> upload=%ux%u%s%s",
                dtexCount++, (void *)orig_addr, fmt, siz, (int)rdp.tex_lod,
                tile_w, tile_h, row, loaded_texture.line_size_bytes,
                loaded_texture.full_image_line_size_bytes,
                loaded_texture.size_bytes,
                row ? (row >> (siz ? siz - 1 : 0)) : 0, up_h,
                (up_h > tile_h + 1) ? "  <-- OVER-TALL (mip contamination?)" : "",
                (loaded_texture.full_image_line_size_bytes !=
                 loaded_texture.line_size_bytes) ? "  <-- STRIDED (pitch shear?)" : "");
        }
    }

#ifdef PORT
    /* D157-I (M-158): import census for the D219/D252 rainbow-spark repro.
     * Logs every small-texture (<= 16 KiB) import inside the known-bad frame
     * window of the Bunker1 -level_09 repro, deduped per source address (first
     * 3 hits each). Paired with the D157T vertex probe: the census names the
     * exact source addresses bound during the spark frames (the smoke/flare/
     * scattered family should appear in lockstep with the scripted shots),
     * which the vertex lines can then be grepped by. Remove once root-caused.
     * D250: cached getenv, inert unless GE_D157 is set. */
    static int ge_d157i = -1;
    if (ge_d157i < 0) ge_d157i = getenv("GE_D157") != NULL;
    if (ge_d157i) {
        extern uint32_t num_dls;
        /* 2026-09-18: dropped the old num_dls in [90,240] gate here too (see
         * the matching D157T comment) -- per-address dedup below already
         * bounds this to 3 lines per distinct texture for the whole session. */
        if (loaded_texture.size_bytes <= 16384) {
            static std::map<const void*, int> d157i_seen;
            int& n = d157i_seen[(const void*)orig_addr];
            if (++n <= 3) {
                sysLogPrintf(LOG_NOTE,
                    "D157I: frame=%u hit#%d addr=%p fmt=%u siz=%u size=%u line=%u tile=%u tmem=%u",
                    num_dls, n, (const void*)orig_addr, fmt, siz,
                    loaded_texture.size_bytes, loaded_texture.line_size_bytes,
                    tile, rdp.texture_tile[tile].tmem);
            }
        }
    }
#endif

    static int ge_texdump = -1;
    if (ge_texdump < 0) ge_texdump = getenv("GE_TEXDUMP") != NULL;
    if (ge_texdump) {
        static int tdc = 0;
        const uint16_t* pal = (const uint16_t*)rdp.palette;
        sysLogPrintf(LOG_NOTE,
            "GE_TEXI[%d] addr=%p fmt=%u siz=%u palfmt=%u palidx=%u size=%u "
            "tile=%dx%d pal[0..3]=%04x %04x %04x %04x", tdc++, (void*)orig_addr,
            fmt, siz, rdp.palette_fmt, palette_index, loaded_texture.size_bytes,
            ((rdp.texture_tile[tile].lrs - rdp.texture_tile[tile].uls) >> 2) + 1,
            ((rdp.texture_tile[tile].lrt - rdp.texture_tile[tile].ult) >> 2) + 1,
            pal[0], pal[1], pal[2], pal[3]);
        /* GE_TEXRAW=1 additionally writes the raw source bytes handed to the
         * importer (texdump/rNNN_f<fmt>_s<siz>_<w>x<h>.bin) -- lets a decode
         * bug be told apart from a source-data bug offline (D183). */
        const int tw = (int)(((rdp.texture_tile[tile].lrs - rdp.texture_tile[tile].uls) >> 2) + 1);
        const int th = (int)(((rdp.texture_tile[tile].lrt - rdp.texture_tile[tile].ult) >> 2) + 1);
        /* D219: same cap-exhausted-before-the-explosion problem as the
         * D172/GE_TEXDUMP probes -- never suppress the fire particle image
         * dump. NOTE: `tw`/`th` above come from gsDPSetTileSize (the DL sets
         * the fire tile's logical wrap size to 56x56, unrelated to its real
         * 16x14 pixel content), so they do NOT identify this texture -- a
         * first attempt at this exemption keyed on tw/th==16/14 and silently
         * never matched, burning a whole live-playtest cycle for nothing.
         * The real, always-correct signature is the load's byte count: 16 *
         * 14 * 2 bytes/texel (RGBA16) = 448, matching CALC_LRS(16,14,...) in
         * every one of assets/oddtextures.c's 15 fire DLs and confirmed
         * against the TEXEL1_bytes=448 field already proven out via the
         * gfx_sp_tri1 D172/D219 probe. */
        const bool is_fire_bytes = (loaded_texture.size_bytes == 448);
        static int ge_texraw = -1;
        if (ge_texraw < 0) ge_texraw = getenv("GE_TEXRAW") != NULL;
        if ((is_fire_bytes || tdc <= 400) && ge_texraw) {
            char nm[160];
            snprintf(nm, sizeof nm, "texdump/r%03d_f%u_s%u_%ux%u.bin", tdc - 1, fmt, siz, tw, th);
            FILE* bf = fopen(nm, "wb");
            if (bf) { fwrite(loaded_texture.addr, 1, loaded_texture.size_bytes, bf); fclose(bf); }
        }
        /* D75 round 3: GE_TEXDUMP_ADDR=<hex> -- dump the raw source bytes of
         * EVERY import from that address (bypassing the 400-cap) with byte
         * stats, so a late re-import into a scratch-arena alias can be told
         * apart from a never-loaded buffer. */
        static uint64_t ge_texdump_addr = 0;
        static int ge_texdump_addr_init = 0;
        if (!ge_texdump_addr_init) {
            ge_texdump_addr_init = 1;
            const char* ea = getenv("GE_TEXDUMP_ADDR");
            if (ea && *ea) ge_texdump_addr = strtoull(ea, NULL, 16);
        }
        if (ge_texdump_addr && (uint64_t)(uintptr_t)orig_addr == ge_texdump_addr) {
            unsigned mn = 255, mx = 0, nz = 0;
            for (uint32_t i = 0; i < loaded_texture.size_bytes; i++) {
                uint8_t b = loaded_texture.addr[i];
                if (b < mn) mn = b; if (b > mx) mx = b; if (b) nz++;
            }
            char nm2[160];
            snprintf(nm2, sizeof nm2, "texdump/a%05d_%ux%u.bin", tdc - 1, tw, th);
            FILE* bf2 = fopen(nm2, "wb");
            if (bf2) { fwrite(loaded_texture.addr, 1, loaded_texture.size_bytes, bf2); fclose(bf2); }
            extern uint32_t num_dls; // same counter the D157I probe uses
            sysLogPrintf(LOG_NOTE,
                "GE_TEXA[%d] dls=%u addr=%p fmt=%u siz=%u size=%u min=%u max=%u nonzero=%u/%u file=%s",
                tdc - 1, num_dls, (void*)orig_addr, fmt, siz,
                loaded_texture.size_bytes, mn, mx, nz, loaded_texture.size_bytes, nm2);
        }
    }

    /* D161: a CI-format tile drawn with the TLUT disabled (G_TT_NONE) must NOT
     * do a palette lookup -- the N64 RDP feeds the raw TMEM texel straight into
     * the colour pipe, i.e. it behaves as a plain intensity (I) texture. GE's
     * Depot ceiling emits exactly this (CI8 + gsDPSetTextureLUT(G_TT_NONE));
     * decoding it against the stale rdp.palette produced the blue-speckle roof
     * (docs/dev/TEXTURE-GLITCH-ANALYSIS.md, B2). Route CI4/CI8 -> I4/I8 here. */
    uint8_t fmt_eff = fmt;
    uint8_t siz_eff = siz;
    if (fmt == G_IM_FMT_CI && rdp.palette_fmt == G_TT_NONE) {
        fmt_eff = G_IM_FMT_I;
    }

    /* D229: green/pulsating sky water. GE's texSelect loads CI8 mipmap chains
     * with gDPLoadBlock (SetTexImage format = CI; the 16b "size" is only the
     * fast3d 4KB-per-block convention), then the sky-water draw re-declares
     * the same TMEM slot as RGBA/16b (sub_GAME_7F09343C). On N64 GE's custom
     * RSP ucode expands the indices through the TLUT into real 16-bit pixels
     * in TMEM, so the RGBA16 tile samples blue water. fast3d has no such
     * expansion: it uploads the raw index bytes as 16-bit texels (g = 2*idx
     * mod 32 dominates) -> green mottle. Route the import through the CI8
     * palette path instead -- the port-layer equivalent of the ucode's
     * expand-at-load. Only fires when the tile format genuinely disagrees
     * with the loaded source, so real RGBA16 textures are untouched. */
    if (fmt_eff == G_IM_FMT_RGBA && siz == G_IM_SIZ_16b &&
        loaded_texture.src_fmt == G_IM_FMT_CI) {
#ifdef PORT
        static int ge_d229_a = -1;
        if (ge_d229_a < 0) ge_d229_a = getenv("GE_D229") != NULL;
        if (ge_d229_a) {
            static int n_ci8r = 0;
            if (n_ci8r++ < 8)
                sysLogPrintf(LOG_NOTE, "D229: RGBA16 tile over CI8 source -> ci8 import (addr=%p size=%u palidx=%u)",
                             (const void *)orig_addr, loaded_texture.size_bytes, palette_index);
        }
#endif
        fmt_eff = G_IM_FMT_CI;
        siz_eff = G_IM_SIZ_8b;
    }

    if (fmt_eff == G_IM_FMT_RGBA) {
        if (siz_eff == G_IM_SIZ_16b) {
            import_texture_rgba16(tile, loaded_texture, rdp.tex_lod);
        } else if (siz_eff == G_IM_SIZ_32b) {
            import_texture_rgba32(tile, loaded_texture, rdp.tex_lod);
        } else {
            sysFatalError("Bad size for RGBA texture in tile %d: %02x", tile, siz);
        }
    } else if (fmt_eff == G_IM_FMT_IA) {
        if (siz_eff == G_IM_SIZ_4b) {
            import_texture_ia4(tile, loaded_texture, rdp.tex_lod);
        } else if (siz_eff == G_IM_SIZ_8b) {
            import_texture_ia8(tile, loaded_texture, rdp.tex_lod);
        } else if (siz_eff == G_IM_SIZ_16b) {
            import_texture_ia16(tile, loaded_texture, rdp.tex_lod);
        } else {
            sysFatalError("Bad size for IA texture in tile %d: %02x", tile, siz);
        }
    } else if (fmt_eff == G_IM_FMT_CI) {
        if (siz_eff == G_IM_SIZ_4b) {
            import_texture_ci4(tile, loaded_texture, rdp.tex_lod);
        } else if (siz_eff == G_IM_SIZ_8b) {
            /* D245 (M-201): for the D229 case (RGBA16 tile over a CI8 load --
             * the IsWater sky water), upload only the base level: the load
             * carries the whole mip chain (32x32 base + mips = 1400 B), which
             * made a 32x43 image whose GL REPEAT period (43 rows) differs from
             * the N64's mask period (32). See the tri-path counterpart. */
            static int d245_oldtex = -1;
            if (d245_oldtex < 0) d245_oldtex = getenv("GE_D245_OLDTEX") != NULL;
            const uint8_t maskt = rdp.texture_tile[tile].maskt;
            /* Crop by the TILE line size (the importer's row width): for a
             * LoadBlock, loaded_texture.line_size_bytes is the whole block, so
             * testing against it never cropped (M-201 follow-up: the mip rows
             * then showed as coloured dashes on Frigate's water). */
            const uint32_t d245_row = rdp.texture_tile[tile].line_size_bytes;
            if (!d245_oldtex && fmt == G_IM_FMT_RGBA && siz == G_IM_SIZ_16b && maskt > 0 && maskt < 12 &&
                d245_row > 0 && loaded_texture.size_bytes > d245_row * (1u << maskt)) {
                LoadedTexture lt = loaded_texture;
                lt.size_bytes = d245_row * (1u << maskt);
                import_texture_ci8(tile, lt, rdp.tex_lod);
            } else {
                import_texture_ci8(tile, loaded_texture, rdp.tex_lod);
            }
        } else {
            sysFatalError("Bad size for CI texture in tile %d: %02x", tile, siz);
        }
    } else if (fmt_eff == G_IM_FMT_I) {
        if (siz_eff == G_IM_SIZ_4b) {
            import_texture_i4(tile, loaded_texture, rdp.tex_lod);
        } else if (siz_eff == G_IM_SIZ_8b) {
            import_texture_i8(tile, loaded_texture, rdp.tex_lod);
        } else {
            sysFatalError("Bad size for I texture in tile %d: %02x", tile, siz);
        }
    } else {
        sysFatalError("Bad texture format in tile %d: %02x %02x", tile, fmt, siz);
    }

    loaded_texture.addr = saved_addr;
    loaded_texture.full_image_line_size_bytes = saved_full_line;
}

static void gfx_normalize_vector(float v[3]) {
    float s = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    v[0] /= s;
    v[1] /= s;
    v[2] /= s;
}

static void gfx_transposed_matrix_mul(float res[3], const float a[3], const float b[4][4]) {
    res[0] = a[0] * b[0][0] + a[1] * b[0][1] + a[2] * b[0][2];
    res[1] = a[0] * b[1][0] + a[1] * b[1][1] + a[2] * b[1][2];
    res[2] = a[0] * b[2][0] + a[1] * b[2][1] + a[2] * b[2][2];
}

static void calculate_normal_dir(const Light_t* light, float coeffs[3]) {
    const float light_dir[3] = { light->dir[0] / 127.f, light->dir[1] / 127.f, light->dir[2] / 127.f };

    gfx_transposed_matrix_mul(coeffs, light_dir, rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1]);
    gfx_normalize_vector(coeffs);
}

static void calculate_normal_dir(const struct NormalColor *vcn, float coeffs[3]) {
    const float light_dir[3] = { vcn->x / 127.f, vcn->y / 127.f, vcn->z / 127.f };

    gfx_transposed_matrix_mul(coeffs, light_dir, rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1]);
    gfx_normalize_vector(coeffs);
}

static void gfx_matrix_mul(float res[4][4], const float a[4][4], const float b[4][4]) {
    float tmp[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    memcpy(res, tmp, sizeof(tmp));
}

/* D409: 120 FPS via frame interpolation (port-layer only, opt-in).
 *
 * GE's sim cannot tick faster than the console VI rate: waitForNextFrame()
 * blocks until a whole 1/60 s has elapsed and every timer downstream of it
 * (speedgraphframes -> g_ClockTimer) is an integer count of those ticks, so
 * the game only ever produces 60 (PAL: 50) distinct frames a second. To show
 * more, each game display list is rendered TWICE: first with every G_MTX
 * blended halfway between the previous frame's matrix and this frame's, then
 * as-is. Both passes run the same (intact) DL inside one osSpTaskStartGo, so
 * no game state is touched and nothing extra is kept but the previous
 * frame's decoded float matrices. Costs half a tick (~8 ms) of latency.
 *
 * Matrices are matched frame-to-frame by the first geometry the matrix is
 * used for (the next G_VTX / G_DL / G_FLOATVTX_EXT target -- model and room
 * geometry lives at stable addresses) plus an occurrence count for repeated
 * instances of one model. A matrix with no match, or whose prev/cur values are
 * too far apart to be the same object one tick apart (a camera cut, a draw
 * order swap between two instances), is drawn at its current value. Screen-
 * space geometry the CPU transformed itself does not move on the in-between
 * frame. Off (mode OFF, no per-command cost) unless Video.FpsCap exceeds the
 * VI rate. */
enum { INTERP_OFF = 0, INTERP_RECORD, INTERP_BLEND };
struct InterpMtx { float m[4][4]; };
static int s_interp_mode = INTERP_OFF;
static bool s_interp_enabled = false;
static bool s_interp_have_prev = false;
static uintptr_t s_interp_next_geo = 0;
static uint64_t s_interp_last_run_ns = 0;
static uint64_t s_interp_tick_ns = 16666667;   /* one VI tick */
static uint64_t s_interp_refresh_ns = 0;       /* display refresh period; 0 = unknown */
static bool s_interp_vsync = false;
static double s_interp_pass_ema_ns = 0;        /* smoothed cost of one render pass */
static bool s_interp_too_slow = false;
/* Deferred present of the real frame (see gfx_run). */
static bool s_present_pending = false;
static uint64_t s_present_deadline_ns = 0;
static uint64_t s_pass_render_ns = 0;          /* last pass: start -> just before swap */
static std::unordered_map<uint64_t, InterpMtx> s_interp_prev, s_interp_cur;
static std::unordered_map<uint64_t, uint32_t> s_interp_occ;

static uint64_t gfx_interp_key(uintptr_t geo, bool proj) {
    const uint64_t base = ((uint64_t)geo & 0x0000ffffffffffffULL) | (proj ? 0x8000000000000000ULL : 0);
    const uint32_t occ = s_interp_occ[base]++;
    return base ^ ((uint64_t)(occ & 0x7fff) << 48);
}

/* Could prev and cur be the same transform one sim tick apart? */
static bool gfx_interp_plausible(const float a[4][4], const float b[4][4], bool proj) {
    float d = 0, n = 0;
    if (proj) {
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                d += (a[i][j] - b[i][j]) * (a[i][j] - b[i][j]);
                n += b[i][j] * b[i][j];
            }
        }
        return d <= 0.04f * n;
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            d += (a[i][j] - b[i][j]) * (a[i][j] - b[i][j]);
            n += b[i][j] * b[i][j];
        }
    }
    if (d > 0.25f * n) { /* > ~35 deg of rotation, or a big scale change */
        return false;
    }
    float dt = 0, nt = 0;
    for (int j = 0; j < 3; j++) {
        dt += (a[3][j] - b[3][j]) * (a[3][j] - b[3][j]);
        nt += b[3][j] * b[3][j];
    }
    const float lim = 0.35f * sqrtf(nt) + 16.0f;
    return dt <= lim * lim;
}

static void gfx_interp_matrix(float matrix[4][4], bool proj) {
    const uint64_t key = gfx_interp_key(proj ? 0 : s_interp_next_geo, proj);
    if (s_interp_mode == INTERP_RECORD) {
        memcpy(s_interp_cur[key].m, matrix, sizeof(InterpMtx));
        return;
    }
    auto it = s_interp_prev.find(key);
    if (it == s_interp_prev.end() || !gfx_interp_plausible(it->second.m, matrix, proj)) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            matrix[i][j] = 0.5f * (it->second.m[i][j] + matrix[i][j]);
        }
    }
}

static void gfx_sp_matrix(uint8_t parameters, const int32_t* addr) {
    float matrix[4][4];

    /* D144: a front-end 3D model (MISSION COMPLETE dossier, mode-select
     * wallets - the D75 family) can emit a gSPMatrix whose pointer field was
     * never resolved: w1 comes through as 0xFFFF... or another non-canonical
     * value, and seg_addr() then hands us a wild pointer -> AV reading addr[].
     * A real N64 matrix pointer always lands in mapped memory. Rather than
     * crash the menu transition, substitute identity for an unmapped source
     * (the model draws with the wrong transform - already a parked D75
     * cosmetic - but the screen and its "Next" -> menu path work). */
    const bool addr_bad = ((uintptr_t)addr < 0x10000 ||
                           (uintptr_t)addr >= 0x0000800000000000ULL);

    if (addr_bad) {
        memset(matrix, 0, sizeof(matrix));
        matrix[0][0] = matrix[1][1] = matrix[2][2] = matrix[3][3] = 1.0f;
    } else
#ifndef GBI_FLOATS
    // Original GBI where fixed point matrices are used
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j += 2) {
            int32_t int_part = addr[i * 2 + j / 2];
            uint32_t frac_part = addr[8 + i * 2 + j / 2];
            matrix[i][j] = (int32_t)((int_part & 0xffff0000) | (frac_part >> 16)) / 65536.0f;
            matrix[i][j + 1] = (int32_t)((int_part << 16) | (frac_part & 0xffff)) / 65536.0f;
        }
    }
#else
    // For a modified GBI where fixed point values are replaced with floats
    memcpy(matrix, addr, sizeof(matrix));
#endif

    if (s_interp_mode != INTERP_OFF && !addr_bad) {
        gfx_interp_matrix(matrix, (parameters & G_MTX_PROJECTION) != 0);
    }

    if (parameters & G_MTX_PROJECTION) {
        if (parameters & G_MTX_LOAD) {
            memcpy(rsp.P_matrix, matrix, sizeof(matrix));
        } else {
            gfx_matrix_mul(rsp.P_matrix, matrix, rsp.P_matrix);
        }
    } else { // G_MTX_MODELVIEW
        if ((parameters & G_MTX_PUSH) && rsp.modelview_matrix_stack_size < 11) {
            ++rsp.modelview_matrix_stack_size;
            memcpy(rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1],
                   rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 2], sizeof(matrix));
        }
        if (parameters & G_MTX_LOAD) {
            memcpy(rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], matrix, sizeof(matrix));
        } else {
            gfx_matrix_mul(rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], matrix,
                           rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1]);
        }
        rsp.lights_changed = 1;
    }
    gfx_matrix_mul(rsp.MP_matrix, rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], rsp.P_matrix);
}

static void gfx_sp_pop_matrix(uint32_t count) {
    while (count--) {
        if (rsp.modelview_matrix_stack_size > 0) {
            --rsp.modelview_matrix_stack_size;
            if (rsp.modelview_matrix_stack_size > 0) {
                gfx_matrix_mul(rsp.MP_matrix, rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1],
                               rsp.P_matrix);
            }
        }
    }
}

static float gfx_adjust_x_for_aspect_ratio(float x, float w = 1.f) {
    if (fbActive) {
        return x;
    } else {
        return (rsp.aspect_ofs * w + x) * rsp.aspect_scale / gfx_current_dimensions.aspect_ratio;
    }
}

static void gfx_adjust_width_height_for_scale(uint32_t& width, uint32_t& height) {
    width = std::round(width * RATIO_Y);
    height = std::round(height * RATIO_Y);
    if (width == 0) {
        width = 1;
    }
    if (height == 0) {
        height = 1;
    }
}

/* D146: a valid mapped-memory pointer check. A desynced / corrupt front-end
 * DL (D75 family) hands wild pointers to the RSP command handlers; abort()
 * over one bad menu model is the wrong trade for a breadth-first port. */
static inline bool fast3d_ptr_ok(const void *p) {
    uintptr_t v = (uintptr_t)p;
    return v >= 0x10000 && v < 0x0000800000000000ULL;
}

extern "C" u32 videoGetFrameCount(void); /* port/src/video.c (D75 probe) */

/* D75 DL-sequence dump (M-196, GE_D75D="lo-hi"): the M-195 slot-overwrite
 * hypothesis says every G_VTX batch encodes dest_index=0, so a multi-batch
 * node's later batches clobber earlier ones and only the last survives. That
 * is ONLY harmful if a node's triangles are drawn AFTER a later node's G_VTX
 * has overwritten their slots -- i.e. only if the DL is structured batched
 * (all vertices for all nodes, then all triangles) rather than streamed
 * (each node: its vertices then its triangles). gfx_sp_tri1 copies vertex data
 * into buf_vbo eagerly at triangle time, so streaming order makes cross-node
 * overwrites harmless exactly like the N64 RDP. This probe records, per frame,
 * a compact V/T token stream (V=G_VTX batch, T=triangle) plus max-triangle-
 *index vs cumulative-vertices-loaded, to decide which structure GE's front-end
 * model DLs actually use. Zero cost unless GE_D75D is set. Remove once D75 is
 * root-caused. */
static int d75d_lo = -1, d75d_hi = 0x7FFFFFFF;
static int d75d_init_done = 0;
static uint32_t d75d_frame = 0xFFFFFFFFu;
static char d75d_seq[4096];
static int d75d_seqlen = 0;
static uint32_t d75d_maxtri = 0, d75d_vtxloaded = 0, d75d_batchhi = 0, d75d_nv = 0, d75d_nt = 0;
static uint32_t d75d_rej_triv = 0, d75d_rej_cull = 0, d75d_emitted = 0;
static uint32_t d75d_emit_on = 0, d75d_emit_off = 0; // emitted tris: NDC bbox intersects [-1,1]^2 vs not
static float d75d_ndc_minx=1e9f,d75d_ndc_maxx=-1e9f,d75d_ndc_miny=1e9f,d75d_ndc_maxy=-1e9f;
static uint32_t d75d_emit_invis = 0; // emitted tris with SHADER_OPT_INVISIBLE (G_BL_0+G_BL_CLR_MEM)
static int d75d_cmin=999, d75d_cmax=-1; // min/max over all vertex color channels of emitted tris
static uint32_t d75d_oml = 0xFFFFFFFFu; static int d75d_oml_set = 0;
static float d75d_zmin=1e9f,d75d_zmax=-1e9f; static uint32_t d75d_znan=0; static int d75d_use_tex=-1;
static uint64_t d75d_comb=0; static int d75d_prim[4]={-1,-1,-1,-1}; static int d75d_env[4]={-1,-1,-1,-1};
static int d75d_comb_utex=-1; // comb->used_textures[0] (combiner's own claim)
// D75D round 2: logo-mode-only sampling (the frame-level fields above mix the
// first emitted tri -- often a title quad -- with the LAST tri's combine mode,
// which is what made 'comb claims 0' look like an anomaly). These are updated
// on EVERY emitted tri whose rdp.combine_mode is the logo word, so comb and
// utex always describe the same triangle.
static int d75d_lg_utex=-1, d75d_lg_tex=-1; static uint32_t d75d_lg_cnt=0;
static int d75d_lg_set=0; static uint32_t d75d_lg_tmem,d75d_lg_fmt,d75d_lg_siz;
static int d75d_lg_shifts,d75d_lg_shifft,d75d_lg_uls,d75d_lg_ult,d75d_lg_lrs,d75d_lg_lrt;
static const uint8_t* d75d_lg_texaddr=nullptr; static uint32_t d75d_lg_texid,d75d_lg_texbytes;
static int d75d_lg_u0,d75d_lg_v0;

static void d75d_init(void) {
    if (d75d_init_done) return;
    d75d_init_done = 1;
    const char* e = getenv("GE_D75D");
    if (e && *e) {
        d75d_lo = atoi(e);
        const char* dash = strchr(e, '-');
        if (dash) d75d_hi = atoi(dash + 1);
    }
}
static bool d75d_env_active(void) { d75d_init(); return d75d_lo >= 0; }
static void d75d_flush(uint32_t f) {
    if (f == 0xFFFFFFFFu || d75d_seqlen == 0) return;
    fprintf(stderr,
        "D75D: f=%u nv=%u nt=%u maxtri=%u vtxloaded=%u batchhi=%u rej_triv=%u rej_cull=%u emitted=%u emit_on=%u emit_off=%u ndc_x=[%.3f,%.3f] ndc_y=[%.3f,%.3f] invis=%u col=[%d,%d] oml=0x%08x omset=%d z=[%.4f,%.4f] znan=%u utex=%d cutex=%d comb=0x%016llx prim=(%d,%d,%d,%d) env=(%d,%d,%d,%d) lg_cnt=%u lg_utex=%d lg_tex=%d lg_tile(tmem=%u fmt=%u siz=%u sh=%d st=%d uls=%d ult=%d lrs=%d lrt=%d) lg_texaddr=%p lg_texid=%u lg_texbytes=%u lg_uv0=(%d,%d) seq=%.8s%s\n",
        f, d75d_nv, d75d_nt, d75d_maxtri, d75d_vtxloaded, d75d_batchhi,
        d75d_rej_triv, d75d_rej_cull, d75d_emitted, d75d_emit_on, d75d_emit_off,
        d75d_ndc_minx, d75d_ndc_maxx, d75d_ndc_miny, d75d_ndc_maxy,
        d75d_emit_invis, d75d_cmin, d75d_cmax, (unsigned)d75d_oml, d75d_oml_set,
        d75d_zmin, d75d_zmax, d75d_znan, d75d_use_tex, d75d_comb_utex,
        (unsigned long long)d75d_comb, d75d_prim[0],d75d_prim[1],d75d_prim[2],d75d_prim[3],
        d75d_env[0],d75d_env[1],d75d_env[2],d75d_env[3],
        d75d_lg_cnt, d75d_lg_utex, d75d_lg_tex,
        (unsigned)d75d_lg_tmem,(unsigned)d75d_lg_fmt,(unsigned)d75d_lg_siz,
        d75d_lg_shifts,d75d_lg_shifft,d75d_lg_uls,d75d_lg_ult,d75d_lg_lrs,d75d_lg_lrt,
        (const void*)d75d_lg_texaddr,(unsigned)d75d_lg_texid,(unsigned)d75d_lg_texbytes,
        d75d_lg_u0,d75d_lg_v0,
        d75d_seq, (d75d_seqlen >= 4096) ? "...[trunc]" : "");
    d75d_seqlen = 0; d75d_maxtri = 0; d75d_vtxloaded = 0; d75d_batchhi = 0; d75d_nv = 0; d75d_nt = 0;
    d75d_rej_triv = 0; d75d_rej_cull = 0; d75d_emitted = 0; d75d_emit_on = 0; d75d_emit_off = 0;
    d75d_ndc_minx=1e9f;d75d_ndc_maxx=-1e9f;d75d_ndc_miny=1e9f;d75d_ndc_maxy=-1e9f;
    d75d_emit_invis = 0; d75d_cmin=999; d75d_cmax=-1; d75d_oml=0xFFFFFFFFu; d75d_oml_set=0;
    d75d_zmin=1e9f;d75d_zmax=-1e9f;d75d_znan=0;d75d_use_tex=-1;
    d75d_comb=0; for(int i=0;i<4;i++){d75d_prim[i]=-1;d75d_env[i]=-1;} d75d_comb_utex=-1;
    d75d_lg_utex=-1; d75d_lg_tex=-1;
}
static void d75d_frame_check(uint32_t f) {
    if (f != d75d_frame) { d75d_flush(f); d75d_frame = f; }
}
static void d75d_note_vtx(uint32_t f, uint32_t count, uint32_t dest) {
    d75d_init();
    if (d75d_lo < 0 || f < (uint32_t)d75d_lo || f > (uint32_t)d75d_hi) return;
    d75d_frame_check(f);
    d75d_nv++;
    d75d_vtxloaded += count;
    if (dest + count > d75d_batchhi) d75d_batchhi = dest + count;
    if (d75d_seqlen < 4096) d75d_seq[d75d_seqlen++] = 'V';
}
/* TEMP D306/D308: env-gated z-fighting FLICKER detector (docs/dev/findings.md
 * §D306/§D308). A coarse grid of screen cells remembers the depth values seen
 * recently at each cell; when a new value arrives that is close to a
 * recently-seen-but-different one (within GE_ZFTOL, default 2 units of 24-bit
 * quantized NDC z), that is the screen-space signature of genuine z-fighting
 * (the winner alternates between two near-equal depths as the camera moves).
 * Tessellation noise (adjacent tris of one surface) does not alternate. Gated
 * on GE_ZF; optional NDC box filter GE_ZFBOX="x0 y0 x1 y1"; caps output per
 * run. Remove once D306/D308 are resolved. */
static int zf_on = -1;
static int zf_tol = 2;
static float zfbox[4] = { -1.05f, -1.05f, 1.05f, 1.05f };
static int zfbox_on = 0;
#define ZF_GRIDX 96
#define ZF_GRIDY 54
#define ZF_HIST 6
struct ZfCell { uint32_t hist[ZF_HIST]; uint8_t n; uint16_t osc; };
static ZfCell zf_grid[ZF_GRIDX * ZF_GRIDY];
static uint32_t zf_hits = 0;
#define ZF_MIN_OSC 5   // a fight must recur this many times at the same cell
static void zf_note_emit(uint32_t f, struct LoadedVertex* const* v_arr) {
    if (zf_on < 0) {
        const char* e = getenv("GE_ZF");
        zf_on = (e && e[0]) ? 1 : 0;
        const char* t = getenv("GE_ZFTOL");
        if (t) zf_tol = atoi(t);
        const char* b = getenv("GE_ZFBOX");
        if (b && sscanf(b, "%f %f %f %f", &zfbox[0], &zfbox[1], &zfbox[2], &zfbox[3]) == 4)
            zfbox_on = 1;
    }
    if (!zf_on || zf_hits >= 600) return;
    float cx=0.f, cy=0.f, czw=0.f, wsum=0.f; int ok=0;
    for (int i = 0; i < 3; i++) {
        float w = v_arr[i]->w;
        if (w <= 0.f) continue;
        cx += (v_arr[i]->x / w) * w; cy += (v_arr[i]->y / w) * w; czw += (v_arr[i]->z / w) * w; wsum += w;
        ok = 1;
    }
    if (!ok || wsum <= 0.f) return;
    cx /= wsum; cy /= wsum; czw /= wsum;
    if (cx < -1.0f || cx > 1.0f || cy < -1.0f || cy > 1.0f) return; // on-screen only
    if (zfbox_on && (cx < zfbox[0] || cx > zfbox[2] || cy < zfbox[1] || cy > zfbox[3])) return;
    uint32_t z24 = (uint32_t)(czw * 255.f); // NDC z in [-1,1] -> 24-bit [0,65535]
    int gx = (int)((cx + 1.0f) * 0.5f * ZF_GRIDX); if (gx >= ZF_GRIDX) gx = ZF_GRIDX - 1;
    int gy = (int)((cy + 1.0f) * 0.5f * ZF_GRIDY); if (gy >= ZF_GRIDY) gy = ZF_GRIDY - 1;
    ZfCell* c = &zf_grid[gy * ZF_GRIDX + gx];
    int dup = 0;
    for (uint8_t k = 0; k < c->n; k++) if (c->hist[k] == z24) { dup = 1; break; }
    if (!dup) {
        // TRUE oscillation: new value close to an OLDER history entry while a
        // different value sat between them (a b a), AND the cell's history is
        // not a monotonic drift (tessellation gradient of one surface).
        int anyup = 0, anydn = 0;
        for (uint8_t k = 1; k < c->n; k++) {
            if (c->hist[k] > c->hist[k - 1]) anyup = 1;
            else if (c->hist[k] < c->hist[k - 1]) anydn = 1;
        }
        if (!(anyup && anydn)) goto zf_push;   // monotonic drift = gradient, not a fight
        for (uint8_t k = 0; k + 1 < c->n; k++) {
            long dz = (long)z24 - (long)c->hist[k];
            if (dz < 0) dz = -dz;
            if (dz > 0 && dz <= (long)zf_tol) {
                uint16_t o = ++c->osc;
                if (o < ZF_MIN_OSC || (o % 10) != 0 && o != ZF_MIN_OSC) goto zf_push;
                zf_hits++;
                fprintf(stderr,
                        "ZF: f=%u z24=%u dprev=%ld osc=%u ndc=(%.3f,%.3f) oml=0x%08x comb=0x%016llx wavg=%.1f\n",
                        f, z24, dz, (unsigned)o, cx, cy, rdp.other_mode_l,
                        (unsigned long long)rdp.combine_mode, wsum / 3.0f);
                if (zf_hits >= 600) return;
            }
        }
zf_push:
        if (c->n < ZF_HIST) c->hist[c->n++] = z24;
        else { memmove(c->hist, c->hist + 1, sizeof(uint32_t) * (ZF_HIST - 1)); c->hist[ZF_HIST - 1] = z24; }
    }
}

/* TEMP D303: env-gated detector for the "extra long flash straight up" quad
 * (M-?? / docs/dev/findings.md §D303). Logs any emitted triangle whose NDC bbox
 * is tall + thin (aspect > 3, height > 25% of frame) and mostly on-screen — the
 * screen-space signature of the reported artifact, whatever its source. Gated
 * on GE_D303; caps output. Remove once D303 is resolved. */
static int d303_on = -1;
static uint32_t d303_hits = 0;
static void d303_note_emit(uint32_t f, struct LoadedVertex* const* v_arr) {
    if (d303_on < 0) { const char* e = getenv("GE_D303"); d303_on = (e && e[0]) ? 1 : 0; }
    if (!d303_on || d303_hits >= 200) return;
    float mnx=1e9f,mxx=-1e9f,mny=1e9f,mxy=-1e9f, wsum = 0.f;
    int ok = 0;
    for (int i = 0; i < 3; i++) {
        float w = v_arr[i]->w;
        if (w <= 0.f) continue;
        wsum += w;
        float nx = v_arr[i]->x / w, ny = v_arr[i]->y / w;
        if (nx<mnx)mnx=nx; if(nx>mxx)mxx=nx; if(ny<mny)mny=ny; if(ny>mxy)mxy=ny;
        ok = 1;
    }
    if (!ok) return;
    float h = mxy - mny, wd = mxx - mnx;
    if (h < 0.45f || wd <= 0.f || h / wd < 3.0f) return;          // VERY tall + thin
    if (mxy < 0.1f || mny > 0.9f) return;                          // must reach upper screen region
    d303_hits++;
    int bl = (rdp.other_mode_l >> 24) & 3u, blc = (rdp.other_mode_l >> 20) & 3u;
    fprintf(stderr,
        "D303: f=%u ndc=(%.3f,%.3f)-(%.3f,%.3f) h=%.3f w=%.3f asp=%.1f bl=%d blc=%d oml=0x%08x comb=0x%016llx prim=(%u,%u,%u) env=(%u,%u,%u) wavg=%.1f c0=(%d,%d,%d)\n",
        f, mnx, mny, mxx, mxy, h, wd, (wd > 0.f ? h / wd : 99.0f), bl, blc,
        rdp.other_mode_l, (unsigned long long)rdp.combine_mode,
        rdp.prim_color.r, rdp.prim_color.g, rdp.prim_color.b,
        rdp.env_color.r, rdp.env_color.g, rdp.env_color.b, wsum / 3.0f,
        v_arr[0]->color.r, v_arr[0]->color.g, v_arr[0]->color.b);
}

static void d75d_note_tri(uint32_t f, uint32_t maxidx) {
    d75d_init();
    if (d75d_lo < 0 || f < (uint32_t)d75d_lo || f > (uint32_t)d75d_hi) return;
    d75d_frame_check(f);
    d75d_nt++;
    if (maxidx > d75d_maxtri) d75d_maxtri = maxidx;
    if (d75d_seqlen < 4096) d75d_seq[d75d_seqlen++] = 'T';
}
static void d75d_note_rej(uint32_t f, int which) {
    d75d_init();
    if (d75d_lo < 0 || f < (uint32_t)d75d_lo || f > (uint32_t)d75d_hi) return;
    d75d_frame_check(f);
    if (which == 0) d75d_rej_triv++; else d75d_rej_cull++;
}
static inline int gfx_lod_tile_offset(const int i); // fwd (defined below; D75D round 2)

static void d75d_note_emit(uint32_t f, struct LoadedVertex* const* v_arr) {
    d75d_init();
    if (d75d_lo < 0 || f < (uint32_t)d75d_lo || f > (uint32_t)d75d_hi) return;
    d75d_frame_check(f);
    d75d_emitted++;
    // NDC bbox of this triangle (x/w, y/w); count on-screen vs off
    float mnx=1e9f,mxx=-1e9f,mny=1e9f,mxy=-1e9f;
    for (int i = 0; i < 3; i++) {
        float w = v_arr[i]->w;
        if (w == 0.f) continue;
        float nx = v_arr[i]->x / w, ny = v_arr[i]->y / w;
        if (nx<mnx)mnx=nx; if(nx>mxx)mxx=nx; if(ny<mny)mny=ny; if(ny>mxy)mxy=ny;
    }
    if (mxx < -1e8f) return; // all w==0, degenerate
    if (mxx >= -1.05f && mnx <= 1.05f && mxy >= -1.05f && mny <= 1.05f) d75d_emit_on++; else d75d_emit_off++;
    if (mnx<d75d_ndc_minx)d75d_ndc_minx=mnx; if(mxx>d75d_ndc_maxx)d75d_ndc_maxx=mxx;
    if (mny<d75d_ndc_miny)d75d_ndc_miny=mny; if(mxy>d75d_ndc_maxy)d75d_ndc_maxy=mxy;
    // invisible flag + oml + vertex color range
    if ((rdp.other_mode_l & (3u << 24)) == ((uint32_t)G_BL_0 << 24) &&
        (rdp.other_mode_l & (3u << 20)) == ((uint32_t)G_BL_CLR_MEM << 20)) d75d_emit_invis++;
    if (!d75d_oml_set) { d75d_oml = rdp.other_mode_l; d75d_oml_set = 1; }
    for (int i = 0; i < 3; i++) {
        int cr=v_arr[i]->color.r, cg=v_arr[i]->color.g, cb=v_arr[i]->color.b;
        if (cr<d75d_cmin)d75d_cmin=cr; if (cr>d75d_cmax)d75d_cmax=cr;
        if (cg<d75d_cmin)d75d_cmin=cg; if (cg>d75d_cmax)d75d_cmax=cg;
        if (cb<d75d_cmin)d75d_cmin=cb; if (cb>d75d_cmax)d75d_cmax=cb;
    }
}
// called at emit with the texunit-0 bound flag
static void d75d_note_emit_z(uint32_t f, struct LoadedVertex* const* v_arr, int has_tex, int comb_utex) {
    d75d_init();
    if (d75d_lo < 0 || f < (uint32_t)d75d_lo || f > (uint32_t)d75d_hi) return;
    d75d_frame_check(f);
    for (int i = 0; i < 3; i++) {
        float w = v_arr[i]->w;
        if (w == 0.f) { d75d_znan++; continue; }
        float nz = v_arr[i]->z / w;
        if (!std::isfinite(nz)) { d75d_znan++; continue; }
        if (nz<d75d_zmin)d75d_zmin=nz; if(nz>d75d_zmax)d75d_zmax=nz;
    }
    if (d75d_use_tex < 0) d75d_use_tex = has_tex;
    if (d75d_comb_utex < 0) d75d_comb_utex = comb_utex;
    if (rdp.combine_mode == 0x009ffe4f19ffe4f1ULL) {
        d75d_lg_utex = comb_utex; d75d_lg_tex = has_tex; d75d_lg_cnt++;
        if (!d75d_lg_set && v_arr[0] && v_arr[0]->w != 0.f) {
            d75d_lg_set = 1;
            const uint32_t t0 = rdp.first_tile_index + gfx_lod_tile_offset(0);
            d75d_lg_tmem = rdp.texture_tile[t0].tmem; d75d_lg_fmt = rdp.texture_tile[t0].fmt;
            d75d_lg_siz = rdp.texture_tile[t0].siz;
            d75d_lg_shifts = rdp.texture_tile[t0].shifts; d75d_lg_shifft = rdp.texture_tile[t0].shiftt;
            d75d_lg_uls = rdp.texture_tile[t0].uls; d75d_lg_ult = rdp.texture_tile[t0].ult;
            d75d_lg_lrs = rdp.texture_tile[t0].lrs; d75d_lg_lrt = rdp.texture_tile[t0].lrt;
            if (rendering_state.textures[0]) {
                d75d_lg_texaddr = rendering_state.textures[0]->first.texture_addr;
                d75d_lg_texid = rendering_state.textures[0]->second.texture_id;
                d75d_lg_texbytes = rendering_state.textures[0]->first.size_bytes;
            }
            d75d_lg_u0 = (int)v_arr[0]->u; d75d_lg_v0 = (int)v_arr[0]->v;
        }
    }
    d75d_comb = rdp.combine_mode;
    d75d_prim[0]=rdp.prim_color.r; d75d_prim[1]=rdp.prim_color.g; d75d_prim[2]=rdp.prim_color.b; d75d_prim[3]=rdp.prim_color.a;
    d75d_env[0]=rdp.env_color.r; d75d_env[1]=rdp.env_color.g; d75d_env[2]=rdp.env_color.b; d75d_env[3]=rdp.env_color.a;
}

/* D252 TEMP (2026-09-26): F12 with GE_D157 set opens a full-detail D157T
 * burst (every small-texture vertex, no sampling) for ~90 frames, so a live
 * sighting is captured even after the continuous cap has dropped to 1-in-50.
 * Armed from videoEndFrame's screenshot path via gfxD157Burst(). */
static volatile uint32_t s_d157BurstUntil = 0;

static void gfx_sp_vertex(size_t n_vertices, size_t dest_index, const Vtx* vertices) {
    const size_t d75_di0 = dest_index; /* D75 probe: loop below mutates dest_index */
    SUPPORT_CHECK(n_vertices <= MAX_VERTICES);

    if (!fast3d_ptr_ok(vertices) || dest_index + n_vertices > MAX_VERTICES) {
        return; /* D146 */
    }

    for (size_t i = 0; i < n_vertices; i++, dest_index++) {
        const Vtx* v = &vertices[i];
        struct LoadedVertex* d = &rsp.loaded_vertices[dest_index];

        /* GE's Vtx layout (include/PR/gbi.h): ob[] position, tc[] texture
         * coords, and the last 4 bytes are either RGBA (Vtx_t.cn) or a
         * normal + alpha (Vtx_tn.n/a). Unlike PD there is no G_COL colour
         * table: colours/normals live in the vertex itself. */
        float x = v->v.ob[0] * rsp.MP_matrix[0][0] + v->v.ob[1] * rsp.MP_matrix[1][0] + v->v.ob[2] * rsp.MP_matrix[2][0] + rsp.MP_matrix[3][0];
        float y = v->v.ob[0] * rsp.MP_matrix[0][1] + v->v.ob[1] * rsp.MP_matrix[1][1] + v->v.ob[2] * rsp.MP_matrix[2][1] + rsp.MP_matrix[3][1];
        float z = v->v.ob[0] * rsp.MP_matrix[0][2] + v->v.ob[1] * rsp.MP_matrix[1][2] + v->v.ob[2] * rsp.MP_matrix[2][2] + rsp.MP_matrix[3][2];
        float w = v->v.ob[0] * rsp.MP_matrix[0][3] + v->v.ob[1] * rsp.MP_matrix[1][3] + v->v.ob[2] * rsp.MP_matrix[2][3] + rsp.MP_matrix[3][3];

        x = gfx_adjust_x_for_aspect_ratio(x, w);

        short U = v->v.tc[0] * rsp.texture_scaling_factor.s >> 16;
        short V = v->v.tc[1] * rsp.texture_scaling_factor.t >> 16;

        struct NormalColor vcn_data;
        if (rsp.geometry_mode & G_LIGHTING) {
            /* Lit vertices carry their normal in the last 3 bytes. */
            vcn_data.x = (int8_t)v->n.n[0];
            vcn_data.y = (int8_t)v->n.n[1];
            vcn_data.z = (int8_t)v->n.n[2];
            vcn_data.a = v->n.a;
        } else {
            vcn_data.r = v->v.cn[0];
            vcn_data.g = v->v.cn[1];
            vcn_data.b = v->v.cn[2];
            vcn_data.a = v->v.cn[3];
        }
        const struct NormalColor *vcn = &vcn_data;

        if (rsp.geometry_mode & G_LIGHTING) {
            if (rsp.lights_changed) {
                for (int i = 0; i < rsp.current_num_lights - 1; i++) {
                    calculate_normal_dir(&rsp.current_lights[i], rsp.current_lights_coeffs[i]);
                }
                if (rsp.lookat_enabled) {
                    calculate_normal_dir(&rsp.lookat[0], rsp.current_lookat_coeffs[0]);
                    calculate_normal_dir(&rsp.lookat[1], rsp.current_lookat_coeffs[1]);
                }
                rsp.lights_changed = false;
            }

            int r = rsp.current_lights[rsp.current_num_lights - 1].col[0];
            int g = rsp.current_lights[rsp.current_num_lights - 1].col[1];
            int b = rsp.current_lights[rsp.current_num_lights - 1].col[2];

            for (int i = 0; i < rsp.current_num_lights - 1; i++) {
                float intensity = 0;
                intensity += vcn->x * rsp.current_lights_coeffs[i][0];
                intensity += vcn->y * rsp.current_lights_coeffs[i][1];
                intensity += vcn->z * rsp.current_lights_coeffs[i][2];
                intensity /= 127.0f;
                if (intensity > 0.0f) {
                    r += intensity * rsp.current_lights[i].col[0];
                    g += intensity * rsp.current_lights[i].col[1];
                    b += intensity * rsp.current_lights[i].col[2];
                }
            }

            d->color.r = r > 255 ? 255 : r;
            d->color.g = g > 255 ? 255 : g;
            d->color.b = b > 255 ? 255 : b;

            /* D195/D72 correction: GE DOES use RSP-generated (environment-
             * mapped) texture coordinates -- shiny gold/silver weapon skins
             * and Control's reflective console glass all set G_TEXTURE_GEN,
             * the real GBI bit that gates this on real hardware. D72.1
             * originally removed this whole PD-inherited block to fix the
             * Rareware logo (a lit surface that does NOT set G_TEXTURE_GEN,
             * so it was never supposed to hit this path in the first place)
             * but over-corrected: it deleted the feature instead of gating
             * it on the bit that actually distinguishes the two cases. With
             * no envmap UV generation, every G_TEXTURE_GEN draw sampled its
             * texture at whatever raw tc[] happened to be authored (usually
             * ~0), landing on the same corner texel every frame regardless
             * of view angle -- for a typical dark-edged reflection map, a
             * solid near-black surface instead of a shiny/reflective one.
             * Gating on G_TEXTURE_GEN preserves D72.1's actual fix (the logo
             * never sets this bit, so it's unaffected) while restoring the
             * envmap effect for the draws that do. */
            if (rsp.geometry_mode & G_TEXTURE_GEN) {
                float dotx = 0, doty = 0;
                if (rsp.lookat_enabled) {
                    dotx += vcn->x * rsp.current_lookat_coeffs[0][0];
                    dotx += vcn->y * rsp.current_lookat_coeffs[0][1];
                    dotx += vcn->z * rsp.current_lookat_coeffs[0][2];
                    doty += vcn->x * rsp.current_lookat_coeffs[1][0];
                    doty += vcn->y * rsp.current_lookat_coeffs[1][1];
                    doty += vcn->z * rsp.current_lookat_coeffs[1][2];
                    dotx /= 127.0f;
                    doty /= 127.0f;
                } else {
                    float tvcn[3];
                    calculate_normal_dir(vcn, tvcn);
                    dotx = tvcn[0];
                    doty = tvcn[1];
                }

                dotx = clampf(dotx, -1.0f, 1.0f);
                doty = clampf(doty, -1.0f, 1.0f);

                if (rsp.geometry_mode & G_TEXTURE_GEN_LINEAR) {
                    dotx = acosf(-dotx) / 4.0f;
                    doty = acosf(-doty) / 4.0f;
                } else {
                    dotx = (dotx + 1.0f) / 4.0f;
                    doty = (doty + 1.0f) / 4.0f;
                }

                U = (int32_t)(dotx * rsp.texture_scaling_factor.s);
                V = (int32_t)(doty * rsp.texture_scaling_factor.t);
            }
        } else {
            d->color.r = vcn->r;
            d->color.g = vcn->g;
            d->color.b = vcn->b;
        }

#ifdef PORT
        /* D157 diag (temporary, session 2026-09-16/17): does bullet_spark_render
         * (glass2.c) actually draw with G_LIGHTING on? The M-110 mechanism
         * (this vertex's v.cn[]/n.n[] union reinterpretation above) was
         * already ruled out for explosion.c's particles (5000/5000 samples
         * measured LIGHTING=off, see the D219 comment a few hundred lines
         * below) -- don't assume it holds for glass2.c's bullet sparks
         * without measuring. Key on the ORIGINAL authored bytes
         * (v->v.cn[], readable regardless of which branch fired above,
         * since n.n[]/cn[] are the same union storage) matching one of
         * glass2.c's g_BulletSparkColors[] entries exactly. */
        static int ge_d157 = -1;
        if (ge_d157 < 0) ge_d157 = getenv("GE_D157") != NULL;
        if (ge_d157) {
            static const uint8_t known[][4] = {
                {0xFF,0xFF,0xFF,0xFF}, {0xFF,0xFF,0xC8,0xFF}, {0xFF,0x00,0x00,0xFF},
            };
            /* 2026-09-26: this match was uncapped and white (255,255,255,255)
             * hits every HUD/text/effect vertex -> ~200k lines in 18 s, 4-6 fps
             * in live play. Now: first 500 hits, then only inside an F12 burst. */
            extern uint32_t num_dls;
            static int d157m_n = 0;
            const bool d157m_on = d157m_n < 500 || num_dls <= s_d157BurstUntil;
            if (d157m_on) for (const auto& k : known) {
                if (v->v.cn[0] == k[0] && v->v.cn[1] == k[1] && v->v.cn[2] == k[2] && v->v.cn[3] == k[3]) {
                    d157m_n++;
                    sysLogPrintf(LOG_NOTE,
                        "D157: bulletspark-color-match cn=(%d,%d,%d,%d) geometry_mode=%08x LIGHTING=%s -> shaded=(%d,%d,%d)",
                        v->v.cn[0], v->v.cn[1], v->v.cn[2], v->v.cn[3], rsp.geometry_mode,
                        (rsp.geometry_mode & G_LIGHTING) ? "ON" : "off",
                        (int)d->color.r, (int)d->color.g, (int)d->color.b);
                    break;
                }
            }

            /* D157-B (M-158): the exact-color-match key above NEVER caught the
             * pale-yellow (255,255,200) door-spark vertices in the Bunker1
             * rainbow repro (only 2 white hits, both LIGHTING=off), so per
             * D219 M-157's revised next step this sibling block keys on
             * TEXTURE IDENTITY instead: any vertex whose render tile (tile 0)
             * holds a small loaded texture (size <= 16 KiB -- the spark
             * family is IA 64x64, promoted 8b->16b by texSelect) inside the
             * known-bad frame window of that repro (shots at frames 60-350,
             * visible rainbow frames 108-220). Logs the full RGBA path per
             * vertex: authored cn, shaded result, LIGHTING, geometry_mode,
             * combine_mode (texSelect sets G_CC_MODULATEIA for these -- a
             * different value here is a combiner-state leak), and the tile's
             * source identity (addr/fmt/siz/size = the TextureCacheKey for
             * non-CI textures, so cache-collision reasoning works offline).
             * Frame window + size key are repro-specific; widen if re-running
             * elsewhere. Remove once D219/D252 is root-caused. */
            {
                extern uint32_t num_dls;
                /* 2026-09-18: dropped the old num_dls in [90,240] gate -- that
                 * window only fit the original short GE_INPUTSCRIPT repro; a
                 * real interactive session runs for thousands of frames before
                 * the player actually triggers the bug, so the gate silently
                 * discarded 100% of useful data in live play. The existing
                 * per-run hit caps below already bound log growth without it. */
                {
                    const uint32_t tile0 = rdp.first_tile_index;
                    LoadedTexture& lt0 = rdp.loaded_texture[rdp.texture_tile[tile0].tmem];
                    if (lt0.addr && lt0.size_bytes <= 16384) {
                        static int d157b_n = 0;
                        d157b_n++;
                        /* Raised from 400/1-in-200 (sized for a ~250-frame
                         * scripted repro) to a much larger continuous window
                         * so a real, minutes-long play session doesn't drop
                         * to sparse 1-in-200 sampling before the player
                         * actually triggers the bug. */
                        static int d157burst_n = 0;
                        const bool burst = num_dls <= s_d157BurstUntil && d157burst_n < 60000;
                        if (burst) d157burst_n++;
                        /* 2026-09-26: continuous logging cut to a 2000-line baseline;
                         * the 20000 + 1-in-50 tail was a big share of the
                         * live-play slowdown. Full detail now comes from F12. */
                        if (burst || d157b_n <= 2000) {
                            sysLogPrintf(LOG_NOTE,
                                "D157T%s: frame=%u cn=(%d,%d,%d,%d) shaded=(%d,%d,%d) LIGHTING=%s geom=%08x combine=%llx | "
                                "tile0=%u tmem=%u fmt=%u siz=%u addr=%p size=%u line=%u | uv=(%d,%d)",
                                burst ? "B" : "", num_dls,
                                v->v.cn[0], v->v.cn[1], v->v.cn[2], v->v.cn[3],
                                (int)d->color.r, (int)d->color.g, (int)d->color.b,
                                (rsp.geometry_mode & G_LIGHTING) ? "ON" : "off", rsp.geometry_mode,
                                (unsigned long long)rdp.combine_mode,
                                tile0, rdp.texture_tile[tile0].tmem,
                                rdp.texture_tile[tile0].fmt, rdp.texture_tile[tile0].siz,
                                (const void*)lt0.addr, lt0.size_bytes, lt0.line_size_bytes,
                                (int)U, (int)V);
                        }
                    }
                }
            }
        }
#endif

        d->u = U;
        d->v = V;

        // trivial clip rejection
        d->clip_rej = 0;
        if (x < -w) {
            d->clip_rej |= 1; // CLIP_LEFT
        }
        if (x > w) {
            d->clip_rej |= 2; // CLIP_RIGHT
        }
        if (y < -w) {
            d->clip_rej |= 4; // CLIP_BOTTOM
        }
        if (y > w) {
            d->clip_rej |= 8; // CLIP_TOP
        }
        // if (z < -w) d->clip_rej |= 16; // CLIP_NEAR
        if (z > w) {
            d->clip_rej |= 32; // CLIP_FAR
        }

        d->x = x;
        d->y = y;
        d->z = z;
        d->w = w;

        if (rsp.geometry_mode & G_FOG) {
            if (fabsf(w) < 0.001f) {
                // To avoid division by zero
                w = 0.001f;
            }

            float winv = 1.0f / w;
            if (winv < 0.0f) {
                winv = std::numeric_limits<int16_t>::max();
            }

            float fog_z = z * winv * rsp.fog_mul + rsp.fog_offset;
            d->fog = clampf(fog_z, 0.f, 255.f);
        } else {
            d->fog = rdp.fog_color.a;
        }

        d->color.a = vcn->a; // can be required for SHADE_ALPHA even if fog is enabled
    }

#ifdef PORT
    /* TEMP D75 (M-19x): per-batch transformed-vertex range probe. Static
     * analysis of the nintendologo path exhausted itself clean (all 23 op=4
     * nodes visited every frame, GDLs byte-faithful to N64, every opcode
     * handled, G_VTX counts sum exactly to nv, G_TRI4 decode matches the
     * gSP4Triangles macro) -- so log what the pipeline actually computes:
     * for frames in [lo,hi] (env GE_D75V="lo-hi"), one line per G_VTX batch
     * with the resolved vertex ptr, raw first-vertex ob[], transformed
     * x/y/z/w ranges and the MP translation row. The logo's batches are
     * identifiable offline by vtx ptr == BaseAddr(0x70157a98)+file offset.
     * Remove once D75 is root-caused. */
    {
        static int d75v_lo = -1, d75v_hi = 0;
        if (d75v_lo < 0) {
            const char* v = getenv("GE_D75V");
            d75v_lo = 1; d75v_hi = 0x7fffffff;
            if (!v || sscanf(v, "%d-%d", &d75v_lo, &d75v_hi) != 2)
                d75v_lo = -1;
        }
        if (d75v_lo >= 0 && (int)videoGetFrameCount() >= d75v_lo &&
            (int)videoGetFrameCount() <= d75v_hi) {
            float mnx = 1e30f, mxx = -1e30f, mny = 1e30f, mxy = -1e30f,
                  mnz = 1e30f, mxz = -1e30f, mnw = 1e30f, mxw = -1e30f;
            for (size_t i = 0; i < n_vertices; i++) {
                const struct LoadedVertex* d2 = &rsp.loaded_vertices[d75_di0 + i];
                if (d2->x < mnx) mnx = d2->x; if (d2->x > mxx) mxx = d2->x;
                if (d2->y < mny) mny = d2->y; if (d2->y > mxy) mxy = d2->y;
                if (d2->z < mnz) mnz = d2->z; if (d2->z > mxz) mxz = d2->z;
                if (d2->w < mnw) mnw = d2->w; if (d2->w > mxw) mxw = d2->w;
            }
            const Vtx* v0 = &vertices[0];
            sysLogPrintf(LOG_NOTE,
                "D75V: f=%u vtx=%p n=%zu di=%zu raw0=(%d,%d,%d) x[%.0f,%.0f] y[%.0f,%.0f] z[%.0f,%.0f] w[%.0f,%.0f] MPt=(%.1f,%.1f,%.1f)",
                videoGetFrameCount(), (const void*)vertices, n_vertices, d75_di0,
                (int)v0->v.ob[0], (int)v0->v.ob[1], (int)v0->v.ob[2],
                mnx, mxx, mny, mxy, mnz, mxz, mnw, mxw,
                rsp.MP_matrix[3][0], rsp.MP_matrix[3][1], rsp.MP_matrix[3][2]);
        }
    }
#endif
}

static void gfx_sp_modify_vertex(uint16_t vtx_idx, uint8_t where, uint32_t val) {
    SUPPORT_CHECK(where == G_MWO_POINT_ST);

    int16_t s = (int16_t)(val >> 16);
    int16_t t = (int16_t)val;

    struct LoadedVertex* v = &rsp.loaded_vertices[vtx_idx];
    v->u = s;
    v->v = t;
}

/* Intro blood (M-201): set while a texture rectangle is being drawn. */
static bool s_in_texrect = false;

static inline int gfx_lod_tile_offset(const int i) {
    /* Intro blood (M-201): a texture rectangle outside 2-cycle mode samples
     * exactly the tile its command names -- the RDP only selects LOD tiles in
     * 2-cycle mode. The title leaves G_TL_LOD set from earlier draws, and the
     * D236 detail-base rule below then sent the 1-cycle gun-barrel blood
     * rectangle to a stale tile-1 CI8 declaration (static garbage instead of
     * the drip). Scoped to texrects so 3D LOD/detail paths are untouched. */
    if (s_in_texrect && (rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) != G_CYC_2CYCLE)
        return i;
    if (gfx_detail_textures_enabled)
        return ((rdp.tex_lod && !rdp.tex_detail) ? 0 : i);
    // D107: GE has no true detail textures (gfx_detail_textures_enabled is
    // false), but its room GDLs still emit G_TL_LOD + G_TD_DETAIL for
    // mip-mapped textures. The old `rdp.tex_lod ? rdp.tex_detail : i` then
    // returned tex_detail (1) for every texel -> fast3d sampled GE's first
    // mip (tile 1), whose single-LOADBLOCK TMEM slot fast3d never registers
    // -> a magnified crop of the base image (the "blurry blob" ceilings /
    // wall panels in BUNKER1). GE loads the whole mip chain at TMEM 0, so
    // for an LOD texture the base render tile is the only correctly-loaded
    // level: use it.
    //
    // D172: but this must NOT collapse a genuine non-LOD two-texture combine.
    // The explosion/blood/spark particle records (assets/oddtextures.c
    // globalDL_0x078..) set G_TL_TILE (no LOD) + G_CC_INTERFERENCE
    // (TEXEL0 * TEXEL1) and bind tile 0 = IA8 smoke @ TMEM 0, tile 1 = RGBA16
    // fire @ TMEM 0x188. Returning 0 here fed TEXEL1 the smoke texture too
    // (smoke * smoke) -> the magenta/cyan particle colour. Only fold to the
    // base tile when LOD is actually active.
    //
    // D236 pass 26: "GE loads the whole mip chain at TMEM 0" holds for the
    // TEXTURETYPE_LOD / TEXTURETYPE_MIPMAP bindings (texHandleType0 /
    // texHandleType2 both do texWriteLoadToTmemAddr(tex, 0)), but NOT for
    // TEXTURETYPE_DETAIL. texHandleType1 (tex.c) loads the DETAIL texture at
    // TMEM 0 and the real base image at TMEM offset texGetSizeInBytes(tex2,0)
    // on tiles 1+. Folding to tile 0 there samples the detail texture instead
    // of the base image. On Surface 1 that is literally the D236 bug: all four
    // treeline cards (tex1198-1201, RGBA5551 64x17 cut-outs) are type-1
    // bindings whose detail texture is tex2465, an opaque 32x32 IA8 noise
    // tile -- so the treeline draws as an opaque tiled noise wall and the
    // cards themselves are never even imported (pass 25's "zero fmt=0 siz=2
    // imports in 4800 frames").
    //
    // Discriminator: tile fi+1's declared FORMAT. Tiles 1.. of a type-0/type-2
    // binding are LOD levels of tile 0's image, so they necessarily carry the
    // same fmt/siz; a type-1 pair is two unrelated textures and generally does
    // not (Surface 1's cards: tile 0 = tex2465 IA8, tile 1 = tex1198 RGBA16).
    // Deliberately asymmetric -- a mip chain can never trip this, and a detail
    // pair that happens to share a format just keeps today's behaviour -- so
    // the D107 mip case is safe by construction.
    if (rdp.tex_lod) {
        const uint32_t fi = rdp.first_tile_index;
        if (g_detail_base_tile && fi + 1 < 8 &&
            rdp.texture_tile[fi + 1].tmem != rdp.texture_tile[fi].tmem &&
            (rdp.texture_tile[fi + 1].fmt != rdp.texture_tile[fi].fmt ||
             rdp.texture_tile[fi + 1].siz != rdp.texture_tile[fi].siz)) {
            return 1;
        }
        return 0;
    }
    return i;
}

static void gfx_sp_tri1(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx, bool is_rect) {
    /* D75D: only real model triangles (all indices < MAX_VERTICES); the
     * fullscreen-quad helpers use indices MAX_VERTICES+0..3 and would skew maxtri. */
    if (vtx1_idx < MAX_VERTICES && vtx2_idx < MAX_VERTICES && vtx3_idx < MAX_VERTICES) {
        uint32_t d75d_mx = vtx1_idx > vtx2_idx ? (vtx1_idx > vtx3_idx ? vtx1_idx : vtx3_idx)
                                               : (vtx2_idx > vtx3_idx ? vtx2_idx : vtx3_idx);
        d75d_note_tri(videoGetFrameCount(), d75d_mx);
    }
    struct LoadedVertex* v1 = &rsp.loaded_vertices[vtx1_idx];
    struct LoadedVertex* v2 = &rsp.loaded_vertices[vtx2_idx];
    struct LoadedVertex* v3 = &rsp.loaded_vertices[vtx3_idx];
    struct LoadedVertex* v_arr[3] = { v1, v2, v3 };

    if ((rsp.extra_geometry_mode & G_NO_CLIPPING_EXT) == 0) {
        /* D233: the outcode bits set in gfx_sp_vertex (x<-w / x>w / y<-w /
         * y>w / z>w) are only valid half-space tests when w>0. A vertex
         * that has crossed behind the camera plane (w<0) flips the sense of
         * those comparisons, so its clip_rej bits can come out wrong. If
         * that spurious bit happens to match the other two (genuinely
         * correct) vertices' bits, the AND-reduction below fires and drops
         * the whole triangle before it ever reaches the GPU -- even though
         * OpenGL's own homogeneous clipper (which handles w<0 correctly)
         * would have rendered the visible portion of it. This hits large
         * polygons near the camera (room walls/ceilings near a doorway,
         * where the camera is close enough for a vertex to sit behind it)
         * far more than small prop models, matching D233's "room geometry
         * intermittently vanishes near doors, furniture/terminals still
         * draw" report. Fix: never trust the trivial-reject AND test when
         * any vertex has a negative w -- defer to GL's clipper instead,
         * same as the backface-cull code just below already does for the
         * same w-sign hazard. Worst case for the (rare) mixed-sign case is
         * a few extra triangles reaching the GPU; never fewer. */
        bool any_behind_camera = (v1->w < 0) || (v2->w < 0) || (v3->w < 0);
        if (!any_behind_camera && (v1->clip_rej & v2->clip_rej & v3->clip_rej)) {
            // The whole triangle lies outside the visible area
            d75d_note_rej(videoGetFrameCount(), 0);
            return;
        }

        /* D288 diag (M-152, frame-gated M-155, any_behind-restricted
         * M-155-follow-up): the silo intro's screen-filling stray triangle
         * (bounded to level_20 frames ~562-741) isn't explained by this
         * function's existing D233/D106 guards on static review -- log any
         * triangle whose clip-space NDC bbox covers an unreasonable chunk
         * of the screen, together with each vertex's w and the render-mode
         * word (oml), to catch it at draw time. M-155: the first ungated
         * run fired 116k times in ~1500-2000 frames -- bbox_frac >0.15
         * alone is a common occurrence for ordinary near-camera geometry,
         * not a distinguishing signature. Gated to the reported frame
         * window (num_dls, the pattern the since-removed GE_D236RM
         * probe also used) and the
         * threshold raised well above the ~0.15 "normal" baseline M-155
         * established, so a hit here is actually rare. M-155 follow-up:
         * for any_behind_camera==1 triangles the x/w perspective divide is
         * meaningless (w near zero or negative -> arbitrarily large
         * "bbox_frac" for perfectly ordinary geometry), which is exactly
         * why the code above defers those to GL's own clipper -- so the
         * probe now skips them entirely; a hit here has comfortably
         * positive w's and is a genuine large-NDC-bbox triangle. oml is
         * logged so the culprit's material/model can be identified against
         * the level's asset tables (the flat RGB(83,62,44) color grep
         * found nothing in the .c sources -- binary model blobs). Remove
         * once D288 is root-caused. */
        static int ge_d288 = -1;
        if (ge_d288 < 0) ge_d288 = getenv("GE_D288") != NULL;
        if (ge_d288 && !any_behind_camera) {
            extern uint32_t num_dls;
            if (num_dls >= 550 && num_dls <= 755) {
                float minx = std::fmin(std::fmin(v1->x / v1->w, v2->x / v2->w), v3->x / v3->w);
                float maxx = std::fmax(std::fmax(v1->x / v1->w, v2->x / v2->w), v3->x / v3->w);
                float miny = std::fmin(std::fmin(v1->y / v1->w, v2->y / v2->w), v3->y / v3->w);
                float maxy = std::fmax(std::fmax(v1->y / v1->w, v2->y / v2->w), v3->y / v3->w);
                float bbox_frac = (maxx - minx) * (maxy - miny) / 4.0f; // NDC quad is [-1,1]^2
                if (!std::isfinite(bbox_frac) || bbox_frac > 0.5f) {
                    fprintf(stderr, "D288: dl=%u tri w=(%.6f,%.6f,%.6f) bbox_frac=%.3f oml=0x%08x color=(%d,%d,%d)\n",
                            num_dls, v1->w, v2->w, v3->w, bbox_frac,
                            (unsigned)rdp.other_mode_l,
                            v1->color.r, v1->color.g, v1->color.b);
                }
            }
        }
    }

    if ((rsp.geometry_mode & G_CULL_BOTH) != 0) {
        float dx1 = v1->x / (v1->w) - v2->x / (v2->w);
        float dy1 = v1->y / (v1->w) - v2->y / (v2->w);
        float dx2 = v3->x / (v3->w) - v2->x / (v2->w);
        float dy2 = v3->y / (v3->w) - v2->y / (v2->w);
        float cross = dx1 * dy2 - dy1 * dx2;

        if ((v1->w < 0) ^ (v2->w < 0) ^ (v3->w < 0)) {
            // If one vertex lies behind the eye, negating cross will give the correct result.
            // If all vertices lie behind the eye, the triangle will be rejected anyway.
            cross = -cross;
        }

        // If inverted culling is requested, negate the cross
        // if ((rsp.extra_geometry_mode & G_EX_INVERT_CULLING) == 1) {
        //     cross = -cross;
        // }

        switch (rsp.geometry_mode & G_CULL_BOTH) {
            case G_CULL_FRONT:
                if (cross <= 0) {
                    d75d_note_rej(videoGetFrameCount(), 1);
                    return;
                }
                break;
            case G_CULL_BACK:
                if (cross >= 0) {
                    d75d_note_rej(videoGetFrameCount(), 1);
                    return;
                }
                break;
            case G_CULL_BOTH:
                // Why is this even an option?
                d75d_note_rej(videoGetFrameCount(), 1);
                return;
        }
    }

    bool depth_test = ((rsp.geometry_mode & G_ZBUFFER) == G_ZBUFFER || (rdp.other_mode_l & G_ZS_PRIM) == G_ZS_PRIM) &&
                      ((rdp.other_mode_h & G_CYC_1CYCLE) == G_CYC_1CYCLE || (rdp.other_mode_h & G_CYC_2CYCLE) == G_CYC_2CYCLE);
    bool depth_update = (rdp.other_mode_l & Z_UPD) == Z_UPD;
    bool depth_compare = (rdp.other_mode_l & Z_CMP) == Z_CMP;
    bool depth_source_prim = (rdp.other_mode_l & G_ZS_PRIM) == G_ZS_PRIM /* && gDP.primDepth.z == 1.0f */;
    uint16_t zmode = rdp.other_mode_l & ZMODE_DEC;
    uint8_t depth_mode = (depth_test ? 1 : 0) | (depth_update ? 2 : 0) | (depth_compare ? 4 : 0) | (depth_source_prim ? 8 : 0) | (zmode >> 6);

    if (depth_mode != rendering_state.depth_mode) {
        gfx_flush();
        gfx_rapi->set_depth_mode(depth_test, depth_update, depth_compare, depth_source_prim, zmode);
        rendering_state.depth_mode = depth_mode;
    }

    if (rdp.viewport_or_scissor_changed) {
        if (memcmp(&rdp.viewport, &rendering_state.viewport, sizeof(rdp.viewport)) != 0) {
            gfx_flush();
            gfx_rapi->set_viewport(rdp.viewport.x, rdp.viewport.y, rdp.viewport.width, rdp.viewport.height);
            rendering_state.viewport = rdp.viewport;
        }
        if (memcmp(&rdp.scissor, &rendering_state.scissor, sizeof(rdp.scissor)) != 0) {
            gfx_flush();
            gfx_rapi->set_scissor(rdp.scissor.x, rdp.scissor.y, rdp.scissor.width, rdp.scissor.height);
            rendering_state.scissor = rdp.scissor;
        }
        rdp.viewport_or_scissor_changed = false;
    }

    uint64_t cc_options = 0;
    bool use_alpha =
        (rdp.other_mode_l & (3 << 20)) == (G_BL_CLR_MEM << 20) && (rdp.other_mode_l & (3 << 16)) == (G_BL_1MA << 16);
    const bool use_fog = ((rdp.other_mode_l >> 30) == G_BL_CLR_FOG) || ((rdp.other_mode_l >> 26) == G_BL_A_FOG);
    /* D266: G_AC_DECAL (alphacompare == 2) never discards, so it must not
     * take the texedge path at all; NONE/THRESHOLD/DITHER are distinguished
     * in gfx_opengl.cpp via the existing options. */
    const bool ac_decal = (rdp.other_mode_l & (3U << G_MDSFT_ALPHACOMPARE)) == (2U << G_MDSFT_ALPHACOMPARE);
    const bool texture_edge = (rdp.other_mode_l & CVG_X_ALPHA) == CVG_X_ALPHA && !ac_decal;
    const bool use_noise = (rdp.other_mode_l & (3U << G_MDSFT_ALPHACOMPARE)) == G_AC_DITHER;
    const bool use_2cyc = (rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_2CYCLE;
    const bool alpha_threshold = (rdp.other_mode_l & (3U << G_MDSFT_ALPHACOMPARE)) == G_AC_THRESHOLD;
    const bool invisible = (rdp.other_mode_l & (3 << 24)) == (G_BL_0 << 24) && (rdp.other_mode_l & (3 << 20)) == (G_BL_CLR_MEM << 20);
    const bool use_grayscale = rdp.grayscale;
    const bool use_modulate = use_alpha && (rsp.extra_geometry_mode & G_MODULATE_EXT) != 0;
    const bool use_blur = (rdp.other_mode_h & (3U << G_MDSFT_TEXTFILT)) == G_TF_BLUR_EXT;

    if ((rdp.other_mode_l & CVG_X_ALPHA) == CVG_X_ALPHA) {
        use_alpha = true;
    }

    if (use_alpha) {
        cc_options |= (uint64_t)SHADER_OPT_ALPHA;
    }
    if (use_fog) {
        cc_options |= (uint64_t)SHADER_OPT_FOG;
    }
    if (texture_edge) {
        cc_options |= (uint64_t)SHADER_OPT_TEXTURE_EDGE;
    }
    if (use_noise) {
        cc_options |= (uint64_t)SHADER_OPT_NOISE;
    }
    if (use_2cyc) {
        cc_options |= (uint64_t)SHADER_OPT_2CYC;
    }
    if (alpha_threshold) {
        cc_options |= (uint64_t)SHADER_OPT_ALPHA_THRESHOLD;
    }
    if (invisible) {
        cc_options |= (uint64_t)SHADER_OPT_INVISIBLE;
    }
    if (use_grayscale) {
        cc_options |= (uint64_t)SHADER_OPT_GRAYSCALE;
    }
    if (use_blur) {
        cc_options |= (uint64_t)SHADER_OPT_BLUR;
    }

    // If we are not using alpha, clear the alpha components of the combiner as they have no effect
    if (!use_alpha) {
        cc_options &= ~((0xfff << 16) | ((uint64_t)0xfff << 44));
    }

    ColorCombinerKey key;
    key.combine_mode = rdp.combine_mode;
    key.options = cc_options;

    ColorCombiner* comb = gfx_lookup_or_create_color_combiner(key);

    uint32_t tm = 0;
    uint32_t tex_width[2], tex_height[2], tex_width2[2], tex_height2[2];

    /* D74 (Video.WrapFix): per-texunit pre-wrap window. N64 wraps a render
     * tile's UVs at the TILE period (uls/ult + lrs/lrt window) when the tile
     * is a sub-region of the uploaded image; GL wraps at the full image size.
     * Computed once per texunit here (needs the ORIGINAL cms/cmt, before the
     * CLAMP-clearing below) and applied per vertex in the loop. Opt-in --
     * this path never ran before (the old guard was `cms & G_TX_WRAP` ==
     * `& 0`), so it is new behaviour. */
    bool  wrap_s[2] = { false, false }, wrap_t[2] = { false, false };
    float wrap_tw[2] = { 0, 0 }, wrap_th[2] = { 0, 0 };
    float wrap_uls[2] = { 0, 0 }, wrap_ult[2] = { 0, 0 };

    for (int i = 0; i < 2; i++) {
        // TODO: fix this; for now just ignore smaller mips
        const uint32_t tile = rdp.first_tile_index + gfx_lod_tile_offset(i);
        if (comb->used_textures[i]) {
            if (rdp.textures_changed[i]) {
                gfx_flush();
                import_texture(i, tile, false);
                rdp.textures_changed[i] = false;
            }

            uint8_t cms = rdp.texture_tile[tile].cms;
            uint8_t cmt = rdp.texture_tile[tile].cmt;

            uint32_t tex_size_bytes = rdp.loaded_texture[rdp.texture_tile[tile].tmem].orig_size_bytes;
            uint32_t line_size = rdp.texture_tile[tile].line_size_bytes;

            if (line_size == 0) {
                line_size = 1;
            }

            tex_height[i] = tex_size_bytes / line_size;
            switch (rdp.texture_tile[tile].siz) {
                case G_IM_SIZ_4b:
                    line_size <<= 1;
                    break;
                case G_IM_SIZ_8b:
                    break;
                case G_IM_SIZ_16b:
                    line_size /= G_IM_SIZ_16b_LINE_BYTES;
                    break;
                case G_IM_SIZ_32b:
                    line_size /= G_IM_SIZ_32b_LINE_BYTES; // this is 2!
                    tex_height[i] /= 2;
                    break;
            }
            tex_width[i] = line_size;
            /* D245 (M-201): D229 water (RGBA16 tile over a CI8 load) is
             * imported as CI8 -- one byte per texel, base level only -- so the
             * UV normalisation must use that geometry: width = line bytes
             * (32, not 32/2 = 16, which doubled the S frequency) and height =
             * the mask period (32, not 1400/32 = 43 rows of mip chain). */
            {
                static int d245_oldtex2 = -1;
                if (d245_oldtex2 < 0) d245_oldtex2 = getenv("GE_D245_OLDTEX") != NULL;
                const auto& ltx = rdp.loaded_texture[rdp.texture_tile[tile].tmem];
                if (!d245_oldtex2 && rdp.texture_tile[tile].fmt == G_IM_FMT_RGBA &&
                    rdp.texture_tile[tile].siz == G_IM_SIZ_16b && ltx.src_fmt == G_IM_FMT_CI) {
                    const uint8_t mkt = rdp.texture_tile[tile].maskt;
                    tex_width[i] = rdp.texture_tile[tile].line_size_bytes;
                    if (mkt > 0 && mkt < 12 && tex_height[i] > (1u << mkt)) tex_height[i] = 1u << mkt;
                }
            }

            tex_width2[i] = (rdp.texture_tile[tile].lrs - rdp.texture_tile[tile].uls + 4) / 4;
            tex_height2[i] = (rdp.texture_tile[tile].lrt - rdp.texture_tile[tile].ult + 4) / 4;

            uint32_t tex_width1 = tex_width[i] << (cms & G_TX_MIRROR);
            uint32_t tex_height1 = tex_height[i] << (cmt & G_TX_MIRROR);

            if (g_wrap_fix && !(cms & G_TX_MIRROR)) {
                /* (a) sub-tile window: wrap tile (clamp bit not set) whose
                 * uls..lrs window is smaller than the uploaded image -> pre-fmod
                 * the UVs at the window size. */
                if (!(cms & G_TX_CLAMP) && tex_width2[i] > 0 && tex_width2[i] < tex_width[i]) {
                    wrap_s[i]   = true;
                    wrap_tw[i]  = (float)tex_width2[i];
                    wrap_uls[i] = rdp.texture_tile[tile].uls / 4.0f;
                }
                if (!(cmt & G_TX_CLAMP) && tex_height2[i] > 0 && tex_height2[i] < tex_height[i]) {
                    wrap_t[i]   = true;
                    wrap_th[i]  = (float)tex_height2[i];
                    wrap_ult[i] = rdp.texture_tile[tile].ult / 4.0f;
                }
                /* (b) RC3 non-power-of-two wrap period: the N64 RDP masks the
                 * texel coordinate at 1<<mask (GE sets mask = ceil(log2(dim)),
                 * texDimensionToMask), so a non-PoT tile repeats at the NEXT
                 * power of two, not at its image size the way GL GL_REPEAT does.
                 * Fold the UV at the N64 period; the [dim, 1<<mask) overflow
                 * band (TMEM smear on console) is clamped to the last texel so
                 * it reads as an edge streak instead of a bogus early repeat. */
                {
                    uint8_t mks = rdp.texture_tile[tile].masks;
                    uint8_t mkt = rdp.texture_tile[tile].maskt;
                    if (!wrap_s[i] && !(cms & G_TX_CLAMP) && mks >= 1 && mks <= 14) {
                        float period = (float)(1u << mks);
                        if (period != (float)tex_width[i] && tex_width[i] > 0) {
                            wrap_s[i]   = true;
                            wrap_tw[i]  = period;
                            wrap_uls[i] = 0.0f;
                        }
                    }
                    if (!wrap_t[i] && !(cmt & G_TX_CLAMP) && mkt >= 1 && mkt <= 14) {
                        float period = (float)(1u << mkt);
                        if (period != (float)tex_height[i] && tex_height[i] > 0) {
                            wrap_t[i]   = true;
                            wrap_th[i]  = period;
                            wrap_ult[i] = 0.0f;
                        }
                    }
                }
            }

            if ((cms & G_TX_CLAMP) && ((cms & G_TX_MIRROR) || tex_width1 != tex_width2[i])) {
                tm |= 1 << 2 * i;
                cms &= ~G_TX_CLAMP;
            }
            if ((cmt & G_TX_CLAMP) && ((cmt & G_TX_MIRROR) || tex_height1 != tex_height2[i])) {
                tm |= 1 << (2 * i + 1);
                cmt &= ~G_TX_CLAMP;
            }

            if (rendering_state.textures[i]) {
                bool linear_filter = (rdp.other_mode_h & (3U << G_MDSFT_TEXTFILT)) != G_TF_POINT;
                if (linear_filter != rendering_state.textures[i]->second.linear_filter ||
                    cms != rendering_state.textures[i]->second.cms || cmt != rendering_state.textures[i]->second.cmt) {
                    gfx_flush();
                    gfx_rapi->set_sampler_parameters(i, linear_filter, cms, cmt, rdp.tex_lod);
                    rendering_state.textures[i]->second.linear_filter = linear_filter;
                    rendering_state.textures[i]->second.cms = cms;
                    rendering_state.textures[i]->second.cmt = cmt;
                }
            }

#ifdef PORT
            /* D229 probe (env-gated, inert): green/pulsating IsWater water.
             * The water quad is a 2-cycle LERP(TEXEL1, TEXEL0) draw binding
             * BOTH tiles to the same TMEM with tile 1 offset by uls/ult --
             * the D172 probe's `tmem differs` filter never fires for it. Dump
             * each texunit's full decoded tile state + wrap decision + GL
             * texture identity so one Frigate capture pins whether TEXEL1
             * resolves to the same image/region as TEXEL0 or drifts into
             * other TMEM content (the green). */
            static int ge_d229_b = -1;
            if (ge_d229_b < 0) ge_d229_b = getenv("GE_D229") != NULL;
            if (ge_d229_b && use_2cyc && comb->used_textures[0] && comb->used_textures[1]) {
                static int d229x = 0;
                static int d229_total = 0;
                d229_total++;
                if (d229x < 12) {
                    d229x++;
                    const uint32_t tmem = rdp.texture_tile[tile].tmem;
                    sysLogPrintf(LOG_NOTE,
                        "D229: texunit%d first=%u lodoff=%u tile=%u tmem=%u fmt=%u siz=%u | "
                        "uls=%u ult=%u lrs=%u lrt=%u masks=%u maskt=%u shifts=%d shiftt=%d cms=%u cmt=%u | "
                        "texw=%u texh=%u texw2=%u texh2=%u | "
                        "wrapS=%d(tw=%.1f,uls=%.1f) wrapT=%d(th=%.1f,ult=%.1f) | "
                        "tmembytes=%u GLaddr=%p texid=%u glbytes=%u same_tmem_as_other=%d",
                        i, rdp.first_tile_index, gfx_lod_tile_offset(i), tile, tmem,
                        rdp.texture_tile[tile].fmt, rdp.texture_tile[tile].siz,
                        rdp.texture_tile[tile].uls, rdp.texture_tile[tile].ult,
                        rdp.texture_tile[tile].lrs, rdp.texture_tile[tile].lrt,
                        rdp.texture_tile[tile].masks, rdp.texture_tile[tile].maskt,
                        (int)rdp.texture_tile[tile].shifts, (int)rdp.texture_tile[tile].shiftt,
                        (unsigned)cms, (unsigned)cmt,
                        tex_width[i], tex_height[i], tex_width2[i], tex_height2[i],
                        (int)wrap_s[i], wrap_tw[i], wrap_uls[i],
                        (int)wrap_t[i], wrap_th[i], wrap_ult[i],
                        rdp.loaded_texture[tmem].orig_size_bytes,
                        rendering_state.textures[i] ? (const void*)rendering_state.textures[i]->first.texture_addr : nullptr,
                        rendering_state.textures[i] ? rendering_state.textures[i]->second.texture_id : 0u,
                        rendering_state.textures[i] ? rendering_state.textures[i]->first.size_bytes : 0u,
                        (int)(rdp.texture_tile[rdp.first_tile_index + gfx_lod_tile_offset(1 - i)].tmem == tmem));
                } else if (d229_total % 400 == 0) {
                    sysLogPrintf(LOG_NOTE, "D229: (summary) %d dual-texunit 2-cycle tris so far", d229_total);
                }
            }
#endif
        }
    }

#ifdef PORT
    /* D172 probe (env-gated, inert): the magenta/cyan blood/spark bug. The
     * particle records (assets/oddtextures.c globalDL_0x078..) draw a 2-cycle
     * G_CC_INTERFERENCE combine (TEXEL0*TEXEL1) binding two tiles of two
     * formats - tile 0 IA8 smoke, tile 1 RGBA16 fire @ tmem 0x188. Dump both
     * texunits' tile state whenever a 2-cycle tri actually consumes TEXEL1, so
     * one Silo capture pins whether TEXEL1 resolves to the right tmem/format. */
    static int ge_d172_a = -1;
    if (ge_d172_a < 0) ge_d172_a = getenv("GE_D172") != NULL;
    if (ge_d172_a && use_2cyc && comb->used_textures[1] && !rdp.tex_lod) {
        const uint32_t fi = rdp.first_tile_index;
        /* the tile fast3d will actually SAMPLE for each texunit */
        const uint32_t s0 = fi + gfx_lod_tile_offset(0);
        const uint32_t s1 = fi + gfx_lod_tile_offset(1);
        /* the tile the DL actually CONFIGURED for texunit 1 */
        const uint32_t c1 = fi + 1;
        /* only interesting when the DL set up a genuinely distinct 2nd tile
         * (different tmem) -- filters out mip/LOD-bilerp false positives */
        if (rdp.texture_tile[c1].tmem != rdp.texture_tile[fi].tmem) {
            static int d172x = 0;
            static int d172_total = 0;
            /* D219: the original 40-hit cap (sized for M-90's short scripted
             * repro) silently went dark for the rest of a real play session
             * after ~40 ordinary bullet-impact particles, well before any
             * actual explosion -- and the silence itself was indistinguishable
             * from "no more particle draws happened at all". Never suppress
             * the interesting (lit) case, and keep an always-on counter with
             * periodic summaries so a long session still proves whether this
             * code path is being hit throughout, not just early on. */
            const bool lit = (rsp.geometry_mode & G_LIGHTING) != 0;
            d172_total++;
            if (!lit && d172x >= 40) {
                if (d172_total % 200 == 0) {
                    sysLogPrintf(LOG_NOTE, "D172: (summary) %d multitex tris seen so far, still LIGHTING=off", d172_total);
                }
            } else {
                d172x++;
                sysLogPrintf(LOG_NOTE,
                    "D172: multitex tri combine=%llx tex_lod=%d first=%u | "
                    "cfg1[tile=%u tmem=%u fmt=%u siz=%u] | "
                    "SAMPLED0=tile%u(tmem=%u) SAMPLED1=tile%u(tmem=%u fmt=%u)%s | "
                    /* D219 (docs/dev/findings.md): explosion.c never clears
                     * G_LIGHTING before drawing these particle billboards -
                     * it relies on whatever last set it. If lighting is ON
                     * here, cn[]'s authored RGBA tint is misread as a normal
                     * vector (gfx_pc.cpp:1301-1346) and SHADE becomes a
                     * scene-light color instead of red/orange, which would
                     * explain a state-dependent purple/blue that a synthetic
                     * scripted repro (M-90) might not reproduce. */
                    "geometry_mode=%08x LIGHTING=%s | "
                    /* D219 round 2: G_LIGHTING is ruled out (5000/5000
                     * samples off, live-confirmed still purple/blue). Next
                     * suspect: a D217-style texture-cache collision -- the
                     * cache key is {texture_addr, fmt, siz, size_bytes,
                     * palette_hash} (gfx_pc.h) with no content hash for
                     * non-CI textures, so if two of the 15 FIRE_N images
                     * ever resolve to the same address (Globalimagetable
                     * fixup aliasing) or the cache evicts/reuses a texture_id
                     * without a real re-upload, this tile's GL texture could
                     * be serving stale/wrong-image content while every
                     * bookkeeping field above still looks correct. */
                    "TEXEL1_addr=%p TEXEL1_texid=%u TEXEL1_bytes=%u",
                    (unsigned long long)rdp.combine_mode, (int)rdp.tex_lod, fi,
                    c1, rdp.texture_tile[c1].tmem, rdp.texture_tile[c1].fmt, rdp.texture_tile[c1].siz,
                    s0, rdp.texture_tile[s0].tmem,
                    s1, rdp.texture_tile[s1].tmem, rdp.texture_tile[s1].fmt,
                    (s1 == s0) ? "  <-- TEXEL1 == TEXEL0 (BUG)" : "",
                    rsp.geometry_mode,
                    (rsp.geometry_mode & G_LIGHTING) ? "ON <-- D219 SUSPECT" : "off",
                    rendering_state.textures[1] ? (const void*)rendering_state.textures[1]->first.texture_addr : nullptr,
                    rendering_state.textures[1] ? rendering_state.textures[1]->second.texture_id : 0u,
                    rendering_state.textures[1] ? rendering_state.textures[1]->first.size_bytes : 0u);
            }
        }
    }
#endif

    struct ShaderProgram* prg = comb->prg[tm];
    if (prg == NULL) {
        comb->prg[tm] = prg =
            gfx_lookup_or_create_shader_program(comb->shader_id0, comb->shader_id1 | (tm * SHADER_OPT_TEXEL0_CLAMP_S));
    }
    if (prg != rendering_state.shader_program) {
        gfx_flush();
        gfx_rapi->unload_shader(rendering_state.shader_program);
        gfx_rapi->load_shader(prg);
        rendering_state.shader_program = prg;
    }
    if (use_alpha != rendering_state.alpha_blend || use_modulate != rendering_state.modulate) {
        gfx_flush();
        gfx_rapi->set_use_alpha(use_alpha, use_modulate);
        rendering_state.alpha_blend = use_alpha;
        rendering_state.modulate = use_modulate;
    }
    uint8_t num_inputs;
    bool used_textures[2];

    gfx_rapi->shader_get_info(prg, &num_inputs, used_textures);

    struct GfxClipParameters clip_parameters = gfx_rapi->get_clip_parameters();

    d75d_note_emit(videoGetFrameCount(), v_arr); // survived all rejection gates -> reaches GL
    d303_note_emit(videoGetFrameCount(), v_arr); // TEMP D303
    zf_note_emit(videoGetFrameCount(), v_arr);   // TEMP D306/D308
    /* TEMP D306 (M-201): depth-state census of emitted triangles in a frame
     * window, GE_D306C="f0 f1". One line per tri: depth flags as sent to GL,
     * geometry_mode, other_mode_l, tile-0 texture, NDC centroid, z/w range and
     * min w (<=0 = crosses the eye plane). Cap 60000. */
    {
        static int d306c = -1; static uint32_t d306f0 = 0, d306f1 = 0, d306n = 0; static float d306wmax = 1e30f;
        if (d306c < 0) {
            const char* e = getenv("GE_D306C");
            d306c = (e && sscanf(e, "%u %u %f", &d306f0, &d306f1, &d306wmax) >= 2) ? 1 : 0;
        }
        const uint32_t fc = videoGetFrameCount();
        if (d306c && fc >= d306f0 && fc <= d306f1 && d306n < 60000) {
            float cx = 0, cy = 0, zmin = 1e9f, zmax = -1e9f, wmin = 1e9f;
            for (int i = 0; i < 3; i++) {
                const float w = v_arr[i]->w;
                if (w < wmin) wmin = w;
                if (w > 0.f) {
                    const float zn = v_arr[i]->z / w;
                    cx += v_arr[i]->x / w; cy += v_arr[i]->y / w;
                    if (zn < zmin) zmin = zn;
                    if (zn > zmax) zmax = zn;
                }
            }
            cx /= 3.f; cy /= 3.f;
            if (wmin > d306wmax) goto d306c_skip; /* optional 3rd arg: near geometry only */
            {
            const void* t0 = rendering_state.textures[0] ? (const void*)rendering_state.textures[0]->first.texture_addr : nullptr;
            d306n++;
            fprintf(stderr, "D306C f=%u dm=%02x geom=%08x oml=%08x tex0=%p c=(%.3f,%.3f) z=[%.5f,%.5f] wmin=%.2f\n",
                    fc, rendering_state.depth_mode, rsp.geometry_mode, rdp.other_mode_l, t0, cx, cy, zmin, zmax, wmin);
            }
        }
        d306c_skip:;
    }
    d75d_note_emit_z(videoGetFrameCount(), v_arr, (used_textures[0] || used_textures[1]) ? 1 : 0, comb->used_textures[0] ? 1 : 0);
    for (int i = 0; i < 3; i++) {
        float z = v_arr[i]->z, w = v_arr[i]->w;
        if (clip_parameters.z_is_from_0_to_1) {
            z = (z + w) / 2.0f;
        }

        buf_vbo[buf_vbo_len++] = v_arr[i]->x;
        buf_vbo[buf_vbo_len++] = clip_parameters.invert_y ? -v_arr[i]->y : v_arr[i]->y;
        buf_vbo[buf_vbo_len++] = z;
        buf_vbo[buf_vbo_len++] = w;

        for (int t = 0; t < 2; t++) {
            if (!used_textures[t]) {
                continue;
            }

            // TODO: fix this; for now just ignore smaller mips
            const uint32_t tile = gfx_lod_tile_offset(t);

            float u = v_arr[i]->u / 32.0f;
            float v = v_arr[i]->v / 32.0f;

            int shifts = rdp.texture_tile[rdp.first_tile_index + tile].shifts;
            int shiftt = rdp.texture_tile[rdp.first_tile_index + tile].shiftt;
            if (shifts != 0) {
                if (shifts <= 10) {
                    u /= 1 << shifts;
                } else {
                    u *= 1 << (16 - shifts);
                }
            }
            if (shiftt != 0) {
                if (shiftt <= 10) {
                    v /= 1 << shiftt;
                } else {
                    v *= 1 << (16 - shiftt);
                }
            }

            u -= rdp.texture_tile[rdp.first_tile_index + tile].uls / 4.0f;
            v -= rdp.texture_tile[rdp.first_tile_index + tile].ult / 4.0f;

            // D74 (Video.WrapFix, opt-in): pre-wrap UVs at the tile-window
            // period when the render tile is a sub-region of the uploaded
            // image. Flags/sizes are precomputed per texunit above (indexed by
            // `t`, not the vertex `i`). The old in-place version was inert
            // (guard `cms & G_TX_WRAP` == `& 0`) and OOB (`tex_width2[i]` with
            // `i` = vertex). See SMALL-FIXES B1.
            if (wrap_s[t]) {
                u = fmodf(u, wrap_tw[t]);
                if (u < 0.0f) u += wrap_tw[t];
                u += wrap_uls[t];
                /* RC3: when the N64 wrap period (wrap_tw) exceeds the uploaded
                 * image width, the [width, period) band has no real texels --
                 * clamp it to the edge rather than let GL_REPEAT restart the
                 * image early. Never triggers for the sub-tile-window case
                 * (wrap_tw + wrap_uls stay within tex_width). */
                if (tex_width[t] > 0 && u > (float)tex_width[t]) u = (float)tex_width[t] - 0.5f;
            }
            if (wrap_t[t]) {
                v = fmodf(v, wrap_th[t]);
                if (v < 0.0f) v += wrap_th[t];
                v += wrap_ult[t];
                if (tex_height[t] > 0 && v > (float)tex_height[t]) v = (float)tex_height[t] - 0.5f;
            }

            if (!is_rect) {
                if (!(rdp.other_mode_h & G_TP_PERSP)) {
                    u *= 0.5f;
                    v *= 0.5f;
                }

                if ((rdp.other_mode_h & (3U << G_MDSFT_TEXTFILT)) != G_TF_POINT) {
                    // Linear filter adds 0.5f to the coordinates
                    u += 0.5f;
                    v += 0.5f;
                }
            }

            buf_vbo[buf_vbo_len++] = u / tex_width[t];
            buf_vbo[buf_vbo_len++] = v / tex_height[t];

#ifdef PORT
            {
                static int ge_d116 = -1;
                if (ge_d116 < 0) ge_d116 = getenv("GE_D116") ? 1 : 0;
                if (ge_d116 && is_rect && t == 0 && tex_width[t] > 0 && tex_width[t] <= 32) {
                    fprintf(stderr,
                        "[D116/vbo] rect vtx%d  x=%.4f y=%.4f  u=%.4f v=%.4f  "
                        "(raw v->u=%.1f  texw=%.0f texw2=%u  tile.uls=%d lrs=%d cms=%d)\n",
                        i, v_arr[i]->x, v_arr[i]->y, u / tex_width[t], v / tex_height[t],
                        (double)v_arr[i]->u, (double)tex_width[t], (unsigned)tex_width2[i],
                        (int)rdp.texture_tile[rdp.first_tile_index + tile].uls,
                        (int)rdp.texture_tile[rdp.first_tile_index + tile].lrs,
                        (int)rdp.texture_tile[rdp.first_tile_index + tile].cms);
                }
            }
#endif

            bool clampS = tm & (1 << 2 * t);
            bool clampT = tm & (1 << (2 * t + 1));

            if (clampS) {
                buf_vbo[buf_vbo_len++] = (tex_width2[t] - 0.5f) / tex_width[t];
            }
            if (clampT) {
                buf_vbo[buf_vbo_len++] = (tex_height2[t] - 0.5f) / tex_height[t];
            }
        }

        if (use_fog) {
            buf_vbo[buf_vbo_len++] = rdp.fog_color.r / 255.0f;
            buf_vbo[buf_vbo_len++] = rdp.fog_color.g / 255.0f;
            buf_vbo[buf_vbo_len++] = rdp.fog_color.b / 255.0f;
            buf_vbo[buf_vbo_len++] = v_arr[i]->fog / 255.0f; // fog factor
        }

        if (use_grayscale) {
            buf_vbo[buf_vbo_len++] = rdp.grayscale_color.r / 255.0f;
            buf_vbo[buf_vbo_len++] = rdp.grayscale_color.g / 255.0f;
            buf_vbo[buf_vbo_len++] = rdp.grayscale_color.b / 255.0f;
            buf_vbo[buf_vbo_len++] = rdp.grayscale_color.a / 255.0f; // lerp interpolation factor (not alpha)
        }

        for (int j = 0; j < num_inputs; j++) {
            struct RGBA* color = 0;
            struct RGBA tmp = { 0 };
            for (int k = 0; k < 1 + (use_alpha ? 1 : 0); k++) {
                switch (comb->shader_input_mapping[k][j]) {
                        // Note: CCMUX constants and ACMUX constants used here have same value, which is why this works
                        // (except LOD fraction).
                    case G_CCMUX_PRIMITIVE:
                        color = &rdp.prim_color;
                        break;
                    case G_CCMUX_SHADE:
                        color = &v_arr[i]->color;
                        break;
                    case G_CCMUX_SHADE_ALPHA:
                        tmp.r = tmp.g = tmp.b = v_arr[i]->color.a;
                        color = &tmp;
                        break;
                    case G_CCMUX_ENVIRONMENT:
                        color = &rdp.env_color;
                        break;
                    case G_CCMUX_PRIMITIVE_ALPHA: {
                        tmp.r = tmp.g = tmp.b = rdp.prim_color.a;
                        color = &tmp;
                        break;
                    }
                    case G_CCMUX_ENV_ALPHA: {
                        tmp.r = tmp.g = tmp.b = rdp.env_color.a;
                        color = &tmp;
                        break;
                    }
                    case G_CCMUX_PRIM_LOD_FRAC: {
                        tmp.r = tmp.g = tmp.b = rdp.prim_lod_fraction;
                        color = &tmp;
                        break;
                    }
                    case G_CCMUX_LOD_FRACTION: {
                        if (rdp.other_mode_h & G_TL_LOD) {
                            // HACK: very roughly eyeballed based on the carpets in Defection
                            // this is actually supposed to be calculated per pixel
                            const float distance_frac = std::max(0.f, std::min(w / 1024.f, 1.f));
                            tmp.r = tmp.g = tmp.b = tmp.a = (0.7f + distance_frac * 0.3f) * 255.f;
                        } else {
                            tmp.r = tmp.g = tmp.b = tmp.a = 255;
                        }
                        color = &tmp;
                        break;
                    }
                    case G_ACMUX_PRIM_LOD_FRAC:
                        tmp.a = rdp.prim_lod_fraction;
                        color = &tmp;
                        break;
                    default:
                        memset(&tmp, 0, sizeof(tmp));
                        color = &tmp;
                        break;
                }
                if (k == 0) {
                    buf_vbo[buf_vbo_len++] = color->r / 255.0f;
                    buf_vbo[buf_vbo_len++] = color->g / 255.0f;
                    buf_vbo[buf_vbo_len++] = color->b / 255.0f;
                } else {
                    buf_vbo[buf_vbo_len++] = color->a / 255.0f;
                }
            }
        }
    }

    if (++buf_vbo_num_tris == MAX_BUFFERED) {
        gfx_flush();
    }
}

static inline void gfx_sp_tri4(Gfx *cmd) {
    // the game issues gSPTri2 for quads, which uses G_TRI4 with 2 empty triangles
    uint8_t x = C1(0, 4);
    uint8_t y = C1(4, 4);
    uint8_t z = C0(0, 4);

    if(x || y || z) {
        gfx_sp_tri1(x, y, z, false);
    }

    x = C1(8, 4);
    y = C1(12, 4);
    z = C0(4, 4);

    if (x || y || z) {
        gfx_sp_tri1(x, y, z, false);
    }

    x = C1(16, 4);
    y = C1(20, 4);
    z = C0(8, 4);

    if (x || y || z) {
        gfx_sp_tri1(x, y, z, false);
    }

    x = C1(24, 4);
    y = C1(28, 4);
    z = C0(12, 4);

    if (x || y || z) {
        gfx_sp_tri1(x, y, z, false);
    }
}

static void gfx_sp_geometry_mode(uint32_t clear, uint32_t set) {
    rsp.geometry_mode &= ~clear;
    rsp.geometry_mode |= set;
}

static inline void gfx_update_aspect_mode(void) {
    const uint32_t side = rsp.aspect_mode & G_ASPECT_CENTER_EXT;

    rsp.aspect_scale = rsp.aspect_mode ? gfx_current_native_aspect : gfx_current_window_dimensions.aspect_ratio;

    if (side == G_ASPECT_LEFT_EXT) {
        rsp.aspect_ofs = 1.f - gfx_current_dimensions.aspect_ratio / gfx_current_native_aspect;
    } else if (side == G_ASPECT_RIGHT_EXT) {
        rsp.aspect_ofs = gfx_current_dimensions.aspect_ratio / gfx_current_native_aspect - 1.f;
    } else {
        rsp.aspect_ofs = 0.f;
    }

    if (side && (rsp.aspect_mode & G_ASPECT_WIDE_EXT)) {
        constexpr float c = 16.f / 9.f;
        if (gfx_current_dimensions.aspect_ratio > c) {
            rsp.aspect_ofs *= c / gfx_current_dimensions.aspect_ratio;
        }
    }
}

static void gfx_sp_extra_geometry_mode(uint32_t clear, uint32_t set) {
    rsp.extra_geometry_mode &= ~clear;
    rsp.extra_geometry_mode |= set;
    rsp.aspect_mode = (rsp.extra_geometry_mode & G_ASPECT_MODE_EXT);
    gfx_update_aspect_mode();
}

static void gfx_adjust_viewport_or_scissor(XYWidthHeight* area, bool preserve_aspect = false) {
    // HACK: assume all target framebuffers have the same aspect
    // Use floor/ceil to ensure scissor fully contains the logical region
    // and prevents sub-pixel gaps at viewport edges
    // g_gpSafeTop already plays the exact role SCREEN_HEIGHT plays below
    // (both are the bottom-up Y value of the mapped region's TOP edge) --
    // this reduces to the untouched original formula when crop is off.
    const bool crop = g_safe_area_crop_enabled && g_gpSafeHeight > 0.0f;
    const float safeTop = crop ? g_gpSafeTop : (float)SCREEN_HEIGHT;
    const float safeHeight = crop ? g_gpSafeHeight : (float)SCREEN_HEIGHT;
    const float ratioY = gfx_current_dimensions.height / safeHeight;

    // D246 (findings.md): a consistent, exactly-1-logical-unit gap at the
    // left AND right screen edges (measured empirically: 2px/3px/4px at
    // RATIO_X 2/3/4 -- always exactly 1 unit of the 320-wide logical space,
    // on both edges, symmetric, same underlying cause on every level tested)
    // reveals whatever's drawn behind the foreground scene there (varies by
    // level, e.g. sky/ambient colour) instead of scene content. Root cause
    // not isolated to a specific game-code or fast3d call site despite a
    // deep pass (viewport/scissor math here is provably exact at integer
    // RATIO_X, ruling out a floor/ceil rounding bug) -- treat this as the
    // same class of issue as the vertical safe-area crop above (D247/the
    // TV-overscan margin) and fold it into the same toggle: trim the same
    // fixed 1-unit margin from both edges rather than trying to force
    // content to reach a boundary it may never actually be drawn to.
    const float safeLeft = g_safe_area_crop_enabled ? 1.0f : 0.0f;
    const float safeWidth = g_safe_area_crop_enabled ? (float)SCREEN_WIDTH - 2.0f : (float)SCREEN_WIDTH;
    const float ratioX = gfx_current_dimensions.width / safeWidth;

    float x1 = (area->x - safeLeft) * ratioX;
    float y1 = (safeTop - area->y) * ratioY;
    float x2 = (area->x + area->width - safeLeft) * ratioX;
    float y2 = (safeTop - area->y + area->height) * ratioY;
    
    area->x = std::floor(x1);
    area->y = std::floor(y1);
    area->width = std::ceil(x2) - area->x;
    area->height = std::ceil(y2) - area->y;
    
    if (preserve_aspect) {
        // preserve native aspect ratio
        const float ratio = gfx_current_native_aspect / gfx_current_dimensions.aspect_ratio;
        const float midx = gfx_current_dimensions.width * 0.5f;
        area->x = midx + (area->x - midx) * ratio;
        area->x += rsp.aspect_ofs * gfx_current_dimensions.width * 0.5f;
        area->width *= ratio;
    }

    if (!game_renders_to_framebuffer ||
        (gfx_msaa_level > 1 && gfx_current_dimensions.width == gfx_current_game_window_viewport.width &&
            gfx_current_dimensions.height == gfx_current_game_window_viewport.height)) {
        area->x += gfx_current_game_window_viewport.x;
        area->y += gfx_current_window_dimensions.height -
                    (gfx_current_game_window_viewport.y + gfx_current_game_window_viewport.height);
    }
}

/* D316: the on-window pixel rect the full VI canvas (0,0)-(SCREEN_WIDTH,
 * SCREEN_HEIGHT) currently maps to, i.e. exactly the rect gfx_draw_rectangle's
 * default_viewport (below) resolves to once the safe-area crop above is
 * applied. Reuses gfx_adjust_viewport_or_scissor itself (not a hand-derived
 * inverse) so this can never drift from the real forward transform: port-side
 * mouse-to-logical-2D-space mapping (port/src/optionsoverlay.c) needs this to
 * invert clicks correctly when the crop shrinks that mapped rect below the
 * full window (D316 -- the overlay's own click math previously assumed the
 * logical canvas always fills the whole window, which is false whenever the
 * last-set gameplay viewport was inset, e.g. NTSC "Full" removes ~8% top and
 * bottom). Top-left origin, window pixel units -- matches SDL mouse coords
 * and gfx_current_game_window_viewport's documented convention. */
extern "C" void gfx_get_ui_screen_rect(int32_t *outX, int32_t *outY, int32_t *outW, int32_t *outH) {
    struct XYWidthHeight area = { 0, (int16_t)SCREEN_HEIGHT, (uint32_t)SCREEN_WIDTH, (uint32_t)SCREEN_HEIGHT };
    gfx_adjust_viewport_or_scissor(&area, false);
    *outX = area.x;
    *outY = area.y;
    *outW = (int32_t)area.width;
    *outH = (int32_t)area.height;
}

static void gfx_calc_and_set_viewport(const Vp_t* viewport) {
    // 2 bits fraction
    float width = 2.0f * viewport->vscale[0] / 4.0f;
    float height = 2.0f * viewport->vscale[1] / 4.0f;
    float x = (viewport->vtrans[0] / 4.0f) - width / 2.0f;
    float y = ((viewport->vtrans[1] / 4.0f) + height / 2.0f);

    rdp.viewport.x = x;
    rdp.viewport.y = y;
    rdp.viewport.width = width;
    rdp.viewport.height = height;

    /* Cache the raw (pre window-scale) viewport bounds for the safe-area
     * crop above -- guard against a degenerate/zero-height viewport so a
     * later divide can't ever see one. */
    if (height > 1.0f) {
        g_gpSafeTop = y;
        g_gpSafeHeight = height;
    }

    gfx_adjust_viewport_or_scissor(&rdp.viewport);

    rdp.viewport_or_scissor_changed = true;
}

static void gfx_sp_movemem(uint8_t index, uint8_t offset, const void* data) {
    if (!fast3d_ptr_ok(data)) {
        return; /* D146: corrupt DL -> wild pointer */
    }
    switch (index) {
        case G_MV_VIEWPORT:
            gfx_calc_and_set_viewport((const Vp_t*)data);
            break;
        case G_MV_LOOKATY:
        case G_MV_LOOKATX:
            // I think this is only really used for guLookAtReflect
            index = !((index - G_MV_LOOKATY) / 2);
            rsp.lookat[index] = ((const Light *)data)->l;
            rsp.lookat_enabled = (index == 0) || (rsp.lookat[1].dir[0] || rsp.lookat[1].dir[1]);
            rsp.lights_changed = true;
            break;
        case G_MV_L0:
        case G_MV_L1:
        case G_MV_L2:
            // NOTE: reads out of bounds if it is an ambient light
            memcpy(rsp.current_lights + (index - G_MV_L0) / 2, data, sizeof(Light_t));
            break;
    }
}

static void gfx_sp_moveword(uint8_t index, uint16_t offset, uintptr_t data) {
    switch (index) {
        case G_MW_NUMLIGHT:
            // Ambient light is included
            // The 31th bit is a flag that lights should be recalculated
            rsp.current_num_lights = (data - 0x80000000U) / 32;
            rsp.lights_changed = 1;
            break;
        case G_MW_FOG:
            rsp.fog_mul = (int16_t)(data >> 16);
            rsp.fog_offset = (int16_t)data;
            break;
        case G_MW_SEGMENT:
            // GE registers segment bases as OS_K0_TO_PHYSICAL(ptr); store the
            // live host pointer so seg_addr() resolves seg+offset correctly.
            segmentPointers[(offset >> 2) & 0xff] = (data < 0x800000) ? (data + 0x80000000) : data;
            break;
    }
}

static void gfx_sp_texture(uint16_t sc, uint16_t tc, uint8_t level, uint8_t tile, uint8_t on) {
    rsp.texture_scaling_factor.s = sc;
    rsp.texture_scaling_factor.t = tc;
    rdp.tex_max_lod = level;
    if (rdp.first_tile_index != tile) {
        rdp.textures_changed[0] = true;
        rdp.textures_changed[1] = true;
        rdp.first_tile_index = tile;
    }
}

static void gfx_dp_set_scissor(uint32_t mode, uint32_t ulx, uint32_t uly, uint32_t lrx, uint32_t lry) {
    float x = ulx / 4.0f;
    float y = lry / 4.0f;
    float width = (lrx - ulx) / 4.0f;
    float height = (lry - uly) / 4.0f;

    rdp.scissor.x = x;
    rdp.scissor.y = y;
    rdp.scissor.width = width;
    rdp.scissor.height = height;

    gfx_adjust_viewport_or_scissor(&rdp.scissor, rsp.aspect_mode != 0);

    rdp.viewport_or_scissor_changed = true;
}

static void gfx_dp_set_texture_image(uint32_t format, uint32_t size, uint32_t width, uint32_t tex_flags, const void* addr) {
    rdp.texture_to_load.addr = (const uint8_t*)addr;
    rdp.texture_to_load.fmt = (uint8_t)format; /* D229 */
    rdp.texture_to_load.siz = size;
    rdp.texture_to_load.width = width;
    rdp.texture_to_load.tex_flags = tex_flags;
}

static void gfx_dp_set_tile(uint8_t fmt, uint32_t siz, uint32_t line, uint32_t tmem, uint8_t tile, uint32_t palette,
                            uint32_t cmt, uint32_t maskt, uint32_t shiftt, uint32_t cms, uint32_t masks,
                            uint32_t shifts) {
    // OTRTODO:
    // SUPPORT_CHECK(tmem == 0 || tmem == 256);
    static uint32_t max_tmem = 0;
    if (cms == G_TX_WRAP && masks == G_TX_NOMASK) {
        cms = G_TX_CLAMP;
    }
    if (cmt == G_TX_WRAP && maskt == G_TX_NOMASK) {
        cmt = G_TX_CLAMP;
    }

    if (fmt == G_IM_FMT_RGBA && siz < G_IM_SIZ_16b) {
        // HACK: sometimes the game will submit G_IM_FMT_RGBA, G_IM_SIZ_8b/4b, intending it to read as CI8/CI4 with RGBA16 palette
        fmt = G_IM_FMT_CI;
    } else if (fmt == G_IM_FMT_IA && siz == G_IM_SIZ_32b) {
        // HACK: ... and sometimes it submits this, apparently intending it to be I8
        fmt = G_IM_FMT_I;
        siz = G_IM_SIZ_8b;
    }

    rdp.texture_tile[tile].palette = palette; // palette should set upper 4 bits of color index in 4b mode
    rdp.texture_tile[tile].fmt = fmt;
    rdp.texture_tile[tile].siz = siz;
    rdp.texture_tile[tile].cms = cms;
    rdp.texture_tile[tile].cmt = cmt;
    rdp.texture_tile[tile].masks = masks; /* RC3 */
    rdp.texture_tile[tile].maskt = maskt; /* RC3 */
    rdp.texture_tile[tile].shifts = shifts;
    rdp.texture_tile[tile].shiftt = shiftt;
    rdp.texture_tile[tile].line_size_bytes = line * 8;
    rdp.texture_tile[tile].tmem = tmem;

    rdp.textures_changed[0] = true;
    rdp.textures_changed[1] = true;
}

static void gfx_dp_set_tile_size(uint8_t tile, uint16_t uls, uint16_t ult, uint16_t lrs, uint16_t lrt) {
    rdp.texture_tile[tile].uls = uls;
    rdp.texture_tile[tile].ult = ult;
    rdp.texture_tile[tile].lrs = lrs;
    rdp.texture_tile[tile].lrt = lrt;
    rdp.texture_tile[tile].width = (lrs - uls + 4) / 4;
    rdp.texture_tile[tile].height = (lrt - ult + 4) / 4;
    rdp.textures_changed[0] = true;
    rdp.textures_changed[1] = true;
}

static void gfx_dp_load_tlut(uint8_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt) {
    // SUPPORT_CHECK(tile == G_TX_LOADTILE);
    SUPPORT_CHECK(rdp.texture_to_load.siz == G_IM_SIZ_16b);
    SUPPORT_CHECK(rdp.texture_tile[tile].tmem >= 256);

    rdp.texture_tile[tile].uls = uls;
    rdp.texture_tile[tile].ult = ult;
    rdp.texture_tile[tile].lrs = lrs;
    rdp.texture_tile[tile].lrt = lrt;

    const uint32_t width = (lrs - uls + 1);
    const uint32_t height = (lrt - ult + 1);
    const uint32_t pitch = rdp.texture_to_load.width + 1;
    const uint32_t count =  width * height;
    const uint16_t *base = (const uint16_t *)rdp.texture_to_load.addr + pitch * ult + uls;

    if (rdp.texture_tile[tile].tmem == 256) {
        rdp.palette_addrs[0] = (const uint8_t *)base;
        if (count >= 256) {
            rdp.palette_addrs[1] = (const uint8_t *)(base + 128);
        }
    } else {
        rdp.palette_addrs[1] = (const uint8_t *)base;
    }

    const uint32_t palofs = rdp.texture_tile[tile].tmem - 256;
    SUPPORT_CHECK(palofs + count <= 256);

    const uint16_t *src = base;
    uint16_t *dst = rdp.palette + palofs;
    for (uint32_t i = 0; i < count; ++i) {
        *dst++ = PD_BE16(*src++);
    }

    /* D217: refresh the palette-content hash that keys the CI texture cache.
     * GE reissues gDPLoadTLUT from a repeated scratch source address with
     * different content between weapon / character model materials; the CI
     * TextureCacheKey keys on the source *address*, so without a content hash a
     * later material can take a stale cache HIT decoded against an earlier
     * palette. gDPLoadTLUT is rare relative to draws, so hashing the whole
     * 512-byte table here keeps the per-texel path untouched. */
    {
        uint32_t h = 2166136261u;
        const uint8_t *pb = (const uint8_t *)rdp.palette;
        for (uint32_t k = 0; k < sizeof(rdp.palette); ++k) {
            h ^= pb[k];
            h *= 16777619u;
        }
        rdp.palette_hash = h;
    }

    rdp.textures_changed[0] = rdp.textures_changed[1] = true;
}

static void gfx_dp_load_block(uint8_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t dxt) {
    // SUPPORT_CHECK(tile == G_TX_LOADTILE);
    SUPPORT_CHECK(uls == 0);
    SUPPORT_CHECK(ult == 0);

    // The lrs field rather seems to be number of pixels to load
    uint32_t orig_size_bytes = (lrs + 1) << rdp.texture_to_load.siz >> 1;
    uint32_t size_bytes = orig_size_bytes;
    if (rdp.texture_to_load.raw_tex_metadata.h_byte_scale != 1 ||
        rdp.texture_to_load.raw_tex_metadata.v_pixel_scale != 1) {
        size_bytes *= rdp.texture_to_load.raw_tex_metadata.h_byte_scale;
        size_bytes *= rdp.texture_to_load.raw_tex_metadata.v_pixel_scale;
    }

    LoadedTexture& loaded_texture = rdp.loaded_texture[rdp.texture_tile[tile].tmem];
    loaded_texture.orig_size_bytes = orig_size_bytes;
    loaded_texture.size_bytes = size_bytes;
    loaded_texture.full_size_bytes = size_bytes;
    loaded_texture.line_size_bytes = size_bytes;
    loaded_texture.full_image_line_size_bytes = size_bytes;
    loaded_texture.tex_flags = rdp.texture_to_load.tex_flags;
    loaded_texture.raw_tex_metadata = rdp.texture_to_load.raw_tex_metadata;
    loaded_texture.addr = rdp.texture_to_load.addr;
    loaded_texture.src_fmt = rdp.texture_to_load.fmt; /* D229 */

    rdp.textures_changed[0] = rdp.textures_changed[1] = true;
}

static void gfx_dp_load_tile(uint8_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt) {
    SUPPORT_CHECK(tile == G_TX_LOADTILE);

    uint32_t offset_x = uls >> G_TEXTURE_IMAGE_FRAC;
    uint32_t offset_y = ult >> G_TEXTURE_IMAGE_FRAC;
    uint32_t tile_width = ((lrs - uls) >> G_TEXTURE_IMAGE_FRAC) + 1;
    uint32_t tile_height = ((lrt - ult) >> G_TEXTURE_IMAGE_FRAC) + 1;
    uint32_t full_image_width = rdp.texture_to_load.width + 1;

    uint32_t offset_x_in_bytes = offset_x << rdp.texture_to_load.siz >> 1;
    uint32_t tile_line_size_bytes = tile_width << rdp.texture_to_load.siz >> 1;
    uint32_t full_image_line_size_bytes = full_image_width << rdp.texture_to_load.siz >> 1;

    uint32_t orig_size_bytes = tile_line_size_bytes * tile_height;
    uint32_t size_bytes = orig_size_bytes;
    uint32_t start_offset_bytes = full_image_line_size_bytes * offset_y + offset_x_in_bytes;

    float h_byte_scale = rdp.texture_to_load.raw_tex_metadata.h_byte_scale;
    float v_pixel_scale = rdp.texture_to_load.raw_tex_metadata.v_pixel_scale;

    if (h_byte_scale != 1 || v_pixel_scale != 1) {
        start_offset_bytes = h_byte_scale * (v_pixel_scale * offset_y * full_image_line_size_bytes + offset_x_in_bytes);
        size_bytes *= h_byte_scale * v_pixel_scale;
        full_image_line_size_bytes *= h_byte_scale;
        tile_line_size_bytes *= h_byte_scale;
    }

    LoadedTexture& loaded_texture = rdp.loaded_texture[rdp.texture_tile[tile].tmem];
    loaded_texture.orig_size_bytes = orig_size_bytes;
    loaded_texture.size_bytes = size_bytes;
    loaded_texture.full_size_bytes = full_image_line_size_bytes * tile_height;
    loaded_texture.full_image_line_size_bytes = full_image_line_size_bytes;
    loaded_texture.line_size_bytes = tile_line_size_bytes;
    loaded_texture.tex_flags = rdp.texture_to_load.tex_flags;
    loaded_texture.raw_tex_metadata = rdp.texture_to_load.raw_tex_metadata;
    loaded_texture.addr = rdp.texture_to_load.addr + start_offset_bytes;
    loaded_texture.src_fmt = rdp.texture_to_load.fmt; /* D229 */

    rdp.texture_tile[tile].uls = uls;
    rdp.texture_tile[tile].ult = ult;
    rdp.texture_tile[tile].lrs = lrs;
    rdp.texture_tile[tile].lrt = lrt;
    rdp.texture_tile[tile].width = ((lrs - uls) >> G_TEXTURE_IMAGE_FRAC) + 1;
    rdp.texture_tile[tile].height = ((lrt - ult) >> G_TEXTURE_IMAGE_FRAC) + 1;

    rdp.textures_changed[0] = rdp.textures_changed[1] = true;
}

static void gfx_dp_set_combine_mode(uint32_t rgb, uint32_t alpha, uint32_t rgb_cyc2, uint32_t alpha_cyc2) {
    rdp.combine_mode = rgb | (alpha << 16) | ((uint64_t)rgb_cyc2 << 28) | ((uint64_t)alpha_cyc2 << 44);
}

static inline uint32_t color_comb(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a & 0xf) | ((b & 0xf) << 4) | ((c & 0x1f) << 8) | ((d & 7) << 13);
}

static inline uint32_t alpha_comb(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a & 7) | ((b & 7) << 3) | ((c & 7) << 6) | ((d & 7) << 9);
}

static void gfx_dp_set_grayscale_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    rdp.grayscale_color.r = r;
    rdp.grayscale_color.g = g;
    rdp.grayscale_color.b = b;
    rdp.grayscale_color.a = a;
}

static void gfx_dp_set_env_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    rdp.env_color.r = r;
    rdp.env_color.g = g;
    rdp.env_color.b = b;
    rdp.env_color.a = a;
}

static void gfx_dp_set_prim_color(uint8_t m, uint8_t l, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    rdp.prim_lod_fraction = l;
    rdp.prim_color.r = r;
    rdp.prim_color.g = g;
    rdp.prim_color.b = b;
    rdp.prim_color.a = a;
    rdp.fill_color.r = r;
    rdp.fill_color.g = g;
    rdp.fill_color.b = b;
    rdp.fill_color.a = a;
    rdp.tex_min_lod = m;

}

static void gfx_dp_set_fog_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    rdp.fog_color.r = r;
    rdp.fog_color.g = g;
    rdp.fog_color.b = b;
    rdp.fog_color.a = a;
}

static void gfx_dp_set_fill_color(uint32_t packed_color) {
    uint16_t col16 = (uint16_t)packed_color;
    uint32_t r = col16 >> 11;
    uint32_t g = (col16 >> 6) & 0x1f;
    uint32_t b = (col16 >> 1) & 0x1f;
    uint32_t a = col16 & 1;
    rdp.fill_color.r = SCALE_5_8(r);
    rdp.fill_color.g = SCALE_5_8(g);
    rdp.fill_color.b = SCALE_5_8(b);
    rdp.fill_color.a = a * 255;
}

static void gfx_dp_set_subpixel_offset(int16_t x, int16_t y) {
    rdp.subpixel_ofs_x = x;
    rdp.subpixel_ofs_y = y;
}

static void gfx_draw_rectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    uint32_t saved_other_mode_h = rdp.other_mode_h;
    uint32_t cycle_type = (rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE));

    if (cycle_type == G_CYC_COPY) {
        rdp.other_mode_h = (rdp.other_mode_h & ~(3U << G_MDSFT_TEXTFILT)) | G_TF_POINT;
    }

    ulx += rdp.subpixel_ofs_x;
    lrx += rdp.subpixel_ofs_x;
    uly += rdp.subpixel_ofs_y;
    lry += rdp.subpixel_ofs_y;

    // D397: the N64 RDP floors the U10.2 top/bottom edges of a texture rect to
    // whole scanlines before its per-scanline coverage test, so a rect covers
    // scanlines [floor(uly/4), floor((lry-1)/4]]. A rect whose height is not a
    // whole number of native pixels therefore rasterizes to a whole number of
    // scanlines on hardware at every window scale. The gun-barrel / file-select
    // gradient is built from 0.75px-tall row strips (title2.c: uly=(i+12)<<2,
    // lry=((i+13)<<2)-1 -> 0.75px tall at 1px pitch); on the RDP each strip is
    // exactly one scanline. The continuous-NDC conversion below instead passes
    // the fractional bottom edge straight to the GPU, whose coverage test
    // aliases the 0.75px pitch into a visible "comb" (H2: period fixed in
    // native rows, ~3 = the 0.75px-at-1px-pitch beat, window period = 3*scale).
    // Snap the top/bottom edges to the RDP scanline range so a sub-pixel strip
    // rasterizes to exactly the scanline it targets. No-op for whole-pixel
    // rects (edges already multiples of 4) and for the copy-mode +1<<2 edge
    // (already a whole scanline), so normal fills are unchanged.
    if (lry > uly) {
        const int32_t top_scan = uly >> 2;
        const int32_t bot_scan = (lry - 1) >> 2;
        uly = top_scan << 2;
        lry = (bot_scan + 1) << 2;
    }

    // U10.2 coordinates
    float ulxf = ulx;
    float ulyf = uly;
    float lrxf = lrx;
    float lryf = lry;

    ulxf = ulxf / (4.0f * HALF_SCREEN_WIDTH) - 1.0f;
    ulyf = -(ulyf / (4.0f * HALF_SCREEN_HEIGHT)) + 1.0f;
    lrxf = lrxf / (4.0f * HALF_SCREEN_WIDTH) - 1.0f;
    lryf = -(lryf / (4.0f * HALF_SCREEN_HEIGHT)) + 1.0f;

    ulxf = gfx_adjust_x_for_aspect_ratio(ulxf);
    lrxf = gfx_adjust_x_for_aspect_ratio(lrxf);

    struct LoadedVertex* ul = &rsp.loaded_vertices[MAX_VERTICES + 0];
    struct LoadedVertex* ll = &rsp.loaded_vertices[MAX_VERTICES + 1];
    struct LoadedVertex* lr = &rsp.loaded_vertices[MAX_VERTICES + 2];
    struct LoadedVertex* ur = &rsp.loaded_vertices[MAX_VERTICES + 3];

    ul->x = ulxf;
    ul->y = ulyf;
    ul->z = -1.0f;
    ul->w = 1.0f;

    ll->x = ulxf;
    ll->y = lryf;
    ll->z = -1.0f;
    ll->w = 1.0f;

    lr->x = lrxf;
    lr->y = lryf;
    lr->z = -1.0f;
    lr->w = 1.0f;

    ur->x = lrxf;
    ur->y = ulyf;
    ur->z = -1.0f;
    ur->w = 1.0f;

    // The coordinates for texture rectangle shall bypass the viewport setting
    struct XYWidthHeight default_viewport = { 0, (int16_t)SCREEN_HEIGHT, (uint32_t)SCREEN_WIDTH, (uint32_t)SCREEN_HEIGHT };
    struct XYWidthHeight viewport_saved = rdp.viewport;
    uint32_t geometry_mode_saved = rsp.geometry_mode;

    gfx_adjust_viewport_or_scissor(&default_viewport);

    rdp.viewport = default_viewport;
    rdp.viewport_or_scissor_changed = true;
    rsp.geometry_mode = 0;

    gfx_sp_tri1(MAX_VERTICES + 0, MAX_VERTICES + 1, MAX_VERTICES + 3, true);
    gfx_sp_tri1(MAX_VERTICES + 1, MAX_VERTICES + 2, MAX_VERTICES + 3, true);

    rsp.geometry_mode = geometry_mode_saved;
    rdp.viewport = viewport_saved;
    rdp.viewport_or_scissor_changed = true;

    if (cycle_type == G_CYC_COPY) {
        rdp.other_mode_h = saved_other_mode_h;
    }
}

/* D226: HUD span scale (G_HUDSCALE_EXT, emitted by port/include/hudaspect.h
 * PORT_HUD_SCALE around specific HUD draws). While active, every rectangle
 * (text glyphs, message boxes, ammo icons are all rects) is scaled about the
 * anchor in logical 10.2 screen space, and texrect steps are divided by the
 * scale so the same texels cover the larger/smaller rect. 1.0 = inactive. */
static float s_hud_scale = 1.0f;
static int32_t s_hud_ax = 0, s_hud_ay = 0;   /* anchor, 10.2 fixed (px*4) */

static inline void gfx_hud_scale_rect(int32_t& ulx, int32_t& uly, int32_t& lrx, int32_t& lry) {
    if (s_hud_scale == 1.0f) {
        return;
    }
    ulx = s_hud_ax + (int32_t)lroundf((float)(ulx - s_hud_ax) * s_hud_scale);
    lrx = s_hud_ax + (int32_t)lroundf((float)(lrx - s_hud_ax) * s_hud_scale);
    uly = s_hud_ay + (int32_t)lroundf((float)(uly - s_hud_ay) * s_hud_scale);
    lry = s_hud_ay + (int32_t)lroundf((float)(lry - s_hud_ay) * s_hud_scale);
    /* A full-width box (e.g. the dialogue backdrop, 0..W) scaled about a
     * centre anchor goes negative; negative rect coords are not drawn, so
     * clip at the canvas origin (the right/bottom overhang is harmless). */
    if (ulx < 0) ulx = 0;
    if (uly < 0) uly = 0;
}

static void gfx_dp_texture_rectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry, uint8_t tile, int16_t uls,
                                     int16_t ult, int16_t dsdx, int16_t dtdy, bool flip) {
    if (s_hud_scale != 1.0f) {   /* D226 */
        gfx_hud_scale_rect(ulx, uly, lrx, lry);
        dsdx = (int16_t)lroundf((float)dsdx / s_hud_scale);
        dtdy = (int16_t)lroundf((float)dtdy / s_hud_scale);
    }
    uint64_t saved_combine_mode = rdp.combine_mode;
    if ((rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_COPY) {
        // Per RDP Command Summary Set Tile's shift s and this dsdx should be set to 4 texels
        // Divide by 4 to get 1 instead
        dsdx >>= 2;

        // Color combiner is turned off in copy mode
        gfx_dp_set_combine_mode(color_comb(0, 0, 0, G_CCMUX_TEXEL0), alpha_comb(0, 0, 0, G_ACMUX_TEXEL0), 0, 0);

        // Per documentation one extra pixel is added in this modes to each edge
        lrx += 1 << 2;
        lry += 1 << 2;
    }

    // uls and ult are S10.5
    // dsdx and dtdy are S5.10
    // lrx, lry, ulx, uly are U10.2
    // lrs, lrt are S10.5

    const int16_t width = flip ? lry - uly : lrx - ulx;
    const int16_t height = flip ? lrx - ulx : lry - uly;
    const float lrs = ((uls << 7) + dsdx * width) >> 7;
    const float lrt = ((ult << 7) + dtdy * height) >> 7;

    struct LoadedVertex* ul = &rsp.loaded_vertices[MAX_VERTICES + 0];
    struct LoadedVertex* ll = &rsp.loaded_vertices[MAX_VERTICES + 1];
    struct LoadedVertex* lr = &rsp.loaded_vertices[MAX_VERTICES + 2];
    struct LoadedVertex* ur = &rsp.loaded_vertices[MAX_VERTICES + 3];
    ul->u = uls;
    ul->v = ult;
    lr->u = lrs;
    lr->v = lrt;
    if (!flip) {
        ll->u = uls;
        ll->v = lrt;
        ur->u = lrs;
        ur->v = ult;
    } else {
        ll->u = lrs;
        ll->v = ult;
        ur->u = uls;
        ur->v = lrt;
    }

    {
        static int ge_d116 = -1;
        if (ge_d116 < 0) ge_d116 = getenv("GE_D116") ? 1 : 0;
        if (ge_d116) {
            const auto& tt = rdp.texture_tile[tile];
            fprintf(stderr,
                "[D116/f3d] tile=%d flip=%d ul(%d,%d) lr(%d,%d) uls=%d ult=%d dsdx=%d dtdy=%d "
                "-> lrs=%.2f lrt=%.2f ul.u=%.2f lr.u=%.2f | TILE siz=%d fmt=%d line_bytes=%d "
                "uls=%d lrs=%d width=%d height=%d cms=%d\n",
                tile, (int)flip, ulx, uly, lrx, lry, (int)uls, (int)ult, (int)dsdx, (int)dtdy,
                lrs, lrt, ul->u, lr->u,
                tt.siz, tt.fmt, tt.line_size_bytes, tt.uls, tt.lrs, tt.width, tt.height, tt.cms);
        }
    }

    uint8_t saved_tile = rdp.first_tile_index;
    if (saved_tile != tile) {
        rdp.textures_changed[0] = true;
        rdp.textures_changed[1] = true;
    }
    rdp.first_tile_index = tile;

    s_in_texrect = true;
    gfx_draw_rectangle(ulx, uly, lrx, lry);
    s_in_texrect = false;
    if (saved_tile != tile) {
        rdp.textures_changed[0] = true;
        rdp.textures_changed[1] = true;
    }
    rdp.first_tile_index = saved_tile;
    rdp.combine_mode = saved_combine_mode;
}

static void gfx_dp_image_rectangle(int32_t tile, int32_t w, int32_t h,
                                   int32_t ulx, int32_t uly, int16_t uls, int16_t ult,
                                   int32_t lrx, int32_t lry, int16_t lrs, int16_t lrt) {
    uint64_t saved_combine_mode = rdp.combine_mode;

    struct LoadedVertex* ul = &rsp.loaded_vertices[MAX_VERTICES + 0];
    struct LoadedVertex* ll = &rsp.loaded_vertices[MAX_VERTICES + 1];
    struct LoadedVertex* lr = &rsp.loaded_vertices[MAX_VERTICES + 2];
    struct LoadedVertex* ur = &rsp.loaded_vertices[MAX_VERTICES + 3];
    ul->u = uls * 32;
    ul->v = ult * 32;
    lr->u = lrs * 32;
    lr->v = lrt * 32;
    ll->u = uls * 32;
    ll->v = lrt * 32;
    ur->u = lrs * 32;
    ur->v = ult * 32;

    // ensure we have the correct texture size
    rdp.texture_tile[tile].line_size_bytes = w << rdp.texture_tile[tile].siz >> 1;
    rdp.texture_tile[tile].width = w;
    rdp.texture_tile[tile].height = h;
    rdp.texture_tile[tile].cms = 0;
    rdp.texture_tile[tile].cmt = 0;
    rdp.texture_tile[tile].shifts = 0;
    rdp.texture_tile[tile].shiftt = 0;
    auto& loadtex = rdp.loaded_texture[rdp.texture_tile[tile].tmem];
    loadtex.full_image_line_size_bytes = loadtex.line_size_bytes = rdp.texture_tile[tile].line_size_bytes;
    loadtex.size_bytes = loadtex.orig_size_bytes = loadtex.full_size_bytes = loadtex.line_size_bytes * h;

    uint8_t saved_tile = rdp.first_tile_index;
    if (saved_tile != tile) {
        rdp.textures_changed[0] = true;
        rdp.textures_changed[1] = true;
    }
    rdp.first_tile_index = tile;

    s_in_texrect = true;
    gfx_draw_rectangle(ulx, uly, lrx, lry);
    s_in_texrect = false;
    if (saved_tile != tile) {
        rdp.textures_changed[0] = true;
        rdp.textures_changed[1] = true;
    }
    rdp.first_tile_index = saved_tile;

    rdp.combine_mode = saved_combine_mode;
}

static void gfx_dp_fill_rectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    gfx_hud_scale_rect(ulx, uly, lrx, lry);   /* D226 */
    if (rdp.color_image_address == rdp.z_buf_address) {
        // Don't clear Z buffer here since we already did it with glClear
        return;
    }
    uint32_t mode = (rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE));

    // OTRTODO: This is a bit of a hack for widescreen screen fades, but it'll work for now...
    if (ulx == 0 && uly == 0 && lrx == 319 * 4 && lry == 239 * 4) {
        ulx = -1024;
        uly = -1024;
        lrx = 2048;
        lry = 2048;
    }

    if (mode == G_CYC_COPY || mode == G_CYC_FILL) {
        // Per documentation one extra pixel is added in this modes to each edge
        lrx += 1 << 2;
        lry += 1 << 2;
    }

    for (int i = MAX_VERTICES; i < MAX_VERTICES + 4; i++) {
        struct LoadedVertex* v = &rsp.loaded_vertices[i];
        v->color = rdp.fill_color;
    }

    uint64_t saved_combine_mode = rdp.combine_mode;

    if (mode == G_CYC_FILL) {
        gfx_dp_set_combine_mode(color_comb(0, 0, 0, G_CCMUX_SHADE), alpha_comb(0, 0, 0, G_ACMUX_SHADE), 0, 0);
    }

    gfx_draw_rectangle(ulx, uly, lrx, lry);
    rdp.combine_mode = saved_combine_mode;
}

static void gfx_dp_set_z_image(void* z_buf_address) {
    rdp.z_buf_address = z_buf_address;
}

static void gfx_dp_set_color_image(uint32_t format, uint32_t size, uint32_t width, void* address) {
    rdp.color_image_address = address;
}

static void gfx_sp_set_other_mode(uint32_t shift, uint32_t num_bits, uint64_t mode) {
    uint64_t mask = (((uint64_t)1 << num_bits) - 1) << shift;
    uint64_t om = rdp.other_mode_l | ((uint64_t)rdp.other_mode_h << 32);
    om = (om & ~mask) | mode;
    rdp.other_mode_l = (uint32_t)om;
    rdp.other_mode_h = (uint32_t)(om >> 32);
    rdp.palette_fmt = rdp.other_mode_h & (3U << G_MDSFT_TEXTLUT);
    rdp.tex_lod = (rdp.other_mode_h & G_TL_LOD) != 0;
    rdp.tex_detail = (rdp.other_mode_h & (2U << G_MDSFT_TEXTDETAIL)) == G_TD_DETAIL;
}

static void gfx_sp_set_vertex_colors(uint32_t count, const struct NormalColor *vcn) {
    // common sense dictates that we should copy the colors as the command is supposed to do,
    // but it actually doesn't seem to matter
    // SUPPORT_CHECK(count <= sizeof(rsp.vertex_colors) / sizeof(rsp.vertex_colors[0]));
    // for (uint32_t i = 0; i < count; ++i) {
    //     rsp.vertex_colors[i] = vcn[i];
    // }
    if (fast3d_ptr_ok(vcn)) { /* D146: ignore a wild pointer from a corrupt DL */
        rsp.vertex_colors = vcn;
    }
}

static void gfx_dp_set_other_mode(uint32_t h, uint32_t l) {
    rdp.other_mode_h = h;
    rdp.other_mode_l = l;
}

/* D245 (M-201): G_FLOATVTX_EXT loader -- gfx_sp_vertex for pre-transformed
 * clip-space float vertices with float S/T (see port/include/floatvtx.h).
 * Only the unlit, unfogged path the sky/water fans use is supported. */
static void gfx_sp_vertex_float(size_t n_vertices, size_t dest_index, const PortFloatVtx* vertices) {
    if (!fast3d_ptr_ok(vertices) || dest_index + n_vertices > MAX_VERTICES) {
        return;
    }
    for (size_t i = 0; i < n_vertices; i++, dest_index++) {
        const PortFloatVtx* v = &vertices[i];
        struct LoadedVertex* d = &rsp.loaded_vertices[dest_index];
        float x = gfx_adjust_x_for_aspect_ratio(v->x, v->w);
        float y = v->y, z = v->z, w = v->w;
        d->u = v->s * (float)rsp.texture_scaling_factor.s / 65536.0f;
        d->v = v->t * (float)rsp.texture_scaling_factor.t / 65536.0f;
        d->color.r = v->r;
        d->color.g = v->g;
        d->color.b = v->b;
        d->color.a = v->a;
        d->clip_rej = 0;
        if (x < -w) d->clip_rej |= 1;
        if (x > w) d->clip_rej |= 2;
        if (y < -w) d->clip_rej |= 4;
        if (y > w) d->clip_rej |= 8;
        if (z > w) d->clip_rej |= 32;
        d->x = x;
        d->y = y;
        d->z = z;
        d->w = w;
        d->fog = rdp.fog_color.a;
    }
}

static inline void *seg_addr(uintptr_t w1) {
    // GE model files reference GDLs (gSPDisplayList) and vertex arrays by raw
    // VMA 0x05xxxxxx WITHOUT the LSB set; segment 5 is set per-render to the
    // live host file base by the game's gSPSegment. Resolve it explicitly
    // before the segmented-address path below (a converter-remapped seg-5 w1
    // already carries the LSB and takes that path).
    if ((w1 & 0xFF000000) == 0x05000000 && segmentPointers[5]) {
        return (void *)(segmentPointers[5] + (w1 & 0x00FFFFFF));
    }
    // all segmented addresses have the least significant bit set
    if (w1 & 1) {
        // seg 0 is reserved and doesn't count here
        const uintptr_t seg = (w1 & 0x0f000000) >> 24;
        if (seg && segmentPointers[seg]) {
            const uintptr_t addr = (w1 & 0x00fffffe);
            return (void *)(segmentPointers[seg] + addr);
        }
    }
    // GE's ROM GDLs also carry UNMARKED segmented refs with the LSB clear:
    // G_MTX w1=0x03xxxxxx (seg 3 = render_pos), G_VTX w1=0x04/0x05xxxxxx
    // (seg 4 = runtime vtx buffer, seg 5 = file base), G_SETTIMG w1=0x05xxxxxx
    // (embedded image blob). Convention: segment in bits 24-27, 24-bit offset.
    // Runtime pointers never land here: DRAM lives at >= 0x70000000 and
    // K0-physical values are < 0x800000 (nibble 24 == 0).
    if (w1 < 0x10000000 && ((w1 >> 24) & 0xf)) {
        const uintptr_t seg = (w1 >> 24) & 0xf;
        if (segmentPointers[seg]) {
            return (void *)(segmentPointers[seg] + (w1 & 0x00FFFFFF));
        }
    }
    // GE passes OS_K0_TO_PHYSICAL(ptr) == ptr - 0x80000000 for RAM that lives
    // in the reserved N64-DRAM region (port/src/dram.c); map it back. The
    // region is 8 MB, so any offset below 0x800000 came from there.
    if (w1 < 0x800000) {
        return (void *)(w1 + 0x80000000);
    }
    // D131: a GBI DL built by game code can reference a COMPILED symbol via
    // osVirtualToPhysical() (a u32-returning shim), which truncates the
    // module's 0x1_00000000 high word. Seen in explosionRenderPropSmoke:
    // gSPMatrix(gdl++, osVirtualToPhysical((void*)&dword_CODE_bss_8007A100),
    // ..MODELVIEW) -> w1 == 0x40xxxxxx -> wild deref in gfx_sp_matrix. The
    // module is based at 0x140000000 (fixed, no ASLR), and nothing legit
    // reaches this fallthrough with a value in [0x40000000, DRAM_V1): DRAM
    // (>=0x70000000), KSEG0 (>=0x80000000), segmented addrs and sub-0x800000
    // physical offsets are all handled above. Restore the high word from
    // this TU's own load address.
    {
        static const uintptr_t mod_hi =
            ((uintptr_t)(void *)&segmentPointers[0]) & 0xffffffff00000000ULL;
        if (mod_hi && w1 >= 0x40000000 && w1 < 0x70000000) {
            return (void *)(mod_hi | w1);
        }
    }
    return (void *)w1;
}

uintptr_t clearMtx;

static void gfx_run_dl(Gfx* cmd) {
    // puts("dl");
    int dummy = 0;
    char dlName[128];
    const char* fileName;

    Gfx* dListStart = cmd;
    uint64_t ourHash = -1;

    for (;;) {
        uint32_t opcode = cmd->words.w0 >> 24;
        // gfx_print_cmd(cmd);
        switch (opcode) {
                // RSP commands:
            case G_NOOP: /* 0xc0. GE's gbi_extension.h also names this slot
                             G_SETTEX (gsSPUseTexture); the game never emits it
                             (finding B1), so treating it as a no-op is safe. */
                break;
            case G_MTX: {
                if (s_interp_mode != INTERP_OFF) {
                    /* D409: key the matrix by the geometry it transforms. */
                    s_interp_next_geo = 0;
                    for (int i = 1; i <= 24; i++) {
                        const uint32_t op = cmd[i].words.w0 >> 24;
                        if (op == G_VTX || op == G_DL || op == G_FLOATVTX_EXT) {
                            s_interp_next_geo = (uintptr_t)seg_addr(cmd[i].words.w1);
                            break;
                        }
                        if (op == (uint8_t)G_ENDDL || op == G_MTX) {
                            break;
                        }
                    }
                }
                gfx_sp_matrix(C0(16, 8), (const int32_t*)seg_addr(cmd->words.w1));
                break;
            }
            case (uint8_t)G_POPMTX:
                gfx_sp_pop_matrix(1);
                break;
            case G_MOVEMEM:
                gfx_sp_movemem(C0(16, 8), 0, seg_addr(cmd->words.w1));
                break;
            case (uint8_t)G_MOVEWORD:
                gfx_sp_moveword(C0(0, 8), C0(8, 16), cmd->words.w1);
                break;
            case (uint8_t)G_TEXTURE:
                gfx_sp_texture(C1(16, 16), C1(0, 16), C0(11, 3), C0(8, 3), C0(0, 8));
                break;
            case G_VTX:
                gfx_sp_vertex(C0(0, 16) / sizeof(Vtx), C0(16, 4), (const Vtx*)seg_addr(cmd->words.w1));
                d75d_note_vtx(videoGetFrameCount(), C0(0, 16) / sizeof(Vtx), C0(16, 4));
                break;
            case G_DL: {
                if (C0(16, 1) == 0) {
                    // Push return address
                    Gfx* subGFX = (Gfx*)seg_addr(cmd->words.w1);

                    if (subGFX != nullptr) {
                        gfx_run_dl(subGFX);
                    }
                } else {
                    cmd = (Gfx*)seg_addr(cmd->words.w1);
                    --cmd; // increase after break
                }
                break;
            }
            case (uint8_t)G_ENDDL:
                return;
            case (uint8_t)G_SETGEOMETRYMODE:
                gfx_sp_geometry_mode(0, cmd->words.w1);
                break;
            case (uint8_t)G_CLEARGEOMETRYMODE:
                gfx_sp_geometry_mode(cmd->words.w1, 0);
                break;
            case G_FLOATVTX_EXT: /* D245 */
                gfx_sp_vertex_float(C0(0, 16), C0(16, 8), (const PortFloatVtx*)seg_addr(cmd->words.w1));
                break;
            case 0x46: /* G_HUDSCALE_EXT (D226): w0 low16 = scale*256 (0/256 = off), w1 = ax4<<16 | ay4 */
            {
                const uint32_t sc = C0(0, 16);
                s_hud_scale = (sc == 0 || sc == 256) ? 1.0f : (float)sc / 256.0f;
                s_hud_ax = (int32_t)C1(16, 16);
                s_hud_ay = (int32_t)C1(0, 16);
                break;
            }
            case G_EXTRAGEOMETRYMODE_EXT:
                gfx_sp_extra_geometry_mode(~C0(0, 24), cmd->words.w1);
                break;
            case (uint8_t)G_TRI1:
                gfx_sp_tri1(C1(16, 8) / 10, C1(8, 8) / 10, C1(0, 8) / 10, false);
                break;
            case (uint8_t)G_TRI4:
                gfx_sp_tri4(cmd);
                break;
            case (uint8_t)G_SETOTHERMODE_L:
                gfx_sp_set_other_mode(C0(8, 8), C0(0, 8), cmd->words.w1);
                break;
            case (uint8_t)G_SETOTHERMODE_H:
                gfx_sp_set_other_mode(C0(8, 8) + 32, C0(0, 8), (uint64_t)cmd->words.w1 << 32);
                break;
            case G_COL:
                gfx_sp_set_vertex_colors(C0(0, 16) / 4, (NormalColor *)seg_addr(cmd->words.w1));
                break;

            // RDP Commands:
            case G_SETTIMG: {
                gfx_dp_set_texture_image(C0(21, 3), C0(19, 2), C0(0, 10), 0, seg_addr(cmd->words.w1));
                break;
            }
            case G_SETTIMG_FB_EXT:
                gfx_flush();
                gfx_rapi->select_texture_fb(cmd->words.w1);
                rdp.textures_changed[0] = false;
                rdp.textures_changed[1] = false;
                break;
            case G_SETGRAYSCALE_EXT:
                rdp.grayscale = cmd->words.w1;
                break;
            case G_LOADBLOCK:
                gfx_dp_load_block(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;
            case G_LOADTILE:
                gfx_dp_load_tile(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;
            case G_SETTILE:
                gfx_dp_set_tile(C0(21, 3), C0(19, 2), C0(9, 9), C0(0, 9), C1(24, 3), C1(20, 4), C1(18, 2), C1(14, 4),
                                C1(10, 4), C1(8, 2), C1(4, 4), C1(0, 4));
                break;
            case G_SETTILESIZE:
                gfx_dp_set_tile_size(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;
            case G_LOADTLUT:
                gfx_dp_load_tlut(C1(24, 3), C0(14, 10), C0(2, 10), C1(14, 10), C1(2, 10));
                break;
            case G_SETENVCOLOR:
                gfx_dp_set_env_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;
            case G_SETPRIMCOLOR:
                gfx_dp_set_prim_color(C0(8, 8), C0(0, 8), C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;
            case G_SETFOGCOLOR:
                gfx_dp_set_fog_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;
            case G_SETFILLCOLOR:
                gfx_dp_set_fill_color(cmd->words.w1);
                break;
            case G_SETINTENSITY_EXT:
                gfx_dp_set_grayscale_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;
            case G_SETCOMBINE:
#ifdef PORT
                /* D172 probe (env-gated, inert): log every SETCOMBINE with the
                 * cycle-type active at that moment. Particle records
                 * (explosion.c g_ExplosionDisplayLists[]) set a 2-cycle
                 * combine but never set cycletype - this tells us what
                 * cycletype fast3d has when they replay. */
                static int ge_d172_b = -1;
                if (ge_d172_b < 0) ge_d172_b = getenv("GE_D172") != NULL;
                if (ge_d172_b) {
                    static uint64_t d172seen[64];
                    static int d172cnt = 0;
                    uint64_t key = ((uint64_t)(uint32_t)cmd->words.w0 << 32) | (uint32_t)cmd->words.w1;
                    bool dup = false;
                    for (int k = 0; k < d172cnt; k++) if (d172seen[k] == key) { dup = true; break; }
                    if (!dup && d172cnt < 64) {
                        d172seen[d172cnt++] = key;
                        uint32_t ct = (rdp.other_mode_h >> G_MDSFT_CYCLETYPE) & 3;
                        sysLogPrintf(LOG_NOTE,
                            "D172: SETCOMBINE w0=%08x w1=%08x  cycletype=%u (%s)",
                            (uint32_t)cmd->words.w0, (uint32_t)cmd->words.w1,
                            ct, ct == 0 ? "1CYC" : ct == 1 ? "2CYC" : ct == 2 ? "COPY" : "FILL");
                    }
                }
#endif
                gfx_dp_set_combine_mode(color_comb(C0(20, 4), C1(28, 4), C0(15, 5), C1(15, 3)),
                                        alpha_comb(C0(12, 3), C1(12, 3), C0(9, 3), C1(9, 3)),
                                        color_comb(C0(5, 4), C1(24, 4), C0(0, 5), C1(6, 3)),
                                        alpha_comb(C1(21, 3), C1(3, 3), C1(18, 3), C1(0, 3)));
                break;
            // G_SETPRIMCOLOR, G_CCMUX_PRIMITIVE, G_ACMUX_PRIMITIVE, is used by Goddard
            // G_CCMUX_TEXEL1, LOD_FRACTION is used in Bowser room 1
            case G_SETSUBPIXELOFFSET_EXT: {
                gfx_dp_set_subpixel_offset(C0(0, 16), C1(0, 16));
                break;
            }
            case G_TEXRECT:
            case G_TEXRECTFLIP: {
                int32_t lrx, lry, tile, ulx, uly;
                uint32_t uls, ult, dsdx, dtdy;
                lrx = C0(12, 12);
                lry = C0(0, 12);
                tile = C1(24, 3);
                ulx = C1(12, 12);
                uly = C1(0, 12);
                ++cmd;
                uls = C1(16, 16);
                ult = C1(0, 16);
                ++cmd;
                dsdx = C1(16, 16);
                dtdy = C1(0, 16);
                gfx_dp_texture_rectangle(ulx, uly, lrx, lry, tile, uls, ult, dsdx, dtdy, opcode == G_TEXRECTFLIP);
                break;
            }
            case G_FILLRECT:
                gfx_dp_fill_rectangle(C1(12, 12), C1(0, 12), C0(12, 12), C0(0, 12));
                break;
            case G_FILLRECT_WIDE_EXT: {
                int32_t lrx, lry, ulx, uly;
                lrx = (int32_t)(C0(0, 24) << 8) >> 8;
                lry = (int32_t)(C1(0, 24) << 8) >> 8;
                ++cmd;
                ulx = (int32_t)(C0(0, 24) << 8) >> 8;
                uly = (int32_t)(C1(0, 24) << 8) >> 8;
                gfx_dp_fill_rectangle(ulx, uly, lrx, lry);
                break;
            }
            case G_TEXRECT_WIDE_EXT: {
                int32_t lrx, lry, tile, ulx, uly;
                uint32_t uls, ult, dsdx, dtdy;
                bool flip;
                lrx = (int32_t)((C0(0, 24) << 8)) >> 8;
                lry = (int32_t)((C1(0, 24) << 8)) >> 8;
                tile = C1(24, 3);
                flip = C1(27, 1);
                ++cmd;
                ulx = (int32_t)((C0(0, 24) << 8)) >> 8;
                uly = (int32_t)((C1(0, 24) << 8)) >> 8;
                ++cmd;
                uls = C0(16, 16);
                ult = C0(0, 16);
                dsdx = C1(16, 16);
                dtdy = C1(0, 16);
                gfx_dp_texture_rectangle(ulx, uly, lrx, lry, tile, uls, ult, dsdx, dtdy, flip);
                break;
            }
            case G_IMAGERECT_EXT: {
                int16_t tile, iw, ih;
                int16_t x0, y0, s0, t0;
                int16_t x1, y1, s1, t1;
                tile = C0(0, 3);
                iw = C1(16, 16);
                ih = C1(0, 16);
                ++cmd;
                x0 = C0(16, 16);
                y0 = C0(0, 16);
                s0 = C1(16, 16);
                t0 = C1(0, 16);
                ++cmd;
                x1 = C0(16, 16);
                y1 = C0(0, 16);
                s1 = C1(16, 16);
                t1 = C1(0, 16);
                gfx_dp_image_rectangle(tile, iw, ih, x0, y0, s0, t0, x1, y1, s1, t1);
                break;
            }
            case G_SETSCISSOR:
                gfx_dp_set_scissor(C1(24, 2), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;
            case G_SETZIMG:
                gfx_dp_set_z_image(seg_addr(cmd->words.w1));
                break;
            case G_SETCIMG:
                gfx_dp_set_color_image(C0(21, 3), C0(19, 2), C0(0, 11), seg_addr(cmd->words.w1));
                break;
            case G_SETFB_EXT:
                gfx_flush();
                if (cmd->words.w1) {
                    // don't care about noise here
                    gfx_set_framebuffer(cmd->words.w1, 1.f);
                    fbActive = true;
                } else {
                    gfx_reset_framebuffer();
                    fbActive = false;
                }
                break;
            case G_COPYFB_EXT:
                gfx_copy_framebuffer(C0(11, 11), C0(0, 11), (int16_t)C1(16, 16), (int16_t)C1(0, 16), C0(22, 1));
                break;
            case G_RDPSETOTHERMODE:
                gfx_dp_set_other_mode(C0(0, 24), cmd->words.w1);
                break;
            case G_INVALTEXCACHE_EXT:
                if (cmd->words.w1) {
                    gfx_texture_cache_delete((const uint8_t *)seg_addr(cmd->words.w1));
                } else {
                    gfx_texture_cache_clear();
                }
                break;
            case (uint8_t)G_RDPHALF_1:
            case (uint8_t)G_RDPHALF_2:
            case (uint8_t)G_RDPHALF_CONT:
                // on N64 skyRender uses these to render some types of skies and skybox water
                // by issuing low-level ucode commands G_TRI_FILL and G_TRI_SHADE_TXTR
                // the port renders the sky in a different manner
                break;
            case G_RDPFLUSH_EXT:
                gfx_flush();
                break;
            case G_CLEAR_DEPTH_EXT:
                gfx_flush();
                gfx_rapi->clear_framebuffer(false, true);
                break;
            case G_RDPPIPESYNC:
            case G_RDPFULLSYNC:
            case G_RDPLOADSYNC:
            case G_RDPTILESYNC:
                break;
            default: {
                (void)dListStart;
                /* D146: an unknown opcode means the DL walk has desynced or
                 * this DL is garbage (a front-end 3D model whose display list
                 * was never built - the D75 / D144 family - lives at a valid
                 * DRAM address full of junk). Aborting the whole process over
                 * one bad menu model is the wrong trade for a breadth-first
                 * port: end this (sub-)DL and let the frame finish. Still
                 * logged loudly, rate-limited, so a real level-render desync
                 * is not hidden. */
                {
                    static int warned = 0;
                    if (warned < 20) {
                        warned++;
                        sysLogPrintf(LOG_ERROR,
                            "D146: unknown GBI opcode 0x%02x at %p (w0=%llx w1=%llx) - ending DL",
                            opcode, (void *)cmd,
                            (unsigned long long)cmd->words.w0,
                            (unsigned long long)cmd->words.w1);
                    }
                }
                return;
            }
        }
        ++cmd;
    }
}

static void gfx_sp_reset() {
    rsp.modelview_matrix_stack_size = 1;
    rsp.current_num_lights = 2;
    rsp.lights_changed = true;
}

extern "C" void gfx_get_dimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) {
    gfx_wapi->get_dimensions(width, height, posX, posY);
}

extern "C" void gfx_init(const GfxInitSettings *settings) {
    gfx_wapi = settings->wapi;
    gfx_rapi = settings->rapi;
    gfx_wapi->init(&settings->window_settings);
    gfx_rapi->init();
    gfx_rapi->update_framebuffer_parameters(0, settings->window_settings.width, settings->window_settings.height, 1, false, true, true, true);
    gfx_current_dimensions.internal_mul = 1;
    gfx_current_game_window_viewport.width = gfx_current_dimensions.width = settings->window_settings.width;
    gfx_current_game_window_viewport.height = gfx_current_dimensions.height = settings->window_settings.height;
    game_framebuffer = gfx_rapi->create_framebuffer();
    game_framebuffer_msaa_resolved = gfx_rapi->create_framebuffer();

    if (gfx_msaa_level > 1 && !gfx_framebuffers_enabled) {
        sysLogPrintf(LOG_WARNING, "F3D: MSAA set to %d, but framebuffers are not available; disabling", gfx_msaa_level);
        gfx_msaa_level = 1;
    }

    for (int i = 0; i < 16; i++) {
        segmentPointers[i] = 0;
    }

    if (tex_upload_buffer == nullptr) {
        // We cap texture max to 8k, because why would you need more?
        int max_tex_size = std::min(8192, gfx_rapi->get_max_texture_size());
        tex_upload_buffer = (uint8_t*)malloc(max_tex_size * max_tex_size * 4);
    }

    /* D72: N64 boots with RSP memory zeroed — no lookat until gSPLookAt. */
    rsp.lookat_enabled = false;
}

extern "C" void gfx_destroy(void) {
    // TODO: should also destroy rapi and wapi, and any other resources acquired in fast3d

    // Texture cache and loaded textures store references to Resources which need to be unreferenced.
    gfx_texture_cache_clear();
}

extern "C" struct GfxRenderingAPI* gfx_get_current_rendering_api(void) {
    return gfx_rapi;
}

extern "C" void gfx_start_frame(void) {
    gfx_wapi->handle_events();
    gfx_wapi->get_dimensions(&gfx_current_window_dimensions.width, &gfx_current_window_dimensions.height,
                             &gfx_current_window_position_x, &gfx_current_window_position_y);

    if (gfx_current_window_dimensions.height == 0) {
        // Avoid division by zero
        gfx_current_window_dimensions.height = 1;
    }

    gfx_current_window_dimensions.aspect_ratio = (float)gfx_current_window_dimensions.width / gfx_current_window_dimensions.height;

    gfx_current_dimensions = gfx_current_window_dimensions;

    gfx_current_game_window_viewport.width = gfx_current_dimensions.width;
    gfx_current_game_window_viewport.height = gfx_current_dimensions.height;

    if (gfx_current_dimensions.height != gfx_prev_dimensions.height) {
        for (auto& fb : framebuffers) {
            uint32_t width, height, msaa;
            if (fb.second.autoresize) {
                if (fb.second.upscale) {
                    width = fb.second.orig_width;
                    height = fb.second.orig_height;
                    gfx_adjust_width_height_for_scale(width, height);
                } else {
                    // assume this is a fullscreen fb
                    width = gfx_current_dimensions.width;
                    height = gfx_current_dimensions.height;
                }
                if (width != fb.second.applied_width || height != fb.second.applied_height) {
                    gfx_rapi->update_framebuffer_parameters(fb.first, width, height, 1, true, true, true, true);
                    fb.second.applied_width = width;
                    fb.second.applied_height = height;
                }
            }
        }
    }
    gfx_prev_dimensions = gfx_current_dimensions;

    bool different_size = gfx_current_dimensions.width != gfx_current_game_window_viewport.width ||
                          gfx_current_dimensions.height != gfx_current_game_window_viewport.height;
    if (gfx_framebuffers_enabled && (different_size || gfx_msaa_level > 1)) {
        game_renders_to_framebuffer = true;
        if (different_size) {
            gfx_rapi->update_framebuffer_parameters(game_framebuffer, gfx_current_dimensions.width,
                                                    gfx_current_dimensions.height, gfx_msaa_level, true, true, true,
                                                    true);
        } else {
            // MSAA framebuffer needs to be resolved to an equally sized target when complete, which must therefore
            // match the window size
            gfx_rapi->update_framebuffer_parameters(game_framebuffer, gfx_current_window_dimensions.width,
                                                    gfx_current_window_dimensions.height, gfx_msaa_level, false, true,
                                                    true, true);
        }
        if (gfx_msaa_level > 1 && different_size) {
            gfx_rapi->update_framebuffer_parameters(game_framebuffer_msaa_resolved, gfx_current_dimensions.width,
                                                    gfx_current_dimensions.height, 1, false, false, false, false);
        }
    } else {
        game_renders_to_framebuffer = false;
    }

    fbActive = 0;

    // update aspect scale and offset
    gfx_update_aspect_mode();
}

uint32_t num_dls = 0;

/* D252 TEMP: see s_d157BurstUntil. No-op unless GE_D157 is set. */
extern "C" void gfxD157Burst(void) {
    static int on = -1;
    if (on < 0) on = getenv("GE_D157") != NULL;
    if (!on) return;
    s_d157BurstUntil = num_dls + 90;
    sysLogPrintf(LOG_NOTE, "D157 burst: F12 at frame=%u, full logging to frame=%u",
                 num_dls, (uint32_t)s_d157BurstUntil);
}

/* F10 port-layer options overlay (port/src/optionsoverlay.c). Returns a
 * self-contained 2D display list to draw on top of the game's frame, or NULL
 * when the overlay is closed -- in which case nothing is appended and the
 * frame is byte-identical to before (golden dumps unaffected). */
extern "C" Gfx* optionsOverlayEmit(void);

static uint64_t gfx_perf_now_ns(void) {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
static int gfx_perfstat_on(void) {
    static int on = -1;
    if (on < 0) on = getenv("GE_PERFSTAT") != NULL;
    return on;
}
static double s_perf_dl = 0, s_perf_run = 0, s_perf_present = 0, s_perf_interval = 0;
static double s_perf_pre = 0, s_perf_post = 0, s_perf_swap = 0;
static uint64_t s_perf_frames = 0, s_perf_last_start = 0;

static void gfx_run_pass(Gfx* commands, bool swap);

extern "C" void gfx_set_frame_interpolation(int on, int vi_rate, int refresh_hz, int vsync) {
    if (vi_rate > 0) {
        s_interp_tick_ns = 1000000000ULL / (uint64_t)vi_rate;
    }
    s_interp_refresh_ns = refresh_hz > 0 ? 1000000000ULL / (uint64_t)refresh_hz : 0;
    s_interp_vsync = vsync != 0;
    s_interp_enabled = on != 0;
}

/* Present hook for the extra frame (video.c counts it toward the FPS stats). */
extern "C" void videoNoteInterpFrame(void);

/* D409: microseconds until the deferred real-frame present is due (0 = due
 * now), or -1 when nothing is pending. Polled by the scheduler thread's
 * message wait (port/src/libultra.c osRecvMesg). */
extern "C" int64_t gfx_pending_present_wait_us(void) {
    if (!s_present_pending) {
        return -1;
    }
    const uint64_t now = gfx_perf_now_ns();
    return now >= s_present_deadline_ns ? 0 : (int64_t)((s_present_deadline_ns - now + 999) / 1000);
}

/* Swap the already-rendered real frame. Render (scheduler) thread only. */
extern "C" void gfx_present_pending(void) {
    if (!s_present_pending) {
        return;
    }
    s_present_pending = false;
    gfx_wapi->swap_buffers_begin();
    gfx_rapi->finish_render();
    gfx_wapi->swap_buffers_end();
}

extern "C" void gfx_run(Gfx* commands) {
    /* A real frame still waiting for its slot is shown now rather than lost
     * (only when the next DL beats its deadline). */
    if (s_present_pending) {
        gfx_present_pending();
    }

    if (!s_interp_enabled) {
        if (s_interp_mode != INTERP_OFF) {
            s_interp_mode = INTERP_OFF;
            s_interp_have_prev = false;
            s_interp_too_slow = false;
            s_interp_pass_ema_ns = 0;
            s_interp_prev.clear();
            s_interp_cur.clear();
        }
        gfx_run_pass(commands, true);
        return;
    }

    /* D409 pacing. The scheduler thread runs this inline, and that same
     * thread is what forwards VI retraces to the game thread -- the game only
     * starts its next frame on a forwarded retrace (boss.c). So it must not
     * sit here waiting for the real frame's present slot: both passes are
     * rendered back to back, the in-between frame is swapped at once, and the
     * real frame's swap is DEFERRED to half a tick later, performed from the
     * scheduler's own message wait (gfx_present_pending via osRecvMesg).
     * (First cut waited inline at 2x the VI rate: gfx_run then held the
     * thread for most of the tick, retraces were forwarded late, the sim
     * slipped to 2-tick frames and the blend was dropped on those -- ~70-84
     * fps measured on a user's machine.) */
    const uint64_t now = gfx_perf_now_ns();
    const bool stale = s_interp_last_run_ns == 0 || now - s_interp_last_run_ns > 4 * s_interp_tick_ns;
    s_interp_last_run_ns = now;

    /* Too slow to afford a second pass: hysteresis on the smoothed cost. */
    if (s_interp_pass_ema_ns > 0.40 * (double)s_interp_tick_ns) {
        s_interp_too_slow = true;
    } else if (s_interp_pass_ema_ns < 0.30 * (double)s_interp_tick_ns) {
        s_interp_too_slow = false;
    }

    bool blended = false;
    if (s_interp_have_prev && !stale && !s_interp_too_slow) {
        uintptr_t segs[16];
        struct RSP rsp_saved;
        memcpy(segs, segmentPointers, sizeof(segs));
        memcpy(&rsp_saved, &rsp, sizeof(rsp));
        s_interp_mode = INTERP_BLEND;
        s_interp_occ.clear();
        gfx_run_pass(commands, true);
        if (!dropped_frame) {
            gfx_rapi->finish_render();
            gfx_wapi->swap_buffers_end();
            videoNoteInterpFrame();
            blended = true;
        }
        memcpy(segmentPointers, segs, sizeof(segs));
        memcpy(&rsp, &rsp_saved, sizeof(rsp));
    }
    const uint64_t a_ns = gfx_perf_now_ns();

    s_interp_mode = INTERP_RECORD;
    s_interp_occ.clear();
    s_interp_cur.clear();
    gfx_run_pass(commands, !blended);
    s_interp_prev.swap(s_interp_cur);
    s_interp_have_prev = !dropped_frame;

    const double pass = (double)s_pass_render_ns;
    s_interp_pass_ema_ns = s_interp_pass_ema_ns > 0 ? 0.9 * s_interp_pass_ema_ns + 0.1 * pass : pass;

    if (blended) {
        /* Due half a tick after the in-between frame. With VSync the swap
         * itself then waits for a refresh, so aim one refresh early. */
        uint64_t delay = s_interp_tick_ns / 2;
        if (s_interp_vsync && s_interp_refresh_ns) {
            delay = delay > s_interp_refresh_ns ? delay - s_interp_refresh_ns : 0;
        } else {
            delay = delay > 500000 ? delay - 500000 : 0;
        }
        s_present_deadline_ns = a_ns + delay;
        s_present_pending = true;
    }
}

static void gfx_run_pass(Gfx* commands, bool swap) {
    const uint64_t pass_t0 = gfx_perf_now_ns();
    s_hud_scale = 1.0f;   /* D226: never carry a HUD scale across frames */
    const uint64_t perf_t0 = gfx_perfstat_on() ? gfx_perf_now_ns() : 0;
    ++num_dls;
    gfx_sp_reset();

    // puts("New frame");

    if (!gfx_wapi->start_frame()) {
        dropped_frame = true;
        return;
    }
    dropped_frame = false;

    gfx_rapi->update_framebuffer_parameters(0, gfx_current_window_dimensions.width,
                                            gfx_current_window_dimensions.height, 1, false, true, true,
                                            !game_renders_to_framebuffer);
    gfx_rapi->start_frame();
    gfx_rapi->start_draw_to_framebuffer(game_renders_to_framebuffer ? game_framebuffer : 0,
                                        (float)gfx_current_dimensions.height / SCREEN_HEIGHT);
    gfx_rapi->clear_framebuffer(true, false);
    rdp.viewport_or_scissor_changed = true;
    rendering_state.viewport = {};
    rendering_state.scissor = {};
    const uint64_t perf_tpre = perf_t0 ? gfx_perf_now_ns() : 0;
    gfx_run_dl(commands);
    {
        Gfx* overlay = optionsOverlayEmit();
        if (overlay != nullptr) {
            gfx_run_dl(overlay);
        }
    }
    gfx_flush();
    const uint64_t perf_t1 = perf_t0 ? gfx_perf_now_ns() : 0;
    gfxFramebuffer = 0;

    if (game_renders_to_framebuffer) {
        gfx_rapi->start_draw_to_framebuffer(0, 1);
        gfx_rapi->clear_framebuffer(true, true);

        if (gfx_msaa_level > 1) {
            bool different_size = gfx_current_dimensions.width != gfx_current_game_window_viewport.width ||
                                  gfx_current_dimensions.height != gfx_current_game_window_viewport.height;

            if (different_size) {
                gfx_rapi->resolve_msaa_color_buffer(game_framebuffer_msaa_resolved, game_framebuffer);
                gfxFramebuffer = (uintptr_t)gfx_rapi->get_framebuffer_texture_id(game_framebuffer_msaa_resolved);
            } else {
                gfx_rapi->resolve_msaa_color_buffer(0, game_framebuffer);
            }
        } else {
            gfxFramebuffer = (uintptr_t)gfx_rapi->get_framebuffer_texture_id(game_framebuffer);
        }
    }

    gfx_rapi->end_frame();
    const uint64_t perf_tpost = perf_t0 ? gfx_perf_now_ns() : 0;
    s_pass_render_ns = gfx_perf_now_ns() - pass_t0;
    if (swap) {
        gfx_wapi->swap_buffers_begin();
    }
    if (perf_t0) {
        s_perf_pre += (double)(perf_tpre - perf_t0) / 1.0e6;
        s_perf_post += (double)(perf_tpost - perf_t1) / 1.0e6;
        s_perf_swap += (double)(gfx_perf_now_ns() - perf_tpost) / 1.0e6;
        const double f = 1.0e6;
        const uint64_t t2 = gfx_perf_now_ns();
        s_perf_dl += (double)(perf_t1 - perf_tpre) / f;
        s_perf_run += (double)(t2 - perf_t0) / f;
        if (s_perf_last_start) s_perf_interval += (double)(perf_t0 - s_perf_last_start) / f;
        s_perf_last_start = perf_t0;
    }
}

extern "C" void gfx_end_frame(void) {
    const uint64_t perf_t0 = gfx_perfstat_on() ? gfx_perf_now_ns() : 0;
    if (!dropped_frame && !s_present_pending) { /* D409: a deferred present finishes itself */
        gfx_rapi->finish_render();
        gfx_wapi->swap_buffers_end();
    }
    if (perf_t0) {
        const double f = 1.0e6;
        s_perf_present += (double)(gfx_perf_now_ns() - perf_t0) / f;
        if (++s_perf_frames % 300 == 0) {
            const double n = 300.0;
            fprintf(stderr, "PERFSTAT pre=%.2f post=%.2f swap/pace=%.2f | ", s_perf_pre / n, s_perf_post / n, s_perf_swap / n);
            s_perf_pre = s_perf_post = s_perf_swap = 0;
            fprintf(stderr, "frames=%llu dl=%.2fms run=%.2fms present=%.2fms interval=%.2fms (%.1f fps) tris=%.0f batches=%.0f\n",
                    (unsigned long long)s_perf_frames, s_perf_dl / n, s_perf_run / n, s_perf_present / n,
                    s_perf_interval / n, s_perf_interval > 0 ? 1000.0 * n / s_perf_interval : 0.0,
                    (double)s_perf_tris / n, (double)s_perf_batches / n);
            s_perf_dl = s_perf_run = s_perf_present = s_perf_interval = 0;
            s_perf_tris = s_perf_batches = 0;
        }
    }
}

extern "C" void gfx_set_target_fps(int fps) {
    gfx_wapi->set_target_fps(fps);
}

extern "C" void reset_texture_state() {
    gfx_texture_cache_clear();
    if (rendering_state.shader_program) {
        gfx_rapi->unload_shader(rendering_state.shader_program);
        rendering_state.shader_program = nullptr;
    }
    gfx_rapi->clear_shaders();
    color_combiner_pool.clear();
    prev_combiner = color_combiner_pool.end();
}

extern "C" void gfx_set_texture_filter(enum FilteringMode mode) {
    reset_texture_state();
    gfx_rapi->set_texture_filter(mode);
}

extern "C" void gfx_set_mipmap_filter(enum MipmapFilteringMode mode) {
    reset_texture_state();
    gfx_rapi->set_mipmap_filter(mode);
}

extern "C" void gfx_set_fix_mip_textures(int on) { g_fix_mip_textures = !!on; }

extern "C" void gfx_set_detail_base_tile(int on) { g_detail_base_tile = !!on; }

/* D212: expose the (already-implemented) rendering-API anisotropy hook to the
 * port layer. Clamp to [1, GL max] so a stale ini value can't feed an invalid
 * GL_TEXTURE_MAX_ANISOTROPY. 1 = isotropic (driver default). */
extern "C" void gfx_set_anisotropy_level(int level) {
    reset_texture_state();
    int max = gfx_rapi->get_max_anisotropy_level ? gfx_rapi->get_max_anisotropy_level() : 1;
    if (max < 1) max = 1;
    if (level < 1) level = 1;
    if (level > max) level = max;
    if (gfx_rapi->set_anisotropy_level) {
        gfx_rapi->set_anisotropy_level(level);
    }
}
extern "C" void gfx_set_wrap_fix(int on) {
    const char* e = getenv("GE_WRAPFIX"); /* RC3 test override */
    g_wrap_fix = e ? (atoi(e) != 0) : !!on;
}

extern "C" int gfx_create_framebuffer(uint32_t width, uint32_t height, int upscale, int autoresize) {
    int fb = gfx_rapi->create_framebuffer();
    gfx_resize_framebuffer(fb, width, height, upscale, autoresize);
    return fb;
}

extern "C" void gfx_resize_framebuffer(int fb, uint32_t width, uint32_t height, int upscale, int autoresize) {
    uint32_t orig_width, orig_height;

    if (width && height) {
        // user-specified size
        orig_width = width;
        orig_height = height;
        if (upscale) {
            gfx_adjust_width_height_for_scale(width, height);
        }
        gfx_rapi->update_framebuffer_parameters(fb, width, height, 1, true, true, true, true);
    } else {
        // same size as main fb
        orig_width = width = gfx_current_dimensions.width;
        orig_height = height = gfx_current_dimensions.height;
        upscale = false;
        autoresize = true;
        gfx_rapi->update_framebuffer_parameters(fb, width, height, 1, true, true, true, true);
    }

    framebuffers[fb] = { orig_width, orig_height, width, height, (bool)upscale, (bool)autoresize };
}

extern "C" void gfx_set_framebuffer(int fb, float noise_scale) {
    gfx_rapi->start_draw_to_framebuffer(fb, noise_scale);
    gfx_rapi->clear_framebuffer(true, true);
    active_fb = framebuffers.find(fb);
}

extern "C" void gfx_copy_framebuffer(int fb_dst, int fb_src, int left, int top, int use_back) {
    const bool is_main_fb = (fb_src == 0);

    if (is_main_fb) {
        if (left > 0 && top > 0) {
            // upscale the position
            left = left * gfx_current_dimensions.width / gfx_current_native_viewport.width;
            top = top * gfx_current_dimensions.height / gfx_current_native_viewport.height;
            // flip Y
            top = gfx_current_dimensions.height - top - 1;
        }
        if (use_back && gfx_msaa_level > 1) {
            // read from the framebuffer we've been rendering to
            fb_src = game_framebuffer;
        }
    }

    gfx_rapi->copy_framebuffer(fb_dst, fb_src, left, top, is_main_fb, (bool)use_back);
}

extern "C" void gfx_reset_framebuffer(void) {
    gfx_rapi->start_draw_to_framebuffer(0, (float)gfx_current_dimensions.height / SCREEN_HEIGHT);
    active_fb = framebuffers.end();
}

