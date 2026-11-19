#ifndef GFX_SDL_H
#define GFX_SDL_H

#include "gfx_window_manager_api.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct GfxWindowManagerAPI gfx_sdl;

/* Make the window's GL context current on the calling thread (rendering
 * runs on the game's scheduler thread, not the thread that created it). */
void gfx_sdl_make_context_current(void);

/* Release the GL context from the calling (host main) thread after init, so
 * the game's scheduler thread can bind it per frame (see gfx_sdl2.cpp). */
void gfx_sdl_release_context(void);
/* D344: finish queued GL work and unbind the context (render thread, quit). */
void gfx_sdl_park_for_exit(void);
/* D632: delete the GL context + window (host thread, exit, render parked). */
void gfx_sdl_teardown_for_exit(void);

/* Refresh the cached drawable size after a window resize (called by the
 * host thread's event pump). */
void gfx_sdl_update_cached_size(void);

/* D578: display refresh rate (Hz) of the window; poll on the host thread,
 * read anywhere. 0 = unknown. */
void gfx_sdl_poll_refresh_rate(void);
int gfx_sdl_get_refresh_rate(void);

#if defined(__SWITCH__) || defined(GE_VULKAN)
/* D973 / issue #44: the rendering backend owns the swapchain; SDL calls these
 * instead of swapping a GL context. */
void gfx_sdl_set_external_present(void (*present)(void), void (*dims)(uint32_t*, uint32_t*), void (*park)(void));
void gfx_sdl_set_external_swap_interval(void (*set_interval)(int));
#endif
#ifdef GE_VULKAN
/* Issue #44: create a Vulkan window and no GL context (before gfx init). */
void gfx_sdl_set_vulkan_window(bool on);
/* The SDL_Window* (for SDL_Vulkan_CreateSurface). */
void* gfx_sdl_get_sdl_window(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
