#ifndef GFX_VULKAN_H
#define GFX_VULKAN_H

/* Issue #44: Vulkan rendering backend for the PC build (GE_RENDERER=vk; GL
 * stays the default). Implements GfxRenderingAPI with gfx_opengl.cpp's call
 * order and state semantics; the structure (frames in flight, external
 * present through SDL) follows gfx_deko3d.cpp. The window is an SDL2 Vulkan
 * window (gfx_sdl_set_vulkan_window); the swapchain belongs to the backend. */

#include <stdint.h>
#include <stdbool.h>

#include "gfx_rendering_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef GE_VULKAN
extern struct GfxRenderingAPI gfx_vulkan_api;

/* true when GE_RENDERER=vk; read once. */
bool gfx_vulkan_wanted(void);
/* Loads the Vulkan loader and checks for a Vulkan 1.3 device with dynamic
 * rendering. false: no usable driver (the caller falls back to GL). */
bool gfx_vulkan_probe(void);
/* Submit the frame and present (external present hook, render thread). */
void gfx_vulkan_present(void);
/* Quit: device idle, swapchain, device and instance destroyed (render thread). */
void gfx_vulkan_park(void);
/* Drawable size of the Vulkan window (the swapchain's once it exists). */
void gfx_vulkan_get_dimensions(uint32_t* w, uint32_t* h);
void gfx_vulkan_set_swap_interval(int interval);
/* GE_PCDUMP / screenshots: the next present writes the window to this PPM (before it returns). */
bool gfx_vulkan_dump_window(const char* path);
const char* gfx_vulkan_renderer_string(void);

/* Passes outside the rendering API (gfx_pc.cpp picks these or the GL ones). */
struct PortSkyPlus;
void gfx_vulkan_fxaa_pass(int src_fb_id);
int gfx_vulkan_skyplus_stage(const struct PortSkyPlus* p, const int vp[4], float aspA, float aspB);
void gfx_vulkan_skyplus_pass(int slot);
void gfx_vulkan_interp_store(int slot, uint32_t width, uint32_t height);
void gfx_vulkan_interp_show(int slot);
void gfx_vulkan_interp_sync(void);
void gfx_vulkan_frame_count_rewind(void);
void gfx_vulkan_perf_report(double frames);
#endif

#ifdef __cplusplus
}
#endif

#endif
