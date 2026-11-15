/* D968: Fast3D frame recorder + GL replay. See gfx_frame.h.
 *
 * The recorder is a GfxRenderingAPI in front of the real backend. Outside a
 * frame body it forwards every call at once (init, settings, framebuffer
 * resizes, SHADERWARM) and only keeps its mirrors up to date. Inside a frame
 * body (gfx_frame_begin .. gfx_frame_submit) it records:
 *
 *  - state setters (shader load, texture unit select, depth, viewport, scissor,
 *    blend) update the recorder's current GfxFrameState and bump that group's
 *    version; a draw snapshots the state once per change (states are shared by
 *    consecutive batches);
 *  - texture uploads, sampler parameters, deletes, framebuffer and clear
 *    commands become ordered GfxFrameCmds (uploads copy the pixels into the
 *    frame);
 *  - vertices are written by gfx_sp_tri1 straight into the frame's float array
 *    (stream_begin hands out the write position); draw_triangles closes a batch.
 *
 * Queries the decoder needs an answer to during the walk are answered from
 * mirrors (clip parameters = the current target's y flip, start_draw_to_framebuffer's
 * result) or forwarded when they are pure CPU lookups (get_framebuffer_texture_id).
 * D969: shader programs are recorder handles (RecShader: combiner ids + the CC
 * features shader_get_info returns, from gfx_cc_get_features); lookup_shader
 * and shader_get_info never touch the backend, and the GL program is compiled on
 * the GL side (a GFC_CREATE_SHADER command, or on first use in the replay).
 * Texture names come from a pool the GL side refills; only a pool running dry
 * inside one frame still needs GL (counted, "texnames").
 *
 * D969 (GE_FRAMETHREAD, default on on the Switch): the replay runs on its own
 * thread, which owns the GL context. Two GfxFrames: the decoder records frame
 * N+1 while the GL thread replays frame N; the hand-off (gfx_frame_thread_present,
 * from gfx_end_frame) waits until the GL thread has finished the previous frame
 * (replay + window events + swap), so at most one frame is replaying while the
 * next one is recorded. While pipelined the recorder records continuously, so
 * gfx_start_frame's framebuffer updates and the video layer's settings land in
 * the frame too; rare calls that need an answer (create_framebuffer) run
 * synchronously on the GL thread (run_on_gl, counted "sync GL calls").
 *
 * Replay (gfx_frame_submit) walks the commands in order. Before each draw it
 * applies every state group whose version differs from what it last applied,
 * so GL sees the same state changes at the same points as with the old
 * immediate path; the shader program is loaded with the uniform values latched
 * when the decoder loaded it (load_shader_with), because GL keeps a program's
 * uniforms from its load. Vertices go through the backend's persistent ring when
 * it has one (GE_GLSTREAM, default on the Switch): one memcpy per batch. */

#include "gfx_frame.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <unordered_map>
#include <map>
#include <vector>
#include <pthread.h>

#include "system.h"
#include "gfx_cc.h"
#include "gfx_window_manager_api.h"
#include "gfx_sdl.h"

extern bool gfx_framebuffers_enabled;   /* gfx_pc.cpp */

namespace {

struct GfxRenderingAPI* B = nullptr;   /* the real backend */
struct GfxRenderingAPI s_api;          /* what gfx_pc calls */
struct GfxWindowManagerAPI* W = nullptr;   /* D969: window API (events, swap) for the GL thread */

bool s_rec = false;                    /* inside a frame body (D969 threaded: always, from one hand-off to the next) */
bool s_thr = false;                    /* D969: pipelined, a replay thread owns GL */
GfxFrame s_frames[2];                  /* D969: one replaying, one recording */
GfxFrame* s_fr = &s_frames[0];         /* the frame being recorded */

/* D969: shader programs as recorder handles. The decoder only needs the
 * combiner's input count and texture use (gfx_cc_get_features, no GL); the
 * backend program is created on the GL thread (real_of). Handles live for the
 * whole run: a frame waiting for the replay may still name one after a
 * clear_shaders, which only drops the backend programs. */
struct RecShader {
    uint64_t id0;
    uint32_t id1;
    uint8_t num_inputs;
    bool used_textures[2];
    bool known;                        /* decoder: created since the last clear_shaders (lookup_shader) */
    struct ShaderProgram* real;        /* GL thread: the backend program, or null */
};
std::map<std::pair<uint64_t, uint32_t>, RecShader*> s_shaders;   /* decoder side */
std::vector<RecShader*> s_realised;    /* GL thread: handles whose real is set */
inline RecShader* RS(struct ShaderProgram* p) { return (RecShader*)p; }

/* ---- recorder mirrors (decoder-visible state, kept in both modes) ---- */
GfxFrameState s_cur;                   /* current render state */
bool s_cur_dirty = true;               /* s_cur changed since the last pushed state */
int s_active_unit = 0;                 /* the unit the last select / sampler call went to */
uint32_t s_frame_count = 0;            /* backend frame_count */
float s_noise = 0.0f;                  /* backend current_noise_scale */
int s_fb_count = 1;                    /* backend framebuffers.size() (fb 0 = window) */
std::map<int, bool> s_fb_invert;       /* invert_y per framebuffer */
int s_cur_fb = 0;                      /* backend current_framebuffer */
std::unordered_map<uint32_t, GfxFrameSampler> s_tex_sampler;   /* sampling parameters per texture id */
std::vector<uint32_t> s_tex_pool;      /* pre-generated texture names (decoder side) */
int s_max_aniso = -1;                  /* D969: backend's max anisotropy, read once (a GL query) */

/* ---- replay state (what the backend / GL has) ---- */
struct Applied {
    bool valid;                        /* false: apply every group at the next draw */
    struct ShaderProgram* prg;         /* program whose vertex attributes are enabled (classic path) */
    uint32_t ver_prg, ver_unit[2], ver_depth, ver_viewport, ver_scissor, ver_blend;
};
Applied s_app = {};
bool s_prg_invalid = true;             /* the GL program was changed behind the replay's back */
bool s_units_invalid = true;           /* a texture unit binding was changed behind the replay's back */

/* ---- PERFSTAT ---- */
uint64_t s_st_frames, s_st_cmds, s_st_batches, s_st_states, s_st_vbytes, s_st_ubytes, s_st_uploads;
uint64_t s_st_replay_ns, s_st_gl_compiles, s_st_gl_texnames, s_st_flush_outside;
/* D969 (threaded): decoder blocked at hand-off / in a sync GL call; GL thread
 * idle (no frame to replay), in the present (events + swap); sync calls;
 * programs compiled on the GL thread. */
uint64_t s_st_dec_wait_ns, s_st_sync_ns, s_st_rep_idle_ns, s_st_present_ns, s_st_syncs, s_st_rep_compiles, s_st_handoffs;

inline uint64_t now_ns(void) {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int perf_on(void) {
    static int on = -1;
    if (on < 0) on = getenv("GE_PERFSTAT") != NULL;
    return on;
}

template <typename T> void grow(T*& p, size_t& cap, size_t need) {
    if (need <= cap) return;
    size_t n = cap ? cap : 256;
    while (n < need) n *= 2;
    T* q = (T*)realloc(p, n * sizeof(T));
    if (q == nullptr) {
        sysFatalError("gfx_frame: out of memory (%u bytes)", (unsigned)(n * sizeof(T)));
    }
    p = q;
    cap = n;
}

GfxFrameCmd& push_cmd(uint8_t type) {
    grow(s_fr->cmds, s_fr->cap_cmds, s_fr->ncmds + 1);
    GfxFrameCmd& c = s_fr->cmds[s_fr->ncmds++];
    memset(&c, 0, sizeof(c));
    c.type = type;
    return c;
}

void frame_reset(void) {
    s_fr->ncmds = s_fr->nbatches = s_fr->nstates = s_fr->nverts = s_fr->nbytes = 0;
    s_fr->tris = 0;
    s_cur_dirty = true;   /* the first batch of a frame pushes its state */
}

void latch_uniforms(void) {
    s_cur.uniforms.frame_count = s_frame_count;
    s_cur.uniforms.noise_scale = s_noise;
    s_cur.uniforms.three_point[0] = s_cur.unit[0].linear;
    s_cur.uniforms.three_point[1] = s_cur.unit[1].linear;
}

/* the program load the decoder made (or a create, which loads); prg is a RecShader handle */
void note_load(struct ShaderProgram* prg) {
    s_cur.prg = prg;
    if (prg != nullptr) {
        s_cur.shader_id0 = RS(prg)->id0;
        s_cur.shader_id1 = RS(prg)->id1;
    } else {
        s_cur.shader_id0 = 0;
        s_cur.shader_id1 = 0;
    }
    latch_uniforms();
    s_cur.ver_prg++;
    s_cur_dirty = true;
}

/* GL's program / vertex setup changed outside the replay (a compile, SHADERWARM):
 * reload at the next draw; nothing has its attributes enabled any more. */
void invalidate_program(void) {
    s_prg_invalid = true;
    s_app.prg = nullptr;
}

/* D969: GL thread (or the only thread when not pipelined): the backend program
 * of a handle, compiled on first use. Leaves GL clean for the replay (the
 * create also loads the program). */
struct ShaderProgram* real_of(RecShader* r) {
    if (r == nullptr) {
        return nullptr;
    }
    if (r->real == nullptr) {
        r->real = B->lookup_shader(r->id0, r->id1);
        if (r->real == nullptr) {
            B->unload_shader(s_app.prg);
            r->real = B->create_and_load_new_shader(r->id0, r->id1);
            B->unload_shader(r->real);
            invalidate_program();
            s_st_rep_compiles++;
        }
        s_realised.push_back(r);
    }
    return r->real;
}

/* GL thread: delete every backend program; handles stay valid and recompile on use. */
void do_clear_shaders(void) {
    B->clear_shaders();
    for (RecShader* r : s_realised) {
        r->real = nullptr;
    }
    s_realised.clear();
    invalidate_program();
}

/* D969: run fn(arg) on the GL thread and wait (pipelined), else here. */
void run_on_gl(void (*fn)(void*), void* arg);

void gen_names_on_gl(void* arg) {
    std::vector<uint32_t>* out = (std::vector<uint32_t>*)arg;
    while (out->size() < 64) {
        out->push_back(B->new_texture());
    }
}

/* ================= recorder entry points ================= */

const char* r_get_name(void) { return B->get_name(); }
int r_get_max_texture_size(void) { return B->get_max_texture_size(); }

struct GfxClipParameters r_get_clip_parameters(void) {
    struct GfxClipParameters p;
    p.z_is_from_0_to_1 = false;   /* the GL backend: always false */
    p.invert_y = s_fb_invert[s_cur_fb];
    return p;
}

void r_unload_shader(struct ShaderProgram* old_prg) {
    if (!s_rec) {
        struct ShaderProgram* real = old_prg != nullptr ? RS(old_prg)->real : nullptr;
        B->unload_shader(real);
        if (real == s_app.prg) {
            s_app.prg = nullptr;   /* its attributes are disabled now */
        }
    }
    /* recording: the replay unloads what it has loaded before each program load */
}

void r_load_shader(struct ShaderProgram* new_prg) {
    if (!s_rec) {
        /* not used outside a frame body by gfx_pc; keep GL and the mirror together */
        B->load_shader(real_of(RS(new_prg)));
        note_load(new_prg);
        invalidate_program();
        return;
    }
    note_load(new_prg);
}

/* D969: no GL here any more. The handle carries what the decoder asks of a
 * program (shader_get_info); recording, a GFC_CREATE_SHADER command makes the
 * GL thread compile it at this point of the frame (SHADERWARM compiles before
 * the first body, as before); outside a frame body it is compiled now. */
struct ShaderProgram* r_create_and_load_new_shader(uint64_t id0, uint32_t id1) {
    RecShader*& r = s_shaders[std::make_pair(id0, id1)];
    if (r == nullptr) {
        r = new RecShader();
        r->id0 = id0;
        r->id1 = id1;
        struct CCFeatures cc = {};
        gfx_cc_get_features(id0, id1, &cc);
        r->num_inputs = (uint8_t)cc.num_inputs;
        r->used_textures[0] = cc.used_textures[0];
        r->used_textures[1] = cc.used_textures[1];
        r->real = nullptr;
        /* D973 (GE_CCLOG=1): combiner census for the deko3d shader pack
         * (port/switch/shaders/combiners.txt, tools_pc/cc_census_merge.py);
         * once per distinct pair per run. Cached getenv (D250). */
        static int s_cclog = -1;
        if (s_cclog < 0) {
            const char* e = getenv("GE_CCLOG");
            s_cclog = (e != NULL && e[0] == '1') ? 1 : 0;
        }
        if (s_cclog) {
            sysLogPrintf(LOG_NOTE, "CCID %016llx %08x", (unsigned long long)id0, (unsigned)id1);
        }
    }
    r->known = true;
    if (s_rec) {
        s_st_gl_compiles++;   /* counted as before: a combiner first seen inside a body */
        GfxFrameCmd& c = push_cmd(GFC_CREATE_SHADER);
        c.p = r;
    } else {
        real_of(r);
    }
    note_load((struct ShaderProgram*)r);
    return (struct ShaderProgram*)r;
}

struct ShaderProgram* r_lookup_shader(uint64_t id0, uint32_t id1) {
    auto it = s_shaders.find(std::make_pair(id0, id1));
    return (it != s_shaders.end() && it->second->known) ? (struct ShaderProgram*)it->second : nullptr;
}

void r_shader_get_info(struct ShaderProgram* prg, uint8_t* num_inputs, bool used_textures[2]) {
    *num_inputs = RS(prg)->num_inputs;
    used_textures[0] = RS(prg)->used_textures[0];
    used_textures[1] = RS(prg)->used_textures[1];
}

void r_clear_shaders(void) {
    for (auto& kv : s_shaders) {
        kv.second->known = false;   /* lookup misses again: SHADERWARM re-creates, as before */
    }
    if (s_rec) {
        push_cmd(GFC_CLEAR_SHADERS);   /* in order with the frame's draws (D968 replayed early instead) */
    } else {
        do_clear_shaders();
    }
    s_cur.prg = nullptr;
    s_cur.ver_prg++;
    s_cur_dirty = true;
}

uint32_t r_new_texture(void) {
    if (s_rec) {
        if (s_tex_pool.empty() && s_thr) {
            /* the GL thread refills the pool after each frame; ran dry inside
             * one (a level load): generate a batch there and wait */
            s_st_gl_texnames++;
            std::vector<uint32_t> names;
            run_on_gl(gen_names_on_gl, &names);
            s_tex_pool.insert(s_tex_pool.end(), names.begin(), names.end());
        }
        if (!s_tex_pool.empty()) {
            const uint32_t t = s_tex_pool.back();
            s_tex_pool.pop_back();
            return t;
        }
        s_st_gl_texnames++;   /* GL during the walk (exception 2) */
    }
    return B->new_texture();
}

void r_select_texture(int tile, uint32_t texture_id, bool linear_filter) {
    s_cur.unit[tile].kind = GFX_UNIT_TEXTURE;
    s_cur.unit[tile].id = texture_id;
    s_cur.unit[tile].linear = linear_filter;
    s_cur.ver_unit[tile]++;
    s_cur_dirty = true;
    s_active_unit = tile;
    if (!s_rec) {
        B->select_texture(tile, texture_id, linear_filter);
        s_app.ver_unit[tile] = s_cur.ver_unit[tile];
    }
}

void r_select_texture_fb(int fb_id) {
    s_cur.unit[0].kind = GFX_UNIT_FRAMEBUFFER;
    s_cur.unit[0].id = (uint32_t)fb_id;
    s_cur.unit[0].linear = true;
    s_cur.ver_unit[0]++;
    s_cur_dirty = true;
    s_active_unit = 0;
    if (!s_rec) {
        B->select_texture_fb(fb_id);
        s_app.ver_unit[0] = s_cur.ver_unit[0];
    }
}

void r_upload_texture(const uint8_t* rgba32_buf, uint32_t width, uint32_t height, bool gen_mipmaps) {
    if (!s_rec) {
        B->upload_texture(rgba32_buf, width, height, gen_mipmaps);
        return;
    }
    const size_t n = (size_t)width * height * 4;
    grow(s_fr->bytes, s_fr->cap_bytes, s_fr->nbytes + n);
    memcpy(s_fr->bytes + s_fr->nbytes, rgba32_buf, n);
    GfxFrameCmd& c = push_cmd(GFC_UPLOAD);
    c.unit = (uint8_t)s_active_unit;
    c.ref = s_cur.unit[s_active_unit];
    c.ref_ver = s_cur.ver_unit[s_active_unit];
    c.a = (int32_t)width;
    c.b = (int32_t)height;
    c.b0 = gen_mipmaps;
    c.off = s_fr->nbytes;
    s_fr->nbytes += n;
    s_st_ubytes += n;
    s_st_uploads++;
}

void r_set_sampler_parameters(int sampler, bool linear_filter, uint32_t cms, uint32_t cmt, bool mipmaps) {
    GfxFrameSampler smp;
    smp.set = true;
    smp.linear = linear_filter;
    smp.mipmaps = mipmaps;
    smp.cms = (uint8_t)cms;
    smp.cmt = (uint8_t)cmt;
    s_active_unit = sampler;
    const GfxFrameUnit& u = s_cur.unit[sampler];
    if (u.kind == GFX_UNIT_TEXTURE) {
        s_tex_sampler[u.id] = smp;
        s_cur_dirty = true;   /* the resolved sampler of a bound unit may have changed */
    }
    if (!s_rec) {
        B->set_sampler_parameters(sampler, linear_filter, cms, cmt, mipmaps);
        return;
    }
    GfxFrameCmd& c = push_cmd(GFC_SAMPLER);
    c.unit = (uint8_t)sampler;
    c.ref = u;
    c.ref_ver = s_cur.ver_unit[sampler];
    c.smp = smp;
}

void r_set_depth_mode(bool depth_test, bool depth_update, bool depth_compare, bool depth_source_prim, uint16_t zmode) {
    s_cur.depth.test = depth_test;
    s_cur.depth.update = depth_update;
    s_cur.depth.compare = depth_compare;
    s_cur.depth.source_prim = depth_source_prim;
    s_cur.depth.zmode = zmode;
    s_cur.ver_depth++;
    s_cur_dirty = true;
    if (!s_rec) {
        B->set_depth_mode(depth_test, depth_update, depth_compare, depth_source_prim, zmode);
        s_app.ver_depth = s_cur.ver_depth;
    }
}

struct DepthRangeArgs { float n, f; };
void depth_range_on_gl(void* a) { B->set_depth_range(((DepthRangeArgs*)a)->n, ((DepthRangeArgs*)a)->f); }

void r_set_depth_range(float znear, float zfar) {
    /* not used by gfx_pc; immediate (on the GL thread when pipelined) */
    DepthRangeArgs a = { znear, zfar };
    run_on_gl(depth_range_on_gl, &a);
}

void r_set_viewport(int x, int y, int width, int height) {
    s_cur.viewport[0] = x;
    s_cur.viewport[1] = y;
    s_cur.viewport[2] = width;
    s_cur.viewport[3] = height;
    s_cur.ver_viewport++;
    s_cur_dirty = true;
    if (!s_rec) {
        B->set_viewport(x, y, width, height);
        s_app.ver_viewport = s_cur.ver_viewport;
    }
}

void r_set_scissor(int x, int y, int width, int height) {
    s_cur.scissor[0] = x;
    s_cur.scissor[1] = y;
    s_cur.scissor[2] = width;
    s_cur.scissor[3] = height;
    s_cur.ver_scissor++;
    s_cur_dirty = true;
    if (!s_rec) {
        B->set_scissor(x, y, width, height);
        s_app.ver_scissor = s_cur.ver_scissor;
    }
}

void r_set_use_alpha(bool use_alpha, bool modulate) {
    s_cur.use_alpha = use_alpha;
    s_cur.modulate = modulate;
    s_cur.ver_blend++;
    s_cur_dirty = true;
    if (!s_rec) {
        B->set_use_alpha(use_alpha, modulate);
        s_app.ver_blend = s_cur.ver_blend;
    }
}

float* r_stream_begin(size_t max_floats) {
    if (!s_rec) {
        return nullptr;   /* the decoder fills its own buffer; draw_triangles forwards it */
    }
    grow(s_fr->verts, s_fr->cap_verts, s_fr->nverts + max_floats);
    return s_fr->verts + s_fr->nverts;
}

void r_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    if (!s_rec) {
        s_st_flush_outside++;
        B->draw_triangles(buf_vbo, buf_vbo_len, buf_vbo_num_tris);
        return;
    }
    size_t first;
    if (buf_vbo == s_fr->verts + s_fr->nverts) {
        first = s_fr->nverts;   /* written in place (stream_begin) */
    } else {
        grow(s_fr->verts, s_fr->cap_verts, s_fr->nverts + buf_vbo_len);
        first = s_fr->nverts;
        memcpy(s_fr->verts + first, buf_vbo, buf_vbo_len * sizeof(float));
    }
    s_fr->nverts = first + buf_vbo_len;
    if (s_cur_dirty) {
        for (int u = 0; u < 2; u++) {
            s_cur.sampler[u] = GfxFrameSampler();
            if (s_cur.unit[u].kind == GFX_UNIT_TEXTURE) {
                auto it = s_tex_sampler.find(s_cur.unit[u].id);
                if (it != s_tex_sampler.end()) s_cur.sampler[u] = it->second;
            }
        }
        grow(s_fr->states, s_fr->cap_states, s_fr->nstates + 1);
        s_fr->states[s_fr->nstates++] = s_cur;
        s_cur_dirty = false;
    }
    grow(s_fr->batches, s_fr->cap_batches, s_fr->nbatches + 1);
    GfxFrameBatch& b = s_fr->batches[s_fr->nbatches];
    b.state = (uint32_t)(s_fr->nstates - 1);
    b.first = (uint32_t)first;
    b.nfloats = (uint32_t)buf_vbo_len;
    b.ntris = (uint32_t)buf_vbo_num_tris;
    GfxFrameCmd& c = push_cmd(GFC_DRAW);
    c.a = (int32_t)s_fr->nbatches;
    s_fr->nbatches++;
    s_fr->tris += (uint32_t)buf_vbo_num_tris;
}

void r_init(void) {
    B->init();
    s_fb_count = 1;   /* gfx_opengl_init: framebuffers.resize(1) */
}

void on_resize_on_gl(void*) { B->on_resize(); }
void r_on_resize(void) { run_on_gl(on_resize_on_gl, nullptr); }

void r_start_frame(void) {
    s_frame_count++;
    if (!s_rec) {
        B->start_frame();
        return;
    }
    push_cmd(GFC_START_FRAME);
}

void r_end_frame(void) {
    if (!s_rec) {
        B->end_frame();
        return;
    }
    push_cmd(GFC_END_FRAME);
}

void r_finish_render(void) {
    if (s_rec) {
        gfx_frame_submit();
        s_rec = true;
    }
    B->finish_render();
}

void create_fb_on_gl(void* a) {
    *(int*)a = B->create_framebuffer();
    /* creation binds texture 0 on the active unit: re-select at the next draw */
    s_units_invalid = true;
}

int r_create_framebuffer(void) {
    if (s_rec && !s_thr) {
        /* GL during the walk; gfx_pc only creates framebuffers outside frame bodies */
        s_st_gl_compiles++;
    }
    int id = 0;
    run_on_gl(create_fb_on_gl, &id);   /* D969: on the GL thread (and waited for) when pipelined */
    s_fb_count = id + 1 > s_fb_count ? id + 1 : s_fb_count;
    s_fb_invert[id] = false;
    return id;
}

void r_update_framebuffer_parameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                     bool opengl_invert_y, bool render_target, bool has_depth_buffer,
                                     bool can_extract_depth) {
    s_fb_invert[fb_id] = opengl_invert_y;
    if (!s_rec) {
        B->update_framebuffer_parameters(fb_id, width, height, msaa_level, opengl_invert_y, render_target,
                                         has_depth_buffer, can_extract_depth);
        s_units_invalid = true;   /* a resize binds texture 0 on the active unit */
        return;
    }
    GfxFrameCmd& c = push_cmd(GFC_UPDATE_FB);
    c.a = fb_id;
    c.b = (int32_t)width;
    c.c = (int32_t)height;
    c.d = (int32_t)msaa_level;
    c.b0 = opengl_invert_y;
    c.b1 = render_target;
    c.b2 = has_depth_buffer;
    c.b3 = can_extract_depth;
}

bool r_start_draw_to_framebuffer(int fb_id, float noise_scale) {
    const bool ok = gfx_framebuffers_enabled && fb_id < s_fb_count;
    if (ok) {
        if (noise_scale != 0.0f) {
            s_noise = 1.0f / noise_scale;
        }
        s_cur.target_fb = fb_id;
        s_cur_dirty = true;
        s_cur_fb = fb_id;
    }
    if (!s_rec) {
        return B->start_draw_to_framebuffer(fb_id, noise_scale);
    }
    GfxFrameCmd& c = push_cmd(GFC_START_FB);
    c.a = fb_id;
    c.fl = noise_scale;
    return ok;
}

void r_copy_framebuffer(int fb_dst, int fb_src, int left, int top, bool flip_y, bool use_back) {
    if (!s_rec) {
        B->copy_framebuffer(fb_dst, fb_src, left, top, flip_y, use_back);
        return;
    }
    GfxFrameCmd& c = push_cmd(GFC_COPY_FB);
    c.a = fb_dst;
    c.b = fb_src;
    c.c = left;
    c.d = top;
    c.b0 = flip_y;
    c.b1 = use_back;
}

void r_clear_framebuffer(bool clear_color, bool clear_depth) {
    if (!s_rec) {
        B->clear_framebuffer(clear_color, clear_depth);
        return;
    }
    GfxFrameCmd& c = push_cmd(GFC_CLEAR);
    c.b0 = clear_color;
    c.b1 = clear_depth;
}

void r_clear_region(bool clear_color, bool clear_depth, int x, int y, int w, int h) {
    if (!s_rec) {
        B->clear_region(clear_color, clear_depth, x, y, w, h);
        return;
    }
    GfxFrameCmd& c = push_cmd(GFC_CLEAR_REGION);
    c.b0 = clear_color;
    c.b1 = clear_depth;
    c.a = x;
    c.b = y;
    c.c = w;
    c.d = h;
}

void r_resolve_msaa_color_buffer(int fb_id_target, int fb_id_source) {
    if (!s_rec) {
        B->resolve_msaa_color_buffer(fb_id_target, fb_id_source);
        return;
    }
    GfxFrameCmd& c = push_cmd(GFC_RESOLVE);
    c.a = fb_id_target;
    c.b = fb_id_source;
}

void* r_get_framebuffer_texture_id(int fb_id) {
    return B->get_framebuffer_texture_id(fb_id);   /* a stored GL name, no GL call */
}

void r_delete_texture(uint32_t texID) {
    s_tex_sampler.erase(texID);
    if (!s_rec) {
        B->delete_texture(texID);
        return;
    }
    GfxFrameCmd& c = push_cmd(GFC_DELETE_TEX);
    c.a = (int32_t)texID;
}

/* D969: texture filter / mipmap / anisotropy settings. Recording, they are
 * commands replayed in order (D968 replayed the frame so far, then applied
 * them); the decoder reads the filter back from a mirror. */
bool s_filter_known = false;
enum FilteringMode s_filter = FILTER_LINEAR;

void apply_setting(int kind, int value) {
    switch (kind) {
        case 0: B->set_texture_filter((enum FilteringMode)value); break;
        case 1: B->set_mipmap_filter((enum MipmapFilteringMode)value); break;
        case 2: if (B->set_anisotropy_level) B->set_anisotropy_level(value); break;
    }
}

void record_setting(int kind, int value) {
    if (!s_rec) {
        apply_setting(kind, value);
        return;
    }
    GfxFrameCmd& c = push_cmd(GFC_SETTING);
    c.a = kind;
    c.b = value;
}

void r_set_texture_filter(enum FilteringMode mode) {
    s_filter = mode;
    s_filter_known = true;
    record_setting(0, (int)mode);
}
enum FilteringMode r_get_texture_filter(void) {
    if (!s_filter_known) {
        s_filter = B->get_texture_filter();   /* a stored value, no GL */
        s_filter_known = true;
    }
    return s_filter;
}
void r_set_mipmap_filter(enum MipmapFilteringMode mode) {
    record_setting(1, (int)mode);
}
void r_set_anisotropy_level(int level) {
    record_setting(2, level);
}
int r_get_max_anisotropy_level(void) {
    if (s_max_aniso < 0) {
        /* a GL query: first asked at video init, on the thread holding the context */
        s_max_aniso = B->get_max_anisotropy_level ? B->get_max_anisotropy_level() : 1;
    }
    return s_max_aniso;
}

void r_load_shader_with(struct ShaderProgram* prg, uint32_t frame_count, float noise_scale, bool tpf0, bool tpf1) {
    /* replay-only entry; never called by gfx_pc */
    B->load_shader_with(prg, frame_count, noise_scale, tpf0, tpf1);
}

/* ================= replay ================= */

inline void select_unit(int u, const GfxFrameUnit& ref) {
    if (ref.kind == GFX_UNIT_FRAMEBUFFER) {
        B->select_texture_fb((int)ref.id);
    } else if (ref.kind == GFX_UNIT_TEXTURE) {
        B->select_texture(u, ref.id, ref.linear);
    }
}

void apply_state(const GfxFrameState& st) {
    const bool all = !s_app.valid;
    for (int u = 0; u < 2; u++) {
        if (all || s_units_invalid || st.ver_unit[u] != s_app.ver_unit[u]) {
            select_unit(u, st.unit[u]);
            s_app.ver_unit[u] = st.ver_unit[u];
        }
    }
    s_units_invalid = false;
    if (st.prg != nullptr && (all || s_prg_invalid || st.ver_prg != s_app.ver_prg)) {
        struct ShaderProgram* real = real_of(RS(st.prg));   /* D969: compiled here if needed (after a clear) */
        B->unload_shader(s_app.prg);
        B->load_shader_with(real, st.uniforms.frame_count, st.uniforms.noise_scale,
                            st.uniforms.three_point[0], st.uniforms.three_point[1]);
        s_app.prg = real;
        s_app.ver_prg = st.ver_prg;
        s_prg_invalid = false;
    }
    if (all || st.ver_depth != s_app.ver_depth) {
        B->set_depth_mode(st.depth.test, st.depth.update, st.depth.compare, st.depth.source_prim, st.depth.zmode);
        s_app.ver_depth = st.ver_depth;
    }
    if (all || st.ver_viewport != s_app.ver_viewport) {
        B->set_viewport(st.viewport[0], st.viewport[1], st.viewport[2], st.viewport[3]);
        s_app.ver_viewport = st.ver_viewport;
    }
    if (all || st.ver_scissor != s_app.ver_scissor) {
        B->set_scissor(st.scissor[0], st.scissor[1], st.scissor[2], st.scissor[3]);
        s_app.ver_scissor = st.ver_scissor;
    }
    if (all || st.ver_blend != s_app.ver_blend) {
        B->set_use_alpha(st.use_alpha, st.modulate);
        s_app.ver_blend = st.ver_blend;
    }
    s_app.valid = true;
}

/* UPLOAD / SAMPLER act on the texture bound to a unit: bind the one the
 * decoder had bound when it made the call. */
inline void bind_ref(const GfxFrameCmd& c, bool force) {
    if (force || !s_app.valid || s_units_invalid || s_app.ver_unit[c.unit] != c.ref_ver) {
        select_unit(c.unit, c.ref);
        s_app.ver_unit[c.unit] = c.ref_ver;
    }
}

void replay(const GfxFrame& f) {
    for (size_t i = 0; i < f.ncmds; i++) {
        const GfxFrameCmd& c = f.cmds[i];
        switch (c.type) {
            case GFC_DRAW: {
                const GfxFrameBatch& b = f.batches[c.a];
                apply_state(f.states[b.state]);
                const float* src = f.verts + b.first;
                float* dst = B->stream_begin(b.nfloats);
                if (dst != nullptr) {
                    memcpy(dst, src, (size_t)b.nfloats * sizeof(float));
                    B->draw_triangles(dst, b.nfloats, b.ntris);
                } else {
                    B->draw_triangles((float*)src, b.nfloats, b.ntris);
                }
                break;
            }
            case GFC_UPLOAD:
                bind_ref(c, true);   /* upload_texture writes to the active unit */
                B->upload_texture(f.bytes + c.off, (uint32_t)c.a, (uint32_t)c.b, c.b0);
                break;
            case GFC_SAMPLER:
                bind_ref(c, false);
                B->set_sampler_parameters(c.unit, c.smp.linear, c.smp.cms, c.smp.cmt, c.smp.mipmaps);
                break;
            case GFC_DELETE_TEX:
                B->delete_texture((uint32_t)c.a);
                break;
            case GFC_START_FRAME:
                B->start_frame();
                break;
            case GFC_END_FRAME:
                B->end_frame();
                break;
            case GFC_UPDATE_FB:
                B->update_framebuffer_parameters(c.a, (uint32_t)c.b, (uint32_t)c.c, (uint32_t)c.d, c.b0, c.b1, c.b2, c.b3);
                if (c.a != 0) s_units_invalid = true;   /* a resize binds texture 0 on the active unit */
                break;
            case GFC_START_FB:
                B->start_draw_to_framebuffer(c.a, c.fl);
                break;
            case GFC_CLEAR:
                B->clear_framebuffer(c.b0, c.b1);
                break;
            case GFC_CLEAR_REGION:
                B->clear_region(c.b0, c.b1, c.a, c.b, c.c, c.d);
                break;
            case GFC_COPY_FB:
                B->copy_framebuffer(c.a, c.b, c.c, c.d, c.b0, c.b1);
                break;
            case GFC_RESOLVE:
                B->resolve_msaa_color_buffer(c.a, c.b);
                break;
            case GFC_CALL:
                c.fn(c.a);
                break;
            case GFC_CREATE_SHADER:
                real_of(RS((struct ShaderProgram*)c.p));
                break;
            case GFC_CLEAR_SHADERS:
                do_clear_shaders();
                break;
            case GFC_SETTING:
                apply_setting(c.a, c.b);
                break;
        }
    }
}


/* ================= D969: the replay thread ================= */

int rec_enabled(void) {
    static int on = -1;   /* GE_FRAMEREC=0: no recording, every call goes straight to the backend (the old immediate path, for A/B) */
    if (on < 0) {
        const char* e = getenv("GE_FRAMEREC");
        on = !(e != NULL && e[0] == '0');
        sysLogPrintf(LOG_NOTE, "F3D: frame recorder %s (D968)", on ? "on: decode -> GfxFrame -> replay" : "OFF (GE_FRAMEREC=0): immediate rendering-API calls");
    }
    return on;
}

enum JobKind { JOB_FRAME, JOB_CALL, JOB_RELEASE, JOB_PARK };

struct Job {
    int kind;
    GfxFrame* frame;          /* JOB_FRAME */
    bool present;             /* JOB_FRAME: events + swap + after-present hook */
    int64_t token;
    void (*fn)(void*);        /* JOB_CALL */
    void* arg;
};

pthread_mutex_t s_tl = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t s_tc = PTHREAD_COND_INITIALIZER;
bool s_job_pending = false;   /* s_job holds a job the GL thread has not taken yet */
bool s_busy = false;          /* the GL thread is running a job */
Job s_job;
bool s_running = false;       /* the GL thread exists (decoder side) */
pthread_t s_th;
std::vector<uint32_t> s_refill;   /* texture names the GL thread generated for the decoder (under s_tl) */
size_t s_want = 64;               /* names the decoder's pool is short of (under s_tl) */
void (*s_hook_enter)(void) = nullptr;
void (*s_hook_after)(int64_t) = nullptr;
int64_t s_token = -1;
bool s_token_set = false;
uint64_t s_last_wait_ns = 0;      /* the last hand-off's wait for the GL thread */

/* GL thread: one recorded frame (plus, for a frame that ends with a present,
 * the window events first and the swap after). */
void run_frame_job(const Job& j) {
    const uint64_t t0 = perf_on() ? now_ns() : 0;
    if (j.present && W != nullptr) {
        W->handle_events();   /* D959: SDL may recreate the EGL surface here; must be the GL thread */
    }
    replay(*j.frame);
    uint64_t t1 = 0;
    if (t0) {
        t1 = now_ns();
        s_st_replay_ns += t1 - t0;
        s_st_frames++;
        s_st_cmds += j.frame->ncmds;
        s_st_batches += j.frame->nbatches;
        s_st_states += j.frame->nstates;
        s_st_vbytes += j.frame->nverts * sizeof(float);
    }
    if (j.present && W != nullptr) {
        W->swap_buffers_begin();   /* FpsCap limiter, pre-swap capture hook, SDL_GL_SwapWindow */
        B->finish_render();
        W->swap_buffers_end();
        if (t1) s_st_present_ns += now_ns() - t1;
        if (s_hook_after) s_hook_after(j.token);
    }
}

void* replay_thread(void*) {
    if (s_hook_enter) s_hook_enter();
    gfx_sdl_make_context_current();
    std::vector<uint32_t> names;
    for (;;) {
        pthread_mutex_lock(&s_tl);
        const uint64_t ti = perf_on() ? now_ns() : 0;
        while (!s_job_pending) {
            pthread_cond_wait(&s_tc, &s_tl);
        }
        if (ti) s_st_rep_idle_ns += now_ns() - ti;
        const Job j = s_job;
        s_job_pending = false;
        s_busy = true;
        pthread_mutex_unlock(&s_tl);

        gfx_sdl_make_context_current();   /* no-op when already current (SDL) */
        bool leave = false;
        switch (j.kind) {
            case JOB_FRAME: run_frame_job(j); break;
            case JOB_CALL: j.fn(j.arg); break;
            case JOB_RELEASE: gfx_sdl_release_context(); break;
            case JOB_PARK: gfx_sdl_park_for_exit(); leave = true; break;   /* glFinish + unbind (D344) */
        }
        if (!leave && j.kind != JOB_RELEASE) {
            /* refill the decoder's pool of texture names (64) with what it used */
            pthread_mutex_lock(&s_tl);
            const size_t want = s_want;
            s_want = 0;
            pthread_mutex_unlock(&s_tl);
            while (names.size() < want) names.push_back(B->new_texture());
        }
        pthread_mutex_lock(&s_tl);
        s_refill.insert(s_refill.end(), names.begin(), names.end());
        names.clear();
        s_busy = false;
        pthread_cond_broadcast(&s_tc);
        pthread_mutex_unlock(&s_tl);
        if (leave) break;
    }
    return nullptr;
}

/* Decoder: wait (with s_tl held) until the GL thread has nothing queued or running. */
void wait_idle_locked(void) {
    while (s_job_pending || s_busy) {
        pthread_cond_wait(&s_tc, &s_tl);
    }
}

void take_refill_locked(void) {
    if (!s_refill.empty()) {
        s_tex_pool.insert(s_tex_pool.end(), s_refill.begin(), s_refill.end());
        s_refill.clear();
    }
    s_want = s_tex_pool.size() < 64 ? 64 - s_tex_pool.size() : 0;
}

void post_locked(const Job& j) {
    s_job = j;
    s_job_pending = true;
    pthread_cond_broadcast(&s_tc);
}

/* Decoder: hand the frame being recorded to the GL thread and switch to the
 * other GfxFrame. Waits until the previous frame's replay (and swap) is done:
 * at most one frame replaying while the next one is recorded. */
void handoff(bool present, int64_t token) {
    const uint64_t t0 = now_ns();
    pthread_mutex_lock(&s_tl);
    wait_idle_locked();
    s_last_wait_ns = now_ns() - t0;
    s_st_dec_wait_ns += s_last_wait_ns;
    Job j = {};
    j.kind = JOB_FRAME;
    j.frame = s_fr;
    j.present = present;
    j.token = token;
    post_locked(j);
    take_refill_locked();
    pthread_mutex_unlock(&s_tl);
    s_st_handoffs++;
    s_fr = (s_fr == &s_frames[0]) ? &s_frames[1] : &s_frames[0];
    frame_reset();   /* the GL thread finished with this one before the wait above returned */
}

void run_on_gl(void (*fn)(void*), void* arg) {
    if (!s_thr) {
        fn(arg);
        return;
    }
    /* keep the order: what was recorded so far goes first (no present) */
    if (s_fr->ncmds > 0) {
        handoff(false, -1);
    }
    const uint64_t t0 = perf_on() ? now_ns() : 0;
    pthread_mutex_lock(&s_tl);
    wait_idle_locked();
    Job j = {};
    j.kind = JOB_CALL;
    j.fn = fn;
    j.arg = arg;
    post_locked(j);
    wait_idle_locked();
    take_refill_locked();
    pthread_mutex_unlock(&s_tl);
    s_st_syncs++;
    if (t0) s_st_sync_ns += now_ns() - t0;
}

void post_and_wait(int kind) {
    pthread_mutex_lock(&s_tl);
    wait_idle_locked();
    Job j = {};
    j.kind = kind;
    post_locked(j);
    wait_idle_locked();
    pthread_mutex_unlock(&s_tl);
}

int thread_wanted(void) {
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("GE_FRAMETHREAD");
#if defined(__SWITCH__)
        on = !(e != NULL && e[0] == '0');   /* the Switch: on unless GE_FRAMETHREAD=0 */
#else
        on = e != NULL && e[0] != '0';      /* elsewhere: opt-in */
#endif
        if (on && !rec_enabled()) {
            on = 0;   /* nothing to replay without the recorder */
        }
        sysLogPrintf(LOG_NOTE, "F3D: replay thread %s (D969, GE_FRAMETHREAD)", on ? "on: decoder and GL replay pipelined" : "off: replay inline after the decode");
    }
    return on;
}

}   // namespace

extern "C" {

struct GfxRenderingAPI* gfx_frame_install(struct GfxRenderingAPI* backend) {
    B = backend;
    memset(&s_cur, 0, sizeof(s_cur));
    s_fb_invert[0] = false;
    s_api = *backend;   /* any member not wrapped below stays the backend's */
    s_api.get_name = r_get_name;
    s_api.get_max_texture_size = r_get_max_texture_size;
    s_api.get_clip_parameters = r_get_clip_parameters;
    s_api.unload_shader = r_unload_shader;
    s_api.load_shader = r_load_shader;
    s_api.create_and_load_new_shader = r_create_and_load_new_shader;
    s_api.lookup_shader = r_lookup_shader;
    s_api.shader_get_info = r_shader_get_info;
    s_api.clear_shaders = r_clear_shaders;
    s_api.new_texture = r_new_texture;
    s_api.select_texture = r_select_texture;
    s_api.upload_texture = r_upload_texture;
    s_api.set_sampler_parameters = r_set_sampler_parameters;
    s_api.set_depth_mode = r_set_depth_mode;
    s_api.set_depth_range = r_set_depth_range;
    s_api.set_viewport = r_set_viewport;
    s_api.set_scissor = r_set_scissor;
    s_api.set_use_alpha = r_set_use_alpha;
    s_api.draw_triangles = r_draw_triangles;
    s_api.init = r_init;
    s_api.on_resize = r_on_resize;
    s_api.start_frame = r_start_frame;
    s_api.end_frame = r_end_frame;
    s_api.finish_render = r_finish_render;
    s_api.create_framebuffer = r_create_framebuffer;
    s_api.update_framebuffer_parameters = r_update_framebuffer_parameters;
    s_api.start_draw_to_framebuffer = r_start_draw_to_framebuffer;
    s_api.copy_framebuffer = r_copy_framebuffer;
    s_api.clear_framebuffer = r_clear_framebuffer;
    s_api.clear_region = r_clear_region;
    s_api.resolve_msaa_color_buffer = r_resolve_msaa_color_buffer;
    s_api.get_framebuffer_texture_id = r_get_framebuffer_texture_id;
    s_api.select_texture_fb = r_select_texture_fb;
    s_api.delete_texture = r_delete_texture;
    s_api.set_texture_filter = r_set_texture_filter;
    s_api.get_texture_filter = r_get_texture_filter;
    s_api.set_mipmap_filter = r_set_mipmap_filter;
    s_api.set_anisotropy_level = r_set_anisotropy_level;
    s_api.get_max_anisotropy_level = r_get_max_anisotropy_level;
    s_api.stream_begin = r_stream_begin;
    s_api.load_shader_with = r_load_shader_with;
    return &s_api;
}

void gfx_frame_begin(void) {
    if (s_thr) {
        return;   /* D969: already recording (from the last hand-off on) */
    }
    if (!rec_enabled()) {
        return;
    }
    /* top up the texture-name pool while GL calls are still allowed */
    while (s_tex_pool.size() < 64) {
        s_tex_pool.push_back(B->new_texture());
    }
    frame_reset();
    s_rec = true;
}

bool gfx_frame_recording(void) {
    return s_rec;
}

void gfx_frame_submit(void) {
    if (!s_rec || s_thr) {
        return;   /* D969: pipelined, the frame goes to the GL thread at gfx_frame_thread_present */
    }
    s_rec = false;
    const uint64_t t0 = perf_on() ? now_ns() : 0;
    replay(*s_fr);
    if (t0) {
        s_st_replay_ns += now_ns() - t0;
        s_st_frames++;
        s_st_cmds += s_fr->ncmds;
        s_st_batches += s_fr->nbatches;
        s_st_states += s_fr->nstates;
        s_st_vbytes += s_fr->nverts * sizeof(float);
    }
    frame_reset();
}


void gfx_frame_set_wapi(struct GfxWindowManagerAPI* wapi) {
    W = wapi;
}

bool gfx_frame_thread_wanted(void) {
    return thread_wanted() != 0;
}

bool gfx_frame_thread_active(void) {
    return s_thr;
}

void gfx_frame_thread_set_hooks(void (*enter)(void), void (*after_present)(int64_t token)) {
    s_hook_enter = enter;
    s_hook_after = after_present;
}

void gfx_frame_thread_activate(bool on) {
    if (on == s_thr) {
        return;
    }
    if (on) {
        if (s_rec) {
            gfx_frame_submit();   /* not expected at a frame start; keep the order */
        }
        /* the decoder thread held the context for the inline frames: give it up */
        gfx_sdl_release_context();
        if (!s_running) {
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr, 4u << 20);   /* Mesa's GLSL compiler recurses deeply */
            if (pthread_create(&s_th, &attr, replay_thread, nullptr) != 0) {
                pthread_attr_destroy(&attr);
                sysLogPrintf(LOG_ERROR, "F3D: replay thread failed to start; replaying inline");
                gfx_sdl_make_context_current();
                return;
            }
            pthread_attr_destroy(&attr);
            s_running = true;
            sysLogPrintf(LOG_NOTE, "F3D: replay thread started (D969)");
        }
        s_thr = true;
        s_rec = true;
        frame_reset();
        return;
    }
    /* back to inline (D578 interpolation turned on): drain, move the context back */
    if (s_fr->ncmds > 0) {
        handoff(false, -1);
    }
    post_and_wait(JOB_RELEASE);
    pthread_mutex_lock(&s_tl);
    take_refill_locked();
    pthread_mutex_unlock(&s_tl);
    s_thr = false;
    s_rec = false;
    frame_reset();
    gfx_sdl_make_context_current();
}

void gfx_frame_thread_set_token(int64_t token) {
    s_token = token;
    s_token_set = true;
}

bool gfx_frame_thread_token_left(int64_t* token) {
    if (!s_token_set) {
        return false;
    }
    *token = s_token;
    s_token_set = false;
    return true;
}

void gfx_frame_thread_present(void) {
    if (!s_thr) {
        return;
    }
    const int64_t tok = s_token_set ? s_token : -1;
    s_token_set = false;
    handoff(true, tok);
}

int64_t gfx_frame_thread_last_wait_us(void) {
    return (int64_t)(s_last_wait_ns / 1000);
}

void gfx_frame_thread_park(void) {
    if (!s_running) {
        return;
    }
    /* what is left of a frame is dropped: the quit parks at a frame boundary */
    post_and_wait(JOB_PARK);
    pthread_join(s_th, nullptr);
    s_running = false;
    s_thr = false;
    s_rec = false;
    sysLogPrintf(LOG_NOTE, "F3D: replay thread parked and joined (D969)");
}

void gfx_frame_call(void (*fn)(int), int arg) {
    if (!s_rec) {
        fn(arg);
        return;
    }
    GfxFrameCmd& c = push_cmd(GFC_CALL);
    c.fn = fn;
    c.a = arg;
}

void gfx_frame_frame_count_rewind(void) {
    s_frame_count--;
}

void gfx_frame_perf_report(double frames) {
    if (!perf_on() || frames <= 0) {
        return;
    }
    const double n = frames;
    const double bodies = s_st_frames > 0 ? (double)s_st_frames : 1.0;
    char line[512];
    snprintf(line, sizeof(line),
             "PERFSTAT frame: replay=%.2fms/f bodies=%.2f/f cmds=%.0f batches=%.0f states=%.0f vtx=%.0fKiB upload=%.1fKiB (%.1f uploads) per body | GL in walk: compiles=%llu texnames=%llu | flushes outside a body=%llu\n",
             (double)s_st_replay_ns / 1.0e6 / n, (double)s_st_frames / n, s_st_cmds / bodies, s_st_batches / bodies,
             s_st_states / bodies, s_st_vbytes / 1024.0 / bodies, s_st_ubytes / 1024.0 / bodies, s_st_uploads / bodies,
             (unsigned long long)s_st_gl_compiles, (unsigned long long)s_st_gl_texnames,
             (unsigned long long)s_st_flush_outside);
    fputs(line, stderr);
    sysLogPrintf(LOG_NOTE, "%s", line);
    if (s_running) {
        snprintf(line, sizeof(line),
                 "PERFSTAT framethread: %s handoffs=%.2f/f | decoder waits for the GL thread %.2fms/f, in sync GL calls %.2fms/f (%llu calls) | GL thread idle %.2fms/f, present %.2fms/f (events + swap), programs compiled there %llu\n",
                 s_thr ? "on" : "off(inline)", (double)s_st_handoffs / n, (double)s_st_dec_wait_ns / 1.0e6 / n,
                 (double)s_st_sync_ns / 1.0e6 / n, (unsigned long long)s_st_syncs,
                 (double)s_st_rep_idle_ns / 1.0e6 / n, (double)s_st_present_ns / 1.0e6 / n,
                 (unsigned long long)s_st_rep_compiles);
        fputs(line, stderr);
        sysLogPrintf(LOG_NOTE, "%s", line);
    }
    s_st_dec_wait_ns = s_st_sync_ns = s_st_rep_idle_ns = s_st_present_ns = s_st_syncs = s_st_rep_compiles = s_st_handoffs = 0;
    s_st_frames = s_st_cmds = s_st_batches = s_st_states = s_st_vbytes = s_st_ubytes = s_st_uploads = 0;
    s_st_replay_ns = s_st_gl_compiles = s_st_gl_texnames = s_st_flush_outside = 0;
}

}   // extern "C"
