#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <chrono>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <SDL.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#include "glad/glad.h"

#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "gfx_pc.h"
#include "skyplus.h"   /* GE+ sky pass */

/* D578 L3: the glad (GL 1.0 base) header lacks this token. */
#ifndef GL_CLEAR_COLOR
#define GL_CLEAR_COLOR 0x0C22
#endif

using namespace std;

struct ShaderProgram {
    GLuint opengl_program_id;
    uint8_t num_inputs;
    bool used_textures[SHADER_MAX_TEXTURES];
    uint8_t num_floats;
    GLint attrib_locations[16];
    uint8_t attrib_sizes[16];
    uint8_t num_attribs;
    GLint frame_count_location;
    GLint noise_scale_location;
    GLint three_point_filter_locations[2];
    GLuint ring_vao;   /* D965: VAO of this shader's vertex layout (GE_GLSTREAM), 0 = not resolved */
};

struct Framebuffer {
    uint32_t width, height;
    bool has_depth_buffer;
    uint32_t msaa_level;
    bool invert_y;

    GLuint fbo, clrbuf, clrbuf_msaa, rbo;
};

static std::map<pair<uint64_t, uint32_t>, struct ShaderProgram> shader_program_pool;
static GLuint opengl_vbo;
static GLuint opengl_vao;
static bool current_depth_mask;

static uint32_t frame_count;

static std::vector<Framebuffer> framebuffers;
static size_t current_framebuffer;
static float current_noise_scale;
static int current_anisotropy_level = 4;   /* B1: sane default (0 was invalid for GL_TEXTURE_MAX_ANISOTROPY) */
static FilteringMode current_filter_mode = FILTER_LINEAR;
static MipmapFilteringMode current_mipmap_filter_mode = MIPMAP_LINEAR;
static bool current_textures_linear_filter[2] = {false, false};

static int gl_glsl_version = 130;
static char gl_glsl_version_str[16] = "130";
static GLenum gl_mirror_clamp = GL_MIRROR_CLAMP_TO_EDGE;
static bool gl_es = false;
extern int gfx_rdp_affine;   /* D540: defined in gfx_pc.cpp */
extern int gfx_fog_vertex;   /* D540: defined in gfx_pc.cpp */
static bool gl_core_profile = false;

static int gfx_opengl_get_max_texture_size() {
    GLint max_texture_size;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    return max_texture_size;
}

static const char* gfx_opengl_get_name() {
    return "OpenGL";
}

static struct GfxClipParameters gfx_opengl_get_clip_parameters(void) {
    return { false, framebuffers[current_framebuffer].invert_y };
}

static void gfx_opengl_vertex_array_set_attribs(struct ShaderProgram* prg) {
    size_t num_floats = prg->num_floats;
    size_t pos = 0;

    for (int i = 0; i < prg->num_attribs; i++) {
        if (prg->attrib_locations[i] >= 0) {
            glEnableVertexAttribArray(prg->attrib_locations[i]);
            glVertexAttribPointer(prg->attrib_locations[i], prg->attrib_sizes[i], GL_FLOAT, GL_FALSE,
                                num_floats * sizeof(float), (void*)(pos * sizeof(float)));
        }
        pos += prg->attrib_sizes[i];
    }
}

static void gfx_opengl_set_uniform_values(struct ShaderProgram* prg, uint32_t fc, float noise, bool tpf0, bool tpf1) {
    if (prg->frame_count_location >= 0) {
        glUniform1i(prg->frame_count_location, fc);
    }
    if (prg->noise_scale_location >= 0) {
        glUniform1f(prg->noise_scale_location, noise);
    }
    if (prg->three_point_filter_locations[0] >= 0) {
        glUniform1i(prg->three_point_filter_locations[0], tpf0);
    }
    if (prg->three_point_filter_locations[1] >= 0) {
        glUniform1i(prg->three_point_filter_locations[1], tpf1);
    }
}

static void gfx_opengl_set_uniforms(struct ShaderProgram* prg) {
    gfx_opengl_set_uniform_values(prg, frame_count, current_noise_scale, current_textures_linear_filter[0],
                                  current_textures_linear_filter[1]);
}

static bool s_stream_active = false;   /* D965: vertices come from the persistent ring (defined below) */


static void gfx_opengl_unload_shader(struct ShaderProgram* old_prg) {
    if (s_stream_active) {
        return;   /* D965: the layout VAOs keep their own enable state */
    }
    if (old_prg != NULL) {
        for (int i = 0; i < old_prg->num_attribs; i++) {
            if (old_prg->attrib_locations[i] >= 0) {
                glDisableVertexAttribArray(old_prg->attrib_locations[i]);
            }
        }
    }
}

#ifdef __SWITCH__
/* project-dam: streaming vertex ring. glBufferData per batch re-creates the
 * buffer's storage each time, hundreds of times a frame; mesa/nouveau pays an
 * allocation and often a GPU sync for it on the console's slow CPU. Instead,
 * one large buffer is filled front to back with unsynchronized maps, and only
 * a wrap orphans it (the driver hands out fresh storage while the GPU still
 * reads the old one). Each batch starts on a multiple of the shader's vertex
 * stride so it can be drawn with glDrawArrays' `first`. GE_VBORING=0 restores
 * the per-batch glBufferData, for A/B timing on the console. */
#define VBO_RING_SIZE (8u << 20)
static int s_vbo_ring = 1;
static size_t s_vbo_ring_off = 0;
static size_t s_vbo_stride = 0;   /* bytes per vertex of the bound shader */
#endif

/* D965 probes (GE_PERFSTAT): where the CPU time of a draw goes, accumulated
 * per frame and printed by gfx_opengl_perf_report(). */
static int s_gl_perf = -1;
static inline bool gl_perf_on(void) {
    if (s_gl_perf < 0) s_gl_perf = getenv("GE_PERFSTAT") != NULL;
    return s_gl_perf != 0;
}
static inline uint64_t gl_now_ns(void) {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
static uint64_t s_gld_map_ns, s_gld_copy_ns, s_gld_unmap_ns, s_gld_draw_ns, s_gld_attrib_ns;
static uint64_t s_gld_draws, s_gld_shader_changes, s_gld_bytes;

static GLuint gfx_opengl_ring_vao_for(struct ShaderProgram* prg);   /* D965, below */
static size_t s_cur_stride = 0;   /* bytes per vertex of the bound shader */


static void gfx_opengl_load_shader(struct ShaderProgram* new_prg) {
    // if (!new_prg) return;
    s_cur_stride = new_prg->num_floats * sizeof(float);
#ifdef __SWITCH__
    s_vbo_stride = s_cur_stride;
#endif
    glUseProgram(new_prg->opengl_program_id);
    const uint64_t t0 = gl_perf_on() ? gl_now_ns() : 0;
    if (s_stream_active) {
        glBindVertexArray(gfx_opengl_ring_vao_for(new_prg));   /* D965: no per-attribute setup */
    } else {
        gfx_opengl_vertex_array_set_attribs(new_prg);
    }
    if (t0) {
        s_gld_attrib_ns += gl_now_ns() - t0;
        s_gld_shader_changes++;
    }
    gfx_opengl_set_uniforms(new_prg);
}

/* D968: load + the uniform values the decoder latched (see GfxRenderingAPI::load_shader_with). */
static void gfx_opengl_load_shader_with(struct ShaderProgram* new_prg, uint32_t fc, float noise, bool tpf0, bool tpf1) {
    s_cur_stride = new_prg->num_floats * sizeof(float);
#ifdef __SWITCH__
    s_vbo_stride = s_cur_stride;
#endif
    glUseProgram(new_prg->opengl_program_id);
    const uint64_t t0 = gl_perf_on() ? gl_now_ns() : 0;
    if (s_stream_active) {
        glBindVertexArray(gfx_opengl_ring_vao_for(new_prg));   /* D965: no per-attribute setup */
    } else {
        gfx_opengl_vertex_array_set_attribs(new_prg);
    }
    if (t0) {
        s_gld_attrib_ns += gl_now_ns() - t0;
        s_gld_shader_changes++;
    }
    gfx_opengl_set_uniform_values(new_prg, fc, noise, tpf0, tpf1);
}

static void append_str(char* buf, size_t* len, const char* str) {
    while (*str != '\0') {
        buf[(*len)++] = *str++;
    }
}

static void append_line(char* buf, size_t* len, const char* str) {
    while (*str != '\0') {
        buf[(*len)++] = *str++;
    }
    buf[(*len)++] = '\n';
}

#define RAND_NOISE "((random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + 1.0) / 2.0)"

static const char* shader_item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool inputs_have_alpha,
                                      bool hint_single_element) {
    if (!only_alpha) {
        switch (item) {
            case SHADER_0:
                return with_alpha ? "vec4(0.0, 0.0, 0.0, 0.0)" : "vec3(0.0, 0.0, 0.0)";
            case SHADER_1:
                return with_alpha ? "vec4(1.0, 1.0, 1.0, 1.0)" : "vec3(1.0, 1.0, 1.0)";
            case SHADER_INPUT_1:
                return with_alpha || !inputs_have_alpha ? "cInput1" : "cInput1.rgb";
            case SHADER_INPUT_2:
                return with_alpha || !inputs_have_alpha ? "cInput2" : "cInput2.rgb";
            case SHADER_INPUT_3:
                return with_alpha || !inputs_have_alpha ? "cInput3" : "cInput3.rgb";
            case SHADER_INPUT_4:
                return with_alpha || !inputs_have_alpha ? "cInput4" : "cInput4.rgb";
            case SHADER_TEXEL0:
                return with_alpha ? "texVal0" : "texVal0.rgb";
            case SHADER_TEXEL0A:
                return hint_single_element ? "texVal0.a"
                                           : (with_alpha ? "vec4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)"
                                                         : "vec3(texVal0.a, texVal0.a, texVal0.a)");
            case SHADER_TEXEL1A:
                return hint_single_element ? "texVal1.a"
                                           : (with_alpha ? "vec4(texVal1.a, texVal1.a, texVal1.a, texVal1.a)"
                                                         : "vec3(texVal1.a, texVal1.a, texVal1.a)");
            case SHADER_TEXEL1:
                return with_alpha ? "texVal1" : "texVal1.rgb";
            case SHADER_COMBINED:
                return with_alpha ? "texel" : "texel.rgb";
            case SHADER_NOISE:
                return with_alpha ? "vec4(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")"
                                  : "vec3(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")";
        }
    } else {
        switch (item) {
            case SHADER_0:
                return "0.0";
            case SHADER_1:
                return "1.0";
            case SHADER_INPUT_1:
                return "cInput1.a";
            case SHADER_INPUT_2:
                return "cInput2.a";
            case SHADER_INPUT_3:
                return "cInput3.a";
            case SHADER_INPUT_4:
                return "cInput4.a";
            case SHADER_TEXEL0:
                return "texVal0.a";
            case SHADER_TEXEL0A:
                return "texVal0.a";
            case SHADER_TEXEL1A:
                return "texVal1.a";
            case SHADER_TEXEL1:
                return "texVal1.a";
            case SHADER_COMBINED:
                return "texel.a";
            case SHADER_NOISE:
                return RAND_NOISE;
        }
    }
    return "";
}

#undef RAND_NOISE

static void append_formula(char* buf, size_t* len, uint8_t c[2][4], bool do_single, bool do_multiply, bool do_mix,
                           bool with_alpha, bool only_alpha, bool opt_alpha) {
    if (do_single) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    } else if (do_multiply) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
    } else if (do_mix) {
        append_str(buf, len, "mix(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, ")");
    } else {
        append_str(buf, len, "(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " - ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ") * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, " + ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    }
}

#define SHADER_VS_MAX 2560
#define SHADER_FS_MAX 12288

/* Generates the vertex + fragment shader text of a combiner program. */
static void gfx_opengl_gen_shader_src(struct CCFeatures& cc_features, char* vs_buf, size_t* vs_len_out,
                                      char* fs_buf, size_t* fs_len_out, size_t* num_floats_out) {
    size_t vs_len = 0;
    size_t fs_len = 0;
    size_t num_floats = 4;

    // Vertex shader

    vs_len += sprintf(vs_buf + vs_len, "#version %s\n", gl_glsl_version_str);

    if (gl_es) {
        append_line(vs_buf, &vs_len, "precision mediump float;");
    }

    if (gl_glsl_version >= 130) {
        append_line(vs_buf, &vs_len, "#define INPUT in");
        append_line(vs_buf, &vs_len, "#define OUTPUT out");
    } else {
        append_line(vs_buf, &vs_len, "#define INPUT attribute");
        append_line(vs_buf, &vs_len, "#define OUTPUT varying");
    }

    append_line(vs_buf, &vs_len, "INPUT vec4 aVtxPos;");

    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            vs_len += sprintf(vs_buf + vs_len, "INPUT vec2 aTexCoord%d;\n", i);
            vs_len += sprintf(vs_buf + vs_len, "OUTPUT vec2 vTexCoord%d;\n", i);
            num_floats += 2;
            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    vs_len += sprintf(vs_buf + vs_len, "INPUT float aTexClamp%s%d;\n", j == 0 ? "S" : "T", i);
                    vs_len += sprintf(vs_buf + vs_len, "OUTPUT float vTexClamp%s%d;\n", j == 0 ? "S" : "T", i);
                    num_floats += 1;
                }
            }
        }
    }
    /* D540: RDP-style screen-affine shade colour (see gfx_opengl_init).
     * D543: fog is the per-vertex RSP value by default, which the RDP also
     * interpolates screen-affine (it rides in shade alpha), so it shares the
     * shade qualifier. GE_FOGPIXEL=1 (D540 per-pixel) needs a perspective
     * attribute: it carries fog * w, divided by w in the shader. */
    const char *shadeq = gfx_rdp_affine ? "noperspective " : "";
    const char *fogq = gfx_fog_vertex ? shadeq : "";
    if (cc_features.opt_fog) {
        append_line(vs_buf, &vs_len, "INPUT vec4 aFog;");
        vs_len += sprintf(vs_buf + vs_len, "%sOUTPUT vec4 vFog;\n", fogq);
        num_floats += 4;
    }

    if (cc_features.opt_grayscale) {
        append_line(vs_buf, &vs_len, "INPUT vec4 aGrayscaleColor;");
        append_line(vs_buf, &vs_len, "OUTPUT vec4 vGrayscaleColor;");
        num_floats += 4;
    }

    for (int i = 0; i < cc_features.num_inputs; i++) {
        vs_len += sprintf(vs_buf + vs_len, "INPUT vec%d aInput%d;\n", cc_features.opt_alpha ? 4 : 3, i + 1);
        vs_len += sprintf(vs_buf + vs_len, "%sOUTPUT vec%d vInput%d;\n", shadeq, cc_features.opt_alpha ? 4 : 3, i + 1);
        num_floats += cc_features.opt_alpha ? 4 : 3;
    }

    append_line(vs_buf, &vs_len, "void main() {");
    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            vs_len += sprintf(vs_buf + vs_len, "    vTexCoord%d = aTexCoord%d;\n", i, i);
            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    vs_len += sprintf(vs_buf + vs_len, "    vTexClamp%s%d = aTexClamp%s%d;\n", j == 0 ? "S" : "T", i,
                                      j == 0 ? "S" : "T", i);
                }
            }
        }
    }
    if (cc_features.opt_fog) {
        append_line(vs_buf, &vs_len, "    vFog = aFog;");
    }
    if (cc_features.opt_grayscale) {
        append_line(vs_buf, &vs_len, "    vGrayscaleColor = aGrayscaleColor;");
    }
    for (int i = 0; i < cc_features.num_inputs; i++) {
        vs_len += sprintf(vs_buf + vs_len, "    vInput%d = aInput%d;\n", i + 1, i + 1);
    }

    append_line(vs_buf, &vs_len, "    gl_Position = aVtxPos;");
    if (!GLAD_GL_ARB_depth_clamp) {
        // HACK: workaround for no GL_DEPTH_CLAMP
        append_line(vs_buf, &vs_len, "    gl_Position.z *= 0.3f;");
    }
    append_line(vs_buf, &vs_len, "}");

    // Fragment shader

    fs_len += sprintf(fs_buf + fs_len, "#version %s\n", gl_glsl_version_str);

    if (gl_es) {
        append_line(fs_buf, &fs_len, "precision mediump float;");
    }

    if (gl_glsl_version >= 130) {
        append_line(fs_buf, &fs_len, "#define INPUT in");
        append_line(fs_buf, &fs_len, "#define OUTPUT_COLOR outColor");
        append_line(fs_buf, &fs_len, "#define SAMPLE_TEX(tex, uv) texture(tex, uv)");
    } else {
        append_line(fs_buf, &fs_len, "#define INPUT varying");
        append_line(fs_buf, &fs_len, "#define OUTPUT_COLOR gl_FragColor");
        append_line(fs_buf, &fs_len, "#define SAMPLE_TEX(tex, uv) texture2D(tex, uv)");
    }

    // Reference approach to color wrapping as per GLideN64
    // Return wrapped value of x in interval [low, high)
    append_line(fs_buf, &fs_len, "#define WRAP(x, low, high) mod((x)-(low), (high)-(low)) + (low)");

    append_line(fs_buf, &fs_len, "#define TEX_OFFSET(tex, uv, texSize, off) SAMPLE_TEX(tex, uv - (off)/texSize)");

    // append_line(fs_buf, &fs_len, "precision mediump float;");
    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            fs_len += sprintf(fs_buf + fs_len, "INPUT vec2 vTexCoord%d;\n", i);
            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    fs_len += sprintf(fs_buf + fs_len, "INPUT float vTexClamp%s%d;\n", j == 0 ? "S" : "T", i);
                }
            }
        }
    }
    if (cc_features.opt_fog) {
        fs_len += sprintf(fs_buf + fs_len, "%sINPUT vec4 vFog;\n", fogq);
    }
    if (cc_features.opt_grayscale) {
        append_line(fs_buf, &fs_len, "INPUT vec4 vGrayscaleColor;");
    }
    for (int i = 0; i < cc_features.num_inputs; i++) {
        fs_len += sprintf(fs_buf + fs_len, "%sINPUT vec%d vInput%d;\n", shadeq, cc_features.opt_alpha ? 4 : 3, i + 1);
    }

    if (cc_features.used_textures[0]) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uTex0;");
        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len, "uniform int three_point_filter0;");
    }
    if (cc_features.used_textures[1]) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uTex1;");
        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len, "uniform int three_point_filter1;");
    }

    append_line(fs_buf, &fs_len, "uniform int frame_count;");
    append_line(fs_buf, &fs_len, "uniform float noise_scale;");

    append_line(fs_buf, &fs_len, "float random(in vec3 value) {");
    append_line(fs_buf, &fs_len, "    float random = dot(sin(value), vec3(12.9898, 78.233, 37.719));");
    append_line(fs_buf, &fs_len, "    return fract(sin(random) * 143758.5453);");
    append_line(fs_buf, &fs_len, "}");

    if (current_filter_mode == FILTER_THREE_POINT) {
        append_line(fs_buf, &fs_len, "vec4 filter3point(in sampler2D tex, in vec2 texCoord, in vec2 texSize) {");
        append_line(fs_buf, &fs_len, "    vec2 offset = fract(texCoord*texSize - vec2(0.5));");
        append_line(fs_buf, &fs_len, "    offset -= step(1.0, offset.x + offset.y);");
        append_line(fs_buf, &fs_len, "    vec4 c0 = TEX_OFFSET(tex, texCoord, texSize, offset);");
        append_line(fs_buf, &fs_len, "    vec4 c1 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x - sign(offset.x), offset.y));");
        append_line(fs_buf, &fs_len, "    vec4 c2 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x, offset.y - sign(offset.y)));");
        append_line(fs_buf, &fs_len, "    return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);");
        append_line(fs_buf, &fs_len, "}");
    }

    if (cc_features.opt_blur) {
        // blur filter, used for menu backgrounds
        // used to be two for loops from 0 to 4, but apparently intel drivers crashed trying to unroll it
        // used to have a const weight array, but apparently drivers for the GT620 don't like const array initializers

        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len, "lowp vec4 hookTexture2D(in sampler2D t, in vec2 uv, in vec2 texSize, in int three_point_filter) {");
        else
            append_line(fs_buf, &fs_len, "lowp vec4 hookTexture2D(in sampler2D t, in vec2 uv, in vec2 texSize) {");

        append_line(fs_buf, &fs_len, "    lowp vec4 cw = vec4(0.0);");
        append_line(fs_buf, &fs_len, "    for (int i = 0; i < 16; ++i) {");
        append_line(fs_buf, &fs_len, "        vec2 xy = vec2(float(i & 3), float(i >> 2));");
        append_line(fs_buf, &fs_len, "        lowp float w = 0.009947 - length(xy) * 0.001;");
        append_line(fs_buf, &fs_len, "        vec2 scaled_uv = uv + (vec2(-1.5) + xy) / texSize;");

        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len, "        lowp vec4 tex = mix(SAMPLE_TEX(t, scaled_uv), filter3point(t, scaled_uv, texSize), three_point_filter);");
        else
            append_line(fs_buf, &fs_len, "        lowp vec4 tex = SAMPLE_TEX(t, scaled_uv);");

        append_line(fs_buf, &fs_len, "        cw += vec4(tex.rgb * w, w);");
        append_line(fs_buf, &fs_len, "    }");
        append_line(fs_buf, &fs_len, "    return vec4(cw.rgb / cw.a, 1.0);");
        append_line(fs_buf, &fs_len, "}");
    } else {
        if (current_filter_mode == FILTER_THREE_POINT) {
            append_line(fs_buf, &fs_len, "vec4 hookTexture2D(in sampler2D tex, in vec2 uv, in vec2 texSize, in int three_point_filter) {");
            append_line(fs_buf, &fs_len, "    return mix(SAMPLE_TEX(tex, uv), filter3point(tex, uv, texSize), three_point_filter);");
            append_line(fs_buf, &fs_len, "}");
        } else {
            append_line(fs_buf, &fs_len, "vec4 hookTexture2D(in sampler2D tex, in vec2 uv, in vec2 texSize) {");
            append_line(fs_buf, &fs_len, "    return SAMPLE_TEX(tex, uv);");
            append_line(fs_buf, &fs_len, "}");
        }
    }

    if (gl_glsl_version >= 130) {
        append_line(fs_buf, &fs_len, "out vec4 outColor;");
    }

    append_line(fs_buf, &fs_len, "void main() {");
    /* D548: combiner inputs are 8-bit on the N64 (shade, prim, env), so never
     * above 1.0. Under the D540 noperspective shade, GPU clipping of a big
     * near-camera triangle can extrapolate them past 1.0; the CC wrap
     * emulation below then turned walls into black/white/yellow/blue bands
     * (Aztec, near rocket fire). Clamp to the hardware range first. */
    for (int i = 0; i < cc_features.num_inputs; i++) {
        fs_len += sprintf(fs_buf + fs_len, "    vec%d cInput%d = clamp(vInput%d, 0.0, 1.0);\n",
                          cc_features.opt_alpha ? 4 : 3, i + 1, i + 1);
    }

    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            bool s = cc_features.clamp[i][0], t = cc_features.clamp[i][1];

            fs_len += sprintf(fs_buf + fs_len, "    vec2 texSize%d = vec2(textureSize(uTex%d, 0));\n", i, i);

            if (!s && !t) {
                fs_len += sprintf(fs_buf + fs_len, "    vec2 vTexCoordAdj%d = vTexCoord%d;\n", i, i);
            } else {
                if (s && t) {
                    fs_len += sprintf(fs_buf + fs_len,
                                      "    vec2 vTexCoordAdj%d = clamp(vTexCoord%d, 0.5 / texSize%d, "
                                      "vec2(vTexClampS%d, vTexClampT%d));\n",
                                      i, i, i, i, i);
                } else if (s) {
                    fs_len += sprintf(fs_buf + fs_len,
                                      "    vec2 vTexCoordAdj%d = vec2(clamp(vTexCoord%d.s, 0.5 / "
                                      "texSize%d.s, vTexClampS%d), vTexCoord%d.t);\n",
                                      i, i, i, i, i);
                } else {
                    fs_len += sprintf(fs_buf + fs_len,
                                      "    vec2 vTexCoordAdj%d = vec2(vTexCoord%d.s, clamp(vTexCoord%d.t, "
                                      "0.5 / texSize%d.t, vTexClampT%d));\n",
                                      i, i, i, i, i);
                }
            }

            if (current_filter_mode == FILTER_THREE_POINT)
                fs_len += sprintf(fs_buf + fs_len, "    vec4 texVal%d = hookTexture2D(uTex%d, vTexCoordAdj%d, texSize%d, three_point_filter%d);\n", i, i, i, i, i);
            else
                fs_len += sprintf(fs_buf + fs_len, "    vec4 texVal%d = hookTexture2D(uTex%d, vTexCoordAdj%d, texSize%d);\n", i, i, i, i);
        }
    }

    append_line(fs_buf, &fs_len, cc_features.opt_alpha ? "    vec4 texel;" : "    vec3 texel;");
    for (int c = 0; c < (cc_features.opt_2cyc ? 2 : 1); c++) {
        append_str(fs_buf, &fs_len, "    texel = ");
        if (!cc_features.color_alpha_same[c] && cc_features.opt_alpha) {
            append_str(fs_buf, &fs_len, "vec4(");
            append_formula(fs_buf, &fs_len, cc_features.c[c], cc_features.do_single[c][0],
                           cc_features.do_multiply[c][0], cc_features.do_mix[c][0], false, false, true);
            append_str(fs_buf, &fs_len, ", ");
            append_formula(fs_buf, &fs_len, cc_features.c[c], cc_features.do_single[c][1],
                           cc_features.do_multiply[c][1], cc_features.do_mix[c][1], true, true, true);
            append_str(fs_buf, &fs_len, ")");
        } else {
            append_formula(fs_buf, &fs_len, cc_features.c[c], cc_features.do_single[c][0],
                           cc_features.do_multiply[c][0], cc_features.do_mix[c][0], cc_features.opt_alpha, false,
                           cc_features.opt_alpha);
        }
        append_line(fs_buf, &fs_len, ";");

        if (c == 0) {
            append_line(fs_buf, &fs_len, "    texel = WRAP(texel, -1.01, 1.01);");
        }
    }

    append_line(fs_buf, &fs_len, "    texel = WRAP(texel, -0.51, 1.51);");
    append_line(fs_buf, &fs_len, "    texel = clamp(texel, 0.0, 1.0);");
    // TODO discard if alpha is 0?
    if (cc_features.opt_fog) {
        /* D543 default: vFog.a is the per-vertex RSP value, used as-is
         * (N64). GE_FOGPIXEL=1 (D540): vFog.a = fog * w, divided by w here =
         * exact per-pixel fog from the pixel's own depth. */
        append_line(fs_buf, &fs_len, gfx_fog_vertex ? "    float fogA = clamp(vFog.a, 0.0, 1.0);"
                                                    : "    float fogA = clamp(vFog.a * gl_FragCoord.w, 0.0, 1.0);");
        /* D540: the D503 snap is OFF by default -- its hard 97% edge swept
         * across the ground in tile-shaped chunks as the player walked
         * (Deck playtest; maintainer-confirmed with it off). GE_FOGSNAP=1
         * restores it. */
        static int fogSnap = -1;
        if (fogSnap < 0) { const char *e = getenv("GE_FOGSNAP"); fogSnap = (e && e[0] == '1'); }
        if (fogSnap) append_line(fs_buf, &fs_len, "    if (fogA >= 247.0 / 255.0) fogA = 1.0;");
        if (cc_features.opt_alpha) {
            append_line(fs_buf, &fs_len, "    texel = vec4(mix(texel.rgb, vFog.rgb, fogA), texel.a);");
        } else {
            append_line(fs_buf, &fs_len, "    texel = mix(texel, vFog.rgb, fogA);");
        }
    }

    /* D266: CVG_X_ALPHA ("texedge") modelled on the RDP's actual semantics,
     * keyed on the alphacompare field. The inherited line quantized to
     * opaque/discard at a hardcoded 0.19: it kept texels the N64 discards and
     * forced survivors fully opaque, defeating the ALPHA_CVG_SEL blend.
     * GE's Surface 1 IA4 tree strip (D236/D265) rendered as a wall because of
     * it. Now: AC_DITHER -> dithered discard; AC_THRESHOLD -> discard only
     * fully-transparent; AC_NONE -> discard below 0x80 and let survivors keep
     * their alpha for blending. (AC_DECAL never sets SHADER_OPT_TEXTURE_EDGE,
     * see gfx_pc.cpp.) */
    if (cc_features.opt_texture_edge && cc_features.opt_alpha) {
        if (cc_features.opt_noise) {
            append_line(fs_buf, &fs_len,
                        "    if (texel.a < ((random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + 1.0) / 2.0)) discard;");
        } else if (cc_features.opt_alpha_threshold) {
            append_line(fs_buf, &fs_len, "    if (texel.a < 1.0 / 256.0) discard;");
        } else {
            append_line(fs_buf, &fs_len, "    if (texel.a < 0.5) discard;");
        }
    }

    if (cc_features.opt_alpha && cc_features.opt_noise && !cc_features.opt_texture_edge) {
        append_line(fs_buf, &fs_len,
                    "    texel.a *= floor(clamp(random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + "
                    "texel.a, 0.0, 1.0));");
    }

    if (cc_features.opt_grayscale) {
        append_line(fs_buf, &fs_len, "    float intensity = (texel.r + texel.g + texel.b) / 3.0;");
        append_line(fs_buf, &fs_len, "    vec3 new_texel = vGrayscaleColor.rgb * intensity;");
        append_line(fs_buf, &fs_len, "    texel.rgb = mix(texel.rgb, new_texel, vGrayscaleColor.a);");
    }

    if (cc_features.opt_alpha) {
        if (cc_features.opt_alpha_threshold) {
            append_line(fs_buf, &fs_len, "    if (texel.a < 8.0 / 256.0) discard;");
        }
        if (cc_features.opt_invisible) {
            append_line(fs_buf, &fs_len, "    texel.a = 0.0;");
        }

        append_line(fs_buf, &fs_len, "    OUTPUT_COLOR = texel;");
    } else {
        append_line(fs_buf, &fs_len, "    OUTPUT_COLOR = vec4(texel, 1.0);");
    }

    append_line(fs_buf, &fs_len, "}");

    vs_buf[vs_len] = '\0';
    fs_buf[fs_len] = '\0';
    *vs_len_out = vs_len;
    *fs_len_out = fs_len;
    *num_floats_out = num_floats;
}

static struct ShaderProgram* gfx_opengl_create_and_load_new_shader(uint64_t shader_id0, uint32_t shader_id1) {
    struct CCFeatures cc_features = { 0 };
    gfx_cc_get_features(shader_id0, shader_id1, &cc_features);


    char vs_buf[SHADER_VS_MAX];
    char fs_buf[SHADER_FS_MAX];
    size_t vs_len = 0;
    size_t fs_len = 0;
    size_t num_floats = 4;
    gfx_opengl_gen_shader_src(cc_features, vs_buf, &vs_len, fs_buf, &fs_len, &num_floats);

    const GLchar* sources[2] = { vs_buf, fs_buf };
    const GLint lengths[2] = { (GLint)vs_len, (GLint)fs_len };
    GLint success;

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &sources[0], &lengths[0]);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 1024;
        glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        glGetShaderInfoLog(vertex_shader, max_length, &max_length, &error_log[0]);
        sysLogPrintf(LOG_ERROR, "Failed to compile this vertex shader (ID %llx, %x):\n%s", shader_id0, shader_id1, vs_buf);
        sysFatalError("Vertex shader compilation failed:\n%s", error_log);
    }

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &sources[1], &lengths[1]);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 1024;
        glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        glGetShaderInfoLog(fragment_shader, max_length, &max_length, &error_log[0]);
        sysLogPrintf(LOG_ERROR, "Failed to compile this fragment shader (ID %llx, %x):\n%s", shader_id0, shader_id1, fs_buf);
        sysFatalError("Fragment shader compilation failed:\n%s", error_log);
    }

    GLuint shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);

    glDetachShader(shader_program, vertex_shader);
    glDetachShader(shader_program, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    size_t cnt = 0;

    struct ShaderProgram* prg = &shader_program_pool[make_pair(shader_id0, shader_id1)];
    prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aVtxPos");
    prg->attrib_sizes[cnt] = 4;
    ++cnt;

    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            char name[32];
            sprintf(name, "aTexCoord%d", i);
            prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
            prg->attrib_sizes[cnt] = 2;
            ++cnt;

            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    sprintf(name, "aTexClamp%s%d", j == 0 ? "S" : "T", i);
                    prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
                    prg->attrib_sizes[cnt] = 1;
                    ++cnt;
                }
            }
        }
    }

    if (cc_features.opt_fog) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aFog");
        prg->attrib_sizes[cnt] = 4;
        ++cnt;
    }

    if (cc_features.opt_grayscale) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aGrayscaleColor");
        prg->attrib_sizes[cnt] = 4;
        ++cnt;
    }

    for (int i = 0; i < cc_features.num_inputs; i++) {
        char name[16];
        sprintf(name, "aInput%d", i + 1);
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
        prg->attrib_sizes[cnt] = cc_features.opt_alpha ? 4 : 3;
        ++cnt;
    }

    prg->opengl_program_id = shader_program;
    prg->num_inputs = cc_features.num_inputs;
    prg->used_textures[0] = cc_features.used_textures[0];
    prg->used_textures[1] = cc_features.used_textures[1];
    prg->num_floats = num_floats;
    prg->num_attribs = cnt;

    glUseProgram(shader_program);

    if (cc_features.used_textures[0]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex0");
        glUniform1i(sampler_location, 0);
    }
    if (cc_features.used_textures[1]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex1");
        glUniform1i(sampler_location, 1);
    }

    prg->frame_count_location = glGetUniformLocation(shader_program, "frame_count");
    prg->noise_scale_location = glGetUniformLocation(shader_program, "noise_scale");
    prg->three_point_filter_locations[0] = glGetUniformLocation(shader_program, "three_point_filter0");
    prg->three_point_filter_locations[1] = glGetUniformLocation(shader_program, "three_point_filter1");


    gfx_opengl_load_shader(prg);

    return prg;
}

static struct ShaderProgram* gfx_opengl_lookup_shader(uint64_t shader_id0, uint32_t shader_id1) {
    auto it = shader_program_pool.find(make_pair(shader_id0, shader_id1));
    return it == shader_program_pool.end() ? nullptr : &it->second;
}

static void gfx_opengl_shader_get_info(struct ShaderProgram* prg, uint8_t* num_inputs, bool used_textures[2]) {
    *num_inputs = prg->num_inputs;
    used_textures[0] = prg->used_textures[0];
    used_textures[1] = prg->used_textures[1];
}

static void gfx_opengl_clear_shaders(void) {
    glUseProgram(0);
    for (auto& pair : shader_program_pool) {
        glDeleteProgram(pair.second.opengl_program_id);
    }
    shader_program_pool.clear();
}

struct SamplerKey {
    GLint min, mag;
    GLfloat aniso;
    GLenum ws, wt;
};

static GLuint gfx_opengl_new_texture(void) {
    GLuint ret;
    glGenTextures(1, &ret);
    return ret;
}

static void gfx_opengl_delete_texture(uint32_t texID) {
    glDeleteTextures(1, &texID);
}

static void gfx_opengl_select_texture(int tile, GLuint texture_id, bool linear_filter) {
    glActiveTexture(GL_TEXTURE0 + tile);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    current_textures_linear_filter[tile] = linear_filter;
}

static void gfx_opengl_upload_texture(const uint8_t* rgba32_buf, uint32_t width, uint32_t height, bool gen_mipmaps) {
#ifdef PORT
    /* GE_TEXDUMP: PPM-dump every uploaded texture (first N) for B2/D161 triage.
     * D250: this runs on every texture upload -- cache the getenv() like the
     * gfx_pc.cpp import_texture() sites (uncached getenv() measured at ~50%
     * of the hot render thread's CPU time via a live profile). */
    static int ge_texdump_gl = -1;
    if (ge_texdump_gl < 0) ge_texdump_gl = getenv("GE_TEXDUMP") != NULL;
    if (ge_texdump_gl) {
        static int td = 0;
        static int td_fire = 0;
        /* D219: the 400-cap fills up during ordinary level load, long before
         * a player-triggered explosion. Never suppress the 16x14 RGBA fire
         * particle images (assets/oddtextures.c IMAGE_FIRE_N via
         * globalDL_0x078..0x9a8) -- that's the one shape we actually need. */
        const bool is_fire_shape = (width == 16 && height == 14);
        if ((is_fire_shape || td < 400) && width && height && width < 4096 && height < 4096) {
            char base[128], nm[160];
            if (is_fire_shape) {
                snprintf(base, sizeof base, "texdump/fire%03d_%ux%u_mip%d", td_fire++, width, height, (int)gen_mipmaps);
            } else {
                snprintf(base, sizeof base, "texdump/t%03d_%ux%u_mip%d", td++, width, height, (int)gen_mipmaps);
            }
            /* D592: PPM = the RGB triage view (alpha dropped). */
            snprintf(nm, sizeof nm, "%s.ppm", base);
            FILE* f = fopen(nm, "wb");
            if (f) {
                fprintf(f, "P6\n%u %u\n255\n", width, height);
                for (uint32_t p = 0; p < width * height; p++)
                    fwrite(rgba32_buf + 4 * p, 1, 3, f);
                fclose(f);
            }
            /* D592 (Q1): the EXACT 4-byte RGBA32 glTexImage2D input (with
             * alpha), so the Mac/Win upload comparison is byte-exact (the PPM
             * alone cannot reveal an alpha / channel-order artifact). */
            snprintf(nm, sizeof nm, "%s.rgba32", base);
            FILE* fb = fopen(nm, "wb");
            if (fb) {
                fwrite(rgba32_buf, 1, (size_t)width * height * 4, fb);
                fclose(fb);
            }
        }
    }
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba32_buf);
	/* Trilinear option (playtest 2026-10-03): force mips for TRILINEAR too. */
	if (gen_mipmaps || current_filter_mode == FILTER_THREE_POINT || current_filter_mode == FILTER_TRILINEAR) {
#ifdef __SWITCH__
		/* project-dam: build the mip chain on the CPU (2x2 box filter, like
		 * glGenerateMipmap) and upload each level. On Switch's mesa,
		 * glGenerateMipmap runs GPU blits that the next draws then wait on:
		 * measured in Ryujinx, it was most of the stalls when new textures
		 * appear (DrawArrays waits 1819 ms -> 90 ms over the same intro loop).
		 * N64 textures are tiny, so the CPU chain costs microseconds. */
		static std::vector<uint8_t> s_mip[2];
		const uint8_t* src = rgba32_buf;
		uint32_t w = width, h = height;
		for (GLint level = 1; w > 1 || h > 1; level++) {
			const uint32_t nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
			std::vector<uint8_t>& dst = s_mip[level & 1];
			dst.resize((size_t)nw * nh * 4);
			for (uint32_t y = 0; y < nh; y++) {
				const uint32_t y0 = y * 2 < h ? y * 2 : h - 1, y1 = y * 2 + 1 < h ? y * 2 + 1 : y0;
				for (uint32_t x = 0; x < nw; x++) {
					const uint32_t x0 = x * 2 < w ? x * 2 : w - 1, x1 = x * 2 + 1 < w ? x * 2 + 1 : x0;
					const uint8_t* a = src + ((size_t)y0 * w + x0) * 4;
					const uint8_t* b = src + ((size_t)y0 * w + x1) * 4;
					const uint8_t* c = src + ((size_t)y1 * w + x0) * 4;
					const uint8_t* d = src + ((size_t)y1 * w + x1) * 4;
					uint8_t* o = &dst[((size_t)y * nw + x) * 4];
					for (int k = 0; k < 4; k++) {
						o[k] = (uint8_t)((a[k] + b[k] + c[k] + d[k] + 2) >> 2);
					}
				}
			}
			glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, nw, nh, 0, GL_RGBA, GL_UNSIGNED_BYTE, dst.data());
			src = dst.data();
			w = nw;
			h = nh;
		}
#else
		glGenerateMipmap(GL_TEXTURE_2D);
#endif
	}
}

static uint32_t gfx_cm_to_opengl(uint32_t val) {
    switch (val) {
        case G_TX_NOMIRROR | G_TX_CLAMP:
            return GL_CLAMP_TO_EDGE;
        case G_TX_MIRROR | G_TX_WRAP:
            return GL_MIRRORED_REPEAT;
        case G_TX_MIRROR | G_TX_CLAMP:
            return gl_mirror_clamp;
        case G_TX_NOMIRROR | G_TX_WRAP:
            return GL_REPEAT;
    }
    return 0;
}

static SamplerKey gfx_opengl_sampler_key(bool linear_filter, uint32_t cms, uint32_t cmt, bool mipmaps) {
    const GLint min_filters[4][3] = {
        // MIPMAP_DISABLED   MIPMAP_NEAREST               MIPMAP_LINEAR
        {  GL_NEAREST,       GL_NEAREST_MIPMAP_NEAREST,   GL_NEAREST_MIPMAP_LINEAR  }, // FILTER_NONE
        {  GL_LINEAR,        GL_LINEAR_MIPMAP_NEAREST,    GL_LINEAR_MIPMAP_LINEAR   }, // FILTER_BILINEAR
        {  GL_NEAREST,       GL_LINEAR_MIPMAP_NEAREST,    GL_LINEAR_MIPMAP_LINEAR   }, // FILTER_THREE_POINT
        {  GL_LINEAR,        GL_LINEAR_MIPMAP_LINEAR,     GL_LINEAR_MIPMAP_LINEAR   }, // FILTER_TRILINEAR (Trilinear option, playtest 2026-10-03)
    };

    // B1: the shader does 3-point on magnification; for minification let the
    // hardware do trilinear mips. THREE_POINT force-generates mips at upload
    // (see gfx_opengl_upload_texture) so it can always sample them here -- this
    // is what kills the grazing-angle shimmer (Depot roof, docs/BRIEF-B2).
    /* Trilinear option (playtest 2026-10-03): TRILINEAR force-generates mips
     * at upload too, so it can always sample them here. */
    if (current_filter_mode == FILTER_THREE_POINT || current_filter_mode == FILTER_TRILINEAR) {
        mipmaps = true;
    }
    mipmaps = mipmaps && (current_mipmap_filter_mode != MIPMAP_DISABLED);
    const int mip_idx = mipmaps ? current_mipmap_filter_mode : 0;
    const GLint min_filter = linear_filter ? min_filters[current_filter_mode][mip_idx] : GL_NEAREST;
    /* Trilinear option (playtest 2026-10-03): TRILINEAR magnifies LINEAR too. */
    const GLint max_filter = linear_filter && (current_filter_mode == FILTER_LINEAR || current_filter_mode == FILTER_TRILINEAR) ? GL_LINEAR : GL_NEAREST;

    SamplerKey key;
    key.min = min_filter;
    key.mag = max_filter;
    key.aniso = mipmaps ? (float)current_anisotropy_level : 1.0f;
    key.ws = gfx_cm_to_opengl(cms);
    key.wt = gfx_cm_to_opengl(cmt);
    return key;
}

static void gfx_opengl_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt, bool mipmaps) {
    const SamplerKey k = gfx_opengl_sampler_key(linear_filter, cms, cmt, mipmaps);
    glActiveTexture(GL_TEXTURE0 + tile);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, k.min);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, k.mag);

#ifdef PORT
    /* D446: anisotropy is per-texture-object state. It was only ever SET
     * (when mipmapped), never reset, so a GL texture id freed by
     * gfx_texture_cache_delete_range and reused for a non-mipmapped texture
     * (HUD / watch text glyphs) kept the previous owner's 4x -- llvmpipe then
     * filters that glyph differently (+-1..2 per channel). Which id a texture
     * inherits follows the unordered_map iteration order, i.e. the texture
     * ADDRESSES, so the frame changed with the binary's data layout. Always
     * write it: the level when mipmapped, 1 (off) otherwise. */
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, k.aniso);
#else
    if (k.aniso > 1.0f) {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, k.aniso);
    }
#endif

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gfx_cm_to_opengl(cms));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gfx_cm_to_opengl(cmt));
}

static void gfx_opengl_set_depth_mode(bool depth_test, bool depth_update, bool depth_compare, bool depth_source_prim, uint16_t zmode) {
    if (depth_test) {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(depth_update ? GL_TRUE : GL_FALSE);
        current_depth_mask = depth_update;

        if (depth_compare) {
            switch (zmode) {
                case ZMODE_INTER:
                    glDepthFunc(GL_LEQUAL);
                    glDisable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(0, 0);
                    break;

                case ZMODE_OPA:
                case ZMODE_XLU:
                    if (depth_source_prim) {
                        glDepthFunc(GL_LEQUAL);
                    } else {
                        glDepthFunc(GL_LESS);
                    }
                    glDisable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(0, 0);
                    break;

                case ZMODE_DEC:
                    glDepthFunc(GL_LEQUAL);
                    glEnable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(-2, -2);
                    break;
            }
        } else {
            glDepthFunc(GL_ALWAYS);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(0, 0);
        }
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

static void gfx_opengl_set_depth_range(float znear, float zfar) {
    if (glDepthRangef) {
        glDepthRangef(znear, zfar);
    } else {
        glDepthRange(znear, zfar);
    }
}

static void gfx_opengl_set_viewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}

static void gfx_opengl_set_scissor(int x, int y, int width, int height) {
    glScissor(x, y, width, height);
}

static void gfx_opengl_set_use_alpha(bool use_alpha, bool modulate) {
    if (use_alpha) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    if (modulate) {
        glBlendFunc(GL_DST_COLOR, GL_ZERO);
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}

/* ---------------------------------------------------------------------------
 * D965 (GE_GLSTREAM=1): persistent coherent vertex ring.
 *
 * One glBufferStorage buffer (MAP_WRITE | MAP_PERSISTENT | MAP_COHERENT) is
 * mapped once and split into STREAM_SECTIONS sections. gfx_sp_tri1 writes a
 * batch's vertices straight into the mapped memory (stream_begin hands out
 * the pointer), and a draw is glDrawArrays(first, count) on the ring: no
 * glMapBufferRange / memcpy / glUnmapBuffer per draw. Sections are used front
 * to back; when the current one cannot hold another full batch, a fence is
 * inserted after its last draw and the next section is entered, waiting on
 * that section's own fence first, and only if the GPU is still reading it
 * (counted). Vertex formats live in VAOs, one per layout (stride + attribute
 * locations/sizes), so a shader change binds a VAO instead of re-issuing a
 * glVertexAttribPointer per attribute. GE_GLSTREAM_MB sets the section size
 * (default 4 MiB, 3 sections). Without ARB_buffer_storage (or GL 4.4) the
 * switch logs once and the old path stays in use.
 * ------------------------------------------------------------------------- */
#ifndef GL_MAP_PERSISTENT_BIT
#define GL_MAP_PERSISTENT_BIT 0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT
#define GL_MAP_COHERENT_BIT 0x0080
#endif
typedef void (APIENTRY *ge_glBufferStorage_t)(GLenum target, GLsizeiptr size, const void* data, GLbitfield flags);

#define STREAM_SECTIONS 3
static bool s_stream_wanted = false;       /* GE_GLSTREAM=1 */
static bool s_stream_avail = false;        /* ring created and mapped */
static GLuint s_stream_buf = 0;
static uint8_t* s_stream_map = NULL;
static size_t s_stream_sec_size = 4u << 20;
static unsigned s_stream_sec = 0;          /* current section */
static size_t s_stream_off = 0;            /* bytes used in the current section */
static GLsync s_stream_fence[STREAM_SECTIONS];
static float* s_stream_batch_ptr = NULL;   /* the open batch (stream_begin -> draw_triangles) */
static size_t s_stream_batch_abs = 0;      /* its byte offset in the buffer */
static uint64_t s_stream_waits, s_stream_wait_ns, s_stream_advances, s_stream_fallbacks;
static size_t s_stream_peak = 0;           /* most bytes used in one section */
static std::map<std::string, GLuint> s_layout_vao;

static GLuint gfx_opengl_ring_vao_for(struct ShaderProgram* prg) {
    if (prg->ring_vao != 0) {
        return prg->ring_vao;
    }
    std::string key;
    key.push_back((char)prg->num_floats);
    for (int i = 0; i < prg->num_attribs; i++) {
        key.push_back((char)prg->attrib_locations[i]);
        key.push_back((char)prg->attrib_sizes[i]);
    }
    auto it = s_layout_vao.find(key);
    if (it == s_layout_vao.end()) {
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        GLint prev_vao = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, s_stream_buf);   /* attribute pointers capture this binding */
        gfx_opengl_vertex_array_set_attribs(prg);
        glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);
        glBindVertexArray((GLuint)prev_vao);
        it = s_layout_vao.insert(std::make_pair(key, vao)).first;
    }
    prg->ring_vao = it->second;
    return prg->ring_vao;
}

static void gfx_opengl_stream_wait(unsigned sec) {
    GLsync f = s_stream_fence[sec];
    if (f == NULL) {
        return;
    }
    GLenum r = glClientWaitSync(f, 0, 0);
    if (r == GL_TIMEOUT_EXPIRED) {
        /* the GPU is still reading this section */
        const uint64_t t0 = gl_now_ns();
        int tries = 0;
        do {
            r = glClientWaitSync(f, GL_SYNC_FLUSH_COMMANDS_BIT, 50ull * 1000 * 1000);
        } while (r == GL_TIMEOUT_EXPIRED && ++tries < 100);
        s_stream_waits++;
        s_stream_wait_ns += gl_now_ns() - t0;
        if (r == GL_TIMEOUT_EXPIRED || r == GL_WAIT_FAILED) {
            sysLogPrintf(LOG_WARNING, "GL: vertex ring fence wait gave up (0x%x); continuing", (unsigned)r);
        }
    }
    glDeleteSync(f);
    s_stream_fence[sec] = NULL;
}

/* Close the current section behind a fence and enter the next one. */
static void gfx_opengl_stream_advance(void) {
    if (s_stream_off > s_stream_peak) {
        s_stream_peak = s_stream_off;
    }
    s_stream_fence[s_stream_sec] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    s_stream_sec = (s_stream_sec + 1) % STREAM_SECTIONS;
    s_stream_off = 0;
    s_stream_advances++;
    gfx_opengl_stream_wait(s_stream_sec);
}

static float* gfx_opengl_stream_begin(size_t max_floats) {
    if (!s_stream_active || s_stream_map == NULL || s_cur_stride == 0) {
        return NULL;
    }
    const size_t need = max_floats * sizeof(float);
    if (need + s_cur_stride > s_stream_sec_size) {
        return NULL;
    }
    const size_t base = (size_t)s_stream_sec * s_stream_sec_size;
    /* glDrawArrays' `first` counts vertices from the buffer start, so a batch
     * starts on a multiple of the stride in buffer coordinates. */
    size_t abs = (base + s_stream_off + s_cur_stride - 1) / s_cur_stride * s_cur_stride;
    if (abs - base + need > s_stream_sec_size) {
        gfx_opengl_stream_advance();
        const size_t nbase = (size_t)s_stream_sec * s_stream_sec_size;
        abs = (nbase + s_cur_stride - 1) / s_cur_stride * s_cur_stride;
        s_stream_off = abs - nbase;
    } else {
        s_stream_off = abs - base;
    }
    s_stream_batch_abs = abs;
    s_stream_batch_ptr = (float*)(s_stream_map + abs);
    return s_stream_batch_ptr;
}


static void gfx_opengl_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    // printf("flushing %d tris\n", buf_vbo_num_tris);
    const bool perf = gl_perf_on();
    const size_t bytes = sizeof(float) * buf_vbo_len;
    uint64_t t0 = 0, t1 = 0;
    if (perf) {
        s_gld_draws++;
        s_gld_bytes += bytes;
    }
    if (s_stream_active) {
        if (s_stream_batch_ptr == NULL || buf_vbo != s_stream_batch_ptr) {
            /* The caller filled its own buffer (stream_begin said no): copy it in. */
            s_stream_fallbacks++;
            if (perf) t0 = gl_now_ns();
            float* dst = gfx_opengl_stream_begin(buf_vbo_len);
            if (dst == NULL) {
                sysLogPrintf(LOG_WARNING, "GL: vertex ring cannot hold a %u byte batch; dropped", (unsigned)bytes);
                return;
            }
            memcpy(dst, buf_vbo, bytes);
            if (perf) s_gld_copy_ns += gl_now_ns() - t0;
        }
        if (perf) t0 = gl_now_ns();
        glDrawArrays(GL_TRIANGLES, (GLint)(s_stream_batch_abs / s_cur_stride), 3 * buf_vbo_num_tris);
        if (perf) s_gld_draw_ns += gl_now_ns() - t0;
        s_stream_off += bytes;
        s_stream_batch_ptr = NULL;
        return;
    }
#ifdef __SWITCH__
    if (s_vbo_ring && s_vbo_stride > 0) {
        size_t off = (s_vbo_ring_off + s_vbo_stride - 1) / s_vbo_stride * s_vbo_stride;
        GLbitfield access = GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_RANGE_BIT;
        if (off + bytes > VBO_RING_SIZE) {
            off = 0;
            access = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT;   /* orphan: fresh storage */
        }
        if (perf) t0 = gl_now_ns();
        void* dst = glMapBufferRange(GL_ARRAY_BUFFER, off, bytes, access);
        if (perf) { t1 = gl_now_ns(); s_gld_map_ns += t1 - t0; }
        if (dst != NULL) {
            memcpy(dst, buf_vbo, bytes);
            if (perf) { t0 = gl_now_ns(); s_gld_copy_ns += t0 - t1; }
            glUnmapBuffer(GL_ARRAY_BUFFER);
            if (perf) { t1 = gl_now_ns(); s_gld_unmap_ns += t1 - t0; }
            glDrawArrays(GL_TRIANGLES, (GLint)(off / s_vbo_stride), 3 * buf_vbo_num_tris);
            if (perf) s_gld_draw_ns += gl_now_ns() - t1;
            s_vbo_ring_off = off + bytes;
            return;
        }
        /* Mapping failed: per-batch uploads from now on. */
        sysLogPrintf(LOG_WARNING, "GL: vertex ring map failed (0x%x); per-batch uploads", glGetError());
        s_vbo_ring = 0;
    }
#endif
    if (perf) t0 = gl_now_ns();
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * buf_vbo_len, buf_vbo, GL_STREAM_DRAW);
    if (perf) { t1 = gl_now_ns(); s_gld_copy_ns += t1 - t0; }   /* glBufferData = upload, counted as copy */
    glDrawArrays(GL_TRIANGLES, 0, 3 * buf_vbo_num_tris);
    if (perf) s_gld_draw_ns += gl_now_ns() - t1;
}

/* D965 probe lines, called by gfx_pc every 300 frames with the frame count. */
extern "C" void gfx_opengl_perf_report(double frames) {
    if (!gl_perf_on() || frames <= 0) {
        return;
    }
    const double n = frames, ms = 1.0e6;
    fprintf(stderr,
            "PERFSTAT gldraw: draws=%.0f/f map=%.2fms copy=%.2fms unmap=%.2fms draw=%.2fms attrib=%.2fms (%.0f shader changes/f) bytes=%.0f/f\n",
            s_gld_draws / n, s_gld_map_ns / ms / n, s_gld_copy_ns / ms / n, s_gld_unmap_ns / ms / n, s_gld_draw_ns / ms / n,
            s_gld_attrib_ns / ms / n, s_gld_shader_changes / n, s_gld_bytes / n);
    if (s_stream_avail) {
        fprintf(stderr,
                "PERFSTAT glstream: active=%d waits=%llu (%.2fms total) section-advances=%llu copy-fallbacks=%llu peak-section-use=%uKiB of %uKiB\n",
                s_stream_active ? 1 : 0, (unsigned long long)s_stream_waits, s_stream_wait_ns / ms,
                (unsigned long long)s_stream_advances, (unsigned long long)s_stream_fallbacks,
                (unsigned)(s_stream_peak >> 10), (unsigned)(s_stream_sec_size >> 10));
    }
    s_gld_map_ns = s_gld_copy_ns = s_gld_unmap_ns = s_gld_draw_ns = s_gld_attrib_ns = 0;
    s_gld_draws = s_gld_shader_changes = s_gld_bytes = 0;
    s_stream_waits = s_stream_wait_ns = s_stream_advances = s_stream_fallbacks = 0;
    s_stream_peak = 0;
}

/* D965 0a: one block at init with what the driver offers the renderer. */
static bool gl_has_ext(const char* name) {
    if (glGetStringi != NULL && GLVersion.major >= 3) {
        GLint n = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (GLint i = 0; i < n; i++) {
            const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
            if (e != NULL && strcmp(e, name) == 0) {
                return true;
            }
        }
        return false;
    }
    const char* all = (const char*)glGetString(GL_EXTENSIONS);
    if (all == NULL) {
        return false;
    }
    const size_t len = strlen(name);
    for (const char* p = all; (p = strstr(p, name)) != NULL; p += len) {
        if ((p == all || p[-1] == ' ') && (p[len] == ' ' || p[len] == '\0')) {
            return true;
        }
    }
    return false;
}

static const char* gl_cap_str(const char* ext, int core_major, int core_minor) {
    if (gl_has_ext(ext)) {
        return "yes (extension)";
    }
    if (!gl_es && (GLVersion.major > core_major || (GLVersion.major == core_major && GLVersion.minor >= core_minor))) {
        return "yes (core)";
    }
    return "no";
}

static void gfx_opengl_log_caps(void) {
    const char* version = (const char*)glGetString(GL_VERSION);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    GLint layers = 0, units = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &layers);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
    while (glGetError() != GL_NO_ERROR) {}
    const char* bs = gl_cap_str("GL_ARB_buffer_storage", 4, 4);
    const char* bl = gl_cap_str("GL_ARB_bindless_texture", 99, 0);   /* never core */
    const char* md = gl_cap_str("GL_ARB_multi_draw_indirect", 4, 3);
    const char* sd = gl_cap_str("GL_ARB_shader_draw_parameters", 4, 6);
    const char* bi = gl_cap_str("GL_ARB_base_instance", 4, 2);
    const char* vb = gl_cap_str("GL_ARB_vertex_attrib_binding", 4, 3);
    sysLogPrintf(LOG_NOTE, "GL caps: %s | %s", version ? version : "?", renderer ? renderer : "?");
    sysLogPrintf(LOG_NOTE,
                 "GL caps: buffer_storage=%s bindless_texture=%s multi_draw_indirect=%s shader_draw_parameters=%s base_instance=%s vertex_attrib_binding=%s",
                 bs, bl, md, sd, bi, vb);
    sysLogPrintf(LOG_NOTE, "GL caps: MAX_ARRAY_TEXTURE_LAYERS=%d MAX_TEXTURE_IMAGE_UNITS=%d", (int)layers, (int)units);
    if (getenv("GE_PERFSTAT") != NULL) {
        fprintf(stderr, "PERFSTAT glcaps: %s | %s | buffer_storage=%s bindless=%s multi_draw_indirect=%s shader_draw_parameters=%s base_instance=%s vertex_attrib_binding=%s | array layers=%d texture units=%d\n",
                version ? version : "?", renderer ? renderer : "?", bs, bl, md, sd, bi, vb, (int)layers, (int)units);
    }
}

/* D965 1a/1c: create and map the ring when GE_GLSTREAM asks for it (default on
 * on the Switch); fall back to the old path, logging once, if the driver cannot. */

static void gfx_opengl_stream_init(void) {
    const char* e = getenv("GE_GLSTREAM");
#ifdef __SWITCH__
    s_stream_wanted = e == NULL || e[0] != '0';   /* D966: on by default on the console (GE_GLSTREAM=0 turns it off) */
#else
    s_stream_wanted = e != NULL && e[0] != '0';
#endif
    if (!s_stream_wanted) {
        return;
    }
    const char* mb = getenv("GE_GLSTREAM_MB");
    if (mb != NULL && atoi(mb) > 0) {
        s_stream_sec_size = (size_t)atoi(mb) << 20;
    }
    if (s_stream_sec_size < (1u << 20)) {
        s_stream_sec_size = 1u << 20;
    }
    ge_glBufferStorage_t bufferStorage = NULL;
    if (gl_has_ext("GL_ARB_buffer_storage") || (!gl_es && (GLVersion.major > 4 || (GLVersion.major == 4 && GLVersion.minor >= 4)))) {
        bufferStorage = (ge_glBufferStorage_t)SDL_GL_GetProcAddress("glBufferStorage");
    }
    if (bufferStorage == NULL || glMapBufferRange == NULL || glFenceSync == NULL || glClientWaitSync == NULL) {
        sysLogPrintf(LOG_NOTE, "GL: GE_GLSTREAM: ARB_buffer_storage unavailable, using the per-batch vertex upload");
        s_stream_wanted = false;
        return;
    }
    const size_t total = s_stream_sec_size * STREAM_SECTIONS;
    const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    glGenBuffers(1, &s_stream_buf);
    glBindBuffer(GL_ARRAY_BUFFER, s_stream_buf);
    while (glGetError() != GL_NO_ERROR) {}
    bufferStorage(GL_ARRAY_BUFFER, (GLsizeiptr)total, NULL, flags);
    void* map = glGetError() == GL_NO_ERROR ? glMapBufferRange(GL_ARRAY_BUFFER, 0, (GLsizeiptr)total, flags) : NULL;
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);
    if (map == NULL || glGetError() != GL_NO_ERROR) {
        sysLogPrintf(LOG_WARNING, "GL: GE_GLSTREAM: persistent map of %u MiB failed, using the per-batch vertex upload",
                     (unsigned)(total >> 20));
        glDeleteBuffers(1, &s_stream_buf);
        s_stream_buf = 0;
        s_stream_wanted = false;
        return;
    }
    s_stream_map = (uint8_t*)map;
    s_stream_avail = true;
    s_stream_active = true;
    sysLogPrintf(LOG_NOTE, "GL: vertex stream: persistent coherent ring, %d x %u KiB, ACTIVE; layout VAOs", STREAM_SECTIONS,
                 (unsigned)(s_stream_sec_size >> 10));
}


typedef void (APIENTRY *DEBUGPROC)(GLenum source,
    GLenum type,
    GLuint id,
    GLenum severity,
    GLsizei length,
    const GLchar *message,
    const void *userParam);

static void APIENTRY gl_debug(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *msg, const void *p) {
    sysLogPrintf(LOG_WARNING, "GL: (%05x) %s", id, msg);
}

static void gfx_opengl_enable_debug(void) {
    if (GLAD_GL_KHR_debug) {
        glEnable(GL_DEBUG_OUTPUT);
    }
    if (glDebugMessageControl != NULL) {
        // enable everything except some specific spam messages
        const GLuint disable[] = {
            0x20061, /* "Framebuffer detailed info" */
            0x20071  /* "Buffer detailed info" */
        };
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
        glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE, 2, disable, GL_FALSE);
    }
    if (glDebugMessageCallback != NULL) {
        glDebugMessageCallback(gl_debug, NULL);
    }
}

static bool gfx_opengl_supports_framebuffers(void) {
    if (GLVersion.major > 2) {
        // GL3.0+ supports everything we need, but we'll still check it for sanity
        return (glad_glFramebufferRenderbuffer && glad_glBlitFramebuffer && glad_glRenderbufferStorageMultisample);
    }
    if (GLAD_GL_ARB_framebuffer_object) {
        // some implementations might be missing these
        return (glad_glBlitFramebuffer && glad_glRenderbufferStorageMultisample);
    }
    if (GLAD_GL_EXT_framebuffer_object && GLAD_GL_EXT_framebuffer_blit && GLAD_GL_EXT_framebuffer_multisample) {
        // sanity check
        return (glad_glFramebufferRenderbuffer && glad_glBlitFramebuffer && glad_glRenderbufferStorageMultisample);
    }
    // nothing
    return false;
}

static bool gfx_opengl_supports_shaders(void) {
    if (GLVersion.major > 2) {
        // should support GLSL130+
        return true;
    }

    // check supported GLSL version
    const char *ver = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    if (ver) {
        int maj = 0, min = 0;
        sscanf(ver, "%d.%d", &maj, &min);
        if (maj > 1 || (maj == 1 && min > 20)) {
            // above 120, should be fine
            return true;
        }
    }

    // check for extension that adds textureSize
    return GLAD_GL_EXT_gpu_shader4;
}

/* D482: GL_RENDERER as read at init, for video.c's low-end default check.
 * A copy, never the driver's pointer; "" when the driver gave nothing. */
static char gl_renderer_str[256] = "";

extern "C" const char *gfx_opengl_renderer_string(void) {
    return gl_renderer_str;
}

static void gfx_opengl_log_info(void) {
    const char *version = (const char *)glGetString(GL_VERSION);
    const char *vendor = (const char *)glGetString(GL_VENDOR);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    snprintf(gl_renderer_str, sizeof(gl_renderer_str), "%s", renderer ? renderer : "");
    const char *glsl_version = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    sysLogPrintf(LOG_NOTE, "GL: version: %s", version ? version : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: vendor: %s", vendor ? vendor : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: renderer: %s", renderer ? renderer : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: GLSL version: %s", glsl_version ? glsl_version : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: ARB_framebuffer_object: %s", gfx_opengl_supports_framebuffers() ? "yes" : "no");
    sysLogPrintf(LOG_NOTE, "GL: ARB_depth_clamp: %s", GLAD_GL_ARB_depth_clamp ? "yes" : "no");
    sysLogPrintf(LOG_NOTE, "GL: ARB_texture_mirror_clamp_to_edge: %s", GLAD_GL_ARB_texture_mirror_clamp_to_edge ? "yes" : "no");
}

static void *gl_load_proc(const char *name) {
    void *ret = SDL_GL_GetProcAddress(name);
    if (ret) {
        return ret;
    }

    // try with postfixes
    static const char *post[] = { "ARB", "EXT" };
    char tmp[256] = { 0 };
    for (size_t i = 0; i < sizeof(post) / sizeof(*post); ++i) {
        snprintf(tmp, sizeof(tmp), "%s%s", name, post[i]);
        ret = SDL_GL_GetProcAddress(tmp);
        if (ret) {
            return ret;
        }
    }

    sysLogPrintf(LOG_ERROR, "GL: could not find function: %s", name);

    return NULL;
}

static void gfx_opengl_init_extensions(void) {
    // patch up some extension values and pointers
    if (!GLAD_GL_ARB_depth_clamp) {
        if (GLAD_GL_EXT_depth_clamp || GLAD_GL_NV_depth_clamp) {
            GLAD_GL_ARB_depth_clamp = 1;
        } else if (!gl_es && GLVersion.major >= 3) {
            // GL3.2+ should have depth_clamp as part of the spec, but some devices don't report it for some reason
            GLAD_GL_ARB_depth_clamp = (GLVersion.major > 3 || (GLVersion.major == 3 && GLVersion.minor >= 2));
        }
    }

    if (!GLAD_GL_ARB_texture_mirror_clamp_to_edge) {
        GLAD_GL_ARB_texture_mirror_clamp_to_edge = GLAD_GL_EXT_texture_mirror_clamp_to_edge;
    }

    if (GLVersion.major < 3 && !GLAD_GL_ARB_framebuffer_object) {
        if (GLAD_GL_EXT_framebuffer_object && GLAD_GL_EXT_framebuffer_blit && GLAD_GL_EXT_framebuffer_multisample) {
            // because of the way glad works we'll have to copy the pointers over
            glad_glGenFramebuffers = glad_glGenFramebuffersEXT;
            glad_glGenRenderbuffers = glad_glGenRenderbuffersEXT;
            glad_glDeleteFramebuffers = glad_glDeleteFramebuffersEXT;
            glad_glDeleteRenderbuffers = glad_glDeleteRenderbuffersEXT;
            glad_glBindFramebuffer = glad_glBindFramebufferEXT;
            glad_glBindRenderbuffer = glad_glBindRenderbufferEXT;
            glad_glFramebufferRenderbuffer = glad_glFramebufferRenderbufferEXT;
            glad_glFramebufferTexture2D = glad_glFramebufferTexture2DEXT;
            glad_glRenderbufferStorage = glad_glRenderbufferStorageEXT;
            glad_glRenderbufferStorageMultisample = glad_glRenderbufferStorageMultisampleEXT;
            glad_glBlitFramebuffer = glad_glBlitFramebufferEXT;
        }
    }
}

static void gfx_opengl_init(void) {
    if (!gladLoadGLLoader(gl_load_proc) || glGetString == NULL || glEnable == NULL) {
        sysFatalError("Could not load OpenGL.\nReported SDL error: %s", SDL_GetError());
    }

    // check if we're using ES or core, which have more limited feature sets
    int val = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &val);
    gl_core_profile = (val == SDL_GL_CONTEXT_PROFILE_CORE);
    gl_es = (val == SDL_GL_CONTEXT_PROFILE_ES);

    gfx_opengl_init_extensions();

    {   /* D482: always capture GL_RENDERER (log_info below is --debug-gl only). */
        const char *renderer = (const char *)glGetString(GL_RENDERER);
        snprintf(gl_renderer_str, sizeof(gl_renderer_str), "%s", renderer ? renderer : "");
    }

    if (sysArgCheck("--debug-gl")) {
        gfx_opengl_enable_debug();
        // dump version info as early as possible
        gfx_opengl_log_info();
    }

    if (GLVersion.major < 2 || (GLVersion.major == 2 && GLVersion.minor < 1)) {
        const char *ver = (const char *)glGetString(GL_VERSION);
        sysFatalError("Could not load OpenGL 2.1.\nReported version: %d.%d (%s)",
            GLVersion.major, GLVersion.minor, ver ? ver : "unknown");
    }

    if (!gfx_opengl_supports_shaders()) {
        sysLogPrintf(LOG_WARNING, "GL: GLSL 1.30 may be unsupported");
        // maybe replace this with sysFatalError, though the GLSL compiler will cause that later anyway
    }

    if (!gfx_framebuffers_enabled) {
        sysLogPrintf(LOG_WARNING, "GL: framebuffer effects disabled by user");
    } else if (!gfx_opengl_supports_framebuffers()) {
        sysLogPrintf(LOG_WARNING, "GL: GL_ARB_framebuffer_object unsupported, framebuffer effects disabled");
        gfx_framebuffers_enabled = false;
    }

    if ((GLVersion.major < 4 || GLVersion.minor < 4) && !GLAD_GL_ARB_texture_mirror_clamp_to_edge) {
        // GL_MIRROR_CLAMP_TO_EDGE unsupported
        gl_mirror_clamp = GL_MIRRORED_REPEAT;
    }

    // determine GLSL version
    if (gl_es) {
        // ES has its own numbering scheme, but it should support 300 even on 3.1 and 3.2
        gl_glsl_version = 300;
        snprintf(gl_glsl_version_str, sizeof(gl_glsl_version_str), "%d es", gl_glsl_version);
    } else if (!gl_core_profile) {
        // in compat profile we can just request the lowest possible
        gl_glsl_version = 130;
        snprintf(gl_glsl_version_str, sizeof(gl_glsl_version_str), "%d", gl_glsl_version);
    } else {
        // otherwise we have to pick a specific version
        if (GLVersion.major == 3 && GLVersion.minor == 2) {
            // 3.2core is the earliest core version and it follows the old numbering scheme
            gl_glsl_version = 150;
        } else {
            // 3.3+ follow the new numbering scheme
            gl_glsl_version = GLVersion.major * 100 + GLVersion.minor * 10;
        }
        snprintf(gl_glsl_version_str, sizeof(gl_glsl_version_str), "%d core", gl_glsl_version);
    }
    sysLogPrintf(LOG_NOTE, "GL: using GLSL version %s", gl_glsl_version_str);

    /* D540: the RDP interpolates shade colour affinely in screen space; the
     * GPU default is perspective-correct, which squeezed the sky's horizon
     * fade into a thin band. noperspective needs desktop GLSL 1.30+ (not
     * GLSL ES). GE_SHADEPERSP=1 restores perspective-correct shade (A/B).
     * Fog (D543): per-vertex RSP fog by default, as the N64. Its old
     * light/dark cycling at the player's feet came from GPU near-clipping of
     * behind-camera corners; gfx_sp_tri1 now near-clips on the CPU like the
     * RSP. GE_FOGPIXEL=1 = the D540 exact per-pixel fog (A/B). */
    {
        const char *sp = getenv("GE_SHADEPERSP"), *fp = getenv("GE_FOGPIXEL");
        gfx_rdp_affine = (!gl_es && gl_glsl_version >= 130 && !(sp && sp[0] == '1')) ? 1 : 0;
        gfx_fog_vertex = (fp && fp[0] == '1') ? 0 : 1;   /* D543: per-vertex RSP fog by default */
    }
    sysLogPrintf(LOG_NOTE, "GL: shade %s, fog %s", gfx_rdp_affine ? "affine (RDP)" : "perspective",
                 gfx_fog_vertex ? "per-vertex" : "per-pixel");

    glGenBuffers(1, &opengl_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);
#ifdef __SWITCH__
    {
        const char* e = getenv("GE_VBORING");
        s_vbo_ring = !(e && e[0] == '0') && glMapBufferRange != NULL;
        if (s_vbo_ring) {
            glBufferData(GL_ARRAY_BUFFER, VBO_RING_SIZE, NULL, GL_STREAM_DRAW);
        }
        sysLogPrintf(LOG_NOTE, "GL: vertex upload %s",
                     s_vbo_ring ? "ring (8 MiB, unsynchronized maps)" : "per batch (glBufferData)");
    }
#endif

    if (gl_core_profile || gl_es) {
        // warn user that funny things can happen
        sysLogPrintf(LOG_WARNING, "GL: using core profile or ES, watch out for errors");
        // core/es will explode if we don't use a VAO for our VBO
        glGenVertexArrays(1, &opengl_vao);
        glBindVertexArray(opengl_vao);
    }

    gfx_opengl_log_caps();      /* D965 0a */
    gfx_opengl_stream_init();   /* D965 1a/1c (GE_GLSTREAM) */

    if (GLAD_GL_ARB_depth_clamp) {
        glEnable(GL_DEPTH_CLAMP);
    }
    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    framebuffers.resize(1); // for the default screen buffer
}

static void gfx_opengl_on_resize(void) {
}

/* #92 perf probe (GE_PERFSTAT=1): GPU time per frame via GL_TIME_ELAPSED,
 * double-buffered so reading never stalls the pipeline. Skipped when the
 * driver lacks timer queries. Tells a low-end reporter's log whether frames
 * are GPU-bound (render/MSAA/filtering cost) or CPU-bound. */
static int s_gpuq_on = -1;
static GLuint s_gpuq[2];
static int s_gpuq_idx = 0, s_gpuq_valid[2] = { 0, 0 };
static double s_gpuq_ms = 0.0;
static int s_gpuq_n = 0, s_gpuq_frames = 0;

/* D578: an extra interpolation pass of the same game frame must reuse its
 * noise seed (frame_count advances once per game frame, as at 60 fps). */
extern "C" void gfx_opengl_frame_count_rewind(void) {
    frame_count--;
}

static void gfx_opengl_start_frame(void) {
    frame_count++;
    if (s_gpuq_on < 0) {
        s_gpuq_on = getenv("GE_PERFSTAT") != NULL && glGenQueries != NULL &&
                    glGetQueryObjectui64v != NULL && glBeginQuery != NULL;
        if (s_gpuq_on) {
            glGenQueries(2, s_gpuq);
            fprintf(stderr, "PERFSTAT gpu timer on: %s | %s\n",
                    (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
        }
    }
    if (s_gpuq_on == 1) {
        if (s_gpuq_valid[s_gpuq_idx]) {
            GLuint avail = 0;
            glGetQueryObjectuiv(s_gpuq[s_gpuq_idx], GL_QUERY_RESULT_AVAILABLE, &avail);
            if (avail) {
                GLuint64 ns = 0;
                glGetQueryObjectui64v(s_gpuq[s_gpuq_idx], GL_QUERY_RESULT, &ns);
                s_gpuq_ms += (double)ns / 1.0e6;
                s_gpuq_n++;
            }
        }
        glBeginQuery(GL_TIME_ELAPSED, s_gpuq[s_gpuq_idx]);
    }
}

static void gfx_opengl_end_frame(void) {
    glFlush();
    if (s_gpuq_on == 1) {
        glEndQuery(GL_TIME_ELAPSED);
        s_gpuq_valid[s_gpuq_idx] = 1;
        s_gpuq_idx ^= 1;
        if (++s_gpuq_frames % 300 == 0 && s_gpuq_n > 0) {
            fprintf(stderr, "PERFSTAT gpu=%.2fms/frame (avg over %d frames)\n", s_gpuq_ms / s_gpuq_n, s_gpuq_n);
            s_gpuq_ms = 0.0;
            s_gpuq_n = 0;
        }
    }
}

/* Frame capture (dev tool, env GE_PCDUMP) — see gfx_opengl.h. TEMP D70: read
 * the default framebuffer (the window back buffer; the scene renders there
 * directly at 1:1 window size) and write a P6 PPM. */
extern "C" bool gfx_opengl_pcdump_enabled(void) {
    static int cached = -1;
    if (cached < 0) cached = getenv("GE_PCDUMP") != NULL;
    return cached != 0;
}

extern "C" bool gfx_opengl_dump_bound_fbo(uint32_t width, uint32_t height, const char* path) {
    size_t n = (size_t)width * height * 3;
    uint8_t* pixels = (uint8_t*)malloc(n);
    if (!pixels) return false;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glFinish();
    glReadPixels(0, 0, (GLsizei)width, (GLsizei)height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    {
        GLenum e = glGetError();
        if (e) sysLogPrintf(LOG_WARNING, "dump_bound_fbo: glReadPixels err 0x%x (%ux%u)", (unsigned)e, width, height);
    }
    FILE* f = fopen(path, "wb");
    if (!f) { free(pixels); return false; }
    fprintf(f, "P6\n%u %u\n255\n", width, height);
    /* D168: glReadPixels() returns rows bottom-to-top (GL framebuffer origin is
     * bottom-left); PPM P6 is top-row-first. Emit rows in reverse so the capture
     * is the right way up. Previously written unreversed -> every GE_PCDUMP was
     * vertically flipped (the source of the bogus D114/D116 "text X-mirror"). */
    {
        size_t stride = (size_t)width * 3;
        for (uint32_t row = 0; row < height; row++)
            fwrite(pixels + (size_t)(height - 1 - row) * stride, 1, stride, f);
    }
    fclose(f);
    free(pixels);
    return true;
}

static void gfx_opengl_finish_render(void) {
}

/* D578: present queue for frame interpolation (gfx_pc.cpp). Each slot is a
 * plain RGBA8 colour FBO at window size; the default framebuffer is single-
 * sampled (MSAA lives in the game framebuffer), so a nearest blit each way is
 * an exact copy. Bindings are put back the way copy_framebuffer leaves them. */
static GLuint s_islot_fbo[GFX_INTERP_SLOTS], s_islot_tex[GFX_INTERP_SLOTS];
static uint32_t s_islot_w, s_islot_h;
static uint32_t s_islot_tw[GFX_INTERP_SLOTS], s_islot_th[GFX_INTERP_SLOTS];   /* D583: per-slot storage size */

static void gfx_opengl_interp_restore_binding(void) {
    glBindFramebuffer(GL_FRAMEBUFFER, current_framebuffer == 0 ? 0 : framebuffers[current_framebuffer].fbo);
    glReadBuffer(GL_BACK);
    glEnable(GL_SCISSOR_TEST);
}

extern "C" void gfx_opengl_interp_store(int slot, uint32_t width, uint32_t height) {
    if (slot < 0 || slot >= GFX_INTERP_SLOTS || width == 0 || height == 0) {
        return;
    }
    if (s_islot_fbo[0] == 0) {
        glGenFramebuffers(GFX_INTERP_SLOTS, s_islot_fbo);
        glGenTextures(GFX_INTERP_SLOTS, s_islot_tex);
    }
    if (width != s_islot_tw[slot] || height != s_islot_th[slot]) {
        /* D583: each slot is sized on its first store at a new window size, so
         * slots a mode never uses (5-8 below 240 Hz) never take VRAM. */
        GLint prevTex = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);   /* fast3d caches its bindings */
        glBindTexture(GL_TEXTURE_2D, s_islot_tex[slot]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)width, (GLsizei)height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, s_islot_fbo[slot]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_islot_tex[slot], 0);
        glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
        s_islot_tw[slot] = width;
        s_islot_th[slot] = height;
    }
    s_islot_w = width;
    s_islot_h = height;
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_islot_fbo[slot]);
    glBlitFramebuffer(0, 0, (GLint)width, (GLint)height, 0, 0, (GLint)width, (GLint)height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gfx_opengl_interp_restore_binding();
}

/* D578: block until the GL queue (and so a VSync'd swap) completes, so the
 * render worker's present clock learns the display's real refresh timing. */
extern "C" void gfx_opengl_interp_sync(void) {
    glFinish();
}

extern "C" void gfx_opengl_interp_show(int slot) {
    if (slot < 0 || slot >= GFX_INTERP_SLOTS || s_islot_fbo[slot] == 0 || s_islot_tw[slot] == 0) {
        return;
    }
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, s_islot_fbo[slot]);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    /* The slot's own size: one stored before a window resize can still be pending (D583 ring). */
    glBlitFramebuffer(0, 0, (GLint)s_islot_tw[slot], (GLint)s_islot_th[slot], 0, 0, (GLint)s_islot_tw[slot], (GLint)s_islot_th[slot],
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gfx_opengl_interp_restore_binding();
}

static int gfx_opengl_create_framebuffer() {
    size_t i = framebuffers.size();
    framebuffers.resize(i + 1);

    GLuint clrbuf;
    glGenTextures(1, &clrbuf);
    glBindTexture(GL_TEXTURE_2D, clrbuf);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    framebuffers[i].clrbuf = clrbuf;

    if (!gfx_framebuffers_enabled) {
        return i;
    }

    GLuint clrbuf_msaa;
    glGenRenderbuffers(1, &clrbuf_msaa);
    framebuffers[i].clrbuf_msaa = clrbuf_msaa;

    GLuint rbo;
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 1, 1);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    framebuffers[i].rbo = rbo;

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    framebuffers[i].fbo = fbo;

    return i;
}

static void gfx_opengl_update_framebuffer_parameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                                     bool opengl_invert_y, bool render_target, bool has_depth_buffer,
                                                     bool can_extract_depth) {
    Framebuffer& fb = framebuffers[fb_id];

    width = max(width, 1U);
    height = max(height, 1U);

    /* macOS GL 4.1 core: asking for more samples than GL_MAX_SAMPLES (e.g. 8x)
     * makes the multisample renderbuffers incomplete and the screen goes dark
     * (glReadPixels 0x506). Clamp to what the driver reports. */
    if (msaa_level > 1) {
        static GLint maxSamples = -1;
        if (maxSamples < 0) {
            glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
            if (maxSamples < 1) maxSamples = 1;
        }
        if ((GLint)msaa_level > maxSamples) {
            msaa_level = (uint32_t)maxSamples;
        }
    }

    if (gfx_framebuffers_enabled) {
        glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);

        if (fb_id != 0) {
            if (fb.width != width || fb.height != height || fb.msaa_level != msaa_level) {
                if (msaa_level <= 1) {
                    glBindTexture(GL_TEXTURE_2D, fb.clrbuf);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
                    glBindTexture(GL_TEXTURE_2D, 0);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb.clrbuf, 0);
                } else {
                    glBindRenderbuffer(GL_RENDERBUFFER, fb.clrbuf_msaa);
                    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_level, GL_RGB8, width, height);
                    glBindRenderbuffer(GL_RENDERBUFFER, 0);
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fb.clrbuf_msaa);
                }
            }

            if (has_depth_buffer &&
                (fb.width != width || fb.height != height || fb.msaa_level != msaa_level || !fb.has_depth_buffer)) {
                glBindRenderbuffer(GL_RENDERBUFFER, fb.rbo);
                if (msaa_level <= 1) {
                    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
                } else {
                    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_level, GL_DEPTH24_STENCIL8, width, height);
                }
                glBindRenderbuffer(GL_RENDERBUFFER, 0);
            }

            if (!fb.has_depth_buffer && has_depth_buffer) {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb.rbo);
            } else if (fb.has_depth_buffer && !has_depth_buffer) {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
            }
        }
    } else {
        has_depth_buffer = false;
    }

    fb.width = width;
    fb.height = height;
    fb.has_depth_buffer = has_depth_buffer;
    fb.msaa_level = msaa_level;
    fb.invert_y = opengl_invert_y;
}

bool gfx_opengl_start_draw_to_framebuffer(int fb_id, float noise_scale) {
    if (gfx_framebuffers_enabled && fb_id < (int)framebuffers.size()) {
        Framebuffer& fb = framebuffers[fb_id];
        if (noise_scale != 0.0f) {
            current_noise_scale = 1.0f / noise_scale;
        }
        /* fb_id 0 is the on-screen target: bind the window's default
         * framebuffer (GL name 0), not framebuffers[0].fbo (a generated but
         * attachment-less FBO -> incomplete -> nothing renders on a strict
         * GL implementation like Mesa/llvmpipe). */
        glBindFramebuffer(GL_FRAMEBUFFER, fb_id == 0 ? 0 : fb.fbo);
        current_framebuffer = fb_id;
        return true;
    } else {
        return false;
    }
}

void gfx_opengl_clear_framebuffer(bool clear_color, bool clear_depth) {
    glDisable(GL_SCISSOR_TEST);
    
    GLbitfield mask = 0;
    if (clear_color) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (clear_depth) {
        glDepthMask(GL_TRUE);
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    glClear(mask);
    if (clear_depth) {
        glDepthMask(current_depth_mask ? GL_TRUE : GL_FALSE);
    }

    glEnable(GL_SCISSOR_TEST);
}

/* D579: scissored clear of one region of the bound target. Same
 * bottom-up pixel coordinates as gfx_opengl_set_scissor. Mirrors the
 * depth-mask save/restore of gfx_opengl_clear_framebuffer. */
void gfx_opengl_clear_region(bool clear_color, bool clear_depth, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;

    GLbitfield mask = 0;
    if (clear_color) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (clear_depth) {
        glDepthMask(GL_TRUE);
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    glScissor(x, y, w, h);
    glEnable(GL_SCISSOR_TEST);
    glClear(mask);
    if (clear_depth) {
        glDepthMask(current_depth_mask ? GL_TRUE : GL_FALSE);
    }
}

#ifdef __SWITCH__
/* project-dam: FXAA as a final pass. Draws the single-sample colour texture of
 * framebuffer src_fb_id to the default framebuffer through an edge-directed
 * blur (Timothy Lottes' FXAA idea, written from the published algorithm: find
 * the local luma gradient, blur along the edge, keep the result only if it
 * stays inside the neighbourhood luma range). One fullscreen triangle. */
static GLuint s_fxaa_prog, s_fxaa_vao;
static GLint s_fxaa_loc_tex = -1, s_fxaa_loc_inv = -1;

extern "C" void gfx_opengl_fxaa_pass(int src_fb_id) {
    if (!gfx_framebuffers_enabled || src_fb_id <= 0 || src_fb_id >= (int)framebuffers.size()) {
        return;
    }
    Framebuffer& src = framebuffers[src_fb_id];
    if (!s_fxaa_prog) {
        const char* vs = R"GLSL(#version 330 core
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";
        const char* fs = R"GLSL(#version 330 core
uniform sampler2D uTex;
uniform vec2 uInvRes;
out vec4 o;
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
void main() {
    vec2 uv = gl_FragCoord.xy * uInvRes;
    vec3 m  = texture(uTex, uv).rgb;
    vec3 nw = texture(uTex, uv + vec2(-1.0,  1.0) * uInvRes).rgb;
    vec3 ne = texture(uTex, uv + vec2( 1.0,  1.0) * uInvRes).rgb;
    vec3 sw = texture(uTex, uv + vec2(-1.0, -1.0) * uInvRes).rgb;
    vec3 se = texture(uTex, uv + vec2( 1.0, -1.0) * uInvRes).rgb;
    float lm = luma(m), lnw = luma(nw), lne = luma(ne), lsw = luma(sw), lse = luma(se);
    float lmin = min(lm, min(min(lnw, lne), min(lsw, lse)));
    float lmax = max(lm, max(max(lnw, lne), max(lsw, lse)));
    /* flat or low-contrast area: leave the texture alone (FXAA's edge threshold) */
    if (lmax - lmin < max(0.0625, lmax * 0.125)) {
        o = vec4(m, 1.0);
        return;
    }
    /* edge tangent (perpendicular to the luma gradient), y up */
    vec2 dir = vec2(-((lnw + lne) - (lsw + lse)), -((lnw + lsw) - (lne + lse)));
    float reduce = max((lnw + lne + lsw + lse) * (0.25 / 8.0), 1.0 / 128.0);
    float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcp, vec2(-8.0), vec2(8.0)) * uInvRes;
    vec3 a = 0.5 * (texture(uTex, uv + dir * (1.0 / 3.0 - 0.5)).rgb + texture(uTex, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(uTex, uv - dir * 0.5).rgb + texture(uTex, uv + dir * 0.5).rgb);
    float lb = luma(b);
    o = vec4((lb < lmin || lb > lmax) ? a : b, 1.0);
}
)GLSL";
        GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(v, 1, &vs, NULL);
        glCompileShader(v);
        glShaderSource(f, 1, &fs, NULL);
        glCompileShader(f);
        s_fxaa_prog = glCreateProgram();
        glAttachShader(s_fxaa_prog, v);
        glAttachShader(s_fxaa_prog, f);
        glLinkProgram(s_fxaa_prog);
        GLint ok = 0;
        char log[512] = "";
        glGetProgramiv(s_fxaa_prog, GL_LINK_STATUS, &ok);
        glGetProgramInfoLog(s_fxaa_prog, sizeof log, NULL, log);
        sysLogPrintf(ok ? LOG_NOTE : LOG_ERROR, "GL: FXAA program link=%d %s", ok, log);
        glDeleteShader(v);
        glDeleteShader(f);
        s_fxaa_loc_tex = glGetUniformLocation(s_fxaa_prog, "uTex");
        s_fxaa_loc_inv = glGetUniformLocation(s_fxaa_prog, "uInvRes");
        glGenVertexArrays(1, &s_fxaa_vao);
    }

    GLint prev_prog = 0, prev_vao = 0, prev_unit = 0, prev_vp[4];
    const GLboolean prev_depth = glIsEnabled(GL_DEPTH_TEST), prev_blend = glIsEnabled(GL_BLEND),
                    prev_cull = glIsEnabled(GL_CULL_FACE), prev_scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prev_cm[4];
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_unit);
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    glGetBooleanv(GL_COLOR_WRITEMASK, prev_cm);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, (GLsizei)src.width, (GLsizei)src.height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(s_fxaa_prog);
    glUniform1i(s_fxaa_loc_tex, 7);
    glUniform2f(s_fxaa_loc_inv, 1.0f / (float)src.width, 1.0f / (float)src.height);
    glActiveTexture(GL_TEXTURE0 + 7);   /* a unit the game's tile sampling never uses */
    glBindTexture(GL_TEXTURE_2D, src.clrbuf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindVertexArray(s_fxaa_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindVertexArray((GLuint)prev_vao);
    glUseProgram((GLuint)prev_prog);
    glActiveTexture((GLenum)prev_unit);
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    glColorMask(prev_cm[0], prev_cm[1], prev_cm[2], prev_cm[3]);
    if (prev_depth) glEnable(GL_DEPTH_TEST);
    if (prev_blend) glEnable(GL_BLEND);
    if (prev_cull) glEnable(GL_CULL_FACE);
    if (prev_scissor) glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, current_framebuffer == 0 ? 0 : framebuffers[current_framebuffer].fbo);
}
#endif

/* GE+ sky (Video.SkyPlus): see port/include/skyplus.h. The decoder stages a
 * copy of the game's parameter block with the viewport; the replay calls the
 * pass at the same point of the frame, which draws the player's viewport in
 * one fullscreen triangle (no depth test or write, as GE's sky). */
struct SkyPlusDraw {
    PortSkyPlus p;
    int vp[4];
    float aspA, aspB;
};
#define SKYPLUS_RING 64   /* > skies per frame (4 players) x frames in flight */
static SkyPlusDraw s_skyplus_ring[SKYPLUS_RING];
static unsigned s_skyplus_next;
static GLuint s_skyplus_prog, s_skyplus_vao, s_skyplus_tex;
static int s_skyplus_tex_gen = -1;
extern "C" { volatile int gfx_skyplus_failed = 0; }
enum { SPL_VP, SPL_ASP, SPL_C0, SPL_C1, SPL_C2, SPL_C3, SPL_EYE, SPL_CLOUDY, SPL_SKY, SPL_CLOUD, SPL_OFS,
       SPL_GAIN, SPL_TEXSCALE, SPL_SUNDIR, SPL_SUNCOL, SPL_SUNGLOW, SPL_MOONDIR, SPL_NIGHT, SPL_TEX, SPL_COUNT };
static GLint s_sp_loc[SPL_COUNT];

extern "C" int gfx_opengl_skyplus_stage(const PortSkyPlus* p, const int vp[4], float aspA, float aspB) {
    const int slot = (int)(s_skyplus_next++ % SKYPLUS_RING);
    SkyPlusDraw& d = s_skyplus_ring[slot];
    d.p = *p;
    for (int i = 0; i < 4; i++) {
        d.vp[i] = vp[i];
    }
    d.aspA = aspA;
    d.aspB = aspB;
    return slot;
}

static bool skyplus_build(void) {
    static const char* vs_body = R"GLSL(
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";
    static const char* fs_body = R"GLSL(
uniform vec4 uVP;        /* GL viewport */
uniform vec2 uAsp;       /* x_out = uAsp.x * x_game + uAsp.y (clip x) */
uniform vec3 uC0, uC1, uC2, uC3;   /* image-plane rays TL TR BL BR, world */
uniform vec3 uEye;
uniform float uCloudY;
uniform vec3 uSky, uCloud;
uniform vec2 uOfs[3];
uniform float uGain;
uniform float uTexScale;
#define PLANET_R 300000.0
uniform vec3 uSunDir, uSunCol;
uniform float uSunGlow;
uniform vec3 uMoonDir;
uniform float uNight;
uniform sampler2D uTex;
out vec4 o;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    vec2 f = (gl_FragCoord.xy - uVP.xy) / uVP.zw;          /* 0..1, y up */
    float xg = ((f.x * 2.0 - 1.0) - uAsp.y) / uAsp.x;       /* back to the game's screen x */
    float u = xg * 0.5 + 0.5, v = 1.0 - f.y;                /* v = 0 at the top of the screen */
    vec3 d = mix(mix(uC0, uC1, u), mix(uC2, uC3, u), v);
    float hz = length(d.xz) + 1e-4;
    float e = max(d.y / length(d), 0.0);
    vec3 F = uSky;
    if (d.y <= 0.0) { o = vec4(F, 1.0); return; }           /* GE's clear colour */

    /* 1. sky behind everything: horizon = GE's colour, zenith a little deeper */
    vec3 c = mix(F, F * 0.82, smoothstep(0.0, 0.8, e));
    vec3 nd = normalize(d);
    if (uSunGlow > 0.0) {
        float cs = dot(nd, uSunDir);
        float sunAmt = max(cs, 0.0);
        c += uSunCol * (pow(sunAmt, 5.0) * 0.5 + pow(sunAmt, 48.0) * 0.7) * uSunGlow;
        if (cs > 0.9995) c = max(c, vec3(1.3));
    }
    float star = 0.0;
    if (uNight > 0.5 && e > 0.03) {
        vec2 cell = floor(vec2(atan(nd.x, nd.z), asin(e)) * 400.0);
        float h = hash(cell);
        if (h > 0.997) star = (h - 0.997) / 0.003 * smoothstep(0.03, 0.3, e) * 0.8;
    }

    /* 2. cloud layers, highest first, over the sky; layer 1 = GE's */
    vec3 ld = uSunGlow > 0.0 ? uSunDir : (uNight > 0.5 ? uMoonDir : vec3(0.0, 1.0, 0.0));
    float fr = 1.0 - min(1.0, 2.0 * d.y / hz) * uGain;
    vec3 shade = F + uCloud * (1.0 - F) * (1.0 - fr);
    float H = uCloudY - uEye.y;
    float cover = 0.0;
    if (H > 0.0) {
        for (int i = 0; i < 3; i++) {
            float kH  = i == 0 ? 1.8  : (i == 1 ? 1.0 : 0.55);
            float kSc = i == 0 ? 1.7  : (i == 1 ? 1.0 : 0.45);
            float kRo = i == 0 ? 0.9  : (i == 1 ? 0.0 : -0.6);
            float kOp = i == 0 ? 0.35 : (i == 1 ? 1.0 : 0.25);
            float kK  = i == 0 ? 0.15 : (i == 1 ? 0.0 : 0.25);
            /* a curved shell (a planet's cloud layer seen from the ground), not a
             * flat plane: overhead it is the plane, toward the horizon it bends
             * down, so the distance stays finite and the texture never shrinks
             * to a moire band */
            float Hl = H * kH;
            float b = PLANET_R * nd.y;
            float dl = -b + sqrt(b * b + 2.0 * PLANET_R * Hl + Hl * Hl);   /* distance to the shell */
            /* GE: S = x * 0.1, T = z * 0.1 + offset, in S10.5 (32 per texel) */
            vec2 w = (uEye.xz + nd.xz * dl) * (0.1 / 32.0) * uTexScale;
            float cr = cos(kRo), sr = sin(kRo);
            vec2 t = vec2(w.x * cr - w.y * sr, w.x * sr + w.y * cr) / kSc + uOfs[i];
            float I = texture(uTex, t / 64.0).r;
            if (kK > 0.0) I = clamp((I - kK) / (1.0 - kK), 0.0, 1.0);
            float I2 = texture(uTex, (t + ld.xz * 1.5) / 64.0).r;
            float lit = 1.0 + clamp((I2 - I) * 2.5, -0.35, 0.35) * (i == 1 ? 0.6 : 1.0);
            /* haze: clouds dissolve into the horizon colour just above it (GE's own
             * fade only reaches the shade, not the opacity) */
            float haze = smoothstep(0.0, 0.08, e);
            float a = I * kOp * haze;
            cover = 1.0 - (1.0 - cover) * (1.0 - min(1.0, a * 3.0));
            /* light on the cloud's own colour only, never on the sky part of the shade */
            vec3 cc = min(vec3(1.1), F + (shade - F) * lit);
            if (uSunGlow > 0.0) cc += uSunCol * pow(max(dot(nd, uSunDir), 0.0), 12.0) * (1.0 - I) * 0.6 * haze;   /* backlit edges */
            c = mix(c, cc, a);
        }
    }
    /* 3. the sun's halo shines through: a tight core and a wide bloom, dimmed by the clouds */
    if (uSunGlow > 0.0) {
        float sa = max(dot(nd, uSunDir), 0.0);
        c += uSunCol * (pow(sa, 400.0) * 1.2 + pow(sa, 60.0) * 0.45 + pow(sa, 8.0) * 0.12) * (1.0 - 0.6 * cover);   /* the halo pierces the fog */
    }
    /* 4. stars and moon only where the clouds let them through */
    c += star * (1.0 - cover);
    if (uNight > 0.5) {
        float cs = dot(nd, uMoonDir);
        float gl = pow(max(cs, 0.0), 40.0) * 0.08 * (1.0 - cover * 0.5);
        c = cs > 0.9997 ? mix(c + gl, vec3(0.85), 1.0 - cover) : c + gl;
    }
    /* 5. horizon band: everything fades to the fog colour before the horizon,
     * fully below ~2.3 deg, gone above ~12.7 deg, so whatever the level draws
     * far away (trees popping in at the draw distance) meets a flat fog colour,
     * as with GE's plane, and never cuts into the sun */
    c = mix(F, c, smoothstep(0.04, 0.22, e));
    o = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)GLSL";
    char head[64];
    snprintf(head, sizeof head, "#version %s\n%s", gl_glsl_version_str, gl_es ? "precision highp float;\n" : "");
    const char* vsrc[2] = { head, vs_body };
    const char* fsrc[2] = { head, fs_body };
    GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(v, 2, vsrc, NULL);
    glCompileShader(v);
    glShaderSource(f, 2, fsrc, NULL);
    glCompileShader(f);
    GLint okf = 0;
    char log[1024] = "";
    glGetShaderiv(f, GL_COMPILE_STATUS, &okf);
    if (!okf) {
        glGetShaderInfoLog(f, sizeof log, NULL, log);
        sysLogPrintf(LOG_ERROR, "GL: SkyPlus fragment shader: %s", log);
    }
    s_skyplus_prog = glCreateProgram();
    glAttachShader(s_skyplus_prog, v);
    glAttachShader(s_skyplus_prog, f);
    glLinkProgram(s_skyplus_prog);
    GLint ok = 0;
    glGetProgramiv(s_skyplus_prog, GL_LINK_STATUS, &ok);
    glGetProgramInfoLog(s_skyplus_prog, sizeof log, NULL, log);
    sysLogPrintf(ok ? LOG_NOTE : LOG_ERROR, "GL: SkyPlus program link=%d %s", ok, log);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) {
        glDeleteProgram(s_skyplus_prog);
        s_skyplus_prog = 0;
        return false;
    }
    static const char* const names[SPL_COUNT] = { "uVP", "uAsp", "uC0", "uC1", "uC2", "uC3", "uEye", "uCloudY", "uSky",
        "uCloud", "uOfs", "uGain", "uTexScale", "uSunDir", "uSunCol", "uSunGlow", "uMoonDir", "uNight", "uTex" };
    for (int i = 0; i < SPL_COUNT; i++) {
        s_sp_loc[i] = glGetUniformLocation(s_skyplus_prog, names[i]);
    }
    glGenVertexArrays(1, &s_skyplus_vao);
    glGenTextures(1, &s_skyplus_tex);
    return true;
}

extern "C" void gfx_opengl_skyplus_pass(int slot) {
    static bool failed = false;
    if (failed || slot < 0 || slot >= SKYPLUS_RING) {
        return;
    }
    if (!s_skyplus_prog && !skyplus_build()) {
        failed = true;
        gfx_skyplus_failed = 1;   /* skyplus.c draws GE's sky from the next frame on */
        return;
    }
    const SkyPlusDraw& d = s_skyplus_ring[slot];
    const PortSkyPlus& p = d.p;
    if (d.vp[2] <= 0 || d.vp[3] <= 0 || (d.aspA < 0.0f ? -d.aspA : d.aspA) < 1e-6f || !p.tex) {
        return;
    }

    GLint prev_prog = 0, prev_vao = 0, prev_unit = 0, prev_tex7 = 0, prev_vp[4], prev_sc[4], prev_align = 4;
    const GLboolean prev_depth = glIsEnabled(GL_DEPTH_TEST), prev_blend = glIsEnabled(GL_BLEND),
                    prev_cull = glIsEnabled(GL_CULL_FACE), prev_scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prev_cm[4], prev_dm = GL_TRUE;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_unit);
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    glGetIntegerv(GL_SCISSOR_BOX, prev_sc);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_align);
    glGetBooleanv(GL_COLOR_WRITEMASK, prev_cm);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_dm);
    glActiveTexture(GL_TEXTURE0 + 7);   /* a unit the game's tile sampling never uses */
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex7);

    glBindTexture(GL_TEXTURE_2D, s_skyplus_tex);
    if (p.texGen != s_skyplus_tex_gen) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, SKYPLUS_TEX_W, SKYPLUS_TEX_H, 0, GL_RED, GL_UNSIGNED_BYTE, p.tex);
        glGenerateMipmap(GL_TEXTURE_2D);   /* the plane is seen at grazing angles near the horizon */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, prev_align);
        s_skyplus_tex_gen = p.texGen;
    }

    glViewport(d.vp[0], d.vp[1], d.vp[2], d.vp[3]);
    glEnable(GL_SCISSOR_TEST);
    glScissor(d.vp[0], d.vp[1], d.vp[2], d.vp[3]);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(s_skyplus_prog);
    glUniform4f(s_sp_loc[SPL_VP], (float)d.vp[0], (float)d.vp[1], (float)d.vp[2], (float)d.vp[3]);
    glUniform2f(s_sp_loc[SPL_ASP], d.aspA, d.aspB);
    glUniform3fv(s_sp_loc[SPL_C0], 1, p.corner[0]);
    glUniform3fv(s_sp_loc[SPL_C1], 1, p.corner[1]);
    glUniform3fv(s_sp_loc[SPL_C2], 1, p.corner[2]);
    glUniform3fv(s_sp_loc[SPL_C3], 1, p.corner[3]);
    glUniform3fv(s_sp_loc[SPL_EYE], 1, p.eye);
    glUniform1f(s_sp_loc[SPL_CLOUDY], p.cloudY);
    glUniform3fv(s_sp_loc[SPL_SKY], 1, p.sky);
    glUniform3fv(s_sp_loc[SPL_CLOUD], 1, p.cloud);
    glUniform2fv(s_sp_loc[SPL_OFS], 3, &p.layerOfs[0][0]);
    glUniform1f(s_sp_loc[SPL_GAIN], p.gain);
    glUniform1f(s_sp_loc[SPL_TEXSCALE], p.texScale);
    glUniform3fv(s_sp_loc[SPL_SUNDIR], 1, p.sunDir);
    glUniform3fv(s_sp_loc[SPL_SUNCOL], 1, p.sunCol);
    glUniform1f(s_sp_loc[SPL_SUNGLOW], p.sunGlow);
    glUniform3fv(s_sp_loc[SPL_MOONDIR], 1, p.moonDir);
    glUniform1f(s_sp_loc[SPL_NIGHT], p.night ? 1.0f : 0.0f);
    glUniform1i(s_sp_loc[SPL_TEX], 7);
    glBindVertexArray(s_skyplus_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex7);
    glBindVertexArray((GLuint)prev_vao);
    glUseProgram((GLuint)prev_prog);
    glActiveTexture((GLenum)prev_unit);
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    glScissor(prev_sc[0], prev_sc[1], prev_sc[2], prev_sc[3]);
    glColorMask(prev_cm[0], prev_cm[1], prev_cm[2], prev_cm[3]);
    glDepthMask(prev_dm);
    if (prev_depth) glEnable(GL_DEPTH_TEST);
    if (prev_blend) glEnable(GL_BLEND);
    if (prev_cull) glEnable(GL_CULL_FACE);
    if (!prev_scissor) glDisable(GL_SCISSOR_TEST);
}

void gfx_opengl_resolve_msaa_color_buffer(int fb_id_target, int fb_id_source) {
    if (!gfx_framebuffers_enabled) {
        return;
    }

    Framebuffer& fb_dst = framebuffers[fb_id_target];
    Framebuffer& fb_src = framebuffers[fb_id_source];
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_dst.fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_src.fbo);
    glBlitFramebuffer(0, 0, fb_src.width, fb_src.height, 0, 0, fb_dst.width, fb_dst.height, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    /* current_framebuffer is an INDEX into framebuffers[], not a GL name; fb 0 is the window's default framebuffer (name 0). */
    glBindFramebuffer(GL_FRAMEBUFFER, current_framebuffer == 0 ? 0 : framebuffers[current_framebuffer].fbo);
    glEnable(GL_SCISSOR_TEST);
}

void* gfx_opengl_get_framebuffer_texture_id(int fb_id) {
    return (void*)(uintptr_t)framebuffers[fb_id].clrbuf;
}

void gfx_opengl_select_texture_fb(int fb_id) {
    // glDisable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, framebuffers[fb_id].clrbuf);

    current_textures_linear_filter[0] = true;
}

void gfx_opengl_copy_framebuffer(int fb_dst, int fb_src, int left, int top, bool flip_y, bool use_back) {
    if (!gfx_framebuffers_enabled || fb_dst >= (int)framebuffers.size() || fb_src >= (int)framebuffers.size()) {
        return;
    }

    const Framebuffer& src = framebuffers[fb_src];
    const Framebuffer& dst = framebuffers[fb_dst];

    int srcX0, srcY0, srcX1, srcY1;
    int dstX0, dstY0, dstX1, dstY1;

    dstX0 = dstY0 = 0;
    dstX1 = dst.width;
    dstY1 = dst.height;

    if (left >= 0 && top >= 0) {
        // unscaled rect copy
        srcX0 = left;
        srcY0 = top;
        srcX1 = left + dst.width;
        srcY1 = top + dst.height;
    } else {
        // scaled full copy
        srcX0 = 0;
        srcY0 = 0;
        srcX1 = src.width;
        srcY1 = src.height;
    }

    glDisable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, src.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.fbo);

    if (flip_y) {
        // flip the dst rect to mirror the image vertically
        std::swap(dstY0, dstY1);
    }

    if (fb_src == 0) {
        // GLES does not support GL_FRONT here
        glReadBuffer((use_back || gl_es) ? GL_BACK : GL_FRONT);
    } else {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, current_framebuffer == 0 ? 0 : framebuffers[current_framebuffer].fbo);

    glReadBuffer(GL_BACK);

    glEnable(GL_SCISSOR_TEST);
}

void gfx_opengl_set_texture_filter(FilteringMode mode) {
    current_filter_mode = mode;
}

FilteringMode gfx_opengl_get_texture_filter(void) {
    return current_filter_mode;
}

void gfx_opengl_set_mipmap_filter(MipmapFilteringMode mode) {
    current_mipmap_filter_mode = mode;
}

static int gfx_opengl_get_max_anisotropy_level() {
	GLfloat max_aniso_level;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &max_aniso_level);
	return (int)max_aniso_level;
}

static void gfx_opengl_set_anisotropy_level(int level) {
	current_anisotropy_level = level;
}

struct GfxRenderingAPI gfx_opengl_api = {
    gfx_opengl_get_name,
    gfx_opengl_get_max_texture_size,
    gfx_opengl_get_clip_parameters,
    gfx_opengl_unload_shader,
    gfx_opengl_load_shader,
    gfx_opengl_create_and_load_new_shader,
    gfx_opengl_lookup_shader,
    gfx_opengl_shader_get_info,
    gfx_opengl_clear_shaders,
    gfx_opengl_new_texture,
    gfx_opengl_select_texture,
    gfx_opengl_upload_texture,
    gfx_opengl_set_sampler_parameters,
    gfx_opengl_set_depth_mode,
    gfx_opengl_set_depth_range,
    gfx_opengl_set_viewport,
    gfx_opengl_set_scissor,
    gfx_opengl_set_use_alpha,
    gfx_opengl_draw_triangles,
    gfx_opengl_init,
    gfx_opengl_on_resize,
    gfx_opengl_start_frame,
    gfx_opengl_end_frame,
    gfx_opengl_finish_render,
    gfx_opengl_create_framebuffer,
    gfx_opengl_update_framebuffer_parameters,
    gfx_opengl_start_draw_to_framebuffer,
    gfx_opengl_copy_framebuffer,
    gfx_opengl_clear_framebuffer,
    gfx_opengl_clear_region,
    gfx_opengl_resolve_msaa_color_buffer,
    gfx_opengl_get_framebuffer_texture_id,
    gfx_opengl_select_texture_fb,
    gfx_opengl_delete_texture,
    gfx_opengl_set_texture_filter,
    gfx_opengl_get_texture_filter,
    gfx_opengl_set_mipmap_filter,
    gfx_opengl_set_anisotropy_level,
    gfx_opengl_get_max_anisotropy_level,
    gfx_opengl_stream_begin,
    gfx_opengl_load_shader_with
};
