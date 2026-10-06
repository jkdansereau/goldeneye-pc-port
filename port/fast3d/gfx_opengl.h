#ifndef GFX_OPENGL_H
#define GFX_OPENGL_H

#include "gfx_rendering_api.h"

extern struct GfxRenderingAPI gfx_opengl_api;

#ifdef __cplusplus
extern "C" {
#endif
/* D482: GL_RENDERER captured at init ("" if unknown). */
const char *gfx_opengl_renderer_string(void);

/* Frame capture (dev tool, env GE_PCDUMP=1) — see gfx_opengl.cpp. */
bool gfx_opengl_pcdump_enabled(void);
bool gfx_opengl_dump_bound_fbo(uint32_t width, uint32_t height, const char* path);
#ifdef __cplusplus
}
#endif

#endif
