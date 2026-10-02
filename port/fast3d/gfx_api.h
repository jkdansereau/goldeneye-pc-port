#ifndef GFX_API_H
#define GFX_API_H

#ifndef __cplusplus
#include <stdint.h>
#include <stdbool.h>
#endif

#include "gfx_rendering_api.h"
#include "gfx_window_manager_api.h"

struct XYWidthHeight {
    int16_t x, y;
    uint32_t width, height;
};

struct GfxDimensions {
    float internal_mul;
    uint32_t width, height;
    float aspect_ratio;
};

struct GfxInitSettings {
    struct GfxWindowManagerAPI *wapi;
    struct GfxRenderingAPI *rapi;
    struct GfxWindowInitSettings window_settings;
};

extern struct GfxDimensions gfx_current_window_dimensions; // The dimensions of the window
extern struct GfxDimensions
    gfx_current_dimensions; // The dimensions of the draw area the game draws to, before scaling (if applicable)
extern struct XYWidthHeight
    gfx_current_game_window_viewport; // The area of the window the game is drawn to, (0, 0) is top-left corner
extern uint32_t gfx_msaa_level;
extern struct XYWidthHeight gfx_current_native_viewport; // The internal/native video mode of the game
extern float gfx_current_native_aspect; // The aspect ratio of the above mode
extern bool gfx_framebuffers_enabled;
extern bool gfx_detail_textures_enabled;

void gfx_init(const struct GfxInitSettings *settings);
void gfx_destroy(void);
struct GfxRenderingAPI* gfx_get_current_rendering_api(void);
void gfx_start_frame(void);
void gfx_run(Gfx* commands);
void gfx_end_frame(void);
void gfx_set_target_fps(int);
/* D409: render an interpolated in-between frame per game frame (see gfx_pc.cpp). */
void gfx_set_frame_interpolation(int on, int vi_rate, int refresh_hz, int vsync);
/* D409: deferred real-frame present, driven from the scheduler thread's
 * message wait (port/src/libultra.c osRecvMesg). */
int64_t gfx_pending_present_wait_us(void);
void gfx_present_pending(void);
void gfx_set_texture_filter(enum FilteringMode mode);
void gfx_set_mipmap_filter(enum MipmapFilteringMode mode);
void gfx_set_fix_mip_textures(int on);
void gfx_set_detail_base_tile(int on);  /* D236: sample the base image of a DETAIL binding */
void gfx_set_wrap_fix(int on);
void gfx_set_anisotropy_level(int level);  /* 1 = off; clamped to GL max */
void gfx_set_safe_area_crop(int on);       /* crop the N64 TV-overscan safe-area margin instead of showing it as black bars */
/* On-window pixel rect (top-left origin) the full VI logical canvas (0,0)-
 * (SCREEN_WIDTH, SCREEN_HEIGHT) currently maps to, honoring the safe-area
 * crop above -- for inverting a window mouse click into logical 2D UI space
 * (see port/src/optionsoverlay.c, D316). */
void gfx_get_ui_screen_rect(int32_t *outX, int32_t *outY, int32_t *outW, int32_t *outH);
void gfx_texture_cache_clear(void);
int gfx_texture_cache_count(void); /* D235 */
void gfx_texture_cache_delete(const uint8_t *orig_addr);
void gfx_texture_cache_delete_range(const uint8_t *start, const uint8_t *end);
int gfx_create_framebuffer(uint32_t width, uint32_t height, int upscale, int autoresize);
void gfx_resize_framebuffer(int fb, uint32_t width, uint32_t height, int upscale, int autoresize);
void gfx_set_framebuffer(int fb, float noise_scale) ;
void gfx_reset_framebuffer(void);
void gfx_copy_framebuffer(int fb_dst, int fb_src, int left, int top, int use_back);

#endif
