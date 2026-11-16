#ifndef GFX_RENDERING_API_H
#define GFX_RENDERING_API_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct ShaderProgram;

struct GfxClipParameters {
    bool z_is_from_0_to_1;
    bool invert_y;
};

/* Trilinear option (playtest 2026-10-03) */
enum FilteringMode { FILTER_NONE, FILTER_LINEAR, FILTER_THREE_POINT, FILTER_TRILINEAR };
enum MipmapFilteringMode { MIPMAP_DISABLED, MIPMAP_NEAREST, MIPMAP_LINEAR };

/* D578: frame-interpolation present slots (gfx_opengl.cpp / gfx_pc.cpp), a
 * ring: a game frame's passes go after the slots still waiting to be shown
 * (D583). 8 = two game frames of 4 presents (240 Hz); allocated on first use. */
#define GFX_INTERP_SLOTS 8

/* Issue #10 step B (GE_MESHGPU): a static vertex buffer drawn with the vertex
 * transform on the GPU. Vertices use the bound program's layout, except that
 * the position attribute holds (model x, y, z, matrix index). */
#define GFX_MESH_MAX_MTX 32
#define GFX_MESH_MAX_INPUTS 8
struct GfxMeshDraw {
    uint32_t byte_offset;      /* first vertex of the run in the mesh buffer */
    uint32_t nverts;
    uint32_t nmtx;
    uint8_t cull;              /* 0 none, 1 back, 2 front (N64 sense, y up) */
    bool invert_y;             /* the target is drawn y-flipped: front faces are clockwise */
    bool fog_from_z;           /* fog alpha from clip z/w (RSP G_FOG) instead of the attribute */
    float fog_color[4];        /* live RDP fog RGB (kept out of the baked vertices) */
    float fog_mul, fog_off;
    float lod_mask[GFX_MESH_MAX_INPUTS][4];   /* 1: that input component is the LOD fraction (from w) */
    float env_in[GFX_MESH_MAX_INPUTS][4];     /* live RDP env colour as each input reads it ... */
    float env_mask[GFX_MESH_MAX_INPUTS][4];   /* ... and 1 where that component comes from it */
    uint32_t frame_count;      /* uniform values latched with the program (as load_shader_with) */
    float noise_scale;
    bool tpf0, tpf1;
    float mtx[GFX_MESH_MAX_MTX][16];          /* row-vector clip matrices: clip = (x y z 1) * m */
};

struct GfxRenderingAPI {
    const char* (*get_name)(void);
    int (*get_max_texture_size)(void);
    struct GfxClipParameters (*get_clip_parameters)(void);
    void (*unload_shader)(struct ShaderProgram* old_prg);
    void (*load_shader)(struct ShaderProgram* new_prg);
    struct ShaderProgram* (*create_and_load_new_shader)(uint64_t shader_id0, uint32_t shader_id1);
    struct ShaderProgram* (*lookup_shader)(uint64_t shader_id0, uint32_t shader_id1);
    void (*shader_get_info)(struct ShaderProgram* prg, uint8_t* num_inputs, bool used_textures[2]);
    void (*clear_shaders)(void);
    uint32_t (*new_texture)(void);
    void (*select_texture)(int tile, uint32_t texture_id, bool linear_filter);
    void (*upload_texture)(const uint8_t* rgba32_buf, uint32_t width, uint32_t height, bool gen_mipmaps);
    void (*set_sampler_parameters)(int sampler, bool linear_filter, uint32_t cms, uint32_t cmt, bool mipmaps);
    void (*set_depth_mode)(bool depth_test, bool depth_update, bool depth_compare, bool depth_source_prim, uint16_t zmode);
    void (*set_depth_range)(float znear, float zfar);
    void (*set_viewport)(int x, int y, int width, int height);
    void (*set_scissor)(int x, int y, int width, int height);
    void (*set_use_alpha)(bool use_alpha, bool modulate);
    void (*draw_triangles)(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris);
    void (*init)(void);
    void (*on_resize)(void);
    void (*start_frame)(void);
    void (*end_frame)(void);
    void (*finish_render)(void);
    int (*create_framebuffer)();
    void (*update_framebuffer_parameters)(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                          bool opengl_invert_y, bool render_target, bool has_depth_buffer,
                                          bool can_extract_depth);
    bool (*start_draw_to_framebuffer)(int fb_id, float noise_scale);
    void (*copy_framebuffer)(int fb_dst, int fb_src, int left, int top, bool flip_y, bool use_back);
    void (*clear_framebuffer)(bool clear_color, bool clear_depth);
    /* D579: scissored clear of one axis-aligned region of the BOUND target
     * (x, y in GL bottom-up target pixels, as for set_scissor). */
    void (*clear_region)(bool clear_color, bool clear_depth, int x, int y, int w, int h);
    void (*resolve_msaa_color_buffer)(int fb_id_target, int fb_id_source);
    void* (*get_framebuffer_texture_id)(int fb_id);
    void (*select_texture_fb)(int fb_id);
    void (*delete_texture)(uint32_t texID);
    void (*set_texture_filter)(enum FilteringMode mode);
    enum FilteringMode (*get_texture_filter)(void);
    void (*set_mipmap_filter)(enum MipmapFilteringMode mode);
	void (*set_anisotropy_level)(int);
	int (*get_max_anisotropy_level)(void);
    /* D965 (GE_GLSTREAM): persistent vertex ring. Returns where the next
     * batch's vertices (up to max_floats floats, laid out at the bound
     * shader's stride) are to be written in place, or NULL when streaming is
     * off (the caller then fills its own buffer). The pointer is passed back
     * to draw_triangles as buf_vbo, which then draws from the ring without a
     * copy. At most one stream batch is open at a time. */
    float* (*stream_begin)(size_t max_floats);
    /* D968: load_shader with explicit uniform values (frame counter, noise scale
     * as the backend stores it = 1 / start_draw_to_framebuffer's argument, and
     * the two three-point flags), as latched when the decoder loaded it: the
     * frame replay loads programs later than the decoder did. */
    void (*load_shader_with)(struct ShaderProgram* prg, uint32_t frame_count, float noise_scale, bool tpf0, bool tpf1);
    /* Issue #10 step B: static meshes. mesh_create returns a backend buffer
     * name; mesh_draw draws one run with the GPU-transform twin of the bound
     * program and leaves the bound program, vertex array and culling as they
     * were. */
    uint32_t (*mesh_create)(const float* data, size_t nfloats);
    void (*mesh_delete)(uint32_t name);
    void (*mesh_draw)(uint32_t name, const struct GfxMeshDraw* d);
    /* Whether the program (id0, id1) has a GPU-transform twin. Pure and safe
     * from the decoder thread (the frame recorder passes it through). Null:
     * every program does (GL compiles the twin on demand). */
    bool (*mesh_supported)(uint64_t id0, uint32_t id1);
};

#endif
