/* gfx_meshcache.inc -- display-list mesh cache, step A (issue #10; GE_MESHCACHE, on by default on the Switch).
 *
 * Included by gfx_pc.cpp right after the gfx_sp_tri1 helpers (gfx_tri_reject /
 * gfx_tri_state / gfx_tri_nearclip / gfx_tri_emit, struct TriState) and before
 * gfx_sp_tri1 itself: it needs the decoder's static state (rsp, rdp,
 * rendering_state, the texture cache).
 *
 * What it does. A display list pushed with G_DL is compiled, the first time it
 * is walked, into a compact program:
 *   - the RSP commands that feed the vertex pipeline (G_MTX, G_POPMTX, G_VTX,
 *     geometry mode, G_TEXTURE, fog/light MOVEWORD/MOVEMEM) are kept and are
 *     re-executed on every replay through the normal functions, so matrices
 *     (incl. the D578/D955 hooks), vertex positions, colours (lighting, shot-out
 *     lights darkening room vertices in place), fog and texture coordinates
 *     stay live;
 *   - everything the RDP side resolves per triangle (texture loads and tiles,
 *     the combiner and its shader, depth/blend, sampler, per-batch emission
 *     constants) collapses into "segments" of resolved state, applied lazily
 *     at the first triangle that survives the reject/cull, as gfx_sp_tri1 does.
 * Per replayed triangle only the reject/cull and the camera-plane near clip
 * (the same helpers as the classic path) and a lean emission remain: no
 * combiner lookup, no texture import, no per-corner divisions.
 *
 * Exactness. Positions, fog, colours, texture coordinates come from the same
 * gfx_sp_vertex; the emission repeats gfx_tri_emit's arithmetic in the same
 * order (x / 255 through a table of the same quotients, divisions by a power
 * of two as multiplications by the exact reciprocal). So the vertices sent
 * are the classic ones. Batches can merge where the classic path flushed
 * around a texture re-import of the same texture (same pixels).
 *
 * Validity of an entry, checked at every hit:
 *   - the bytes of every display-list range walked and of every TLUT source,
 *     and the first/last 32 bytes of CI8 textures (as import_texture keys them);
 *   - the segment bases used for control flow and texture addresses (G_DL,
 *     G_SETTIMG); segments used only by G_MTX/G_VTX/MOVEMEM resolve live, so
 *     one entry serves every object instance (seg 3 = its joint matrices);
 *   - the state the list inherits from its caller and reads before writing it
 *     (render modes, colours, geometry-mode bits, tile descriptors, a texture
 *     binding it never re-imports), plus clip parameters and settings.
 * After a replay the RDP state the list wrote is set to its exit value.
 *
 * Not cached (classic path, counted per reason): rectangles, scissor/viewport,
 * framebuffer commands, segment changes inside the list, texture cache
 * invalidation, a texture whose TMEM / TLUT / image pointer comes from the
 * caller, per-frame dynamic textures, lists without triangles, frame
 * interpolation passes (D578; never on the Switch). */

static inline void* seg_addr(uintptr_t w1);
static void gfx_run_dl(Gfx* cmd);
static void gfx_sp_movemem(uint8_t index, uint8_t offset, const void* data);
static void gfx_sp_moveword(uint8_t index, uint16_t offset, uintptr_t data);
static void gfx_sp_texture(uint16_t sc, uint16_t tc, uint8_t level, uint8_t tile, uint8_t on);
static void gfx_sp_geometry_mode(uint32_t clear, uint32_t set);
static void gfx_sp_extra_geometry_mode(uint32_t clear, uint32_t set);
static uint64_t gfx_perf_now_ns(void);
static void gfx_dp_set_env_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
static void gfx_dp_set_prim_color(uint8_t m, uint8_t l, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
static void gfx_dp_set_fog_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
static void gfx_dp_set_grayscale_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

/* ------------------------------------------------------------------ data */

enum McOpKind : uint8_t {
    MCO_MTX, MCO_POPMTX, MCO_VTX, MCO_GEOM, MCO_XGEOM, MCO_TEXTURE, MCO_MOVEWORD, MCO_MOVEMEM, MCO_SEG, MCO_TRI, MCO_COLOR
};

struct McOp {
    uint8_t k, a, b, c;
    uint32_t x;
    uintptr_t p;
};

/* Resolved state of a run of triangles. POD, memset before fill (memcmp'd). */
struct McSeg {
    uint8_t depth_mode;
    bool use_alpha, use_modulate;
    bool unit_on[2];       /* the combiner uses the unit (gfx_tri_state touched it) */
    bool has_node[2];      /* a texture was bound on it */
    bool s_linear[2];
    uint8_t s_cms[2], s_cmt[2];
    bool s_lod;            /* rdp.tex_lod: set_sampler_parameters' mipmap argument */
    TextureCacheKey key[2];
    TriState ts;           /* emission inputs (comb, prg, tm, sizes, wraps, num_inputs, clip) */
    /* emission constants read from rdp in gfx_tri_emit */
    float sh_mul_s[2], sh_mul_t[2];   /* 1 (no shift), exact 2^-n or 2^n */
    float uls4[2], ult4[2];           /* tile uls / 4.0f, ult / 4.0f */
    float inv_w[2], inv_h[2];         /* exact reciprocal of tex_width/height, 0 = divide */
    float clamp_s[2], clamp_t[2];     /* (tex_width2 - 0.5f) / tex_width ... */
    bool persp, linear_filt, tl_lod;
    /* colours are not here: read live from rdp at emission (MCO_COLOR) */
    uint8_t colour_use;   /* step B: which live colours the emission reads (MC_CU_*) */
};
enum { MC_CU_PRIM = 1, MC_CU_ENV = 2, MC_CU_FOG = 4, MC_CU_GRAY = 8, MC_CU_LODF = 16 };

/* Fingerprint of the inherited state (fields read before written). */
enum {
    MCW_COMB = 1 << 0, MCW_ENV = 1 << 1, MCW_PRIM = 1 << 2, MCW_FOG = 1 << 3, MCW_GRAY = 1 << 4,
    MCW_GRAYF = 1 << 5, MCW_FILL = 1 << 6, MCW_FTILE = 1 << 7, MCW_TEXLOD = 1 << 8, MCW_TIMG = 1 << 9,
    MCW_PAL = 1 << 10,
};

struct McTile {   /* a copy of one rdp.texture_tile entry */
    uint8_t raw[sizeof(rdp.texture_tile[0])];
};

struct McScal {
    uint32_t oml, omh, geom, xgeom;
    uint64_t comb;
    struct RGBA env, prim, fog, gray, fill;
    uint8_t prim_lod, tex_min_lod, grayscale, first_tile, tex_lod, tex_detail;
    uint32_t palette_fmt;
};

struct McRange {
    const uint8_t* p;
    uint32_t off, len;   /* bytes in McEntry::bytes */
};

struct McTexProbe {     /* CI8 image fingerprint, as import_texture computes it */
    const uint8_t* addr;
    uint32_t size;
    uint32_t fp;
};

struct McEntry {
    /* key */
    uint16_t seg_mask;            /* keyed segments */
    uintptr_t seg_val[16];
    /* inherited state */
    uint32_t in_flags;            /* MCW_* read before written */
    uint32_t in_oml_mask, in_omh_mask, in_geom_mask, in_xgeom_mask;
    McScal in;                    /* entry values (masked by the above) */
    uint8_t in_tiles;             /* tile descriptors read before written */
    McTile in_tile[8];
    bool in_bind[2];              /* a unit's binding is inherited */
    TextureCacheNode* in_node[2];
    uint8_t settings;
    struct GfxClipParameters clip;
    /* program */
    std::vector<McOp> ops;
    std::vector<McSeg> segs;
    uint32_t ntris;
    /* verification */
    std::vector<McRange> ranges;
    std::vector<uint8_t> bytes;
    std::vector<McTexProbe> probes;
    /* exit state */
    uint32_t w_flags, w_oml, w_omh;
    uint8_t w_tile_a, w_tile_b;   /* SETTILE part / size part written */
    McScal out;
    McTile out_tile[8];
    std::vector<std::pair<uint16_t, LoadedTexture>> out_tmem;
    uint16_t out_palette[256];
    uint32_t pal_mask[8];         /* D985: palette entries the list's G_LOADTLUTs write */
    const uint8_t* out_palette_addrs[2];
    uint32_t out_palette_hash;
    decltype(rdp.texture_to_load) out_timg;
    bool tc_touched[2], out_tc[2];
    /* replay cache of texture nodes */
    std::vector<TextureCacheNode*> nodes;   /* 2 per segment */
    uint32_t nodes_gen;
    /* step B (GE_MESHGPU): static GPU meshes of this program, one per vertex
     * source (per-instance vertex arrays differ) */
    std::vector<struct McGpu*> gpu;
    const char* gpu_bad;          /* non-null: never drawn on the GPU (reason) */
    uint32_t hits;                /* CPU replays so far (a bake waits for a few) */
    uint32_t last_used;
};

struct McSlot {
    std::vector<McEntry*> v;      /* variants (different inherited state / keyed segments) */
    uint32_t refuse_until;
    uint32_t stale_frame0;
    uint16_t stales;
    uint32_t last_used;
};

/* ---- step B: GPU meshes (GE_MESHGPU) ---- */
struct McRun {                /* consecutive triangles drawn with one segment's state */
    uint16_t seg;
    uint8_t cull;             /* 0 none, 1 back, 2 front, 3 both (not drawn) */
    bool fog_from_z;
    uint32_t byte_offset;     /* in the mesh buffer */
    uint32_t nverts;
    float lod_mask[GFX_MESH_MAX_INPUTS][4];
    /* per input, which components read the env colour live at the list's entry
     * (MC_ENV_*); 0 once the list sets its own env colour (then it is baked) */
    uint8_t env_sel[GFX_MESH_MAX_INPUTS];
};
enum { MC_ENV_RGB = 1, MC_ENV_AAA = 2, MC_ENV_A = 4 };

struct McGpu {
    uint64_t key;             /* vertex source pointers + bytes */
    uint64_t ptrkey;          /* pointers only (PERFSTAT: tells moved from rewritten vertices) */
    uint64_t groups[8];       /* signature per group (PERFSTAT: which state changed) */
    uint64_t colour_sig;      /* colours baked into the vertices */
    uint32_t mesh_id;         /* rendering API mesh */
    uint8_t nslots;           /* matrix slots */
    std::vector<McRun> runs;
    std::vector<float> box;   /* per slot: min xyz, max xyz (model space); empty slot: min > max */
    uint32_t last_used;
};

static std::vector<uint32_t> s_mc_gpu_dead;   /* mesh ids to delete at the next frame start */

/* capture state */
struct McCap {
    McEntry* e;
    const char* refused;          /* non-null: abandon at the end */
    uint8_t refused_op;
    bool any_tri;
    McScal entry;                 /* snapshot at entry */
    McTile entry_tile[8];
    TextureCacheNode* entry_node[2];
    bool entry_tc[2];
    bool imported[2];
    /* written so far */
    uint32_t w_flags, w_oml, w_omh, w_geom, w_xgeom;
    uint8_t w_tile_a, w_tile_b;
    std::vector<uint16_t> w_tmem;
    bool first_tri_done;
    Gfx* range_end;               /* one past the last command of the open range */
    int cur_seg;
};

static std::unordered_map<uintptr_t, McSlot> s_mc_map;
static McCap* s_mc_cap = nullptr;
static uint32_t s_mc_frame = 1;
static float s_mc_lut255[256];

/* counters (GE_PERFSTAT) */
enum {
    MCR_REFUSED_SKIP, MCR_INTERP, MCR_STALE, MCR_FP, MCR_SEGS, MCR_TEXGONE, MCR_NEW, MCR_VARIANT,
    MCR_COUNT
};
static const char* const s_mc_reason_name[MCR_COUNT] = {
    "refused-skip", "interp", "stale", "inherited", "segments", "tex-evicted", "new", "variant"
};
static uint64_t s_mc_cnt_try = 0, s_mc_cnt_hit = 0, s_mc_cnt_hit_tris = 0, s_mc_cnt_cap = 0, s_mc_cnt_cap_ok = 0;
static uint64_t s_mc_cnt_reason[MCR_COUNT];
static uint64_t s_mc_ns_replay = 0, s_mc_ns_verify = 0, s_mc_ns_cap = 0;
/* PERFSTAT replay split, measured on one replay in 8 (clock reads per step) */
enum { MCT_MTX, MCT_VTX, MCT_OTHER, MCT_REJECT, MCT_SEG, MCT_CLIP, MCT_EMIT, MCT_EXIT, MCT_COUNT };
static const char* const s_mc_t_name[MCT_COUNT] = { "mtx", "vtx", "other-ops", "reject+cull", "seg-apply", "nearclip", "emit", "exit" };
static uint64_t s_mc_t_ns[MCT_COUNT];
static uint64_t s_mc_t_tris = 0, s_mc_t_rej = 0, s_mc_t_emitted = 0, s_mc_t_replays = 0, s_mc_split_n = 0;
static std::map<std::string, uint64_t> s_mc_refusals;   /* reason -> count, per report */
static std::map<std::string, uint64_t> s_mc_stale_ops;  /* "op 0xNN" -> count */
static uint64_t s_mc_g_replays = 0, s_mc_g_runs = 0, s_mc_g_tris = 0, s_mc_g_bakes = 0, s_mc_g_bake_bytes = 0;
static uint64_t s_mc_g_ns = 0;
static std::map<std::string, uint64_t> s_mc_g_why;     /* why a hit was not drawn on the GPU */
static int s_mc_log_budget = 40;

static int mc_on(void) {
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("GE_MESHCACHE");
#if defined(__SWITCH__)
        on = !(e != NULL && e[0] == '0');           /* the Switch: on unless GE_MESHCACHE=0 */
#else
        on = (e && e[0] && e[0] != '0') ? 1 : 0;    /* elsewhere: opt-in */
#endif
        for (int i = 0; i < 256; i++) s_mc_lut255[i] = i / 255.0f;
        if (on) sysLogPrintf(LOG_NOTE, "GE_MESHCACHE: display-list mesh cache ON (issue #10, step A; GE_MESHCACHE=0 turns it off)");
    }
    return on;
}

static inline uint8_t mc_settings(void) {
    return (uint8_t)((g_wrap_fix ? 1 : 0) | (gfx_detail_textures_enabled ? 2 : 0) | (g_detail_base_tile ? 4 : 0) |
                     (g_fix_mip_textures ? 8 : 0) | (gfx_fog_vertex ? 16 : 0));
}

/* Which segment seg_addr() would read for w1, or -1 (mirrors seg_addr). */
static int mc_seg_of(uintptr_t w1) {
    uintptr_t v = w1;
    if (gfx_w1_is_host(v)) return -1;
    if ((w1 & 0xFF000000) == 0x05000000 && segmentPointers[5]) return 5;
    if (w1 & 1) {
        const int seg = (int)((w1 & 0x0f000000) >> 24);
        if (seg && segmentPointers[seg]) return seg;
    }
    if (w1 < 0x10000000 && ((w1 >> 24) & 0xf)) {
        const int seg = (int)((w1 >> 24) & 0xf);
        if (segmentPointers[seg]) return seg;
    }
    return -1;
}

static void mc_snap_scal(McScal& s) {
    memset(&s, 0, sizeof(s));
    s.oml = rdp.other_mode_l;
    s.omh = rdp.other_mode_h;
    s.geom = rsp.geometry_mode;
    s.xgeom = rsp.extra_geometry_mode;
    s.comb = rdp.combine_mode;
    s.env = rdp.env_color;
    s.prim = rdp.prim_color;
    s.fog = rdp.fog_color;
    s.gray = rdp.grayscale_color;
    s.fill = rdp.fill_color;
    s.prim_lod = rdp.prim_lod_fraction;
    s.tex_min_lod = rdp.tex_min_lod;
    s.grayscale = rdp.grayscale ? 1 : 0;
    s.first_tile = rdp.first_tile_index;
    s.tex_lod = rdp.tex_lod ? 1 : 0;
    s.tex_detail = rdp.tex_detail ? 1 : 0;
    s.palette_fmt = rdp.palette_fmt;
}

/* Bits of other_mode_h Fast3D reads for triangles and texture imports
 * (D967: dither and other unread bits would make every entry miss). */
#define MC_OMH_READ ((3U << G_MDSFT_CYCLETYPE) | (3U << G_MDSFT_TEXTFILT) | (1U << G_MDSFT_TEXTPERSP) | \
                     (1U << G_MDSFT_TEXTLOD) | (3U << G_MDSFT_TEXTDETAIL) | (3U << G_MDSFT_TEXTLUT) |   \
                     (7U << G_MDSFT_TEXTCONV))
#define MC_GEOM_READ (G_ZBUFFER | G_CULL_BOTH)

static void mc_refuse(const char* why, uint8_t op = 0) {
    if (s_mc_cap != nullptr && s_mc_cap->refused == nullptr) {
        s_mc_cap->refused = why;
        s_mc_cap->refused_op = op;
    }
}

static void mc_tmem_written(uint16_t slot) {
    for (uint16_t s : s_mc_cap->w_tmem) {
        if (s == slot) return;
    }
    s_mc_cap->w_tmem.push_back(slot);
}

static bool mc_tmem_was_written(uint16_t slot) {
    for (uint16_t s : s_mc_cap->w_tmem) {
        if (s == slot) return true;
    }
    return false;
}

static void mc_push_op(uint8_t k, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint32_t x = 0, uintptr_t p = 0) {
    McOp op;
    op.k = k; op.a = a; op.b = b; op.c = c; op.x = x; op.p = p;
    s_mc_cap->e->ops.push_back(op);
}

/* A tile read by a triangle or a load: inherited if not written yet. */
static void mc_tile_read(int t, bool part_a, bool part_b) {
    McCap* c = s_mc_cap;
    const uint8_t bit = (uint8_t)(1u << (t & 7));
    if ((part_a && !(c->w_tile_a & bit)) || (part_b && !(c->w_tile_b & bit))) {
        c->e->in_tiles |= bit;
    }
}

/* ------------------------------------------------------- capture: commands */

/* Called by gfx_run_dl for every command walked while capturing, before it
 * is executed. */
static void mc_cap_cmd(Gfx* cmd) {
    McCap* c = s_mc_cap;
    McEntry* e = c->e;
    {
        /* Colours (env / prim / fog / intensity) are per-draw values, not part
         * of the recipe: re-read from the list at every replay (an animated
         * G_SETPRIMCOLOR, D967, or a per-object fade does not invalidate the
         * entry), so their bytes stay out of the verified ranges. */
        const uint32_t op0 = cmd->words.w0 >> 24;
        if (op0 == G_SETENVCOLOR || op0 == G_SETPRIMCOLOR || op0 == G_SETFOGCOLOR || op0 == G_SETINTENSITY_EXT) {
            c->range_end = nullptr;
            if (c->refused == nullptr) mc_push_op(MCO_COLOR, (uint8_t)op0, 0, 0, 0, (uintptr_t)cmd);
            return;
        }
    }
    /* display-list ranges walked (verified byte for byte at every hit) */
    if (cmd != c->range_end || e->ranges.empty()) {
        McRange r;
        r.p = (const uint8_t*)cmd;
        r.off = 0;
        r.len = 0;
        e->ranges.push_back(r);
    }
    e->ranges.back().len += sizeof(Gfx);
    c->range_end = cmd + 1;
    if (c->refused != nullptr) return;

    const uint32_t opcode = cmd->words.w0 >> 24;
    switch (opcode) {
        case G_NOOP:
        case G_RDPPIPESYNC:
        case G_RDPFULLSYNC:
        case G_RDPLOADSYNC:
        case G_RDPTILESYNC:
        case (uint8_t)G_RDPHALF_1:
        case (uint8_t)G_RDPHALF_2:
        case (uint8_t)G_RDPHALF_CONT:
        case (uint8_t)G_ENDDL:
        case (uint8_t)G_TRI1:
        case (uint8_t)G_TRI4:
            break;
        case G_DL: {
            const int s = mc_seg_of(cmd->words.w1);
            if (s >= 0) e->seg_mask |= (uint16_t)(1u << s);
            break;
        }
        case G_MTX:
            mc_push_op(MCO_MTX, (uint8_t)C0(16, 8), 0, 0, 0, cmd->words.w1);
            break;
        case (uint8_t)G_POPMTX:
            mc_push_op(MCO_POPMTX);
            break;
        case G_VTX:
            mc_push_op(MCO_VTX, (uint8_t)(C0(0, 16) / sizeof(Vtx)), (uint8_t)C0(16, 4), 0, 0, cmd->words.w1);
            break;
        case (uint8_t)G_SETGEOMETRYMODE:
            c->w_geom |= (uint32_t)cmd->words.w1;
            mc_push_op(MCO_GEOM, 0, 0, 0, 0, (uint32_t)cmd->words.w1);
            break;
        case (uint8_t)G_CLEARGEOMETRYMODE:
            c->w_geom |= (uint32_t)cmd->words.w1;
            mc_push_op(MCO_GEOM, 0, 0, 0, (uint32_t)cmd->words.w1, 0);
            break;
        case G_EXTRAGEOMETRYMODE_EXT: {
            const uint32_t clr = ~(uint32_t)C0(0, 24);
            c->w_xgeom |= clr | (uint32_t)cmd->words.w1;
            mc_push_op(MCO_XGEOM, 0, 0, 0, clr, (uint32_t)cmd->words.w1);
            break;
        }
        case (uint8_t)G_TEXTURE:
            c->w_flags |= MCW_FTILE;
            e->tc_touched[0] = e->tc_touched[1] = true;
            mc_push_op(MCO_TEXTURE, 0, 0, 0, (uint32_t)cmd->words.w0, cmd->words.w1);
            break;
        case (uint8_t)G_MOVEWORD:
            if (C0(0, 8) == G_MW_SEGMENT) {
                mc_refuse("segment-in-list", (uint8_t)opcode);
            } else {
                mc_push_op(MCO_MOVEWORD, (uint8_t)C0(0, 8), 0, 0, (uint32_t)C0(8, 16), cmd->words.w1);
            }
            break;
        case G_MOVEMEM:
            if (C0(16, 8) == G_MV_VIEWPORT) {
                mc_refuse("viewport", (uint8_t)opcode);
            } else {
                mc_push_op(MCO_MOVEMEM, (uint8_t)C0(16, 8), 0, 0, 0, cmd->words.w1);
            }
            break;
        case (uint8_t)G_SETOTHERMODE_L: {
            const uint32_t sh = C0(8, 8), n = C0(0, 8);
            const uint64_t m = (((uint64_t)1 << n) - 1) << sh;
            c->w_oml |= (uint32_t)m;
            c->w_omh |= (uint32_t)(m >> 32);
            c->w_flags |= MCW_TEXLOD;   /* gfx_sp_set_other_mode re-derives tex_lod / detail / palette_fmt */
            break;
        }
        case (uint8_t)G_SETOTHERMODE_H: {
            const uint32_t sh = C0(8, 8) + 32, n = C0(0, 8);
            const uint64_t m = (((uint64_t)1 << n) - 1) << sh;
            c->w_oml |= (uint32_t)m;
            c->w_omh |= (uint32_t)(m >> 32);
            c->w_flags |= MCW_TEXLOD;
            break;
        }
        case G_RDPSETOTHERMODE:
            c->w_oml = 0xFFFFFFFFu;
            c->w_omh = 0xFFFFFFFFu;   /* tex_lod etc. are NOT re-derived here */
            break;
        case G_SETCOMBINE:
            c->w_flags |= MCW_COMB;
            break;
        case G_SETFILLCOLOR:
            c->w_flags |= MCW_FILL;
            break;
        case G_SETGRAYSCALE_EXT:
            c->w_flags |= MCW_GRAYF;
            break;
        case G_SETTIMG: {
            const int s = mc_seg_of(cmd->words.w1);
            if (s >= 0) e->seg_mask |= (uint16_t)(1u << s);
            c->w_flags |= MCW_TIMG;
            break;
        }
        case G_SETTILE: {
            const int t = (int)C1(24, 3);
            c->w_tile_a |= (uint8_t)(1u << t);
            e->tc_touched[0] = e->tc_touched[1] = true;
            break;
        }
        case G_SETTILESIZE: {
            const int t = (int)C1(24, 3);
            c->w_tile_b |= (uint8_t)(1u << t);
            e->tc_touched[0] = e->tc_touched[1] = true;
            break;
        }
        case G_LOADBLOCK:
        case G_LOADTILE: {
            const int t = (int)C1(24, 3);
            if (!(c->w_flags & MCW_TIMG)) {
                mc_refuse("timg-inherited", (uint8_t)opcode);
                break;
            }
            mc_tile_read(t, true, false);
            mc_tmem_written(rdp.texture_tile[t].tmem);
            if (opcode == G_LOADTILE) c->w_tile_b |= (uint8_t)(1u << t);
            e->tc_touched[0] = e->tc_touched[1] = true;
            break;
        }
        case G_LOADTLUT: {
            const int t = (int)C1(24, 3);
            if (!(c->w_flags & MCW_TIMG)) {
                mc_refuse("timg-inherited", (uint8_t)opcode);
                break;
            }
            mc_tile_read(t, true, false);
            c->w_tile_b |= (uint8_t)(1u << t);
            c->w_flags |= MCW_PAL;
            e->tc_touched[0] = e->tc_touched[1] = true;
            /* the palette source bytes key every CI texture decoded from it */
            const uint32_t uls = C0(14, 10), ult = C0(2, 10), lrs = C1(14, 10), lrt = C1(2, 10);
            const uint32_t count = (lrs - uls + 1) * (lrt - ult + 1);
            const uint32_t pitch = rdp.texture_to_load.width + 1;
            const uint16_t* base = (const uint16_t*)rdp.texture_to_load.addr + pitch * ult + uls;
            if (count > 256 || !fast3d_ptr_ok(base)) {
                mc_refuse("tlut-range", (uint8_t)opcode);
                break;
            }
            McRange r;
            r.p = (const uint8_t*)base;
            r.off = 0;
            r.len = count * 2;
            e->ranges.push_back(r);
            c->range_end = nullptr;   /* next command opens a new DL range */
            {
                const uint32_t tm = rdp.texture_tile[t].tmem;
                if (tm < 256 || tm - 256 + count > 256) {
                    mc_refuse("tlut-range", (uint8_t)opcode);
                    break;
                }
                for (uint32_t k = tm - 256; k < tm - 256 + count; k++) e->pal_mask[k >> 5] |= 1u << (k & 31);
            }
            break;
        }
        default:
            mc_refuse("opcode", (uint8_t)opcode);
            break;
    }
}

/* ------------------------------------------------------ capture: triangles */

static void mc_fill_seg(McSeg& s, const TriState& ts) {
    memset(&s, 0, sizeof(s));
    s.depth_mode = rendering_state.depth_mode;
    s.use_alpha = ts.blend;   /* D984: the blend state (an A2C draw keeps ts.use_alpha for its vertices) */
    s.use_modulate = ts.use_modulate;
    s.ts = ts;
    s.persp = (rdp.other_mode_h & G_TP_PERSP) != 0;
    s.linear_filt = (rdp.other_mode_h & (3U << G_MDSFT_TEXTFILT)) != G_TF_POINT;
    s.tl_lod = (rdp.other_mode_h & G_TL_LOD) != 0;
    s.s_lod = rdp.tex_lod;
    {
        uint8_t cu = 0;
        for (int j = 0; j < ts.num_inputs; j++) {
            for (int k = 0; k < 1 + (ts.use_alpha ? 1 : 0); k++) {
                switch (ts.comb->shader_input_mapping[k][j]) {
                    case G_CCMUX_PRIMITIVE: case G_CCMUX_PRIMITIVE_ALPHA: cu |= MC_CU_PRIM; break;
                    case G_CCMUX_ENVIRONMENT: case G_CCMUX_ENV_ALPHA: cu |= MC_CU_ENV; break;
                    case G_CCMUX_PRIM_LOD_FRAC: case G_ACMUX_PRIM_LOD_FRAC: cu |= MC_CU_LODF; break;
                }
            }
        }
        if (ts.use_fog) cu |= MC_CU_FOG;
        if (ts.use_grayscale) cu |= MC_CU_GRAY;
        s.colour_use = cu;
    }
    for (int i = 0; i < 2; i++) {
        s.unit_on[i] = ts.comb->used_textures[i];
        const TextureCacheNode* n = rendering_state.textures[i];
        if (s.unit_on[i] && n != nullptr) {
            s.has_node[i] = true;
            memcpy(&s.key[i], &n->first, sizeof(TextureCacheKey));
            s.s_linear[i] = n->second.linear_filter;
            s.s_cms[i] = n->second.cms;
            s.s_cmt[i] = n->second.cmt;
        }
        if (ts.used_textures[i]) {
            const auto& tt = rdp.texture_tile[rdp.first_tile_index + gfx_lod_tile_offset(i)];
            const int shs = tt.shifts, sht = tt.shiftt;
            s.sh_mul_s[i] = shs == 0 ? 1.0f : shs <= 10 ? 1.0f / (float)(1 << shs) : (float)(1 << (16 - shs));
            s.sh_mul_t[i] = sht == 0 ? 1.0f : sht <= 10 ? 1.0f / (float)(1 << sht) : (float)(1 << (16 - sht));
            s.uls4[i] = tt.uls / 4.0f;
            s.ult4[i] = tt.ult / 4.0f;
            const uint32_t tw = ts.tex_width[i], th = ts.tex_height[i];
            s.inv_w[i] = (tw != 0 && (tw & (tw - 1)) == 0) ? 1.0f / (float)tw : 0.0f;
            s.inv_h[i] = (th != 0 && (th & (th - 1)) == 0) ? 1.0f / (float)th : 0.0f;
            s.clamp_s[i] = (ts.tex_width2[i] - 0.5f) / ts.tex_width[i];
            s.clamp_t[i] = (ts.tex_height2[i] - 0.5f) / ts.tex_height[i];
        }
    }
}

/* First triangle of the capture: everything not written so far is inherited. */
static void mc_cap_first_tri(void) {
    McCap* c = s_mc_cap;
    McEntry* e = c->e;
    /* colours are live (MCO_COLOR + read at emission), never inherited state */
    const uint32_t all = MCW_COMB | MCW_GRAYF | MCW_FTILE | MCW_TEXLOD;
    e->in_flags = all & ~c->w_flags;
    e->in_oml_mask = ~c->w_oml;
    e->in_omh_mask = ~c->w_omh & MC_OMH_READ;
    e->in_geom_mask = ~c->w_geom & MC_GEOM_READ;
    e->in_xgeom_mask = ~c->w_xgeom;
    e->in = c->entry;
    c->first_tri_done = true;
}

/* gfx_sp_tri1 while capturing: state first (for every triangle, so that the
 * segments are known even for triangles this frame culls), then the classic
 * reject / near clip / emission. */
static void mc_cap_tri(uint8_t i1, uint8_t i2, uint8_t i3) {
    McCap* c = s_mc_cap;
    McEntry* e = c->e;
    struct LoadedVertex* v1 = &rsp.loaded_vertices[i1];
    struct LoadedVertex* v2 = &rsp.loaded_vertices[i2];
    struct LoadedVertex* v3 = &rsp.loaded_vertices[i3];
    struct LoadedVertex* v_arr[3] = { v1, v2, v3 };

    if (c->refused == nullptr) {
        if (!c->first_tri_done) mc_cap_first_tri();
        const int fi = rdp.first_tile_index;
        mc_tile_read(fi, true, true);
        if (fi + 1 < 8) mc_tile_read(fi + 1, true, true);
    }
    bool tc_before[2] = { rdp.textures_changed[0], rdp.textures_changed[1] };

    TriState ts;
    gfx_tri_state(ts);

    if (c->refused == nullptr) {
        for (int i = 0; i < 2; i++) {
            if (!ts.comb->used_textures[i]) continue;   /* not used by this triangle: binding irrelevant here */
            if (!tc_before[i] && !c->imported[i]) {
                /* D985: drawn with the caller's binding (this list has not
                 * imported on this unit yet). Recorded even after a G_TEXTURE
                 * or tile command: a G_TEXTURE on the tile already current
                 * leaves the binding as it is (the weapon models draw their
                 * first G_TRI4 with the previous part's texture), so the replay
                 * must check the binding, and the entry tile index that kept
                 * that G_TEXTURE from asking for a re-import. */
                e->in_bind[i] = true;
                e->in_node[i] = c->entry_node[i];
                if (e->tc_touched[i]) e->in_flags |= MCW_FTILE;
            }
            if (tc_before[i]) {
                c->imported[i] = true;
                e->tc_touched[i] = true;
                const auto& tt = rdp.texture_tile[rdp.first_tile_index + gfx_lod_tile_offset(i)];
                if (tt.line_size_bytes == 0) { mc_refuse("zero-line-tile"); break; }
                if (!mc_tmem_was_written(tt.tmem)) { mc_refuse("tmem-inherited"); break; }
                mc_tmem_written(tt.tmem);   /* import_texture may fix up the slot */
                if (tt.fmt == G_IM_FMT_CI && !(c->w_flags & MCW_PAL)) { mc_refuse("tlut-inherited"); break; }
            }
            const TextureCacheNode* n = rendering_state.textures[i];
            if (n != nullptr) {
                const uint8_t* v1a = n->first.texture_addr;
                if ((uintptr_t)v1a >= PORT_DRAM_K0_BASE && (uintptr_t)v1a < PORT_DRAM_K0_BASE + PORT_DRAM_SIZE)
                    v1a -= (PORT_DRAM_K0_BASE - PORT_DRAM_V1_BASE);
                if (g_VtxBuffers[0] && v1a >= g_VtxBuffers[0] && v1a < g_VtxBuffers[2]) {
                    mc_refuse("dyn-texture");
                    break;
                }
                if (tc_before[i] && n->first.fmt == G_IM_FMT_CI && n->first.siz == G_IM_SIZ_8b && n->first.size_bytes) {
                    McTexProbe pr;
                    pr.addr = n->first.texture_addr;
                    pr.size = n->first.size_bytes;
                    uint32_t fp = 2166136261u;
                    const uint32_t nb = pr.size, head = nb < 32 ? nb : 32;
                    for (uint32_t b = 0; b < head; b++) fp = (fp ^ pr.addr[b]) * 16777619u;
                    for (uint32_t b = nb > 32 ? nb - 32 : 0; b < nb; b++) fp = (fp ^ pr.addr[b]) * 16777619u;
                    pr.fp = fp;
                    e->probes.push_back(pr);
                }
            }
        }
    }
    if (c->refused == nullptr) {
        McSeg s;
        mc_fill_seg(s, ts);
        if (c->cur_seg < 0 || memcmp(&s, &e->segs[c->cur_seg], sizeof(McSeg)) != 0) {
            e->segs.push_back(s);
            c->cur_seg = (int)e->segs.size() - 1;
            mc_push_op(MCO_SEG, 0, 0, 0, (uint32_t)c->cur_seg);
        }
        mc_push_op(MCO_TRI, i1, i2, i3);
        e->ntris++;
        c->any_tri = true;
    }

    /* classic order from here (reject, near clip, emission) */
    if (gfx_tri_reject(v1, v2, v3)) return;
    struct LoadedVertex* emit_v[6];
    struct LoadedVertex nc_vtx[2];
    const int n = gfx_tri_nearclip(v_arr, false, emit_v, nc_vtx);
    if (n == 0) return;
    gfx_tri_emit(ts, emit_v, n, false);
}

/* ----------------------------------------------------------------- replay */

/* One vertex of gfx_tri_emit with the segment constants; returns the end of
 * what it wrote (the program stride). */
static inline float* mc_emit_vertex(const McSeg& s, const struct LoadedVertex* vi, float* out) {
    const TriState& ts = s.ts;
    const float* L = s_mc_lut255;
    float z = vi->z, w = vi->w;
    if (ts.clip_parameters.z_is_from_0_to_1) {
        z = (z + w) / 2.0f;
    }
    *out++ = vi->x;
    *out++ = ts.clip_parameters.invert_y ? -vi->y : vi->y;
    *out++ = z;
    *out++ = w;
    for (int t = 0; t < 2; t++) {
        if (!ts.used_textures[t]) continue;
        float u = vi->u / 32.0f;
        float v = vi->v / 32.0f;
        u *= s.sh_mul_s[t];
        v *= s.sh_mul_t[t];
        u -= s.uls4[t];
        v -= s.ult4[t];
        if (ts.wrap_s[t]) {
            u = fmodf(u, ts.wrap_tw[t]);
            if (u < 0.0f) u += ts.wrap_tw[t];
            u += ts.wrap_uls[t];
            if (ts.tex_width[t] > 0 && u > (float)ts.tex_width[t]) u = (float)ts.tex_width[t] - 0.5f;
        }
        if (ts.wrap_t[t]) {
            v = fmodf(v, ts.wrap_th[t]);
            if (v < 0.0f) v += ts.wrap_th[t];
            v += ts.wrap_ult[t];
            if (ts.tex_height[t] > 0 && v > (float)ts.tex_height[t]) v = (float)ts.tex_height[t] - 0.5f;
        }
        if (!s.persp) {
            u *= 0.5f;
            v *= 0.5f;
        }
        if (s.linear_filt) {
            u += 0.5f;
            v += 0.5f;
        }
        *out++ = s.inv_w[t] != 0.0f ? u * s.inv_w[t] : u / ts.tex_width[t];
        *out++ = s.inv_h[t] != 0.0f ? v * s.inv_h[t] : v / ts.tex_height[t];
        if (ts.tm & (1 << 2 * t)) *out++ = s.clamp_s[t];
        if (ts.tm & (1 << (2 * t + 1))) *out++ = s.clamp_t[t];
    }
    if (ts.use_fog) {
        *out++ = L[rdp.fog_color.r];
        *out++ = L[rdp.fog_color.g];
        *out++ = L[rdp.fog_color.b];
        *out++ = gfx_fog_vertex ? L[(uint8_t)vi->fog_n] : vi->fog_n / 255.0f;
    }
    if (ts.use_grayscale) {
        *out++ = L[rdp.grayscale_color.r];
        *out++ = L[rdp.grayscale_color.g];
        *out++ = L[rdp.grayscale_color.b];
        *out++ = L[rdp.grayscale_color.a];
    }
    for (int j = 0; j < ts.num_inputs; j++) {
        const struct RGBA* color = 0;
        struct RGBA tmp = { 0 };
        for (int k = 0; k < 1 + (ts.use_alpha ? 1 : 0); k++) {
            switch (ts.comb->shader_input_mapping[k][j]) {
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
                case G_CCMUX_PRIMITIVE_ALPHA:
                    tmp.r = tmp.g = tmp.b = rdp.prim_color.a;
                    color = &tmp;
                    break;
                case G_CCMUX_ENV_ALPHA:
                    tmp.r = tmp.g = tmp.b = rdp.env_color.a;
                    color = &tmp;
                    break;
                case G_CCMUX_PRIM_LOD_FRAC:
                    tmp.r = tmp.g = tmp.b = rdp.prim_lod_fraction;
                    color = &tmp;
                    break;
                case G_CCMUX_LOD_FRACTION:
                    if (s.tl_lod) {
                        const float distance_frac = std::max(0.f, std::min(w / 1024.f, 1.f));
                        tmp.r = tmp.g = tmp.b = tmp.a = (0.7f + distance_frac * 0.3f) * 255.f;
                    } else {
                        tmp.r = tmp.g = tmp.b = tmp.a = 255;
                    }
                    color = &tmp;
                    break;
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
                *out++ = L[color->r];
                *out++ = L[color->g];
                *out++ = L[color->b];
            } else {
                *out++ = L[color->a];
            }
        }
    }
    return out;
}

/* gfx_tri_emit with the segment's constants instead of the live rdp state
 * (non-rect triangles only). Same arithmetic, same order. */
static void mc_emit(const McSeg& s, struct LoadedVertex* const* emit_v, int n_out_tris) {
    const TriState& ts = s.ts;
    const int n_emit_verts = n_out_tris * 3;
    if (buf_vbo_num_tris + n_out_tris > MAX_BUFFERED) {
        gfx_flush();
    }
    if (buf_vbo_len == 0) {
        float* ring = n_emit_verts > 0 ? gfx_rapi->stream_begin(MAX_BUFFERED * 3 * 32) : nullptr;
        buf_vbo_w = ring != nullptr ? ring : buf_vbo;
    }
    float* out = buf_vbo_w + buf_vbo_len;
    for (int i = 0; i < n_emit_verts; i++) {
        out = mc_emit_vertex(s, emit_v[i], out);
    }
    buf_vbo_len = (size_t)(out - buf_vbo_w);
    buf_vbo_num_tris += n_out_tris;
    if (buf_vbo_num_tris >= MAX_BUFFERED) {
        gfx_flush();
    }
}

/* The GL-side state changes gfx_tri_state makes for this segment. */
static void mc_apply_seg(const McEntry* e, int si) {
    const McSeg& s = e->segs[si];
    if (s.depth_mode != rendering_state.depth_mode) {
        const uint8_t dm = s.depth_mode;
        gfx_flush();
        gfx_rapi->set_depth_mode((dm & 1) != 0, (dm & 2) != 0, (dm & 4) != 0, (dm & 8) != 0,
                                 (uint16_t)((dm & 0x30) << 6));
        rendering_state.depth_mode = dm;
    }
    gfx_apply_viewport_scissor();
    for (int i = 0; i < 2; i++) {
        if (!s.has_node[i]) continue;
        TextureCacheNode* node = e->nodes[si * 2 + i];
        if (rendering_state.textures[i] != node) {
            gfx_flush();
            gfx_rapi->select_texture(i, node->second.texture_id, node->second.linear_filter);
            rendering_state.textures[i] = node;
            gfx_texture_cache.lru.splice(gfx_texture_cache.lru.end(), gfx_texture_cache.lru, node->second.lru_location);
        }
        if (s.s_linear[i] != node->second.linear_filter || s.s_cms[i] != node->second.cms ||
            s.s_cmt[i] != node->second.cmt) {
            gfx_flush();
            gfx_rapi->set_sampler_parameters(i, s.s_linear[i], s.s_cms[i], s.s_cmt[i], s.s_lod);
            node->second.linear_filter = s.s_linear[i];
            node->second.cms = s.s_cms[i];
            node->second.cmt = s.s_cmt[i];
        }
    }
    if (s.ts.prg != rendering_state.shader_program) {
        gfx_flush();
        gfx_rapi->unload_shader(rendering_state.shader_program);
        gfx_rapi->load_shader(s.ts.prg);
        rendering_state.shader_program = s.ts.prg;
    }
    if (s.use_alpha != rendering_state.alpha_blend || s.use_modulate != rendering_state.modulate) {
        gfx_flush();
        gfx_rapi->set_use_alpha(s.use_alpha, s.use_modulate);
        rendering_state.alpha_blend = s.use_alpha;
        rendering_state.modulate = s.use_modulate;
    }
}

static bool mc_fp_ok(const McEntry* e, struct GfxClipParameters clip, int* why) {
    for (int i = 0; i < 16; i++) {
        if ((e->seg_mask >> i) & 1) {
            if (segmentPointers[i] != e->seg_val[i]) { *why = MCR_SEGS; return false; }
        }
    }
    *why = MCR_FP;
    const McScal& in = e->in;
    if (((rdp.other_mode_l ^ in.oml) & e->in_oml_mask) != 0) return false;
    if (((rdp.other_mode_h ^ in.omh) & e->in_omh_mask) != 0) return false;
    if (((rsp.geometry_mode ^ in.geom) & e->in_geom_mask) != 0) return false;
    if (((rsp.extra_geometry_mode ^ in.xgeom) & e->in_xgeom_mask) != 0) return false;
    const uint32_t f = e->in_flags;
    if ((f & MCW_COMB) && rdp.combine_mode != in.comb) return false;
    if ((f & MCW_GRAYF) && (rdp.grayscale ? 1 : 0) != in.grayscale) return false;
    if ((f & MCW_FTILE) && rdp.first_tile_index != in.first_tile) return false;
    if ((f & MCW_TEXLOD) && ((rdp.tex_lod ? 1 : 0) != in.tex_lod || (rdp.tex_detail ? 1 : 0) != in.tex_detail ||
                             rdp.palette_fmt != in.palette_fmt)) return false;
    for (int t = 0; t < 8; t++) {
        if (((e->in_tiles >> t) & 1) && memcmp(&rdp.texture_tile[t], e->in_tile[t].raw, sizeof(McTile)) != 0) return false;
    }
    for (int i = 0; i < 2; i++) {
        if (e->in_bind[i] && (rdp.textures_changed[i] || rendering_state.textures[i] != e->in_node[i])) return false;
    }
    if (e->settings != mc_settings()) return false;
    if (e->clip.invert_y != clip.invert_y || e->clip.z_is_from_0_to_1 != clip.z_is_from_0_to_1) return false;
    return true;
}

/* Byte-exact check of everything the program was built from; on a mismatch
 * names the opcode at the first differing DL command (PERFSTAT). */
static bool mc_bytes_ok(const McEntry* e) {
    for (const McRange& r : e->ranges) {
        if (memcmp(r.p, e->bytes.data() + r.off, r.len) != 0) {
            if (gfx_perfstat_on()) {
                uint32_t o = 0;
                while (o < r.len && r.p[o] == e->bytes[r.off + o]) o++;
                char k[32];
                const uint32_t cmdo = o & ~(uint32_t)(sizeof(Gfx) - 1);
                const Gfx* g = (const Gfx*)(r.p + cmdo);
                snprintf(k, sizeof(k), "op 0x%02x", (unsigned)(g->words.w0 >> 24) & 0xff);
                s_mc_stale_ops[k]++;
            }
            return false;
        }
    }
    for (const McTexProbe& pr : e->probes) {
        uint32_t fp = 2166136261u;
        const uint32_t nb = pr.size, head = nb < 32 ? nb : 32;
        for (uint32_t b = 0; b < head; b++) fp = (fp ^ pr.addr[b]) * 16777619u;
        for (uint32_t b = nb > 32 ? nb - 32 : 0; b < nb; b++) fp = (fp ^ pr.addr[b]) * 16777619u;
        if (fp != pr.fp) {
            s_mc_stale_ops["ci8-image"]++;
            return false;
        }
    }
    return true;
}

/* Find every texture of the program in the cache (nothing is imported on a
 * replay); false = one was evicted, re-capture. */
static bool mc_nodes_ok(McEntry* e) {
    if (e->nodes_gen == s_mc_texgen && e->nodes.size() == e->segs.size() * 2) return true;
    e->nodes.assign(e->segs.size() * 2, nullptr);
    for (size_t si = 0; si < e->segs.size(); si++) {
        for (int i = 0; i < 2; i++) {
            if (!e->segs[si].has_node[i]) continue;
            auto it = gfx_texture_cache.map.find(e->segs[si].key[i]);
            if (it == gfx_texture_cache.map.end()) return false;
            e->nodes[si * 2 + i] = &*it;
        }
    }
    e->nodes_gen = s_mc_texgen;
    return true;
}

static void mc_apply_exit(const McEntry* e) {
    const McScal& x = e->out;
    rdp.other_mode_l = (rdp.other_mode_l & ~e->w_oml) | (x.oml & e->w_oml);
    rdp.other_mode_h = (rdp.other_mode_h & ~e->w_omh) | (x.omh & e->w_omh);
    const uint32_t f = e->w_flags;
    if (f & MCW_TEXLOD) {
        rdp.tex_lod = x.tex_lod != 0;
        rdp.tex_detail = x.tex_detail != 0;
        rdp.palette_fmt = x.palette_fmt;
    }
    if (f & MCW_COMB) rdp.combine_mode = x.comb;
    if (f & MCW_FILL) rdp.fill_color = x.fill;
    if (f & MCW_GRAYF) rdp.grayscale = x.grayscale != 0;
    for (int t = 0; t < 8; t++) {
        auto& tt = rdp.texture_tile[t];
        const auto* xt = (const std::remove_reference<decltype(rdp.texture_tile[0])>::type*)e->out_tile[t].raw;
        if ((e->w_tile_a >> t) & 1) {
            tt.fmt = xt->fmt; tt.siz = xt->siz; tt.cms = xt->cms; tt.cmt = xt->cmt;
            tt.masks = xt->masks; tt.maskt = xt->maskt; tt.shifts = xt->shifts; tt.shiftt = xt->shiftt;
            tt.tmem = xt->tmem; tt.line_size_bytes = xt->line_size_bytes; tt.palette = xt->palette;
        }
        if ((e->w_tile_b >> t) & 1) {
            tt.uls = xt->uls; tt.ult = xt->ult; tt.lrs = xt->lrs; tt.lrt = xt->lrt;
            tt.width = xt->width; tt.height = xt->height;
        }
    }
    for (const auto& pt : e->out_tmem) rdp.loaded_texture[pt.first] = pt.second;
    if (f & MCW_PAL) {
        /* D985: only the entries the list loads; the others stay the caller's,
         * as after the classic walk (the whole capture-time table handed the
         * lists drawn next another frame's colours in the unloaded entries). */
        for (int k = 0; k < 256; k++) {
            if ((e->pal_mask[k >> 5] >> (k & 31)) & 1) rdp.palette[k] = e->out_palette[k];
        }
        rdp.palette_addrs[0] = e->out_palette_addrs[0];
        rdp.palette_addrs[1] = e->out_palette_addrs[1];
        {   /* as gfx_dp_load_tlut (D217) */
            uint32_t h = 2166136261u;
            const uint8_t* pb = (const uint8_t*)rdp.palette;
            for (uint32_t k = 0; k < sizeof(rdp.palette); ++k) {
                h ^= pb[k];
                h *= 16777619u;
            }
            rdp.palette_hash = h;
        }
    }
    if (f & MCW_TIMG) rdp.texture_to_load = e->out_timg;
    for (int i = 0; i < 2; i++) {
        if (e->tc_touched[i]) rdp.textures_changed[i] = e->out_tc[i];
    }
}

/* ------------------------------------------------- step B: GPU meshes (GE_MESHGPU) */

static int mc_gpu_on(void) {
    static int on = -1;
    if (on < 0) {
        /* default: on where the backend vets mesh programs (deko3d); GL keeps
         * it opt-in (D972: a mesh draw costs ~25 us on the Switch GL driver) */
        const char* e = getenv("GE_MESHGPU");
        on = (e && e[0]) ? (e[0] != '0') : (gfx_rapi->mesh_supported != nullptr);
        if (on) sysLogPrintf(LOG_NOTE, "GE_MESHGPU: cached lists drawn with the vertex transform on the GPU (issue #10, step B)");
    }
    return on;   /* both fog forms: the twins also write fog * w (per-pixel fog, Video.Visuals >= Fixed) */
}

/* GE_MESHGPU_FULL (default off, =1 to enable): skip the near/far-plane box
 * test and let the GPU clip those meshes too. Off by default (D985): the GPU
 * clip interpolates shade and fog from the corner behind the eye (fog 255),
 * so the Dam ground near the wall turned fog-blue (perspective shade) or
 * black (affine); the CPU near clip re-derives fog at the plane (D543/D553). */
static int mc_gpu_full(void) {
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("GE_MESHGPU_FULL");
        on = (e && e[0]) ? (e[0] != '0') : 0;
        if (on) sysLogPrintf(LOG_NOTE, "GE_MESHGPU_FULL: near/far-plane meshes also drawn on the GPU (default off: CPU clip as on the N64)");
    }
    return on;
}

static void mc_gpu_why(const char* why) {
    if (gfx_perfstat_on()) s_mc_g_why[why]++;
}

struct McBakeSrc {
    const Vtx* v;
    uint8_t slot;
    bool fogz;
};

struct McBake {
    McBakeSrc src[MAX_VERTICES];
    int slot;
    const char* bad;
    bool env_in_list;   /* a G_SETENVCOLOR of the list has run: env is baked from here on */
    bool new_run;       /* start a run at the next triangle */
    std::vector<float> data;
    std::vector<McRun> runs;
    float box[GFX_MESH_MAX_MTX][6];
};

static inline uint64_t mc_mix(uint64_t h, uint64_t v) {
    return (h ^ v) * 1099511628211ULL;
}

static inline uint64_t mc_rgba(const struct RGBA& c) {
    return (uint64_t)c.r | ((uint64_t)c.g << 8) | ((uint64_t)c.b << 16) | ((uint64_t)c.a << 24);
}

/* Key of the vertex sources (pointers + bytes) and signature of every colour
 * state the list goes through, without executing anything. */
enum { MCS_VTX, MCS_SEG, MCS_PRIM, MCS_ENV, MCS_FOG, MCS_GRAY, MCS_LODF, MCS_N };
static const char* const s_mcs_name[MCS_N] = { "vtx-state", "segments", "prim", "env", "fog", "gray", "primlod" };

static void mc_gpu_prepass(const McEntry* e, uint64_t* key, uint64_t* sig, uint64_t* ptrkey = nullptr, uint64_t* groups = nullptr) {
    uint64_t k = 1469598103934665603ULL, pk = k;
    uint64_t sg[MCS_N];
    for (int i = 0; i < MCS_N; i++) sg[i] = 1469598103934665603ULL;
    struct RGBA env = rdp.env_color, prim = rdp.prim_color, gray = rdp.grayscale_color;
    uint8_t lodf = rdp.prim_lod_fraction;
    uint64_t g = 1469598103934665603ULL;
    /* the vertex-pipeline state a G_VTX bakes in (lighting / fog / texgen
     * bits, texture scale), as each G_VTX of the list sees it */
    uint32_t sgeom = rsp.geometry_mode;
    uint32_t sscale = ((uint32_t)rsp.texture_scaling_factor.s << 16) | rsp.texture_scaling_factor.t;
    uint64_t lastv = ~0ULL;
    /* only the colours a triangle actually reads (its segment's combiner), in order */
    bool dirty = true;
    bool env_list = false;   /* env colour set by the list itself (baked); before that it is a draw uniform */
    int seg = -1;
    for (const McOp& op : e->ops) {
        if (op.k == MCO_GEOM) {
            sgeom = (sgeom & ~op.x) | (uint32_t)op.p;
        } else if (op.k == MCO_TEXTURE) {
            sscale = (uint32_t)op.p;
        } else if (op.k == MCO_VTX) {
            const uint64_t vs = ((uint64_t)(sgeom & (G_LIGHTING | G_FOG | G_TEXTURE_GEN)) << 32) | sscale;
            if (vs != lastv) {
                sg[MCS_VTX] = mc_mix(sg[MCS_VTX], vs);
                lastv = vs;
            }
            const uint8_t* p = (const uint8_t*)seg_addr(op.p);
            pk = mc_mix(pk, (uint64_t)(uintptr_t)p);
            if (!fast3d_ptr_ok(p)) continue;
            const size_t nw = (size_t)op.a * sizeof(Vtx) / 8;
            for (size_t i = 0; i < nw; i++) {
                uint64_t w;
                memcpy(&w, p + i * 8, 8);
                k = mc_mix(k, w);
            }
        } else if (op.k == MCO_SEG) {
            if ((int)op.x != seg) dirty = true;
            seg = (int)op.x;
        } else if (op.k == MCO_TRI) {
            if (dirty && seg >= 0) {
                const uint8_t cu = e->segs[seg].colour_use;
                sg[MCS_SEG] = mc_mix(sg[MCS_SEG], (uint64_t)seg);
                if (cu & MC_CU_PRIM) sg[MCS_PRIM] = mc_mix(sg[MCS_PRIM], mc_rgba(prim));
                /* the entry env colour is a draw uniform (GE_MESHGPU): objects
                 * set it per instance (fades, tints) before calling the list */
                if ((cu & MC_CU_ENV) && env_list) sg[MCS_ENV] = mc_mix(sg[MCS_ENV], mc_rgba(env));
                /* Fog RGB is a draw uniform (GE_MESHGPU): it changes every
                 * Jungle frame and must not invalidate the static mesh. */
                if (cu & MC_CU_GRAY) sg[MCS_GRAY] = mc_mix(sg[MCS_GRAY], mc_rgba(gray));
                if (cu & MC_CU_LODF) sg[MCS_LODF] = mc_mix(sg[MCS_LODF], lodf);
                dirty = false;
            }
        } else if (op.k == MCO_COLOR) {
            const Gfx* cmd = (const Gfx*)op.p;
            switch (op.a) {
                case G_SETENVCOLOR:
                    env = { (uint8_t)C1(24, 8), (uint8_t)C1(16, 8), (uint8_t)C1(8, 8), (uint8_t)C1(0, 8) };
                    env_list = true;
                    break;
                case G_SETPRIMCOLOR:
                    lodf = (uint8_t)C0(0, 8);
                    prim = { (uint8_t)C1(24, 8), (uint8_t)C1(16, 8), (uint8_t)C1(8, 8), (uint8_t)C1(0, 8) };
                    break;
                case G_SETFOGCOLOR:
                    break;  /* live mesh draw uniform, intentionally not in its bake key */
                case G_SETINTENSITY_EXT:
                    gray = { (uint8_t)C1(24, 8), (uint8_t)C1(16, 8), (uint8_t)C1(8, 8), (uint8_t)C1(0, 8) };
                    break;
            }
            dirty = true;
        }
    }
    *key = mc_mix(k, pk);
    for (int i = 0; i < MCS_N; i++) g = mc_mix(g, sg[i]);
    *sig = g;
    if (groups != nullptr) memcpy(groups, sg, sizeof(sg));
    if (ptrkey != nullptr) *ptrkey = pk;
}

/* The float index range of the combiner inputs in a vertex of this segment. */
static int mc_input_base(const McSeg& s) {
    const TriState& ts = s.ts;
    int base = 4;
    for (int t = 0; t < 2; t++) {
        if (!ts.used_textures[t]) continue;
        base += 2;
        if (ts.tm & (1 << 2 * t)) base++;
        if (ts.tm & (1 << (2 * t + 1))) base++;
    }
    if (ts.use_fog) base += 4;
    if (ts.use_grayscale) base += 4;
    return base;
}

/* Bake one triangle (all three corners, before any reject) into the mesh. */
static void mc_bake_tri(const McEntry* e, McBake* bk, int seg, uint8_t i1, uint8_t i2, uint8_t i3) {
    if (seg < 0) { bk->bad = "no-segment"; return; }
    const McSeg& sg = e->segs[seg];
    const uint8_t idx[3] = { i1, i2, i3 };
    for (int c = 0; c < 3; c++) {
        if (idx[c] >= MAX_VERTICES || bk->src[idx[c]].v == nullptr) { bk->bad = "outside-vertex"; return; }
    }
    const bool fogz = sg.ts.use_fog && bk->src[i1].fogz;
    if (sg.ts.use_fog && (bk->src[i2].fogz != bk->src[i1].fogz || bk->src[i3].fogz != bk->src[i1].fogz)) {
        bk->bad = "fog-mode-mix";
        return;
    }
    const uint32_t gm = rsp.geometry_mode & G_CULL_BOTH;
    const uint8_t cull = gm == G_CULL_BOTH ? 3 : gm == G_CULL_BACK ? 1 : gm == G_CULL_FRONT ? 2 : 0;
    if (bk->runs.empty() || bk->new_run || bk->runs.back().seg != seg || bk->runs.back().cull != cull ||
        bk->runs.back().fog_from_z != fogz) {
        /* deko3d: a combiner outside the shader pack is drawn by the uber-shader,
         * which has no GPU-transform twin; its runs stay on the CPU path. */
        if (gfx_rapi->mesh_supported != nullptr &&
            (sg.ts.comb == nullptr ||
             !gfx_rapi->mesh_supported(sg.ts.comb->shader_id0,
                                       sg.ts.comb->shader_id1 | (sg.ts.tm * SHADER_OPT_TEXEL0_CLAMP_S)))) {
            bk->bad = "no-mesh-shader";
            return;
        }
        McRun r;
        memset(&r, 0, sizeof(r));
        r.seg = (uint16_t)seg;
        r.cull = cull;
        r.fog_from_z = fogz;
        r.byte_offset = (uint32_t)(bk->data.size() * sizeof(float));
        /* inputs that read the entry env colour (as mc_emit_vertex does):
         * k = 0 writes rgb, k = 1 (use_alpha) writes a */
        if (!bk->env_in_list && sg.ts.comb != nullptr) {
            for (int j = 0; j < sg.ts.num_inputs && j < GFX_MESH_MAX_INPUTS; j++) {
                const uint8_t m0 = sg.ts.comb->shader_input_mapping[0][j];
                if (m0 == G_CCMUX_ENVIRONMENT) r.env_sel[j] |= MC_ENV_RGB;
                else if (m0 == G_CCMUX_ENV_ALPHA) r.env_sel[j] |= MC_ENV_AAA;
                if (sg.ts.use_alpha && sg.ts.comb->shader_input_mapping[1][j] == G_CCMUX_ENVIRONMENT)
                    r.env_sel[j] |= MC_ENV_A;
            }
        }
        bk->new_run = false;
        bk->runs.push_back(r);
    }
    McRun& run = bk->runs.back();
    const int base = mc_input_base(sg);
    const int ncomp = sg.ts.use_alpha ? 4 : 3;
    for (int c = 0; c < 3; c++) {
        const McBakeSrc& src = bk->src[idx[c]];
        float a[64], b[64], v[64];
        struct LoadedVertex lv = rsp.loaded_vertices[idx[c]];
        const int n = (int)(mc_emit_vertex(sg, &lv, v) - v);
        if (n > 64) { bk->bad = "stride"; return; }
        /* which floats follow w (the LOD fraction): emit at two depths and compare */
        struct LoadedVertex lw = lv;
        lw.w = 1.0f;
        mc_emit_vertex(sg, &lw, a);
        lw.w = 3000.0f;
        mc_emit_vertex(sg, &lw, b);
        for (int i = 4; i < n; i++) {
            if (a[i] != b[i]) {
                if (i < base || (i - base) / ncomp >= GFX_MESH_MAX_INPUTS) { bk->bad = "w-dependent"; return; }
                run.lod_mask[(i - base) / ncomp][(i - base) % ncomp] = 1.0f;
            }
        }
        v[0] = src.v->v.ob[0];
        v[1] = src.v->v.ob[1];
        v[2] = src.v->v.ob[2];
        v[3] = (float)src.slot;
        bk->data.insert(bk->data.end(), v, v + n);
        float* bx = bk->box[src.slot];
        for (int k = 0; k < 3; k++) {
            if (v[k] < bx[k]) bx[k] = v[k];
            if (v[k] > bx[3 + k]) bx[3 + k] = v[k];
        }
        run.nverts++;
    }
}

static void mc_gpu_free(McGpu* g) {
    s_mc_gpu_dead.push_back(g->mesh_id);
    delete g;
}

/* As gfx_sp_matrix, without its D578/D955 hooks (never active when this runs). */
static void mc_sim_matrix(uint8_t parameters, const int32_t* addr, float (*mv)[4][4], int* mvsize, float P[4][4], float MP[4][4]) {
    float matrix[4][4];
    const bool addr_bad = ((uintptr_t)addr < 0x10000 || (uintptr_t)addr >= 0x0000800000000000ULL);
    if (addr_bad) {
        memset(matrix, 0, sizeof(matrix));
        matrix[0][0] = matrix[1][1] = matrix[2][2] = matrix[3][3] = 1.0f;
    } else {
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j += 2) {
                int32_t int_part = addr[i * 2 + j / 2];
                uint32_t frac_part = addr[8 + i * 2 + j / 2];
                matrix[i][j] = (int32_t)((int_part & 0xffff0000) | (frac_part >> 16)) / 65536.0f;
                matrix[i][j + 1] = (int32_t)((int_part << 16) | (frac_part & 0xffff)) / 65536.0f;
            }
        }
    }
    if (parameters & G_MTX_PROJECTION) {
        if (parameters & G_MTX_LOAD) {
            memcpy(P, matrix, sizeof(matrix));
        } else {
            gfx_matrix_mul(P, matrix, P);
        }
    } else {
        if ((parameters & G_MTX_PUSH) && *mvsize < 11) {
            ++*mvsize;
            memcpy(mv[*mvsize - 1], mv[*mvsize - 2], sizeof(matrix));
        }
        if (parameters & G_MTX_LOAD) {
            memcpy(mv[*mvsize - 1], matrix, sizeof(matrix));
        } else {
            gfx_matrix_mul(mv[*mvsize - 1], matrix, mv[*mvsize - 1]);
        }
    }
    gfx_matrix_mul(MP, mv[*mvsize - 1], P);
}

/* true: every corner of every slot's box is in front of the camera plane and
 * not beyond the far plane under its matrix (no CPU near clip or far reject
 * would apply to any of its triangles). */
static bool mc_box_ok(const float* box, const float m[4][4], const char** why) {
    if (box[0] > box[3]) return true;   /* slot without vertices */
    for (int c = 0; c < 8; c++) {
        const float x = (c & 1) ? box[3] : box[0];
        const float y = (c & 2) ? box[4] : box[1];
        const float z = (c & 4) ? box[5] : box[2];
        const float cz = x * m[0][2] + y * m[1][2] + z * m[2][2] + m[3][2];
        const float cw = x * m[0][3] + y * m[1][3] + z * m[2][3] + m[3][3];
        if (cw <= 0.02f) { *why = "near-plane"; return false; }
        if (cz > cw) { *why = "far-plane"; return false; }
    }
    return true;
}

static int s_mc_g_last = 0;   /* 0 drawn, 1 no variant (bake), 2 colours changed (re-bake), 3 other */

/* Draw a validated entry from its GPU mesh. false = not possible this frame
 * (nothing executed; the caller replays on the CPU). */
static bool mc_replay_gpu(McEntry* e) {
    s_mc_g_last = 3;
    if (e->gpu_bad != nullptr) { mc_gpu_why(e->gpu_bad); return false; }
    if (s_obj_frame || s_pcam_armed) { mc_gpu_why("interp-hooks"); return false; }
    const struct GfxClipParameters clip = gfx_rapi->get_clip_parameters();
    if (clip.z_is_from_0_to_1) { mc_gpu_why("z01"); return false; }
    uint64_t key, sig, pk, grp[8];
    mc_gpu_prepass(e, &key, &sig, &pk, grp);
    McGpu* g = nullptr;
    bool same_ptrs = false;
    for (McGpu* v : e->gpu) {
        if (v->key == key) { g = v; break; }
        if (v->ptrkey == pk) same_ptrs = true;
    }
    if (g == nullptr) {
        s_mc_g_last = 1;
        mc_gpu_why(e->gpu.empty() ? "not-baked" : same_ptrs ? "vertex-bytes-changed" : "vertex-pointers-new");
        return false;
    }
    if (g->colour_sig != sig) {
        s_mc_g_last = 2;
        if (gfx_perfstat_on()) {
            for (int i = 0; i < MCS_N; i++) {
                if (grp[i] != g->groups[i]) {
                    char k[48];
                    snprintf(k, sizeof(k), "changed:%s", s_mcs_name[i]);
                    s_mc_g_why[k]++;
                    break;
                }
            }
        }
        return false;
    }
    /* the matrices this replay will load, simulated: is any part near the camera? */
    if (!mc_gpu_full()) {
        static float mv[11][4][4];
        int mvsize = rsp.modelview_matrix_stack_size;
        if (mvsize < 1) { mc_gpu_why("matrix-stack"); return false; }
        memcpy(mv, rsp.modelview_matrix_stack, sizeof(mv));
        float P[4][4], MP[4][4];
        memcpy(P, rsp.P_matrix, sizeof(P));
        memcpy(MP, rsp.MP_matrix, sizeof(MP));
        int slot = 0;
        const char* why = nullptr;
        if (!mc_box_ok(&g->box[0], MP, &why)) { mc_gpu_why(why); return false; }
        for (const McOp& op : e->ops) {
            if (op.k == MCO_MTX) {
                mc_sim_matrix(op.a, (const int32_t*)seg_addr(op.p), mv, &mvsize, P, MP);
            } else if (op.k == MCO_POPMTX) {
                if (mvsize > 0) {
                    --mvsize;
                    if (mvsize > 0) gfx_matrix_mul(MP, mv[mvsize - 1], P);
                }
            } else {
                continue;
            }
            if (++slot >= g->nslots) break;
            if (!mc_box_ok(&g->box[slot * 6], MP, &why)) { mc_gpu_why(why); return false; }
        }
    }
    /* execute the list's state commands for real (the classic matrices go
     * through gfx_sp_matrix), collecting the matrix of each slot */
    static struct GfxMeshDraw d;
    float pal[GFX_MESH_MAX_MTX][4][4];
    int slot = 0;
    const struct RGBA env0 = rdp.env_color;   /* entry value: runs with env_sel read it, the walk below may change it */
    memcpy(pal[0], rsp.MP_matrix, sizeof(pal[0]));
    for (const McOp& op : e->ops) {
        switch (op.k) {
            case MCO_MTX:
                gfx_sp_matrix(op.a, (const int32_t*)seg_addr(op.p));
                if (++slot < GFX_MESH_MAX_MTX) memcpy(pal[slot], rsp.MP_matrix, sizeof(pal[0]));
                break;
            case MCO_POPMTX:
                gfx_sp_pop_matrix(1);
                if (++slot < GFX_MESH_MAX_MTX) memcpy(pal[slot], rsp.MP_matrix, sizeof(pal[0]));
                break;
            case MCO_GEOM:
                gfx_sp_geometry_mode(op.x, (uint32_t)op.p);
                break;
            case MCO_XGEOM:
                gfx_sp_extra_geometry_mode(op.x, (uint32_t)op.p);
                break;
            case MCO_TEXTURE: {
                const uint32_t w0 = op.x, w1 = (uint32_t)op.p;
                gfx_sp_texture((uint16_t)(w1 >> 16), (uint16_t)w1, (uint8_t)((w0 >> 11) & 7), (uint8_t)((w0 >> 8) & 7),
                               (uint8_t)w0);
                break;
            }
            case MCO_MOVEWORD:
                gfx_sp_moveword(op.a, (uint16_t)op.x, op.p);
                break;
            case MCO_MOVEMEM:
                gfx_sp_movemem(op.a, 0, seg_addr(op.p));
                break;
            case MCO_COLOR: {
                const Gfx* cmd = (const Gfx*)op.p;
                switch (op.a) {
                    case G_SETENVCOLOR:
                        gfx_dp_set_env_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                        break;
                    case G_SETPRIMCOLOR:
                        gfx_dp_set_prim_color(C0(8, 8), C0(0, 8), C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                        break;
                    case G_SETFOGCOLOR:
                        gfx_dp_set_fog_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                        break;
                    case G_SETINTENSITY_EXT:
                        gfx_dp_set_grayscale_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                        break;
                }
                break;
            }
            case MCO_VTX:   /* D985: only the loads still live at the end (op.c), for the lists drawn next */
                if (op.c) gfx_sp_vertex(op.a, op.b, (const Vtx*)seg_addr(op.p));
                break;
            default:   /* SEG, TRI */
                break;
        }
    }
    /* clip matrices as the CPU path applies them: aspect fix on x (unless a
     * framebuffer is the target), y flip of the target */
    const int nmtx = g->nslots;
    for (int k = 0; k < nmtx; k++) {
        float* m = d.mtx[k];
        for (int i = 0; i < 4; i++) {
            const float* p = pal[k][i];
            m[i * 4 + 0] = fbActive ? p[0]
                                    : (rsp.aspect_ofs * p[3] + p[0]) * rsp.aspect_scale / gfx_current_dimensions.aspect_ratio;
            m[i * 4 + 1] = clip.invert_y ? -p[1] : p[1];
            m[i * 4 + 2] = p[2];
            m[i * 4 + 3] = p[3];
        }
    }
    d.nmtx = (uint32_t)nmtx;
    d.invert_y = clip.invert_y;
    d.fog_mul = rsp.fog_mul;
    d.fog_off = rsp.fog_offset;
    for (const McRun& r : g->runs) {
        if (r.cull == 3 || r.nverts == 0) continue;
        mc_apply_seg(e, r.seg);
        gfx_flush();   /* classic triangles batched before this run draw first */
        d.byte_offset = r.byte_offset;
        d.nverts = r.nverts;
        d.cull = r.cull;
        d.fog_from_z = r.fog_from_z;
        d.fog_color[0] = rdp.fog_color.r / 255.0f;
        d.fog_color[1] = rdp.fog_color.g / 255.0f;
        d.fog_color[2] = rdp.fog_color.b / 255.0f;
        d.fog_color[3] = rdp.fog_color.a / 255.0f;
        memcpy(d.lod_mask, r.lod_mask, sizeof(d.lod_mask));
        {
            const float* L = s_mc_lut255;   /* the values mc_emit_vertex would have baked */
            for (int j = 0; j < GFX_MESH_MAX_INPUTS; j++) {
                const uint8_t es = r.env_sel[j];
                const float rgb = (es & (MC_ENV_RGB | MC_ENV_AAA)) ? 1.0f : 0.0f;
                d.env_mask[j][0] = d.env_mask[j][1] = d.env_mask[j][2] = rgb;
                d.env_mask[j][3] = es & MC_ENV_A ? 1.0f : 0.0f;
                if (es & MC_ENV_AAA) {
                    d.env_in[j][0] = d.env_in[j][1] = d.env_in[j][2] = L[env0.a];
                } else {
                    d.env_in[j][0] = L[env0.r];
                    d.env_in[j][1] = L[env0.g];
                    d.env_in[j][2] = L[env0.b];
                }
                d.env_in[j][3] = L[env0.a];
            }
        }
        gfx_rapi->mesh_draw(g->mesh_id, &d);
        s_mc_g_runs++;
        s_mc_g_tris += r.nverts / 3;
    }
    mc_apply_exit(e);
    g->last_used = s_mc_frame;
    s_mc_g_replays++;
    s_mc_g_last = 0;
    return true;
}

static int s_mc_g_bakes_frame = 0;
#define MC_GPU_BAKES_PER_FRAME 6
#define MC_GPU_MAX_VARIANTS 8

/* After a CPU replay that baked: keep the mesh. */
static void mc_gpu_commit(McEntry* e, McBake* bk, uint64_t key, uint64_t sig, uint64_t pk, const uint64_t* grp) {
    if (bk->bad != nullptr) {
        e->gpu_bad = bk->bad;
        return;
    }
    if (bk->runs.empty() || bk->data.empty()) return;
    for (size_t i = 0; i < e->gpu.size(); i++) {
        if (e->gpu[i]->key == key) {   /* colours changed: replace */
            mc_gpu_free(e->gpu[i]);
            e->gpu.erase(e->gpu.begin() + i);
            break;
        }
    }
    if (e->gpu.size() >= MC_GPU_MAX_VARIANTS) {
        size_t lru = 0;
        for (size_t i = 1; i < e->gpu.size(); i++) {
            if (e->gpu[i]->last_used < e->gpu[lru]->last_used) lru = i;
        }
        mc_gpu_free(e->gpu[lru]);
        e->gpu.erase(e->gpu.begin() + lru);
    }
    McGpu* g = new McGpu();
    g->key = key;
    g->ptrkey = pk;
    g->colour_sig = sig;
    memcpy(g->groups, grp, sizeof(uint64_t) * MCS_N);
    g->nslots = (uint8_t)(bk->slot + 1);
    g->runs = bk->runs;
    g->box.assign(&bk->box[0][0], &bk->box[0][0] + 6 * g->nslots);
    g->last_used = s_mc_frame;
    g->mesh_id = gfx_rapi->mesh_create(bk->data.data(), bk->data.size());
    e->gpu.push_back(g);
    s_mc_g_bakes++;
    s_mc_g_bake_bytes += bk->data.size() * sizeof(float);
}

static void mc_replay(McEntry* e, McBake* bk = nullptr) {
    int pending = -1, applied = -1;
    const bool ts = gfx_perfstat_on() && ((++s_mc_split_n & 7) == 0);
    uint64_t t = ts ? gfx_perf_now_ns() : 0;
#define MC_T(cat) do { if (ts) { const uint64_t t2_ = gfx_perf_now_ns(); s_mc_t_ns[cat] += t2_ - t; t = t2_; } } while (0)
    if (ts) s_mc_t_replays++;
    for (const McOp& op : e->ops) {
        switch (op.k) {
            case MCO_MTX:
                gfx_sp_matrix(op.a, (const int32_t*)seg_addr(op.p));
                if (bk != nullptr && ++bk->slot >= GFX_MESH_MAX_MTX && bk->bad == nullptr) bk->bad = "matrix-slots";
                MC_T(MCT_MTX);
                break;
            case MCO_POPMTX:
                gfx_sp_pop_matrix(1);
                if (bk != nullptr && ++bk->slot >= GFX_MESH_MAX_MTX && bk->bad == nullptr) bk->bad = "matrix-slots";
                MC_T(MCT_MTX);
                break;
            case MCO_VTX:
                gfx_sp_vertex(op.a, op.b, (const Vtx*)seg_addr(op.p));
                if (bk != nullptr && bk->bad == nullptr) {
                    if (rsp.geometry_mode & G_LIGHTING) {
                        bk->bad = "lit";   /* colours (and maybe UVs) depend on the matrices */
                    } else {
                        const Vtx* vp = (const Vtx*)seg_addr(op.p);
                        for (int i = 0; i < op.a && op.b + i < MAX_VERTICES; i++) {
                            bk->src[op.b + i].v = vp + i;
                            bk->src[op.b + i].slot = (uint8_t)bk->slot;
                            bk->src[op.b + i].fogz = (rsp.geometry_mode & G_FOG) != 0;
                        }
                    }
                }
                MC_T(MCT_VTX);
                break;
            case MCO_GEOM:
                gfx_sp_geometry_mode(op.x, (uint32_t)op.p);
                break;
            case MCO_XGEOM:
                gfx_sp_extra_geometry_mode(op.x, (uint32_t)op.p);
                break;
            case MCO_TEXTURE: {
                const uint32_t w0 = op.x, w1 = (uint32_t)op.p;
                gfx_sp_texture((uint16_t)(w1 >> 16), (uint16_t)w1, (uint8_t)((w0 >> 11) & 7), (uint8_t)((w0 >> 8) & 7),
                               (uint8_t)w0);
                break;
            }
            case MCO_MOVEWORD:
                gfx_sp_moveword(op.a, (uint16_t)op.x, op.p);
                if (bk != nullptr && op.a == G_MW_FOG && bk->bad == nullptr) bk->bad = "fog-in-list";
                break;
            case MCO_MOVEMEM:
                gfx_sp_movemem(op.a, 0, seg_addr(op.p));
                break;
            case MCO_SEG:
                pending = (int)op.x;
                break;
            case MCO_COLOR: {
                const Gfx* cmd = (const Gfx*)op.p;   /* live: read from the list now */
                switch (op.a) {
                    case G_SETENVCOLOR:
                        gfx_dp_set_env_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                        if (bk != nullptr) {   /* the list's own env colour: baked from here on */
                            bk->env_in_list = true;
                            bk->new_run = true;
                        }
                        break;
                    case G_SETPRIMCOLOR:
                        gfx_dp_set_prim_color(C0(8, 8), C0(0, 8), C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                        break;
                    case G_SETFOGCOLOR:
                        gfx_dp_set_fog_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                        break;
                    case G_SETINTENSITY_EXT:
                        gfx_dp_set_grayscale_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                        break;
                }
                break;
            }
            case MCO_TRI: {
                struct LoadedVertex* v1 = &rsp.loaded_vertices[op.a];
                struct LoadedVertex* v2 = &rsp.loaded_vertices[op.b];
                struct LoadedVertex* v3 = &rsp.loaded_vertices[op.c];
                if (bk != nullptr && bk->bad == nullptr) mc_bake_tri(e, bk, pending, op.a, op.b, op.c);
                if (ts) s_mc_t_tris++;
                if (gfx_tri_reject(v1, v2, v3)) {
                    if (ts) s_mc_t_rej++;
                    MC_T(MCT_REJECT);
                    break;
                }
                MC_T(MCT_REJECT);
                if (pending != applied) {
                    mc_apply_seg(e, pending);
                    applied = pending;
                    MC_T(MCT_SEG);
                }
                struct LoadedVertex* v_arr[3] = { v1, v2, v3 };
                struct LoadedVertex* emit_v[6];
                struct LoadedVertex nc_vtx[2];
                const int n = gfx_tri_nearclip(v_arr, false, emit_v, nc_vtx);
                MC_T(MCT_CLIP);
                if (n == 0) break;
                mc_emit(e->segs[applied], emit_v, n);
                if (ts) s_mc_t_emitted += n;
                MC_T(MCT_EMIT);
                break;
            }
            default:
                MC_T(MCT_OTHER);
                break;
        }
    }
    /* leave the GL-side state where the capture (which resolved every
     * triangle's state) left it */
    if (pending >= 0 && pending != applied) mc_apply_seg(e, pending);
    mc_apply_exit(e);
    MC_T(MCT_EXIT);
#undef MC_T
}

/* ---------------------------------------------------------------- capture */

static void mc_free_entry(McEntry* e) {
    for (McGpu* g : e->gpu) mc_gpu_free(g);
    delete e;
}

static void mc_note_refusal(const char* why, uint8_t op, Gfx* dl) {
    char k[48];
    if (strcmp(why, "opcode") == 0) {
        snprintf(k, sizeof(k), "opcode 0x%02x", op);
    } else {
        snprintf(k, sizeof(k), "%s", why);
    }
    s_mc_refusals[k]++;
    if (s_mc_log_budget > 0 && gfx_perfstat_on()) {
        s_mc_log_budget--;
        sysLogPrintf(LOG_NOTE, "GE_MESHCACHE: dl=%p not cached: %s", (void*)dl, k);
    }
}

/* Walk dl classically while recording it. Returns the new entry or null. */
static McEntry* mc_capture(Gfx* dl) {
    McCap cap;
    cap.e = new McEntry();
    McEntry* e = cap.e;
    e->seg_mask = 0;
    memset(e->seg_val, 0, sizeof(e->seg_val));
    e->in_flags = 0;
    e->in_oml_mask = e->in_omh_mask = e->in_geom_mask = e->in_xgeom_mask = 0;
    e->in_tiles = 0;
    e->in_bind[0] = e->in_bind[1] = false;
    e->in_node[0] = e->in_node[1] = nullptr;
    memset(e->pal_mask, 0, sizeof(e->pal_mask));
    e->ntris = 0;
    e->tc_touched[0] = e->tc_touched[1] = false;
    e->nodes_gen = 0;
    e->gpu_bad = nullptr;
    e->hits = 0;
    e->last_used = s_mc_frame;
    cap.refused = nullptr;
    cap.refused_op = 0;
    cap.any_tri = false;
    mc_snap_scal(cap.entry);
    for (int t = 0; t < 8; t++) memcpy(cap.entry_tile[t].raw, &rdp.texture_tile[t], sizeof(McTile));
    for (int i = 0; i < 2; i++) {
        cap.entry_node[i] = rendering_state.textures[i];
        cap.entry_tc[i] = rdp.textures_changed[i];
        cap.imported[i] = false;
    }
    cap.w_flags = cap.w_oml = cap.w_omh = cap.w_geom = cap.w_xgeom = 0;
    cap.w_tile_a = cap.w_tile_b = 0;
    cap.first_tri_done = false;
    cap.range_end = nullptr;
    cap.cur_seg = -1;
    e->settings = mc_settings();
    e->clip = gfx_rapi->get_clip_parameters();

    s_mc_cap = &cap;
    gfx_run_dl(dl);
    s_mc_cap = nullptr;

    if (cap.refused == nullptr && !cap.any_tri) cap.refused = "no-triangles";
    if (cap.refused != nullptr) {
        mc_note_refusal(cap.refused, cap.refused_op, dl);
        mc_free_entry(e);
        return nullptr;
    }
    /* key segments */
    for (int i = 0; i < 16; i++) {
        if ((e->seg_mask >> i) & 1) e->seg_val[i] = segmentPointers[i];
    }
    /* inherited tile descriptors: values at entry */
    for (int t = 0; t < 8; t++) {
        if ((e->in_tiles >> t) & 1) e->in_tile[t] = cap.entry_tile[t];
    }
    /* snapshot of the verified bytes */
    size_t total = 0;
    for (McRange& r : e->ranges) {
        r.off = (uint32_t)total;
        total += r.len;
    }
    e->bytes.resize(total);
    for (const McRange& r : e->ranges) memcpy(e->bytes.data() + r.off, r.p, r.len);
    /* exit state */
    e->w_flags = cap.w_flags;
    e->w_oml = cap.w_oml;
    e->w_omh = cap.w_omh;
    e->w_tile_a = cap.w_tile_a;
    e->w_tile_b = cap.w_tile_b;
    mc_snap_scal(e->out);
    for (int t = 0; t < 8; t++) memcpy(e->out_tile[t].raw, &rdp.texture_tile[t], sizeof(McTile));
    for (uint16_t slot : cap.w_tmem) e->out_tmem.push_back(std::make_pair(slot, rdp.loaded_texture[slot]));
    memcpy(e->out_palette, rdp.palette, sizeof(rdp.palette));
    e->out_palette_addrs[0] = rdp.palette_addrs[0];
    e->out_palette_addrs[1] = rdp.palette_addrs[1];
    e->out_palette_hash = rdp.palette_hash;
    e->out_timg = rdp.texture_to_load;
    e->out_tc[0] = rdp.textures_changed[0];
    e->out_tc[1] = rdp.textures_changed[1];
    /* D985: mark the G_VTX ops whose vertices are still loaded when the list
     * ends (op.c = 1). The GPU replay skips G_VTX (its vertices are in the
     * mesh) but must leave rsp.loaded_vertices as the classic walk does: the
     * next list may draw with them (a replay-vs-classic check found the
     * vertex buffer differing after every GPU replay). */
    {
        bool covered[MAX_VERTICES] = { false };
        for (size_t i = e->ops.size(); i-- > 0;) {
            McOp& op = e->ops[i];
            if (op.k != MCO_VTX) continue;
            op.c = 0;
            for (int v = op.b; v < op.b + op.a && v < MAX_VERTICES; v++) {
                if (!covered[v]) {
                    op.c = 1;
                    covered[v] = true;
                }
            }
        }
    }
    e->ops.shrink_to_fit();
    e->segs.shrink_to_fit();
    return e;
}

/* --------------------------------------------------------------- entry */

#define MC_MAX_VARIANTS 4
#define MC_REFUSE_FRAMES 600
#define MC_STALE_WINDOW 300
#define MC_STALE_MAX 8
#define MC_MAX_SLOTS 8192

/* gfx_run_dl, pushed G_DL: true when the list was drawn here (replayed, or
 * walked while capturing), false = walk it classically. */
static bool mc_try(Gfx* dl) {
    if (!mc_on() || s_mc_cap != nullptr) return false;
    s_mc_cnt_try++;
    if (s_interp_mode != INTERP_OFF) {
        s_mc_cnt_reason[MCR_INTERP]++;
        return false;
    }
    const bool perf = gfx_perfstat_on() != 0;
    const uint64_t t0 = perf ? gfx_perf_now_ns() : 0;
    if (s_mc_map.size() > MC_MAX_SLOTS) {
        for (auto& kv : s_mc_map) {
            for (McEntry* v : kv.second.v) mc_free_entry(v);
        }
        s_mc_map.clear();
    }
    McSlot& slot = s_mc_map[(uintptr_t)dl];
    slot.last_used = s_mc_frame;
    if (slot.refuse_until > s_mc_frame) {
        s_mc_cnt_reason[MCR_REFUSED_SKIP]++;
        return false;
    }
    const struct GfxClipParameters clip = gfx_rapi->get_clip_parameters();
    int reason = slot.v.empty() ? MCR_NEW : MCR_VARIANT;
    for (size_t vi = 0; vi < slot.v.size(); vi++) {
        McEntry* e = slot.v[vi];
        int why = MCR_FP;
        if (!mc_fp_ok(e, clip, &why)) {
            reason = why;
            continue;
        }
        const uint64_t tv = perf ? gfx_perf_now_ns() : 0;
        const bool bytes_ok = mc_bytes_ok(e);
        if (perf) s_mc_ns_verify += gfx_perf_now_ns() - tv;
        if (!bytes_ok || !mc_nodes_ok(e)) {
            reason = bytes_ok ? MCR_TEXGONE : MCR_STALE;
            mc_free_entry(e);
            slot.v.erase(slot.v.begin() + vi);
            if (!bytes_ok) {
                if (s_mc_frame - slot.stale_frame0 > MC_STALE_WINDOW) {
                    slot.stale_frame0 = s_mc_frame;
                    slot.stales = 0;
                }
                if (++slot.stales > MC_STALE_MAX) {
                    /* edited every frame (D967: an animated G_SETPRIMCOLOR, a
                     * game-built list): leave it to the classic path for a while */
                    slot.refuse_until = s_mc_frame + MC_REFUSE_FRAMES;
                    for (McEntry* v : slot.v) mc_free_entry(v);
                    slot.v.clear();
                    s_mc_cnt_reason[reason]++;
                    mc_note_refusal("stale-every-frame", 0, dl);
                    return false;
                }
            }
            break;
        }
        /* hit */
        e->last_used = s_mc_frame;
        if (vi != 0) std::swap(slot.v[0], slot.v[vi]);
        e->hits++;
        bool drawn = false;
        if (mc_gpu_on()) {
            const uint64_t tg = perf ? gfx_perf_now_ns() : 0;
            drawn = mc_replay_gpu(e);
            if (perf && drawn) s_mc_g_ns += gfx_perf_now_ns() - tg;
            if (!drawn && (s_mc_g_last == 1 || s_mc_g_last == 2) && e->gpu_bad == nullptr && e->hits >= 3 &&
                s_mc_g_bakes_frame < MC_GPU_BAKES_PER_FRAME) {
                /* bake the GPU mesh during this CPU replay */
                static McBake bk;
                uint64_t key, sig, pk, grp[8];
                mc_gpu_prepass(e, &key, &sig, &pk, grp);
                memset(bk.src, 0, sizeof(bk.src));
                bk.slot = 0;
                bk.bad = nullptr;
                bk.env_in_list = false;
                bk.new_run = false;
                bk.data.clear();
                bk.runs.clear();
                for (int k = 0; k < GFX_MESH_MAX_MTX; k++) {
                    bk.box[k][0] = bk.box[k][1] = bk.box[k][2] = 1e30f;
                    bk.box[k][3] = bk.box[k][4] = bk.box[k][5] = -1e30f;
                }
                mc_replay(e, &bk);
                mc_gpu_commit(e, &bk, key, sig, pk, grp);
                s_mc_g_bakes_frame++;
                drawn = true;
            }
        }
        if (!drawn) mc_replay(e);
        s_mc_cnt_hit++;
        s_mc_cnt_hit_tris += e->ntris;
        if (perf) s_mc_ns_replay += gfx_perf_now_ns() - t0;
        return true;
    }
    s_mc_cnt_reason[reason]++;
    /* miss: walk it while recording */
    s_mc_cnt_cap++;
    McEntry* e = mc_capture(dl);
    if (perf) s_mc_ns_cap += gfx_perf_now_ns() - t0;
    if (e == nullptr) {
        slot.refuse_until = s_mc_frame + MC_REFUSE_FRAMES;
        return true;   /* it was drawn by the capture walk */
    }
    s_mc_cnt_cap_ok++;
    if (slot.v.size() >= MC_MAX_VARIANTS) {
        mc_free_entry(slot.v.back());
        slot.v.pop_back();
    }
    slot.v.insert(slot.v.begin(), e);
    return true;
}

/* gfx_frame_body: once per pass. */
static void mc_frame_begin(void) {
    if (!mc_on()) return;
    s_mc_frame++;
    s_mc_g_bakes_frame = 0;
    for (uint32_t id : s_mc_gpu_dead) gfx_rapi->mesh_delete(id);
    s_mc_gpu_dead.clear();
    if ((s_mc_frame % 300) == 0) {
        /* lists not drawn for a while (unloaded rooms, models out of view) */
        for (auto it = s_mc_map.begin(); it != s_mc_map.end();) {
            if (s_mc_frame - it->second.last_used > 900) {
                for (McEntry* v : it->second.v) mc_free_entry(v);
                it = s_mc_map.erase(it);
            } else {
                ++it;
            }
        }
    }
}

/* reset_texture_state: combiners and shaders are gone, drop every entry. */
static void mc_clear_all(void) {
    for (auto& kv : s_mc_map) {
        for (McEntry* v : kv.second.v) mc_free_entry(v);
    }
    s_mc_map.clear();
    s_mc_texgen++;
}

static void mc_perf_report(double n) {
    if (!mc_on()) return;
    size_t entries = 0, bytes = 0;
    for (const auto& kv : s_mc_map) {
        for (const McEntry* e : kv.second.v) {
            entries++;
            bytes += e->bytes.size() + e->ops.size() * sizeof(McOp) + e->segs.size() * sizeof(McSeg);
        }
    }
    /* stderr (nxlink) and the game log (Ryujinx shows no stderr) */
    std::string line;
    char buf[512];
    snprintf(buf, sizeof(buf),
             "PERFSTAT meshcache: tries=%.1f/f hits=%.1f/f (%.0f%%) hit-tris=%.0f/f captures=%.1f/f (ok %.1f) | "
             "replay=%.2fms verify=%.2fms capture=%.2fms | slots=%zu entries=%zu %zuKiB",
             s_mc_cnt_try / n, s_mc_cnt_hit / n, s_mc_cnt_try ? 100.0 * s_mc_cnt_hit / s_mc_cnt_try : 0.0,
             s_mc_cnt_hit_tris / n, s_mc_cnt_cap / n, s_mc_cnt_cap_ok / n, s_mc_ns_replay / 1.0e6 / n,
             s_mc_ns_verify / 1.0e6 / n, s_mc_ns_cap / 1.0e6 / n, s_mc_map.size(), entries, bytes / 1024);
    fprintf(stderr, "%s\n", buf);
    sysLogPrintf(LOG_NOTE, "%s", buf);
    line = "PERFSTAT meshcache misses:";
    for (int r = 0; r < MCR_COUNT; r++) {
        snprintf(buf, sizeof(buf), " %s=%.1f", s_mc_reason_name[r], s_mc_cnt_reason[r] / n);
        line += buf;
    }
    line += " (per frame)";
    fprintf(stderr, "%s\n", line.c_str());
    sysLogPrintf(LOG_NOTE, "%s", line.c_str());
    if (!s_mc_refusals.empty()) {
        line = "PERFSTAT meshcache refusals:";
        for (const auto& kv : s_mc_refusals) {
            snprintf(buf, sizeof(buf), " %s=%llu", kv.first.c_str(), (unsigned long long)kv.second);
            line += buf;
        }
        line += " (per report)";
        fprintf(stderr, "%s\n", line.c_str());
        sysLogPrintf(LOG_NOTE, "%s", line.c_str());
    }
    if (!s_mc_stale_ops.empty()) {
        line = "PERFSTAT meshcache stale-at:";
        for (const auto& kv : s_mc_stale_ops) {
            snprintf(buf, sizeof(buf), " %s=%llu", kv.first.c_str(), (unsigned long long)kv.second);
            line += buf;
        }
        line += " (per report)";
        fprintf(stderr, "%s\n", line.c_str());
        sysLogPrintf(LOG_NOTE, "%s", line.c_str());
    }
    if (s_mc_t_replays) {
        /* clock overhead per read, to discount the split (about 2 reads per triangle) */
        const uint64_t c0 = gfx_perf_now_ns();
        for (int i = 0; i < 1000; i++) (void)gfx_perf_now_ns();
        const double clk = (double)(gfx_perf_now_ns() - c0) / 1000.0;
        line = "PERFSTAT meshcache split (1 replay in 8, x8 per frame):";
        for (int k = 0; k < MCT_COUNT; k++) {
            snprintf(buf, sizeof(buf), " %s=%.2fms", s_mc_t_name[k], 8.0 * s_mc_t_ns[k] / 1.0e6 / n);
            line += buf;
        }
        snprintf(buf, sizeof(buf), " | tris=%.0f rejected=%.0f emitted=%.0f per frame | clock read %.0fns",
                 8.0 * s_mc_t_tris / n, 8.0 * s_mc_t_rej / n, 8.0 * s_mc_t_emitted / n, clk);
        line += buf;
        fprintf(stderr, "%s\n", line.c_str());
        sysLogPrintf(LOG_NOTE, "%s", line.c_str());
    }
    if (mc_gpu_on()) {
        snprintf(buf, sizeof(buf),
                 "PERFSTAT meshgpu: replays=%.1f/f runs=%.1f/f tris=%.0f/f time=%.2fms | bakes=%llu (%llu KiB) |",
                 s_mc_g_replays / n, s_mc_g_runs / n, s_mc_g_tris / n, s_mc_g_ns / 1.0e6 / n,
                 (unsigned long long)s_mc_g_bakes, (unsigned long long)(s_mc_g_bake_bytes / 1024));
        line = buf;
        for (const auto& kv : s_mc_g_why) {
            snprintf(buf, sizeof(buf), " %s=%.1f", kv.first.c_str(), kv.second / n);
            line += buf;
        }
        line += " (CPU replays per frame, by reason)";
        fprintf(stderr, "%s\n", line.c_str());
        sysLogPrintf(LOG_NOTE, "%s", line.c_str());
    }
    s_mc_g_replays = s_mc_g_runs = s_mc_g_tris = s_mc_g_ns = s_mc_g_bakes = s_mc_g_bake_bytes = 0;
    s_mc_g_why.clear();
    memset(s_mc_t_ns, 0, sizeof(s_mc_t_ns));
    s_mc_t_tris = s_mc_t_rej = s_mc_t_emitted = s_mc_t_replays = 0;
    s_mc_cnt_try = s_mc_cnt_hit = s_mc_cnt_hit_tris = s_mc_cnt_cap = s_mc_cnt_cap_ok = 0;
    memset(s_mc_cnt_reason, 0, sizeof(s_mc_cnt_reason));
    s_mc_ns_replay = s_mc_ns_verify = s_mc_ns_cap = 0;
    s_mc_refusals.clear();
    s_mc_stale_ops.clear();
}
