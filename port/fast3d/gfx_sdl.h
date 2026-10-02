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

/* Refresh the cached drawable size after a window resize (called by the
 * host thread's event pump). */
void gfx_sdl_update_cached_size(void);

/* D409: refresh rate (Hz) of the display the window is on; 0 = unknown. */
int gfx_sdl_get_refresh_rate(void);

#ifdef __cplusplus
}
#endif

#endif
