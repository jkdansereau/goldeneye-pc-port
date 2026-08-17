/*
 * Video: SDL2 window + OpenGL context + frame pacing.
 *
 * Owns the GL context that the software RSP (fast3d) renders into. Drives the
 * frame boundary hooks called by the port scheduler (gesched.c).
 *
 * Modelled on the PD port's port/src/video.c (~590 lines).
 *
 * STATUS: scaffolding stub — implement during Phase 1 (window + clear) and
 * Phase 2 (frame pacing, options).
 */

#include <SDL.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "video.h"

/* fast3d entry points (see port/fast3d/gfx_api.h). */
extern void gfx_init(const void *settings);
extern void gfx_destroy(void);
extern void gfx_start_frame(void);
extern void gfx_run(void *commands);
extern void gfx_end_frame(void);

static int  initDone = 0;
static int  vidVsync = 1;
static int  vidFpsLimit = 0;
static int  vidFullscreen = 0;
static int  vidMaximize = 0;

int videoInit(void)
{
    /* TODO(Phase 1):
     *  - SDL_Init(VIDEO)
     *  - create window (title "GoldenEye 007", default 640x480 or config)
     *  - create GL 3.0+ context
     *  - build the GfxInitSettings (window-manager API = SDL2, rendering API
     *    = OpenGL) and call gfx_init()
     *  - apply config: vsync, fps limit, fullscreen, maximize
     */
    sysLogPrintf(LOG_INFO, "videoInit: TODO (Phase 1)");
    initDone = 1;
    return 0;
}

void videoDestroy(void)
{
    if (initDone) {
        gfx_destroy();
        initDone = 0;
    }
}

void videoStartFrame(void)
{
    if (initDone) gfx_start_frame();
}

void videoSubmitCommands(Gfx *cmds)
{
    if (initDone) gfx_run(cmds);
}

void videoEndFrame(void)
{
    if (!initDone) return;
    gfx_end_frame();
    /* TODO(Phase 2): FPS accounting + framerate limiting. */
}

void videoSetVsync(int enabled)        { vidVsync = enabled; /* TODO: SDL_GL_SetSwapInterval */ }
void videoSetFramerateLimit(int fps)   { vidFpsLimit = fps;  /* TODO */ }
int  videoGetFullscreen(void)          { return vidFullscreen; }
void videoSetFullscreen(int enabled)   { vidFullscreen = enabled; /* TODO */ }
int  videoGetMaximize(void)            { return vidMaximize; }
void videoSetMaximize(int enabled)     { vidMaximize = enabled; /* TODO */ }
float videoGetFPS(void)                { return 0.0f; /* TODO */ }

PD_CONSTRUCTOR static void videoConfigInit(void)
{
    configRegisterInt("Video.Vsync", &vidVsync, 0, 1);
    configRegisterInt("Video.FramerateLimit", &vidFpsLimit, 0, 240);
    configRegisterInt("Video.Fullscreen", &vidFullscreen, 0, 1);
    configRegisterInt("Video.Maximize", &vidMaximize, 0, 1);
}
