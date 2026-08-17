#ifndef GFX_API_H
#define GFX_API_H

/*
 * Public API for the software RSP (fast3d).
 *
 * This is the interface the rest of the port (video.c / gesched.c) uses to
 * drive the RSP. It mirrors the PD port's port/fast3d/gfx_api.h.
 *
 * STATUS: scaffolding — the implementation (gfx_pc.cpp etc.) is brought in
 * during Phase 2 by adapting the PD fast3d. See port/fast3d/README.md.
 */

#ifndef __cplusplus
#include <stdint.h>
#include <stdbool.h>
#endif

#include <PR/gbi.h>

#ifdef __cplusplus
extern "C" {
#endif

struct GfxRenderingAPI;
struct GfxWindowManagerAPI;

struct GfxDimensions {
    float internal_mul;
    uint32_t width, height;
    float aspect_ratio;
};

struct GfxInitSettings {
    struct GfxWindowManagerAPI *wapi;
    struct GfxRenderingAPI *rapi;
    /* window title / size / fullscreen / etc. — see PD gfx_window_manager_api.h */
    const char *title;
    uint32_t width, height;
    int fullscreen;
    int vsync;
};

/* Current draw-area dimensions (before any scaling). */
extern struct GfxDimensions gfx_current_dimensions;

void gfx_init(const struct GfxInitSettings *settings);
void gfx_destroy(void);

/* Frame boundary. */
void gfx_start_frame(void);
void gfx_end_frame(void);

/* Run the RSP over a display list (the core of the emulation). */
void gfx_run(Gfx *commands);

/* Options. */
void gfx_set_target_fps(int fps);
void gfx_set_texture_filter(int mode);
void gfx_set_mipmap_filter(int mode);

#ifdef __cplusplus
}
#endif

#endif /* GFX_API_H */
