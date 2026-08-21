/*
 * Software-RSP placeholder (Phase 2).
 *
 * port/fast3d/ currently contains only the API header (gfx_api.h); the real
 * implementation arrives in Phase 2 by adapting the PD port's fast3d. video.c
 * calls these entry points, so they must exist to link. Every function is a
 * no-op: frames are submitted and dropped until the RSP emulator lands.
 *
 * DELETE THIS FILE when the real fast3d sources are added (they define the
 * same symbols; keeping both would be a duplicate-definition link error).
 */

#include <stdint.h>
#include <stdbool.h>

#include <PR/gbi.h>

/* port/fast3d is not on the include path; use the relative form. */
#include "../fast3d/gfx_api.h"

struct GfxDimensions gfx_current_dimensions = {
    1.0f,           /* internal_mul   */
    640, 480,       /* width, height  */
    1.5f,           /* aspect_ratio   */
};

void gfx_init(const struct GfxInitSettings *settings) { (void)settings; }
void gfx_destroy(void) {}
void gfx_start_frame(void) {}
void gfx_end_frame(void) {}
void gfx_run(Gfx *commands) { (void)commands; }

void gfx_set_target_fps(int fps)            { (void)fps; }
void gfx_set_texture_filter(int mode)       { (void)mode; }
void gfx_set_mipmap_filter(int mode)        { (void)mode; }
