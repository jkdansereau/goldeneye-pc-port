/* D973: deko3d rendering backend (Switch only, the default there; GE_RENDERER=gl selects GL).
 *
 * A GfxRenderingAPI that the frame replay (gfx_frame.cpp) drives exactly like
 * the GL backend: same call order, same state semantics (gfx_opengl.cpp is the
 * reference for every mapping below). The decoder never sees a difference.
 *
 * Orientation (measured, Ryujinx and hardware agree): with the device in
 * deko3d's native upper-left mode, NDC y = +1 lands on memory row 0, viewport
 * and scissor y count from the top, gl_FragCoord.y runs top-down, a texture's
 * row 0 is t = 0 and the display shows row 0 at the top. GL is the mirror
 * image: its rendered images have row 0 at the bottom. So every render target
 * (fb 0 included) is drawn the right way up for the display, and GL's
 * bottom-up coordinates are mirrored at the boundary: viewport / scissor /
 * clear-region y' = H - y - h, blit rectangles mirrored on both sides (their
 * relative orientation, and copy_framebuffer's flip, unchanged), and a
 * framebuffer sampled as a texture gets t' = 1 - t in the shader (Draw block
 * flip_v0/1). The FXAA and GE+ sky passes take their viewport position from a
 * varying of the fullscreen triangle (GL's y-up convention) and FXAA samples
 * its source with t mirrored. Face culling is never enabled (Fast3D culls on
 * the CPU), so the mirrored winding does not matter.
 *
 * Frames: one command buffer fed with slices of a command-memory ring; one
 * data ring (vertices, upload staging); images in a first-fit heap. Each
 * submission gets a serial and a DkFence; memory, image descriptors and the
 * swapchain-dependent resources are recycled only after the serial that last
 * used them has completed (gfx_dk_heap.h). Combiner shaders come prebuilt from
 * the shader pack embedded in the NRO (gfx_dk_pack.S); a combiner missing from
 * it is drawn by the uber-shader and logged once ("DKSH miss"). */

#ifdef __SWITCH__

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <map>
#include <vector>
#include <utility>
#include <chrono>

#include <deko3d.h>

/* <switch.h> and <PR/gbi.h> cannot meet in one TU (u64/s64 typedefs differ):
 * the few libnx / GBI names used here are declared directly. */
extern "C" void* nwindowGetDefault(void);
#define G_TX_NOMIRROR 0x0
#define G_TX_WRAP 0x0
#define G_TX_MIRROR 0x1
#define G_TX_CLAMP 0x2
#define ZMODE_OPA 0
#define ZMODE_INTER 0x400
#define ZMODE_XLU 0x800
#define ZMODE_DEC 0xc00

#include "system.h"
#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "gfx_deko3d.h"
#include "gfx_dk_layout.h"
#include "gfx_dk_heap.h"
#include "skyplus.h"

extern bool gfx_framebuffers_enabled;   /* gfx_pc.cpp */
extern int gfx_rdp_affine;              /* gfx_pc.cpp (D540) */
extern int gfx_fog_vertex;              /* gfx_pc.cpp (D543) */
extern "C" volatile int gfx_skyplus_failed;   /* gfx_opengl.cpp */
extern "C" int nxIsHandheld(void);      /* port/src/switch/nxmem.c */
extern "C" const uint8_t ge007_dksh_pack[], ge007_dksh_pack_end[];

struct ShaderProgram {
    DkShader vert, frag;
    bool uber;
    struct DkCombinerParams params;
    struct DkLayout layout;
    uint8_t num_inputs;
    bool used_textures[2];
    DkVtxAttribState attribs[DK_ATTR_COUNT];
    uint32_t stride;
};

namespace {

/* ---------------- device, memory, submissions ---------------- */

const uint32_t DATA_CHUNK = 8u << 20;
const uint32_t CMD_CHUNK = 2u << 20;
const uint32_t CMD_SLICE = 256u << 10;
const uint32_t CMD_SLACK = 256;          /* deko3d 0.5.0 writes a few words past the end of command memory */
const uint32_t IMG_CHUNK = 32u << 20;
const uint32_t NUM_IMG_DESC = 4096;
const uint32_t NUM_SMP_DESC = 512;
const int NUM_FENCES = 16;
const int FRAMES_IN_FLIGHT = 2;

struct Block {
    DkMemBlock mb = nullptr;
    uint8_t* cpu = nullptr;
    DkGpuAddr gpu = 0;
    uint32_t size = 0;
};

bool s_up = false, s_parked = false;
DkDevice s_dev;
DkQueue s_queue;
DkCmdBuf s_cmd;
DkRing s_dataRing(DATA_CHUNK), s_cmdRing(CMD_CHUNK);
std::vector<Block> s_dataBlocks, s_cmdBlocks, s_imgBlocks;
DkHeap s_imgHeap(IMG_CHUNK);
DkSlots s_imgSlots(NUM_IMG_DESC);
Block s_desc, s_ubo, s_query, s_code;
uint64_t s_serial = 1, s_completed = 0;
bool s_cmd_pending = false;   /* something recorded since the last submission */

struct InFlight { bool used; uint64_t serial; DkFence fence; };
InFlight s_fl[NUM_FENCES];
uint64_t s_frame_serial[FRAMES_IN_FLIGHT + 1];   /* last serial of each recent frame */
unsigned s_frame_idx = 0;

/* PERFSTAT counters */
uint64_t s_st_draws, s_st_pushes, s_st_chunks, s_st_uber_draws, s_st_submits, s_st_uploads;
/* CPU time the replay thread spends blocked: frame throttle / fence-slot reuse,
 * swapchain acquire (nwindow dequeue), queue idle waits */
uint64_t s_st_wait_frame_ns, s_st_wait_acq_ns, s_st_wait_idle_ns, s_st_submit_ns;
inline uint64_t dk_now_ns(void) {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
/* draws recorded since the last submission: past this many, submit at the next
 * draw so the GPU starts while the replay is still recording (GE_DKSUBMIT) */
uint32_t s_draws_since_submit = 0;
uint32_t s_submit_every = 0;

int s_dbg_logged = 0;
void dk_debug(void*, const char* context, DkResult result, const char* message) {
    if (s_dbg_logged < 64) {
        s_dbg_logged++;
        sysLogPrintf(LOG_ERROR, "DK: deko3d error %d in %s: %s", (int)result, context ? context : "?", message ? message : "");
    }
}

Block make_block(uint32_t size, uint32_t flags) {
    Block b;
    size = (size + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1);
    DkMemBlockMaker m;
    dkMemBlockMakerDefaults(&m, s_dev, size);
    m.flags = flags;
    b.mb = dkMemBlockCreate(&m);
    if (b.mb == nullptr) {
        sysFatalError("deko3d: could not allocate %u KiB of GPU memory", (unsigned)(size >> 10));
    }
    b.cpu = (flags & DkMemBlockFlags_CpuAccessMask) ? (uint8_t*)dkMemBlockGetCpuAddr(b.mb) : nullptr;
    b.gpu = dkMemBlockGetGpuAddr(b.mb);
    b.size = size;
    return b;
}

void free_block(Block& b) {
    if (b.mb) dkMemBlockDestroy(b.mb);
    b = Block();
}

/* allocation in a ring; returns the CPU pointer, GPU address via *gpu */
uint8_t* ring_alloc(DkRing& r, std::vector<Block>& blocks, uint32_t size, uint32_t align, DkGpuAddr* gpu,
                    Block** blk = nullptr, uint32_t* off = nullptr) {
    DkRingAlloc a;
    r.alloc(size, align, s_serial, &a);
    if (a.new_chunk) {
        blocks.push_back(make_block(r.chunk_size(a.chunk) + (&r == &s_cmdRing ? 0 : 0),
                                    DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached));
        s_st_chunks++;
    }
    Block& b = blocks[a.chunk];
    if (gpu) *gpu = b.gpu + a.offset;
    if (blk) *blk = &b;
    if (off) *off = a.offset;
    return b.cpu + a.offset;
}

void give_cmd_memory(void) {
    Block* b;
    uint32_t off;
    ring_alloc(s_cmdRing, s_cmdBlocks, CMD_SLICE, DK_CMDMEM_ALIGNMENT, nullptr, &b, &off);
    dkCmdBufAddMemory(s_cmd, b->mb, off, CMD_SLICE - CMD_SLACK);
}

void cmd_add_mem(void*, DkCmdBuf, size_t min_req) {
    if (min_req + CMD_SLACK > CMD_SLICE) {
        sysFatalError("deko3d: command of %u bytes", (unsigned)min_req);
    }
    give_cmd_memory();
}

void retire(void) {
    for (;;) {
        InFlight* oldest = nullptr;
        for (int i = 0; i < NUM_FENCES; i++) {
            if (s_fl[i].used && (oldest == nullptr || s_fl[i].serial < oldest->serial)) oldest = &s_fl[i];
        }
        if (oldest == nullptr || dkFenceWait(&oldest->fence, 0) != DkResult_Success) break;
        s_completed = oldest->serial;
        oldest->used = false;
    }
    s_dataRing.retire(s_completed);
    s_cmdRing.retire(s_completed);
    s_imgHeap.retire(s_completed);
    s_imgSlots.retire(s_completed);
}

void wait_serial(uint64_t serial) {
    if (serial == 0 || serial <= s_completed) return;
    const uint64_t t0 = dk_now_ns();
    for (int i = 0; i < NUM_FENCES; i++) {
        if (s_fl[i].used && s_fl[i].serial <= serial) {
            dkFenceWait(&s_fl[i].fence, -1);
        }
    }
    s_st_wait_frame_ns += dk_now_ns() - t0;
    retire();
}

void submit(void) {
    if (!s_up) return;
    const uint64_t t0 = dk_now_ns();
    s_draws_since_submit = 0;
    DkCmdList l = dkCmdBufFinishList(s_cmd);
    dkQueueSubmitCommands(s_queue, l);
    InFlight& f = s_fl[s_serial % NUM_FENCES];
    if (f.used) {
        const uint64_t tw = dk_now_ns();
        dkFenceWait(&f.fence, -1);
        s_st_wait_frame_ns += dk_now_ns() - tw;
        f.used = false;
    }
    dkQueueSignalFence(s_queue, &f.fence, true);
    f.used = true;
    f.serial = s_serial;
    dkQueueFlush(s_queue);
    s_serial++;
    s_st_submits++;
    s_cmd_pending = false;
    dkCmdBufClear(s_cmd);
    retire();
    give_cmd_memory();
    s_st_submit_ns += dk_now_ns() - t0;
}

/* ---------------- textures, samplers, descriptors ---------------- */

struct SmpKey {
    uint8_t min, mag, mip, ws, wt, aniso;
    bool operator<(const SmpKey& o) const { return memcmp(this, &o, sizeof(*this)) < 0; }
};
std::map<SmpKey, int> s_smp_slots;
bool s_desc_dirty = false;   /* new descriptors written: invalidate the descriptor cache before the next use */

DkImageDescriptor* img_desc(int slot) { return (DkImageDescriptor*)(s_desc.cpu + slot * sizeof(DkImageDescriptor)); }
DkSamplerDescriptor* smp_desc(int slot) {
    return (DkSamplerDescriptor*)(s_desc.cpu + NUM_IMG_DESC * sizeof(DkImageDescriptor) + slot * sizeof(DkSamplerDescriptor));
}

int sampler_slot(const SmpKey& k) {
    auto it = s_smp_slots.find(k);
    if (it != s_smp_slots.end()) return it->second;
    int slot = (int)s_smp_slots.size();
    if (slot >= (int)NUM_SMP_DESC) {
        sysLogPrintf(LOG_WARNING, "DK: sampler descriptors exhausted; reusing slot 0");
        return 0;
    }
    DkSampler s;
    dkSamplerDefaults(&s);
    s.minFilter = (DkFilter)k.min;
    s.magFilter = (DkFilter)k.mag;
    s.mipFilter = (DkMipFilter)k.mip;
    s.wrapMode[0] = (DkWrapMode)k.ws;
    s.wrapMode[1] = (DkWrapMode)k.wt;
    s.wrapMode[2] = DkWrapMode_Repeat;
    s.maxAnisotropy = (float)k.aniso;
    dkSamplerDescriptorInitialize(smp_desc(slot), &s);
    s_smp_slots[k] = slot;
    s_desc_dirty = true;
    return slot;
}

/* GL's defaults for a fresh texture object: NEAREST_MIPMAP_LINEAR / LINEAR / REPEAT */
SmpKey default_tex_sampler(void) {
    return SmpKey{ DkFilter_Nearest, DkFilter_Linear, DkMipFilter_Linear, DkWrapMode_Repeat, DkWrapMode_Repeat, 1 };
}

struct Tex {
    bool alive = false;
    bool has_img = false;
    DkImage img;
    DkHeapBlock blk;
    int slot = -1;
    uint32_t w = 0, h = 0, levels = 0;
    SmpKey smp;
};
std::vector<Tex> s_tex(1);              /* id 0 = GL's "no texture" */
std::vector<uint32_t> s_free_ids;
int s_dummy_slot = 0;
Block s_dummy_mem;
DkImage s_dummy_img;

void free_image(DkHeapBlock& blk, int& slot) {
    s_imgHeap.free_after(blk, s_serial);
    if (slot >= 0) s_imgSlots.release_after(slot, s_serial);
    slot = -1;
}

bool alloc_image(DkImageLayout* layout, DkImage* img, DkHeapBlock* blk) {
    const uint64_t size = dkImageLayoutGetSize(layout);
    const uint32_t align = dkImageLayoutGetAlignment(layout);
    if (!s_imgHeap.alloc((uint32_t)size, align, blk)) return false;
    if (s_imgHeap.grew()) {
        s_imgBlocks.push_back(make_block(s_imgHeap.chunk_size(blk->chunk), DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image));
    }
    dkImageInitialize(img, layout, s_imgBlocks[blk->chunk].mb, blk->offset);
    return true;
}

int write_img_desc(const DkImage* img, bool alpha_one) {
    int slot = s_imgSlots.take();
    if (slot < 0) {
        retire();
        slot = s_imgSlots.take();
        if (slot < 0) {
            sysLogPrintf(LOG_ERROR, "DK: image descriptors exhausted");
            return s_dummy_slot;
        }
    }
    DkImageView v;
    dkImageViewDefaults(&v, img);
    if (alpha_one) v.swizzle[3] = DkImageSwizzle_One;   /* GL's framebuffer textures are RGB8: alpha reads 1 */
    dkImageDescriptorInitialize(img_desc(slot), &v, false, false);
    s_desc_dirty = true;
    return slot;
}

/* ---------------- framebuffers ---------------- */

struct Fb {
    uint32_t w = 1, h = 1, msaa = 1;
    bool has_depth = false, invert_y = false;
    bool created = false;           /* off-screen images exist */
    DkImage color, color_ms, depth;
    DkHeapBlock blk_color, blk_ms, blk_depth;
    bool has_ms = false, has_depth_img = false;
    int color_slot = -1;
    SmpKey smp;                     /* the colour texture's sampling state (GL: LINEAR, REPEAT) */
};
std::vector<Fb> s_fbs;

/* window (fb 0) */
const int NUM_SWAP = 3;   /* triple buffering: the acquire rarely blocks on the display */
DkSwapchain s_swapchain = nullptr;
DkImage s_win_img[NUM_SWAP];
DkImage s_win_depth;
Block s_win_mem;
uint32_t s_win_w = 1280, s_win_h = 720;
volatile uint32_t s_dims_w = 1280, s_dims_h = 720;
bool s_handheld = true;
int s_acq = -1;                     /* acquired swapchain slot this frame, -1 = none */
int s_swap_interval = 1;

/* ---------------- render state (GL's, as last set) ---------------- */

int s_cur_fb = 0;
bool s_target_dirty = true;
int s_bound_fb = -2;
DkMsMode s_bound_ms = DkMsMode_1x;
float s_noise_scale = 1.0f;
uint32_t s_frame_count = 0;

int s_vp[4] = { 0, 0, 1, 1 }, s_sc[4] = { 0, 0, 1, 1 };
bool s_vp_dirty = true, s_sc_dirty = true;
float s_znear = 0.0f, s_zfar = 1.0f;

bool s_depth_test = false, s_depth_mask = true, s_decal = false;
DkCompareOp s_depth_func = DkCompareOp_Lequal;
bool s_blend = false, s_modulate = false;

struct Unit { uint8_t kind; uint32_t id; };   /* kind: 0 none, 1 texture, 2 framebuffer */
Unit s_unit[2] = {};
int s_active_unit = 0;
bool s_tex_dirty = true;
bool s_barrier_pending = false;     /* image writes (uploads, blits) before the next draw */
bool s_linear_flag[2] = { false, false };

enum FilteringMode s_filter = FILTER_LINEAR;
enum MipmapFilteringMode s_mipfilter = MIPMAP_LINEAR;
int s_aniso = 4;

ShaderProgram* s_prg = nullptr;
struct DkDrawUniforms s_uni = {};

/* streaming (stream_begin) */
float* s_stream_ptr = nullptr;
DkGpuAddr s_stream_gpu = 0;

/* ---------------- shader pack ---------------- */

struct PackShader {
    const uint8_t* blob;
    uint32_t code_off;   /* in s_code */
};
const DkPackEntry* s_pack = nullptr;
uint32_t s_pack_count = 0;
std::vector<PackShader> s_pack_shaders;   /* 2 per entry (vert, frag) */
std::map<std::pair<uint64_t, uint32_t>, ShaderProgram*> s_progs;
std::map<std::pair<uint64_t, uint32_t>, bool> s_missed;
DkShader s_uber_vert, s_uber_frag, s_fs_vert, s_fxaa_frag, s_sky_frag;
bool s_sky_ok = false, s_fxaa_ok = false;

struct DkshHeader { uint32_t magic, header_sz, control_sz, code_sz, programs_off, num_programs; };

int pack_find(uint64_t id0, uint32_t id1) {
    int lo = 0, hi = (int)s_pack_count - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const DkPackEntry& e = s_pack[mid];
        if (e.id0 == id0 && e.id1 == id1) return mid;
        if (e.id0 < id0 || (e.id0 == id0 && e.id1 < id1)) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

bool pack_shader(int entry, int stage, DkShader* out) {
    if (entry < 0) return false;
    const PackShader& ps = s_pack_shaders[entry * 2 + stage];
    if (ps.blob == nullptr) return false;
    DkShaderMaker m;
    dkShaderMakerDefaults(&m, s_code.mb, ps.code_off);
    m.control = ps.blob;
    m.programId = 0;
    dkShaderInitialize(out, &m);
    return dkShaderIsValid(out);
}

void load_pack(void) {
    const uint8_t* p = ge007_dksh_pack;
    if ((size_t)(ge007_dksh_pack_end - p) < 16 || memcmp(p, DK_PACK_MAGIC, 8) != 0) {
        sysFatalError("deko3d: shader pack missing or corrupt");
    }
    memcpy(&s_pack_count, p + 8, 4);
    s_pack = (const DkPackEntry*)(p + 16);
    s_pack_shaders.assign(s_pack_count * 2, PackShader{ nullptr, 0 });
    uint32_t total = 0;
    for (uint32_t i = 0; i < s_pack_count; i++) {
        const uint32_t offs[2] = { s_pack[i].vert_off, s_pack[i].frag_off }, lens[2] = { s_pack[i].vert_len, s_pack[i].frag_len };
        for (int s = 0; s < 2; s++) {
            if (lens[s] == 0) continue;
            const DkshHeader* h = (const DkshHeader*)(p + offs[s]);
            total += (h->code_sz + DK_SHADER_CODE_ALIGNMENT - 1) & ~(DK_SHADER_CODE_ALIGNMENT - 1);
        }
    }
    s_code = make_block(total + DK_SHADER_CODE_UNUSABLE_SIZE + DK_SHADER_CODE_ALIGNMENT,
                        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
    uint32_t off = 0;
    for (uint32_t i = 0; i < s_pack_count; i++) {
        const uint32_t offs[2] = { s_pack[i].vert_off, s_pack[i].frag_off }, lens[2] = { s_pack[i].vert_len, s_pack[i].frag_len };
        for (int s = 0; s < 2; s++) {
            if (lens[s] == 0) continue;
            const uint8_t* blob = p + offs[s];
            const DkshHeader* h = (const DkshHeader*)blob;
            memcpy(s_code.cpu + off, blob + h->control_sz, h->code_sz);
            s_pack_shaders[i * 2 + s] = PackShader{ blob, off };
            off += (h->code_sz + DK_SHADER_CODE_ALIGNMENT - 1) & ~(DK_SHADER_CODE_ALIGNMENT - 1);
        }
    }
    const int uber = pack_find(0, DK_PACK_UBER);
    if (!pack_shader(uber, 0, &s_uber_vert) || !pack_shader(uber, 1, &s_uber_frag)) {
        sysFatalError("deko3d: uber-shader missing from the shader pack");
    }
    const bool fs = pack_shader(pack_find(0, DK_PACK_FULLSCREEN), 0, &s_fs_vert);
    s_fxaa_ok = fs && pack_shader(pack_find(0, DK_PACK_FXAA), 1, &s_fxaa_frag);
    s_sky_ok = fs && pack_shader(pack_find(0, DK_PACK_SKYPLUS), 1, &s_sky_frag);
    int combiners = 0;
    for (uint32_t i = 0; i < s_pack_count; i++) combiners += (s_pack[i].id0 != 0 || s_pack[i].id1 >= DK_PACK_NAMED_MAX);
    sysLogPrintf(LOG_NOTE, "DK: shader pack: %d combiners + uber, %u KiB of code", combiners, (unsigned)(total >> 10));
}

/* ---------------- window ---------------- */

void create_window(void) {
    s_handheld = nxIsHandheld() != 0;
    s_win_w = s_handheld ? 1280 : 1920;
    s_win_h = s_handheld ? 720 : 1080;
    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, s_dev);
    lm.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_Usage2DEngine;
    lm.format = DkImageFormat_RGBA8_Unorm;
    lm.dimensions[0] = s_win_w;
    lm.dimensions[1] = s_win_h;
    DkImageLayout cl;
    dkImageLayoutInitialize(&cl, &lm);
    dkImageLayoutMakerDefaults(&lm, s_dev);
    lm.flags = DkImageFlags_UsageRender | DkImageFlags_HwCompression;
    lm.format = DkImageFormat_Z24S8;
    lm.dimensions[0] = s_win_w;
    lm.dimensions[1] = s_win_h;
    DkImageLayout dl;
    dkImageLayoutInitialize(&dl, &lm);
    const uint32_t ca = dkImageLayoutGetAlignment(&cl), da = dkImageLayoutGetAlignment(&dl);
    const uint32_t cs = ((uint32_t)dkImageLayoutGetSize(&cl) + ca - 1) / ca * ca;
    const uint32_t ds = (uint32_t)dkImageLayoutGetSize(&dl);
    uint32_t doff = (cs * NUM_SWAP + da - 1) / da * da;
    s_win_mem = make_block(doff + ds, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    const DkImage* imgs[NUM_SWAP];
    for (int i = 0; i < NUM_SWAP; i++) {
        dkImageInitialize(&s_win_img[i], &cl, s_win_mem.mb, cs * i);
        imgs[i] = &s_win_img[i];
    }
    dkImageInitialize(&s_win_depth, &dl, s_win_mem.mb, doff);
    DkSwapchainMaker sm;
    dkSwapchainMakerDefaults(&sm, s_dev, nwindowGetDefault(), imgs, NUM_SWAP);
    s_swapchain = dkSwapchainCreate(&sm);
    if (s_swapchain == nullptr) sysFatalError("deko3d: could not create the swapchain");
    dkSwapchainSetSwapInterval(s_swapchain, (uint32_t)s_swap_interval);
    s_dims_w = s_win_w;
    s_dims_h = s_win_h;
    s_acq = -1;
    s_target_dirty = true;
    sysLogPrintf(LOG_NOTE, "DK: swapchain %ux%u (%s)", s_win_w, s_win_h, s_handheld ? "handheld" : "docked");
}

void destroy_window(void) {
    if (s_swapchain) {
        dkSwapchainDestroy(s_swapchain);
        s_swapchain = nullptr;
    }
    free_block(s_win_mem);
    s_acq = -1;
}

void acquired_hook(void);
void ensure_acquired(void) {
    if (s_acq < 0 && s_swapchain) {
        const uint64_t t0 = dk_now_ns();
        s_acq = dkQueueAcquireImage(s_queue, s_swapchain);
        s_st_wait_acq_ns += dk_now_ns() - t0;
        acquired_hook();
    }
}

/* ---------------- state application ---------------- */

bool is_window(int fb) { return fb == 0; }
uint32_t fb_height(int fb) { return is_window(fb) ? s_win_h : s_fbs[fb].h; }
uint32_t fb_width(int fb) { return is_window(fb) ? s_win_w : s_fbs[fb].w; }

void bind_rasterizer(void) {
    DkRasterizerState r;
    dkRasterizerStateDefaults(&r);
    r.cullMode = DkFace_None;
    r.depthClampEnable = 1;   /* GL_DEPTH_CLAMP (gfx_opengl_init) */
    r.depthBiasEnableMask = s_decal ? DkPolygonFlag_All : 0;
    dkCmdBufBindRasterizerState(s_cmd, &r);
}

void bind_depth(void) {
    DkDepthStencilState d;
    dkDepthStencilStateDefaults(&d);
    d.depthTestEnable = s_depth_test;
    d.depthWriteEnable = s_depth_test && s_depth_mask;   /* GL writes no depth with the test off */
    d.depthCompareOp = s_depth_func;
    dkCmdBufBindDepthStencilState(s_cmd, &d);
}

void bind_blend(void) {
    DkColorState c;
    dkColorStateDefaults(&c);
    dkColorStateSetBlendEnable(&c, 0, s_blend);
    dkCmdBufBindColorState(s_cmd, &c);
    DkBlendState b;
    dkBlendStateDefaults(&b);
    if (s_modulate) {
        dkBlendStateSetFactors(&b, DkBlendFactor_DstColor, DkBlendFactor_Zero, DkBlendFactor_DstAlpha, DkBlendFactor_Zero);
    } else {
        dkBlendStateSetFactors(&b, DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha, DkBlendFactor_SrcAlpha,
                               DkBlendFactor_InvSrcAlpha);
    }
    dkCmdBufBindBlendState(s_cmd, 0, &b);
}

void apply_viewport(void) {
    const int fb = s_cur_fb;
    DkViewport v;
    v.x = (float)s_vp[0];
    v.width = (float)s_vp[2];
    v.height = (float)s_vp[3];
    v.y = (float)((int)fb_height(fb) - s_vp[1] - s_vp[3]);   /* GL bottom-up -> deko3d top-down */
    v.near = s_znear;
    v.far = s_zfar;
    dkCmdBufSetViewports(s_cmd, 0, &v, 1);
    s_vp_dirty = false;
}

void set_scissor_rect(int x, int y, int w, int h) {
    const int H = (int)fb_height(s_cur_fb), W = (int)fb_width(s_cur_fb);
    y = H - y - h;   /* GL bottom-up -> deko3d top-down */
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > W ? W : x + w, y1 = y + h > H ? H : y + h;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    DkScissor s = { (uint32_t)x0, (uint32_t)y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0) };
    dkCmdBufSetScissors(s_cmd, 0, &s, 1);
}

void apply_scissor(void) {
    set_scissor_rect(s_sc[0], s_sc[1], s_sc[2], s_sc[3]);
    s_sc_dirty = false;
}

DkMsMode ms_mode(uint32_t msaa) {
    return msaa >= 8 ? DkMsMode_8x : msaa >= 4 ? DkMsMode_4x : msaa >= 2 ? DkMsMode_2x : DkMsMode_1x;
}

void bind_target(void) {
    const int fb = s_cur_fb;
    if (s_bound_fb != -2) {
        /* what was rendered may be sampled or copied next */
        dkCmdBufBarrier(s_cmd, DkBarrier_Fragments, DkInvalidateFlags_Image);
    }
    DkImageView cv, dv;
    DkMsMode ms = DkMsMode_1x;
    bool depth = false;
    if (is_window(fb)) {
        ensure_acquired();
        dkImageViewDefaults(&cv, &s_win_img[s_acq < 0 ? 0 : s_acq]);
        dkImageViewDefaults(&dv, &s_win_depth);
        depth = true;
    } else {
        Fb& f = s_fbs[fb];
        if (!f.created) return;
        dkImageViewDefaults(&cv, f.has_ms ? &f.color_ms : &f.color);
        if (f.has_depth && f.has_depth_img) {
            dkImageViewDefaults(&dv, &f.depth);
            depth = true;
        }
        if (f.has_ms) ms = ms_mode(f.msaa);
    }
    const DkImageView* cvs[1] = { &cv };
    dkCmdBufBindRenderTargets(s_cmd, cvs, 1, depth ? &dv : nullptr);
    if (ms != s_bound_ms || s_bound_fb == -2) {
        DkMultisampleState m;
        dkMultisampleStateDefaults(&m);
        m.mode = ms;
        m.rasterizerMode = ms;
        dkCmdBufBindMultisampleState(s_cmd, &m);
        s_bound_ms = ms;
    }
    s_bound_fb = fb;
    s_target_dirty = false;
    s_vp_dirty = s_sc_dirty = true;
}

DkResHandle unit_handle(int u) {
    const Unit& un = s_unit[u];
    if (un.kind == 1 && un.id < s_tex.size() && s_tex[un.id].alive && s_tex[un.id].has_img) {
        const Tex& t = s_tex[un.id];
        return dkMakeTextureHandle(t.slot, sampler_slot(t.smp));
    }
    if (un.kind == 2 && (int)un.id < (int)s_fbs.size() && s_fbs[un.id].created && s_fbs[un.id].color_slot >= 0) {
        const Fb& f = s_fbs[un.id];
        return dkMakeTextureHandle(f.color_slot, sampler_slot(f.smp));
    }
    return dkMakeTextureHandle(s_dummy_slot, 0);
}

void prepare_draw(void) {
    if (s_target_dirty) bind_target();
    if (s_tex_dirty) {
        DkResHandle h[2] = { unit_handle(0), unit_handle(1) };
        dkCmdBufBindTextures(s_cmd, DkStage_Fragment, 0, h, 2);
        s_tex_dirty = false;
        /* a rendered image is stored top-down, GL's bottom-up: the shader mirrors t */
        const int32_t f0 = s_unit[0].kind == 2, f1 = s_unit[1].kind == 2;
        if (f0 != s_uni.flip_v0 || f1 != s_uni.flip_v1) {
            s_uni.flip_v0 = f0;
            s_uni.flip_v1 = f1;
            dkCmdBufPushConstants(s_cmd, s_ubo.gpu, 256, 0, sizeof(s_uni), &s_uni);
            s_st_pushes++;
        }
    }
    if (s_barrier_pending || s_desc_dirty) {
        dkCmdBufBarrier(s_cmd, s_barrier_pending ? DkBarrier_Primitives : DkBarrier_None,
                        DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
        s_barrier_pending = s_desc_dirty = false;
    }
    if (s_vp_dirty) apply_viewport();
    if (s_sc_dirty) apply_scissor();
    s_cmd_pending = true;
}

void bind_program(ShaderProgram* p) {
    const DkShader* sh[2] = { &p->vert, &p->frag };
    dkCmdBufBindShaders(s_cmd, DkStageFlag_GraphicsMask, sh, 2);
    dkCmdBufBindVtxAttribState(s_cmd, p->attribs, DK_ATTR_COUNT);
    DkVtxBufferState vb = { p->stride, 0 };
    dkCmdBufBindVtxBufferState(s_cmd, &vb, 1);
    dkCmdBufPushConstants(s_cmd, s_ubo.gpu, 256, 0, sizeof(s_uni), &s_uni);
    if (p->uber) {
        dkCmdBufPushConstants(s_cmd, s_ubo.gpu + 256, 256, 0, sizeof(p->params), &p->params);
    }
    s_st_pushes++;
}

/* Re-issue every GL-held state after a pass that changed the deko3d state (FXAA, sky, blits). */
void restore_state(void) {
    bind_rasterizer();
    bind_depth();
    bind_blend();
    if (s_prg) bind_program(s_prg);
    s_target_dirty = true;
    s_tex_dirty = true;
    s_vp_dirty = s_sc_dirty = true;
}

/* ---------------- frame model ---------------- */

uint32_t s_q_frame = 0;
struct PerfFrame { bool pending; uint64_t serial; };
PerfFrame s_pf[8];
double s_gpu_ms = 0;
int s_gpu_n = 0, s_gpu_frames = 0;
int s_perf = -1;
bool perf_on(void) {
    if (s_perf < 0) s_perf = getenv("GE_PERFSTAT") != NULL;
    return s_perf != 0;
}

/* per frame (8 in a ring, 48 bytes each): timestamps at frame start, right
 * after the swapchain acquire (the queue waits there for a free image), frame end */
double s_gpu_acq_ms = 0;
int s_gpu_acq_n = 0;
bool s_acq_reported = false;
void perf_collect(void) {
    for (int i = 0; i < 8; i++) {
        if (s_pf[i].pending && s_pf[i].serial <= s_completed) {
            const uint64_t* q = (const uint64_t*)(s_query.cpu + i * 48);
            const uint64_t t0 = q[1], ta = q[3], t1 = q[5];
            if (t1 > t0) {
                s_gpu_ms += (double)dkTimestampToNs(t1 - t0) / 1.0e6;
                s_gpu_n++;
            }
            if (ta > t0 && t1 > ta) {
                s_gpu_acq_ms += (double)dkTimestampToNs(t1 - ta) / 1.0e6;
                s_gpu_acq_n++;
            }
            s_pf[i].pending = false;
        }
    }
}

void acquired_hook(void) {
    if (perf_on() && !s_acq_reported) {
        dkCmdBufReportCounter(s_cmd, DkCounter_Timestamp, s_query.gpu + (s_q_frame % 8) * 48 + 16);
        s_acq_reported = true;
    }
}

void check_dock(void) {
    const bool hh = nxIsHandheld() != 0;
    if (hh == s_handheld) return;
    if (s_cmd_pending) submit();
    dkQueueWaitIdle(s_queue);
    retire();
    destroy_window();
    create_window();
}

/* ---------------- GfxRenderingAPI ---------------- */

const char* dk_get_name(void) { return "deko3d"; }
int dk_get_max_texture_size(void) { return 16384; }
struct GfxClipParameters dk_get_clip_parameters(void) {
    return { false, s_cur_fb < (int)s_fbs.size() ? s_fbs[s_cur_fb].invert_y : false };
}

void dk_unload_shader(struct ShaderProgram*) {}

void dk_load_shader_with(struct ShaderProgram* p, uint32_t fc, float noise, bool tpf0, bool tpf1) {
    if (p == nullptr) return;
    s_prg = p;
    s_uni.frame_count = (int32_t)fc;
    s_uni.noise_scale = noise;
    /* GL compiles the three-point code only while that mode is on */
    s_uni.three_point0 = (tpf0 && s_filter == FILTER_THREE_POINT) ? 1 : 0;
    s_uni.three_point1 = (tpf1 && s_filter == FILTER_THREE_POINT) ? 1 : 0;
    bind_program(p);
    s_cmd_pending = true;
}

void dk_load_shader(struct ShaderProgram* p) {
    dk_load_shader_with(p, s_frame_count, s_noise_scale, s_linear_flag[0], s_linear_flag[1]);
}

struct ShaderProgram* dk_create_and_load_new_shader(uint64_t id0, uint32_t id1) {
    struct CCFeatures f;
    memset(&f, 0, sizeof f);
    gfx_cc_get_features(id0, id1, &f);
    ShaderProgram* p = new ShaderProgram();
    dk_layout_from_features(&f, &p->layout);
    dk_combiner_params(&f, &p->params);
    p->num_inputs = (uint8_t)f.num_inputs;
    p->used_textures[0] = f.used_textures[0];
    p->used_textures[1] = f.used_textures[1];
    p->stride = p->layout.num_floats * 4;
    for (int i = 0; i < DK_ATTR_COUNT; i++) {
        memset(&p->attribs[i], 0, sizeof(p->attribs[i]));
        p->attribs[i].isFixed = 1;
        p->attribs[i].size = DkVtxAttribSize_1x32;
        p->attribs[i].type = DkVtxAttribType_Float;
    }
    static const DkVtxAttribSize sizes[5] = { DkVtxAttribSize_1x32, DkVtxAttribSize_1x32, DkVtxAttribSize_2x32,
                                              DkVtxAttribSize_3x32, DkVtxAttribSize_4x32 };
    for (int i = 0; i < p->layout.count; i++) {
        DkVtxAttribState& a = p->attribs[p->layout.loc[i]];
        a.bufferId = 0;
        a.isFixed = 0;
        a.offset = p->layout.offset[i] * 4;
        a.size = sizes[p->layout.size[i]];
        a.type = DkVtxAttribType_Float;
    }
    const int e = pack_find(id0, id1);
    if (e >= 0 && pack_shader(e, 0, &p->vert) && pack_shader(e, 1, &p->frag)) {
        p->uber = false;
    } else {
        p->uber = true;
        p->vert = s_uber_vert;
        p->frag = s_uber_frag;
        if (!s_missed[std::make_pair(id0, id1)]) {
            s_missed[std::make_pair(id0, id1)] = true;
            sysLogPrintf(LOG_NOTE, "DKSH miss %016llx %08x", (unsigned long long)id0, (unsigned)id1);
        }
    }
    s_progs[std::make_pair(id0, id1)] = p;
    dk_load_shader(p);
    return p;
}

struct ShaderProgram* dk_lookup_shader(uint64_t id0, uint32_t id1) {
    auto it = s_progs.find(std::make_pair(id0, id1));
    return it == s_progs.end() ? nullptr : it->second;
}

void dk_shader_get_info(struct ShaderProgram* p, uint8_t* num_inputs, bool used_textures[2]) {
    *num_inputs = p->num_inputs;
    used_textures[0] = p->used_textures[0];
    used_textures[1] = p->used_textures[1];
}

void dk_clear_shaders(void) {
    for (auto& kv : s_progs) delete kv.second;   /* DkShader only points into the persistent code block */
    s_progs.clear();
    s_prg = nullptr;
}

uint32_t dk_new_texture(void) {
    uint32_t id;
    if (!s_free_ids.empty()) {
        id = s_free_ids.back();
        s_free_ids.pop_back();
    } else {
        id = (uint32_t)s_tex.size();
        s_tex.emplace_back();
    }
    Tex& t = s_tex[id];
    t = Tex();
    t.alive = true;
    t.smp = default_tex_sampler();
    return id;
}

void dk_delete_texture(uint32_t id) {
    if (id == 0 || id >= s_tex.size() || !s_tex[id].alive) return;
    Tex& t = s_tex[id];
    if (t.has_img) free_image(t.blk, t.slot);
    t = Tex();
    s_free_ids.push_back(id);
    for (int u = 0; u < 2; u++) {
        if (s_unit[u].kind == 1 && s_unit[u].id == id) s_tex_dirty = true;
    }
}

void dk_select_texture(int tile, uint32_t id, bool linear) {
    s_unit[tile].kind = 1;
    s_unit[tile].id = id;
    s_linear_flag[tile] = linear;
    s_active_unit = tile;
    s_tex_dirty = true;
}

void dk_select_texture_fb(int fb) {
    s_unit[0].kind = 2;
    s_unit[0].id = (uint32_t)fb;
    s_linear_flag[0] = true;
    s_active_unit = 0;
    s_tex_dirty = true;
}

/* the 2x2 box mip chain of gfx_opengl_upload_texture (Switch path) */
void build_mips(const uint8_t* src0, uint32_t width, uint32_t height, std::vector<uint8_t>& out,
                std::vector<uint32_t>& offs, uint32_t* levels) {
    out.clear();
    offs.clear();
    out.insert(out.end(), src0, src0 + (size_t)width * height * 4);
    offs.push_back(0);
    uint32_t w = width, h = height, n = 1;
    while (w > 1 || h > 1) {
        const uint32_t nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        const size_t so = offs.back(), dofs = out.size();
        out.resize(dofs + (size_t)nw * nh * 4);
        const uint8_t* src = out.data() + so;
        uint8_t* dst = out.data() + dofs;
        for (uint32_t y = 0; y < nh; y++) {
            const uint32_t y0 = y * 2 < h ? y * 2 : h - 1, y1 = y * 2 + 1 < h ? y * 2 + 1 : y0;
            for (uint32_t x = 0; x < nw; x++) {
                const uint32_t x0 = x * 2 < w ? x * 2 : w - 1, x1 = x * 2 + 1 < w ? x * 2 + 1 : x0;
                const uint8_t* a = src + ((size_t)y0 * w + x0) * 4;
                const uint8_t* b = src + ((size_t)y0 * w + x1) * 4;
                const uint8_t* c = src + ((size_t)y1 * w + x0) * 4;
                const uint8_t* d = src + ((size_t)y1 * w + x1) * 4;
                uint8_t* o = dst + ((size_t)y * nw + x) * 4;
                for (int k = 0; k < 4; k++) o[k] = (uint8_t)((a[k] + b[k] + c[k] + d[k] + 2) >> 2);
            }
        }
        offs.push_back((uint32_t)dofs);
        w = nw;
        h = nh;
        n++;
    }
    *levels = n;
}

/* Upload width x height RGBA8 (+ levels) into a fresh image; returns false on failure. */
bool upload_image(DkImage* img, DkHeapBlock* blk, DkImageFormat fmt, uint32_t bpp, const uint8_t* data,
                  const std::vector<uint32_t>& offs, uint32_t width, uint32_t height, uint32_t levels, size_t total) {
    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, s_dev);
    lm.format = fmt;
    lm.dimensions[0] = width;
    lm.dimensions[1] = height;
    lm.mipLevels = levels;
    if (levels > 1) {
        /* small mipmapped images: one-GOB tiles, or the default tile height of
         * a 24-64 px level can make the mip copies land over the base level
         * (seen on hardware by MKDD-NX; Ryujinx does not show it) */
        lm.flags |= DkImageFlags_CustomTileSize;
        lm.tileSize = DkTileSize_OneGob;
    }
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &lm);
    if (!alloc_image(&layout, img, blk)) return false;
    DkGpuAddr gpu;
    uint8_t* stage = ring_alloc(s_dataRing, s_dataBlocks, (uint32_t)total, 256, &gpu);
    memcpy(stage, data, total);
    uint32_t w = width, h = height;
    for (uint32_t l = 0; l < levels; l++) {
        DkImageView v;
        dkImageViewDefaults(&v, img);
        v.mipLevelOffset = (uint8_t)l;
        v.mipLevelCount = 1;
        DkCopyBuf src = { gpu + offs[l], 0, 0 };
        DkImageRect r = { 0, 0, 0, w, h, 1 };
        dkCmdBufCopyBufferToImage(s_cmd, &src, &v, &r, 0);
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    (void)bpp;
    s_barrier_pending = true;
    s_cmd_pending = true;
    return true;
}

void dk_upload_texture(const uint8_t* rgba, uint32_t width, uint32_t height, bool gen_mipmaps) {
    const Unit& un = s_unit[s_active_unit];
    if (un.kind != 1 || un.id == 0 || un.id >= s_tex.size() || !s_tex[un.id].alive || width == 0 || height == 0) {
        return;
    }
    Tex& t = s_tex[un.id];
    static std::vector<uint8_t> s_buf;
    static std::vector<uint32_t> s_offs;
    uint32_t levels = 1;
    const uint8_t* data = rgba;
    size_t total = (size_t)width * height * 4;
    s_offs.assign(1, 0);
    if (gen_mipmaps || s_filter == FILTER_THREE_POINT || s_filter == FILTER_TRILINEAR) {
        build_mips(rgba, width, height, s_buf, s_offs, &levels);
        data = s_buf.data();
        total = s_buf.size();
    }
    /* always a new image: earlier draws of this frame may still sample the old one */
    if (t.has_img) free_image(t.blk, t.slot);
    t.has_img = false;
    if (!upload_image(&t.img, &t.blk, DkImageFormat_RGBA8_Unorm, 4, data, s_offs, width, height, levels, total)) {
        sysLogPrintf(LOG_ERROR, "DK: texture %ux%u: out of image memory", width, height);
        return;
    }
    t.slot = write_img_desc(&t.img, false);
    t.has_img = true;
    t.w = width;
    t.h = height;
    t.levels = levels;
    s_tex_dirty = true;
    s_st_uploads++;
}

DkWrapMode cm_to_dk(uint32_t val) {
    switch (val) {
        case G_TX_NOMIRROR | G_TX_CLAMP: return DkWrapMode_ClampToEdge;
        case G_TX_MIRROR | G_TX_WRAP: return DkWrapMode_MirroredRepeat;
        case G_TX_MIRROR | G_TX_CLAMP: return DkWrapMode_MirrorClampToEdge;
    }
    return DkWrapMode_Repeat;
}

/* gfx_opengl_sampler_key */
SmpKey sampler_key(bool linear_filter, uint32_t cms, uint32_t cmt, bool mipmaps) {
    static const uint8_t mins[4][3][2] = {
        { { DkFilter_Nearest, DkMipFilter_None }, { DkFilter_Nearest, DkMipFilter_Nearest }, { DkFilter_Nearest, DkMipFilter_Linear } },
        { { DkFilter_Linear, DkMipFilter_None }, { DkFilter_Linear, DkMipFilter_Nearest }, { DkFilter_Linear, DkMipFilter_Linear } },
        { { DkFilter_Nearest, DkMipFilter_None }, { DkFilter_Linear, DkMipFilter_Nearest }, { DkFilter_Linear, DkMipFilter_Linear } },
        { { DkFilter_Linear, DkMipFilter_None }, { DkFilter_Linear, DkMipFilter_Linear }, { DkFilter_Linear, DkMipFilter_Linear } },
    };
    if (s_filter == FILTER_THREE_POINT || s_filter == FILTER_TRILINEAR) mipmaps = true;
    mipmaps = mipmaps && s_mipfilter != MIPMAP_DISABLED;
    const int mip_idx = mipmaps ? (int)s_mipfilter : 0;
    SmpKey k;
    if (linear_filter) {
        k.min = mins[s_filter][mip_idx][0];
        k.mip = mins[s_filter][mip_idx][1];
    } else {
        k.min = DkFilter_Nearest;
        k.mip = DkMipFilter_None;
    }
    k.mag = (linear_filter && (s_filter == FILTER_LINEAR || s_filter == FILTER_TRILINEAR)) ? DkFilter_Linear : DkFilter_Nearest;
    k.aniso = (uint8_t)(mipmaps ? s_aniso : 1);
    k.ws = cm_to_dk(cms);
    k.wt = cm_to_dk(cmt);
    return k;
}

void dk_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt, bool mipmaps) {
    s_active_unit = tile;
    const SmpKey k = sampler_key(linear_filter, cms, cmt, mipmaps);
    const Unit& un = s_unit[tile];
    if (un.kind == 1 && un.id < s_tex.size() && s_tex[un.id].alive) {
        s_tex[un.id].smp = k;
    } else if (un.kind == 2 && (int)un.id < (int)s_fbs.size()) {
        s_fbs[un.id].smp = k;
    }
    s_tex_dirty = true;
}

void dk_set_depth_mode(bool depth_test, bool depth_update, bool depth_compare, bool depth_source_prim, uint16_t zmode) {
    if (depth_test) {
        s_depth_test = true;
        s_depth_mask = depth_update;
        if (depth_compare) {
            switch (zmode) {
                case ZMODE_INTER: s_depth_func = DkCompareOp_Lequal; s_decal = false; break;
                case ZMODE_OPA:
                case ZMODE_XLU: s_depth_func = depth_source_prim ? DkCompareOp_Lequal : DkCompareOp_Less; s_decal = false; break;
                case ZMODE_DEC: s_depth_func = DkCompareOp_Lequal; s_decal = true; break;
            }
        } else {
            s_depth_func = DkCompareOp_Always;
            s_decal = false;
        }
    } else {
        s_depth_test = false;
    }
    bind_depth();
    bind_rasterizer();
    s_cmd_pending = true;
}

void dk_set_depth_range(float znear, float zfar) {
    s_znear = znear;
    s_zfar = zfar;
    s_vp_dirty = true;
}

void dk_set_viewport(int x, int y, int w, int h) {
    s_vp[0] = x; s_vp[1] = y; s_vp[2] = w; s_vp[3] = h;
    s_vp_dirty = true;
}

void dk_set_scissor(int x, int y, int w, int h) {
    s_sc[0] = x; s_sc[1] = y; s_sc[2] = w; s_sc[3] = h;
    s_sc_dirty = true;
}

void dk_set_use_alpha(bool use_alpha, bool modulate) {
    s_blend = use_alpha;
    s_modulate = modulate;
    bind_blend();
    s_cmd_pending = true;
}

float* dk_stream_begin(size_t max_floats) {
    if (!s_up) return nullptr;
    s_stream_ptr = (float*)ring_alloc(s_dataRing, s_dataBlocks, (uint32_t)(max_floats * 4), 64, &s_stream_gpu);
    return s_stream_ptr;
}

void dk_draw_triangles(float buf[], size_t len, size_t ntris) {
    if (!s_up || s_prg == nullptr || ntris == 0) return;
    prepare_draw();
    DkGpuAddr addr;
    const uint32_t bytes = (uint32_t)(len * 4);
    if (buf != nullptr && buf == s_stream_ptr) {
        addr = s_stream_gpu;
    } else {
        uint8_t* dst = ring_alloc(s_dataRing, s_dataBlocks, bytes, 64, &addr);
        memcpy(dst, buf, bytes);
    }
    s_stream_ptr = nullptr;
    dkCmdBufBindVtxBuffer(s_cmd, 0, addr, bytes);
    dkCmdBufDraw(s_cmd, DkPrimitive_Triangles, (uint32_t)(3 * ntris), 1, 0, 0);
    s_st_draws++;
    if (s_submit_every && ++s_draws_since_submit >= s_submit_every) {
        submit();   /* the GPU starts on this part while the rest is recorded */
    }
    if (s_prg->uber) s_st_uber_draws++;
}

void dk_init(void) {
    {
        const char *sp = getenv("GE_SHADEPERSP"), *fp = getenv("GE_FOGPIXEL");
        if ((sp && sp[0] == '1') || (fp && fp[0] == '1')) {
            sysLogPrintf(LOG_WARNING, "DK: GE_SHADEPERSP/GE_FOGPIXEL ignored under deko3d");
        }
        if (getenv("GE_FOGSNAP")) sysLogPrintf(LOG_WARNING, "DK: GE_FOGSNAP ignored under deko3d");
    }
    {
        const char* e = getenv("GE_DKSUBMIT");
        s_submit_every = e ? (uint32_t)atoi(e) : 64;
        sysLogPrintf(LOG_NOTE, "DK: submit every %u draws (GE_DKSUBMIT; 0 = once per frame)", s_submit_every);
    }
    gfx_rdp_affine = 1;   /* the pack is built with noperspective shade and per-vertex fog */
    gfx_fog_vertex = 1;

    DkDeviceMaker dm;
    dkDeviceMakerDefaults(&dm);
    dm.cbDebug = dk_debug;
    dm.flags = DkDeviceFlags_OriginUpperLeft | DkDeviceFlags_DepthMinusOneToOne;
    s_dev = dkDeviceCreate(&dm);
    if (s_dev == nullptr) sysFatalError("deko3d: could not create the device");

    DkQueueMaker qm;
    dkQueueMakerDefaults(&qm, s_dev);
    qm.flags = DkQueueFlags_Graphics | DkQueueFlags_HighPrio;
    qm.commandMemorySize = 4 * DK_QUEUE_MIN_CMDMEM_SIZE;
    qm.flushThreshold = DK_QUEUE_MIN_CMDMEM_SIZE / 2;
    s_queue = dkQueueCreate(&qm);
    if (s_queue == nullptr) sysFatalError("deko3d: could not create the queue");

    DkCmdBufMaker cm;
    dkCmdBufMakerDefaults(&cm, s_dev);
    cm.cbAddMem = cmd_add_mem;
    s_cmd = dkCmdBufCreate(&cm);
    s_up = true;
    give_cmd_memory();

    s_desc = make_block(NUM_IMG_DESC * sizeof(DkImageDescriptor) + NUM_SMP_DESC * sizeof(DkSamplerDescriptor),
                        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    s_ubo = make_block(4096, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    s_query = make_block(4096, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    memset(s_desc.cpu, 0, s_desc.size);
    memset(s_ubo.cpu, 0, s_ubo.size);

    load_pack();

    /* sampler slot 0: nearest/repeat; image slot 0: a 1x1 opaque black texture (GL's unbound texture) */
    sampler_slot(SmpKey{ DkFilter_Nearest, DkFilter_Nearest, DkMipFilter_None, DkWrapMode_Repeat, DkWrapMode_Repeat, 1 });
    {
        static const uint8_t black[4] = { 0, 0, 0, 255 };
        std::vector<uint32_t> offs(1, 0);
        DkHeapBlock b;
        upload_image(&s_dummy_img, &b, DkImageFormat_RGBA8_Unorm, 4, black, offs, 1, 1, 1, 4);
        s_dummy_slot = write_img_desc(&s_dummy_img, false);
    }

    dkCmdBufBindImageDescriptorSet(s_cmd, s_desc.gpu, NUM_IMG_DESC);
    dkCmdBufBindSamplerDescriptorSet(s_cmd, s_desc.gpu + NUM_IMG_DESC * sizeof(DkImageDescriptor), NUM_SMP_DESC);
    dkCmdBufBindUniformBuffer(s_cmd, DkStage_Fragment, 0, s_ubo.gpu, 256);
    dkCmdBufBindUniformBuffer(s_cmd, DkStage_Fragment, 1, s_ubo.gpu + 256, 256);
    DkColorWriteState cw;
    dkColorWriteStateDefaults(&cw);
    dkCmdBufBindColorWriteState(s_cmd, &cw);
    dkCmdBufSetDepthBias(s_cmd, -4.0f, 0.0f, -2.0f);   /* glPolygonOffset(-2, -2); nvc0 sends units x 2 */
    s_depth_func = DkCompareOp_Lequal;   /* glDepthFunc(GL_LEQUAL) at init */
    bind_rasterizer();
    bind_depth();
    bind_blend();

    create_window();
    s_fbs.resize(1);   /* fb 0 = the window */
    submit();
    sysLogPrintf(LOG_NOTE, "DK: deko3d backend ready (device origin upper-left, depth -1..1, GL memory layout on every target)");
}

void dk_on_resize(void) {}

void dk_start_frame(void) {
    s_frame_count++;
    check_dock();
    /* at most FRAMES_IN_FLIGHT frames queued on the GPU */
    wait_serial(s_frame_serial[(s_frame_idx + 1) % (FRAMES_IN_FLIGHT + 1)]);
    if (perf_on()) {
        perf_collect();
        const DkGpuAddr q = s_query.gpu + (s_q_frame % 8) * 48;
        memset(s_query.cpu + (s_q_frame % 8) * 48, 0, 48);
        dkCmdBufReportCounter(s_cmd, DkCounter_Timestamp, q);
        s_acq_reported = false;
    }
}

void dk_end_frame(void) {
    if (perf_on()) {
        const uint32_t i = s_q_frame % 8;
        dkCmdBufReportCounter(s_cmd, DkCounter_Timestamp, s_query.gpu + i * 48 + 32);
        s_pf[i].pending = true;
        s_pf[i].serial = s_serial;
        s_q_frame++;
        if (++s_gpu_frames % 300 == 0 && s_gpu_n > 0) {
            fprintf(stderr, "PERFSTAT gpu=%.2fms/frame (avg over %d frames)\n", s_gpu_ms / s_gpu_n, s_gpu_n);
            if (s_gpu_acq_n > 0) {
                fprintf(stderr, "PERFSTAT dk gpu after acquire=%.2fms/frame (start..end minus the queue's wait for a swapchain image, %d frames)\n",
                        s_gpu_acq_ms / s_gpu_acq_n, s_gpu_acq_n);
            }
            s_gpu_ms = 0;
            s_gpu_n = 0;
            s_gpu_acq_ms = 0;
            s_gpu_acq_n = 0;
        }
    }
    s_cmd_pending = true;
}

void dk_finish_render(void) {}

void destroy_fb_images(Fb& f) {
    if (!f.created) return;
    s_imgHeap.free_after(f.blk_color, s_serial);
    if (f.color_slot >= 0) s_imgSlots.release_after(f.color_slot, s_serial);
    f.color_slot = -1;
    if (f.has_ms) s_imgHeap.free_after(f.blk_ms, s_serial);
    if (f.has_depth_img) s_imgHeap.free_after(f.blk_depth, s_serial);
    f.has_ms = f.has_depth_img = false;
    f.created = false;
}

bool make_rt(DkImage* img, DkHeapBlock* blk, DkImageFormat fmt, uint32_t w, uint32_t h, DkMsMode ms, uint32_t flags) {
    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, s_dev);
    lm.flags = flags;
    lm.format = fmt;
    lm.msMode = ms;
    lm.dimensions[0] = w;
    lm.dimensions[1] = h;
    if (ms != DkMsMode_1x) lm.type = DkImageType_2DMS;
    DkImageLayout l;
    dkImageLayoutInitialize(&l, &lm);
    return alloc_image(&l, img, blk);
}

void build_fb_images(Fb& f) {
    const DkMsMode ms = ms_mode(f.msaa);
    bool ok = make_rt(&f.color, &f.blk_color, DkImageFormat_RGBA8_Unorm, f.w, f.h, DkMsMode_1x,
                      DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine);
    if (ok && ms != DkMsMode_1x) {
        ok = make_rt(&f.color_ms, &f.blk_ms, DkImageFormat_RGBA8_Unorm, f.w, f.h, ms,
                     DkImageFlags_UsageRender | DkImageFlags_HwCompression);
        f.has_ms = ok;
    }
    if (ok && f.has_depth) {
        ok = make_rt(&f.depth, &f.blk_depth, DkImageFormat_Z24S8, f.w, f.h, ms, DkImageFlags_UsageRender | DkImageFlags_HwCompression);
        f.has_depth_img = ok;
    }
    if (!ok) {
        sysLogPrintf(LOG_ERROR, "DK: framebuffer %ux%u x%u: out of image memory", f.w, f.h, f.msaa);
        return;
    }
    f.color_slot = write_img_desc(&f.color, true);
    f.created = true;
}

int dk_create_framebuffer(void) {
    const int i = (int)s_fbs.size();
    s_fbs.resize(i + 1);
    Fb& f = s_fbs[i];
    f.smp = SmpKey{ DkFilter_Linear, DkFilter_Linear, DkMipFilter_None, DkWrapMode_Repeat, DkWrapMode_Repeat, 1 };
    build_fb_images(f);   /* 1x1 until sized, as GL */
    return i;
}

void dk_update_framebuffer_parameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level, bool invert_y,
                                      bool render_target, bool has_depth_buffer, bool can_extract_depth) {
    (void)render_target;
    (void)can_extract_depth;
    if (fb_id < 0 || fb_id >= (int)s_fbs.size()) return;
    Fb& f = s_fbs[fb_id];
    width = width < 1 ? 1 : width;
    height = height < 1 ? 1 : height;
    if (msaa_level > 8) msaa_level = 8;   /* the GPU's maximum */
    if (!gfx_framebuffers_enabled) has_depth_buffer = false;
    if (fb_id != 0 && gfx_framebuffers_enabled) {
        const bool depth_missing = has_depth_buffer && !f.has_depth_img;
        if (f.w != width || f.h != height || f.msaa != msaa_level || depth_missing || !f.created) {
            destroy_fb_images(f);
            f.w = width;
            f.h = height;
            f.msaa = msaa_level;
            f.has_depth = has_depth_buffer;
            build_fb_images(f);
            if (s_cur_fb == fb_id) s_target_dirty = true;
            s_tex_dirty = true;
        }
    }
    f.w = width;
    f.h = height;
    f.msaa = msaa_level;
    f.has_depth = has_depth_buffer;
    f.invert_y = invert_y;
}

bool dk_start_draw_to_framebuffer(int fb_id, float noise_scale) {
    if (gfx_framebuffers_enabled && fb_id < (int)s_fbs.size()) {
        if (noise_scale != 0.0f) s_noise_scale = 1.0f / noise_scale;
        if (fb_id != s_cur_fb || s_bound_fb != fb_id) s_target_dirty = true;
        s_cur_fb = fb_id;
        return true;
    }
    return false;
}

void do_clear(bool color, bool depth) {
    if (color) dkCmdBufClearColorFloat(s_cmd, 0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 1.0f);
    const bool has_depth = is_window(s_cur_fb) || (s_fbs[s_cur_fb].has_depth && s_fbs[s_cur_fb].has_depth_img);
    if (depth && has_depth) dkCmdBufClearDepthStencil(s_cmd, true, 1.0f, 0, 0);
    s_cmd_pending = true;
}

void dk_clear_framebuffer(bool color, bool depth) {
    if (!s_up) return;
    if (s_target_dirty) bind_target();
    if (!is_window(s_cur_fb) && !s_fbs[s_cur_fb].created) return;
    /* GL disables the scissor for the clear; deko3d clears respect it */
    set_scissor_rect(0, 0, (int)fb_width(s_cur_fb), (int)fb_height(s_cur_fb));
    do_clear(color, depth);
    s_sc_dirty = true;   /* GL re-enables its scissor box */
}

void dk_clear_region(bool color, bool depth, int x, int y, int w, int h) {
    if (!s_up || w <= 0 || h <= 0) return;
    if (s_target_dirty) bind_target();
    if (!is_window(s_cur_fb) && !s_fbs[s_cur_fb].created) return;
    s_sc[0] = x; s_sc[1] = y; s_sc[2] = w; s_sc[3] = h;   /* GL keeps this scissor box afterwards (D48) */
    apply_scissor();
    do_clear(color, depth);
}

/* The single-sample image holding fb's picture (resolving its MSAA image first). */
const DkImage* readable_image(int fb) {
    if (is_window(fb)) {
        ensure_acquired();
        return &s_win_img[s_acq < 0 ? 0 : s_acq];
    }
    Fb& f = s_fbs[fb];
    if (!f.created) return nullptr;
    if (f.has_ms) {
        DkImageView s, d;
        dkImageViewDefaults(&s, &f.color_ms);
        dkImageViewDefaults(&d, &f.color);
        dkCmdBufResolveImage(s_cmd, &s, &d);
    }
    return &f.color;
}

const DkImage* writable_image(int fb) {
    if (is_window(fb)) {
        ensure_acquired();
        return &s_win_img[s_acq < 0 ? 0 : s_acq];
    }
    Fb& f = s_fbs[fb];
    return f.created ? &f.color : nullptr;
}

/* blit a GL-coordinate rect between two framebuffers (fb 0 is stored top-down) */
void blit_gl(int fb_src, int sx0, int sy0, int sx1, int sy1, int fb_dst, int dx0, int dy0, int dx1, int dy1, bool linear) {
    dkCmdBufBarrier(s_cmd, DkBarrier_Fragments, DkInvalidateFlags_Image);
    const DkImage* si = readable_image(fb_src);
    const DkImage* di = writable_image(fb_dst);
    if (si == nullptr || di == nullptr) return;
    bool flip = false;
    if (sy1 < sy0) { std::swap(sy0, sy1); flip = !flip; }
    if (dy1 < dy0) { std::swap(dy0, dy1); flip = !flip; }
    {   /* GL bottom-up rows -> top-down memory rows, both sides (relative orientation kept) */
        const int hs = (int)fb_height(fb_src), hd = (int)fb_height(fb_dst);
        int a = hs - sy1, b = hs - sy0;
        sy0 = a; sy1 = b;
        a = hd - dy1; b = hd - dy0;
        dy0 = a; dy1 = b;
    }
    const int sw = (int)fb_width(fb_src), sh = (int)fb_height(fb_src);
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx1 > sw) sx1 = sw;
    if (sy1 > sh) sy1 = sh;
    if (sx1 <= sx0 || sy1 <= sy0 || dx1 <= dx0 || dy1 <= dy0) return;
    DkImageView sv, dv;
    dkImageViewDefaults(&sv, si);
    dkImageViewDefaults(&dv, di);
    DkImageRect sr = { (uint32_t)sx0, (uint32_t)sy0, 0, (uint32_t)(sx1 - sx0), (uint32_t)(sy1 - sy0), 1 };
    DkImageRect dr = { (uint32_t)dx0, (uint32_t)dy0, 0, (uint32_t)(dx1 - dx0), (uint32_t)(dy1 - dy0), 1 };
    dkCmdBufBlitImage(s_cmd, &sv, &sr, &dv, &dr, (flip ? DkBlitFlag_FlipY : 0) | (linear ? DkBlitFlag_FilterLinear : DkBlitFlag_FilterNearest), 0);
    s_barrier_pending = true;
    s_cmd_pending = true;
}

void dk_copy_framebuffer(int fb_dst, int fb_src, int left, int top, bool flip_y, bool use_back) {
    (void)use_back;   /* deko3d reads the image being drawn (GL: GL_BACK) */
    if (!gfx_framebuffers_enabled || fb_dst >= (int)s_fbs.size() || fb_src >= (int)s_fbs.size()) return;
    const int dw = (int)fb_width(fb_dst), dh = (int)fb_height(fb_dst);
    int sx0, sy0, sx1, sy1;
    if (left >= 0 && top >= 0) {
        sx0 = left; sy0 = top; sx1 = left + dw; sy1 = top + dh;
    } else {
        sx0 = 0; sy0 = 0; sx1 = (int)fb_width(fb_src); sy1 = (int)fb_height(fb_src);
    }
    int dy0 = 0, dy1 = dh;
    if (flip_y) std::swap(dy0, dy1);
    blit_gl(fb_src, sx0, sy0, sx1, sy1, fb_dst, 0, dy0, dw, dy1, false);
    restore_state();
}

void dk_resolve_msaa_color_buffer(int fb_target, int fb_source) {
    if (!gfx_framebuffers_enabled || fb_source <= 0 || fb_source >= (int)s_fbs.size() || fb_target >= (int)s_fbs.size()) return;
    Fb& src = s_fbs[fb_source];
    if (!src.created) return;
    dkCmdBufBarrier(s_cmd, DkBarrier_Fragments, DkInvalidateFlags_Image);
    if (!is_window(fb_target) && src.has_ms && s_fbs[fb_target].created && s_fbs[fb_target].w == src.w &&
        s_fbs[fb_target].h == src.h) {
        DkImageView s, d;
        dkImageViewDefaults(&s, &src.color_ms);
        dkImageViewDefaults(&d, &s_fbs[fb_target].color);
        dkCmdBufResolveImage(s_cmd, &s, &d);
        s_barrier_pending = true;
        s_cmd_pending = true;
    } else {
        blit_gl(fb_source, 0, 0, (int)src.w, (int)src.h, fb_target, 0, 0, (int)fb_width(fb_target), (int)fb_height(fb_target), false);
    }
    restore_state();
}

void* dk_get_framebuffer_texture_id(int fb_id) {
    return (void*)(uintptr_t)(fb_id < (int)s_fbs.size() && s_fbs[fb_id].color_slot >= 0 ? s_fbs[fb_id].color_slot : 0);
}

void dk_set_texture_filter(enum FilteringMode mode) { s_filter = mode; }
enum FilteringMode dk_get_texture_filter(void) { return s_filter; }
void dk_set_mipmap_filter(enum MipmapFilteringMode mode) { s_mipfilter = mode; }
void dk_set_anisotropy_level(int level) { s_aniso = level < 1 ? 1 : level > 16 ? 16 : level; }
int dk_get_max_anisotropy_level(void) { return 16; }

/* ---------------- extra passes ---------------- */

struct SkyPlusDraw {
    PortSkyPlus p;
    int vp[4];
    float aspA, aspB;
};
#define SKYPLUS_RING 64
SkyPlusDraw s_sky_ring[SKYPLUS_RING];
unsigned s_sky_next;
DkImage s_sky_img;
DkHeapBlock s_sky_blk;
int s_sky_slot = -1;
int s_sky_gen = -1;

struct DkSkyUniforms { float v[14][4]; };

void draw_fullscreen(const DkShader* frag, const void* uni, uint32_t uni_size, DkResHandle tex) {
    const DkShader* sh[2] = { &s_fs_vert, frag };
    dkCmdBufBindShaders(s_cmd, DkStageFlag_GraphicsMask, sh, 2);
    dkCmdBufBindVtxAttribState(s_cmd, nullptr, 0);
    dkCmdBufPushConstants(s_cmd, s_ubo.gpu, 256, 0, uni_size, uni);
    dkCmdBufBindTextures(s_cmd, DkStage_Fragment, 0, &tex, 1);
    DkRasterizerState r;
    dkRasterizerStateDefaults(&r);
    r.cullMode = DkFace_None;
    r.depthClampEnable = 1;
    dkCmdBufBindRasterizerState(s_cmd, &r);
    DkDepthStencilState d;
    dkDepthStencilStateDefaults(&d);
    d.depthTestEnable = 0;
    d.depthWriteEnable = 0;
    dkCmdBufBindDepthStencilState(s_cmd, &d);
    DkColorState c;
    dkColorStateDefaults(&c);
    dkCmdBufBindColorState(s_cmd, &c);
    if (s_desc_dirty || s_barrier_pending) {
        dkCmdBufBarrier(s_cmd, s_barrier_pending ? DkBarrier_Primitives : DkBarrier_None,
                        DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
        s_desc_dirty = s_barrier_pending = false;
    }
    dkCmdBufDraw(s_cmd, DkPrimitive_Triangles, 3, 1, 0, 0);
    s_cmd_pending = true;
}

}   // namespace

/* ================= public ================= */

struct GfxRenderingAPI gfx_deko3d_api = {
    dk_get_name,
    dk_get_max_texture_size,
    dk_get_clip_parameters,
    dk_unload_shader,
    dk_load_shader,
    dk_create_and_load_new_shader,
    dk_lookup_shader,
    dk_shader_get_info,
    dk_clear_shaders,
    dk_new_texture,
    dk_select_texture,
    dk_upload_texture,
    dk_set_sampler_parameters,
    dk_set_depth_mode,
    dk_set_depth_range,
    dk_set_viewport,
    dk_set_scissor,
    dk_set_use_alpha,
    dk_draw_triangles,
    dk_init,
    dk_on_resize,
    dk_start_frame,
    dk_end_frame,
    dk_finish_render,
    dk_create_framebuffer,
    dk_update_framebuffer_parameters,
    dk_start_draw_to_framebuffer,
    dk_copy_framebuffer,
    dk_clear_framebuffer,
    dk_clear_region,
    dk_resolve_msaa_color_buffer,
    dk_get_framebuffer_texture_id,
    dk_select_texture_fb,
    dk_delete_texture,
    dk_set_texture_filter,
    dk_get_texture_filter,
    dk_set_mipmap_filter,
    dk_set_anisotropy_level,
    dk_get_max_anisotropy_level,
    dk_stream_begin,
    dk_load_shader_with,
};

extern "C" {

bool gfx_deko3d_wanted(void) {
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("GE_RENDERER");
        on = (e != NULL && strcmp(e, "gl") == 0) ? 0 : 1;   /* default on the Switch */
    }
    return on != 0;
}

const char* gfx_deko3d_renderer_string(void) {
    return "NVIDIA Tegra X1 (deko3d)";
}

void gfx_deko3d_get_dimensions(uint32_t* w, uint32_t* h) {
    if (!s_up) {   /* asked by gfx_sdl_init before the swapchain exists */
        const bool hh = nxIsHandheld() != 0;
        *w = hh ? 1280 : 1920;
        *h = hh ? 720 : 1080;
        return;
    }
    *w = s_dims_w;
    *h = s_dims_h;
}

void gfx_deko3d_set_swap_interval(int interval) {
    s_swap_interval = interval < 0 ? 0 : interval;
    if (s_swapchain) dkSwapchainSetSwapInterval(s_swapchain, (uint32_t)s_swap_interval);
}

void gfx_deko3d_present(void) {
    if (!s_up || s_parked) return;
    if (s_acq >= 0) {
        submit();
        dkQueuePresentImage(s_queue, s_swapchain, s_acq);
        s_acq = -1;
        /* GE_DKLATSYNC=1: return only once the GPU has finished this frame, so
         * INPUTLAT's "swap returned" stamp means "image ready" as with a
         * blocking GL swap (D973 latency A/B); off by default */
        static int s_latsync = -1;
        if (s_latsync < 0) s_latsync = getenv("GE_DKLATSYNC") != NULL;
        if (s_latsync) wait_serial(s_serial - 1);
        s_target_dirty = true;   /* the next frame draws into a newly acquired image */
    } else if (s_cmd_pending) {
        submit();   /* nothing reached fb 0: no present (a present of an untouched image flickers) */
    }
    s_frame_idx = (s_frame_idx + 1) % (FRAMES_IN_FLIGHT + 1);
    s_frame_serial[s_frame_idx] = s_serial - 1;
}

void gfx_deko3d_park(void) {
    if (!s_up || s_parked) return;
    if (s_cmd_pending) submit();
    dkQueueWaitIdle(s_queue);
    s_parked = true;
    destroy_window();   /* before nwindow goes away with the process */
    for (auto& b : s_dataBlocks) free_block(b);
    for (auto& b : s_cmdBlocks) free_block(b);
    for (auto& b : s_imgBlocks) free_block(b);
    free_block(s_desc);
    free_block(s_ubo);
    free_block(s_query);
    free_block(s_code);
    dkCmdBufDestroy(s_cmd);
    dkQueueDestroy(s_queue);
    dkDeviceDestroy(s_dev);
    s_up = false;
    sysLogPrintf(LOG_NOTE, "DK: deko3d backend parked (queue idle, swapchain and device destroyed)");
}

void gfx_deko3d_fxaa_pass(int src_fb_id) {
    if (!s_up || !s_fxaa_ok || !gfx_framebuffers_enabled || src_fb_id <= 0 || src_fb_id >= (int)s_fbs.size()) return;
    Fb& src = s_fbs[src_fb_id];
    if (!src.created) return;
    src.smp.ws = src.smp.wt = DkWrapMode_ClampToEdge;   /* GL sets these on the texture and leaves them */
    const int prev_fb = s_cur_fb;
    s_cur_fb = 0;
    bind_target();
    DkViewport v = { 0.0f, (float)((int)s_win_h - (int)src.h), (float)src.w, (float)src.h, s_znear, s_zfar };
    dkCmdBufSetViewports(s_cmd, 0, &v, 1);
    set_scissor_rect(0, 0, (int)s_win_w, (int)s_win_h);
    float u[4] = { 1.0f / (float)src.w, 1.0f / (float)src.h, 0.0f, 0.0f };
    draw_fullscreen(&s_fxaa_frag, u, sizeof u, dkMakeTextureHandle(src.color_slot, sampler_slot(src.smp)));
    s_cur_fb = prev_fb;
    restore_state();
}

int gfx_deko3d_skyplus_stage(const struct PortSkyPlus* p, const int vp[4], float aspA, float aspB) {
    const int slot = (int)(s_sky_next++ % SKYPLUS_RING);
    SkyPlusDraw& d = s_sky_ring[slot];
    d.p = *p;
    for (int i = 0; i < 4; i++) d.vp[i] = vp[i];
    d.aspA = aspA;
    d.aspB = aspB;
    return slot;
}

void gfx_deko3d_skyplus_pass(int slot) {
    if (!s_up || slot < 0 || slot >= SKYPLUS_RING) return;
    if (!s_sky_ok) {
        gfx_skyplus_failed = 1;   /* skyplus.c draws GE's sky from the next frame on */
        return;
    }
    const SkyPlusDraw& d = s_sky_ring[slot];
    const PortSkyPlus& p = d.p;
    if (d.vp[2] <= 0 || d.vp[3] <= 0 || (d.aspA < 0.0f ? -d.aspA : d.aspA) < 1e-6f || !p.tex) return;
    if (p.texGen != s_sky_gen || s_sky_slot < 0) {
        /* R8 with a CPU mip chain (GL: glGenerateMipmap; the plane is seen at grazing angles) */
        /* RGBA8 with the intensity in every channel (GL: R8 read as .r): the
         * same format and copy path as every game texture */
        std::vector<uint8_t> buf((size_t)SKYPLUS_TEX_W * SKYPLUS_TEX_H * 4);
        for (int i = 0; i < SKYPLUS_TEX_W * SKYPLUS_TEX_H; i++) {
            buf[i * 4 + 0] = buf[i * 4 + 1] = buf[i * 4 + 2] = buf[i * 4 + 3] = p.tex[i];
        }
        std::vector<uint32_t> offs(1, 0);
        uint32_t levels = 1;
        static std::vector<uint8_t> mips;
        build_mips(buf.data(), SKYPLUS_TEX_W, SKYPLUS_TEX_H, mips, offs, &levels);
        buf.swap(mips);
        if (s_sky_slot >= 0) free_image(s_sky_blk, s_sky_slot);
        if (!upload_image(&s_sky_img, &s_sky_blk, DkImageFormat_RGBA8_Unorm, 4, buf.data(), offs, SKYPLUS_TEX_W,
                          SKYPLUS_TEX_H, levels, buf.size())) {
            return;
        }
        s_sky_slot = write_img_desc(&s_sky_img, false);
        s_sky_gen = p.texGen;
    }
    if (s_target_dirty) bind_target();
    const bool win = is_window(s_cur_fb);
    if (!win && !s_fbs[s_cur_fb].created) return;
    const int H = (int)fb_height(s_cur_fb);
    DkViewport v = { (float)d.vp[0], (float)(H - d.vp[1] - d.vp[3]), (float)d.vp[2], (float)d.vp[3], s_znear, s_zfar };
    dkCmdBufSetViewports(s_cmd, 0, &v, 1);
    set_scissor_rect(d.vp[0], d.vp[1], d.vp[2], d.vp[3]);
    DkSkyUniforms u;
    memset(&u, 0, sizeof u);
    auto v3 = [&](int i, const float* a, float w) { u.v[i][0] = a[0]; u.v[i][1] = a[1]; u.v[i][2] = a[2]; u.v[i][3] = w; };
    u.v[0][0] = (float)d.vp[0]; u.v[0][1] = (float)d.vp[1]; u.v[0][2] = (float)d.vp[2]; u.v[0][3] = (float)d.vp[3];
    u.v[1][0] = d.aspA; u.v[1][1] = d.aspB; u.v[1][2] = 0.0f;
    for (int i = 0; i < 4; i++) v3(2 + i, p.corner[i], 0.0f);
    v3(6, p.eye, p.cloudY);
    v3(7, p.sky, p.gain);
    v3(8, p.cloud, p.texScale);
    u.v[9][0] = p.layerOfs[0][0]; u.v[9][1] = p.layerOfs[0][1]; u.v[9][2] = p.layerOfs[1][0]; u.v[9][3] = p.layerOfs[1][1];
    u.v[10][0] = p.layerOfs[2][0]; u.v[10][1] = p.layerOfs[2][1]; u.v[10][2] = p.sunGlow; u.v[10][3] = p.night ? 1.0f : 0.0f;
    v3(11, p.sunDir, 0.0f);
    v3(12, p.sunCol, 0.0f);
    v3(13, p.moonDir, 0.0f);
    const int smp = sampler_slot(SmpKey{ DkFilter_Linear, DkFilter_Linear, DkMipFilter_Linear, DkWrapMode_Repeat, DkWrapMode_Repeat, 1 });
    draw_fullscreen(&s_sky_frag, &u, sizeof u, dkMakeTextureHandle(s_sky_slot, smp));
    restore_state();
}

/* D578 present slots */
static DkImage s_islot[GFX_INTERP_SLOTS];
static DkHeapBlock s_islot_blk[GFX_INTERP_SLOTS];
static uint32_t s_islot_w[GFX_INTERP_SLOTS], s_islot_h[GFX_INTERP_SLOTS];

void gfx_deko3d_interp_store(int slot, uint32_t width, uint32_t height) {
    if (!s_up || slot < 0 || slot >= GFX_INTERP_SLOTS || width == 0 || height == 0) return;
    if (width != s_islot_w[slot] || height != s_islot_h[slot]) {
        if (s_islot_w[slot]) s_imgHeap.free_after(s_islot_blk[slot], s_serial);
        s_islot_w[slot] = 0;
        if (!make_rt(&s_islot[slot], &s_islot_blk[slot], DkImageFormat_RGBA8_Unorm, width, height, DkMsMode_1x,
                     DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine)) {
            return;
        }
        s_islot_w[slot] = width;
        s_islot_h[slot] = height;
    }
    dkCmdBufBarrier(s_cmd, DkBarrier_Fragments, DkInvalidateFlags_Image);
    ensure_acquired();
    DkImageView sv, dv;
    dkImageViewDefaults(&sv, &s_win_img[s_acq < 0 ? 0 : s_acq]);
    dkImageViewDefaults(&dv, &s_islot[slot]);
    const uint32_t w = width < s_win_w ? width : s_win_w, h = height < s_win_h ? height : s_win_h;
    DkImageRect r = { 0, 0, 0, w, h, 1 };
    dkCmdBufBlitImage(s_cmd, &sv, &r, &dv, &r, DkBlitFlag_FilterNearest, 0);
    s_barrier_pending = true;
    s_cmd_pending = true;
}

void gfx_deko3d_interp_show(int slot) {
    if (!s_up || slot < 0 || slot >= GFX_INTERP_SLOTS || s_islot_w[slot] == 0) return;
    dkCmdBufBarrier(s_cmd, DkBarrier_Fragments, DkInvalidateFlags_Image);
    ensure_acquired();
    DkImageView sv, dv;
    dkImageViewDefaults(&sv, &s_islot[slot]);
    dkImageViewDefaults(&dv, &s_win_img[s_acq < 0 ? 0 : s_acq]);
    const uint32_t w = s_islot_w[slot] < s_win_w ? s_islot_w[slot] : s_win_w, h = s_islot_h[slot] < s_win_h ? s_islot_h[slot] : s_win_h;
    DkImageRect r = { 0, 0, 0, w, h, 1 };
    dkCmdBufBlitImage(s_cmd, &sv, &r, &dv, &r, DkBlitFlag_FilterNearest, 0);
    s_barrier_pending = true;
    s_cmd_pending = true;
    s_target_dirty = true;
}

void gfx_deko3d_interp_sync(void) {
    if (!s_up) return;
    const uint64_t t0 = dk_now_ns();
    dkQueueWaitIdle(s_queue);
    s_st_wait_idle_ns += dk_now_ns() - t0;
    retire();
}

void gfx_deko3d_frame_count_rewind(void) {
    s_frame_count--;
}

void gfx_deko3d_perf_report(double frames) {
    if (!perf_on() || frames <= 0) return;
    const double ms = 1.0e6;
    fprintf(stderr, "PERFSTAT dk waits (replay thread, ms/frame): frame throttle+fence %.2f, swapchain acquire %.2f, queue idle %.2f | submit cost %.2f\n",
            s_st_wait_frame_ns / ms / frames, s_st_wait_acq_ns / ms / frames, s_st_wait_idle_ns / ms / frames,
            s_st_submit_ns / ms / frames);
    s_st_wait_frame_ns = s_st_wait_acq_ns = s_st_wait_idle_ns = s_st_submit_ns = 0;
    fprintf(stderr, "PERFSTAT dk draws=%llu pushes=%llu ring_chunks=%llu uber_draws=%llu submits=%llu uploads=%llu (per %.0f frames) | data chunks %u, image heap chunks %u\n",
            (unsigned long long)s_st_draws, (unsigned long long)s_st_pushes, (unsigned long long)s_st_chunks,
            (unsigned long long)s_st_uber_draws, (unsigned long long)s_st_submits, (unsigned long long)s_st_uploads, frames,
            s_dataRing.chunks(), s_imgHeap.chunks());
    s_st_draws = s_st_pushes = s_st_chunks = s_st_uber_draws = s_st_submits = s_st_uploads = 0;
}

}   // extern "C"

#endif   /* __SWITCH__ */
