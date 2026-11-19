// seaview-nx: the GE+ sky and sea alone on the console, through deko3d and the game's own shaders
// (port/switch/shaders, compiled by uam with VIEWER defined), from one captured frame of the game
// (tools_pc/seaview/data/sea_dump.bin, in the romfs). The passes mirror gfx_deko3d.cpp's sky draw:
// reflection panorama, far-sea bake, the sea at half width, the final pass.
//
// Pad: left stick move, right stick look, ZL/ZR down/up, B (held) faster, D-pad up/down sea variant,
// D-pad left/right sky variant, L/R load (passes drawn 1..64 times), Y sway, X freeze time, A re-bake,
// + quit. The GPU time is printed on the nxlink stdio every second.
// Arguments (seaview.py nx-send turns a viewer .ini into them): uN=v (block Sky member N), prN=v
// (editable shader value N), sea=N, sky=N, tex=N, rep=N, sway=0/1.
#include <switch.h>
#include <deko3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include "../../common/clouds.h"

static const char* kSeaNames[] = { "current", "no medium swell", "1 layer", "2 layers", "3 layers", "bump, 1 layer (sunset light)",
    "bump, 2 layers (sunset light)", "no sky reflection", "all baked (nothing live)", "baked from 600-2500", "baked from 300-1500",
    "no calm patches", "2 layers, reflection, no swell or patches" };
static const char* kSkyNames[] = { "current", "1 cloud layer", "2 cloud layers", "no cloud lighting", "no halo or stars",
    "sky = low-res panorama", "flat sky (cost floor)" };
#define NSEA ((int)(sizeof kSeaNames / sizeof *kSeaNames))
#define NSKY ((int)(sizeof kSkyNames / sizeof *kSkyNames))
/* the shaders' PRM defaults (the game's constants) */
static const float kPrDef[16] = { 100000.f, 5.f, 0.9f, 0.08f, 0.5f, 0.35f, 0.35f, 1.45f, 1.f, 600.f, 6000.f, 5.f, 0.006f, 0.82f, 0.3f, 0.8f };

/* a 5x7 font for the on-screen counter (rows top first, bit 4 = leftmost column) */
static const char kFontChars[] = "0123456789.GPUMSXERCILAN/=OTFKY ";
static const unsigned char kFont[][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, {0,0,0,0,0,0,0} };
#define TXT_W 320
#define TXT_H 9
static void raster_text(const char* s, uint8_t* rgba) {
    memset(rgba, 0, TXT_W * TXT_H * 4);
    for (int i = 0; i < TXT_W * TXT_H; i++) rgba[i * 4 + 3] = 255;
    for (int k = 0; s[k] && (k + 1) * 6 <= TXT_W; k++) {
        const char* p = strchr(kFontChars, s[k]); if (!p) continue;
        const unsigned char* g = kFont[p - kFontChars];
        for (int y = 0; y < 7; y++) for (int x = 0; x < 5; x++) if (g[y] & (0x10 >> x)) {
            uint8_t* px = rgba + ((y + 1) * TXT_W + k * 6 + x + 1) * 4; px[0] = px[1] = px[2] = 255;
        }
    }
}

/* the game's resolution: 1280x720 handheld, 1920x1080 docked (gfx_deko3d.cpp create_window); set at start from the console's mode */
static int W = 1920, H = 1080;
#define HW (W / 2)
#define LUT_W 128
#define LUT_H 64
#define PANO_W 4096
#define PANO_H 512

static DkDevice g_dev;
static DkQueue g_queue;
struct Blk { DkMemBlock mb = nullptr; uint8_t* cpu = nullptr; DkGpuAddr gpu = 0; uint32_t size = 0; };
static Blk mkblk(uint32_t size, uint32_t flags) {
    Blk b;
    size = (size + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1);
    DkMemBlockMaker m; dkMemBlockMakerDefaults(&m, g_dev, size); m.flags = flags;
    b.mb = dkMemBlockCreate(&m);
    b.cpu = (flags & DkMemBlockFlags_CpuAccessMask) ? (uint8_t*)dkMemBlockGetCpuAddr(b.mb) : nullptr;
    b.gpu = dkMemBlockGetGpuAddr(b.mb); b.size = size;
    return b;
}
struct Img { DkImage img; Blk mem; int slot; uint32_t w, h, mips; };
static Blk g_desc;   /* image descriptors (16), then sampler descriptors (8) */
static int g_nslot = 0;
static void write_desc(Img& im) {
    DkImageView v; dkImageViewDefaults(&v, &im.img);
    dkImageDescriptorInitialize((DkImageDescriptor*)g_desc.cpu + im.slot, &v, false, false);
}
/* (re)make an image: the first time it takes a descriptor slot, later ones keep it and only replace the memory */
static void ensure_img(Img& im, uint32_t w, uint32_t h, uint32_t flags, uint32_t mips) {
    if (im.mem.mb) dkMemBlockDestroy(im.mem.mb); else im.slot = g_nslot++;
    im.w = w; im.h = h; im.mips = mips;
    DkImageLayoutMaker lm; dkImageLayoutMakerDefaults(&lm, g_dev);
    lm.flags = flags; lm.format = DkImageFormat_RGBA8_Unorm; lm.dimensions[0] = w; lm.dimensions[1] = h; lm.mipLevels = mips;
    DkImageLayout L; dkImageLayoutInitialize(&L, &lm);
    im.mem = mkblk((uint32_t)dkImageLayoutGetSize(&L), DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    dkImageInitialize(&im.img, &L, im.mem.mb, 0);
    write_desc(im);
}
static Img mkimg(uint32_t w, uint32_t h, uint32_t flags, uint32_t mips) {
    Img im; im.w = w; im.h = h; im.mips = mips;
    DkImageLayoutMaker lm; dkImageLayoutMakerDefaults(&lm, g_dev);
    lm.flags = flags; lm.format = DkImageFormat_RGBA8_Unorm; lm.dimensions[0] = w; lm.dimensions[1] = h; lm.mipLevels = mips;
    DkImageLayout L; dkImageLayoutInitialize(&L, &lm);
    im.mem = mkblk((uint32_t)dkImageLayoutGetSize(&L), DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    dkImageInitialize(&im.img, &L, im.mem.mb, 0);
    im.slot = g_nslot++;
    write_desc(im);
    return im;
}
static void mips_rgba8(const uint8_t* src, uint32_t w, uint32_t h, uint32_t levels, std::vector<uint8_t>& out, std::vector<uint32_t>& offs) {
    out.assign(src, src + w * h * 4); offs.assign(1, 0);
    uint32_t cw = w, ch = h, prev = 0;
    for (uint32_t l = 1; l < levels; l++) {
        const uint32_t nw = cw > 1 ? cw / 2 : 1, nh = ch > 1 ? ch / 2 : 1, base = (uint32_t)out.size();
        out.resize(base + nw * nh * 4); offs.push_back(base);
        for (uint32_t y = 0; y < nh; y++) for (uint32_t x = 0; x < nw; x++) for (int c = 0; c < 4; c++) {
            auto px = [&](uint32_t xx, uint32_t yy) { return out[prev + ((yy % ch) * cw + (xx % cw)) * 4 + c]; };
            out[base + (y * nw + x) * 4 + c] = (uint8_t)((px(2 * x, 2 * y) + px(2 * x + 1, 2 * y) + px(2 * x, 2 * y + 1) + px(2 * x + 1, 2 * y + 1) + 2) / 4);
        }
        prev = base; cw = nw; ch = nh;
    }
}
static DkCmdBuf g_cmd;
static Blk g_cmdmem, g_stage;
static void upload(Img& im, const uint8_t* rgba) {
    std::vector<uint8_t> data; std::vector<uint32_t> offs;
    mips_rgba8(rgba, im.w, im.h, im.mips, data, offs);
    if (data.size() > g_stage.size) { printf("staging too small\n"); return; }
    memcpy(g_stage.cpu, data.data(), data.size());
    uint32_t w = im.w, h = im.h;
    for (uint32_t l = 0; l < im.mips; l++) {
        DkImageView v; dkImageViewDefaults(&v, &im.img); v.mipLevelOffset = l; v.mipLevelCount = 1;
        DkCopyBuf src = { g_stage.gpu + offs[l], 0, 0 };
        DkImageRect r = { 0, 0, 0, w, h, 1 };
        dkCmdBufCopyBufferToImage(g_cmd, &src, &v, &r, 0);
        w = w > 1 ? w / 2 : 1; h = h > 1 ? h / 2 : 1;
    }
    dkQueueSubmitCommands(g_queue, dkCmdBufFinishList(g_cmd));
    dkQueueWaitIdle(g_queue);
    dkCmdBufClear(g_cmd); dkCmdBufAddMemory(g_cmd, g_cmdmem.mb, 0, g_cmdmem.size);
}
static Blk g_code; static uint32_t g_codeoff = 0;
static bool load_shader(DkShader* sh, const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) { printf("no %s\n", path); return false; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    g_codeoff = (g_codeoff + DK_SHADER_CODE_ALIGNMENT - 1) & ~(DK_SHADER_CODE_ALIGNMENT - 1);
    if (g_codeoff + n > g_code.size) { fclose(f); printf("code block too small\n"); return false; }
    fread(g_code.cpu + g_codeoff, 1, n, f); fclose(f);
    DkShaderMaker sm; dkShaderMakerDefaults(&sm, g_code.mb, g_codeoff); dkShaderInitialize(sh, &sm);
    g_codeoff += (uint32_t)n;
    return true;
}
struct V3 { float x, y, z; };
static V3 add(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static V3 sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static V3 mul(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
static float len(V3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
static V3 nrm(V3 a) { return mul(a, 1.0f / len(a)); }
static V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static V3 rotAxis(V3 v, V3 k, float a) { float c = cosf(a), s = sinf(a); return add(add(mul(v, c), mul(cross(k, v), s)), mul(k, (k.x * v.x + k.y * v.y + k.z * v.z) * (1 - c))); }

int main(int argc, char** argv) {
    const Result sr = socketInitializeDefault();
    const int nx = R_SUCCEEDED(sr) ? nxlinkStdio() : -1;
    setvbuf(stdout, nullptr, _IONBF, 0);   /* the nxlink stream gets every line at once, even if the program dies */
    romfsInit();
    {
        const bool handheld = appletGetOperationMode() == AppletOperationMode_Handheld;
        W = handheld ? 1280 : 1920; H = handheld ? 720 : 1080;
        printf("seaview-nx (sockets 0x%x, nxlink %d) %s %dx%d\n", (unsigned)sr, nx, handheld ? "handheld" : "docked", W, H);
    }

    /* the captured frame */
    float U[64]; uint8_t cloud[4096], wave[65536];
    {
        FILE* f = fopen("romfs:/sea_dump.bin", "rb");
        if (!f || fread(U, 4, 64, f) != 64 || fread(cloud, 1, 4096, f) != 4096 || fread(wave, 1, 65536, f) != 65536) { printf("no romfs:/sea_dump.bin\n"); return 1; }
        fclose(f);
    }
    float pr[24] = { 0 }; memcpy(pr, kPrDef, sizeof kPrDef);
    int seaVar = 0, skyVar = 0, tex = 0, rep = 1; bool sway = true, frozen = false, baked_build = false; int start_scale = 1;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i]; const char* eq = strchr(a, '='); if (!eq) continue;
        const float v = (float)atof(eq + 1);
        if (a[0] == 'u' && a[1] >= '0' && a[1] <= '9') { int k = atoi(a + 1); if (k >= 0 && k < 64) U[k] = v; }
        else if (!strncmp(a, "pr", 2)) { int k = atoi(a + 2); if (k >= 0 && k < 24) pr[k] = v; }
        else if (!strncmp(a, "sea=", 4)) seaVar = (int)v;
        else if (!strncmp(a, "sky=", 4)) skyVar = (int)v;
        else if (!strncmp(a, "tex=", 4)) tex = (int)v;
        else if (!strncmp(a, "rep=", 4)) rep = (int)v;
        else if (!strncmp(a, "sway=", 5)) sway = v != 0;
        else if (!strncmp(a, "baked=", 6)) baked_build = v != 0;
        else if (!strncmp(a, "scale=", 6)) start_scale = (int)v;
    }
    if (seaVar < 0 || seaVar >= NSEA) seaVar = 0;
    if (skyVar < 0 || skyVar >= NSKY) skyVar = 0;
    if (rep < 1) rep = 1;

    DkDeviceMaker dm; dkDeviceMakerDefaults(&dm); g_dev = dkDeviceCreate(&dm);
    DkQueueMaker qm; dkQueueMakerDefaults(&qm, g_dev); qm.flags = DkQueueFlags_Graphics; g_queue = dkQueueCreate(&qm);
    g_cmdmem = mkblk(4 << 20, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    DkCmdBufMaker cm; dkCmdBufMakerDefaults(&cm, g_dev); g_cmd = dkCmdBufCreate(&cm);
    dkCmdBufAddMemory(g_cmd, g_cmdmem.mb, 0, g_cmdmem.size);
    g_desc = mkblk(32 * sizeof(DkImageDescriptor) + 8 * sizeof(DkSamplerDescriptor), DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    g_stage = mkblk(1 << 20, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    g_code = mkblk(1 << 20, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
    Blk ubo = mkblk(4096, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    Blk qry = mkblk(4096, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);

    DkShader vs, fsSky, fsLut, fsOv;
    if (!load_shader(&vs, "romfs:/fullscreen.vert.dksh") || !load_shader(&fsSky, "romfs:/skyplus.frag.dksh") || !load_shader(&fsLut, "romfs:/skylut.frag.dksh")
        || !load_shader(&fsOv, "romfs:/overlay.frag.dksh")) return 1;

    /* samplers: 0 clouds (mips, repeat), 1 waves (mips, repeat, aniso 2), 2 panoramas (repeat x, clamp y), 3 clamp */
    {
        DkSamplerDescriptor* sd = (DkSamplerDescriptor*)(g_desc.cpu + 32 * sizeof(DkImageDescriptor));
        for (int i = 0; i < 5; i++) {   /* 4: nearest, clamp (the counter's text) */
            DkSampler s; dkSamplerDefaults(&s);
            s.minFilter = i == 4 ? DkFilter_Nearest : DkFilter_Linear; s.magFilter = i == 4 ? DkFilter_Nearest : DkFilter_Linear;
            s.mipFilter = i < 2 ? DkMipFilter_Linear : DkMipFilter_None;
            DkWrapMode wx = i >= 3 ? DkWrapMode_ClampToEdge : DkWrapMode_Repeat, wy = i >= 2 ? DkWrapMode_ClampToEdge : DkWrapMode_Repeat;
            s.wrapMode[0] = wx; s.wrapMode[1] = wy; s.wrapMode[2] = wy;
            s.maxAnisotropy = i == 1 ? 2.0f : 1.0f;
            dkSamplerDescriptorInitialize(&sd[i], &s);
        }
    }
    /* the targets a later pass samples: as the game's (gfx_deko3d.cpp build_fb_images), no hardware compression: sampling a compressed
     * target makes the GPU decompress it, which cost the viewer several times what the game's same passes cost */
    const uint32_t RT = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
    Img tCloud = mkimg(64, 64, 0, 7), tWave = mkimg(128, 128, 0, 8), tDummy = mkimg(1, 1, 0, 1);
    Img tLut = mkimg(LUT_W, LUT_H, RT, 1), tPano = mkimg(PANO_W, PANO_H, RT, 1);
    /* the render scale (Minus): 1, 1/2, 1/4 of the screen on each axis. The sea target is half width of that (as the game's), the final
     * pass goes into a sky target of that size, then is enlarged onto the screen */
    Img tHalfS[3] = {}, tSkyS[2] = {};   /* size-dependent: build_screen() makes them, at start and when the console is docked or undocked */
    Img tText = mkimg(TXT_W, TXT_H, 0, 1);
    Blk txtStage = mkblk(TXT_W * TXT_H * 4, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    double gpuEma = 0, fpsEma = 0; u64 tFrame = armGetSystemTick();
    /* the swapchain and everything the screen's size decides: made at start, and again when the console is docked or undocked (as the
     * game's check_dock / create_window do) */
    const int NSWAP = 3;   /* triple buffering, as the game: with two, the console waits on the display and locks to 30 fps */
    Img win[3] = {};
    DkSwapchain swap = nullptr;
    auto build_screen = [&]() {
        dkQueueWaitIdle(g_queue);
        if (swap) { dkSwapchainDestroy(swap); swap = nullptr; }
        const uint32_t hw = (uint32_t)W / 2, h = (uint32_t)H;
        ensure_img(tHalfS[0], hw, h, RT, 1); ensure_img(tHalfS[1], hw / 2, h / 2, RT, 1); ensure_img(tHalfS[2], hw / 4, h / 4, RT, 1);
        ensure_img(tSkyS[0], (uint32_t)W / 2, h / 2, RT, 1); ensure_img(tSkyS[1], (uint32_t)W / 4, h / 4, RT, 1);
        nwindowSetDimensions(nwindowGetDefault(), W, H);
        for (int i = 0; i < NSWAP; i++) ensure_img(win[i], (uint32_t)W, h, DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression, 1);   /* the display images: as the game's */
        const DkImage* wimgs[3] = { &win[0].img, &win[1].img, &win[2].img };
        DkSwapchainMaker scm; dkSwapchainMakerDefaults(&scm, g_dev, nwindowGetDefault(), wimgs, NSWAP);
        swap = dkSwapchainCreate(&scm);
        dkSwapchainSetSwapInterval(swap, 1);   /* one refresh per image (60 Hz): the default can be two (30 fps) */
    };
    build_screen();
    bool handheldNow = W == 1280;

    {   /* the textures */
        std::vector<uint8_t> c4(64 * 64 * 4); uint8_t t64[4096];
        makeCloudTex(tex, cloud, t64);
        for (int i = 0; i < 4096; i++) c4[i * 4] = c4[i * 4 + 1] = c4[i * 4 + 2] = c4[i * 4 + 3] = t64[i];
        upload(tCloud, c4.data());
        upload(tWave, wave);
        const uint8_t black[4] = { 0, 0, 0, 255 };
        upload(tDummy, black);
    }

    /* the camera, from the captured corner rays */
    V3 C[4]; for (int i = 0; i < 4; i++) C[i] = { U[(2 + i) * 4], U[(2 + i) * 4 + 1], U[(2 + i) * 4 + 2] };
    V3 cen = mul(add(add(C[0], C[1]), add(C[2], C[3])), 0.25f);
    V3 hw = mul(add(sub(C[1], C[0]), sub(C[3], C[2])), 0.25f), hh = mul(add(sub(C[0], C[2]), sub(C[1], C[3])), 0.25f);
    hh = mul(nrm(hh), len(hw) * (float)H / (float)W);
    V3 eye = { U[24], U[25], U[26] };
    float yaw = 0, pitch = 0;

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);
    bool baked = false; V3 beye = eye; int bsea = -1;
    int rsi = start_scale >= 4 ? 2 : (start_scale >= 2 ? 1 : 0);   /* render scale index: 0 full, 1 half, 2 quarter (Minus; argument scale=1|2|4) */
    double T = 1.0, gpuAcc = 0; int gpuN = 0, frame = 0;
    u64 tPrint = armGetSystemTick();
    printf("sea: %s | sky: %s | load x%d\n", kSeaNames[seaVar], kSkyNames[skyVar], rep);

    while (appletMainLoop()) {
        {   /* docked or undocked while running: the game's resolution follows (1280x720 handheld, 1920x1080 docked) */
            const bool hhd = appletGetOperationMode() == AppletOperationMode_Handheld;
            if (hhd != handheldNow) {
                handheldNow = hhd;
                W = hhd ? 1280 : 1920; H = hhd ? 720 : 1080;
                build_screen();
                gpuEma = 0; gpuN = 0; gpuAcc = 0;
                printf("%s %dx%d\n", hhd ? "handheld" : "docked", W, H);
            }
        }
        padUpdate(&pad);
        const u64 down = padGetButtonsDown(&pad), held = padGetButtons(&pad);
        if (down & HidNpadButton_Plus) break;
        bool changed = false;
        if (!baked_build && (down & HidNpadButton_Up)) { seaVar = (seaVar + 1) % NSEA; changed = true; }
        if (!baked_build && (down & HidNpadButton_Down)) { seaVar = (seaVar + NSEA - 1) % NSEA; changed = true; }
        if (!baked_build && (down & HidNpadButton_Right)) { skyVar = (skyVar + 1) % NSKY; changed = true; }
        if (!baked_build && (down & HidNpadButton_Left)) { skyVar = (skyVar + NSKY - 1) % NSKY; changed = true; }
        if ((down & HidNpadButton_R) && rep < 64) { rep *= 2; changed = true; }
        if ((down & HidNpadButton_L) && rep > 1) { rep /= 2; changed = true; }
        if (down & HidNpadButton_Minus) { rsi = (rsi + 1) % 3; gpuEma = 0; printf("render scale 1/%d\n", 1 << rsi); }
        if (down & HidNpadButton_Y) sway = !sway;
        if (down & HidNpadButton_X) frozen = !frozen;
        if (down & HidNpadButton_A) baked = false;
        if (changed) { printf("sea: %s | sky: %s | load x%d\n", kSeaNames[seaVar], kSkyNames[skyVar], rep); gpuAcc = 0; gpuN = 0; }
        const HidAnalogStickState ls = padGetStickPos(&pad, 0), rs = padGetStickPos(&pad, 1);
        /* the real time of the last image: movement and the clock do not depend on the frame rate */
        static u64 tPrevLoop = armGetSystemTick();
        const u64 tNowLoop = armGetSystemTick();
        float dt = (float)armTicksToNs(tNowLoop - tPrevLoop) / 1.0e9f; tPrevLoop = tNowLoop;
        if (dt < 0.001f) dt = 0.001f; if (dt > 0.1f) dt = 0.1f;
        const float speed = (held & HidNpadButton_B) ? 6000.0f : 700.0f;   /* units per second (Frigate's deck is ~500 high) */
        const float sp = dt * speed;
        {   /* stick right turns toward the camera's right, whatever the handedness of the game's axes */
            const V3 f0 = nrm({ cen.x, 0, cen.z }), r0 = nrm({ hw.x, 0, hw.z }), t = cross({ 0, 1, 0 }, f0);
            const float ys = t.x * r0.x + t.z * r0.z > 0 ? 1.0f : -1.0f;
            yaw += ys * rs.x / 32767.0f * dt * 1.5f; pitch += rs.y / 32767.0f * dt * 1.0f;
        }
        if (pitch > 1.5f) pitch = 1.5f; if (pitch < -1.5f) pitch = -1.5f;
        V3 fw0 = nrm({ cen.x, 0, cen.z });
        /* the camera's own frame: forward and right turn with the same rotation as the view (rotAxis about +y) */
        const V3 rt0 = nrm({ hw.x, 0, hw.z });
        V3 fw = rotAxis(fw0, { 0, 1, 0 }, yaw), rt = rotAxis(rt0, { 0, 1, 0 }, yaw);
        eye = add(eye, mul(fw, ls.y / 32767.0f * sp)); eye = add(eye, mul(rt, ls.x / 32767.0f * sp));
        if (held & HidNpadButton_ZR) eye.y += sp * 0.5f;
        if (held & HidNpadButton_ZL) eye.y -= sp * 0.5f;
        if (!frozen) T += dt;

        /* the view, as the PC viewer builds it (breathing sway: ~0.15 deg over 2 s, a tiny roll) */
        const float swp = sway ? 0.0026f * sinf((float)T * 3.14159f) : 0.f, swr = sway ? 0.0012f * sinf((float)T * 2.1f) : 0.f;
        V3 up0 = { 0, 1, 0 }, rax = nrm(hw);
        V3 cc = rotAxis(cen, rax, pitch + swp), hx = hw, hy = rotAxis(hh, rax, pitch + swp);
        cc = rotAxis(cc, up0, yaw); hx = rotAxis(hx, up0, yaw); hy = rotAxis(hy, up0, yaw);
        if (swr != 0) { V3 ax = nrm(cc); hx = rotAxis(hx, ax, swr); hy = rotAxis(hy, ax, swr); }
        V3 cor[4] = { add(sub(cc, hx), hy), add(add(cc, hx), hy), sub(sub(cc, hx), hy), sub(add(cc, hx), hy) };
        float u[64]; memcpy(u, U, 256);
        u[0] = 0; u[1] = 0; u[2] = (float)W; u[3] = (float)H; u[4] = 1; u[5] = 0; u[6] = 0;
        for (int i = 0; i < 4; i++) { u[(2 + i) * 4] = cor[i].x; u[(2 + i) * 4 + 1] = cor[i].y; u[(2 + i) * 4 + 2] = cor[i].z; u[(2 + i) * 4 + 3] = 0; }
        u[24] = eye.x; u[25] = eye.y; u[26] = eye.z;
        u[60] = (float)T; u[61] = 1.0f; u[62] = 1.0f; u[63] = 0.0f;
        struct { int var[4]; float pr[24]; } vb = { { seaVar, skyVar, 0, 0 }, {} };
        memcpy(vb.pr, pr, sizeof vb.pr);

        const int slot = dkQueueAcquireImage(g_queue, swap);
        DkCmdBuf c = g_cmd;
        dkCmdBufBindImageDescriptorSet(c, g_desc.gpu, 32);
        dkCmdBufBindSamplerDescriptorSet(c, g_desc.gpu + 32 * sizeof(DkImageDescriptor), 8);
        dkCmdBufBarrier(c, DkBarrier_None, DkInvalidateFlags_Descriptors);
        dkCmdBufBindUniformBuffer(c, DkStage_Fragment, 0, ubo.gpu, 256);
        dkCmdBufBindUniformBuffer(c, DkStage_Fragment, 1, ubo.gpu + 256, 256);
        DkRasterizerState rs0; dkRasterizerStateDefaults(&rs0); rs0.cullMode = DkFace_None; dkCmdBufBindRasterizerState(c, &rs0);
        DkColorState cs0; dkColorStateDefaults(&cs0); dkCmdBufBindColorState(c, &cs0);
        DkColorWriteState cw0; dkColorWriteStateDefaults(&cw0); dkCmdBufBindColorWriteState(c, &cw0);
        DkDepthStencilState ds0; dkDepthStencilStateDefaults(&ds0); ds0.depthTestEnable = 0; ds0.depthWriteEnable = 0; dkCmdBufBindDepthStencilState(c, &ds0);
        dkCmdBufBindVtxAttribState(c, nullptr, 0);
        dkCmdBufBindVtxBufferState(c, nullptr, 0);
        dkCmdBufPushConstants(c, ubo.gpu + 256, 256, 0, sizeof vb, &vb);
        dkCmdBufReportCounter(c, DkCounter_Timestamp, qry.gpu);
        auto tex2 = [&](int binding, const Img& im, int smp) { DkResHandle h = dkMakeTextureHandle(im.slot, smp); dkCmdBufBindTextures(c, DkStage_Fragment, binding, &h, 1); };
        auto target = [&](Img& im) {
            DkImageView v; dkImageViewDefaults(&v, &im.img); dkCmdBufBindRenderTarget(c, &v, nullptr);
            DkViewport vp = { 0, 0, (float)im.w, (float)im.h, 0, 1 }; dkCmdBufSetViewports(c, 0, &vp, 1);
            DkScissor sc = { 0, 0, im.w, im.h }; dkCmdBufSetScissors(c, 0, &sc, 1);
        };
        auto draw = [&](DkShader* fs, int passId, int times) {
            u[11] = (float)passId;
            dkCmdBufPushConstants(c, ubo.gpu, 256, 0, 256, u);
            const DkShader* sh[2] = { &vs, fs }; dkCmdBufBindShaders(c, DkStageFlag_GraphicsMask, sh, 2);
            for (int r = 0; r < times; r++) dkCmdBufDraw(c, DkPrimitive_Triangles, 3, 1, 0, 0);
        };
        auto barrier = [&]() { dkCmdBufBarrier(c, DkBarrier_Fragments, DkInvalidateFlags_Image); };
        if (frame % 15 == 0) {   /* the counter's text, four times a second */
            char line[64];
            snprintf(line, sizeof line, "%2.0f FPS  GPU %5.2f MS  SCALE 1/%d  X%d  SEA %d  SKY %d", fpsEma, gpuEma, 1 << rsi, rep, seaVar, skyVar);
            raster_text(line, txtStage.cpu);
            DkImageView tv; dkImageViewDefaults(&tv, &tText.img);
            DkCopyBuf src = { txtStage.gpu, 0, 0 }; DkImageRect r = { 0, 0, 0, TXT_W, TXT_H, 1 };
            dkCmdBufCopyBufferToImage(c, &src, &tv, &r, 0);
            dkCmdBufBarrier(c, DkBarrier_Full, DkInvalidateFlags_Image);
        }
        tex2(0, tCloud, 0); tex2(1, tWave, 1); tex2(2, tDummy, 3); tex2(3, tDummy, 3); tex2(4, tDummy, 3);
        /* 1. the reflection panorama */
        target(tLut); draw(&fsLut, 0, 1); barrier();
        tex2(2, tLut, 2);
        /* 2. the far sea, baked at start, and again when the variant changes (as the game: not for a move) */
        const float ex = eye.x - beye.x, ez = eye.z - beye.z;
        (void)ex; (void)ez;
        if (!baked || bsea != seaVar) {   /* as the game: never for a move, only when what the bake shows changes */
            target(tPano); draw(&fsSky, 4, 1); barrier();
            baked = true; beye = eye; bsea = seaVar;
        }
        u[15] = beye.x; u[19] = beye.z; u[23] = beye.y - u[59];   /* where the panorama was baked from (uBake*) */
        tex2(4, tPano, 2);
        /* 3. the sea at half width */
        target(tHalfS[rsi]); draw(&fsSky, 1, rep); barrier();
        tex2(3, tHalfS[rsi], 3);
        /* 4. the final pass: at full size onto the screen, or into the reduced sky target, enlarged onto the screen */
        if (rsi == 0) {
            target(win[slot]); draw(&fsSky, 2, rep);
        } else {
            target(tSkyS[rsi - 1]); draw(&fsSky, 2, rep); barrier();
            target(win[slot]);
            const float full[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
            dkCmdBufPushConstants(c, ubo.gpu, 256, 0, sizeof full, full);
            tex2(0, tSkyS[rsi - 1], 3);   /* linear, clamped */
            const DkShader* shc[2] = { &vs, &fsOv }; dkCmdBufBindShaders(c, DkStageFlag_GraphicsMask, shc, 2);
            dkCmdBufDraw(c, DkPrimitive_Triangles, 3, 1, 0, 0);
        }
        dkCmdBufReportCounter(c, DkCounter_Timestamp, qry.gpu + 16);
        {   /* the counter, top left, 3x */
            const float rect[4] = { 20.0f / W, 1.0f - (20.0f + TXT_H * 3) / H, (float)(TXT_W * 3) / W, (float)(TXT_H * 3) / H };
            dkCmdBufPushConstants(c, ubo.gpu, 256, 0, sizeof rect, rect);
            tex2(0, tText, 4);
            const DkShader* sh[2] = { &vs, &fsOv }; dkCmdBufBindShaders(c, DkStageFlag_GraphicsMask, sh, 2);
            dkCmdBufDraw(c, DkPrimitive_Triangles, 3, 1, 0, 0);
        }
        dkQueueSubmitCommands(g_queue, dkCmdBufFinishList(c));
        dkQueuePresentImage(g_queue, swap, slot);
        dkQueueWaitIdle(g_queue);
        dkCmdBufClear(g_cmd); dkCmdBufAddMemory(g_cmd, g_cmdmem.mb, 0, g_cmdmem.size);
        const uint64_t* q = (const uint64_t*)qry.cpu;
        if (q[3] > q[1]) {
            const double ms = (double)dkTimestampToNs(q[3] - q[1]) / 1.0e6;
            gpuAcc += ms; gpuN++;
            gpuEma = gpuEma == 0 ? ms : gpuEma * 0.95 + ms * 0.05;
        }
        frame++;
        {   /* frames per second, from the time between two presents */
            const u64 tn = armGetSystemTick(); const double s = (double)armTicksToNs(tn - tFrame) / 1.0e9; tFrame = tn;
            if (s > 0) fpsEma = fpsEma == 0 ? 1.0 / s : fpsEma * 0.95 + (1.0 / s) * 0.05;
        }
        const u64 now = armGetSystemTick();
        if (armTicksToNs(now - tPrint) > 1000000000ull && gpuN > 0) {
            printf("GPU %.2f ms/frame | %dx%d scale 1/%d | load x%d | sea: %s | sky: %s\n", gpuAcc / gpuN, W, H, 1 << rsi, rep, kSeaNames[seaVar], kSkyNames[skyVar]);
            gpuAcc = 0; gpuN = 0; tPrint = now;
        }
    }
    /* everything deko3d made goes back before the process ends (the swapchain first: it holds the window) */
    dkQueueWaitIdle(g_queue);
    dkSwapchainDestroy(swap);
    dkCmdBufDestroy(g_cmd);
    Img* imgs[] = { &tCloud, &tWave, &tDummy, &tLut, &tPano, &tHalfS[0], &tHalfS[1], &tHalfS[2], &tSkyS[0], &tSkyS[1], &tText, &win[0], &win[1], &win[2] };
    for (Img* im : imgs) dkMemBlockDestroy(im->mem.mb);
    for (Blk* b : { &g_cmdmem, &g_desc, &g_stage, &g_code, &ubo, &qry, &txtStage }) dkMemBlockDestroy(b->mb);
    dkQueueDestroy(g_queue);
    dkDeviceDestroy(g_dev);
    printf("seaview-nx: bye\n");
    romfsExit();
    socketExit();
    return 0;
}
