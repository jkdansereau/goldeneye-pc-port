/* Issue #44: Vulkan rendering backend for the PC build (GE_RENDERER=vk; GL
 * stays the default until this backend matches it).
 *
 * A GfxRenderingAPI that the frame replay (gfx_frame.cpp) drives exactly like
 * the GL backend: same call order, same state semantics (gfx_opengl.cpp is the
 * reference for every mapping). The structure follows gfx_deko3d.cpp (D973),
 * which already solves the same problems in explicit-API terms; most mappings
 * below are its, translated.
 *
 * Loader: volk (port/fast3d/volk/, MIT) opens the Vulkan loader at run time,
 * so the binary does not link it; without a usable driver gfx_vulkan_probe
 * fails and video.c falls back to GL.
 *
 * Target: Vulkan 1.3 core (dynamic rendering, synchronization2, extended
 * dynamic state) plus VK_KHR_push_descriptor. Pipelines key on program x blend
 * x colour writes x MSAA x alpha-to-coverage x attachment formats; viewport,
 * scissor, depth test / write / compare, depth bias and culling are dynamic.
 * MoltenVK: portability enumeration / subset are enabled when offered.
 *
 * Orientation: as deko3d. Every target (the window too) is stored top-down,
 * the way the display shows it; the viewport has a negative height so NDC
 * y = +1 is the top, as in GL, and the winding GL sees is kept. GL's
 * bottom-up viewport / scissor / clear rectangles are mirrored at the
 * boundary (y' = H - y - h). Depth: VK_EXT_depth_clip_control gives GL's
 * -1..1 clip z when the device has it; otherwise get_clip_parameters asks
 * gfx_pc.cpp for 0..1 z (z_is_from_0_to_1).
 *
 * Shaders: the SPIR-V pack embedded in the binary (gfx_vk_pack.S, built by
 * cmake/VulkanShaders.cmake from the deko3d pack's GLSL). Descriptor set 0,
 * pushed per draw: binding 0 / 1 the two textures, 4 the Draw block
 * (DkDrawUniforms), 5 the uber-shader's Comb block (DkCombinerParams), 6 the
 * mesh block. A combiner missing from the pack is drawn by the uber-shader
 * and logged once ("SPIR-V miss").
 *
 * Frames: FRAMES_IN_FLIGHT command-buffer pairs, each with a fence and the
 * serial of its submission. The "pre" buffer holds the frame's texture
 * uploads (always into a fresh image, so they can run ahead of the draws), the
 * main buffer the rendering; both go in one submission. Host memory (vertices,
 * uniforms, upload staging) comes from a ring and images from a first-fit heap
 * (gfx_dk_heap.h); both are recycled once the serial that last used them has
 * completed.
 *
 * Validation: VK_LAYER_KHRONOS_validation is enabled whenever it is installed,
 * unless GE_VKVALIDATE=0; every message goes to the log and the counts are
 * reported at exit.
 *
 * Framebuffers: an off-screen fb is a single-sample colour image (sampled with
 * alpha forced to 1, copied, resolved into), a multisample colour image it
 * renders into under MSAA, and depth at the rendering's sample count. Each
 * image's layout is tracked; transitions, copies and resolves happen between
 * rendering passes (a pass is ended when a draw samples a framebuffer that is
 * not yet readable). Copies are blits with GL's rectangles mirrored on both
 * sides, as in deko3d.
 *
 * Post passes: FXAA and the GE+ sky draw the pack's fullscreen triangle with
 * their own pipelines; the D578 present slots are image copies of the window
 * in the swapchain's format.
 *
 * Mesh path (GE_MESHCACHE=1 on PC): static vertex buffers drawn with the pack's
 * GPU-transform vertex shader (uniform block 6), culling as dynamic state.
 *
 * Status (step 5 of the issue): drawing, textures, depth, off-screen
 * framebuffers, MSAA, copies, clear-region, FXAA, the GE+ sky, present slots,
 * the mesh path. Not yet: the on-disk pipeline cache (step 6), a readback for
 * GE_PCDUMP. */

#ifdef GE_VULKAN

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <map>
#include <string>
#include <tuple>
#include <vector>
#include <utility>

#include "volk/volk.h"
#include <SDL.h>
#include <SDL_vulkan.h>

/* the few GBI names used here (as gfx_deko3d.cpp) */
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
#include "gfx_vulkan.h"
#include "gfx_sdl.h"
#include "gfx_dk_layout.h"
#include "gfx_dk_heap.h"
#include "skyplus.h"

extern bool gfx_framebuffers_enabled;           /* gfx_pc.cpp */
extern int gfx_rdp_affine;                      /* gfx_pc.cpp (D540) */
extern int gfx_fog_vertex;                      /* gfx_pc.cpp (D543) */
extern int gfx_fog_force;                       /* gfx_pc.cpp (D982) */
extern "C" int portDisplayFixes;                /* port/src/video.c (D982) */
extern "C" volatile int gfx_skyplus_sea;        /* gfx_opengl.cpp: 1 = skyplus.c may give the water levels the sea */
extern "C" volatile int gfx_skyplus_failed;     /* gfx_opengl.cpp: 1 = skyplus.c draws GE's own sky */
extern "C" const uint8_t ge007_spv_pack[], ge007_spv_pack_end[];   /* gfx_vk_pack.S */

struct ShaderProgram {
    uint64_t id0;
    uint32_t id1;
    bool uber;
    VkShaderModule vert, frag, mesh_vert;
    struct DkCombinerParams params;
    struct DkLayout layout;
    uint8_t num_inputs;
    bool used_textures[2];
    uint32_t stride;
};

namespace {

const int FRAMES_IN_FLIGHT = 2;
const uint32_t HOST_CHUNK = 8u << 20;    /* vertices, uniforms, upload staging */
const uint32_t IMG_CHUNK = 64u << 20;    /* device-local image memory */
const VkDeviceSize UBO_COMB_OFF = 256;   /* Comb after Draw in one uniform allocation */

#define VK_CHECK(call)                                                                      \
    do {                                                                                    \
        VkResult r_ = (call);                                                               \
        if (r_ != VK_SUCCESS) {                                                             \
            sysFatalError("Vulkan: %s failed (VkResult %d)\n%s:%d", #call, (int)r_,         \
                          __FILE__, __LINE__);                                              \
        }                                                                                   \
    } while (0)

/* ---------------- instance, device ---------------- */

bool s_loader_ok = false;
bool s_up = false, s_parked = false;
VkInstance s_instance = VK_NULL_HANDLE;
VkDebugUtilsMessengerEXT s_messenger = VK_NULL_HANDLE;
VkPhysicalDevice s_phys = VK_NULL_HANDLE;
VkDevice s_dev = VK_NULL_HANDLE;
uint32_t s_qfam = 0;
VkQueue s_queue = VK_NULL_HANDLE;
VkSurfaceKHR s_surface = VK_NULL_HANDLE;
char s_renderer_str[256] = "Vulkan";
int s_validation_errors = 0, s_validation_warnings = 0;

/* device capabilities in use */
bool s_clip_control = false;     /* VK_EXT_depth_clip_control: GL's -1..1 clip z */
bool s_mirror_clamp = false;     /* samplerMirrorClampToEdge */
bool s_depth_clamp = false;
float s_max_aniso = 1.0f;        /* 1 = no anisotropic filtering */
VkDeviceSize s_ubo_align = 256;
uint32_t s_max_tex = 8192;
VkSampleCountFlags s_sample_counts = VK_SAMPLE_COUNT_1_BIT;   /* colour and depth targets */
VkPhysicalDeviceMemoryProperties s_memprops;

/* ---------------- swapchain, frames ---------------- */

VkSwapchainKHR s_swapchain = VK_NULL_HANDLE;
VkFormat s_sc_format = VK_FORMAT_B8G8R8A8_UNORM;
VkExtent2D s_sc_extent = { 0, 0 };
VkPresentModeKHR s_sc_mode = VK_PRESENT_MODE_FIFO_KHR;
std::vector<VkImage> s_sc_images;
std::vector<VkImageView> s_sc_views;
std::vector<VkSemaphore> s_sc_render_done;   /* per swapchain image: present waits on it */
bool s_sc_dirty = false;                     /* recreate before the next acquire */
int s_swap_interval = 1;
int s_acq = -1;                              /* acquired swapchain image this frame, -1 = none */
bool s_acq_failed = false;                   /* this frame cannot reach the window (minimised) */
VkImageLayout s_sc_layout = VK_IMAGE_LAYOUT_UNDEFINED;   /* of the acquired image */
bool s_sc_readable = false;                               /* swapchain images are transfer sources */

struct Frame {
    VkCommandPool pool;
    VkCommandBuffer pre, cmd;   /* uploads, then rendering; one submission */
    VkFence fence;
    VkSemaphore acquired;
    uint64_t serial;            /* of its last submission */
};
Frame s_frames[FRAMES_IN_FLIGHT];
uint32_t s_frame_idx = 0;
bool s_frame_open = false;
uint64_t s_serial = 1, s_completed = 0;

VkCommandBuffer pre_cmd(void) { return s_frames[s_frame_idx].pre; }
VkCommandBuffer main_cmd(void) { return s_frames[s_frame_idx].cmd; }

/* ---------------- memory ---------------- */

struct HostChunk {
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    uint8_t* cpu = nullptr;
};
DkRing s_ring(HOST_CHUNK);
std::vector<HostChunk> s_ring_chunks;
HostChunk s_zero;   /* 256 zero bytes: the uber-shader's absent attributes (stride 0) */

struct ImgHeap {
    DkHeap heap{ IMG_CHUNK };
    std::vector<VkDeviceMemory> mem;
};
std::map<uint32_t, ImgHeap> s_img_heaps;   /* by memory type */

struct Img {
    VkImage img = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImageView view_a1 = VK_NULL_HANDLE;   /* framebuffer colour sampled as a texture: alpha reads 1 */
    uint32_t memtype = 0;
    DkHeapBlock blk = {};
};
struct DeadImg { Img i; uint64_t serial; };
std::vector<DeadImg> s_dead_imgs;
std::vector<std::pair<VkPipeline, uint64_t>> s_dead_pipes;
/* GE_MESHGPU: static vertex buffers (host-visible for now), freed once the
 * submissions that may read them have completed */
struct VkMesh { HostChunk chunk; uint32_t bytes = 0; };
std::vector<VkMesh> s_meshes(1);
std::vector<std::pair<HostChunk, uint64_t>> s_dead_meshes;

/* PERFSTAT counters */
uint64_t s_st_draws, s_st_uber_draws, s_st_uploads, s_st_pipelines, s_st_mesh_draws, s_st_mesh_total;
double s_st_pipe_ms, s_st_pipe_ms_max;   /* time inside vkCreateGraphicsPipelines (a pipeline-cache hit is fast) */

VKAPI_ATTR VkBool32 VKAPI_CALL debug_cb(VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT,
                                        const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    const char* msg = data && data->pMessage ? data->pMessage : "(null)";
    if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        s_validation_errors++;
        if (s_validation_errors <= 64) sysLogPrintf(LOG_ERROR, "VK validation: %s", msg);
    } else if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        s_validation_warnings++;
        if (s_validation_warnings <= 64) sysLogPrintf(LOG_WARNING, "VK validation: %s", msg);
    } else {
        sysLogPrintf(LOG_NOTE, "VK: %s", msg);
    }
    return VK_FALSE;
}

bool has_layer(const char* name) {
    uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> v(n);
    vkEnumerateInstanceLayerProperties(&n, v.data());
    for (auto& l : v) {
        if (strcmp(l.layerName, name) == 0) return true;
    }
    return false;
}

bool has_instance_ext(const char* name) {
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> v(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, v.data());
    for (auto& e : v) {
        if (strcmp(e.extensionName, name) == 0) return true;
    }
    return false;
}

bool has_device_ext(VkPhysicalDevice pd, const char* name) {
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> v(n);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, v.data());
    for (auto& e : v) {
        if (strcmp(e.extensionName, name) == 0) return true;
    }
    return false;
}

bool load_loader(void) {
    if (!s_loader_ok) {
        if (volkInitialize() != VK_SUCCESS) {
            sysLogPrintf(LOG_WARNING, "VK: no Vulkan loader found");
            return false;
        }
        if (volkGetInstanceVersion() < VK_API_VERSION_1_3) {
            const uint32_t v = volkGetInstanceVersion();
            sysLogPrintf(LOG_WARNING, "VK: loader is Vulkan %u.%u, 1.3 needed", VK_API_VERSION_MAJOR(v),
                         VK_API_VERSION_MINOR(v));
            return false;
        }
        s_loader_ok = true;
    }
    return true;
}

/* A device the backend can use: Vulkan 1.3, dynamic rendering, synchronization2,
 * extended dynamic state, swapchain, push descriptors, and (when a surface is
 * given) a graphics queue that presents to it. Returns the queue family, or -1. */
int usable_queue_family(VkPhysicalDevice pd, VkSurfaceKHR surface) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(pd, &props);
    if (props.apiVersion < VK_API_VERSION_1_3) return -1;
    VkPhysicalDeviceVulkan13Features f13 = {};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceFeatures2 f2 = {};
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f2.pNext = &f13;
    vkGetPhysicalDeviceFeatures2(pd, &f2);
    if (!f13.dynamicRendering || !f13.synchronization2) return -1;
    if (!has_device_ext(pd, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) return -1;
    if (!has_device_ext(pd, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) return -1;
    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, nullptr);
    std::vector<VkQueueFamilyProperties> q(nq);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, q.data());
    for (uint32_t i = 0; i < nq; i++) {
        if (!(q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        if (surface != VK_NULL_HANDLE) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &present);
            if (!present) continue;
        }
        return (int)i;
    }
    return -1;
}

/* Discrete over integrated over the rest. */
VkPhysicalDevice pick_device(VkInstance inst, VkSurfaceKHR surface, uint32_t* qfam) {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, nullptr);
    std::vector<VkPhysicalDevice> v(n);
    vkEnumeratePhysicalDevices(inst, &n, v.data());
    VkPhysicalDevice best = VK_NULL_HANDLE;
    int best_score = -1;
    for (uint32_t i = 0; i < n; i++) {
        const int fam = usable_queue_family(v[i], surface);
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(v[i], &props);
        if (surface != VK_NULL_HANDLE) {
            sysLogPrintf(LOG_NOTE, "VK: device %u: %s (Vulkan %u.%u.%u)%s", i, props.deviceName,
                         VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
                         VK_API_VERSION_PATCH(props.apiVersion), fam < 0 ? " - not usable" : "");
        }
        if (fam < 0) continue;
        int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                  : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
        if (score > best_score) {
            best_score = score;
            best = v[i];
            *qfam = (uint32_t)fam;
        }
    }
    return best;
}

void create_instance(bool with_window) {
    std::vector<const char*> exts;
    std::vector<const char*> layers;
    if (with_window) {
        SDL_Window* w = (SDL_Window*)gfx_sdl_get_sdl_window();
        unsigned int n = 0;
        if (!SDL_Vulkan_GetInstanceExtensions(w, &n, nullptr)) {
            sysFatalError("Vulkan: SDL_Vulkan_GetInstanceExtensions failed:\n%s", SDL_GetError());
        }
        exts.resize(n);
        SDL_Vulkan_GetInstanceExtensions(w, &n, exts.data());
    }
    VkInstanceCreateFlags flags = 0;
    if (has_instance_ext(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {   /* MoltenVK */
        exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    bool validate = false;
    if (with_window) {
        const char* e = getenv("GE_VKVALIDATE");
        const bool want = !(e && e[0] == '0');
        if (want && has_layer("VK_LAYER_KHRONOS_validation") && has_instance_ext(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            validate = true;
        }
        sysLogPrintf(LOG_NOTE, "VK: validation layer %s", validate ? "on (GE_VKVALIDATE=0 turns it off)"
                                                         : want ? "not installed (Vulkan SDK)" : "off (GE_VKVALIDATE=0)");
    }

    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "ge007";
    app.applicationVersion = 1;
    app.pEngineName = "fast3d";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_3;

    VkDebugUtilsMessengerCreateInfoEXT dbg = {};
    dbg.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dbg.pfnUserCallback = debug_cb;

    VkInstanceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pNext = validate ? &dbg : nullptr;   /* also covers vkCreateInstance / vkDestroyInstance */
    ci.flags = flags;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount = (uint32_t)layers.size();
    ci.ppEnabledLayerNames = layers.data();
    VkResult r = vkCreateInstance(&ci, nullptr, &s_instance);
    if (r != VK_SUCCESS && validate) {
        /* The layer's manifest was found but its DLL did not load (missing
         * dependency, broken build): run without validation rather than die. */
        sysLogPrintf(LOG_WARNING, "VK: instance with the validation layer failed (VkResult %d); retrying without it", (int)r);
        validate = false;
        layers.clear();
        exts.pop_back();   /* VK_EXT_debug_utils, pushed last */
        ci.pNext = nullptr;
        ci.enabledExtensionCount = (uint32_t)exts.size();
        ci.enabledLayerCount = 0;
        ci.ppEnabledLayerNames = nullptr;
        s_validation_errors = s_validation_warnings = 0;
        r = vkCreateInstance(&ci, nullptr, &s_instance);
    }
    VK_CHECK(r);
    volkLoadInstanceOnly(s_instance);
    if (validate) {
        VK_CHECK(vkCreateDebugUtilsMessengerEXT(s_instance, &dbg, nullptr, &s_messenger));
    }
}

void create_device(void) {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = s_qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    std::vector<const char*> exts = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME };
    if (has_device_ext(s_phys, "VK_KHR_portability_subset")) exts.push_back("VK_KHR_portability_subset");

    /* what the device offers */
    VkPhysicalDeviceDepthClipControlFeaturesEXT clip_avail = {};
    clip_avail.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT;
    VkPhysicalDeviceVulkan12Features f12_avail = {};
    f12_avail.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    const bool clip_ext = has_device_ext(s_phys, VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME);
    f12_avail.pNext = clip_ext ? &clip_avail : nullptr;
    VkPhysicalDeviceFeatures2 avail = {};
    avail.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    avail.pNext = &f12_avail;
    vkGetPhysicalDeviceFeatures2(s_phys, &avail);

    /* what is enabled */
    VkPhysicalDeviceDepthClipControlFeaturesEXT clip = {};
    clip.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT;
    clip.depthClipControl = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12 = {};
    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    f12.samplerMirrorClampToEdge = f12_avail.samplerMirrorClampToEdge;
    VkPhysicalDeviceVulkan13Features f13 = {};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    f13.shaderDemoteToHelperInvocation = VK_TRUE;   /* glslang's SPIR-V 1.6 `discard` (required in 1.3) */
    f13.pNext = &f12;
    s_clip_control = clip_ext && clip_avail.depthClipControl;
    if (s_clip_control) {
        exts.push_back(VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME);
        f12.pNext = &clip;
    }
    VkPhysicalDeviceFeatures2 f2 = {};
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f2.pNext = &f13;
    f2.features.samplerAnisotropy = avail.features.samplerAnisotropy;
    f2.features.depthClamp = avail.features.depthClamp;   /* GL runs with depth clamp */
    s_mirror_clamp = f12.samplerMirrorClampToEdge != 0;
    s_depth_clamp = avail.features.depthClamp != 0;

    VkDeviceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.pNext = &f2;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qci;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    VK_CHECK(vkCreateDevice(s_phys, &ci, nullptr, &s_dev));
    volkLoadDevice(s_dev);
    vkGetDeviceQueue(s_dev, s_qfam, 0, &s_queue);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(s_phys, &props);
    s_max_aniso = avail.features.samplerAnisotropy ? props.limits.maxSamplerAnisotropy : 1.0f;
    s_ubo_align = props.limits.minUniformBufferOffsetAlignment;
    if (s_ubo_align < 16) s_ubo_align = 16;
    s_max_tex = props.limits.maxImageDimension2D;
    s_sample_counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
    vkGetPhysicalDeviceMemoryProperties(s_phys, &s_memprops);
    sysLogPrintf(LOG_NOTE, "VK: depth clip control %s, mirror-clamp %s, depth clamp %s, anisotropy up to %.0fx",
                 s_clip_control ? "on (GL clip z)" : "off (0..1 clip z from the CPU)", s_mirror_clamp ? "on" : "off",
                 s_depth_clamp ? "on" : "off", s_max_aniso);
}

/* ---------------- memory ---------------- */

uint32_t find_memtype(uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < s_memprops.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (s_memprops.memoryTypes[i].propertyFlags & want) == want) return i;
    }
    return UINT32_MAX;
}

HostChunk make_host_chunk(VkDeviceSize size, VkBufferUsageFlags usage) {
    HostChunk c;
    VkBufferCreateInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(s_dev, &bi, nullptr, &c.buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(s_dev, c.buf, &req);
    const uint32_t mt = find_memtype(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == UINT32_MAX) sysFatalError("Vulkan: no host-visible coherent memory");
    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = mt;
    if (vkAllocateMemory(s_dev, &ai, nullptr, &c.mem) != VK_SUCCESS) {
        sysFatalError("Vulkan: could not allocate %u KiB of host memory", (unsigned)(size >> 10));
    }
    VK_CHECK(vkBindBufferMemory(s_dev, c.buf, c.mem, 0));
    void* p = nullptr;
    VK_CHECK(vkMapMemory(s_dev, c.mem, 0, VK_WHOLE_SIZE, 0, &p));
    c.cpu = (uint8_t*)p;
    return c;
}

void free_host_chunk(HostChunk& c) {
    if (c.buf) vkDestroyBuffer(s_dev, c.buf, nullptr);
    if (c.mem) vkFreeMemory(s_dev, c.mem, nullptr);
    c = HostChunk();
}

/* size bytes from the frame's host ring: CPU pointer, buffer and offset */
uint8_t* ring_alloc(uint32_t size, uint32_t align, VkBuffer* buf, VkDeviceSize* off) {
    DkRingAlloc a;
    s_ring.alloc(size, align, s_serial, &a);
    if (a.new_chunk) {
        s_ring_chunks.push_back(make_host_chunk(s_ring.chunk_size(a.chunk),
                                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
    }
    HostChunk& c = s_ring_chunks[a.chunk];
    *buf = c.buf;
    *off = a.offset;
    return c.cpu + a.offset;
}

bool create_image(Img* out, VkFormat fmt, uint32_t w, uint32_t h, uint32_t levels, VkImageUsageFlags usage,
                  VkSampleCountFlagBits samples, VkImageAspectFlags aspect) {
    Img im;
    VkImageCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = fmt;
    ci.extent = { w, h, 1 };
    ci.mipLevels = levels;
    ci.arrayLayers = 1;
    ci.samples = samples;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(s_dev, &ci, nullptr, &im.img) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(s_dev, im.img, &req);
    uint32_t mt = find_memtype(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt == UINT32_MAX) mt = find_memtype(req.memoryTypeBits, 0);
    if (mt == UINT32_MAX || req.size > 0xffffffffull || req.alignment > 0xffffffffull) {
        vkDestroyImage(s_dev, im.img, nullptr);
        return false;
    }
    ImgHeap& hp = s_img_heaps[mt];
    if (!hp.heap.alloc((uint32_t)req.size, (uint32_t)req.alignment, &im.blk)) {
        vkDestroyImage(s_dev, im.img, nullptr);
        return false;
    }
    if (hp.heap.grew()) {
        VkMemoryAllocateInfo ai = {};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = hp.heap.chunk_size(im.blk.chunk);
        ai.memoryTypeIndex = mt;
        VkDeviceMemory m = VK_NULL_HANDLE;
        if (vkAllocateMemory(s_dev, &ai, nullptr, &m) != VK_SUCCESS) {
            sysFatalError("Vulkan: could not allocate %u MiB of image memory", (unsigned)(ai.allocationSize >> 20));
        }
        hp.mem.push_back(m);
    }
    im.memtype = mt;
    VK_CHECK(vkBindImageMemory(s_dev, im.img, hp.mem[im.blk.chunk], im.blk.offset));
    VkImageViewCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = im.img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange.aspectMask = aspect;
    vi.subresourceRange.levelCount = levels;
    vi.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(s_dev, &vi, nullptr, &im.view));
    *out = im;
    return true;
}

void destroy_image_now(Img& i) {
    if (i.view) vkDestroyImageView(s_dev, i.view, nullptr);
    if (i.view_a1) vkDestroyImageView(s_dev, i.view_a1, nullptr);
    if (i.img) {
        vkDestroyImage(s_dev, i.img, nullptr);
        s_img_heaps[i.memtype].heap.free_after(i.blk, 0);
    }
    i = Img();
}

/* after every submission so far that may use it has completed */
void destroy_image_later(Img& i) {
    if (i.img) s_dead_imgs.push_back(DeadImg{ i, s_serial });
    i = Img();
}

void retire(void) {
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        const Frame& f = s_frames[i];
        if (f.serial > s_completed && vkGetFenceStatus(s_dev, f.fence) == VK_SUCCESS) s_completed = f.serial;
    }
    size_t w = 0;
    for (size_t i = 0; i < s_dead_imgs.size(); i++) {
        if (s_dead_imgs[i].serial <= s_completed) destroy_image_now(s_dead_imgs[i].i);
        else s_dead_imgs[w++] = s_dead_imgs[i];
    }
    s_dead_imgs.resize(w);
    w = 0;
    for (size_t i = 0; i < s_dead_pipes.size(); i++) {
        if (s_dead_pipes[i].second <= s_completed) vkDestroyPipeline(s_dev, s_dead_pipes[i].first, nullptr);
        else s_dead_pipes[w++] = s_dead_pipes[i];
    }
    s_dead_pipes.resize(w);
    w = 0;
    for (size_t i = 0; i < s_dead_meshes.size(); i++) {
        if (s_dead_meshes[i].second <= s_completed) free_host_chunk(s_dead_meshes[i].first);
        else s_dead_meshes[w++] = s_dead_meshes[i];
    }
    s_dead_meshes.resize(w);
    for (auto& kv : s_img_heaps) kv.second.heap.retire(s_completed);
    s_ring.retire(s_completed);
}

void image_barrier(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags aspect, uint32_t levels,
                   VkImageLayout from, VkImageLayout to,
                   VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                   VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 b = {};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = src_stage;
    b.srcAccessMask = src_access;
    b.dstStageMask = dst_stage;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.levelCount = levels;
    b.subresourceRange.layerCount = 1;
    VkDependencyInfo di = {};
    di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &di);
}

/* Move an image (one level) to `to`, ordered after everything before it.
 * Conservative masks: these happen a few times per frame (target switches,
 * copies), never per draw. Read-only to read-only needs no barrier. */
void layout_to(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags aspect, VkImageLayout& cur, VkImageLayout to) {
    if (cur == to && to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return;
    image_barrier(cmd, img, aspect, 1, cur, to, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    cur = to;
}

/* ---------------- frames ---------------- */

/* dynamic state to re-send (a new command buffer starts with none) */
bool s_vp_dirty = true, s_sc_dirty_rect = true, s_depth_dirty = true, s_desc_dirty = true, s_uni_dirty = true;
bool s_static_dyn_set = false;   /* cull mode, front face, depth bias values */
VkPipeline s_bound_pipe = VK_NULL_HANDLE;
bool s_rendering = false;
int s_render_fb = -1;

void begin_frame(void) {
    Frame& f = s_frames[s_frame_idx];
    VK_CHECK(vkWaitForFences(s_dev, 1, &f.fence, VK_TRUE, UINT64_MAX));
    if (f.serial > s_completed) s_completed = f.serial;   /* submissions complete in order */
    retire();
    VK_CHECK(vkResetFences(s_dev, 1, &f.fence));
    VK_CHECK(vkResetCommandPool(s_dev, f.pool, 0));
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(f.pre, &bi));
    VK_CHECK(vkBeginCommandBuffer(f.cmd, &bi));
    s_frame_open = true;
    s_acq = -1;
    s_acq_failed = false;
    s_sc_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    s_rendering = false;
    s_render_fb = -1;
    s_bound_pipe = VK_NULL_HANDLE;
    s_vp_dirty = s_sc_dirty_rect = s_depth_dirty = s_desc_dirty = s_uni_dirty = true;
    s_static_dyn_set = false;
}

void ensure_frame(void) {
    if (!s_frame_open) begin_frame();
}

void create_frames(void) {
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        Frame& f = s_frames[i];
        VkCommandPoolCreateInfo pci = {};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pci.queueFamilyIndex = s_qfam;
        VK_CHECK(vkCreateCommandPool(s_dev, &pci, nullptr, &f.pool));
        VkCommandBufferAllocateInfo ai = {};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 2;
        VkCommandBuffer cbs[2];
        VK_CHECK(vkAllocateCommandBuffers(s_dev, &ai, cbs));
        f.pre = cbs[0];
        f.cmd = cbs[1];
        VkFenceCreateInfo fci = {};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(s_dev, &fci, nullptr, &f.fence));
        VkSemaphoreCreateInfo si = {};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(s_dev, &si, nullptr, &f.acquired));
        f.serial = 0;
    }
}

/* ---------------- swapchain ---------------- */

VkFormat s_depth_fmt = VK_FORMAT_D32_SFLOAT;
VkImageAspectFlags s_depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
Img s_win_depth;
VkImageLayout s_win_depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;

void pick_depth_format(void) {
    /* 24-bit fixed point first: GL's default depth buffer (depth bias units match) */
    static const VkFormat cand[] = { VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D32_SFLOAT };
    for (VkFormat f : cand) {
        VkFormatProperties p;
        vkGetPhysicalDeviceFormatProperties(s_phys, f, &p);
        if (p.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            s_depth_fmt = f;
            break;
        }
    }
    s_depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT | (s_depth_fmt == VK_FORMAT_D24_UNORM_S8_UINT ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
}

void window_drawable_size(uint32_t* w, uint32_t* h) {
    int iw = 0, ih = 0;
    SDL_Window* win = (SDL_Window*)gfx_sdl_get_sdl_window();
    if (win) SDL_Vulkan_GetDrawableSize(win, &iw, &ih);
    *w = iw > 0 ? (uint32_t)iw : 0;
    *h = ih > 0 ? (uint32_t)ih : 0;
}

VkPresentModeKHR choose_present_mode(void) {
    if (s_swap_interval != 0) return VK_PRESENT_MODE_FIFO_KHR;   /* VSync; always supported */
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(s_phys, s_surface, &n, nullptr);
    std::vector<VkPresentModeKHR> v(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(s_phys, s_surface, &n, v.data());
    bool mailbox = false, immediate = false;
    for (auto m : v) {
        mailbox |= m == VK_PRESENT_MODE_MAILBOX_KHR;
        immediate |= m == VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    return immediate ? VK_PRESENT_MODE_IMMEDIATE_KHR : mailbox ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;
}

void destroy_swapchain_views(void) {
    for (auto v : s_sc_views) vkDestroyImageView(s_dev, v, nullptr);
    s_sc_views.clear();
    for (auto s : s_sc_render_done) vkDestroySemaphore(s_dev, s, nullptr);
    s_sc_render_done.clear();
    s_sc_images.clear();
}

/* Returns false when the window has no area (minimised): nothing to present. */
bool create_swapchain(void) {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(s_phys, s_surface, &caps));
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFFu) {   /* the surface takes the swapchain's size (Wayland) */
        uint32_t w, h;
        window_drawable_size(&w, &h);
        ext.width = w < caps.minImageExtent.width ? caps.minImageExtent.width
                  : w > caps.maxImageExtent.width ? caps.maxImageExtent.width : w;
        ext.height = h < caps.minImageExtent.height ? caps.minImageExtent.height
                   : h > caps.maxImageExtent.height ? caps.maxImageExtent.height : h;
    }
    if (ext.width == 0 || ext.height == 0) return false;

    {
        uint32_t n = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(s_phys, s_surface, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> v(n);
        vkGetPhysicalDeviceSurfaceFormatsKHR(s_phys, s_surface, &n, v.data());
        /* UNORM, like GL's default framebuffer (the shaders write display values) */
        VkSurfaceFormatKHR pick = v.empty() ? VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } : v[0];
        for (auto& f : v) {
            if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                pick = f;
                break;
            }
        }
        s_sc_format = pick.format;

        uint32_t count = caps.minImageCount + 1;
        if (caps.maxImageCount != 0 && count > caps.maxImageCount) count = caps.maxImageCount;
        s_sc_mode = choose_present_mode();

        VkSwapchainKHR old = s_swapchain;
        VkSwapchainCreateInfoKHR ci = {};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = s_surface;
        ci.minImageCount = count;
        ci.imageFormat = pick.format;
        ci.imageColorSpace = pick.colorSpace;
        ci.imageExtent = ext;
        ci.imageArrayLayers = 1;
        /* transfer src: GE_PCDUMP / screenshot readback; transfer dst: blits and clears */
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                        (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        s_sc_readable = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                                ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                : (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
        ci.presentMode = s_sc_mode;
        ci.clipped = VK_TRUE;
        ci.oldSwapchain = old;
        VK_CHECK(vkCreateSwapchainKHR(s_dev, &ci, nullptr, &s_swapchain));
        if (old != VK_NULL_HANDLE) vkDestroySwapchainKHR(s_dev, old, nullptr);
    }

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(s_dev, s_swapchain, &n, nullptr);
    s_sc_images.resize(n);
    vkGetSwapchainImagesKHR(s_dev, s_swapchain, &n, s_sc_images.data());
    s_sc_views.resize(n);
    s_sc_render_done.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        VkImageViewCreateInfo vi = {};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = s_sc_images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = s_sc_format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(s_dev, &vi, nullptr, &s_sc_views[i]));
        VkSemaphoreCreateInfo si = {};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(s_dev, &si, nullptr, &s_sc_render_done[i]));
    }
    /* the window's depth buffer, the swapchain's size */
    destroy_image_later(s_win_depth);
    if (!create_image(&s_win_depth, s_depth_fmt, ext.width, ext.height, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                      VK_SAMPLE_COUNT_1_BIT, s_depth_aspect)) {
        sysFatalError("Vulkan: could not create the %ux%u window depth buffer", ext.width, ext.height);
    }
    s_win_depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    s_sc_extent = ext;
    s_sc_dirty = false;
    static const char* const mode_names[] = { "IMMEDIATE", "MAILBOX", "FIFO", "FIFO_RELAXED" };
    sysLogPrintf(LOG_NOTE, "VK: swapchain %ux%u, %u images, format %d, %s, depth format %d", ext.width, ext.height, n,
                 (int)s_sc_format, (int)s_sc_mode < 4 ? mode_names[s_sc_mode] : "?", (int)s_depth_fmt);
    return true;
}

bool recreate_swapchain(void) {
    vkDeviceWaitIdle(s_dev);
    destroy_swapchain_views();
    return create_swapchain();
}

/* The window's image for this frame; false when there is none (minimised). */
bool ensure_acquired(void) {
    if (s_acq >= 0) return true;
    if (s_acq_failed) return false;
    uint32_t w, h;
    window_drawable_size(&w, &h);
    if (w == 0 || h == 0) {
        s_acq_failed = true;
        return false;
    }
    if (w != s_sc_extent.width || h != s_sc_extent.height) s_sc_dirty = true;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (s_sc_dirty || s_swapchain == VK_NULL_HANDLE) {
            if (!recreate_swapchain()) break;
        }
        uint32_t img = 0;
        const VkResult r = vkAcquireNextImageKHR(s_dev, s_swapchain, UINT64_MAX, s_frames[s_frame_idx].acquired,
                                                 VK_NULL_HANDLE, &img);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            s_sc_dirty = true;
            continue;
        }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) VK_CHECK(r);
        if (r == VK_SUBOPTIMAL_KHR) s_sc_dirty = true;   /* recreated at the next frame */
        s_acq = (int)img;
        s_sc_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        return true;
    }
    s_acq_failed = true;
    return false;
}

/* ---------------- shader pack ---------------- */

const DkPackEntry* s_pack = nullptr;
uint32_t s_pack_count = 0;
std::map<std::pair<uint64_t, uint32_t>, ShaderProgram*> s_progs;
std::map<std::pair<uint64_t, uint32_t>, bool> s_missed;
std::map<uint32_t, VkShaderModule> s_modules;   /* by blob offset in the pack */
VkShaderModule s_uber_vert = VK_NULL_HANDLE, s_uber_frag = VK_NULL_HANDLE;
VkShaderModule s_fs_vert = VK_NULL_HANDLE, s_fxaa_frag = VK_NULL_HANDLE, s_sky_frag = VK_NULL_HANDLE,
                s_lut_frag = VK_NULL_HANDLE;

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

/* stage 0 vertex, 1 fragment, 2 mesh vertex */
VkShaderModule pack_module(int entry, int stage) {
    if (entry < 0) return VK_NULL_HANDLE;
    const DkPackEntry& e = s_pack[entry];
    const uint32_t off = stage == 0 ? e.vert_off : stage == 1 ? e.frag_off : e.mesh_vert_off;
    const uint32_t len = stage == 0 ? e.vert_len : stage == 1 ? e.frag_len : e.mesh_vert_len;
    if (len == 0 || (len & 3) != 0) return VK_NULL_HANDLE;
    auto it = s_modules.find(off);
    if (it != s_modules.end()) return it->second;
    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = len;
    ci.pCode = (const uint32_t*)(ge007_spv_pack + off);
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(s_dev, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    s_modules[off] = m;
    return m;
}

void load_pack(void) {
    const uint8_t* p = ge007_spv_pack;
    if ((size_t)(ge007_spv_pack_end - p) < 16 || memcmp(p, DK_PACK_MAGIC, 8) != 0) {
        sysFatalError("Vulkan: SPIR-V shader pack missing or corrupt");
    }
    memcpy(&s_pack_count, p + 8, 4);
    s_pack = (const DkPackEntry*)(p + 16);
    const int uber = pack_find(0, DK_PACK_UBER);
    s_uber_vert = pack_module(uber, 0);
    s_uber_frag = pack_module(uber, 1);
    if (s_uber_vert == VK_NULL_HANDLE || s_uber_frag == VK_NULL_HANDLE) {
        sysFatalError("Vulkan: uber-shader missing from the SPIR-V shader pack");
    }
    s_fs_vert = pack_module(pack_find(0, DK_PACK_FULLSCREEN), 0);
    s_fxaa_frag = pack_module(pack_find(0, DK_PACK_FXAA), 1);
    s_sky_frag = pack_module(pack_find(0, DK_PACK_SKYPLUS), 1);
    s_lut_frag = pack_module(pack_find(0, DK_PACK_SKYLUT), 1);
    if (!s_fs_vert || !s_fxaa_frag || !s_sky_frag) {
        sysLogPrintf(LOG_WARNING, "VK: post-pass shaders missing from the pack (FXAA %d, GE+ sky %d)",
                     s_fs_vert && s_fxaa_frag, s_fs_vert && s_sky_frag);
    }
    int combiners = 0;
    for (uint32_t i = 0; i < s_pack_count; i++) combiners += (s_pack[i].id0 != 0 || s_pack[i].id1 >= DK_PACK_NAMED_MAX);
    sysLogPrintf(LOG_NOTE, "VK: shader pack: %d combiners + uber, %u KiB of SPIR-V", combiners,
                 (unsigned)((ge007_spv_pack_end - p) >> 10));
}

/* ---------------- pipelines ---------------- */

VkDescriptorSetLayout s_set_layout = VK_NULL_HANDLE;
VkDescriptorSetLayout s_sky_set_layout = VK_NULL_HANDLE;   /* the GE+ sky passes: five samplers (0..4), the Sky block at 5 */
VkPipelineLayout s_pipe_layout = VK_NULL_HANDLE, s_sky_pipe_layout = VK_NULL_HANDLE;
VkPipelineCache s_pipe_cache = VK_NULL_HANDLE;

/* The driver's pipeline cache lives in $S/ge007.vkcache between runs: the first
 * run pays for every pipeline, later ones read the compiled code back. The
 * driver validates the blob itself (header: vendor, device, cache UUID), so a
 * file from another GPU or driver is simply ignored; GE_NOSHADERWARM=1 skips it
 * as it does the GL shader list. */
std::string pipeline_cache_path(void) {
    return sysResolvePath("$S/ge007.vkcache");
}

void pipeline_cache_load(void) {
    std::vector<uint8_t> blob;
    if (getenv("GE_NOSHADERWARM") == NULL) {
        if (FILE* f = fopen(pipeline_cache_path().c_str(), "rb")) {
            fseek(f, 0, SEEK_END);
            const long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (n > 0 && n < (256l << 20)) {
                blob.resize((size_t)n);
                if (fread(blob.data(), 1, blob.size(), f) != blob.size()) blob.clear();
            }
            fclose(f);
        }
    }
    VkPipelineCacheCreateInfo pc = {};
    pc.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    pc.initialDataSize = blob.size();
    pc.pInitialData = blob.empty() ? nullptr : blob.data();
    VK_CHECK(vkCreatePipelineCache(s_dev, &pc, nullptr, &s_pipe_cache));
    sysLogPrintf(LOG_NOTE, "VK: pipeline cache, %u KiB read from disk", (unsigned)(blob.size() >> 10));
}

/* Write the cache back (to a temporary file first, so a crash never leaves half a cache). */
void pipeline_cache_save(void) {
    if (s_pipe_cache == VK_NULL_HANDLE || getenv("GE_NOSHADERWARM") != NULL) return;
    size_t n = 0;
    if (vkGetPipelineCacheData(s_dev, s_pipe_cache, &n, nullptr) != VK_SUCCESS || n == 0) return;
    std::vector<uint8_t> blob(n);
    if (vkGetPipelineCacheData(s_dev, s_pipe_cache, &n, blob.data()) != VK_SUCCESS) return;
    const std::string path = pipeline_cache_path();
    const std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (f == NULL) return;
    const bool ok = fwrite(blob.data(), 1, n, f) == n;
    fclose(f);
    if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
#ifdef _WIN32
        remove(path.c_str());   /* rename does not overwrite there */
        if (!ok || rename(tmp.c_str(), path.c_str()) != 0) remove(tmp.c_str());
#else
        remove(tmp.c_str());
#endif
        return;
    }
    sysLogPrintf(LOG_NOTE, "VK: pipeline cache, %u KiB written", (unsigned)(n >> 10));
}

/* vkCreateGraphicsPipelines through the cache, timed for the park log */
VkPipeline build_pipeline(const VkGraphicsPipelineCreateInfo& ci) {
    VkPipeline pipe = VK_NULL_HANDLE;
    const auto t0 = std::chrono::steady_clock::now();
    VK_CHECK(vkCreateGraphicsPipelines(s_dev, s_pipe_cache, 1, &ci, nullptr, &pipe));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    s_st_pipe_ms += ms;
    if (ms > s_st_pipe_ms_max) s_st_pipe_ms_max = ms;
    s_st_pipelines++;
    return pipe;
}

enum { BLEND_OFF = 0, BLEND_ALPHA = 1, BLEND_MODULATE = 2 };

struct PipeKey {
    const ShaderProgram* prg;
    uint8_t blend, cwrite, a2c, samples, mesh;   /* mesh: the program's GPU-transform vertex shader */
    VkFormat color, depth;
    bool operator<(const PipeKey& o) const {
        return std::tie(prg, blend, cwrite, a2c, samples, mesh, color, depth) <
               std::tie(o.prg, o.blend, o.cwrite, o.a2c, o.samples, o.mesh, o.color, o.depth);
    }
};
std::map<PipeKey, VkPipeline> s_pipes;

void create_layouts(void) {
    VkDescriptorSetLayoutBinding b[5] = {};
    const uint32_t nums[5] = { 0, 1, 4, 5, 6 };
    for (int i = 0; i < 5; i++) {
        b[i].binding = nums[i];
        b[i].descriptorCount = 1;
        b[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b[i].stageFlags = nums[i] == 6 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    ci.bindingCount = 5;
    ci.pBindings = b;
    VK_CHECK(vkCreateDescriptorSetLayout(s_dev, &ci, nullptr, &s_set_layout));
    VkPipelineLayoutCreateInfo pl = {};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &s_set_layout;
    VK_CHECK(vkCreatePipelineLayout(s_dev, &pl, nullptr, &s_pipe_layout));
    /* the GE+ sky and sea read five textures (cloud, wave, sky LUT, half-size sea, far-sea panorama), so binding 4 is a sampler
     * there and cannot be the draws' Draw block: a layout of their own, the Sky block (uniform binding 0 in the GLSL) at 5 */
    VkDescriptorSetLayoutBinding sb[6] = {};
    for (int i = 0; i < 6; i++) {
        sb[i].binding = (uint32_t)i;
        sb[i].descriptorCount = 1;
        sb[i].descriptorType = i < 5 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sb[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    ci.bindingCount = 6;
    ci.pBindings = sb;
    VK_CHECK(vkCreateDescriptorSetLayout(s_dev, &ci, nullptr, &s_sky_set_layout));
    pl.pSetLayouts = &s_sky_set_layout;
    VK_CHECK(vkCreatePipelineLayout(s_dev, &pl, nullptr, &s_sky_pipe_layout));
    pipeline_cache_load();
}

VkPipeline create_pipeline(const PipeKey& k) {
    const ShaderProgram* p = k.prg;
    VkPipelineShaderStageCreateInfo st[2] = {};
    st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = k.mesh ? p->mesh_vert : p->vert;
    st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = p->frag;
    st[1].pName = "main";

    static const VkFormat fmts[5] = { VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT,
                                      VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT };
    VkVertexInputBindingDescription vb[2] = {};
    vb[0].binding = 0;
    vb[0].stride = p->stride;
    vb[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vb[1].binding = 1;
    vb[1].stride = 0;   /* constant zero: what the uber-shader reads for attributes the program lacks */
    vb[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription va[DK_ATTR_COUNT] = {};
    uint32_t na = 0;
    bool have[DK_ATTR_COUNT] = {};
    for (int i = 0; i < p->layout.count; i++) {
        va[na].location = p->layout.loc[i];
        va[na].binding = 0;
        va[na].format = fmts[p->layout.size[i]];
        va[na].offset = p->layout.offset[i] * 4u;
        have[p->layout.loc[i]] = true;
        na++;
    }
    if (p->uber) {
        for (int loc = 0; loc < DK_ATTR_COUNT; loc++) {
            if (have[loc]) continue;
            va[na].location = (uint32_t)loc;
            va[na].binding = 1;
            va[na].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            va[na].offset = 0;
            na++;
        }
    }
    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = p->uber ? 2 : 1;
    vi.pVertexBindingDescriptions = vb;
    vi.vertexAttributeDescriptionCount = na;
    vi.pVertexAttributeDescriptions = va;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportDepthClipControlCreateInfoEXT clip = {};
    clip.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT;
    clip.negativeOneToOne = VK_TRUE;
    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.pNext = s_clip_control ? &clip : nullptr;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.depthClampEnable = s_depth_clamp ? VK_TRUE : VK_FALSE;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = (VkSampleCountFlagBits)k.samples;
    ms.alphaToCoverageEnable = k.a2c ? VK_TRUE : VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;   /* dynamic */

    VkPipelineColorBlendAttachmentState ba = {};
    ba.blendEnable = k.blend != BLEND_OFF ? VK_TRUE : VK_FALSE;
    if (k.blend == BLEND_MODULATE) {
        ba.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        ba.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
        ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    } else {
        ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    ba.colorBlendOp = ba.alphaBlendOp = VK_BLEND_OP_ADD;
    ba.colorWriteMask = k.cwrite ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                    VK_COLOR_COMPONENT_A_BIT)
                                 : 0;
    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &ba;

    static const VkDynamicState dyn[] = {
        VK_DYNAMIC_STATE_VIEWPORT,           VK_DYNAMIC_STATE_SCISSOR,           VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,  VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_BIAS,         VK_DYNAMIC_STATE_CULL_MODE,         VK_DYNAMIC_STATE_FRONT_FACE,
    };
    VkPipelineDynamicStateCreateInfo dy = {};
    dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dy.dynamicStateCount = (uint32_t)(sizeof dyn / sizeof dyn[0]);
    dy.pDynamicStates = dyn;

    VkPipelineRenderingCreateInfo ri = {};
    ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &k.color;
    ri.depthAttachmentFormat = k.depth;

    VkGraphicsPipelineCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.pNext = &ri;
    ci.stageCount = 2;
    ci.pStages = st;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = s_pipe_layout;
    return build_pipeline(ci);
}

VkPipeline get_pipeline(const PipeKey& k) {
    auto it = s_pipes.find(k);
    if (it != s_pipes.end()) return it->second;
    VkPipeline p = create_pipeline(k);
    s_pipes[k] = p;
    return p;
}

/* Post passes (FXAA, GE+ sky): the fullscreen triangle, no vertex input, no
 * blending, every channel written; same layout and dynamic state as the game's
 * pipelines. Not dropped by clear_shaders (they name no program). */
typedef std::tuple<VkShaderModule, VkFormat, VkFormat, uint8_t, bool> FsKey;
std::map<FsKey, VkPipeline> s_fs_pipes;

/* sky: the GE+ sky passes' pipeline layout (five samplers) instead of the draws' */
VkPipeline get_fs_pipeline(VkShaderModule frag, VkFormat color, VkFormat depth, uint8_t samples, bool sky = false) {
    const FsKey key(frag, color, depth, samples, sky);
    auto it = s_fs_pipes.find(key);
    if (it != s_fs_pipes.end()) return it->second;
    VkPipelineShaderStageCreateInfo st[2] = {};
    st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = s_fs_vert;
    st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = frag;
    st[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportDepthClipControlCreateInfoEXT clip = {};
    clip.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT;
    clip.negativeOneToOne = VK_TRUE;
    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.pNext = s_clip_control ? &clip : nullptr;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.depthClampEnable = s_depth_clamp ? VK_TRUE : VK_FALSE;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = (VkSampleCountFlagBits)samples;
    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState ba = {};
    ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                        VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &ba;
    static const VkDynamicState dyn[] = {
        VK_DYNAMIC_STATE_VIEWPORT,           VK_DYNAMIC_STATE_SCISSOR,           VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,  VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_BIAS,         VK_DYNAMIC_STATE_CULL_MODE,         VK_DYNAMIC_STATE_FRONT_FACE,
    };
    VkPipelineDynamicStateCreateInfo dy = {};
    dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dy.dynamicStateCount = (uint32_t)(sizeof dyn / sizeof dyn[0]);
    dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri = {};
    ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &color;
    ri.depthAttachmentFormat = depth;
    VkGraphicsPipelineCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.pNext = &ri;
    ci.stageCount = 2;
    ci.pStages = st;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = sky ? s_sky_pipe_layout : s_pipe_layout;
    VkPipeline pipe = build_pipeline(ci);
    s_fs_pipes[key] = pipe;
    return pipe;
}

/* ---------------- textures, samplers ---------------- */

struct SmpKey {
    uint8_t min, mag, mip, ws, wt, aniso;   /* VkFilter, VkFilter, 0 none / 1 nearest / 2 linear, VkSamplerAddressMode x2 */
    bool operator<(const SmpKey& o) const { return memcmp(this, &o, sizeof(*this)) < 0; }
};
enum { MIP_NONE = 0, MIP_NEAREST = 1, MIP_LINEAR = 2 };
std::map<SmpKey, VkSampler> s_samplers;

VkSampler get_sampler(const SmpKey& k) {
    auto it = s_samplers.find(k);
    if (it != s_samplers.end()) return it->second;
    VkSamplerCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter = (VkFilter)k.mag;
    ci.minFilter = (VkFilter)k.min;
    ci.mipmapMode = k.mip == MIP_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU = (VkSamplerAddressMode)k.ws;
    ci.addressModeV = (VkSamplerAddressMode)k.wt;
    ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    ci.minLod = 0.0f;
    ci.maxLod = k.mip == MIP_NONE ? 0.25f : VK_LOD_CLAMP_NONE;   /* 0.25: GL's non-mipmapped minification */
    ci.anisotropyEnable = k.aniso > 1 ? VK_TRUE : VK_FALSE;
    ci.maxAnisotropy = (float)k.aniso;
    VkSampler s = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(s_dev, &ci, nullptr, &s));
    s_samplers[k] = s;
    return s;
}

/* GL's defaults for a fresh texture object: NEAREST_MIPMAP_LINEAR / LINEAR / REPEAT */
SmpKey default_tex_sampler(void) {
    return SmpKey{ VK_FILTER_NEAREST, VK_FILTER_LINEAR, MIP_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                   VK_SAMPLER_ADDRESS_MODE_REPEAT, 1 };
}

struct Tex {
    bool alive = false;
    bool has_img = false;
    Img img;
    uint32_t w = 0, h = 0, levels = 0;
    SmpKey smp;
};
std::vector<Tex> s_tex(1);              /* id 0 = GL's "no texture" */
std::vector<uint32_t> s_free_ids;
Img s_dummy;                            /* 1x1 opaque black (GL's unbound texture) */

struct Unit { uint8_t kind; uint32_t id; };   /* kind: 0 none, 1 texture, 2 framebuffer */
Unit s_unit[2] = {};
int s_active_unit = 0;
bool s_linear_flag[2] = { false, false };

enum FilteringMode s_filter = FILTER_LINEAR;
enum MipmapFilteringMode s_mipfilter = MIPMAP_LINEAR;
int s_aniso = 4;

/* the 2x2 box mip chain of gfx_opengl_upload_texture (gfx_deko3d.cpp build_mips) */
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

/* A fresh sampled RGBA8 image holding data (levels at offs), copied in by the
 * frame's upload command buffer. */
bool upload_image(Img* img, const uint8_t* data, const std::vector<uint32_t>& offs, uint32_t width, uint32_t height,
                  uint32_t levels, size_t total) {
    ensure_frame();
    if (!create_image(img, VK_FORMAT_R8G8B8A8_UNORM, width, height, levels,
                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SAMPLE_COUNT_1_BIT,
                      VK_IMAGE_ASPECT_COLOR_BIT)) {
        return false;
    }
    VkBuffer buf;
    VkDeviceSize base;
    uint8_t* stage = ring_alloc((uint32_t)total, 16, &buf, &base);
    memcpy(stage, data, total);
    VkCommandBuffer c = pre_cmd();
    image_barrier(c, img->img, VK_IMAGE_ASPECT_COLOR_BIT, levels, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy regs[16];
    uint32_t w = width, h = height;
    for (uint32_t l = 0; l < levels && l < 16; l++) {
        VkBufferImageCopy& r = regs[l];
        memset(&r, 0, sizeof r);
        r.bufferOffset = base + offs[l];
        r.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        r.imageSubresource.mipLevel = l;
        r.imageSubresource.layerCount = 1;
        r.imageExtent = { w, h, 1 };
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    vkCmdCopyBufferToImage(c, buf, img->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, levels < 16 ? levels : 16, regs);
    image_barrier(c, img->img, VK_IMAGE_ASPECT_COLOR_BIT, levels, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    s_st_uploads++;
    return true;
}

VkSamplerAddressMode cm_to_vk(uint32_t val) {
    switch (val) {
        case G_TX_NOMIRROR | G_TX_CLAMP: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case G_TX_MIRROR | G_TX_WRAP: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case G_TX_MIRROR | G_TX_CLAMP:
            /* GL without ARB_texture_mirror_clamp_to_edge falls back the same way */
            return s_mirror_clamp ? VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

/* gfx_opengl_sampler_key (gfx_deko3d.cpp sampler_key) */
SmpKey sampler_key(bool linear_filter, uint32_t cms, uint32_t cmt, bool mipmaps) {
    static const uint8_t mins[4][3][2] = {
        { { VK_FILTER_NEAREST, MIP_NONE }, { VK_FILTER_NEAREST, MIP_NEAREST }, { VK_FILTER_NEAREST, MIP_LINEAR } },
        { { VK_FILTER_LINEAR, MIP_NONE }, { VK_FILTER_LINEAR, MIP_NEAREST }, { VK_FILTER_LINEAR, MIP_LINEAR } },
        { { VK_FILTER_NEAREST, MIP_NONE }, { VK_FILTER_LINEAR, MIP_NEAREST }, { VK_FILTER_LINEAR, MIP_LINEAR } },
        { { VK_FILTER_LINEAR, MIP_NONE }, { VK_FILTER_LINEAR, MIP_LINEAR }, { VK_FILTER_LINEAR, MIP_LINEAR } },
    };
    if (s_filter == FILTER_THREE_POINT || s_filter == FILTER_TRILINEAR) mipmaps = true;
    mipmaps = mipmaps && s_mipfilter != MIPMAP_DISABLED;
    const int mip_idx = mipmaps ? (int)s_mipfilter : 0;
    SmpKey k;
    if (linear_filter) {
        k.min = mins[s_filter][mip_idx][0];
        k.mip = mins[s_filter][mip_idx][1];
    } else {
        k.min = VK_FILTER_NEAREST;
        k.mip = MIP_NONE;
    }
    k.mag = (linear_filter && (s_filter == FILTER_LINEAR || s_filter == FILTER_TRILINEAR)) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    int aniso = mipmaps ? s_aniso : 1;
    if (aniso > (int)s_max_aniso) aniso = (int)s_max_aniso;
    k.aniso = (uint8_t)(aniso < 1 ? 1 : aniso);
    k.ws = (uint8_t)cm_to_vk(cms);
    k.wt = (uint8_t)cm_to_vk(cmt);
    return k;
}

/* ---------------- framebuffers ---------------- */

/* fb 0 is the window (swapchain image + s_win_depth). An off-screen fb has a
 * single-sample colour image (sampled, copied, resolved into), plus a
 * multisample colour image it renders into when MSAA is on, plus depth with
 * the rendering's sample count (gfx_deko3d.cpp's layout). */
struct Fb {
    uint32_t w = 1, h = 1, msaa = 1;
    bool has_depth = false, invert_y = false;
    bool created = false;
    Img color, color_ms, depth;
    VkImageLayout l_color = VK_IMAGE_LAYOUT_UNDEFINED, l_ms = VK_IMAGE_LAYOUT_UNDEFINED,
                  l_depth = VK_IMAGE_LAYOUT_UNDEFINED;
    bool has_ms = false, has_depth_img = false;
    uint32_t samples = 1;
    SmpKey smp;   /* the colour texture's sampling state (GL: LINEAR, REPEAT) */
};
std::vector<Fb> s_fbs;

bool is_window(int fb) { return fb == 0; }
uint32_t fb_height(int fb) { return is_window(fb) ? s_sc_extent.height : s_fbs[fb].h; }
uint32_t fb_width(int fb) { return is_window(fb) ? s_sc_extent.width : s_fbs[fb].w; }

/* the highest supported sample count <= msaa */
uint32_t pick_samples(uint32_t msaa) {
    uint32_t s = 1;
    for (uint32_t c = 2; c <= 64 && c <= msaa; c <<= 1) {
        if (s_sample_counts & c) s = c;
    }
    return s;
}

void destroy_fb_images(Fb& f) {
    destroy_image_later(f.color);
    destroy_image_later(f.color_ms);
    destroy_image_later(f.depth);
    f.has_ms = f.has_depth_img = false;
    f.created = false;
}

void build_fb_images(Fb& f) {
    f.samples = pick_samples(f.msaa);
    const VkSampleCountFlagBits ms = (VkSampleCountFlagBits)f.samples;
    bool ok = create_image(&f.color, VK_FORMAT_R8G8B8A8_UNORM, f.w, f.h, 1,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                           VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    if (ok) {   /* GL's framebuffer textures are RGB8: alpha reads 1 */
        VkImageViewCreateInfo vi = {};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = f.color.img;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.components.a = VK_COMPONENT_SWIZZLE_ONE;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(s_dev, &vi, nullptr, &f.color.view_a1));
    }
    if (ok && f.samples > 1) {
        ok = create_image(&f.color_ms, VK_FORMAT_R8G8B8A8_UNORM, f.w, f.h, 1,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, ms,
                          VK_IMAGE_ASPECT_COLOR_BIT);
        f.has_ms = ok;
    }
    if (ok && f.has_depth) {
        ok = create_image(&f.depth, s_depth_fmt, f.w, f.h, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, ms, s_depth_aspect);
        f.has_depth_img = ok;
    }
    f.l_color = f.l_ms = f.l_depth = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!ok) {
        sysLogPrintf(LOG_ERROR, "VK: framebuffer %ux%u x%u: could not create its images", f.w, f.h, f.msaa);
        destroy_fb_images(f);
        return;
    }
    f.created = true;
}

/* ---------------- render state (GL's, as last set) ---------------- */

int s_cur_fb = 0;
float s_noise_scale = 1.0f;
uint32_t s_frame_count = 0;

int s_vp[4] = { 0, 0, 1, 1 }, s_sc[4] = { 0, 0, 1, 1 };
float s_znear = 0.0f, s_zfar = 1.0f;

bool s_depth_test = false, s_depth_mask = true, s_decal = false;
VkCompareOp s_depth_func = VK_COMPARE_OP_LESS_OR_EQUAL;
bool s_blend = false, s_modulate = false;

ShaderProgram* s_prg = nullptr;
struct DkDrawUniforms s_uni = {};
VkBuffer s_ubo_buf = VK_NULL_HANDLE;
VkDeviceSize s_ubo_off = 0;
bool s_mesh_mode = false;   /* the draw being prepared is a mesh draw (mesh pipeline, Mesh uniforms) */
VkBuffer s_mesh_ubo_buf = VK_NULL_HANDLE;
VkDeviceSize s_mesh_ubo_off = 0;

/* streaming (stream_begin) */
float* s_stream_ptr = nullptr;
VkBuffer s_stream_buf = VK_NULL_HANDLE;
VkDeviceSize s_stream_off = 0;

void end_rendering(void) {
    if (s_rendering) {
        vkCmdEndRendering(main_cmd());
        s_rendering = false;
        s_render_fb = -1;
    }
}

/* Rendering into the current target; false when it cannot be reached. */
bool begin_rendering(void) {
    if (s_rendering && s_render_fb == s_cur_fb) return true;
    ensure_frame();
    end_rendering();
    VkCommandBuffer c = main_cmd();
    VkRenderingAttachmentInfo ca = {};
    ca.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo da = {};
    da.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    da.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri = {};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    if (is_window(s_cur_fb)) {
        if (!ensure_acquired()) return false;
        layout_to(c, s_sc_images[s_acq], VK_IMAGE_ASPECT_COLOR_BIT, s_sc_layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        layout_to(c, s_win_depth.img, s_depth_aspect, s_win_depth_layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        ca.imageView = s_sc_views[s_acq];
        da.imageView = s_win_depth.view;
        ri.pDepthAttachment = &da;
        ri.renderArea.extent = s_sc_extent;
    } else {
        Fb& f = s_fbs[s_cur_fb];
        if (!f.created) return false;
        if (f.has_ms) {
            layout_to(c, f.color_ms.img, VK_IMAGE_ASPECT_COLOR_BIT, f.l_ms, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            ca.imageView = f.color_ms.view;
        } else {
            layout_to(c, f.color.img, VK_IMAGE_ASPECT_COLOR_BIT, f.l_color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            ca.imageView = f.color.view;
        }
        if (f.has_depth && f.has_depth_img) {
            layout_to(c, f.depth.img, s_depth_aspect, f.l_depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            da.imageView = f.depth.view;
            ri.pDepthAttachment = &da;
        }
        ri.renderArea.extent = { f.w, f.h };
    }
    vkCmdBeginRendering(c, &ri);
    s_rendering = true;
    s_render_fb = s_cur_fb;
    s_vp_dirty = s_sc_dirty_rect = s_depth_dirty = true;
    return true;
}

bool target_has_depth(void) {
    return is_window(s_cur_fb) || (s_fbs[s_cur_fb].has_depth && s_fbs[s_cur_fb].has_depth_img);
}
VkFormat target_color_format(void) { return is_window(s_cur_fb) ? s_sc_format : VK_FORMAT_R8G8B8A8_UNORM; }
VkFormat target_depth_format(void) { return s_depth_fmt; }
uint8_t target_samples(void) {
    return (uint8_t)(is_window(s_cur_fb) || !s_fbs[s_cur_fb].has_ms ? 1 : s_fbs[s_cur_fb].samples);
}

/* A framebuffer a unit samples: valid, built, and not the target being drawn. */
Fb* sampled_fb(int u) {
    const Unit& un = s_unit[u];
    if (un.kind != 2 || un.id == 0 || (int)un.id >= (int)s_fbs.size() || (int)un.id == s_cur_fb) return nullptr;
    Fb& f = s_fbs[un.id];
    return f.created ? &f : nullptr;
}

void set_scissor_rect(int x, int y, int w, int h) {
    const int H = (int)fb_height(s_cur_fb), W = (int)fb_width(s_cur_fb);
    y = H - y - h;   /* GL bottom-up -> top-down */
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > W ? W : x + w, y1 = y + h > H ? H : y + h;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    VkRect2D r = { { x0, y0 }, { (uint32_t)(x1 - x0), (uint32_t)(y1 - y0) } };
    vkCmdSetScissor(main_cmd(), 0, 1, &r);
}

void apply_dynamic(void) {
    VkCommandBuffer c = main_cmd();
    if (!s_static_dyn_set) {
        vkCmdSetCullMode(c, VK_CULL_MODE_NONE);   /* Fast3D culls on the CPU */
        vkCmdSetFrontFace(c, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        vkCmdSetDepthBias(c, -2.0f, 0.0f, -2.0f);   /* glPolygonOffset(-2, -2) for decals */
        s_static_dyn_set = true;
    }
    if (s_vp_dirty) {
        const int H = (int)fb_height(s_cur_fb);
        VkViewport v;
        v.x = (float)s_vp[0];
        v.width = (float)(s_vp[2] > 0 ? s_vp[2] : 1);
        v.y = (float)(H - s_vp[1]);                      /* the GL rect's bottom edge, top-down */
        v.height = -(float)(s_vp[3] != 0 ? s_vp[3] : 1); /* negative: NDC +y up, as GL */
        v.minDepth = s_znear;
        v.maxDepth = s_zfar;
        vkCmdSetViewport(c, 0, 1, &v);
        s_vp_dirty = false;
    }
    if (s_sc_dirty_rect) {
        set_scissor_rect(s_sc[0], s_sc[1], s_sc[2], s_sc[3]);
        s_sc_dirty_rect = false;
    }
    if (s_depth_dirty) {
        const bool test = s_depth_test && target_has_depth();
        vkCmdSetDepthTestEnable(c, test);
        vkCmdSetDepthWriteEnable(c, test && s_depth_mask);   /* GL writes no depth with the test off */
        vkCmdSetDepthCompareOp(c, s_depth_func);
        vkCmdSetDepthBiasEnable(c, s_decal);
        s_depth_dirty = false;
    }
}

/* Draw + Comb into a fresh uniform allocation */
void update_uniforms(void) {
    uint8_t* p = ring_alloc((uint32_t)(UBO_COMB_OFF + sizeof(DkCombinerParams)), (uint32_t)s_ubo_align, &s_ubo_buf, &s_ubo_off);
    memcpy(p, &s_uni, sizeof s_uni);
    struct DkCombinerParams cp;
    if (s_prg) cp = s_prg->params;
    else memset(&cp, 0, sizeof cp);
    memcpy(p + UBO_COMB_OFF, &cp, sizeof cp);
    s_uni_dirty = false;
    s_desc_dirty = true;
}

void unit_image(int u, VkImageView* view, VkSampler* smp) {
    const Unit& un = s_unit[u];
    if (un.kind == 1 && un.id < s_tex.size() && s_tex[un.id].alive && s_tex[un.id].has_img) {
        *view = s_tex[un.id].img.view;
        *smp = get_sampler(s_tex[un.id].smp);
        return;
    }
    if (Fb* f = sampled_fb(u)) {
        *view = f->color.view_a1;
        *smp = get_sampler(f->smp);
        return;
    }
    *view = s_dummy.view;   /* unbound */
    *smp = get_sampler(default_tex_sampler());
}

void push_descriptors(void) {
    VkDescriptorImageInfo ii[2];
    for (int u = 0; u < 2; u++) {
        unit_image(u, &ii[u].imageView, &ii[u].sampler);
        ii[u].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    VkDescriptorBufferInfo bi[2] = {
        { s_ubo_buf, s_ubo_off, sizeof(DkDrawUniforms) },
        { s_ubo_buf, s_ubo_off + UBO_COMB_OFF, sizeof(DkCombinerParams) },
    };
    VkWriteDescriptorSet w[5] = {};
    for (int i = 0; i < 5; i++) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].descriptorCount = 1;
    }
    VkDescriptorBufferInfo mi = { s_mesh_ubo_buf, s_mesh_ubo_off, sizeof(DkMeshUniforms) };
    w[0].dstBinding = 0;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[0].pImageInfo = &ii[0];
    w[1].dstBinding = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[1].pImageInfo = &ii[1];
    w[2].dstBinding = 4;
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[2].pBufferInfo = &bi[0];
    w[3].dstBinding = 5;
    w[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[3].pBufferInfo = &bi[1];
    w[4].dstBinding = 6;
    w[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[4].pBufferInfo = &mi;
    vkCmdPushDescriptorSetKHR(main_cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, s_pipe_layout, 0, s_mesh_mode ? 5 : 4, w);
    s_desc_dirty = false;
}

PipeKey current_key(bool cwrite) {
    PipeKey k;
    k.prg = s_prg;
    k.blend = s_blend ? (s_modulate ? BLEND_MODULATE : BLEND_ALPHA) : BLEND_OFF;
    k.cwrite = cwrite ? 1 : 0;
    k.samples = target_samples();
    k.mesh = s_mesh_mode ? 1 : 0;
    k.a2c = (s_prg->params.flags & DK_CF_A2C) != 0 && k.samples > 1;
    k.color = target_color_format();
    k.depth = target_has_depth() ? target_depth_format() : VK_FORMAT_UNDEFINED;
    return k;
}

void bind_pipeline(VkPipeline p) {
    if (p != s_bound_pipe) {
        vkCmdBindPipeline(main_cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, p);
        s_bound_pipe = p;
    }
}

bool prepare_draw(void) {
    if (!s_up || s_prg == nullptr) return false;
    ensure_frame();
    for (int u = 0; u < 2; u++) {   /* a sampled framebuffer is made readable outside the pass */
        Fb* f = sampled_fb(u);
        if (f && f->l_color != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            end_rendering();
            layout_to(main_cmd(), f->color.img, VK_IMAGE_ASPECT_COLOR_BIT, f->l_color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }
    {   /* a rendered image is stored top-down, GL's bottom-up: the shader mirrors t */
        const int32_t f0 = sampled_fb(0) != nullptr, f1 = sampled_fb(1) != nullptr;
        if (f0 != s_uni.flip_v0 || f1 != s_uni.flip_v1) {
            s_uni.flip_v0 = f0;
            s_uni.flip_v1 = f1;
            s_uni_dirty = true;
        }
    }
    if (!begin_rendering()) return false;
    bind_pipeline(get_pipeline(current_key(true)));
    if (s_uni_dirty) update_uniforms();
    if (s_desc_dirty) push_descriptors();
    apply_dynamic();
    return true;
}

/* D987: after the blended draw of a DK_CF_ZCUT program, the same vertices
 * again with colour writes off and depth writes on; the shader (Draw.zpass)
 * discards every fragment whose final alpha is below 0.98, so only the
 * opaque part writes depth. Same depth test as the blended pass. */
void zcut_pass(uint32_t nverts) {
    if ((s_prg->params.flags & DK_CF_ZCUT) == 0 || !s_depth_test || !target_has_depth()) return;
    VkCommandBuffer c = main_cmd();
    bind_pipeline(get_pipeline(current_key(false)));
    vkCmdSetDepthWriteEnable(c, VK_TRUE);
    s_uni.zpass = 1;
    update_uniforms();
    push_descriptors();
    vkCmdDraw(c, nverts, 1, 0, 0);
    s_uni.zpass = 0;
    s_uni_dirty = true;     /* the next draw sends zpass = 0 */
    s_depth_dirty = true;   /* and GL's depth mask again */
    s_st_draws++;
}

/* ---------------- GfxRenderingAPI ---------------- */

const char* vk_get_name(void) { return "Vulkan"; }
int vk_get_max_texture_size(void) { return (int)s_max_tex; }
struct GfxClipParameters vk_get_clip_parameters(void) {
    return { !s_clip_control, s_cur_fb < (int)s_fbs.size() ? s_fbs[s_cur_fb].invert_y : false };
}

void vk_unload_shader(struct ShaderProgram*) {}

void vk_load_shader_with(struct ShaderProgram* p, uint32_t fc, float noise, bool tpf0, bool tpf1) {
    if (p == nullptr) return;
    s_prg = p;
    s_uni.frame_count = (int32_t)fc;
    s_uni.noise_scale = noise;
    s_uni.fog_pixel = gfx_fog_vertex ? 0 : 1;   /* D982: follows a live change */
    /* GL compiles the three-point code only while that mode is on */
    s_uni.three_point0 = (tpf0 && s_filter == FILTER_THREE_POINT) ? 1 : 0;
    s_uni.three_point1 = (tpf1 && s_filter == FILTER_THREE_POINT) ? 1 : 0;
    s_uni_dirty = true;
}

void vk_load_shader(struct ShaderProgram* p) {
    vk_load_shader_with(p, s_frame_count, s_noise_scale, s_linear_flag[0], s_linear_flag[1]);
}

struct ShaderProgram* vk_create_and_load_new_shader(uint64_t id0, uint32_t id1) {
    struct CCFeatures f;
    memset(&f, 0, sizeof f);
    gfx_cc_get_features(id0, id1, &f);
    ShaderProgram* p = new ShaderProgram();
    p->id0 = id0;
    p->id1 = id1;
    dk_layout_from_features(&f, &p->layout);
    dk_combiner_params(&f, &p->params);
    p->num_inputs = (uint8_t)f.num_inputs;
    p->used_textures[0] = f.used_textures[0];
    p->used_textures[1] = f.used_textures[1];
    p->stride = p->layout.num_floats * 4u;
    p->mesh_vert = VK_NULL_HANDLE;
    const int e = pack_find(id0, id1);
    p->vert = pack_module(e, 0);
    p->frag = pack_module(e, 1);
    if (p->vert != VK_NULL_HANDLE && p->frag != VK_NULL_HANDLE) {
        p->uber = false;
        p->mesh_vert = pack_module(e, 2);
    } else {
        p->uber = true;
        p->vert = s_uber_vert;
        p->frag = s_uber_frag;
        if (!s_missed[std::make_pair(id0, id1)]) {
            s_missed[std::make_pair(id0, id1)] = true;
            sysLogPrintf(LOG_NOTE, "SPIR-V miss %016llx %08x", (unsigned long long)id0, (unsigned)id1);
        }
    }
    s_progs[std::make_pair(id0, id1)] = p;
    vk_load_shader(p);
    return p;
}

struct ShaderProgram* vk_lookup_shader(uint64_t id0, uint32_t id1) {
    auto it = s_progs.find(std::make_pair(id0, id1));
    return it == s_progs.end() ? nullptr : it->second;
}

void vk_shader_get_info(struct ShaderProgram* p, uint8_t* num_inputs, bool used_textures[2]) {
    *num_inputs = p->num_inputs;
    used_textures[0] = p->used_textures[0];
    used_textures[1] = p->used_textures[1];
}

void vk_clear_shaders(void) {
    /* The pipelines name the programs: drop them too (a program pointer can be
     * reused), once the submissions that may use them have completed. */
    for (auto& kv : s_pipes) s_dead_pipes.push_back(std::make_pair(kv.second, s_serial));
    s_pipes.clear();
    s_bound_pipe = VK_NULL_HANDLE;
    for (auto& kv : s_progs) delete kv.second;   /* the modules stay: they belong to the pack */
    s_progs.clear();
    s_prg = nullptr;
}

uint32_t vk_new_texture(void) {
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

void vk_delete_texture(uint32_t id) {
    if (id == 0 || id >= s_tex.size() || !s_tex[id].alive) return;
    Tex& t = s_tex[id];
    if (t.has_img) destroy_image_later(t.img);
    t = Tex();
    s_free_ids.push_back(id);
    for (int u = 0; u < 2; u++) {
        if (s_unit[u].kind == 1 && s_unit[u].id == id) s_desc_dirty = true;
    }
}

void vk_select_texture(int tile, uint32_t id, bool linear) {
    s_unit[tile].kind = 1;
    s_unit[tile].id = id;
    s_linear_flag[tile] = linear;
    s_active_unit = tile;
    s_desc_dirty = true;
}

void vk_select_texture_fb(int fb) {
    s_unit[0].kind = 2;
    s_unit[0].id = (uint32_t)fb;
    s_linear_flag[0] = true;
    s_active_unit = 0;
    s_desc_dirty = true;
}

void vk_upload_texture(const uint8_t* rgba, uint32_t width, uint32_t height, bool gen_mipmaps) {
    const Unit& un = s_unit[s_active_unit];
    if (!s_up || un.kind != 1 || un.id == 0 || un.id >= s_tex.size() || !s_tex[un.id].alive || width == 0 || height == 0) {
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
    if (t.has_img) destroy_image_later(t.img);
    t.has_img = false;
    if (!upload_image(&t.img, data, s_offs, width, height, levels, total)) {
        sysLogPrintf(LOG_ERROR, "VK: texture %ux%u: could not create the image", width, height);
        return;
    }
    t.has_img = true;
    t.w = width;
    t.h = height;
    t.levels = levels;
    s_desc_dirty = true;
}

void vk_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt, bool mipmaps) {
    s_active_unit = tile;
    const SmpKey k = sampler_key(linear_filter, cms, cmt, mipmaps);
    const Unit& un = s_unit[tile];
    if (un.kind == 1 && un.id < s_tex.size() && s_tex[un.id].alive) {
        s_tex[un.id].smp = k;
    } else if (un.kind == 2 && (int)un.id < (int)s_fbs.size()) {
        s_fbs[un.id].smp = k;
    }
    s_desc_dirty = true;
}

void vk_set_depth_mode(bool depth_test, bool depth_update, bool depth_compare, bool depth_source_prim, uint16_t zmode) {
    if (depth_test) {
        s_depth_test = true;
        s_depth_mask = depth_update;
        if (depth_compare) {
            switch (zmode) {
                case ZMODE_INTER: s_depth_func = VK_COMPARE_OP_LESS_OR_EQUAL; s_decal = false; break;
                case ZMODE_OPA:
                case ZMODE_XLU:
                    s_depth_func = depth_source_prim ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
                    s_decal = false;
                    break;
                case ZMODE_DEC: s_depth_func = VK_COMPARE_OP_LESS_OR_EQUAL; s_decal = true; break;
            }
        } else {
            s_depth_func = VK_COMPARE_OP_ALWAYS;
            s_decal = false;
        }
    } else {
        s_depth_test = false;
    }
    s_depth_dirty = true;
}

void vk_set_depth_range(float znear, float zfar) {
    s_znear = znear;
    s_zfar = zfar;
    s_vp_dirty = true;
}

void vk_set_viewport(int x, int y, int w, int h) {
    s_vp[0] = x; s_vp[1] = y; s_vp[2] = w; s_vp[3] = h;
    s_vp_dirty = true;
}

void vk_set_scissor(int x, int y, int w, int h) {
    s_sc[0] = x; s_sc[1] = y; s_sc[2] = w; s_sc[3] = h;
    s_sc_dirty_rect = true;
}

void vk_set_use_alpha(bool use_alpha, bool modulate) {
    s_blend = use_alpha;
    s_modulate = modulate;
}

float* vk_stream_begin(size_t max_floats) {
    if (!s_up) return nullptr;
    ensure_frame();
    s_stream_ptr = (float*)ring_alloc((uint32_t)(max_floats * 4), 16, &s_stream_buf, &s_stream_off);
    return s_stream_ptr;
}

void vk_draw_triangles(float buf[], size_t len, size_t ntris) {
    float* const stream = s_stream_ptr;
    s_stream_ptr = nullptr;
    if (ntris == 0 || !prepare_draw()) return;
    VkBuffer vb;
    VkDeviceSize off;
    if (buf != nullptr && buf == stream) {
        vb = s_stream_buf;
        off = s_stream_off;
    } else {
        uint8_t* dst = ring_alloc((uint32_t)(len * 4), 16, &vb, &off);
        memcpy(dst, buf, len * 4);
    }
    VkCommandBuffer c = main_cmd();
    const VkBuffer bufs[2] = { vb, s_zero.buf };
    const VkDeviceSize offs[2] = { off, 0 };
    vkCmdBindVertexBuffers(c, 0, s_prg->uber ? 2 : 1, bufs, offs);
    vkCmdDraw(c, (uint32_t)(3 * ntris), 1, 0, 0);
    s_st_draws++;
    if (s_prg->uber) s_st_uber_draws++;
    zcut_pass((uint32_t)(3 * ntris));   /* D987 */
}

void vk_init(void) {
    if (s_up) return;
    if (!load_loader()) sysFatalError("Vulkan: no usable Vulkan loader (GE_RENDERER=vk)");
    create_instance(true);
    SDL_Window* w = (SDL_Window*)gfx_sdl_get_sdl_window();
    if (!SDL_Vulkan_CreateSurface(w, s_instance, &s_surface)) {
        sysFatalError("Vulkan: SDL_Vulkan_CreateSurface failed:\n%s", SDL_GetError());
    }
    s_phys = pick_device(s_instance, s_surface, &s_qfam);
    if (s_phys == VK_NULL_HANDLE) {
        sysFatalError("Vulkan: no device with Vulkan 1.3, dynamic rendering and push descriptors presents to this window");
    }
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(s_phys, &props);
        snprintf(s_renderer_str, sizeof s_renderer_str, "%s (Vulkan)", props.deviceName);
        sysLogPrintf(LOG_NOTE, "VK: using %s, driver %08x, queue family %u", props.deviceName, props.driverVersion, s_qfam);
    }
    create_device();
    pick_depth_format();
    create_frames();
    create_layouts();
    load_pack();
    gfx_skyplus_sea = s_sky_frag && s_lut_frag && s_fs_vert ? 1 : 0;
    s_up = true;

    gfx_rdp_affine = 0;   /* D986: the pack is built with perspective-correct shade */
    {   /* fog: the pack carries both forms; Draw.fog_pixel picks one (gfx_deko3d.cpp dk_init) */
        const char* fp = getenv("GE_FOGPIXEL");
        gfx_fog_force = (fp && fp[0]) ? (fp[0] == '1') : -1;
        gfx_fog_vertex = gfx_fog_force >= 0 ? !gfx_fog_force : !portDisplayFixes;
        s_uni.fog_pixel = gfx_fog_vertex ? 0 : 1;
    }
    s_uni.noise_scale = 1.0f;
    s_zero = make_host_chunk(256, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    memset(s_zero.cpu, 0, 256);
    {
        static const uint8_t black[4] = { 0, 0, 0, 255 };
        std::vector<uint32_t> offs(1, 0);
        if (!upload_image(&s_dummy, black, offs, 1, 1, 1, 4)) sysFatalError("Vulkan: could not create the dummy texture");
    }
    create_swapchain();
    s_fbs.resize(1);   /* fb 0 = the window, as gfx_opengl_init */
}

void vk_on_resize(void) {}
void vk_start_frame(void) {
    s_frame_count++;
    if (s_up) ensure_frame();
}
void vk_end_frame(void) {}
void vk_finish_render(void) {}

int vk_create_framebuffer(void) {
    const int i = (int)s_fbs.size();
    s_fbs.resize(i + 1);
    Fb& f = s_fbs[i];
    f.smp = SmpKey{ VK_FILTER_LINEAR, VK_FILTER_LINEAR, MIP_NONE, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                    VK_SAMPLER_ADDRESS_MODE_REPEAT, 1 };
    if (s_up && gfx_framebuffers_enabled) build_fb_images(f);   /* 1x1 until sized, as GL */
    return i;
}

void vk_update_framebuffer_parameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level, bool invert_y,
                                      bool render_target, bool has_depth_buffer, bool can_extract_depth) {
    (void)render_target;
    (void)can_extract_depth;
    if (fb_id < 0 || fb_id >= (int)s_fbs.size()) return;
    Fb& f = s_fbs[fb_id];
    width = width < 1 ? 1 : width;
    height = height < 1 ? 1 : height;
    if (!gfx_framebuffers_enabled) has_depth_buffer = false;
    if (fb_id != 0 && gfx_framebuffers_enabled) {
        const bool depth_missing = has_depth_buffer && !f.has_depth_img;
        if (f.w != width || f.h != height || f.msaa != msaa_level || depth_missing || !f.created) {
            if (s_render_fb == fb_id) end_rendering();
            destroy_fb_images(f);
            f.w = width;
            f.h = height;
            f.msaa = msaa_level;
            f.has_depth = has_depth_buffer;
            build_fb_images(f);
            s_desc_dirty = true;
        }
    }
    f.w = width;
    f.h = height;
    f.msaa = msaa_level;
    f.has_depth = has_depth_buffer;
    f.invert_y = invert_y;
}

bool vk_start_draw_to_framebuffer(int fb_id, float noise_scale) {
    if (gfx_framebuffers_enabled && fb_id < (int)s_fbs.size()) {
        if (noise_scale != 0.0f) s_noise_scale = 1.0f / noise_scale;
        if (fb_id != s_cur_fb) {
            s_vp_dirty = s_sc_dirty_rect = s_depth_dirty = s_desc_dirty = true;   /* the old target may be sampled now */
        }
        s_cur_fb = fb_id;
        return true;
    }
    return false;
}

void clear_rect(bool color, bool depth, int x, int y, int w, int h) {
    if (!begin_rendering()) return;
    VkClearAttachment a[2] = {};
    uint32_t n = 0;
    if (color) {
        a[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        a[n].colorAttachment = 0;
        a[n].clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
        n++;
    }
    if (depth && target_has_depth()) {
        a[n].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        a[n].clearValue.depthStencil = { 1.0f, 0 };
        n++;
    }
    if (n == 0 || w <= 0 || h <= 0) return;
    VkClearRect r = {};
    r.rect = { { x, y }, { (uint32_t)w, (uint32_t)h } };
    r.layerCount = 1;
    vkCmdClearAttachments(main_cmd(), n, a, 1, &r);
}

void vk_clear_framebuffer(bool color, bool depth) {
    if (!s_up) return;
    /* GL disables the scissor for the clear */
    clear_rect(color, depth, 0, 0, (int)fb_width(s_cur_fb), (int)fb_height(s_cur_fb));
}

void vk_clear_region(bool color, bool depth, int x, int y, int w, int h) {
    if (!s_up || w <= 0 || h <= 0) return;
    s_sc[0] = x; s_sc[1] = y; s_sc[2] = w; s_sc[3] = h;   /* GL keeps this scissor box afterwards (D48) */
    s_sc_dirty_rect = true;
    const int H = (int)fb_height(s_cur_fb), W = (int)fb_width(s_cur_fb);
    int ty = H - y - h;   /* GL bottom-up -> top-down, clipped to the target */
    int x0 = x < 0 ? 0 : x, y0 = ty < 0 ? 0 : ty;
    int x1 = x + w > W ? W : x + w, y1 = ty + h > H ? H : ty + h;
    if (x1 <= x0 || y1 <= y0) return;
    clear_rect(color, depth, x0, y0, x1 - x0, y1 - y0);
}

/* The single-sample image holding fb's picture (resolving its MSAA image
 * first). Outside a rendering pass. */
bool readable_image(int fb, VkImage* img, VkImageLayout** layout) {
    VkCommandBuffer c = main_cmd();
    if (is_window(fb)) {
        if (!ensure_acquired()) return false;
        *img = s_sc_images[s_acq];
        *layout = &s_sc_layout;
        return true;
    }
    Fb& f = s_fbs[fb];
    if (!f.created) return false;
    if (f.has_ms) {
        layout_to(c, f.color_ms.img, VK_IMAGE_ASPECT_COLOR_BIT, f.l_ms, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        layout_to(c, f.color.img, VK_IMAGE_ASPECT_COLOR_BIT, f.l_color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageResolve r = {};
        r.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        r.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        r.extent = { f.w, f.h, 1 };
        vkCmdResolveImage(c, f.color_ms.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, f.color.img,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    }
    *img = f.color.img;
    *layout = &f.l_color;
    return true;
}

bool writable_image(int fb, VkImage* img, VkImageLayout** layout) {
    if (is_window(fb)) {
        if (!ensure_acquired()) return false;
        *img = s_sc_images[s_acq];
        *layout = &s_sc_layout;
        return true;
    }
    Fb& f = s_fbs[fb];
    if (!f.created) return false;
    *img = f.color.img;
    *layout = &f.l_color;
    return true;
}

/* blit a GL-coordinate rect between two framebuffers (both stored top-down) */
void blit_gl(int fb_src, int sx0, int sy0, int sx1, int sy1, int fb_dst, int dx0, int dy0, int dx1, int dy1, bool linear) {
    ensure_frame();
    end_rendering();
    VkImage si, di;
    VkImageLayout *sl, *dl;
    if (!readable_image(fb_src, &si, &sl) || !writable_image(fb_dst, &di, &dl) || si == di) return;
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
    const int dw = (int)fb_width(fb_dst), dh = (int)fb_height(fb_dst);
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx1 > sw) sx1 = sw;
    if (sy1 > sh) sy1 = sh;
    if (dx0 < 0) dx0 = 0;
    if (dy0 < 0) dy0 = 0;
    if (dx1 > dw) dx1 = dw;
    if (dy1 > dh) dy1 = dh;
    if (sx1 <= sx0 || sy1 <= sy0 || dx1 <= dx0 || dy1 <= dy0) return;
    VkCommandBuffer c = main_cmd();
    layout_to(c, si, VK_IMAGE_ASPECT_COLOR_BIT, *sl, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    layout_to(c, di, VK_IMAGE_ASPECT_COLOR_BIT, *dl, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit r = {};
    r.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    r.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    r.srcOffsets[0] = { sx0, sy0, 0 };
    r.srcOffsets[1] = { sx1, sy1, 1 };
    r.dstOffsets[0] = { dx0, flip ? dy1 : dy0, 0 };
    r.dstOffsets[1] = { dx1, flip ? dy0 : dy1, 1 };
    vkCmdBlitImage(c, si, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, di, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r,
                   linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
}

void vk_copy_framebuffer(int fb_dst, int fb_src, int left, int top, bool flip_y, bool use_back) {
    (void)use_back;   /* the image being drawn is read (GL: GL_BACK) */
    if (!s_up || !gfx_framebuffers_enabled || fb_dst >= (int)s_fbs.size() || fb_src >= (int)s_fbs.size()) return;
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
    s_desc_dirty = true;
}

void vk_resolve_msaa_color_buffer(int fb_target, int fb_source) {
    if (!s_up || !gfx_framebuffers_enabled || fb_source <= 0 || fb_source >= (int)s_fbs.size() ||
        fb_target >= (int)s_fbs.size()) {
        return;
    }
    Fb& src = s_fbs[fb_source];
    if (!src.created) return;
    ensure_frame();
    end_rendering();
    if (!is_window(fb_target) && src.has_ms && s_fbs[fb_target].created && s_fbs[fb_target].w == src.w &&
        s_fbs[fb_target].h == src.h) {
        Fb& dst = s_fbs[fb_target];
        VkCommandBuffer c = main_cmd();
        layout_to(c, src.color_ms.img, VK_IMAGE_ASPECT_COLOR_BIT, src.l_ms, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        layout_to(c, dst.color.img, VK_IMAGE_ASPECT_COLOR_BIT, dst.l_color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageResolve r = {};
        r.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        r.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        r.extent = { src.w, src.h, 1 };
        vkCmdResolveImage(c, src.color_ms.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.color.img,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    } else {
        blit_gl(fb_source, 0, 0, (int)src.w, (int)src.h, fb_target, 0, 0, (int)fb_width(fb_target),
                (int)fb_height(fb_target), false);
    }
    s_desc_dirty = true;
}

void* vk_get_framebuffer_texture_id(int fb_id) { return (void*)(uintptr_t)fb_id; }
void vk_set_texture_filter(enum FilteringMode mode) { s_filter = mode; }
enum FilteringMode vk_get_texture_filter(void) { return s_filter; }
void vk_set_mipmap_filter(enum MipmapFilteringMode mode) { s_mipfilter = mode; }
void vk_set_anisotropy_level(int level) { s_aniso = level < 1 ? 1 : level > 16 ? 16 : level; }
int vk_get_max_anisotropy_level(void) { return (int)s_max_aniso; }
uint32_t vk_mesh_create(const float* data, size_t nfloats) {
    const uint64_t n64 = (uint64_t)nfloats * sizeof(float);
    if (!s_up || n64 == 0 || n64 > UINT32_MAX) return 0;
    VkMesh m;
    m.chunk = make_host_chunk(n64, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    m.bytes = (uint32_t)n64;
    memcpy(m.chunk.cpu, data, m.bytes);
    s_meshes.push_back(m);
    return (uint32_t)s_meshes.size() - 1;
}

void vk_mesh_delete(uint32_t name) {
    if (name == 0 || name >= s_meshes.size() || !s_meshes[name].chunk.buf) return;
    s_dead_meshes.push_back(std::make_pair(s_meshes[name].chunk, s_serial));
    s_meshes[name] = VkMesh();
}

/* One run of a static mesh with the program's GPU-transform twin. The twin's
 * vertex shader writes GL's clip z, so it needs VK_EXT_depth_clip_control
 * (vk_mesh_supported). Culling is a dynamic state set around the draw (Fast3D
 * culls on the CPU otherwise); GL's front-face rule as in gfx_deko3d.cpp. */
void vk_mesh_draw(uint32_t name, const struct GfxMeshDraw* d) {
    if (!s_up || d == nullptr || s_prg == nullptr || s_prg->uber || s_prg->mesh_vert == VK_NULL_HANDLE ||
        !s_clip_control || name == 0 || name >= s_meshes.size()) {
        return;
    }
    const VkMesh& m = s_meshes[name];
    if (!m.chunk.buf || d->nverts == 0 || d->nmtx == 0) return;
    if ((uint64_t)d->byte_offset + (uint64_t)d->nverts * s_prg->stride > m.bytes) return;
    ensure_frame();

    const uint32_t nmtx = d->nmtx < GFX_MESH_MAX_MTX ? d->nmtx : GFX_MESH_MAX_MTX;
    DkMeshUniforms* u = (DkMeshUniforms*)ring_alloc((uint32_t)sizeof(DkMeshUniforms), (uint32_t)s_ubo_align,
                                                    &s_mesh_ubo_buf, &s_mesh_ubo_off);
    memset(u, 0, offsetof(DkMeshUniforms, mtx));
    memcpy(u->mtx, d->mtx, (size_t)nmtx * sizeof u->mtx[0]);
    memcpy(u->fog_color, d->fog_color, sizeof u->fog_color);
    u->fog_mul = d->fog_mul;
    u->fog_off = d->fog_off;
    u->fog_from_z = d->fog_from_z;
    memcpy(u->lod_mask, d->lod_mask, sizeof u->lod_mask);
    memcpy(u->env_in, d->env_in, sizeof u->env_in);
    memcpy(u->env_mask, d->env_mask, sizeof u->env_mask);

    s_mesh_mode = true;
    s_desc_dirty = true;   /* the Mesh block is part of the push */
    const bool ok = prepare_draw();
    if (ok) {
        VkCommandBuffer c = main_cmd();
        if (d->cull != 0) {
            vkCmdSetCullMode(c, d->cull == 1 ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT);
            vkCmdSetFrontFace(c, d->invert_y ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE);
        }
        const VkDeviceSize off = d->byte_offset;
        vkCmdBindVertexBuffers(c, 0, 1, &m.chunk.buf, &off);
        vkCmdDraw(c, d->nverts, 1, 0, 0);
        s_st_draws++;
        s_st_mesh_draws++;
        s_st_mesh_total++;
        zcut_pass(d->nverts);   /* D987 */
        if (d->cull != 0) {
            vkCmdSetCullMode(c, VK_CULL_MODE_NONE);
            vkCmdSetFrontFace(c, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        }
    }
    s_mesh_mode = false;
    s_desc_dirty = true;   /* the next classic draw pushes without the Mesh block */
}

bool vk_mesh_supported(uint64_t id0, uint32_t id1) {
    if (!s_clip_control) return false;
    const int e = pack_find(id0, id1);
    return e >= 0 && s_pack[e].vert_len != 0 && s_pack[e].frag_len != 0 && s_pack[e].mesh_vert_len != 0;
}

/* ---------------- post passes (FXAA, GE+ sky) and present slots ---------------- */

/* The fullscreen triangle into the current target with the GL rects vp / sc
 * (bottom-up), the fragment shader reading `view` at binding 0 and `uni` at
 * binding 4; GL's draw state is re-applied by the next draw. The sampled
 * image must already be SHADER_READ_ONLY. */
void draw_fullscreen(VkShaderModule frag, const void* uni, uint32_t uni_size, VkImageView view, VkSampler smp,
                     const int vp[4], const int sc[4]) {
    if (!begin_rendering()) return;
    VkCommandBuffer c = main_cmd();
    bind_pipeline(get_fs_pipeline(frag, target_color_format(),
                                  target_has_depth() ? target_depth_format() : VK_FORMAT_UNDEFINED, target_samples()));
    const int H = (int)fb_height(s_cur_fb);
    VkViewport v = { (float)vp[0], (float)(H - vp[1]), (float)vp[2], -(float)vp[3], s_znear, s_zfar };
    vkCmdSetViewport(c, 0, 1, &v);
    set_scissor_rect(sc[0], sc[1], sc[2], sc[3]);
    vkCmdSetDepthTestEnable(c, VK_FALSE);
    vkCmdSetDepthWriteEnable(c, VK_FALSE);
    vkCmdSetDepthBiasEnable(c, VK_FALSE);
    VkBuffer ub;
    VkDeviceSize uo;
    uint8_t* up = ring_alloc(uni_size, (uint32_t)s_ubo_align, &ub, &uo);
    memcpy(up, uni, uni_size);
    VkDescriptorImageInfo ii = { smp, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorBufferInfo bi = { ub, uo, uni_size };
    VkWriteDescriptorSet w[2] = {};
    w[0].sType = w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].descriptorCount = w[1].descriptorCount = 1;
    w[0].dstBinding = 0;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[0].pImageInfo = &ii;
    w[1].dstBinding = 4;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[1].pBufferInfo = &bi;
    vkCmdPushDescriptorSetKHR(c, VK_PIPELINE_BIND_POINT_GRAPHICS, s_pipe_layout, 0, 2, w);
    vkCmdDraw(c, 3, 1, 0, 0);
    s_st_draws++;
    s_vp_dirty = s_sc_dirty_rect = s_depth_dirty = s_desc_dirty = true;   /* GL's state again at the next draw */
}

struct SkyPlusDraw {
    PortSkyPlus p;
    int vp[4];
    float aspA, aspB;
};
#define SKYPLUS_RING 64
SkyPlusDraw s_sky_ring[SKYPLUS_RING];
unsigned s_sky_next = 0;
Img s_sky_img;
int s_sky_gen = -1;
#define SKYLUT_W 128   /* the reflection sky's panorama (skylut.frag.glsl) */
#define SKYLUT_H 64
#define SEAPANO_W 4096   /* the far sea, baked (skyplus.frag.glsl pass 4); the shader's PANO_W / PANO_H */
#define SEAPANO_H 512
#define SKY_HALF_SCALE 1   /* 0: the final sky pass at full size */
Img s_wtex_img;
int s_wtex_gen = -1;
int s_lut_fb = -1, s_hsea_fb = -1, s_pano_fb = -1, s_skyr_fb = -1;
bool s_pano_valid = false;
int s_pano_bakes = 0;
float s_pano_eyey = 0.0f, s_pano_ex = 0.0f, s_pano_ez = 0.0f;
float s_pano_key[8][4];   /* the sky block's members the baked sea depends on, as of the bake */
uint32_t s_pano_frame = 0;

void sky_reset_targets(void) {   /* the framebuffer list was cleared */
    s_lut_fb = s_hsea_fb = s_pano_fb = s_skyr_fb = -1;
    s_pano_valid = false;
    s_pano_bakes = 0;
}

struct SkyUniforms { float v[16][4]; };   /* block Sky of skycommon.glsl (256 bytes) */

/* D578 present slots: copies of the window image, in the swapchain's format */
struct ISlot {
    Img img;
    uint32_t w = 0, h = 0;
    VkFormat fmt = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};
ISlot s_islot[GFX_INTERP_SLOTS];

void copy_color(VkImage src, VkImageLayout& sl, VkImage dst, VkImageLayout& dl, uint32_t w, uint32_t h) {
    VkCommandBuffer c = main_cmd();
    layout_to(c, src, VK_IMAGE_ASPECT_COLOR_BIT, sl, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    layout_to(c, dst, VK_IMAGE_ASPECT_COLOR_BIT, dl, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy r = {};
    r.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    r.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    r.extent = { w, h, 1 };
    vkCmdCopyImage(c, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
}

} // namespace

struct GfxRenderingAPI gfx_vulkan_api = {
    vk_get_name,
    vk_get_max_texture_size,
    vk_get_clip_parameters,
    vk_unload_shader,
    vk_load_shader,
    vk_create_and_load_new_shader,
    vk_lookup_shader,
    vk_shader_get_info,
    vk_clear_shaders,
    vk_new_texture,
    vk_select_texture,
    vk_upload_texture,
    vk_set_sampler_parameters,
    vk_set_depth_mode,
    vk_set_depth_range,
    vk_set_viewport,
    vk_set_scissor,
    vk_set_use_alpha,
    vk_draw_triangles,
    vk_init,
    vk_on_resize,
    vk_start_frame,
    vk_end_frame,
    vk_finish_render,
    vk_create_framebuffer,
    vk_update_framebuffer_parameters,
    vk_start_draw_to_framebuffer,
    vk_copy_framebuffer,
    vk_clear_framebuffer,
    vk_clear_region,
    vk_resolve_msaa_color_buffer,
    vk_get_framebuffer_texture_id,
    vk_select_texture_fb,
    vk_delete_texture,
    vk_set_texture_filter,
    vk_get_texture_filter,
    vk_set_mipmap_filter,
    vk_set_anisotropy_level,
    vk_get_max_anisotropy_level,
    vk_stream_begin,
    vk_load_shader_with,
    vk_mesh_create,
    vk_mesh_delete,
    vk_mesh_draw,
    vk_mesh_supported,
};

extern "C" {

bool gfx_vulkan_wanted(void) {
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("GE_RENDERER");
        on = (e != NULL && strcmp(e, "vk") == 0) ? 1 : 0;   /* GL stays the default */
    }
    return on != 0;
}

bool gfx_vulkan_probe(void) {
    if (!load_loader()) return false;
    /* a throwaway instance: is there a usable Vulkan 1.3 device? */
    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    const char* port_ext = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    if (has_instance_ext(port_ext)) {
        ci.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        ci.enabledExtensionCount = 1;
        ci.ppEnabledExtensionNames = &port_ext;
    }
    VkInstance inst = VK_NULL_HANDLE;
    if (vkCreateInstance(&ci, nullptr, &inst) != VK_SUCCESS) {
        sysLogPrintf(LOG_WARNING, "VK: vkCreateInstance failed");
        return false;
    }
    volkLoadInstanceOnly(inst);
    uint32_t qfam = 0;
    const bool ok = pick_device(inst, VK_NULL_HANDLE, &qfam) != VK_NULL_HANDLE;
    vkDestroyInstance(inst, nullptr);
    if (!ok) sysLogPrintf(LOG_WARNING, "VK: no device with Vulkan 1.3, dynamic rendering, synchronization2 and push descriptors");
    return ok;
}

const char* gfx_vulkan_renderer_string(void) {
    return s_renderer_str;
}

void gfx_vulkan_get_dimensions(uint32_t* w, uint32_t* h) {
    window_drawable_size(w, h);
    if (*w == 0 || *h == 0) {   /* minimised: keep the last size */
        *w = s_sc_extent.width ? s_sc_extent.width : 640;
        *h = s_sc_extent.height ? s_sc_extent.height : 480;
    }
}

void gfx_vulkan_set_swap_interval(int interval) {
    interval = interval < 0 ? 0 : interval;
    if (interval != s_swap_interval) {
        s_swap_interval = interval;
        s_sc_dirty = true;   /* the present mode belongs to the swapchain */
    }
}

/* GE_PCDUMP / screenshots: the path of the PPM the next present writes, empty for none. */
std::string s_dump_path;

/* The just-presented image, copied to host memory in the same submission (at
 * the head of the PRESENT transition) and written as a P6 PPM, top row first,
 * as gfx_opengl_dump_bound_fbo does. Called after the present, queue idle. */
void write_dump(const HostChunk& hc, uint32_t w, uint32_t h, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) return;
    const bool bgr = s_sc_format == VK_FORMAT_B8G8R8A8_UNORM || s_sc_format == VK_FORMAT_B8G8R8A8_SRGB;
    std::vector<uint8_t> row((size_t)w * 3);
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* src = hc.cpu + (size_t)y * w * 4;
        for (uint32_t x = 0; x < w; x++) {
            row[x * 3 + 0] = src[x * 4 + (bgr ? 2 : 0)];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + (bgr ? 0 : 2)];
        }
        fwrite(row.data(), 1, row.size(), f);
    }
    fclose(f);
}

/* Submit the frame's two command buffers; present when the window was drawn. */
void gfx_vulkan_present(void) {
    if (!s_up || s_parked || !s_frame_open) return;
    Frame& f = s_frames[s_frame_idx];
    end_rendering();
    const bool present = s_acq >= 0;
    HostChunk dump;
    const uint32_t dw = s_sc_extent.width, dh = s_sc_extent.height;
    const std::string dump_path = s_dump_path;
    s_dump_path.clear();
    if (present && !dump_path.empty() && s_sc_readable && dw && dh) {
        dump = make_host_chunk((VkDeviceSize)dw * dh * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        layout_to(f.cmd, s_sc_images[s_acq], VK_IMAGE_ASPECT_COLOR_BIT, s_sc_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy r = {};
        r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        r.imageExtent = { dw, dh, 1 };
        vkCmdCopyImageToBuffer(f.cmd, s_sc_images[s_acq], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dump.buf, 1, &r);
    }
    if (present) {
        image_barrier(f.cmd, s_sc_images[s_acq], VK_IMAGE_ASPECT_COLOR_BIT, 1, s_sc_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,   /* drawn, or a blit / resolve target */
                      VK_PIPELINE_STAGE_2_NONE, 0);
    }
    VK_CHECK(vkEndCommandBuffer(f.pre));
    VK_CHECK(vkEndCommandBuffer(f.cmd));

    VkSemaphoreSubmitInfo wait = {};
    wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait.semaphore = f.acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal = {};
    signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal.semaphore = present ? s_sc_render_done[s_acq] : VK_NULL_HANDLE;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cbi[2] = {};
    cbi[0].sType = cbi[1].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbi[0].commandBuffer = f.pre;
    cbi[1].commandBuffer = f.cmd;
    VkSubmitInfo2 si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    si.waitSemaphoreInfoCount = present ? 1 : 0;
    si.pWaitSemaphoreInfos = &wait;
    si.commandBufferInfoCount = 2;
    si.pCommandBufferInfos = cbi;
    si.signalSemaphoreInfoCount = present ? 1 : 0;
    si.pSignalSemaphoreInfos = &signal;
    VK_CHECK(vkQueueSubmit2(s_queue, 1, &si, f.fence));
    f.serial = s_serial++;
    s_frame_open = false;

    if (present) {
        const uint32_t img = (uint32_t)s_acq;
        VkPresentInfoKHR pi = {};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &s_sc_render_done[img];
        pi.swapchainCount = 1;
        pi.pSwapchains = &s_swapchain;
        pi.pImageIndices = &img;
        const VkResult r = vkQueuePresentKHR(s_queue, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
            s_sc_dirty = true;
        } else if (r != VK_SUCCESS) {
            VK_CHECK(r);
        }
    }
    s_acq = -1;
    s_frame_idx = (s_frame_idx + 1) % FRAMES_IN_FLIGHT;
    if (dump.buf) {   /* a capture is rare and may stall: wait for the copy, then write the file */
        vkQueueWaitIdle(s_queue);
        write_dump(dump, dw, dh, dump_path);
        free_host_chunk(dump);
    }
}

void gfx_vulkan_park(void) {
    if (!s_up || s_parked) return;
    if (s_frame_open) {   /* a frame recorded but never submitted: drop it */
        end_rendering();
        Frame& f = s_frames[s_frame_idx];
        vkEndCommandBuffer(f.pre);
        vkEndCommandBuffer(f.cmd);
        s_frame_open = false;
    }
    vkDeviceWaitIdle(s_dev);
    s_parked = true;
    for (auto& kv : s_pipes) vkDestroyPipeline(s_dev, kv.second, nullptr);
    s_pipes.clear();
    for (auto& d : s_dead_pipes) vkDestroyPipeline(s_dev, d.first, nullptr);
    s_dead_pipes.clear();
    for (auto& kv : s_fs_pipes) vkDestroyPipeline(s_dev, kv.second, nullptr);
    s_fs_pipes.clear();
    destroy_image_now(s_sky_img);
    for (ISlot& sl : s_islot) {
        destroy_image_now(sl.img);
        sl = ISlot();
    }
    for (auto& kv : s_modules) vkDestroyShaderModule(s_dev, kv.second, nullptr);
    s_modules.clear();
    for (auto& kv : s_samplers) vkDestroySampler(s_dev, kv.second, nullptr);
    s_samplers.clear();
    for (Tex& t : s_tex) {
        if (t.has_img) destroy_image_now(t.img);
    }
    s_tex.clear();
    for (Fb& f : s_fbs) {
        destroy_image_now(f.color);
        destroy_image_now(f.color_ms);
        destroy_image_now(f.depth);
    }
    s_fbs.clear();
    sky_reset_targets();
    destroy_image_now(s_wtex_img);
    s_wtex_gen = -1;
    for (VkMesh& m : s_meshes) free_host_chunk(m.chunk);
    s_meshes.clear();
    for (auto& d : s_dead_meshes) free_host_chunk(d.first);
    s_dead_meshes.clear();
    destroy_image_now(s_dummy);
    destroy_image_now(s_win_depth);
    for (auto& d : s_dead_imgs) destroy_image_now(d.i);
    s_dead_imgs.clear();
    for (auto& kv : s_img_heaps) {
        for (VkDeviceMemory m : kv.second.mem) vkFreeMemory(s_dev, m, nullptr);
    }
    s_img_heaps.clear();
    for (auto& c : s_ring_chunks) free_host_chunk(c);
    s_ring_chunks.clear();
    free_host_chunk(s_zero);
    pipeline_cache_save();
    vkDestroyPipelineCache(s_dev, s_pipe_cache, nullptr);
    vkDestroyPipelineLayout(s_dev, s_pipe_layout, nullptr);
    vkDestroyPipelineLayout(s_dev, s_sky_pipe_layout, nullptr);
    vkDestroyDescriptorSetLayout(s_dev, s_sky_set_layout, nullptr);
    vkDestroyDescriptorSetLayout(s_dev, s_set_layout, nullptr);
    destroy_swapchain_views();
    if (s_swapchain) vkDestroySwapchainKHR(s_dev, s_swapchain, nullptr);
    s_swapchain = VK_NULL_HANDLE;
    for (Frame& f : s_frames) {
        vkDestroySemaphore(s_dev, f.acquired, nullptr);
        vkDestroyFence(s_dev, f.fence, nullptr);
        vkDestroyCommandPool(s_dev, f.pool, nullptr);
    }
    vkDestroyDevice(s_dev, nullptr);
    s_dev = VK_NULL_HANDLE;
    vkDestroySurfaceKHR(s_instance, s_surface, nullptr);
    if (s_messenger) vkDestroyDebugUtilsMessengerEXT(s_instance, s_messenger, nullptr);
    vkDestroyInstance(s_instance, nullptr);
    s_instance = VK_NULL_HANDLE;
    s_up = false;
    sysLogPrintf(LOG_NOTE, "VK: backend parked (device idle, everything destroyed); %llu pipelines built in %.0f ms "
                 "(longest %.1f ms); %llu mesh draws; validation: %d error(s), %d warning(s)", (unsigned long long)s_st_pipelines,
                 s_st_pipe_ms, s_st_pipe_ms_max,
                 (unsigned long long)s_st_mesh_total, s_validation_errors, s_validation_warnings);
}

/* FXAA (gfx_opengl_fxaa_pass, gfx_deko3d_fxaa_pass): src_fb_id's colour
 * drawn into the window at its own size, anti-aliased. */
void gfx_vulkan_fxaa_pass(int src_fb_id) {
    if (!s_up || s_parked || !s_fxaa_frag || !s_fs_vert || !gfx_framebuffers_enabled || src_fb_id <= 0 ||
        src_fb_id >= (int)s_fbs.size()) {
        return;
    }
    Fb& src = s_fbs[src_fb_id];
    if (!src.created) return;
    ensure_frame();
    end_rendering();
    src.smp.ws = src.smp.wt = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;   /* GL sets these on the texture and leaves them */
    layout_to(main_cmd(), src.color.img, VK_IMAGE_ASPECT_COLOR_BIT, src.l_color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const int prev_fb = s_cur_fb;
    s_cur_fb = 0;
    const float u[4] = { 1.0f / (float)src.w, 1.0f / (float)src.h, 0.0f, 0.0f };
    const int vp[4] = { 0, 0, (int)src.w, (int)src.h };
    const int sc[4] = { 0, 0, (int)s_sc_extent.width, (int)s_sc_extent.height };
    draw_fullscreen(s_fxaa_frag, u, sizeof u, src.color.view_a1, get_sampler(src.smp), vp, sc);
    s_cur_fb = prev_fb;
}

int gfx_vulkan_skyplus_stage(const struct PortSkyPlus* p, const int vp[4], float aspA, float aspB) {
    const int slot = (int)(s_sky_next++ % SKYPLUS_RING);
    SkyPlusDraw& d = s_sky_ring[slot];
    d.p = *p;
    for (int i = 0; i < 4; i++) d.vp[i] = vp[i];
    d.aspA = aspA;
    d.aspB = aspB;
    return slot;
}

/* GE+ sky (gfx_opengl_skyplus_pass; the passes of gfx_deko3d_skyplus_pass): the sky shader over GL viewport vp of the
 * current target, no depth test or write, no blending. On a water level the same shader draws the sea: the reflection
 * sky (skylut) into a small panorama, the far sea baked into a panorama (pass 4), the near sea at reduced size (pass 1),
 * then the final pass at half size and its enlargement (pass 5). */

/* five samplers (0..4), the Sky block at 5: the sky passes' descriptor set (s_sky_set_layout) */
void draw_sky(VkShaderModule frag, const void* uni, uint32_t uni_size, const VkImageView views[5], const VkSampler smps[5],
              const int vp[4], const int sc[4]) {
    if (!begin_rendering()) return;
    VkCommandBuffer c = main_cmd();
    bind_pipeline(get_fs_pipeline(frag, target_color_format(),
                                  target_has_depth() ? target_depth_format() : VK_FORMAT_UNDEFINED, target_samples(), true));
    const int H = (int)fb_height(s_cur_fb);
    VkViewport v = { (float)vp[0], (float)(H - vp[1]), (float)vp[2], -(float)vp[3], s_znear, s_zfar };
    vkCmdSetViewport(c, 0, 1, &v);
    set_scissor_rect(sc[0], sc[1], sc[2], sc[3]);
    vkCmdSetDepthTestEnable(c, VK_FALSE);
    vkCmdSetDepthWriteEnable(c, VK_FALSE);
    vkCmdSetDepthBiasEnable(c, VK_FALSE);
    VkBuffer ub;
    VkDeviceSize uo;
    uint8_t* up = ring_alloc(uni_size, (uint32_t)s_ubo_align, &ub, &uo);
    memcpy(up, uni, uni_size);
    VkDescriptorImageInfo ii[5];
    VkWriteDescriptorSet w[6] = {};
    for (int i = 0; i < 5; i++) {
        ii[i] = { smps[i], views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].descriptorCount = 1;
        w[i].dstBinding = (uint32_t)i;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[i].pImageInfo = &ii[i];
    }
    VkDescriptorBufferInfo bi = { ub, uo, uni_size };
    w[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[5].descriptorCount = 1;
    w[5].dstBinding = 5;
    w[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[5].pBufferInfo = &bi;
    vkCmdPushDescriptorSetKHR(c, VK_PIPELINE_BIND_POINT_GRAPHICS, s_sky_pipe_layout, 0, 6, w);
    vkCmdDraw(c, 3, 1, 0, 0);
    s_st_draws++;
    s_vp_dirty = s_sc_dirty_rect = s_depth_dirty = s_desc_dirty = true;   /* GL's state again at the next draw */
}

/* a framebuffer of this size for the sky passes: created on first use, kept; -1 when it cannot be built */
int sky_target(int* id, int w, int h, SmpKey smp) {
    if (*id < 0) {
        *id = vk_create_framebuffer();
        s_fbs[*id].smp = smp;
    }
    const Fb& f = s_fbs[*id];
    if (f.w != (uint32_t)w || f.h != (uint32_t)h || !f.created) {
        vk_update_framebuffer_parameters(*id, (uint32_t)w, (uint32_t)h, 1, false, true, false, false);
    }
    const Fb& g = s_fbs[*id];
    return g.created && g.w == (uint32_t)w && g.h == (uint32_t)h ? *id : -1;
}

void gfx_vulkan_skyplus_pass(int slot) {
    if (!s_up || s_parked || slot < 0 || slot >= SKYPLUS_RING) return;
    if (!s_sky_frag || !s_fs_vert) {
        gfx_skyplus_failed = 1;   /* skyplus.c draws GE's sky from the next frame on */
        return;
    }
    const SkyPlusDraw& d = s_sky_ring[slot];
    const PortSkyPlus& p = d.p;
    if (d.vp[2] <= 0 || d.vp[3] <= 0 || (d.aspA < 0.0f ? -d.aspA : d.aspA) < 1e-6f || !p.tex) return;
    if (!is_window(s_cur_fb) && !s_fbs[s_cur_fb].created) return;
    ensure_frame();
    if (p.texGen != s_sky_gen || !s_sky_img.img) {
        /* RGBA8 with the intensity in every channel (GL: R8 read as .r) and the
         * box mip chain (GL: glGenerateMipmap; the plane is seen at grazing angles) */
        static std::vector<uint8_t> buf, mips;
        buf.resize((size_t)SKYPLUS_TEX_W * SKYPLUS_TEX_H * 4);
        for (int i = 0; i < SKYPLUS_TEX_W * SKYPLUS_TEX_H; i++) {
            buf[i * 4 + 0] = buf[i * 4 + 1] = buf[i * 4 + 2] = buf[i * 4 + 3] = p.tex[i];
        }
        std::vector<uint32_t> offs;
        uint32_t levels = 1;
        build_mips(buf.data(), SKYPLUS_TEX_W, SKYPLUS_TEX_H, mips, offs, &levels);
        if (s_sky_img.img) destroy_image_later(s_sky_img);
        if (!upload_image(&s_sky_img, mips.data(), offs, SKYPLUS_TEX_W, SKYPLUS_TEX_H, levels, mips.size())) return;
        s_sky_gen = p.texGen;
    }
    if (p.sea && p.waterTex && (p.waterTexGen != s_wtex_gen || !s_wtex_img.img)) {   /* the wave heights, for the sea */
        static std::vector<uint8_t> mips;
        std::vector<uint32_t> offs;
        uint32_t levels = 1;
        build_mips(p.waterTex, SKYPLUS_WTEX_W, SKYPLUS_WTEX_H, mips, offs, &levels);
        if (s_wtex_img.img) destroy_image_later(s_wtex_img);
        s_wtex_img = Img();
        if (upload_image(&s_wtex_img, mips.data(), offs, SKYPLUS_WTEX_W, SKYPLUS_WTEX_H, levels, mips.size())) {
            s_wtex_gen = p.waterTexGen;
        }
    }
    const bool sea = p.sea && s_wtex_img.img;

    SkyUniforms u;
    memset(&u, 0, sizeof u);
    auto v3 = [&](int i, const float* a, float w) { u.v[i][0] = a[0]; u.v[i][1] = a[1]; u.v[i][2] = a[2]; u.v[i][3] = w; };
    u.v[0][0] = (float)d.vp[0]; u.v[0][1] = (float)d.vp[1]; u.v[0][2] = (float)d.vp[2]; u.v[0][3] = (float)d.vp[3];
    u.v[1][0] = d.aspA; u.v[1][1] = d.aspB; u.v[1][3] = p.band;
    for (int i = 0; i < 4; i++) v3(2 + i, p.corner[i], 0.0f);
    v3(6, p.eye, p.cloudY);
    v3(7, p.sky, p.gain);
    v3(8, p.cloud, p.texScale);
    u.v[9][0] = p.layerOfs[0][0]; u.v[9][1] = p.layerOfs[0][1]; u.v[9][2] = p.layerOfs[1][0]; u.v[9][3] = p.layerOfs[1][1];
    u.v[10][0] = p.layerOfs[2][0]; u.v[10][1] = p.layerOfs[2][1]; u.v[10][2] = p.sunGlow; u.v[10][3] = p.night ? 1.0f : 0.0f;
    v3(11, p.sunDir, p.swell);
    v3(12, p.sunCol, p.chop);
    v3(13, p.moonDir, p.speed);
    v3(14, p.water, p.seaY);
    u.v[15][0] = p.time;
    u.v[15][1] = sea ? 1.0f : 0.0f;

    const SmpKey cloud_k = { VK_FILTER_LINEAR, VK_FILTER_LINEAR, MIP_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                             VK_SAMPLER_ADDRESS_MODE_REPEAT, 1 };
    /* anisotropic: seen at grazing angles, the far waves keep their detail along the horizon instead of blurring into a mirror */
    const SmpKey wave_k = { VK_FILTER_LINEAR, VK_FILTER_LINEAR, MIP_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                            VK_SAMPLER_ADDRESS_MODE_REPEAT, 2 };
    const SmpKey fb_clamp = { VK_FILTER_LINEAR, VK_FILTER_LINEAR, MIP_NONE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                              VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1 };
    const SmpKey fb_wrapx = { VK_FILTER_LINEAR, VK_FILTER_LINEAR, MIP_NONE, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                              VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1 };
    VkImageView views[5] = { s_sky_img.view, sea ? s_wtex_img.view : s_dummy.view, s_dummy.view, s_dummy.view, s_dummy.view };
    VkSampler smps[5];
    for (int i = 0; i < 5; i++) smps[i] = get_sampler(default_tex_sampler());
    smps[0] = get_sampler(cloud_k);
    smps[1] = get_sampler(wave_k);

    const int prev = s_cur_fb;
    /* draw `frag` over the whole of off-screen target fb (w x h) */
    auto into = [&](VkShaderModule frag, int fb, int w, int h) {
        s_cur_fb = fb;
        const int r[4] = { 0, 0, w, h };
        draw_sky(frag, &u, sizeof u, views, smps, r, r);
        s_cur_fb = prev;
    };
    auto readable = [&](int fb, int slot_no) {   /* the target's picture, as texture `slot_no` of the next draws */
        end_rendering();
        Fb& f = s_fbs[fb];
        layout_to(main_cmd(), f.color.img, VK_IMAGE_ASPECT_COLOR_BIT, f.l_color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        views[slot_no] = f.color.view_a1;
        smps[slot_no] = get_sampler(f.smp);
    };

    /* the reflection sky, once per pass, into its small panorama; the sea reads it */
    bool lut = false;
    if (sea && s_lut_frag && gfx_framebuffers_enabled) {
        const int fb = sky_target(&s_lut_fb, SKYLUT_W, SKYLUT_H, fb_wrapx);
        if (fb >= 0) {
            into(s_lut_frag, fb, SKYLUT_W, SKYLUT_H);
            readable(fb, 2);
            lut = true;
        }
    }
    /* the far sea, baked into its panorama when the sun (or the level's sky and water) changes */
    if (sea && lut) {
        const int fb = sky_target(&s_pano_fb, SEAPANO_W, SEAPANO_H, fb_wrapx);
        if (fb >= 0) {
            /* what the bake shows depends on the sun (or the moon) direction, and on the level's sky and water */
            float key[8][4];
            memset(key, 0, sizeof key);
            memcpy(key[0], u.v[11], 12); memcpy(key[1], u.v[13], 12); memcpy(key[2], u.v[7], 12); memcpy(key[3], u.v[14], 16);
            auto dir_cos = [&](int i) {   /* the angle between the baked and the current direction; a level without that light has none */
                const float c = key[i][0] * s_pano_key[i][0] + key[i][1] * s_pano_key[i][1] + key[i][2] * s_pano_key[i][2];
                const float l = key[i][0] * key[i][0] + key[i][1] * key[i][1] + key[i][2] * key[i][2];
                return l < 0.25f ? 1.0f : c;
            };
            const float cs = dir_cos(0), cm = dir_cos(1);
            float dl = 0.0f;   /* the level's sky colour and water */
            for (int i = 2; i < 4; i++) for (int j = 0; j < 4; j++) { const float e = key[i][j] - s_pano_key[i][j]; dl += e < 0 ? -e : e; }
            /* recooked when the sun or the moon has moved by more than ~3 degrees (cos 3 = 0.9986), or the level changed */
            const bool changed = cs < 0.9986f || cm < 0.9986f || dl > 0.05f;
            /* baked once, on the level's first frame, before its sky, from the player's start eye (skyplus.c bakeEye); a new level or a
             * restart (bakeReset) redoes it */
            if (p.bakeReset) { s_pano_valid = false; s_pano_bakes = 0; }
            /* the bake of the level's very first frame came out black on the console (cause not found): it is made again on the next
             * frame, and the sea stays live until that second one is done (an image is never drawn from a bad bake) */
            const bool second = s_pano_valid && s_pano_bakes == 1 && s_frame_count != s_pano_frame;
            if ((!s_pano_valid && p.bakeReady) || second || (s_pano_valid && changed && s_frame_count - s_pano_frame > 60)) {
                float eyeNow[3];
                for (int j = 0; j < 3; j++) { eyeNow[j] = u.v[6][j]; u.v[6][j] = p.bakeEye[j]; }
                u.v[2][3] = 4.0f;
                into(s_sky_frag, fb, SEAPANO_W, SEAPANO_H);
                u.v[2][3] = 0.0f;
                for (int j = 0; j < 3; j++) u.v[6][j] = eyeNow[j];
                memcpy(s_pano_key, key, sizeof key);
                s_pano_eyey = p.bakeEye[1]; s_pano_ex = p.bakeEye[0]; s_pano_ez = p.bakeEye[2];
                s_pano_valid = true;
                s_pano_bakes++;
                s_pano_frame = s_frame_count;
            }
            if (s_pano_valid && s_pano_bakes >= 2) {   /* where the panorama was baked from (uBake*) */
                u.v[3][3] = s_pano_ex; u.v[4][3] = s_pano_ez; u.v[5][3] = s_pano_eyey - u.v[14][3];
            } else {   /* not baked yet: the shader draws the whole sea live */
                u.v[3][3] = 0.0f; u.v[4][3] = 0.0f; u.v[5][3] = 0.0f;
            }
            readable(fb, 4);
        }
    }
    /* the sea at half width: its heavy per-pixel work (the waves) runs on half the pixels into a small target, the sky pass then
     * reads it bilinearly. With the half-scale final pass it is halved on both axes too (it feeds a half-size image). */
    const bool reduce = SKY_HALF_SCALE && d.vp[2] / 2 >= 16 && d.vp[3] / 2 >= 16;
    if (sea && lut && d.vp[2] / 2 >= 16) {
        const int sdiv = reduce ? 2 : 1;
        const int hw = d.vp[2] / sdiv / 2, hh = d.vp[3] / sdiv;
        const int fb = sky_target(&s_hsea_fb, hw, hh, fb_clamp);
        if (fb >= 0) {
            u.v[2][3] = 1.0f;
            into(s_sky_frag, fb, hw, hh);
            u.v[2][3] = 2.0f;
            readable(fb, 3);
        }
    }
    /* the final pass at half size: the sky and the sea (whose heavy part is already behind us) are smooth, so they are drawn into a
     * target of a quarter of the pixels, then enlarged onto the screen by the composite (uPass 5) */
    if (reduce) {
        const int rw = d.vp[2] / 2, rh = d.vp[3] / 2;
        const int fb = sky_target(&s_skyr_fb, rw, rh, fb_clamp);
        if (fb >= 0) {
            into(s_sky_frag, fb, rw, rh);   /* uPass is 2 (or 0): the whole sky and sea */
            readable(fb, 3);
            u.v[2][3] = 5.0f;   /* the composite */
        }
    }
    draw_sky(s_sky_frag, &u, sizeof u, views, smps, d.vp, d.vp);
}

/* D578: copy the window image (this pass's picture) into present slot `slot` */
void gfx_vulkan_interp_store(int slot, uint32_t width, uint32_t height) {
    if (!s_up || s_parked || slot < 0 || slot >= GFX_INTERP_SLOTS || width == 0 || height == 0 || !s_sc_readable) return;
    ensure_frame();
    end_rendering();
    if (!ensure_acquired()) return;
    ISlot& sl = s_islot[slot];
    if (width != sl.w || height != sl.h || sl.fmt != s_sc_format) {
        /* sized on its first store at a new window size (D583): slots a mode never uses take no memory */
        if (sl.img.img) destroy_image_later(sl.img);
        sl.w = sl.h = 0;
        if (!create_image(&sl.img, s_sc_format, width, height, 1,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_COLOR_BIT)) {
            return;
        }
        sl.w = width;
        sl.h = height;
        sl.fmt = s_sc_format;
        sl.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    const uint32_t w = width < s_sc_extent.width ? width : s_sc_extent.width;
    const uint32_t h = height < s_sc_extent.height ? height : s_sc_extent.height;
    copy_color(s_sc_images[s_acq], s_sc_layout, sl.img.img, sl.layout, w, h);
}

/* D578: present slot `slot` into the window image the next present shows */
void gfx_vulkan_interp_show(int slot) {
    if (!s_up || s_parked || slot < 0 || slot >= GFX_INTERP_SLOTS || s_islot[slot].w == 0) return;
    ensure_frame();
    end_rendering();
    if (!ensure_acquired()) return;
    ISlot& sl = s_islot[slot];
    if (sl.fmt != s_sc_format) return;   /* stored before the swapchain changed format */
    /* the slot's own size: one stored before a window resize can still be pending (D583 ring) */
    const uint32_t w = sl.w < s_sc_extent.width ? sl.w : s_sc_extent.width;
    const uint32_t h = sl.h < s_sc_extent.height ? sl.h : s_sc_extent.height;
    copy_color(sl.img.img, sl.layout, s_sc_images[s_acq], s_sc_layout, w, h);
}

/* D578: block until the queue (and so a VSync'd present) completes, so the
 * render worker's present clock learns the display's real refresh timing */
void gfx_vulkan_interp_sync(void) {
    if (!s_up || s_parked) return;
    vkQueueWaitIdle(s_queue);
    retire();
}

void gfx_vulkan_frame_count_rewind(void) { s_frame_count--; }

/* GE_PCDUMP / screenshots: have the next present write the window to `path`
 * (a PPM). false: the swapchain cannot be read back. */
bool gfx_vulkan_dump_window(const char* path) {
    if (!s_up || s_parked || !s_sc_readable || path == nullptr) return false;
    s_dump_path = path;
    return true;
}

void gfx_vulkan_perf_report(double frames) {
    if (frames <= 0 || getenv("GE_PERFSTAT") == NULL) return;
    fprintf(stderr, "PERFSTAT vk draws=%llu uber_draws=%llu mesh_draws=%llu uploads=%llu pipelines=%llu (per %.0f frames) | host chunks %u\n",
            (unsigned long long)s_st_draws, (unsigned long long)s_st_uber_draws, (unsigned long long)s_st_mesh_draws,
            (unsigned long long)s_st_uploads, (unsigned long long)s_st_pipelines, frames, s_ring.chunks());
    s_st_draws = s_st_uber_draws = s_st_mesh_draws = s_st_uploads = 0;
}

} // extern "C"

#endif /* GE_VULKAN */
