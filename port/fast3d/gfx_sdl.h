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

/* Refresh the cached drawable size after a window resize (called by the
 * host thread's event pump). */
void gfx_sdl_update_cached_size(void);

#ifdef __cplusplus
}
#endif

#endif
