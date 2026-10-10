#define NOMINMAX

#include "floatvtx.h" /* D245 */
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <cstdarg>

#include <map>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <list>
#include <set>
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
#include "portaddr.h"   /* Stage B: arena windows + #95 address model */
#include "envflag.h"
#include "interpportal.h"   /* D578: portal-scissor replay */

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

/* D540: set by the GL backend at init. gfx_rdp_affine: RDP-style
 * screen-affine shade colour. gfx_fog_vertex: 1 = per-vertex RSP fog
 * (default, D543), 0 = the D540 exact per-pixel fog (GE_FOGPIXEL=1). */
int gfx_rdp_affine = 0;
int gfx_fog_vertex = 0;

struct LoadedVertex {
    float x, y, z, w;
    float u, v;
    struct RGBA color;
    uint8_t fog;
    uint8_t clip_rej;
    float fog_n;   /* D540: fog sent to the GPU (0..255): the RSP value (default), or fog * w (GE_FOGPIXEL) */
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
    /* D75: set by gfx_dp_load_block when the load used dxt == 0. The RDP's
     * LoadBlock only swaps odd rows on the way INTO TMEM when its dxt counter
     * advances; TMEM reads always swap odd rows (addr ^ 4). With dxt == 0 the
     * load-side swap never happens, so the sampled image is the memory image
     * with the two 32-bit halves of every 8-byte group swapped on odd rows.
     * Rare relies on this for the Rareware logo's text mip textures. */
    bool dxt0 = false;
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

/* D416: the crop above stretches the LAST set viewport's Y range over the
 * whole window. Correct for one full-screen viewport; in split-screen every
 * player's (half/quarter) viewport got stretched to fill the window, so the
 * last-drawn player covered the others (and the shuffled draw order made the
 * winner alternate frame to frame). While a 2+ player stage is running the
 * N64's own split layout is used unmodified. */
static bool g_split_screen = false;
/* D510: set while the port-layer overlay DL (F10 panel / FPS counter) runs.
 * That is PC chrome, not game content: it maps the logical canvas onto the
 * whole window, bypassing the safe-area crop (which follows the game's last
 * viewport and so differed between the front end and a level). */
static bool g_overlay_window_space = false;
extern "C" void gfx_set_split_screen(int on) {
    g_split_screen = !!on;
}

/* D447: output rect (Video.AspectMode = Original). 0 = fill the window (the
 * historical behaviour, bit-identical). >0 = letter/pillarbox the frame to
 * exactly this aspect, centred; set once per frame by the port layer before
 * gfx_start_frame. g_output_rect is top-left-origin window pixels. */
static float g_output_aspect = 0.0f;
static struct XYWidthHeight g_output_rect = { 0, 0, 0, 0 };

/* D509: the render worker applies a new output aspect at the start of its next
 * frame, but the game thread has already built the next one or two display lists
 * with the old projection (portNativeAspect reads the render side's last
 * aspect). Drawn into the new rect those frames are mis-projected for one or two
 * frames (visible as a flash, worst on the gun/hand model). Hold the previous
 * image for the frames that could carry the old aspect: gfx_run drops them through
 * the same path a minimised window uses. */
static int g_aspect_settle = 0;
#define ASPECT_SETTLE_FRAMES 2

extern "C" void gfx_set_output_aspect(float aspect) {
    const float a = aspect > 0.1f ? aspect : 0.0f;
    if (a != g_output_aspect) g_aspect_settle = ASPECT_SETTLE_FRAMES;
    g_output_aspect = a;
}

extern "C" void gfx_get_output_rect(int32_t *outX, int32_t *outY, int32_t *outW, int32_t *outH) {
    *outX = g_output_rect.x;
    *outY = g_output_rect.y;
    *outW = (int32_t)g_output_rect.width;
    *outH = (int32_t)g_output_rect.height;
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
/* D583 SLOW line: per-game-frame draw counters (all passes), read + reset by
 * gfx_tick_counts() from the render worker. Always on (two increments). */
static unsigned s_tick_batches = 0, s_tick_tris = 0, s_tick_texloads = 0, s_tick_texmiss = 0, s_tick_dynbytes = 0;
static uint64_t s_tick_texns = 0;
extern "C" void gfx_tick_counts(unsigned* batches, unsigned* tris, unsigned* texloads, unsigned* texmiss,
                                unsigned* dynbytes, unsigned* texus) {
    *batches = s_tick_batches; *tris = s_tick_tris; *texloads = s_tick_texloads;
    *texmiss = s_tick_texmiss; *dynbytes = s_tick_dynbytes; *texus = (unsigned)(s_tick_texns / 1000);
    s_tick_batches = s_tick_tris = s_tick_texloads = s_tick_texmiss = s_tick_dynbytes = 0;
    s_tick_texns = 0;
}
struct TickTexTimer {   /* D583 SLOW line: time inside import_texture (all exits) */
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    ~TickTexTimer() {
        s_tick_texns += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    }
};

static void gfx_flush(void) {
    if (buf_vbo_len > 0) {
        s_perf_batches++;
        s_perf_tris += buf_vbo_num_tris;
        s_tick_batches++;
        s_tick_tris += buf_vbo_num_tris;
        gfx_rapi->draw_triangles(buf_vbo, buf_vbo_len, buf_vbo_num_tris);
        buf_vbo_len = 0;
        buf_vbo_num_tris = 0;
    }
}

/* D480: shader pre-warm. A combiner seen for the first time compiles its GL
 * program mid-frame (2.5-11.5 ms each on the dev box; up to ~19 ms frames in
 * play). The generated GLSL is a pure function of (shader_id0, shader_id1) and
 * the GL version, so every pair ever created is remembered in $S/ge007.shaders
 * and compiled at the top of the first gfx_run (and again after a filter change
 * clears the pool). Output-identical; GE_NOSHADERWARM=1 turns it off. */
#define SHADERWARM_HEADER "ge007-shaders v1"
#define SHADERWARM_MAX 1024
static bool s_shader_warm_pending = true;
static int s_shader_list_state = 0; /* 0 = not opened, 1 = writable, -1 = disabled */
static std::string s_shader_list_path;
static std::set<std::pair<uint64_t, uint32_t>> s_shader_list_known;

static bool gfx_shader_list_open(void) {
    if (s_shader_list_state == 0) {
        s_shader_list_state = -1;
        if (GE_ENVFLAG("GE_NOSHADERWARM")) {
            return false;
        }
        s_shader_list_path = sysResolvePath("$S/ge007.shaders");
        bool header_ok = false;
        if (FILE* f = fopen(s_shader_list_path.c_str(), "r")) {
            char line[64];
            if (fgets(line, sizeof line, f) && !strncmp(line, SHADERWARM_HEADER, strlen(SHADERWARM_HEADER))) {
                header_ok = true;
                unsigned long long id0;
                unsigned int id1;
                while (s_shader_list_known.size() < SHADERWARM_MAX && fgets(line, sizeof line, f)) {
                    if (sscanf(line, "%llx %x", &id0, &id1) == 2) {
                        s_shader_list_known.insert(std::make_pair((uint64_t)id0, (uint32_t)id1));
                    }
                }
            }
            fclose(f);
        }
        if (!header_ok) {
            /* missing, foreign or corrupt: start a fresh list */
            FILE* f = fopen(s_shader_list_path.c_str(), "w");
            if (f == NULL) {
                return false;
            }
            fprintf(f, "%s\n", SHADERWARM_HEADER);
            fclose(f);
        }
        s_shader_list_state = 1;
    }
    return s_shader_list_state == 1;
}

static void gfx_shader_list_record(uint64_t shader_id0, uint32_t shader_id1) {
    if (!gfx_shader_list_open() || s_shader_list_known.size() >= SHADERWARM_MAX) {
        return;
    }
    if (!s_shader_list_known.insert(std::make_pair(shader_id0, shader_id1)).second) {
        return;
    }
    if (FILE* f = fopen(s_shader_list_path.c_str(), "a")) {
        fprintf(f, "%016llx %08x\n", (unsigned long long)shader_id0, (unsigned int)shader_id1);
        fclose(f);
    }
}

static void gfx_shader_prewarm(void) {
    s_shader_warm_pending = false;
    if (!gfx_shader_list_open() || s_shader_list_known.empty()) {
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    int compiled = 0;
    gfx_rapi->unload_shader(rendering_state.shader_program);
    rendering_state.shader_program = nullptr;
    for (const auto& id : s_shader_list_known) {
        if (gfx_rapi->lookup_shader(id.first, id.second) == NULL) {
            struct ShaderProgram* prg = gfx_rapi->create_and_load_new_shader(id.first, id.second);
            gfx_rapi->unload_shader(prg);
            compiled++;
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    sysLogPrintf(LOG_NOTE, "SHADERWARM: compiled %d of %d in %.1f ms", compiled, (int)s_shader_list_known.size(), ms);
}

static struct ShaderProgram* gfx_lookup_or_create_shader_program(uint64_t shader_id0, uint32_t shader_id1) {
    struct ShaderProgram* prg = gfx_rapi->lookup_shader(shader_id0, shader_id1);
    if (prg == NULL) {
        gfx_rapi->unload_shader(rendering_state.shader_program);
        prg = gfx_rapi->create_and_load_new_shader(shader_id0, shader_id1);
        rendering_state.shader_program = prg;
        gfx_shader_list_record(shader_id0, shader_id1);
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
    // All boundaries are HOST-space (PORT_DRAM_V1_BASE / PORT_DRAM_K0_BASE
    // already include PORT_ADDR_BASE, see port/include/portaddr.h), so
    // compare the host pointer directly. D591 (docs/dev/findings.md):
    // this used to strip PORT_ADDR_BASE first, putting `a` in N64 raw
    // space (0x70xxxxxx) and comparing it against host-space bounds
    // (0x100070000000 on arm64 macOS). Invisible at PORT_ADDR_BASE==0
    // (x86_64, the two spaces coincide); on arm64 macOS every DRAM / cart
    // source then failed all three range checks and fell through to
    // `return true`, so fast3d bswapped (PD_BE32 copy) every texture
    // source -- a clean rev32 of the whole texture pool (garbled walls,
    // pink ammo HUD; the D219/M-114 symptom class).
    const uintptr_t a = (uintptr_t)addr;
    if (a >= (uintptr_t)PORT_ADDR_BASE + 0x10000000 &&
        a < (uintptr_t)PORT_ADDR_BASE + 0x20000000) return false; // cart map + sidecar
    // V1 dram + KSEG0 mirror: the two mapped views (port/src/dram.c). D441:
    // was `a >= V1 && a < 0x90000000`, which went empty with the arena at
    // 0x90000000 and bswapped every DRAM texture (mirrored texel pairs).
    if (a >= PORT_DRAM_V1_BASE && a < PORT_DRAM_V1_BASE + PORT_DRAM_SIZE) return false;
    if (a >= PORT_DRAM_K0_BASE && a < PORT_DRAM_K0_BASE + PORT_DRAM_SIZE) return false;
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
    s_tick_texloads++;   /* D583 SLOW line */
    TickTexTimer tick_tex_timer;
    LoadedTexture& loaded_texture = rdp.loaded_texture[rdp.texture_tile[tile].tmem];
    const uint8_t fmt = rdp.texture_tile[tile].fmt;
    const uint8_t siz = rdp.texture_tile[tile].siz;
    const uint32_t tex_flags = loaded_texture.tex_flags;
    const uint8_t palette_index = rdp.texture_tile[tile].palette;

    /* D463: a SETTILE with line=0 makes every importer divide by zero
     * (height = size_bytes / line_size_bytes -- 0xc0000094 integer-divide on
     * Windows x64; the crash PC was import_texture_i8). The game emits this
     * legitimately: textrelated.c's outlined-text path renders ANY byte
     * < 0x21 through chars[*text - 0x21], a zeroed fontchar (w=h=0,
     * pixeldata=NULL), and gDPLoadTextureBlock(width=0) then expands to
     * gsDPTile(line=0) + a zero-area gSPTextureRectangle. The N64 RDP draws
     * nothing for a zero-area rect, so the decomp is correct -- fast3d must
     * tolerate it. Skip the import entirely (no cache entry, no upload);
     * the degenerate draw rasterizes no fragments, so whatever stays bound
     * on this unit is never sampled. Nonzero line sizes take the exact
     * original path below. */
    if (rdp.texture_tile[tile].line_size_bytes == 0) {
        static int d463_warned = 0;
        if (d463_warned < 8)
            sysLogPrintf(LOG_NOTE, "D463: texrect on zero-line tile -- import skipped "
                                   "(tile=%d fmt=%u siz=%u tmem=%u)",
                         tile, fmt, siz, rdp.texture_tile[tile].tmem);
        d463_warned++;
        return;
    }

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
        /* D476: a CI4 texture decodes only its 16-entry bank
         * (rdp.palette + palette_index * 16, see import_texture_ci4), so key it
         * on that bank's content. The whole-table D217 hash made unrelated
         * TLUT loads elsewhere in the table re-import unchanged CI4 textures.
         * CI8 reads all 256 entries and keeps the whole-table hash. */
        uint32_t ph = rdp.palette_hash;
        if (siz == G_IM_SIZ_4b) {
            ph = 2166136261u;
            const uint8_t* pb = (const uint8_t*)(rdp.palette + (palette_index & 15) * 16);
            for (uint32_t k = 0; k < 16 * sizeof(uint16_t); ++k) {
                ph ^= pb[k];
                ph *= 16777619u;
            }
        }
        key = { orig_addr, { rdp.palette_addrs[0], rdp.palette_addrs[1] }, fmt, siz, palette_index,
                loaded_texture.size_bytes, ph }; // D217/D476: key on (used) palette content
    } else {
        key = { orig_addr, {}, fmt, siz, palette_index, loaded_texture.size_bytes, 0u };
    }
    /* D75: a dxt==0 load samples with swapped odd rows -> distinct image.
     * Only compiled-in (exe-image) assets: the runtime texture pipeline
     * (tex.c/image.c, 0x70xxxxxx) loads dxt==0 too, but D159 no-ops its
     * compensating pre-swap, so those are already linear. */
    const bool d75_swap = loaded_texture.dxt0 && siz != G_IM_SIZ_32b &&
                          gfx_tex_source_is_c_array(orig_addr) &&
                          rdp.texture_tile[tile].line_size_bytes != 0 &&
                          (rdp.texture_tile[tile].line_size_bytes & 7) == 0 &&
                          loaded_texture.size_bytes > rdp.texture_tile[tile].line_size_bytes;
    if (d75_swap) key.palette_hash ^= 0xD75D75D7u;

    /* Intro blood (M-201): textures the game regenerates IN PLACE in the
     * per-frame dynamic pool (dynAllocate, [g_VtxBuffers[0], g_VtxBuffers[2]))
     * -- e.g. the gun-barrel / death blood-drip image rebuilt by
     * die_blood_image_routine -- reuse the same addresses frame after frame,
     * so an address-only key returns a stale GL texture from an earlier frame.
     * Key those on a content hash (FNV-1a) instead; static textures keep the
     * free address key. */
    {
        /* Game code often reaches this memory through OS_K0_TO_PHYSICAL,
         * which fast3d resolves into the byte-identical KSEG0 mirror at
         * 0x80000000 (port/src/dram.c V2) -- normalise to the V1 view the
         * dyn pool pointers use before the range test. */
        const uint8_t* v1addr = orig_addr;
        if ((uintptr_t)v1addr >= PORT_DRAM_K0_BASE && (uintptr_t)v1addr < PORT_DRAM_K0_BASE + PORT_DRAM_SIZE) {
            v1addr -= (PORT_DRAM_K0_BASE - PORT_DRAM_V1_BASE);
        }
        if (g_VtxBuffers[0] && v1addr >= g_VtxBuffers[0] && v1addr < g_VtxBuffers[2]) {
            uint32_t h = 2166136261u;
            const uint32_t n = loaded_texture.size_bytes;
            s_tick_dynbytes += n;
            for (uint32_t b = 0; b < n; b++) {
                h = (h ^ orig_addr[b]) * 16777619u;
            }
            key.palette_hash = h ? h : 1u;
        }
    }

    if (gfx_texture_cache_lookup(i, key)) {
        return;
    }
    s_tick_texmiss++;

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

    /* D75: emulate the RDP's odd-row TMEM read swap for dxt==0 loads (see
     * LoadedTexture::dxt0). Done after destride so rows are contiguous. */
    std::vector<uint8_t> d75_buf;
    if (d75_swap) {
        const uint32_t ln = rdp.texture_tile[tile].line_size_bytes;
        const uint32_t rows = loaded_texture.size_bytes / ln;
        d75_buf.assign(loaded_texture.addr, loaded_texture.addr + (size_t)rows * ln +
                       (loaded_texture.size_bytes - rows * ln));
        for (uint32_t r = 1; r < rows; r += 2) {
            uint8_t* row = &d75_buf[(size_t)r * ln];
            for (uint32_t b = 0; b + 8 <= ln; b += 8) {
                uint8_t t4[4];
                memcpy(t4, row + b, 4);
                memcpy(row + b, row + b + 4, 4);
                memcpy(row + b + 4, t4, 4);
            }
        }
        loaded_texture.addr = d75_buf.data();
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
        /* D589: raw-vs-normalized source discriminator (diagnostic-only, in
         * the GE_TEXDUMP log family; the rNNN bins dump the NORMALIZED
         * pointer, so a pool-region import classified as a C array dumps a
         * bswapped copy and reads as a clean rev32 against the raw pool even
         * when the pool itself is byte-identical cross-platform). carray is
         * the gfx_tex_source_is_c_array verdict on orig_addr; raw8/norm8 are
         * the first 8 bytes of each view. */
        {
            char hex[64];
            int hi = 0;
            for (int k = 0; k < 8 && k < (int)loaded_texture.size_bytes; k++)
                hi += sprintf(hex + hi, " %02x", ((const uint8_t*)orig_addr)[k]);
            for (int k = 0; k < 8 && k < (int)loaded_texture.size_bytes; k++)
                hi += sprintf(hex + hi, "n %02x", loaded_texture.addr[k]);
            /* D592: log the D75 odd-row-swap verdict (d75) + dxt0 + the
             * RAW-space source address (host ptr - PORT_ADDR_BASE; the low-32
             * address the game uses, a stable cross-platform key for DRAM/cart
             * sources) so a specific texture's import can be aligned across
             * platforms and the d75_swap / classifier divergence localised
             * from the log line alone. The rNNN index (tdc) is positional
             * (camera-dependent import order) and is NOT a stable key. */
            const uint64_t rawaddr =
                (uint64_t)((uintptr_t)orig_addr - (uintptr_t)PORT_ADDR_BASE);
            sysLogPrintf(LOG_NOTE,
                "GE_TEXN[%d] carray=%d dxt0=%d d75=%d rawaddr=%llX line=%u size=%u raw8=%s norm8=%s",
                tdc - 1, gfx_tex_source_is_c_array(orig_addr) ? 1 : 0,
                (int)loaded_texture.dxt0, (int)d75_swap, rawaddr,
                (unsigned)rdp.texture_tile[tile].line_size_bytes,
                (unsigned)loaded_texture.size_bytes, hex, hex);
        }
        /* D592: GE_TEXDUMPROW=1 additionally dumps the FINAL (post-bswap +
         * post-D75-swap + post-destride) source bytes keyed by the stable RAW
         * source address (reusing the texdump/ dir the rNNN dumps use), so a
         * specific texture can be byte-compared across platforms WITHOUT the
         * positional rNNN index. Last import of a given rawaddr wins (the
         * source + transforms are identical per import, so order-independent).
         * Diagnostic-only, no behavior change. */
        static int ge_texdump_row = -1;
        if (ge_texdump_row < 0) ge_texdump_row = getenv("GE_TEXDUMPROW") != NULL;
        if (ge_texdump_row) {
            const uint64_t rawaddr2 =
                (uint64_t)((uintptr_t)orig_addr - (uintptr_t)PORT_ADDR_BASE);
            char nm2[180];
            snprintf(nm2, sizeof nm2, "texdump/row_%llX_f%u_s%u.bin", rawaddr2,
                     (unsigned)fmt, (unsigned)siz);
            FILE* bfr = fopen(nm2, "wb");
            if (bfr) { fwrite(loaded_texture.addr, 1, loaded_texture.size_bytes, bfr); fclose(bfr); }
        }
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
            extern uint32_t num_dls;
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

    /* D589 pin probe: log the import routing decision (does the D229
     * CI-source -> ci8 route fire?) for the first imports, so a same-seed
     * A/B shows whether the two platforms pick the same importer for the
     * same tile. Diagnostic-only, inert unless GE_TEXIMP2 is set. */
    if (getenv("GE_TEXIMP2"))
    {
        static int n_teximp2 = 0;
        if (n_teximp2++ < 64)
            sysLogPrintf(LOG_NOTE,
                "TEXIMP2[%d] tile=%d fmt=%u siz=%u eff=(%u,%u) src_fmt=%u addr=%p size=%u\n",
                n_teximp2 - 1, tile, fmt, siz, fmt_eff, siz_eff,
                loaded_texture.src_fmt, (void *)orig_addr,
                loaded_texture.size_bytes);
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
            const uint8_t maskt = rdp.texture_tile[tile].maskt;
            /* Crop by the TILE line size (the importer's row width): for a
             * LoadBlock, loaded_texture.line_size_bytes is the whole block, so
             * testing against it never cropped (M-201 follow-up: the mip rows
             * then showed as coloured dashes on Frigate's water). */
            const uint32_t d245_row = rdp.texture_tile[tile].line_size_bytes;
            if (fmt == G_IM_FMT_RGBA && siz == G_IM_SIZ_16b && maskt > 0 && maskt < 12 &&
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

/* D578: frame interpolation (opt-in, Video.FpsCap above the VI rate; design
 * from PR #137 by f1zz1ec0ke, reworked onto the D481 render worker).
 *
 * The sim only produces 60 (PAL 50) frames a second. Above that, the render
 * worker draws each game display list several times, once per display
 * refresh that falls before the next game frame, with every G_MTX blended
 * between the previous frame's matrix and this one's by that refresh's
 * position in the tick (alpha; 1 = the frame exactly as the game built it).
 * All passes are drawn when the DL arrives, before SP/DP done is posted, so
 * no game memory is read after the game may reuse it; each finished pass is
 * copied to a present slot (gfx_opengl_interp_store) and the worker swaps the
 * slots on its present clock (port/src/libultra.c). No game state is touched.
 *
 * Matrices are matched frame-to-frame by the geometry they are first used
 * for (the next G_VTX / G_DL / G_FLOATVTX_EXT target; model and room geometry
 * live at stable addresses). Identical models share that geometry (a row of
 * stall doors, sinks), and the game may draw them in another order next
 * frame, so within a group each matrix takes the NEAREST unused previous one
 * (position, plus rotation scaled by distance): matching by draw order
 * blended one door with its neighbour and drew a ghost between them
 * (maintainer playtest, Facility bathroom). No match, or prev/cur too far
 * apart to be one object one tick apart (a camera cut), draws the current
 * value.
 * GE keeps the camera in the modelview matrices (the projection is a pure
 * perspective), so a linear blend of a rotating modelview would shrink it by
 * cos(theta/2): each basis row is blended, then rescaled to the blended
 * length. CPU-transformed screen-space geometry does not move between ticks. */
enum { INTERP_OFF = 0, INTERP_RECORD, INTERP_BLEND };
struct InterpMtx { float m[4][4]; };
static int s_interp_mode = INTERP_OFF;     /* per pass */
static float s_interp_alpha = 1.0f;        /* blend factor of the current pass */
static bool s_interp_record = false;       /* record this pass's raw matrices */
static bool s_interp_have_prev = false;
static uintptr_t s_interp_next_geo = 0;
static std::unordered_map<uint64_t, std::vector<InterpMtx>> s_interp_prev, s_interp_cur;
static std::unordered_map<uint64_t, std::vector<uint8_t>> s_interp_taken;   /* per pass */

static uint64_t gfx_interp_key(uintptr_t geo, bool proj) {
    return ((uint64_t)geo & 0x0000ffffffffffffULL) | (proj ? 0x8000000000000000ULL : 0);
}

/* How far apart two transforms are, in screen-relevant terms: the position
 * change plus the rotation change scaled by distance (a rotation of d at
 * range r moves things by about r*d). */
static float gfx_interp_dist(const float a[4][4], const float b[4][4]) {
    float dt = 0, dr = 0, r2 = 1.0f;
    for (int j = 0; j < 3; j++) {
        dt += (a[3][j] - b[3][j]) * (a[3][j] - b[3][j]);
        r2 += b[3][j] * b[3][j];
        for (int i = 0; i < 3; i++) {
            dr += (a[i][j] - b[i][j]) * (a[i][j] - b[i][j]);
        }
    }
    return dt + dr * r2 / 3.0f;
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

/* Camera correction measured on the last matched modelview LOAD of this pass:
 * corr = inverse(current) * blended. For any static object M = W * V, so this
 * is V_cur^-1 * V_blend whatever W is: the same for every static object.
 * Applied to a modelview with no previous match (a room that just came into
 * view), so it moves with its neighbours instead of sitting at the exact
 * camera while they are blended. */
static float s_interp_corr[4][4];
static bool s_interp_have_corr = false;
static uintptr_t s_interp_corr_key = 0;   /* geo key the corr was measured on (0 = none) */
/* D578 (tunnel vanish): the game's live near/far clip distances (src/fr.c
 * viGetZRange; the runtime D540-scaled values guPerspectiveF is built from).
 * f32 == float. */
extern "C" void viGetZRange(float* zrange);

extern "C" u32 videoGetFrameCount(void);   /* port/src/video.c */
extern "C" float portNativeAspect(void);    /* port/src/video.c (D334) */
extern "C" int current_menu;               /* game front-end state */
#define GE_MENU_RUN_STAGE 11
/* Cached float env-var, one-shot per call site (same pattern as GE_ENVFLAG in
 * envflag.h, which only covers booleans). NULL/empty -> the default. */
#define GE_ENVF(name, def) __extension__({ \
    static float _ge_envf_v = 0.0f; \
    static int _ge_envf_init = 0; \
    if (!_ge_envf_init) { \
        const char* _ge_envf_s = getenv(name); \
        _ge_envf_v = (_ge_envf_s && _ge_envf_s[0]) ? strtof(_ge_envf_s, NULL) : (def); \
        _ge_envf_init = 1; \
    } \
    _ge_envf_v; })

/* D578 depth-clip widening (all three paths below): OFF by default since
 * 2026-10-10 -- equivalent to the old "noclip" state the maintainer verified
 * on the Deck. GE_INTERPCLIP_ON=1 opts back in. (dev switches removed for v0.6.0; see D578) */
static bool interp_clip_off(void) {
    return !GE_ENVFLAG("GE_INTERPCLIP_ON") || GE_ENVFLAG("GE_INTERPCLIP_OFF");
}

/* D578 (tunnel vanish): widen the in-between pass's depth-clip range. A lerp
 * of the two frames' folded projections (guPerspective * lookat, the matrix
 * GE loads for room geometry) cuts the corner between the two source
 * far-clip planes -- each is an exact plane at distance `far` from its own
 * camera, the lerped one sits closer to the camera than either -- and a
 * half-frame of camera travel moves the fixed-distance plane against the
 * scene. Geometry within `far` of BOTH exact cameras is therefore
 * depth-clipped on the in-between pass only: Dam's ridgelines sit at the
 * fog-clipped far distance (D540), so they vanish along the silhouette and
 * the skybox shows through (playtest capture, frames ~1458-1493: sky where
 * the exact pass has rock). Beyond that far distance everything is full fog
 * (the far clip follows the fog end, D540), so the widened range draws only
 * what is already fully fogged -- the cost is a <=~2% fog-ramp remap on the
 * in-between pass (the RSP fog is depth-normalised, gSPFogPosition).
 *
 * This masks the corner-cut vanish on BLENDED passes (cap-fb3, 2026-10-07:
 * removing it brought the ridgeline vanish back the moment the exact-frame
 * fallback disengaged mid-turn). The fallback's hysteresis (below) keeps a
 * turn in exact-presents for its whole duration, so the widened-vs-exact
 * pop at an engage/disengage boundary (the "distant flicker" of cap-fb2)
 * does not occur inside a turn. Knobs: GE_INTERPCLIP_OFF (plain lerp, A/B
 * baseline), GE_INTERPCLIP_FAR (default 1.1), GE_INTERPCLIP_NEAR (default
 * 1.0). Applied to in-between passes only (this call site), projection
 * LOADs only; 60 fps and the exact 120 fps frames are untouched. */
static void gfx_interp_conservative_clip(float matrix[4][4]) {
    /* Was default ON at FAR 1.1 (now OFF, below). Maintainer playtest 2026-10-07 on
     * Dam: 1.5 made distant NPCs flicker through walls, 1.25 partly did too, 0
     * (off) brings back the distant-geometry flicker; 1.1 is the best compromise.
     * GE_INTERPCLIP_OFF=1 disables it. */
    /* 2026-10-10: default OFF. The remap shifts in-between depth by heading
     * (see gfx_interp_conservative_clip_fixed), which drew distant props and
     * shadows through walls (Frigate terminal shadow, Statue Valentin crate;
     * gone on the Deck with "noclip"). Opt back in with GE_INTERPCLIP_ON=1;
     * Dam ridgeline re-check owed. */
    if (interp_clip_off()) {
        return;
    }
    const float kf = GE_ENVF("GE_INTERPCLIP_FAR", 1.1f);
    const float kn = GE_ENVF("GE_INTERPCLIP_NEAR", 1.0f);
    if (kf == 1.0f && kn == 1.0f) {
        return;
    }
    float zr[2];
    viGetZRange(zr);
    const float n = zr[0], f = zr[1];
    if (!(n > 1e-3f && f > n * 1.5f)) {
        return;
    }
    const float n2 = n * kn, f2 = f * kf;
    const float k = 2.0f * n * f / (n - f);   /* signed, <0; P[3][j] = k*L[2][j] */
    const float l22 = matrix[3][2] / k;
    /* Menu / watch projections are built from their OWN guPerspective
     * (title f=10000, watch f=3000, options f=10000), not from the game's
     * current znear/zfar (fr.c builds the main view from g_ViBackData->
     * znear/zfar -- exactly viGetZRange). For a rigid lookat L, the third
     * Pp row is P[3][0..2] = k*L[2][0..2] and L's 3rd row is a unit basis
     * vector, so |P[3][0..2]| == |k|. A mismatch means this matrix was not
     * built from the current znear/zfar -- re-deriving its clip row from
     * (n,f) would be wrong, so leave it. */
    const float knorm = sqrtf(matrix[3][0] * matrix[3][0] +
                              matrix[3][1] * matrix[3][1] +
                              matrix[3][2] * matrix[3][2]);
    if (fabsf(knorm - (-k)) > 0.05f * (-k)) {
        return;
    }
    matrix[2][2] += ((n2 + f2) / (n2 - f2) - (n + f) / (n - f)) * l22;
    matrix[3][2] += (2.0f * n2 * f2 / (n2 - f2) - 2.0f * n * f / (n - f)) * l22;
}

/* D578 (Dam in-between flicker): the corrected far-clip rewrite, UNUSED until
 * the dclamp A/B says whether widening is wanted at all. The room projection
 * is lookat * persp (row vectors, D32), so its z column is
 *   M[i][2] = A*L[i][2] (i < 3),  M[3][2] = A*T.z + B
 * with A = (n+f)/(n-f), B = 2nf/(n-f) and T the lookat translation (nonzero:
 * GE builds the lookat from the camera position relative to the model origin).
 * Changing (n,f) -> (n2,f2) therefore scales the three basis entries by A2/A
 * and shifts only the translation row by the B difference. The live
 * gfx_interp_conservative_clip adds dA*l22 / dB*l22 with l22 = M[3][2]/k
 * (~1, NOT L[2][2]) to M[2][2] / M[3][2] only: a heading-dependent tilt. */
static void __attribute__((unused)) gfx_interp_conservative_clip_fixed(float M[4][4], float n, float f,
                                                                       float n2, float f2) {
    const float A = (n + f) / (n - f), B = 2.0f * n * f / (n - f);
    const float A2 = (n2 + f2) / (n2 - f2), B2 = 2.0f * n2 * f2 / (n2 - f2);
    for (int i = 0; i < 3; i++) {
        M[i][2] *= A2 / A;
    }
    M[3][2] = (M[3][2] - B) * A2 / A + B2;
}

/* Raw (unblended) projection of the most recent G_MTX_PROJECTION LOAD. GE folds
 * the camera lookat into the projection it loads for room geometry (field_10E0
 * = pure perspective * lookat; the room modelview is scale*translation only),
 * so the in-between pass's camera rotation lives here, not in the modelviews.
 * A scissor rectangle the game computed under this raw matrix must be
 * un-projected with it before it is re-projected through the blended one.
 * (D578, maintainer's 4th/5th playtest passes: straight-edged cuts.) */
static float s_interp_proj_raw[4][4];
static bool s_interp_have_proj_raw = false;
/* Raw (N64-unit, pre window mapping) scissor and viewport: x, y = BOTTOM edge
 * (N64 y grows down), width, height. Used to move portal scissors with the
 * camera in in-between passes (gfx_interp_scissor). */
static struct XYWidthHeight s_interp_sc_raw, s_interp_vp_raw;

/* Fast camera turns. The game culls rooms and portals for the CURRENT camera
 * only; on a fast flick an in-between pass looks back toward where the
 * camera was and finds rooms that are simply not in this frame's display list
 * (fog colour shows through: maintainer playtest, Dam tunnel entrance). No
 * blend can draw what is not there, so a frame whose camera turns faster than
 * this per tick is shown exactly (its in-between presents repeat it). The turn
 * is measured on the first modelview LOADs of the pass (the rooms come first).
 * D578 (tunnel holes, step b/c): the culling strip appears on the *moderate*
 * turns below the gate too, so the threshold is env-overridable
 * (GE_INTERP_FAST_TURN; degrees/tick, default 6.0; interp_fast_turn_deg). */
static int s_interp_loads_seen = 0;
static bool s_interp_fast = false;
/* D578: matrices this blend pass could not pair with the previous frame (new
 * rooms/objects), plus previous-frame matrices nobody claimed (vanished ones),
 * counted after the pass. Nonzero means the visible set changed between game
 * ticks: a blend cannot draw what is missing from one list, so the frame is
 * presented exact. */
static int s_interp_unmatched = 0;
/* D578: why the current frame fell back / was softened (stats): 1 = turn,
 * 2 = set change. s_interp_clamp_req: a moderate turn asks for the frame's
 * alphas to be pulled toward 1 by this factor (0 = none) instead of going
 * fully exact; s_interp_clamp_active: the frame is already a clamped re-run. */
static int s_interp_why = 0;
static int s_interp_body_runs = 0;
static float s_interp_clamp_req = 0.0f;
static bool s_interp_clamp_active = false;
static unsigned s_interp_cnt_turn = 0, s_interp_cnt_clamp = 0, s_interp_cnt_set = 0;
static unsigned s_interp_cnt_room = 0;
/* D583 (Deck 90): decide a frame's turn handling at its first blended
 * projection load, not after the whole pass is drawn. The turn is measured on
 * the projection (the camera), and nothing drawn before it depends on alpha
 * (modelviews are drawn as loaded, the sky is always this frame's), so the
 * pass can switch to its softened alpha, or to exact, right there instead of
 * being finished and redrawn. Restarting drew 3-5 passes for a 2-present frame
 * (8-10 ms on the Deck's Dam wall, against an 11.1 ms refresh).
 * s_interp_inplace_ok: set by gfx_interp_tick for the frame's first pass only;
 * s_interp_inplace_exact / _req: what that pass switched to. */
static bool s_interp_inplace_ok = false;
static bool s_interp_inplace_exact = false;
static float s_interp_inplace_req = 0.0f;
static int s_interp_proj_blends = 0;   /* blended projection loads this pass */
/* D583: projection loads this pass whose nearest previous one is / is not a
 * plausible one-tick move. A cut moves every projection; one odd projection
 * (a view with no partner last frame) is not a cut (first build fired on 8
 * consecutive Deck frames, 23-33 per 10 s). */
static int s_interp_cut = 0, s_interp_proj_ok = 0;
/* D583: the turn is first measured on whichever projection load shows it,
 * often not the pass's first (GE loads one per room), and switching alpha
 * there would tear between rooms already drawn; so a frame is pre-softened at
 * its start from the previous frame's turn (a camera turn is smooth) and only
 * redrawn when its own turn needs > 25% more softening. s_interp_pre_req =
 * the factor applied at the start of this frame (1 = none). */
static float s_interp_pre_req = 1.0f, s_interp_next_req = 1.0f, s_interp_turn_max = 0.0f;
static unsigned s_interp_cnt_pre_soft = 0;
static unsigned s_interp_cnt_soft_skipped = 0;   /* D583: late softens skipped by the budget gate */
static unsigned s_interp_cnt_ip_exact = 0, s_interp_cnt_ip_soft = 0, s_interp_cnt_late_exact = 0,
                s_interp_cnt_late_soft = 0;
static unsigned s_interp_hid_entering = 0;
static uint32_t s_interp_hid_ids[8];   /* D583: rooms hidden this frame (ROOMSET log) */
static int s_interp_hid_n = 0;   /* D583: entering rooms hidden in in-between passes */
/* D578 (room-change-exact): bg.c draws every room as gSPSegment(14 =
 * SPSEGMENT_BG_VTX, room vertices) + gSPDisplayList, inside a per-room portal
 * scissor. The visible room set and its scissors are computed for the CURRENT
 * camera only, so when the set differs from the previous game frame an
 * in-between pass has rooms missing (fog-colour holes) or extra. The
 * segment-14 base (unique per room) identifies the room; the sorted, unique
 * list is compared prev -> cur and a difference makes the frame exact
 * (GE_INTERPROOM_OFF=1 disables). */
static std::vector<uintptr_t> s_interp_rooms_pass;
/* D578: last gSPSegment(14) value (room vertices base) of this pass; maps a
 * room scissor to its room for the portal replay (interpportal.c). */
static uint32_t s_interp_seg14 = 0;
static std::vector<uintptr_t> s_interp_rooms_prev;
static bool s_interp_rooms_prev_valid = false;

static float interp_fast_turn_deg(void) {
    return GE_ENVF("GE_INTERP_FAST_TURN", 6.0f);
}

/* D578 (tunnel holes, step d -- the fix): the in-between pass repeats the
 * EXACT present (camera N, the list-owning camera) when the blended camera's
 * TOTAL offset from camera N exceeds a small threshold, instead of the fast
 * per-tick gate's extrapolated turn rate. The gate (above) measures the
 * modelview correction and cannot see the room turns at all -- GE folds the
 * camera lookat into the projection it loads (field_10E0 = perspective*lookat,
 * the room modelviews are scale*translation only, so their correction is
 * translation-only and tickDeg stays 0) -- which is why the cap-gate A/B run
 * saw zero substitutions at either the 6 or 3 deg gate while the tunnel holes
 * still showed (the missing-room-culling hypothesis: the list is built for
 * tick N's camera; the blended camera, even a half-tick behind, can look past
 * a culling boundary N's list culled). Reusing s_interp_fast below (the gate's
 * own mechanism) makes every present of the frame exact; the in-between
 * present shows N instead of the (possibly culling-mismatched) blend.
 *
 * The offset is the view-angle between the two folded projections' lookat
 * rows. For P = perspective*lookat the lookat rows sit in P's rows 0, 1 and
 * 3 (P[3][0..2] = (2nf/(n-f))*L[2], a common scale for any pair built from
 * the same znear/zfar), so
 *   cos(offset) = (d0 + d1 + d2 - 1) / 2   (d_i = dot of the normalised rows)
 * (trace(Ra^T*Rr) - 1)/2 for orthonormal L rows). The 6 deg fast-turn gate is
 * kept as the large-turn backstop; this supersedes it for moderate turns.
 *
 * Default ON; GE_INTERPFALLBACK_OFF=1 disables it (pre-fix behaviour, for A/B).
 * Threshold GE_INTERPFALLBACK_DEG (default 1.5 deg PER TICK, alpha-normalised).
 *
 * Alpha-normalisation (cap-fb run 1): comparing the raw blended-vs-exact
 * offset to a fixed threshold is pacing-sensitive -- the present lands at a
 * jittering fraction of the tick (alpha 0.15-0.23 observed, not a steady
 * 0.5), so the offset from camera N swings with the pacing and the 1.5 deg
 * threshold fired only intermittently on a ~2.5 deg/tick turn (holes kept
 * showing on the ON segment, frames 3519+). Dividing by (1-alpha) makes the
 * threshold mean the per-tick view turn, pacing-independent. */
static int interp_fallback_on(void) {
    static int on = -1;
    if (on < 0) {
        on = GE_ENVF("GE_INTERPFALLBACK_OFF", 0.0f) > 0.0f ? 0 : 1;
    }
    return on;
}

/* View angle between the two folded projections' cameras, degrees. A/B are
 * the raw (camera N) and blended matrices; a pair built from the same
 * znear/zfar shares the row scales, so the normalised row dots are well
 * defined. 0.0 when the rows carry no lookat (degenerate / not a product).
 * Rows 0, 1: right and up; row 3: the view direction L[2] (common sign). */
static float gfx_interp_cam_offset_deg(const float A[4][4], const float B[4][4]) {
    static const int rows[3] = { 0, 1, 3 };
    float dsum = 0.0f;
    for (int r = 0; r < 3; r++) {
        const int i = rows[r];
        const float la2 = A[i][0] * A[i][0] + A[i][1] * A[i][1] + A[i][2] * A[i][2];
        const float lb2 = B[i][0] * B[i][0] + B[i][1] * B[i][1] + B[i][2] * B[i][2];
        if (la2 < 1e-12f || lb2 < 1e-12f) {
            return 0.0f;
        }
        dsum += (A[i][0] * B[i][0] + A[i][1] * B[i][1] + A[i][2] * B[i][2]) /
                sqrtf(la2 * lb2);
    }
    float c = (dsum - 1.0f) * 0.5f;
    if (c > 1.0f) {
        c = 1.0f;
    } else if (c < -1.0f) {
        c = -1.0f;
    }
    return acosf(c) * 57.29578f;
}

/* Inverse of an affine row-vector matrix (last column 0,0,0,1). */
static bool gfx_interp_affine_inverse(const float m[4][4], float out[4][4]) {
    if (fabsf(m[0][3]) > 1e-4f || fabsf(m[1][3]) > 1e-4f || fabsf(m[2][3]) > 1e-4f || fabsf(m[3][3] - 1.0f) > 1e-4f) {
        return false;
    }
    const float a = m[0][0], b = m[0][1], c = m[0][2];
    const float d = m[1][0], e = m[1][1], f = m[1][2];
    const float g = m[2][0], h = m[2][1], k = m[2][2];
    const float det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
    if (fabsf(det) < 1e-12f) {
        return false;
    }
    const float id = 1.0f / det;
    float r[3][3] = {
        { (e * k - f * h) * id, (c * h - b * k) * id, (b * f - c * e) * id },
        { (f * g - d * k) * id, (a * k - c * g) * id, (c * d - a * f) * id },
        { (d * h - e * g) * id, (b * g - a * h) * id, (a * e - b * d) * id },
    };
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            out[i][j] = r[i][j];
        }
        out[i][3] = 0.0f;
    }
    for (int j = 0; j < 3; j++) {
        out[3][j] = -(m[3][0] * r[0][j] + m[3][1] * r[1][j] + m[3][2] * r[2][j]);
    }
    out[3][3] = 1.0f;
    return true;
}

static void gfx_interp_apply_corr(float matrix[4][4], bool proj, bool load) {
    float inv[4][4];
    if (!proj && load && s_interp_have_corr && gfx_interp_affine_inverse(matrix, inv)) {
        gfx_matrix_mul(matrix, matrix, s_interp_corr);
    }
}

/* D583: the last projection pair given to the portal replay (pre-widening). */
static float s_interp_note_bl[4][4];
static void gfx_interp_note_proj(const float raw[4][4], const float bl[4][4]) {
    memcpy(s_interp_note_bl, bl, sizeof(s_interp_note_bl));
    interpPortalNoteProj(raw, bl);
}

/* D583: per-pass memo of blended projections. GE loads the same camera
 * projection ~60 times a pass (per room / object); each load was paired
 * greedily with the nearest unclaimed one of the previous frame, so when the
 * count changed (a room or object came or went) one load found no partner and
 * that object was drawn at the CURRENT camera while the rest used the blended
 * one: on an in-between present it jumped (Deck: door / wall alarm in a Dam
 * guard tower flickered; gone with every present exact). An identical source
 * projection now always gets the identical blended result within a pass. The
 * pairing still runs (set-change bookkeeping). */
struct InterpProjMemo {
    float raw[4][4], out[4][4], note[4][4];
};
static InterpProjMemo s_interp_pmemo[16];
static int s_interp_pmemo_n = 0;
static unsigned s_interp_pmemo_hit = 0, s_interp_pmemo_diff = 0;

static void gfx_interp_matrix_body(float matrix[4][4], bool proj, bool load);
static void gfx_interp_matrix(float matrix[4][4], bool proj, bool load) {
    if (!proj || !load || s_interp_mode != INTERP_BLEND || s_interp_alpha >= 1.0f) {
        gfx_interp_matrix_body(matrix, proj, load);
        return;
    }
    float raw[4][4];
    memcpy(raw, matrix, sizeof(raw));
    int hit = -1;
    for (int k = 0; k < s_interp_pmemo_n; k++) {
        if (!memcmp(s_interp_pmemo[k].raw, raw, sizeof(raw))) {
            hit = k;
            break;
        }
    }
    gfx_interp_matrix_body(matrix, proj, load);
    if (hit >= 0) {
        const InterpProjMemo& m = s_interp_pmemo[hit];
        s_interp_pmemo_hit++;
        if (memcmp(m.out, matrix, sizeof(m.out)) != 0) {
            s_interp_pmemo_diff++;
            memcpy(matrix, m.out, sizeof(m.out));
            gfx_interp_note_proj(raw, m.note);
        }
    } else if (s_interp_pmemo_n < 16) {
        InterpProjMemo& m = s_interp_pmemo[s_interp_pmemo_n++];
        memcpy(m.raw, raw, sizeof(raw));
        memcpy(m.out, matrix, sizeof(m.out));
        memcpy(m.note, s_interp_note_bl, sizeof(m.note));
    }
}

static void gfx_interp_matrix_body(float matrix[4][4], bool proj, bool load) {
    const uint64_t key = gfx_interp_key(proj ? 0 : s_interp_next_geo, proj);
    if (proj && load) {
        /* Raw matrix as the game loaded it, before this pass blends it. */
        memcpy(s_interp_proj_raw, matrix, sizeof(s_interp_proj_raw));
        s_interp_have_proj_raw = true;
        gfx_interp_note_proj(matrix, matrix);   /* D578: blended == raw unless the blend below says otherwise */
    }
    if (s_interp_record) {
        InterpMtx rec;
        memcpy(rec.m, matrix, sizeof(InterpMtx));
        s_interp_cur[key].push_back(rec);
    }
    if (s_interp_mode != INTERP_BLEND || s_interp_alpha >= 1.0f) {
        return;
    }
    auto it = s_interp_prev.find(key);
    if (it == s_interp_prev.end()) {
        s_interp_unmatched++;
        if (proj) gfx_interp_apply_corr(matrix, proj, load);
        return;
    }
    const std::vector<InterpMtx>& cand = it->second;
    std::vector<uint8_t>& taken = s_interp_taken[key];
    taken.resize(cand.size(), 0);
    int best = -1;
    float bestd = 0;
    const bool shareProj = proj;
    for (size_t j = 0; j < cand.size(); j++) {
        /* D583: a projection is a camera that many loads share; it pairs with
         * the nearest previous one even if another load claimed it. One-to-one
         * pairing ran out of partners when the count changed (the outside
         * rooms seen from a Dam guard tower window coming and going) and paired
         * a load with another kind of projection: the wall alarm vanished for
         * one in-between present. */
        if (taken[j] && !shareProj) {
            continue;
        }
        const float d = gfx_interp_dist(cand[j].m, matrix);
        if (best < 0 || d < bestd) {
            best = (int)j;
            bestd = d;
        }
    }
    if (best < 0) {
        s_interp_unmatched++;
        if (proj) gfx_interp_apply_corr(matrix, proj, load);
        return;
    }
    taken[best] = 1;
    if (!proj) {
        /* D578 (Dam tunnel / gap flicker): in-between passes draw modelviews
         * exactly as the game loaded them; only the projection (which carries
         * the camera) is blended. Nearest-transform blending of modelviews
         * drew room/prop geometry between two instances (maintainer Deck A/B
         * 2026-10-09). The pairing above still runs so the set-change
         * detector below counts matched matrices correctly (skipping it made
         * nearly every frame "a cut" and exact, which also dragged VSync
         * pacing to 60). */
        return;
    }
    if (!gfx_interp_plausible(cand[best].m, matrix, proj)) {
        if (proj && load) {
            s_interp_cut++;   /* D583: camera jump, see the cut check in gfx_interp_tick */
        }
        return;
    }
    if (proj && load) {
        s_interp_proj_ok++;
    }
    const float t = s_interp_alpha;
    const float (*a)[4] = cand[best].m;
    float raw[4][4];
    memcpy(raw, matrix, sizeof(raw));
    for (int i = 0; i < 4; i++) {
        float la = 0, lb = 0, lr = 0;
        for (int j = 0; j < 4; j++) {
            const float v = a[i][j] + (matrix[i][j] - a[i][j]) * t;
            if (j < 3) {
                la += a[i][j] * a[i][j];
                lb += matrix[i][j] * matrix[i][j];
                lr += v * v;
            }
            matrix[i][j] = v;
        }
        /* Basis rows of a modelview: keep the blended length (no shrink). */
        if (!proj && i < 3 && lr > 1e-12f) {
            const float want = sqrtf(la) + (sqrtf(lb) - sqrtf(la)) * t;
            const float k = want / sqrtf(lr);
            for (int j = 0; j < 3; j++) {
                matrix[i][j] *= k;
            }
        }
    }
    if (!proj && load) {
        float inv[4][4];
        if (gfx_interp_affine_inverse(raw, inv)) {
            gfx_matrix_mul(s_interp_corr, inv, matrix);
            s_interp_have_corr = true;
            s_interp_corr_key = key;   /* which geometry's matrix the corr came from */
            rdp.viewport_or_scissor_changed = true;   /* portal scissor follows (gfx_interp_scissor) */
            if (s_interp_loads_seen < 8 && t < 1.0f) {
                s_interp_loads_seen++;
                const float (*c)[4] = s_interp_corr;
                const float det = c[0][0] * (c[1][1] * c[2][2] - c[1][2] * c[2][1]) -
                                  c[0][1] * (c[1][0] * c[2][2] - c[1][2] * c[2][0]) +
                                  c[0][2] * (c[1][0] * c[2][1] - c[1][1] * c[2][0]);
                const float sc = cbrtf(fabsf(det));
                if (sc > 1e-6f) {
                    float cs = ((c[0][0] + c[1][1] + c[2][2]) / sc - 1.0f) * 0.5f;
                    cs = cs > 1.0f ? 1.0f : (cs < -1.0f ? -1.0f : cs);
                    const float tickDeg = acosf(cs) * 57.29578f / (1.0f - t);
                    /* D578 (a): same graded scheme as the projection check
                     * below -- soften (alphas pulled toward 1) from softDeg,
                     * fully exact only above the hard limit (the larger of
                     * the fast-turn gate and GE_INTERPFALLBACK_HARD). The
                     * old 6 deg cliff was the real turn-exact trigger. */
                    const float softDeg = GE_ENVF("GE_INTERPFALLBACK_DEG", 1.5f);
                    const float hardLim = fmaxf(interp_fast_turn_deg(), GE_ENVF("GE_INTERPFALLBACK_HARD", 8.0f));
                    if (tickDeg > hardLim) {
                        s_interp_fast = true;
                        if (!s_interp_why) {
                            s_interp_why = 1;
                        }
                    } else if (!s_interp_clamp_active && tickDeg > softDeg) {
                        const float req = softDeg / tickDeg;
                        if (s_interp_clamp_req == 0.0f || req < s_interp_clamp_req) {
                            s_interp_clamp_req = req;
                        }
                    }
                }
            }
        }
    }
    if (proj && load) {
        gfx_interp_note_proj(raw, matrix);   /* D578: blended projection, before the far-clip widening */
        /* D583: the turn below is measured on this, not on the widened matrix:
         * the widening alone reads as a ~0.16 deg camera offset, and divided by
         * (1 - alpha) on a pass whose alpha is just under 1 (the lag falls
         * slowly) it became 3 deg/tick: a "softened" restart of the whole frame
         * that changed nothing (100-230 per 10 s on the Deck). */
        float blended[4][4];
        memcpy(blended, matrix, sizeof(blended));
        gfx_interp_conservative_clip(matrix);
        /* D583: only on a pass well away from exact. Near alpha 1 the offset is
         * float rounding in the angle (~0.05 deg), and divided by (1 - alpha)
         * (floor 0.05) it read as a 1.5+ deg/tick turn: still 20-160 "softened"
         * whole-frame redraws per 10 s on the Deck after the clip fix. A pass
         * that close to exact cannot need softening; the frame's earlier pass
         * (alpha ~0.3-0.5) measures the turn. */
        if (interp_fallback_on()) {
            /* D578 (tunnel holes, step d): total blended-vs-exact offset on
             * the projection (the camera turn the modelview gate cannot see).
             * Normalised by (1-alpha) it is the per-tick view turn, pacing-
             * independent. Hysteresis (cap-fb2 -> cap-fb3): the fallback
             * engages at GE_INTERPFALLBACK_DEG (default 1.5 deg/tick) and
             * releases only below 0.5, so a turn stays in exact presents for
             * its whole duration -- the engage/disengage boundary cannot
             * alternate widened-blended / exact frames mid-turn (the distant
             * flicker of cap-fb2), and the far-clip corner-cut cannot show
             * on the blended frames a turn's dips would otherwise produce
             * (the vanish of cap-fb3). Above the threshold: exact frames
             * for every present of this frame. */
            const float off = gfx_interp_cam_offset_deg(raw, blended);
            /* D583: near alpha 1 the offset is float rounding in the angle
             * (~0.05 deg); divided by (1 - alpha) it read as a 1.5+ deg/tick
             * turn (20-160 needless redraws per 10 s on the Deck). Measured only
             * where it means something. */
            const bool meas = (1.0f - s_interp_alpha) > 0.1f || off > 0.25f;
            const float perTick = meas ? off / fmaxf(1.0f - s_interp_alpha, 0.05f) : 0.0f;
            if (perTick > s_interp_turn_max) {
                s_interp_turn_max = perTick;
            }
            /* Two-level (D578 follow-up, Deck 90 Hz: 9-64% of frames went
             * exact on ordinary stick turns): between GE_INTERPFALLBACK_DEG
             * (1.5) and GE_INTERPFALLBACK_HARD (default 8 deg/tick, a genuine
             * cut/flick) the frame is re-run with its alphas pulled toward 1
             * so the blended offset equals the threshold-level offset that
             * showed no holes (still moves smoothly, just less of the step);
             * only above HARD (hysteresis release at 70%) go fully exact. */
            const float softDeg = GE_ENVF("GE_INTERPFALLBACK_DEG", 1.5f);
            const float hardDeg = GE_ENVF("GE_INTERPFALLBACK_HARD", 8.0f);
            static bool s_fb_engaged = false;
            if (perTick > hardDeg) {
                s_fb_engaged = true;
            } else if (meas && perTick < hardDeg * 0.7f) {
                s_fb_engaged = false;
            }
            /* D583: first blended projection of the frame's first pass, nothing
             * alpha-dependent drawn yet: switch this pass in place (see
             * s_interp_inplace_ok). */
            const bool inplace = s_interp_inplace_ok && s_interp_proj_blends == 0 && !s_interp_have_corr;
            /* Softening still needed beyond what the frame start applied. */
            const float addReq = perTick > softDeg ? (softDeg / perTick) / s_interp_pre_req : 1.0f;
            if (!s_fb_engaged && !s_interp_clamp_active && addReq < (s_interp_pre_req < 1.0f ? 0.8f : 1.0f) &&
                s_interp_clamp_req == 0.0f) {
                if (inplace) {
                    const float req = addReq;
                    const float t2 = 1.0f - (1.0f - t) * req;
                    for (int i = 0; i < 4; i++) {
                        for (int j = 0; j < 4; j++) {
                            matrix[i][j] = a[i][j] + (raw[i][j] - a[i][j]) * t2;
                        }
                    }
                    gfx_interp_note_proj(raw, matrix);
                    gfx_interp_conservative_clip(matrix);
                    s_interp_alpha = t2;
                    s_interp_inplace_req = req;
                    s_interp_clamp_active = true;
                } else {
                    s_interp_clamp_req = addReq;
                }
            }
            if (s_fb_engaged && inplace) {
                memcpy(matrix, raw, sizeof(raw));   /* this pass becomes the frame's exact image */
                gfx_interp_note_proj(raw, raw);
                s_interp_alpha = 1.0f;
                s_interp_inplace_exact = true;
                if (!s_interp_why) {
                    s_interp_why = 1;
                }
            } else if (s_fb_engaged) {
                s_interp_fast = true;
                if (!s_interp_why) {
                    s_interp_why = 1;
                }
                static int fblog = 0;
                if (fblog++ < 8 || fblog % 120 == 0) {
                    fprintf(stderr,
                            "D578 FALLBACK frame %u alpha=%.2f cam offset %.2f (%.1f deg/tick) -> exact presents\n",
                            (unsigned)videoGetFrameCount(), s_interp_alpha, off, perTick);
                }
            }
        }
        s_interp_proj_blends++;
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

    if (s_interp_mode != INTERP_OFF && !addr_bad) {   /* D578 */
        gfx_interp_matrix(matrix, (parameters & G_MTX_PROJECTION) != 0, (parameters & G_MTX_LOAD) != 0);
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

/* D474 (low-end perf): one cached gate for the per-triangle / per-vertex debug
 * hooks (D75D, ZF, D303). All three are off for players; without this every
 * triangle paid several calls + frame-counter reads just to return. Same
 * enable semantics as each hook's own lazy init (non-empty env value). */
static int s_tri_dbg = -1;
static inline bool tri_dbg(void) {
    if (s_tri_dbg < 0) {
        const char* a = getenv("GE_D75D");
        const char* b = getenv("GE_ZF");
        const char* c = getenv("GE_D303");
        const char* d = getenv("GE_D526");
        s_tri_dbg = ((a && *a) || (b && *b) || (c && *c) || (d && *d)) ? 1 : 0;
    }
    return s_tri_dbg != 0;
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

/* D526 (docs/dev/findings.md §D526): per-triangle draw-state census
 * probe, text-only. Retained as a generic transparency/texture triage
 * tool (the D195/D266/D268/D273 bug class; the P13 Dam-ending grate it
 * targeted, D526, was confirmed faithful N64 behaviour on 1964/GEPD,
 * so no fix was needed). GE_D526="lo-hi" = sim-frame range (the same
 * dash syntax as GE_D75D); optional GE_D526BOX="x0 y0 x1 y1" targets
 * an NDC region (same syntax as GE_ZFBOX; default = on-screen);
 * GE_D526MAX caps per-triangle lines (default 400). For every emitted
 * triangle in range whose NDC bbox intersects the region, one line:
 * frame, NDC bbox, z/w, geometry_mode, other_mode_l/h, combine word,
 * sampled tile 0 (and 1 in 2-cycle) tmem/fmt/siz, first_tile_index,
 * prim alpha, vertex alpha min/max, use_alpha/modulate/invisible.
 * Plus one per-frame line aggregating the in-region tris by
 * (comb, oml, omh, alpha) signature, so a suspect surface's signature
 * stands out without enumerating every tri. Zero cost unless
 * GE_D75D/GE_ZF/GE_D303/GE_D526 is set (folded into the D474 tri_dbg
 * gate). */
static int d526_on = -1;
static int d526_lo = -1, d526_hi = 0x7FFFFFFF, d526_max = 400;
static uint32_t d526_lines = 0;
static float d526_box[4] = { -1.05f, -1.05f, 1.05f, 1.05f };
static int d526_box_on = 0;
#define D526_SIGS 32
static struct { uint64_t comb; uint32_t oml, omh; int alpha; uint32_t cnt; } d526_sigs[D526_SIGS];
static int d526_nsigs = 0;
static uint32_t d526_sig_frame = 0, d526_sig_total = 0;

static void d526_flush_sigs(uint32_t f) {
    if (d526_nsigs == 0) { d526_sig_frame = f; return; }
    fprintf(stderr, "D526F: f=%u inbox=%u sigs: ", f, d526_sig_total);
    for (int i = 0; i < d526_nsigs; i++)
        fprintf(stderr, "(comb=0x%016llx oml=0x%08x omh=0x%08x a=%d)x%u ",
                (unsigned long long)d526_sigs[i].comb, d526_sigs[i].oml,
                d526_sigs[i].omh, d526_sigs[i].alpha, d526_sigs[i].cnt);
    fprintf(stderr, "\n");
    d526_nsigs = 0; d526_sig_total = 0;
    d526_sig_frame = f;
}
static void d526_atexit(void) { d526_flush_sigs(0xFFFFFFFFu); }
static void d526_init(void) {
    if (d526_on >= 0) return;
    d526_on = 0;
    const char* e = getenv("GE_D526");
    if (e && *e) {
        d526_on = 1;
        d526_lo = atoi(e);
        const char* dash = strchr(e, '-');
        if (dash) d526_hi = atoi(dash + 1);
    }
    if (d526_on) {
        const char* m = getenv("GE_D526MAX");
        if (m && *m) d526_max = atoi(m);
        const char* b = getenv("GE_D526BOX");
        if (b && *b && sscanf(b, "%f %f %f %f",
                              &d526_box[0], &d526_box[1], &d526_box[2], &d526_box[3]) == 4)
            d526_box_on = 1;
        atexit(d526_atexit);
    }
}
static void d526_note_emit(uint32_t f, struct LoadedVertex* const* v_arr, int use_alpha, int use_modulate) {
    d526_init();
    if (!d526_on) return;
    if (f < (uint32_t)d526_lo || f > (uint32_t)d526_hi) return;
    if (f != d526_sig_frame) d526_flush_sigs(f);
    float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
    float zsum = 0.f, wsum = 0.f; int vmin = 255, vmax = -1, ok = 0;
    for (int i = 0; i < 3; i++) {
        float w = v_arr[i]->w;
        if (w <= 0.f) continue;
        float nx = v_arr[i]->x / w, ny = v_arr[i]->y / w;
        if (nx < mnx) mnx = nx; if (nx > mxx) mxx = nx;
        if (ny < mny) mny = ny; if (ny > mxy) mxy = ny;
        zsum += v_arr[i]->z / w; wsum += w;
        int a = v_arr[i]->color.a;
        if (a < vmin) vmin = a; if (a > vmax) vmax = a;
        ok = 1;
    }
    if (!ok) return;
    if (d526_box_on) {
        if (mxx < d526_box[0] || mnx > d526_box[2] || mxy < d526_box[1] || mny > d526_box[3]) return;
    } else if (mxx < -1.05f || mnx > 1.05f || mxy < -1.05f || mny > 1.05f) return; // off-screen
    d526_sig_total++;
    int found = 0;
    for (int i = 0; i < d526_nsigs; i++) {
        if (d526_sigs[i].comb == rdp.combine_mode && d526_sigs[i].oml == rdp.other_mode_l &&
            d526_sigs[i].omh == rdp.other_mode_h && d526_sigs[i].alpha == use_alpha) {
            d526_sigs[i].cnt++; found = 1; break;
        }
    }
    if (!found && d526_nsigs < D526_SIGS) {
        d526_sigs[d526_nsigs].comb = rdp.combine_mode;
        d526_sigs[d526_nsigs].oml = rdp.other_mode_l;
        d526_sigs[d526_nsigs].omh = rdp.other_mode_h;
        d526_sigs[d526_nsigs].alpha = use_alpha;
        d526_sigs[d526_nsigs].cnt = 1;
        d526_nsigs++;
    }
    if (d526_lines++ >= (uint32_t)d526_max) return;
    const uint32_t fi = rdp.first_tile_index;
    const uint32_t t0 = fi + gfx_lod_tile_offset(0);
    const uint32_t omh = rdp.other_mode_h;
    const int two = ((omh & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_2CYCLE);
    const uint32_t t1 = two ? fi + gfx_lod_tile_offset(1) : t0;
    const int inv = (rdp.other_mode_l & (3u << 24)) == ((uint32_t)G_BL_0 << 24) &&
                    (rdp.other_mode_l & (3u << 20)) == ((uint32_t)G_BL_CLR_MEM << 20);
    fprintf(stderr,
        "D526: f=%u ndc=(%.3f,%.3f)-(%.3f,%.3f) zw=%.4f gm=0x%08x oml=0x%08x omh=0x%08x comb=0x%016llx fi=%u "
        "tile0[tmem=%u fmt=%u siz=%u]",
        f, mnx, mny, mxx, mxy, (wsum > 0.f ? zsum / wsum : -1.f),
        rsp.geometry_mode, rdp.other_mode_l, omh,
        (unsigned long long)rdp.combine_mode, fi,
        rdp.texture_tile[t0].tmem, rdp.texture_tile[t0].fmt, rdp.texture_tile[t0].siz);
    if (two)
        fprintf(stderr, " tile1[tmem=%u fmt=%u siz=%u]",
                rdp.texture_tile[t1].tmem, rdp.texture_tile[t1].fmt, rdp.texture_tile[t1].siz);
    fprintf(stderr, " pa=%u va=[%d,%d] alpha=%d mod=%d inv=%d 2cyc=%d\n",
            rdp.prim_color.a, vmin, vmax, use_alpha, use_modulate, inv, two);
}

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
            /* D540: the RSP divides by a negative w as-is: a vertex behind
             * the camera gets z/w at/beyond the far end of the range (full
             * fog), continuously. The old override (winv < 0 -> +32767)
             * flipped the sign, so a corner crossing behind the player
             * jumped no-fog <-> full-fog and big ground triangles popped
             * one at a time (Deck playtest). */
            if (!gfx_fog_vertex && winv < 0.0f) {
                winv = std::numeric_limits<int16_t>::max();
            }

            float fog_z = z * winv * rsp.fog_mul + rsp.fog_offset;
            d->fog = clampf(fog_z, 0.f, 255.f);
            /* D543 default: the per-vertex RSP value (as the N64; D543's CPU
             * near clip lerps it for clip-created vertices like the RSP).
             * GE_FOGPIXEL=1 (D540): the GPU gets fog * w (z*mul + w*off,
             * linear in clip space) and the shader divides by w -- exact
             * per-pixel fog. The D503 full-fog snap is opt-in (GE_FOGSNAP=1). */
            d->fog_n = gfx_fog_vertex ? d->fog
                                      : d->z * rsp.fog_mul + d->w * rsp.fog_offset;
        } else {
            d->fog = rdp.fog_color.a;
            d->fog_n = gfx_fog_vertex ? d->fog : d->fog * d->w;
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
        /* D473: a separate init flag. d75v_lo == -1 also means "disabled", so
         * the old `if (d75v_lo < 0)` re-ran getenv on EVERY G_VTX when the env
         * was unset -- ~55% of render-thread CPU (D302/D250 class). */
        static bool d75v_init = false;
        if (!d75v_init) {
            d75v_init = true;
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

static void gfx_interp_scissor(XYWidthHeight* sc);   /* D578, defined after gfx_adjust_viewport_or_scissor */
static XYWidthHeight gfx_scissor_raw(uint32_t ulx, uint32_t uly, uint32_t lrx, uint32_t lry);   /* D578 */

static void gfx_sp_tri1(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx, bool is_rect) {
    /* D75D: only real model triangles (all indices < MAX_VERTICES); the
     * fullscreen-quad helpers use indices MAX_VERTICES+0..3 and would skew maxtri. */
    if (vtx1_idx < MAX_VERTICES && vtx2_idx < MAX_VERTICES && vtx3_idx < MAX_VERTICES) {
        uint32_t d75d_mx = vtx1_idx > vtx2_idx ? (vtx1_idx > vtx3_idx ? vtx1_idx : vtx3_idx)
                                               : (vtx2_idx > vtx3_idx ? vtx2_idx : vtx3_idx);
        if (tri_dbg()) d75d_note_tri(videoGetFrameCount(), d75d_mx);
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
            if (tri_dbg()) d75d_note_rej(videoGetFrameCount(), 0);
            return;
        }
    }

    if ((rsp.geometry_mode & G_CULL_BOTH) != 0) {
        { /* D579 (#150) fix: the cull test is a 2-D face-orientation
         * check, well-defined only when all three vertices sit in front of
         * the camera (all w > 0). The shipped NDC form (per-vertex x/w) is
         * the golden-captured decision and is used VERBATIM for those
         * triangles -- no cull decision for any all-in-front triangle
         * changes, so goldens are safe by construction. Mixed-w triangles
         * (a vertex behind the camera plane) are NOT cull-tested: the
         * per-vertex x/w form is undefined across w = 0 and the old XOR
         * behind-eye sign flip is a guess, and that guess is exactly where
         * the wall-hug leak came from -- the cull killed wall faces the
         * N64 keeps (1964 shows the wall; the no-cull A/B shows the
         * port's skybox leaking through). In the N64 pipeline the ucode
         * has no CPU near clip (gmain.s audit, D543): mixed-w triangles
         * reach the RDP, whose hardware near clip decides. So they fall
         * through to the D543 clip below, which mirrors the RDP (all
         * behind -> discarded; straddling -> clipped + emitted). The
         * earlier clip-space (w-scaled) cull variants are REFUTED -- the
         * 2026-10-08 A/Bs (GE_CULLCLIP "worse", GE_CULLV2 "completely
         * broken") show their sign diverges from the NDC decision on
         * normal disparate-w geometry (near + far vertex triangle), not
         * just mixed-w, so they are removed, not tuned. */
        const bool any_w_neg = (v1->w < 0) || (v2->w < 0) || (v3->w < 0);
        /* D584 (Cradle catwalk flicker, regression from D579): mixed-w
         * triangles still get a facing test, computed as the homogeneous
         * determinant det[x y w] (no division by w). For all w > 0 its sign
         * equals the NDC cross below; for mixed w it is exactly the old XOR
         * behind-eye flip, but well-conditioned near w = 0 (the x/w blow-up
         * there was the #150 wall-hug leak). Skipping the test entirely drew
         * both faces of Cradle's translucent two-faced catwalk walls (maintainer
         * Deck A/B: "oldcull" fixed it). */
        bool hcull = any_w_neg;
        if (!any_w_neg || hcull) {
            float dx1, dy1, dx2, dy2, cross;
            if (hcull) {
                dx1 = dy1 = dx2 = dy2 = 0.0f;
                cross = -(v1->x * (v2->y * v3->w - v2->w * v3->y) - v1->y * (v2->x * v3->w - v2->w * v3->x) +
                          v1->w * (v2->x * v3->y - v2->y * v3->x));
            } else {
            dx1 = v1->x / (v1->w) - v2->x / (v2->w);
            dy1 = v1->y / (v1->w) - v2->y / (v2->w);
            dx2 = v3->x / (v3->w) - v2->x / (v2->w);
            dy2 = v3->y / (v3->w) - v2->y / (v2->w);
            cross = dx1 * dy2 - dy1 * dx2;
            }

            // If inverted culling is requested, negate the cross
            // if ((rsp.extra_geometry_mode & G_EX_INVERT_CULLING) == 1) {
            //     cross = -cross;
            // }

        switch (rsp.geometry_mode & G_CULL_BOTH) {
            case G_CULL_FRONT:
                if (cross <= 0) {
                    if (tri_dbg()) d75d_note_rej(videoGetFrameCount(), 1);
                    return;
                }
                break;
            case G_CULL_BACK:
                if (cross >= 0) {
                    if (tri_dbg()) d75d_note_rej(videoGetFrameCount(), 1);
                    return;
                }
                break;
            case G_CULL_BOTH:
                // Why is this even an option?
                if (tri_dbg()) d75d_note_rej(videoGetFrameCount(), 1);
                return;
            }
        }
        /* D579: any_w_neg -> no cull decision; the D543 near clip below
         * mirrors the RDP (all behind -> discarded; straddling ->
         * clipped + emitted). */
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
        XYWidthHeight sc = rdp.scissor;
        gfx_interp_scissor(&sc);   /* D578: in-between passes move portal scissors */
        if (memcmp(&sc, &rendering_state.scissor, sizeof(sc)) != 0) {
            gfx_flush();
            gfx_rapi->set_scissor(sc.x, sc.y, sc.width, sc.height);
            rendering_state.scissor = sc;
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
                const auto& ltx = rdp.loaded_texture[rdp.texture_tile[tile].tmem];
                if (rdp.texture_tile[tile].fmt == G_IM_FMT_RGBA &&
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

    if (tri_dbg()) {   /* D474: one cached gate for the debug hooks */
        d75d_note_emit(videoGetFrameCount(), v_arr); // survived all rejection gates -> reaches GL
        d303_note_emit(videoGetFrameCount(), v_arr); // TEMP D303
        zf_note_emit(videoGetFrameCount(), v_arr);   // TEMP D306/D308
        d526_note_emit(videoGetFrameCount(), v_arr, use_alpha ? 1 : 0, use_modulate ? 1 : 0); // D526 census
        d75d_note_emit_z(videoGetFrameCount(), v_arr, (used_textures[0] || used_textures[1]) ? 1 : 0, comb->used_textures[0] ? 1 : 0);
    }
    /* D543: RSP-style near-plane clipping on the CPU. The GPU clips the
     * triangle itself against the near plane, but it interpolates the
     * noperspective shade colour and the fog value in screen space (D548
     * bands, D540 light/dark cycling, D543 bright lit area at Bond's feet),
     * so a big ground triangle that crosses the camera plane extrapolates
     * them. The RSP instead clips in clip space and lerps every vertex
     * attribute linearly there (t = dA / (dA - dB)); do the same, so GL
     * receives only fully-in-front triangles and never clips. Skipped for
     * rects, G_NO_CLIPPING_EXT, and GE_NEARCLIP=0 (A/B). Fog on a vertex
     * the clip creates is re-derived from its own z/w (as gfx_sp_vertex
     * does), like the RSP, whose clipper runs new vertices through the same
     * vertex-finish code: at the near plane that is a constant, so the haze
     * at the player's feet stays static (D553; maintainer compared against
     * LLE mupen64plus, cxd4 + angrylion). GE_NEARCLIPFOG=lerp restores the
     * old interpolation from the parents (whose behind-the-eye corner flips
     * between 0 and 255 fog as w crosses 0 -> the dark/bright popping) for
     * A/B. The new vertices live on the stack: rsp.loaded_vertices is
     * game-visible. */
    static int nearclip_on = -1, nearclip_fog_recompute = -1;
    if (nearclip_on < 0) {
        const char* e = getenv("GE_NEARCLIP");
        nearclip_on = !(e && e[0] == '0');
        const char* f = getenv("GE_NEARCLIPFOG");
        nearclip_fog_recompute = (f && strcmp(f, "lerp") == 0) ? 0 : 1;   /* D553: recompute by default */
    }
    struct LoadedVertex* emit_v[6] = { v1, v2, v3, NULL, NULL, NULL };
    int n_out_tris = 1;
    struct LoadedVertex nc_vtx[2];
    if (nearclip_on && !is_rect && (rsp.extra_geometry_mode & G_NO_CLIPPING_EXT) == 0) {
        /* D579 (#150): clip at the CAMERA plane (w = eps), not the near
         * plane (z = -w). GL runs with GL_DEPTH_CLAMP (gfx_opengl.cpp), so
         * geometry between the eye and the near plane is drawn, like the
         * N64 (1964 shows the wall). Cutting at z = -w deleted exactly that
         * geometry: a wall the player hugs vanished and the sky showed
         * through (regression since v0.5.0 / D543; GE_NEARCLIP=0 cured it).
         * D543's purpose -- no attribute extrapolation across w = 0 -- only
         * needs the camera-plane cut. */
        const float NEAR_EPS = 1e-2f;
        float d[3];
        int n_in = 0;
        for (int i = 0; i < 3; i++) {
            d[i] = v_arr[i]->w - NEAR_EPS;
            if (d[i] > 0.0f) n_in++;
        }
        if (n_in == 0) {
            return; // entirely behind the near plane
        }
        if (n_in < 3) {
            struct LoadedVertex* poly[4];
            int n_poly = 0, n_new = 0;
            for (int i = 0; i < 3; i++) {
                int j = (i + 1) % 3;
                struct LoadedVertex* a = v_arr[i];
                struct LoadedVertex* b = v_arr[j];
                if (d[i] > 0.0f) poly[n_poly++] = a;
                if ((d[i] > 0.0f) != (d[j] > 0.0f)) {
                    float t = d[i] / (d[i] - d[j]);
                    struct LoadedVertex* o = &nc_vtx[n_new++];
                    o->x = a->x + (b->x - a->x) * t;
                    o->y = a->y + (b->y - a->y) * t;
                    o->z = a->z + (b->z - a->z) * t;
                    o->w = a->w + (b->w - a->w) * t;
                    o->u = a->u + (b->u - a->u) * t;
                    o->v = a->v + (b->v - a->v) * t;
                    o->color.r = (uint8_t)clampf(floorf(a->color.r + (b->color.r - a->color.r) * t + 0.5f), 0.f, 255.f);
                    o->color.g = (uint8_t)clampf(floorf(a->color.g + (b->color.g - a->color.g) * t + 0.5f), 0.f, 255.f);
                    o->color.b = (uint8_t)clampf(floorf(a->color.b + (b->color.b - a->color.b) * t + 0.5f), 0.f, 255.f);
                    o->color.a = (uint8_t)clampf(floorf(a->color.a + (b->color.a - a->color.a) * t + 0.5f), 0.f, 255.f);
                    o->clip_rej = 0;
                    o->fog_n = a->fog_n + (b->fog_n - a->fog_n) * t;
                    if (nearclip_fog_recompute) {
                        if (rsp.geometry_mode & G_FOG) {
                            float fw = o->w;
                            if (fabsf(fw) < 0.001f) fw = 0.001f;
                            float winv = 1.0f / fw;
                            if (!gfx_fog_vertex && winv < 0.0f) winv = std::numeric_limits<int16_t>::max();
                            /* D579: the cut is now at w = eps, in front of
                             * which z/w can sit below -1; clamp to the
                             * near-plane value so D553's static haze at
                             * the player's feet is unchanged. */
                            const float zw = std::max(o->z * winv, -1.0f);
                            o->fog = clampf(zw * rsp.fog_mul + rsp.fog_offset, 0.f, 255.f);
                        } else {
                            o->fog = a->fog;
                        }
                    } else {
                        o->fog = (uint8_t)clampf(floorf(a->fog + ((float)b->fog - (float)a->fog) * t + 0.5f), 0.f, 255.f);
                    }
                    if (gfx_fog_vertex) o->fog_n = o->fog; // as gfx_sp_vertex
                    poly[n_poly++] = o;
                }
            }
            // n_poly is 3 or 4 here (one or two vertices inside)
            emit_v[0] = poly[0]; emit_v[1] = poly[1]; emit_v[2] = poly[2];
            if (n_poly == 4) {
                emit_v[3] = poly[0]; emit_v[4] = poly[2]; emit_v[5] = poly[3];
                n_out_tris = 2;
            }
        }
    }
    const int n_emit_verts = n_out_tris * 3;
    if (buf_vbo_num_tris + n_out_tris > MAX_BUFFERED) {
        gfx_flush(); // buf_vbo holds MAX_BUFFERED triangles; make room for 1-2
    }
    for (int i = 0; i < n_emit_verts; i++) {
        struct LoadedVertex* vi = emit_v[i];
        float z = vi->z, w = vi->w;
        if (clip_parameters.z_is_from_0_to_1) {
            z = (z + w) / 2.0f;
        }
        buf_vbo[buf_vbo_len++] = vi->x;
        buf_vbo[buf_vbo_len++] = clip_parameters.invert_y ? -vi->y : vi->y;
        buf_vbo[buf_vbo_len++] = z;
        buf_vbo[buf_vbo_len++] = w;

        for (int t = 0; t < 2; t++) {
            if (!used_textures[t]) {
                continue;
            }

            // TODO: fix this; for now just ignore smaller mips
            const uint32_t tile = gfx_lod_tile_offset(t);

            float u = vi->u / 32.0f;
            float v = vi->v / 32.0f;

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
            buf_vbo[buf_vbo_len++] = vi->fog_n / 255.0f; // D540: see LoadedVertex.fog_n
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
                        color = &vi->color;
                        break;
                    case G_CCMUX_SHADE_ALPHA:
                        tmp.r = tmp.g = tmp.b = vi->color.a;
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

    buf_vbo_num_tris += n_out_tris;
    if (buf_vbo_num_tris >= MAX_BUFFERED) {
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

    rsp.aspect_scale = rsp.aspect_mode ? gfx_current_native_aspect : gfx_current_dimensions.aspect_ratio; /* D447: == window aspect unless the output rect is active */

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

static void gfx_adjust_viewport_or_scissor(XYWidthHeight* area, bool preserve_aspect = false, bool window_space = false) {
    window_space = window_space || g_overlay_window_space;   /* D510 */
    // HACK: assume all target framebuffers have the same aspect
    // Use floor/ceil to ensure scissor fully contains the logical region
    // and prevents sub-pixel gaps at viewport edges
    // g_gpSafeTop already plays the exact role SCREEN_HEIGHT plays below
    // (both are the bottom-up Y value of the mapped region's TOP edge) --
    // this reduces to the untouched original formula when crop is off.
    const bool crop = g_safe_area_crop_enabled && !g_split_screen && !window_space && g_gpSafeHeight > 0.0f;
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
    const float safeLeft = (g_safe_area_crop_enabled && !g_split_screen && !window_space) ? 1.0f : 0.0f;
    const float safeWidth = (g_safe_area_crop_enabled && !g_split_screen && !window_space) ? (float)SCREEN_WIDTH - 2.0f : (float)SCREEN_WIDTH;
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

/* D578: GE clips each room seen through a portal to that portal's screen
 * rectangle (bg.c bgScissorCurrentPlayerView), computed for this frame's
 * camera. An in-between pass draws at the blended camera, so the stale
 * rectangle cut rooms (fog colour showed through, maintainer playtest, Dam
 * tunnel). Widening it instead let rooms drawn WITHOUT depth testing
 * (G_RM_AA_OPA_SURF2 / OPA_TERR2 rely on the portal clip and draw order)
 * paint over nearer ones: everything flickered while walking. So the
 * rectangle is MOVED: its corners, as view directions, are rotated by the
 * camera correction of the room being drawn and re-projected (exact for
 * turning, a few px off at the edges for walking). N64 units throughout,
 * then the usual window mapping. Full-view scissors (HUD, menus) are left. */
static void gfx_interp_scissor(XYWidthHeight* sc) {
    /* D578 portal replay (port/src/interpportal.c): in an in-between pass, a room's
     * scissor is recomputed by re-running bg.c's portal traversal for the
     * interpolated camera. Falls through to the matrix correction below when the
     * scissor cannot be mapped to a room. GE_INTERPPORTAL_OFF=1 disables the
     * replacement. */
    if (!g_overlay_window_space && s_interp_seg14 != 0) {
        const bool blend_pass = (s_interp_mode == INTERP_BLEND && s_interp_alpha < 1.0f);
        const bool replace = blend_pass && !GE_ENVFLAG("GE_INTERPPORTAL_OFF");
        if (replace) {
            const XYWidthHeight r0 = s_interp_sc_raw;
            int pl = 0, pt = 0, pr = 0, pb = 0;
            const int ps = interpPortalScissor(s_interp_seg14, (float)r0.x, (float)r0.y - (float)r0.height,
                                               (float)r0.x + (float)r0.width, (float)r0.y, 1, &pl, &pt, &pr, &pb);
            if (ps == 2 && s_interp_rooms_prev_valid &&
                !std::binary_search(s_interp_rooms_prev.begin(), s_interp_rooms_prev.end(), (uintptr_t)s_interp_seg14)) {
                /* D583: a room that entered the drawn set this frame and that this
                 * pass's (earlier) camera does not see: draw nothing, so it appears
                 * on the exact present as at 60 fps. With the current camera's
                 * rectangle it was drawn clipped at the wrong place (the Dam
                 * guard towers flickered as they came into view). */
                sc->width = 0;
                sc->height = 0;
                s_interp_hid_entering++;
                if (s_interp_hid_n < (int)(sizeof(s_interp_hid_ids) / sizeof(s_interp_hid_ids[0]))) {
                    s_interp_hid_ids[s_interp_hid_n++] = s_interp_seg14;
                }
                return;
            }
            if (ps == 1) {
                XYWidthHeight pout = gfx_scissor_raw((uint32_t)pl << 2, (uint32_t)pt << 2, (uint32_t)pr << 2, (uint32_t)pb << 2);
                gfx_adjust_viewport_or_scissor(&pout, rsp.aspect_mode != 0);
                *sc = pout;
                return;
            }
        }
    }
    if (s_interp_mode != INTERP_BLEND || s_interp_alpha >= 1.0f || !s_interp_have_corr || g_overlay_window_space) {
        return;
    }
    const float (*P)[4] = rsp.P_matrix;   /* this pass's (blended) projection */
    if (P[2][3] == 0.0f || P[0][0] == 0.0f || P[1][1] == 0.0f) {
        return;
    }
    /* The game computed the rectangle under the unblended camera (the raw
     * projection it just loaded; for GE room passes that is the folded
     * perspective*lookat, so it carries the camera rotation the modelviews
     * do not). Un-project with that one, then project the moved direction
     * through the blended P: unproject(P)->project(P) is the identity, so
     * reusing P for both (the old code) cancelled the camera delta and left
     * the rectangle where the game put it while the room geometry sat at the
     * blended camera -- the straight-edged cuts. */
    const float (*Pcur)[4] = rsp.P_matrix;
    if (s_interp_have_proj_raw) {
        Pcur = s_interp_proj_raw;
    }
    if (Pcur[2][3] == 0.0f || Pcur[0][0] == 0.0f || Pcur[1][1] == 0.0f) {
        return;
    }
    const XYWidthHeight r = s_interp_sc_raw, v = s_interp_vp_raw;
    if (v.width <= 0.0f || v.height <= 0.0f) {
        return;
    }
    const float vl = v.x, vr = v.x + v.width, vb = v.y, vt = v.y - v.height;
    const float rl = r.x, rr = r.x + r.width, rb = r.y, rt = r.y - r.height;
    if (rl < vl - 0.5f || rr > vr + 0.5f || rt < vt - 0.5f || rb > vb + 0.5f) {
        return;   /* not a sub-rectangle of the 3D view */
    }
    if (rl <= vl + 0.5f && rr >= vr - 0.5f && rt <= vt + 0.5f && rb >= vb - 0.5f) {
        return;   /* the whole view */
    }
    const float (*c)[4] = s_interp_corr;
    const float det = c[0][0] * (c[1][1] * c[2][2] - c[1][2] * c[2][1]) -
                      c[0][1] * (c[1][0] * c[2][2] - c[1][2] * c[2][0]) +
                      c[0][2] * (c[1][0] * c[2][1] - c[1][1] * c[2][0]);
    const float k = cbrtf(fabsf(det));
    if (k < 1e-6f) {
        return;
    }
    float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f;
    const float w0 = -Pcur[2][3] + Pcur[3][3];   /* clip w of a point at view z = -1, under Pcur */
    for (int corner = 0; corner < 4; corner++) {
        const float px = (corner & 1) ? rr : rl;
        const float py = (corner & 2) ? rb : rt;
        const float nx = (px - vl) / v.width * 2.0f - 1.0f;
        const float ny = 1.0f - (py - vt) / v.height * 2.0f;
        const float x = (nx * w0 + Pcur[2][0] - Pcur[3][0]) / Pcur[0][0];
        const float y = (ny * w0 + Pcur[2][1] - Pcur[3][1]) / Pcur[1][1];
        const float z = -1.0f;
        const float x2 = (x * c[0][0] + y * c[1][0] + z * c[2][0]) / k;
        const float y2 = (x * c[0][1] + y * c[1][1] + z * c[2][1]) / k;
        const float z2 = (x * c[0][2] + y * c[1][2] + z * c[2][2]) / k;
        const float w2 = z2 * P[2][3] + P[3][3];
        if (w2 <= 1e-4f) {
            return;   /* swung behind the camera: keep the game's rectangle */
        }
        const float nx2 = (x2 * P[0][0] + y2 * P[1][0] + z2 * P[2][0] + P[3][0]) / w2;
        const float ny2 = (x2 * P[0][1] + y2 * P[1][1] + z2 * P[2][1] + P[3][1]) / w2;
        const float sx = vl + (nx2 + 1.0f) * 0.5f * v.width;
        const float sy = vt + (1.0f - ny2) * 0.5f * v.height;
        minx = fminf(minx, sx); maxx = fmaxf(maxx, sx);
        miny = fminf(miny, sy); maxy = fmaxf(maxy, sy);
    }
    minx = fmaxf(minx, vl); maxx = fminf(maxx, vr);
    miny = fmaxf(miny, vt); maxy = fminf(maxy, vb);
    if (maxx <= minx || maxy <= miny) {
        return;
    }
    XYWidthHeight out;
    out.x = minx;
    out.y = maxy;
    out.width = maxx - minx;
    out.height = maxy - miny;
    /* The moved rectangle covers the room geometry at the blended camera;
     * union it with the game's rectangle (N64 units, same space as r) so
     * geometry drawn under the current camera -- in particular the
     * non-depth-tested modes that rely on the portal clip and draw order
     * (G_RM_AA_OPA_SURF2/OPA_TERR2) -- is not exposed the way the wide
     * scissor of 9a500ff2's predecessor did. A bounded sliver, not a
     * whole-viewport box. */
    const float ux = fminf(out.x, rl);
    const float ub = fmaxf(out.y, rb);
    const float ur = fmaxf(out.x + out.width, rr);
    const float ut = fminf(out.y - out.height, rt);
    out.x = ux;
    out.y = ub;
    out.width = ur - ux;
    out.height = ub - ut;
    gfx_adjust_viewport_or_scissor(&out, rsp.aspect_mode != 0);
    *sc = out;
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
    gfx_adjust_viewport_or_scissor(&area, false, true);   /* D510: the overlay is window-space */
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
    s_interp_vp_raw = rdp.viewport;   /* D578: N64 units, before the window mapping */

    /* Cache the raw (pre window-scale) viewport bounds for the safe-area
     * crop above -- guard against a degenerate/zero-height viewport so a
     * later divide can't ever see one. */
    if (height >= 0.9f * (float)SCREEN_HEIGHT) {
        g_gpSafeTop = y;
        g_gpSafeHeight = height;
    } else if (height > 1.0f && g_gpSafeHeight <= 0.0f) {
        /* D447: Wide/Cinema (and split-screen) viewports are the player's own
         * letterbox, never the overscan margin: don't let them define the crop
         * band, or the crop would stretch them to fill the window. If no full
         * viewport has been seen yet, seed the standard centred 220-line band
         * (VIEWPORT_HEIGHT_DEFAULT_NTSC) around this viewport's centre. */
        const float centre = y - height * 0.5f;
        const float bandH = 220.0f * (float)SCREEN_HEIGHT / 240.0f;
        g_gpSafeTop = centre + bandH * 0.5f;
        g_gpSafeHeight = bandH;
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
        case G_MW_SEGMENT: {
            // GE registers segment bases as OS_K0_TO_PHYSICAL(ptr); store the
            // live host pointer so seg_addr() resolves seg+offset correctly.
            // Values that fit in 32 bits are N64-space (a small physical
            // offset, or a V1 address); full 64-bit values are already host
            // pointers. Identity at PORT_ADDR_BASE==0.
            uintptr_t v = (uintptr_t)data;
            if (v < 0x100000000ULL) {
                if (v < 0x800000) v += 0x80000000u;
                v = (uintptr_t)portN64ToHost((u32)v);
            }
            segmentPointers[(offset >> 2) & 0xff] = v;
            if (((offset >> 2) & 0xff) == 14) {
                s_interp_seg14 = (uint32_t)data;
                if (s_interp_mode != INTERP_OFF) {
                    s_interp_rooms_pass.push_back((uintptr_t)data);   /* D578: room id (bg.c SPSEGMENT_BG_VTX) */
                }
            }
            break;
        }
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

/* The game's N64-pixel scissor (10.2 fixed in) as the raw XYWidthHeight
 * (x, BOTTOM y, width, height), including the D493b edge trim. Shared by
 * gfx_dp_set_scissor and the D578 portal replay. */
static XYWidthHeight gfx_scissor_raw(uint32_t ulx, uint32_t uly, uint32_t lrx, uint32_t lry) {
    float x = ulx / 4.0f;
    float y = lry / 4.0f;
    float width = (lrx - ulx) / 4.0f;
    float height = (lry - uly) / 4.0f;

    /* D579 (issue #150): a portal that straddles the near plane projects to a
     * garbage screen box; through the N64 12-bit scissor packing that becomes
     * a degenerate/inverted box (width<=0 or height<=0), so the room is not
     * drawn and the skybox/clear shows through (the disappearing-geometry bug).
     * When the camera is up against a portal the room fills the screen, so
     * fall back to the full logical framebuffer (a full-view scissor). Legit
     * portals and full-view HUD scissors are non-degenerate, so this only
     * fires on the garbage near-plane case. */
    const bool degenerate = (width <= 0.0f) || (height <= 0.0f);
    if (degenerate) {
        x = 0.0f;
        y = (float)SCREEN_HEIGHT;
        width = (float)SCREEN_WIDTH;
        height = (float)SCREEN_HEIGHT;
    }

    // D493b: with the overscan crop off (the Original N64 preset turns it
    // off) D246's 1-unit trim is off too, and its gap at the left/right
    // edges showed live scene/background pixels. Keep the geometry unscaled
    // and scissor those two logical columns out instead, so they show the
    // frame clear (black) like the rest of the overscan border.
    if (!degenerate && !g_safe_area_crop_enabled && !g_split_screen && !g_overlay_window_space) {
        const float edge = 1.0f;
        if (x < edge) {
            width -= edge - x;
            x = edge;
        }
        if (x + width > (float)SCREEN_WIDTH - edge) {
            width = (float)SCREEN_WIDTH - edge - x;
        }
        if (width < 0.0f) {
            width = 0.0f;
        }
    }

    XYWidthHeight out;
    out.x = x;
    out.y = y;
    out.width = width;
    out.height = height;
    return out;
}

static void gfx_dp_set_scissor(uint32_t mode, uint32_t ulx, uint32_t uly, uint32_t lrx, uint32_t lry) {
    rdp.scissor = gfx_scissor_raw(ulx, uly, lrx, lry);
    s_interp_sc_raw = rdp.scissor;   /* D578: N64 units, before the window mapping */

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

    /* D474 (low-end perf): a set_tile that changes nothing must not mark the
     * textures dirty -- that forces a texture-cache lookup AND a gfx_flush()
     * (batch break / extra draw call) on the next triangle. Loads, TLUTs and
     * G_TEXTURE still dirty the textures through their own paths. */
    auto& tt = rdp.texture_tile[tile];
    const bool same = tt.palette == palette && tt.fmt == fmt && tt.siz == siz &&
                      tt.cms == cms && tt.cmt == cmt && tt.masks == masks && tt.maskt == maskt &&
                      tt.shifts == shifts && tt.shiftt == shiftt &&
                      tt.line_size_bytes == line * 8 && tt.tmem == tmem;
    tt.palette = palette; // palette should set upper 4 bits of color index in 4b mode
    tt.fmt = fmt;
    tt.siz = siz;
    tt.cms = cms;
    tt.cmt = cmt;
    tt.masks = masks; /* RC3 */
    tt.maskt = maskt; /* RC3 */
    tt.shifts = shifts;
    tt.shiftt = shiftt;
    tt.line_size_bytes = line * 8;
    tt.tmem = tmem;

    if (!same) {
        rdp.textures_changed[0] = true;
        rdp.textures_changed[1] = true;
    }
}

static void gfx_dp_set_tile_size(uint8_t tile, uint16_t uls, uint16_t ult, uint16_t lrs, uint16_t lrt) {
    auto& tt = rdp.texture_tile[tile];
    if (tt.uls == uls && tt.ult == ult && tt.lrs == lrs && tt.lrt == lrt) {
        return;   /* D474: unchanged tile size -- no re-import, no batch break */
    }
    tt.uls = uls;
    tt.ult = ult;
    tt.lrs = lrs;
    tt.lrt = lrt;
    tt.width = (lrs - uls + 4) / 4;
    tt.height = (lrt - ult + 4) / 4;
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
    loaded_texture.dxt0 = (dxt == 0);                 /* D75 */

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
    loaded_texture.dxt0 = false;                      /* D75 */

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

/* D510: uniform scale about the origin for the port overlay DL, which lays
 * itself out on a canonical 320-wide canvas whatever the game's current VI
 * canvas is (440x330 on the front end). Applied after the HUD span scale. */
static float s_overlay_scale = 1.0f;
extern "C" void gfx_set_overlay_scale(float s) {
    s_overlay_scale = (s > 0.0f) ? s : 1.0f;
}

static inline void gfx_hud_scale_rect(int32_t& ulx, int32_t& uly, int32_t& lrx, int32_t& lry) {
    if (s_overlay_scale != 1.0f && g_overlay_window_space) {
        const bool hud = (s_hud_scale != 1.0f);
        if (hud) {
            ulx = s_hud_ax + (int32_t)lroundf((float)(ulx - s_hud_ax) * s_hud_scale);
            lrx = s_hud_ax + (int32_t)lroundf((float)(lrx - s_hud_ax) * s_hud_scale);
            uly = s_hud_ay + (int32_t)lroundf((float)(uly - s_hud_ay) * s_hud_scale);
            lry = s_hud_ay + (int32_t)lroundf((float)(lry - s_hud_ay) * s_hud_scale);
        }
        ulx = (int32_t)lroundf((float)ulx * s_overlay_scale);
        lrx = (int32_t)lroundf((float)lrx * s_overlay_scale);
        uly = (int32_t)lroundf((float)uly * s_overlay_scale);
        lry = (int32_t)lroundf((float)lry * s_overlay_scale);
        if (ulx < 0) ulx = 0;
        if (uly < 0) uly = 0;
        return;
    }
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
    const float ov = (g_overlay_window_space && s_overlay_scale != 1.0f) ? s_overlay_scale : 1.0f;   /* D510 */
    if (s_hud_scale != 1.0f || ov != 1.0f) {   /* D226 */
        gfx_hud_scale_rect(ulx, uly, lrx, lry);
        dsdx = (int16_t)lroundf((float)dsdx / (s_hud_scale * ov));
        dtdy = (int16_t)lroundf((float)dtdy / (s_hud_scale * ov));
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
        d->fog_n = gfx_fog_vertex ? d->fog : d->fog * w;
    }
}

static inline void *seg_addr(uintptr_t w1) {
    // A full 64-bit host pointer (the gSP* macros pack full pointers into w1;
    // the window base makes every real host pointer >= 4 GiB) passes through
    // untouched. Only values that fit in 32 bits are N64-space addresses.
    if (w1 >= 0x100000000ULL) {
        return (void *)w1;
    }
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
    // D131 (folded into the #95 model): a GBI DL built by game code can
    // reference a COMPILED symbol via osVirtualToPhysical() (a u32-returning
    // shim), which truncates the module's high word -> w1 == 0x40xxxxxx. The
    // image-relative path in portN64ToHost() restores it (on Windows the
    // image base is 0x140000000, so this reproduces the old mod_hi|w1 exactly).
    // GE passes OS_K0_TO_PHYSICAL(ptr) == ptr - V1 base for RAM that lives in
    // the reserved N64-DRAM region (port/src/dram.c); map it back into the V2
    // view. The region is 8 MB, so any offset below 0x800000 came from there.
    if (w1 < 0x800000) {
        return portN64ToHost((u32)(w1 + 0x80000000));
    }
    // Everything else is a 32-bit N64-space address: DRAM V1 (0x70xxxxxx),
    // KSEG0 (0x80xxxxxx), cart (0x10xxxxxx), or the image-relative encoding
    // 0x40000000 + (ptr - image base) produced by osVirtualToPhysical() on a
    // pointer into the executable (D131). portN64ToHost applies the window
    // base and, for the image-relative range, the image base. On Windows the
    // image base is 0x140000000, so this reproduces the old mod_hi|w1 exactly;
    // at PORT_ADDR_BASE==0 the whole thing is the identity it was before.
    return portN64ToHost((u32)w1);
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
                    /* D578: key the matrix by the geometry it transforms. */
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
                if (tri_dbg()) d75d_note_vtx(videoGetFrameCount(), C0(0, 16) / sizeof(Vtx), C0(16, 4));
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
    /* D579: fresh FBO attachments are GL-undefined; clear them once at creation
     * so boot frames see black (N64 VRAM at hardware reset), now that the
     * per-frame color clear is gone (see gfx_frame_body). */
    gfx_rapi->start_draw_to_framebuffer(game_framebuffer, 1.0f);
    gfx_rapi->clear_framebuffer(true, true);
    gfx_rapi->start_draw_to_framebuffer(game_framebuffer_msaa_resolved, 1.0f);
    gfx_rapi->clear_framebuffer(true, true);

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

    gfx_current_game_window_viewport.x = 0;
    gfx_current_game_window_viewport.y = 0;
    gfx_current_game_window_viewport.width = gfx_current_dimensions.width;
    gfx_current_game_window_viewport.height = gfx_current_dimensions.height;
    g_output_rect = { 0, 0, gfx_current_window_dimensions.width, gfx_current_window_dimensions.height };

    if (g_output_aspect > 0.0f) {
        /* D447: pillar/letterbox to the exact target aspect, centred, even
         * sizes. dims == game viewport size, so different_size stays false and
         * gfx_adjust_viewport_or_scissor's existing offset path places every
         * canvas rect inside the rect. */
        const uint32_t ww = gfx_current_window_dimensions.width;
        const uint32_t wh = gfx_current_window_dimensions.height;
        uint32_t rw = ww, rh = wh;
        if ((float)ww / (float)wh > g_output_aspect) {
            rw = (uint32_t)std::lround((float)wh * g_output_aspect);
        } else {
            rh = (uint32_t)std::lround((float)ww / g_output_aspect);
        }
        rw &= ~1u;
        rh &= ~1u;
        if (rw < 2) rw = 2;
        if (rh < 2) rh = 2;
        if (rw > ww) rw = ww;
        if (rh > wh) rh = wh;
        g_output_rect = { (int16_t)((ww - rw) / 2), (int16_t)((wh - rh) / 2), rw, rh };
        gfx_current_dimensions.width = rw;
        gfx_current_dimensions.height = rh;
        gfx_current_dimensions.aspect_ratio = g_output_aspect;
        gfx_current_game_window_viewport = g_output_rect;
    }

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
                    width = gfx_current_window_dimensions.width;
                    height = gfx_current_window_dimensions.height;
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
static double s_perf_maxiv = 0, s_perf_maxiv_dl = 0, s_perf_maxiv_run = 0;
static uint64_t s_perf_maxiv_frame = 0, s_perf_over20 = 0, s_perf_over33 = 0;

/* One pass of a frame: draw the game DL (and the port overlay DL) and
 * composite to the window back buffer. Shared by gfx_run and the D578
 * interpolation passes. With emit_overlay the port overlay DL is emitted at
 * its usual point (after the game DL) and returned through overlay_io; later
 * passes of the same game frame replay it. Perf timestamps: 0 = off. */
static void gfx_frame_body(Gfx* commands, bool emit_overlay, Gfx** overlay_io, uint64_t perf_t0, uint64_t* perf_tpre, uint64_t* perf_t1) {
    gfx_rapi->update_framebuffer_parameters(0, gfx_current_window_dimensions.width,
                                            gfx_current_window_dimensions.height, 1, false, true, true,
                                            !game_renders_to_framebuffer);
    gfx_rapi->start_frame();
    gfx_rapi->start_draw_to_framebuffer(game_renders_to_framebuffer ? game_framebuffer : 0,
                                        (float)gfx_current_dimensions.height / SCREEN_HEIGHT);
    /* D579 (issue #150): the N64 RSP has no color clear -- gmain.s clears
     * depth only (G_CLEAR_DEPTH_EXT) and the VI never writes VRAM -- so regions
     * a frame does not draw keep the PREVIOUS frame's pixels (stale VRAM).
     * The port's per-frame black color clear is a port addition: when a room
     * scissor box collapses (a portal straddling the near plane -- the N64-
     * matching finite-garbage clamp path, D106/D271), the undrawn regions show
     * black on the port (the "black wedge + peephole" of #150) but the previous
     * frame's content on N64, where the seam is nearly invisible. Match the N64:
     * do NOT clear the 4:3 canvas (it is persistent VRAM). The 16:9 letterbox
     * bars, however, are a TV-side concept (the N64's 4:3 hardware had no bars;
     * a 16:9 TV shows black around 4:3 video), so clear those to black --
     * otherwise a blanket no-clear leaks stale game pixels into the bars (the
     * file-select regression). Depth is not cleared here either: the game emits
     * G_CLEAR_DEPTH_EXT when it needs a depth reset (line 5536), exactly as on
     * N64, and the original port cleared color-only.
     */
    {
        const bool diff = gfx_current_dimensions.width != gfx_current_game_window_viewport.width ||
                          gfx_current_dimensions.height != gfx_current_game_window_viewport.height;
        int32_t cw = gfx_current_dimensions.width, ch = gfx_current_dimensions.height;
        int32_t tw, th, ox, oy;
        if (game_renders_to_framebuffer && diff) {
            /* Game FBO is canvas-sized; the canvas fills it, no bars inside
             * (window-side bars are cleared at the G_COPYFB_EXT blit). */
            tw = cw;
            th = ch;
            ox = 0;
            oy = 0;
        } else {
            /* Target is window-sized (MSAA FBO or the default framebuffer);
             * the canvas sits at the game-window-viewport offset. */
            tw = gfx_current_window_dimensions.width;
            th = gfx_current_window_dimensions.height;
            ox = gfx_current_game_window_viewport.x;
            oy = gfx_current_game_window_viewport.y;
        }
        /* Bar rects (target top-down) cleared to black. clear_region() takes
         * GL bottom-up y, so top/bottom are mirrored and the full-height side
         * bars are y-agnostic. clear_region() no-ops on zero-size bars. */
        gfx_rapi->clear_region(true, false, 0, 0, ox, th);                             /* left   */
        gfx_rapi->clear_region(true, false, ox + cw, 0, tw - ox - cw, th);             /* right  */
        gfx_rapi->clear_region(true, false, ox, th - oy, cw, oy);                     /* top    */
        gfx_rapi->clear_region(true, false, ox, 0, cw, th - oy - ch);                 /* bottom */
        /* Native widescreen: the canvas is the full window aspect, but the
         * front-end screens (mission folder, file select, main menu) draw only
         * their 4:3 core, so the canvas sides are never drawn and kept the
         * previous screen (stale watch pixels). Outside a running stage those
         * sides are TV-side area like the bars above: clear them. In a stage
         * the 3D view covers the full canvas, so the no-clear stands. */
        if (current_menu != GE_MENU_RUN_STAGE && portNativeAspect() > 4.0f / 3.0f + 0.01f) {
            const int32_t core = (int32_t)lroundf(ch * (4.0f / 3.0f));
            const int32_t side = (cw - core) / 2;
            if (side > 0) {
                const int32_t gy = th - oy - ch;
                gfx_rapi->clear_region(true, false, ox, gy, side, ch);                /* canvas left  */
                gfx_rapi->clear_region(true, false, ox + cw - side, gy, side, ch);    /* canvas right */
            }
        }
    }
    rdp.viewport_or_scissor_changed = true;
    rendering_state.viewport = {};
    rendering_state.scissor = {};
    interpPortalPassBegin(commands);   /* D578: select the frame snapshot, drop the cached replay */
    s_interp_seg14 = 0;
    *perf_tpre = perf_t0 ? gfx_perf_now_ns() : 0;
    gfx_run_dl(commands);
    {
        /* Emitting the overlay also sets its scale (D510, gfx_set_overlay_scale);
         * a D578 replay must reuse that scale, not the 1.0 reset below. */
        static float s_emitted_overlay_scale = 1.0f;
        if (emit_overlay) {
            *overlay_io = optionsOverlayEmit();
            s_emitted_overlay_scale = s_overlay_scale;
        } else {
            s_overlay_scale = s_emitted_overlay_scale;
        }
        Gfx* overlay = *overlay_io;
        if (overlay != nullptr) {
            g_overlay_window_space = true;   /* D510 */
            gfx_run_dl(overlay);
            g_overlay_window_space = false;
            s_overlay_scale = 1.0f;
        }
    }
    gfx_flush();
    *perf_t1 = perf_t0 ? gfx_perf_now_ns() : 0;
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
}

extern "C" void gfx_run(Gfx* commands) {
    s_hud_scale = 1.0f;   /* D226: never carry a HUD scale across frames */
    const uint64_t perf_t0 = gfx_perfstat_on() ? gfx_perf_now_ns() : 0;
    ++num_dls;
    gfx_sp_reset();

    // puts("New frame");

    if (!gfx_wapi->start_frame()) {
        dropped_frame = true;
        return;
    }
    if (g_aspect_settle > 0) {   /* D509: hold the old image, see gfx_set_output_aspect */
        --g_aspect_settle;
        dropped_frame = true;
        return;
    }
    dropped_frame = false;

    if (s_shader_warm_pending) {
        gfx_shader_prewarm();   /* D480 */
    }

    uint64_t perf_tpre = 0, perf_t1 = 0;
    Gfx* overlay = nullptr;
    gfx_frame_body(commands, true, &overlay, perf_t0, &perf_tpre, &perf_t1);
    const uint64_t perf_tpost = perf_t0 ? gfx_perf_now_ns() : 0;
    gfx_wapi->swap_buffers_begin();
    if (perf_t0) {
        s_perf_pre += (double)(perf_tpre - perf_t0) / 1.0e6;
        s_perf_post += (double)(perf_tpost - perf_t1) / 1.0e6;
        s_perf_swap += (double)(gfx_perf_now_ns() - perf_tpost) / 1.0e6;
        const double f = 1.0e6;
        const uint64_t t2 = gfx_perf_now_ns();
        s_perf_dl += (double)(perf_t1 - perf_tpre) / f;
        s_perf_run += (double)(t2 - perf_t0) / f;
        if (s_perf_last_start) {
            const double iv = (double)(perf_t0 - s_perf_last_start) / f;
            s_perf_interval += iv;
            /* Spike stats (hitches hide in 300-frame averages): the worst
             * frame's interval and its own dl/run, plus counts over 20/33 ms. */
            if (iv > s_perf_maxiv) {
                s_perf_maxiv = iv;
                s_perf_maxiv_dl = (double)(perf_t1 - perf_tpre) / f;
                s_perf_maxiv_run = (double)(t2 - perf_t0) / f;
                s_perf_maxiv_frame = s_perf_frames + 1;
            }
            if (iv > 20.0) s_perf_over20++;
            if (iv > 33.4) s_perf_over33++;
        }
        s_perf_last_start = perf_t0;
    }
}

extern "C" void gfx_end_frame(void) {
    const uint64_t perf_t0 = gfx_perfstat_on() ? gfx_perf_now_ns() : 0;
    if (!dropped_frame) {
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
            fprintf(stderr, "PERFSTAT spikes: max=%.2fms at frame %llu (dl=%.2f run=%.2f) >20ms=%llu >33ms=%llu\n",
                    s_perf_maxiv, (unsigned long long)s_perf_maxiv_frame, s_perf_maxiv_dl, s_perf_maxiv_run,
                    (unsigned long long)s_perf_over20, (unsigned long long)s_perf_over33);
            s_perf_maxiv = s_perf_maxiv_dl = s_perf_maxiv_run = 0;
            s_perf_maxiv_frame = s_perf_over20 = s_perf_over33 = 0;
            s_perf_dl = s_perf_run = s_perf_present = s_perf_interval = 0;
            s_perf_tris = s_perf_batches = 0;
        }
    }
}

/* ---- D578: frame interpolation entry points (render worker only) ---- */

extern "C" void gfx_opengl_interp_store(int slot, uint32_t width, uint32_t height);
extern "C" void gfx_opengl_interp_show(int slot);
extern "C" void gfx_opengl_frame_count_rewind(void);
extern "C" void gfx_opengl_interp_sync(void);

/* The emulated RSP/RDP state at the start of a game frame, restored before
 * every further pass so each pass starts exactly where a single render of
 * this frame would have. rendering_state is NOT restored: it mirrors the
 * live GL bindings, which the passes really changed. */
static struct RSP s_interp_rsp_snap;
static struct RDP s_interp_rdp_snap;
static uintptr_t s_interp_seg_snap[16];
static float s_interp_safe_snap[2];

/* D578: the last gfx_interp_tick fell back to exact presents (stats only). */
static bool s_interp_last_exact = false;
extern "C" int gfx_interp_body_runs(void) {   /* D578: passes actually drawn, incl. discarded restarts */
    return s_interp_body_runs;
}

extern "C" int gfx_interp_last_exact(void) {
    return s_interp_last_exact ? 1 : 0;
}

extern "C" void gfx_interp_trigger_counts(unsigned* turn, unsigned* clamp, unsigned* set, unsigned* room) {
    *turn = s_interp_cnt_turn;
    *clamp = s_interp_cnt_clamp;
    *set = s_interp_cnt_set;
    *room = s_interp_cnt_room;
    s_interp_cnt_turn = s_interp_cnt_clamp = s_interp_cnt_set = s_interp_cnt_room = 0;
}

/* Forget the previous frame's matrices (interpolation off, or a stall: the
 * next frame is drawn as built, then blending resumes). */
extern "C" void gfx_interp_reset(void) {
    s_interp_have_prev = false;
    s_interp_rooms_prev_valid = false;
    s_interp_prev.clear();
    s_interp_cur.clear();
    s_interp_mode = INTERP_OFF;
}

/* Draw one game frame `n` times; pass i blends its matrices at alphas[i]
 * (>= 1: exactly as built) and is stored in present slot (base + i) mod
 * GFX_INTERP_SLOTS (D583: the slots form a ring). Returns the
 * number of slots filled, 0 when the frame was dropped (minimised window,
 * D509 aspect settle). Replaces gfx_run for this frame; no swap happens
 * here (gfx_interp_present does it). */
extern "C" int gfx_interp_tick(Gfx* commands, const float* alphas, int n, int base) {
    if (n < 1) {
        n = 1;
    }
    if (n > GFX_INTERP_SLOTS) {
        n = GFX_INTERP_SLOTS;
    }
    s_hud_scale = 1.0f;   /* D226 */
    ++num_dls;
    gfx_sp_reset();
    if (!gfx_wapi->start_frame()) {
        dropped_frame = true;
        return 0;
    }
    if (g_aspect_settle > 0) {   /* D509: once per game frame, as in gfx_run */
        --g_aspect_settle;
        dropped_frame = true;
        return 0;
    }
    dropped_frame = false;
    if (s_shader_warm_pending) {
        gfx_shader_prewarm();   /* D480 */
    }

    /* Known residue: a DL's first draws can rely on the GL texture bound at the
     * end of the previous frame (pass 0 gets exactly that, as at 60 fps); a
     * later pass inherits pass 0's last binding instead. Forcing a re-bind is
     * worse (measured): a few pixels on rare frames, in-between passes only. */
    memcpy(&s_interp_rsp_snap, &rsp, sizeof(rsp));
    memcpy(&s_interp_rdp_snap, &rdp, sizeof(rdp));
    memcpy(s_interp_seg_snap, segmentPointers, sizeof(segmentPointers));
    s_interp_safe_snap[0] = g_gpSafeTop;
    s_interp_safe_snap[1] = g_gpSafeHeight;
    s_interp_cur.clear();

    Gfx* overlay = nullptr;
    uint64_t unused0, unused1;
    float a[GFX_INTERP_SLOTS];
    for (int i = 0; i < n; i++) {
        a[i] = alphas[i];
    }
    /* D583: wall clock of this tick's first pass (late-soften budget gate). */
    const auto tick_t0 = std::chrono::steady_clock::now();
    s_interp_fast = false;
    s_interp_last_exact = false;
    s_interp_body_runs = 0;
    s_interp_why = 0;
    s_interp_clamp_active = false;
    s_interp_clamp_req = 0.0f;
    /* D583: pre-soften from the previous frame's turn (see s_interp_pre_req). */
    s_interp_pre_req = (s_interp_have_prev && s_interp_next_req < 1.0f) ? s_interp_next_req : 1.0f;
    if (s_interp_pre_req < 1.0f) {
        for (int k = 0; k < n; k++) {
            if (a[k] < 1.0f) {
                a[k] = 1.0f - (1.0f - a[k]) * s_interp_pre_req;
            }
        }
        s_interp_cnt_pre_soft++;
    }
    s_interp_turn_max = 0.0f;
    int passes = 0;
    bool record = true;        /* D583: record this pass's raw matrices (the first drawn) */
    bool exact_all = false;    /* D583: this pass is the frame's exact image: store it in every slot */
    for (int i = 0; i < n; i++, passes++) {
        if (passes > 0) {
            memcpy(&rsp, &s_interp_rsp_snap, sizeof(rsp));
            memcpy(&rdp, &s_interp_rdp_snap, sizeof(rdp));
            memcpy(segmentPointers, s_interp_seg_snap, sizeof(segmentPointers));
            g_gpSafeTop = s_interp_safe_snap[0];
            g_gpSafeHeight = s_interp_safe_snap[1];
            s_hud_scale = 1.0f;
            gfx_opengl_frame_count_rewind();   /* start_frame below advances it again */
        }
        s_interp_mode = s_interp_have_prev ? INTERP_BLEND : INTERP_RECORD;
        s_interp_alpha = a[i];
        s_interp_record = record;
        s_interp_inplace_ok = passes == 0;
        s_interp_inplace_exact = false;
        s_interp_inplace_req = 0.0f;
        s_interp_proj_blends = 0;
        s_interp_pmemo_n = 0;
        s_interp_cut = 0;
        s_interp_proj_ok = 0;
        s_interp_taken.clear();
        s_interp_have_corr = false;
        s_interp_loads_seen = 0;
        s_interp_unmatched = 0;
        s_interp_clamp_req = 0.0f;
        s_interp_rooms_pass.clear();
        gfx_frame_body(commands, passes == 0, &overlay, 0, &unused0, &unused1);
        s_interp_body_runs++;
        record = false;
        s_interp_inplace_ok = false;
        if (s_interp_inplace_exact) {
            /* D583: the turn made this pass exact at its first projection: it
             * is the frame's image for every present, nothing is redrawn. */
            for (int k = 0; k < n; k++) {
                a[k] = 1.0f;
            }
            s_interp_last_exact = true;
            s_interp_cnt_turn++;
            s_interp_cnt_ip_exact++;
            exact_all = true;
        } else if (s_interp_inplace_req > 0.0f) {
            /* D583: softened in place; the frame's later passes follow. */
            for (int k = 0; k < n; k++) {
                if (a[k] < 1.0f) {
                    a[k] = 1.0f - (1.0f - a[k]) * s_interp_inplace_req;
                }
            }
            s_interp_cnt_clamp++;
            s_interp_cnt_ip_soft++;
        }
        std::sort(s_interp_rooms_pass.begin(), s_interp_rooms_pass.end());
        s_interp_rooms_pass.erase(std::unique(s_interp_rooms_pass.begin(), s_interp_rooms_pass.end()),
                                  s_interp_rooms_pass.end());
        /* D583: only when the portal replay did not run for this pass. When it
         * did, the REACH test below asks the real question (does this camera
         * see a room the previous frame drew and this one dropped?) and it
         * found none in a whole Dam run, while this set comparison turned every
         * ~5th frame on the open Dam wall exact as distant rooms entered and
         * left the drawn set (a hold, then a jump: visible hitching). A room
         * that entered is drawn by this pass anyway. */
        if (s_interp_mode == INTERP_BLEND && a[i] < 1.0f && s_interp_rooms_prev_valid &&
            s_interp_rooms_pass != s_interp_rooms_prev && !GE_ENVFLAG("GE_INTERPROOM_OFF") &&
            !interpPortalReplayedThisPass()) {
            s_interp_fast = true;
            if (!s_interp_why) {
                s_interp_why = 3;
            }
            static int roomlog = 0;
            if (roomlog++ < 12 || roomlog % 120 == 0) {
                /* D583: to the log (Game Mode drops stderr), with the rooms that differ. */
                char d[160];
                int o = 0;
                for (uintptr_t r : s_interp_rooms_pass) {
                    if (o < 120 && !std::binary_search(s_interp_rooms_prev.begin(), s_interp_rooms_prev.end(), r)) {
                        o += snprintf(d + o, sizeof(d) - o, " +%08x", (unsigned)r);
                    }
                }
                for (uintptr_t r : s_interp_rooms_prev) {
                    if (o < 120 && !std::binary_search(s_interp_rooms_pass.begin(), s_interp_rooms_pass.end(), r)) {
                        o += snprintf(d + o, sizeof(d) - o, " -%08x", (unsigned)r);
                    }
                }
                d[o] = 0;
                sysLogPrintf(LOG_NOTE, "D578 ROOMCHANGE frame %u (#%d): room set %zu -> %zu:%s -> exact presents",
                             (unsigned)videoGetFrameCount(), roomlog, s_interp_rooms_prev.size(), s_interp_rooms_pass.size(), d);
            }
        }
        /* D578 (tunnel holes): the portal replay at this pass's interpolated camera
         * reached a room the game never drew for the current camera -> that room is
         * absent from the list, so this pass would show a hole. Exact presents
         * (same class as a room-set change). GE_INTERPREACH_OFF=1 disables it. */
        {
            uint32_t xv[IP_MAXEXTRA];
            int xp[IP_MAXEXTRA];
            const int nx = interpPortalTakeExtraRooms(xv, xp, IP_MAXEXTRA);
            /* D583: a hole only where the PREVIOUS frame's display list drew the
             * room and this one does not (the camera leaving it: the Dam tunnel).
             * A room drawn in neither list is the game's own cull (distance /
             * fog on the open Dam wall: 7-25 of them every frame turned every
             * frame exact = 60 fps motion on a 90 Hz Deck); one this list does
             * draw is not missing. Ground truth = the SPSEGMENT_BG_VTX bases the
             * passes actually emitted, not the game's drawn-room table. */
            int extra = 0, inCur = 0, neither = 0, prevTab = 0;
            for (int k = 0; k < nx && k < IP_MAXEXTRA; k++) {
                const uintptr_t v = (uintptr_t)xv[k];
                const bool cur = std::binary_search(s_interp_rooms_pass.begin(), s_interp_rooms_pass.end(), v);
                const bool prev = s_interp_rooms_prev_valid &&
                                  std::binary_search(s_interp_rooms_prev.begin(), s_interp_rooms_prev.end(), v);
                if (xp[k] == 1) {
                    prevTab++;
                }
                if (cur) {
                    inCur++;
                } else if (prev || !s_interp_rooms_prev_valid) {
                    extra++;
                } else {
                    neither++;
                }
            }
            if (nx > IP_MAXEXTRA) {
                extra += nx - IP_MAXEXTRA;   /* unseen: count as before */
            }
            if (nx > 0 && extra == 0) {
                static int skiplog = 0;
                if (skiplog++ < 12 || skiplog % 600 == 0) {
                    sysLogPrintf(LOG_NOTE, "D583 REACH skipped frame %u (#%d): %d reached, %d in this DL, %d in neither DL (%d in prev room table)",
                                 (unsigned)videoGetFrameCount(), skiplog, nx, inCur, neither, prevTab);
                }
            }
            if (extra > 0 && s_interp_mode == INTERP_BLEND && a[i] < 1.0f && !s_interp_fast &&
                !GE_ENVFLAG("GE_INTERPREACH_OFF")) {
                s_interp_fast = true;
                if (!s_interp_why) {
                    s_interp_why = 3;
                }
                static int reachlog = 0;
                if (reachlog++ < 12 || reachlog % 120 == 0) {
                    sysLogPrintf(LOG_NOTE, "D578 REACH frame %u (#%d): replay reaches %d undrawn room(s) (%d left since the last frame, %d in this DL, %d in neither) at alpha %.2f -> exact presents",
                                 (unsigned)videoGetFrameCount(), reachlog, nx, extra, inCur, neither, a[i]);
                }
            }
        }
        /* D583: a real cut (the camera jumped) shows exact. (The matrix-counting
         * set-change check was removed for v0.6.0; see D578.) */
        if (s_interp_cut > 0 && s_interp_cut >= s_interp_proj_ok && s_interp_mode == INTERP_BLEND && a[i] < 1.0f &&
            !s_interp_fast) {
            s_interp_fast = true;
            if (!s_interp_why) {
                s_interp_why = 2;
            }
            static int cutlog = 0;
            if (cutlog++ < 12 || cutlog % 120 == 0) {
                sysLogPrintf(LOG_NOTE, "D583 CUT frame %u (#%d): %d of %d projections jumped -> exact presents",
                             (unsigned)videoGetFrameCount(), cutlog, s_interp_cut, s_interp_cut + s_interp_proj_ok);
            }
        }
        /* D583 (Deck Bunker fights): a softened restart redraws every pass. When
         * the pass just drawn says n+1 passes will not fit in 3/4 of a tick
         * (~4.5 ms passes in a 90 Hz fight: 3 passes = 13-17 ms, a late present),
         * keep it: one in-between image with a little more turn blend beats a
         * missed slot. Cheap frames (Dam, 1.5-3 ms) restart as before. */
        if (!s_interp_fast && s_interp_clamp_req > 0.0f && !s_interp_clamp_active && a[i] < 1.0f) {
            const double passUs =
                (double)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - tick_t0).count() /
                (double)(i + 1);
            if (passUs * (double)(n + 1) > 0.75 * 16667.0) {
                s_interp_clamp_req = 0.0f;
                s_interp_cnt_soft_skipped++;
            }
        }
        if (!s_interp_fast && s_interp_clamp_req > 0.0f && !s_interp_clamp_active && a[i] < 1.0f) {
            /* Moderate turn: start over with the alphas pulled toward 1. */
            for (int k = 0; k < n; k++) {
                if (a[k] < 1.0f) {
                    a[k] = 1.0f - (1.0f - a[k]) * s_interp_clamp_req;
                }
            }
            s_interp_clamp_active = true;
            s_interp_cnt_clamp++;
            s_interp_cnt_late_soft++;
            s_interp_cur.clear();
            record = true;
            i = -1;
            continue;
        }
        if (s_interp_fast && a[i] < 1.0f) {
            if (s_interp_why == 3) {
                s_interp_cnt_room++;
            } else if (s_interp_why == 2) {
                s_interp_cnt_set++;
            } else {
                s_interp_cnt_turn++;
            }
            /* Every present exact (see above). D583: draw the exact image
             * once (the last pass) and store it in every slot; redrawing all
             * passes at alpha 1 drew the same image n times. This frame's
             * matrices are already recorded (same display list). */
            for (int k = 0; k < n; k++) {
                a[k] = 1.0f;
            }
            s_interp_fast = false;
            s_interp_last_exact = true;
            s_interp_cnt_late_exact++;
            exact_all = true;
            i = n - 2;   /* the loop's i++ makes it the last pass */
            continue;
        }
        if (exact_all) {
            for (int k = 0; k < n; k++) {
                gfx_opengl_interp_store((base + k) % GFX_INTERP_SLOTS, gfx_current_window_dimensions.width,
                                        gfx_current_window_dimensions.height);
            }
            break;
        }
        const int slot = (base + i) % GFX_INTERP_SLOTS;
        gfx_opengl_interp_store(slot, gfx_current_window_dimensions.width, gfx_current_window_dimensions.height);
    }
    {
        /* D578 diag: one summary line per ~10 s (own clock: the shared stats
         * line lives in libultra.c, outside this change's files). */
        static auto s_dg_t0 = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (now - s_dg_t0 >= std::chrono::seconds(10)) {
            s_dg_t0 = now;
            sysLogPrintf(LOG_NOTE, "D583 decisions: pre-softened %u, in-place exact %u, in-place softened %u, late exact %u, late softened (redrawn) %u, late soften skipped (over budget) %u",
                         s_interp_cnt_pre_soft, s_interp_cnt_ip_exact, s_interp_cnt_ip_soft, s_interp_cnt_late_exact, s_interp_cnt_late_soft,
                         s_interp_cnt_soft_skipped);
            s_interp_cnt_pre_soft = 0;
            s_interp_cnt_soft_skipped = 0;
            sysLogPrintf(LOG_NOTE, "D583 projection memo: %u repeat loads, %u where the pairing gave a different camera",
                         s_interp_pmemo_hit, s_interp_pmemo_diff);
            s_interp_pmemo_hit = s_interp_pmemo_diff = 0;
            s_interp_cnt_ip_exact = s_interp_cnt_ip_soft = s_interp_cnt_late_exact = s_interp_cnt_late_soft = 0;
            if (s_interp_hid_entering > 0) {
                sysLogPrintf(LOG_NOTE, "D583 entering rooms hidden in in-between passes: %u", s_interp_hid_entering);
                s_interp_hid_entering = 0;
            }
        }
    }
    s_interp_mode = INTERP_OFF;
    s_interp_record = false;
    s_interp_prev.swap(s_interp_cur);
    s_interp_have_prev = true;
    if (s_interp_rooms_prev_valid && s_interp_rooms_pass != s_interp_rooms_prev) {
        /* D583: every change of the drawn room set, with the rooms hidden in
         * this frame's in-between passes (a room at the game's distance cull
         * that leaves and re-enters the set would flicker). */
        static int s_rs_log = 0;
        if (s_rs_log++ < 800) {
            char d[200];
            int o = 0;
            for (uintptr_t r : s_interp_rooms_pass) {
                if (o < 150 && !std::binary_search(s_interp_rooms_prev.begin(), s_interp_rooms_prev.end(), r)) {
                    o += snprintf(d + o, sizeof(d) - o, " +%05x", (unsigned)(r & 0xfffff));
                }
            }
            for (uintptr_t r : s_interp_rooms_prev) {
                if (o < 150 && !std::binary_search(s_interp_rooms_pass.begin(), s_interp_rooms_pass.end(), r)) {
                    o += snprintf(d + o, sizeof(d) - o, " -%05x", (unsigned)(r & 0xfffff));
                }
            }
            if (s_interp_hid_n > 0 && o < 160) {
                o += snprintf(d + o, sizeof(d) - o, " | hidden");
                for (int k = 0; k < s_interp_hid_n && o < 190; k++) {
                    o += snprintf(d + o, sizeof(d) - o, " %05x", (unsigned)(s_interp_hid_ids[k] & 0xfffff));
                }
            }
            d[o < (int)sizeof(d) ? o : (int)sizeof(d) - 1] = 0;
            sysLogPrintf(LOG_NOTE, "D583 ROOMSET frame %u: %zu -> %zu:%s", (unsigned)videoGetFrameCount(),
                         s_interp_rooms_prev.size(), s_interp_rooms_pass.size(), d);
        }
    }
    {
        const float softDeg = GE_ENVF("GE_INTERPFALLBACK_DEG", 1.5f);
        s_interp_next_req = s_interp_turn_max > softDeg ? softDeg / s_interp_turn_max : 1.0f;
    }
    s_interp_hid_n = 0;
    s_interp_rooms_prev = s_interp_rooms_pass;
    s_interp_rooms_prev_valid = true;
    return n;
}

/* Show present slot `slot` and swap (with the usual pre-swap capture hook).
 * vsync: wait for the swap to complete. Drivers queue a VSync'd swap and
 * return at once, which hid the display's real refresh from the present clock
 * (a 120.000 Hz timer against a panel that is never exactly 120 Hz drops and
 * repeats a frame every few seconds: a hitch). */
extern "C" void gfx_interp_present(int slot, int vsync) {
    gfx_opengl_interp_show(slot);
    gfx_wapi->swap_buffers_begin();
    gfx_rapi->finish_render();
    gfx_wapi->swap_buffers_end();
    /* D583: under gamescope (Steam Deck) the post-swap glFinish made a
     * present cost 3.3 ms (up to 14) on the thread that also draws: the worker
     * picked up new frames 3.5-6 ms late and dropped presents. Without it 0.3
     * ms and no drops (Deck A/B). The present grid runs from the reported
     * refresh, so it no longer needs the swap-return clock. Kept elsewhere (the
     * PC VRR fix relied on it). */
    static int s_gamescope = -1;
    if (s_gamescope < 0) {
        s_gamescope = getenv("GAMESCOPE_WAYLAND_DISPLAY") != NULL || getenv("SteamDeck") != NULL;
    }
    if (vsync && !s_gamescope) {
        gfx_opengl_interp_sync();
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
    s_shader_warm_pending = true;   /* D480: re-warm on the next frame */
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
            // D447: offset into the output rect (0,0 when the rect fills the window)
            left += gfx_current_game_window_viewport.x;
            top += gfx_current_game_window_viewport.y;
            // flip Y
            top = gfx_current_window_dimensions.height - top - 1;
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

