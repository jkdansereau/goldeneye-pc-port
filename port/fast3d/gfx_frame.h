#ifndef GFX_FRAME_H
#define GFX_FRAME_H

/* D968: the neutral per-frame structure between the Fast3D display-list decoder
 * (gfx_pc.cpp) and the renderer.
 *
 * While a frame body runs (gfx_frame_begin .. gfx_frame_submit), gfx_pc's
 * gfx_rapi is the recorder below, not the GL backend: every rendering-API call
 * the display-list walk makes is turned into data -- render state into the
 * recorder's state (one resolved GfxFrameState per run of batches), vertices
 * into the frame's vertex array, framebuffer / clear / copy / texture-upload
 * work into ordered commands. Nothing reaches GL during the walk (exceptions in
 * gfx_frame.cpp: a shader program compiled on first use of a combiner, and a
 * texture name generated when the pre-generated pool runs dry; both counted in
 * PERFSTAT frame). gfx_frame_submit then replays the frame against the real
 * backend (GL today) in one go.
 *
 * Designed so that later steps do not need a new interface:
 *  - GPU transform: a batch is a range of the frame's float array plus the
 *    resolved state; a model-space vertex format + matrix table can be added
 *    as another per-batch layout without changing commands.
 *  - threads: a GfxFrame owns all its data (vertex floats, upload bytes,
 *    states, commands); nothing points into the decoder's memory, so frames
 *    are double-buffered and replayed on another core (D969, GE_FRAMETHREAD).
 *    Shader programs are recorder handles (combiner ids + the CC features the
 *    decoder needs); the GL thread compiles them.
 *  - another backend (deko3d): GfxFrameState is the complete state of a draw
 *    in API-neutral values; the GL replay's version numbers are an extra that
 *    a new backend may ignore. */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "gfx_rendering_api.h"

struct GfxWindowManagerAPI;

#ifdef __cplusplus
extern "C" {
#endif

enum GfxFrameUnitKind { GFX_UNIT_NONE = 0, GFX_UNIT_TEXTURE = 1, GFX_UNIT_FRAMEBUFFER = 2 };

/* What a texture unit samples. */
struct GfxFrameUnit {
    uint8_t kind;      /* GfxFrameUnitKind */
    bool linear;       /* the select's linear flag (read by the three-point filter) */
    uint32_t id;       /* texture id (backend texture name) or framebuffer id */
};

/* Sampling parameters of a texture (N64 terms: filter, clamp/mirror/wrap per axis). */
struct GfxFrameSampler {
    bool set;          /* false: never set since the texture was created */
    bool linear;
    bool mipmaps;
    uint8_t cms, cmt;  /* G_TX_* */
};

/* Uniform values a shader program is loaded with. GL latches them at load time,
 * so they are part of the program state, captured when the decoder loads it. */
struct GfxFrameUniforms {
    uint32_t frame_count;
    float noise_scale;  /* backend form: 1 / the noise_scale argument of start_draw_to_framebuffer */
    bool three_point[2];
};

struct GfxFrameDepth {
    bool test, update, compare, source_prim;
    uint16_t zmode;
};

/* Complete render state of a run of batches. */
struct GfxFrameState {
    struct ShaderProgram* prg;     /* D969: the recorder's handle of the program; the GL thread maps it to (and creates) the backend program */
    uint64_t shader_id0;           /* combiner ids of prg (0/0 when unknown) */
    uint32_t shader_id1;
    struct GfxFrameUniforms uniforms;
    struct GfxFrameUnit unit[2];
    struct GfxFrameSampler sampler[2];   /* sampling parameters of unit[i]'s texture at the draw */
    struct GfxFrameDepth depth;
    int viewport[4];               /* x, y, w, h (GL bottom-up pixels of the target) */
    int scissor[4];
    bool use_alpha, modulate;      /* blend on; multiply blend (DST_COLOR, ZERO) */
    int target_fb;                 /* framebuffer the draw goes to (0 = window) */
    /* GL replay: bumped each time the decoder set the group, so the replay sets
     * exactly what the immediate path set (state a framebuffer command
     * clobbered stays clobbered until the decoder sets it again). */
    uint32_t ver_prg, ver_unit[2], ver_depth, ver_viewport, ver_scissor, ver_blend;
};

/* A draw: ntris triangles, vertices laid out at the program's stride, starting
 * nfloats-long at verts[first]. */
struct GfxFrameBatch {
    uint32_t state;      /* index into GfxFrame.states */
    uint32_t first;      /* float offset into GfxFrame.verts */
    uint32_t nfloats;
    uint32_t ntris;
};

enum GfxFrameCmdType {
    GFC_DRAW,            /* a: batch index */
    GFC_UPLOAD,          /* texture image: unit, tex, w, h, mip, data at bytes[off] */
    GFC_SAMPLER,         /* sampling parameters of the texture on unit */
    GFC_DELETE_TEX,
    GFC_START_FRAME,
    GFC_END_FRAME,
    GFC_UPDATE_FB,       /* update_framebuffer_parameters */
    GFC_START_FB,        /* start_draw_to_framebuffer(a, f) */
    GFC_CLEAR,           /* clear_framebuffer(color, depth) */
    GFC_CLEAR_REGION,
    GFC_COPY_FB,
    GFC_RESOLVE,
    GFC_CALL,            /* fn(a): a backend pass outside the API (FXAA) */
    GFC_CREATE_SHADER,   /* D969: compile the program p (a recorder shader handle) on the GL thread */
    GFC_CLEAR_SHADERS,   /* D969: delete every program (settings change) */
    GFC_SETTING,         /* D969: a = 0 texture filter / 1 mipmap filter / 2 anisotropy, b = value */
    GFC_MESH_CREATE,     /* issue #10: a = mesh id, b = float count, data at bytes[off] */
    GFC_MESH_DELETE,     /* issue #10: a = mesh id */
    GFC_DRAW_MESH,       /* issue #10: a = state index, b = mesh id, draw parameters at bytes[off] (c = matrix count) */
};

struct GfxFrameCmd {
    uint8_t type;
    uint8_t unit;
    bool b0, b1, b2, b3;
    struct GfxFrameUnit ref;   /* UPLOAD / SAMPLER: the unit binding the immediate call wrote to */
    uint32_t ref_ver;
    int32_t a, b, c, d, e, f;
    float fl;
    uint64_t off;
    struct GfxFrameSampler smp;
    void (*fn)(int);
    void* p;
};

struct GfxFrame {
    struct GfxFrameCmd* cmds;     size_t ncmds, cap_cmds;
    struct GfxFrameBatch* batches; size_t nbatches, cap_batches;
    struct GfxFrameState* states; size_t nstates, cap_states;
    float* verts;                 size_t nverts, cap_verts;   /* floats */
    uint8_t* bytes;               size_t nbytes, cap_bytes;   /* texture upload data */
    uint32_t tris;
};

/* gfx_pc: wrap the backend (once, at init); returns the API gfx_pc calls. */
struct GfxRenderingAPI* gfx_frame_install(struct GfxRenderingAPI* backend);
/* Start recording a frame body / replay it on the backend and stop recording. */
void gfx_frame_begin(void);
void gfx_frame_submit(void);
bool gfx_frame_recording(void);
/* Record a call of fn(arg) at this point of the frame (a backend pass that is not
 * part of the rendering API, e.g. the Switch FXAA pass). Immediate when not recording. */
void gfx_frame_call(void (*fn)(int), int arg);
/* The backend's frame counter went back one (D578 interpolation passes). */
void gfx_frame_frame_count_rewind(void);
/* PERFSTAT: one line, then reset (frames = frames since the last report). */
void gfx_frame_perf_report(double frames);

/* ---- D969: replay on its own thread (GE_FRAMETHREAD) ----
 * The render worker stays the decoder; a second thread owns the GL context and
 * replays frame N while the decoder records frame N+1 into the other GfxFrame.
 * While active, the recorder records from one hand-off to the next (also the
 * calls gfx_start_frame and the video layer make between frame bodies), and
 * nothing on the decoder thread touches GL: window events, the swap, shader
 * compiles, texture-name generation and settings all run on the GL thread. */
void gfx_frame_set_wapi(struct GfxWindowManagerAPI* wapi);   /* gfx_init */
/* GE_FRAMETHREAD resolved once (default: on on the Switch, off elsewhere). */
bool gfx_frame_thread_wanted(void);
bool gfx_frame_thread_active(void);
/* Render worker, at the start of a frame task: start / stop pipelining (the GL
 * context moves to / from the replay thread; stopping drains it first). */
void gfx_frame_thread_activate(bool on);
/* INPUTLAT token of the frame being decoded (libultra.c portLatBegin). */
void gfx_frame_thread_set_token(int64_t token);
/* After the frame: true + the token when it was not handed to the replay
 * thread (dropped frame), so the caller closes it itself. */
bool gfx_frame_thread_token_left(int64_t* token);
/* gfx_end_frame: hand the recorded frame to the replay thread, which replays
 * it, swaps and calls the after-present hook. Waits until the replay thread
 * has finished the previous frame (two GfxFrames: one replaying, one recording). */
void gfx_frame_thread_present(void);
/* How long the last hand-off waited for the GL thread (us). */
int64_t gfx_frame_thread_last_wait_us(void);
/* Quit: drain, glFinish + unbind the context on the GL thread, join it. */
void gfx_frame_thread_park(void);
/* libultra.c: thread entry (core pinning, load probe) and after-present hook
 * (swap returned: INPUTLAT end, D600 tick lock, heartbeat). */
void gfx_frame_thread_set_hooks(void (*enter)(void), void (*after_present)(int64_t token));

#ifdef __cplusplus
}
#endif

#endif
