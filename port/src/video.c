/*
 * Video: SDL2 window + OpenGL context + frame pacing.
 *
 * Owns the GL context that the software RSP (fast3d) renders into. Drives the
 * frame boundary hooks called by the port scheduler (gesched.c).
 *
 * Phase 1: real window + GL context + clear colour, driven by the demo loop
 * in main.c. gfx_* entry points are still the no-ops in gfxstub.c until the
 * PD-derived fast3d lands (Phase 2); videoStartFrame/EndFrame do the GL work
 * directly so a visible frame exists before that.
 */

#include <SDL.h>
#include <GL/gl.h>

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

static SDL_Window *vidWindow = NULL;
static SDL_GLContext vidGL = NULL;

/* FPS accounting. */
static uint64_t fpsLastTick = 0;
static int      fpsFrames = 0;
static float    fpsValue = 0.0f;

int videoInit(void)
{
    if (initDone)
        return 0;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        sysLogPrintf(LOG_ERROR, "videoInit: SDL_Init failed: %s",
                     SDL_GetError());
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    /* fast3d will want a modern context once it lands; 3.0 core is the floor
     * the PD port targets. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    int w = 640, h = 480;   /* GE's internal resolution (NTSC) */
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (vidFullscreen)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    vidWindow = SDL_CreateWindow("GoldenEye 007 (PC port)",
                                 SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!vidWindow) {
        sysLogPrintf(LOG_ERROR, "videoInit: SDL_CreateWindow failed: %s",
                     SDL_GetError());
        return -1;
    }
    if (vidMaximize)
        SDL_MaximizeWindow(vidWindow);

    vidGL = SDL_GL_CreateContext(vidWindow);
    if (!vidGL) {
        sysLogPrintf(LOG_ERROR, "videoInit: SDL_GL_CreateContext failed: %s",
                     SDL_GetError());
        return -1;
    }
    SDL_GL_MakeCurrent(vidWindow, vidGL);
    SDL_GL_SetSwapInterval(vidVsync ? 1 : 0);

    /* Phase 1 clear colour: dark blue-grey so the window is visibly alive. */
    glClearColor(0.10f, 0.12f, 0.16f, 1.0f);
    glViewport(0, 0, w, h);

    /* fast3d hooks (no-ops until Phase 2). */
    gfx_init(NULL);

    fpsLastTick = sysGetMicroseconds();
    initDone = 1;
    sysLogPrintf(LOG_INFO, "videoInit: %dx%d window + GL context up", w, h);
    return 0;
}

void videoDestroy(void)
{
    if (!initDone)
        return;
    gfx_destroy();
    if (vidGL) {
        SDL_GL_DeleteContext(vidGL);
        vidGL = NULL;
    }
    if (vidWindow) {
        SDL_DestroyWindow(vidWindow);
        vidWindow = NULL;
    }
    SDL_Quit();
    initDone = 0;
}

/*
 * Pump window events. Returns 1 when the app should quit (window close or
 * ESC). Called by the demo loop until the port scheduler owns the frame
 * boundary (Phase 2).
 */
int videoHandleEvents(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT)
            return 1;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
            return 1;
    }
    return 0;
}

void videoStartFrame(void)
{
    if (!initDone)
        return;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gfx_start_frame();
}

void videoSubmitCommands(Gfx *cmds)
{
    if (initDone)
        gfx_run(cmds);
}

void videoEndFrame(void)
{
    if (!initDone)
        return;
    gfx_end_frame();
    if (vidWindow)
        SDL_GL_SwapWindow(vidWindow);

    /* FPS accounting. */
    fpsFrames++;
    uint64_t now = sysGetMicroseconds();
    if (now - fpsLastTick >= 1000000) {
        fpsValue = (float)fpsFrames * 1000000.0f / (float)(now - fpsLastTick);
        fpsFrames = 0;
        fpsLastTick = now;
    }
}

void videoSetVsync(int enabled)
{
    vidVsync = enabled;
    if (initDone && vidWindow)
        SDL_GL_SetSwapInterval(vidVsync ? 1 : 0);
}
void videoSetFramerateLimit(int fps)   { vidFpsLimit = fps;  /* TODO Phase 2 */ }
int  videoGetFullscreen(void)          { return vidFullscreen; }
void videoSetFullscreen(int enabled)
{
    vidFullscreen = enabled;
    if (initDone && vidWindow)
        SDL_SetWindowFullscreen(vidWindow,
                                enabled ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}
int  videoGetMaximize(void)            { return vidMaximize; }
void videoSetMaximize(int enabled)
{
    vidMaximize = enabled;
    if (initDone && vidWindow) {
        if (enabled)
            SDL_MaximizeWindow(vidWindow);
        else
            SDL_RestoreWindow(vidWindow);
    }
}
float videoGetFPS(void)                { return fpsValue; }

PD_CONSTRUCTOR static void videoConfigInit(void)
{
    configRegisterInt("Video.Vsync", &vidVsync, 0, 1);
    configRegisterInt("Video.FramerateLimit", &vidFpsLimit, 0, 240);
    configRegisterInt("Video.Fullscreen", &vidFullscreen, 0, 1);
    configRegisterInt("Video.Maximize", &vidMaximize, 0, 1);
}
