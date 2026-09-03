/*
 * Video: SDL2 window + OpenGL context + frame pacing on top of fast3d.
 *
 * The window itself lives in port/fast3d/gfx_sdl2.cpp (the wapi backend);
 * this file wires the rendering API up, owns frame boundaries and FPS stats,
 * and exposes the small surface the libultra VI shims need.
 *
 * Modelled on the PD port's port/src/video.c (slimmed: no options menu).
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define GE_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define GE_MKDIR(p) mkdir(p, 0777)
#endif

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "video.h"
#include "input.h"
#include "optionsoverlay.h"

#include "../fast3d/gfx_api.h"
#include "../fast3d/gfx_sdl.h"
#include "../fast3d/gfx_opengl.h"

/* GE's internal resolution: NTSC LAN1 is 640x480; PAL LAN1 shows a
 * 640x400 area. The window opens at the native size (1:1) by default. */
#ifdef REFRESH_PAL
#define GE_NATIVE_W 640
#define GE_NATIVE_H 400
#else
#define GE_NATIVE_W 640
#define GE_NATIVE_H 480
#endif

static struct GfxWindowManagerAPI *wmAPI;
static struct GfxRenderingAPI *renderingAPI;
static int initDone = 0;

/*
 * [Video] ge007.ini knobs. Every default reproduces the previously-hardcoded
 * behaviour, so a fresh config or a missing [Video] section changes nothing.
 */
static int cfgVSync         = 1;   /* swap interval: 0 = off, 1 = on            */
static int cfgFpsCap        = 0;   /* frame cap in fps; 0 = uncapped (vsync)    */
static int cfgMSAA          = 1;   /* 1/2/4/8 samples; 1 = off                  */
static int cfgTexFilter     = 1;   /* 0 = nearest, 1 = bilinear (default), 2 = N64 3-point + trilinear */
static int cfgFixMipTex     = 1;   /* RC2: clip mip-contaminated texture uploads to base height */
static int cfgWrapFix       = 0;   /* D74 sub-tile UV pre-wrap + RC3/D167 non-PoT mask-period wrap (opt-in; GE_WRAPFIX env overrides) */
static int cfgFullscreen    = 0;   /* 0 = windowed, 1 = borderless fullscreen   */

/*
 * [Window] persistence. W/H = 0 -> auto (gfx_sdl2 fits a 4:3 window into ~85%
 * of the desktop); X/Y = -1 -> let SDL centre the window.
 * videoSaveWindowState() writes the live geometry back into these on a clean
 * exit (see main.c's atexit handler), so after the first run the file pins
 * whatever size you left it at.
 */
static int cfgWinW   = 0;
static int cfgWinH   = 0;
static int cfgWinX   = -1;
static int cfgWinY   = -1;
static int cfgWinMax = 0;

/*
 * [Game] gameplay-cosmetic knobs (route-(b) hooks in src/, findings D181).
 * portScreenShakeScale multiplies every viShake() amplitude (src/fr.c).
 * 1.0f = original behaviour (headless golden dumps unaffected).
 */
f32 portScreenShakeScale = 1.0f;

PD_CONSTRUCTOR static void videoConfigInit(void)
{
    configRegisterFloat("Game.ScreenShakeIntensity", &portScreenShakeScale, 0.0f, 10.0f);
    configRegisterInt("Video.VSync",         &cfgVSync,      0, 1);
    configRegisterInt("Video.FpsCap",        &cfgFpsCap,     0, 1000);
    configRegisterInt("Video.MSAA",          &cfgMSAA,       1, 8);
    configRegisterInt("Video.TextureFilter", &cfgTexFilter,  0, 2);
    configRegisterInt("Video.FixMipTextures", &cfgFixMipTex, 0, 1);
    configRegisterInt("Video.WrapFix", &cfgWrapFix, 0, 1);
    configRegisterInt("Video.Fullscreen",    &cfgFullscreen, 0, 1);
    configRegisterInt("Window.Width",        &cfgWinW,       0, 16384);
    configRegisterInt("Window.Height",       &cfgWinH,       0, 16384);
    configRegisterInt("Window.X",            &cfgWinX,      -1, 16384);
    configRegisterInt("Window.Y",            &cfgWinY,      -1, 16384);
    configRegisterInt("Window.Maximized",    &cfgWinMax,     0, 1);
}

/* Set by videoRequestLiveConfig() (F10 overlay, host thread); consumed on the
 * scheduler thread in videoStartFrame() where the GL context is bound. */
static volatile int liveCfgDirty = 0;

static void videoApplyTexFilter(void)
{
    if (cfgTexFilter >= 2) {
        gfx_set_texture_filter(FILTER_THREE_POINT);
        gfx_set_mipmap_filter(MIPMAP_LINEAR);
    } else if (cfgTexFilter == 1) {
        gfx_set_texture_filter(FILTER_LINEAR);
        gfx_set_mipmap_filter(MIPMAP_LINEAR);
    } else {
        gfx_set_texture_filter(FILTER_NONE);
        gfx_set_mipmap_filter(MIPMAP_NEAREST);
    }
}

/* Re-apply the live-tunable [Video] knobs (VSync / FpsCap / TextureFilter).
 * MSAA and Fullscreen are FBO/window rebuilds -> "(restart)" in the overlay. */
void videoRequestLiveConfig(void)
{
    liveCfgDirty = 1;
}

/* --- F10 overlay: window / fullscreen changes, deferred to the host thread ---
 * optionsOverlayHandleInput() runs on the scheduler thread; SDL_SetWindowSize /
 * SDL_SetWindowFullscreen pump the Win32 message loop and must run on the
 * window's creating thread. The overlay posts a request here; the host-thread
 * event pump drains it in videoDrainWindowRequests(). */
static volatile int winReqKind = 0;          /* 0 none, 1 resize, 2 fullscreen */
static volatile int winReqA = 0, winReqB = 0;

void videoRequestWindowSize(int w, int h)
{
    winReqA = w; winReqB = h; winReqKind = 1;
}

void videoRequestFullscreen(int on)
{
    winReqA = on ? 1 : 0; winReqKind = 2;
}

void videoGetWindowSize(int *w, int *h)
{
    uint32_t ww = 0, hh = 0; int32_t x = 0, y = 0;
    if (initDone && wmAPI && wmAPI->get_dimensions) {
        wmAPI->get_dimensions(&ww, &hh, &x, &y);
    }
    if (w) *w = (int)ww;
    if (h) *h = (int)hh;
}

void videoGetDesktopSize(int *w, int *h)
{
    SDL_DisplayMode m;
    memset(&m, 0, sizeof(m));
    if (SDL_GetDesktopDisplayMode(0, &m) != 0 || m.w <= 0 || m.h <= 0) {
        m.w = 1920; m.h = 1080;
    }
    if (w) *w = m.w;
    if (h) *h = m.h;
}

int videoIsFullscreen(void)
{
    return (initDone && wmAPI && wmAPI->get_fullscreen_state)
         ? (wmAPI->get_fullscreen_state() ? 1 : 0) : 0;
}

static void videoDrainWindowRequests(void)
{
    int kind = winReqKind;
    if (!kind || !wmAPI) {
        winReqKind = 0;
        return;
    }
    winReqKind = 0;

    if (kind == 1) {
        int w = winReqA, h = winReqB;
        int32_t px = 100, py = 100;
        if (wmAPI->get_fullscreen_state && wmAPI->get_fullscreen_state()) {
            if (wmAPI->set_fullscreen) wmAPI->set_fullscreen(false);
            cfgFullscreen = 0;
        }
        if (wmAPI->get_centered_positions) {
            wmAPI->get_centered_positions(w, h, &px, &py);
        }
        if (wmAPI->set_dimensions) {
            wmAPI->set_dimensions((uint32_t)w, (uint32_t)h, px, py);
        }
        gfx_sdl_update_cached_size();
        cfgWinW = w; cfgWinH = h;
        sysLogPrintf(LOG_INFO, "video: window -> %dx%d", w, h);
    } else if (kind == 2) {
        int on = winReqA;
        if (wmAPI->set_fullscreen) wmAPI->set_fullscreen(on != 0);
        gfx_sdl_update_cached_size();
        cfgFullscreen = on ? 1 : 0;
        sysLogPrintf(LOG_INFO, "video: fullscreen %s", on ? "on" : "off");
    }
}

static u32 frames = 0;
/* Set by the host event pump (F12), consumed on the render thread in
 * videoEndFrame where a GL context is current. */
static volatile int screenshotReq = 0;

/* Pre-swap capture hook (defined below, registered in videoInit). */
static void videoPreSwapCapture(void);
extern void (*gfx_pre_swap_hook)(void);
static double fpsWindowStart = 0.0;
static int fpsNumFrames = 0;
static float vidAvgFPS = 0.f;

int videoInit(void)
{
    wmAPI = &gfx_sdl;
    renderingAPI = &gfx_opengl_api;

    gfx_current_native_viewport.width = GE_NATIVE_W;
    gfx_current_native_viewport.height = GE_NATIVE_H;
    gfx_current_native_aspect = (float)GE_NATIVE_W / (float)GE_NATIVE_H;
    gfx_framebuffers_enabled = true;
    gfx_detail_textures_enabled = false;

    /* MSAA: snap the requested sample count down to a supported power of two. */
    gfx_msaa_level = cfgMSAA >= 8 ? 8 : cfgMSAA >= 4 ? 4 : cfgMSAA >= 2 ? 2 : 1;

    int winW = cfgWinW > 0 ? cfgWinW : 0;   /* 0 -> gfx_sdl2 auto-fits to the desktop */
    int winH = cfgWinH > 0 ? cfgWinH : 0;
    int havePos = (cfgWinX >= 0 && cfgWinY >= 0);

    struct GfxInitSettings set = {
        .wapi = wmAPI,
        .rapi = renderingAPI,
        .window_settings = {
            .title = "GoldenEye 007",
            .width = winW,
            .height = winH,
            .x = havePos ? cfgWinX : 100,
            .y = havePos ? cfgWinY : 100,
            .fullscreen = cfgFullscreen != 0,
            .fullscreen_is_exclusive = false,
            .maximized = cfgWinMax != 0,
            .centered = !havePos,
            .allow_hidpi = false,
        },
    };

    gfx_init(&set);

    /* VSync + optional fps cap; fast3d paces the window itself. */
    wmAPI->set_swap_interval(cfgVSync ? 1 : 0);
    gfx_set_target_fps(cfgFpsCap);   /* 0 = uncapped */

    /* Texture filtering. 1 = bilinear (default, matches prior behaviour),
     * 0 = crisp nearest, 2 = N64 3-point emulation + trilinear mips (opt-in;
     * more console-authentic but softens textures at normal distance -- did
     * NOT fix the Depot roof, see docs/BRIEF-B2-depot-textures.md). All keep
     * point-sampled tiles (HUD, G_TF_POINT) crisp via the per-tile flag. */
    gfx_set_fix_mip_textures(cfgFixMipTex);
    gfx_set_wrap_fix(cfgWrapFix);

    videoApplyTexFilter();

    /* The GL context is currently current on this (host main) thread, but all
     * rendering happens on the game's scheduler thread. WGL only allows a
     * context to be current on one thread at a time, so release it here; the
     * scheduler thread re-binds it per frame via gfx_sdl_make_context_current()
     * (see videoStartFrame). Must come after set_swap_interval above, which
     * still needs a current context on this thread. */
    gfx_sdl_release_context();

    gfx_pre_swap_hook = videoPreSwapCapture;

    initDone = 1;
    sysLogPrintf(LOG_INFO, "video: %dx%d window (native %dx%d)",
                 (int)gfx_current_dimensions.width, (int)gfx_current_dimensions.height,
                 GE_NATIVE_W, GE_NATIVE_H);
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
    if (!initDone) {
        return;
    }
    /* Rendering runs on the game's scheduler thread; the GL context was
     * created on the host main thread. */
    gfx_sdl_make_context_current();

    if (liveCfgDirty) {
        liveCfgDirty = 0;
        wmAPI->set_swap_interval(cfgVSync ? 1 : 0);
        gfx_set_target_fps(cfgFpsCap);   /* 0 = uncapped */
        videoApplyTexFilter();
        sysLogPrintf(LOG_INFO, "video: live config applied "
                     "(vsync=%d fpscap=%d texfilter=%d)",
                     cfgVSync, cfgFpsCap, cfgTexFilter);
    }

    gfx_start_frame();
}

/*
 * Host-thread SDL event pump.
 *
 * On Windows, window messages are only dispatched when the thread that
 * CREATED the window pumps them — and every game thread can be blocked on a
 * message queue at any time. So the host main thread (which created the
 * window in videoInit) must keep pumping; otherwise the window goes
 * "Not Responding" and ESC/close never arrive. fast3d's own handle_events
 * (which runs during rendering) remains as a backstop.
 */
void videoPumpEvents(void)
{
    if (!initDone) {
        return;
    }

    /* Apply any window/fullscreen change the F10 overlay posted from the
     * scheduler thread (must run here, on the window's creating thread). */
    videoDrainWindowRequests();

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            sysLogPrintf(LOG_INFO, "video: quit requested");
            exit(0);
            break;
        case SDL_KEYDOWN:
            /* D145: bare ESC used to exit(0). On the front-end / debrief
             * screens ESC is the natural "back" key, so a player pressing it
             * to page back instead quit the whole game (looked like a crash --
             * clean exit, no crash log). ESC now feeds the N64 B button
             * (back / cancel) via input.c; quitting is window-close (the X) or
             * Alt+F4 only. */
            if ((ev.key.keysym.sym == SDLK_F4) && (ev.key.keysym.mod & KMOD_ALT)) {
                sysLogPrintf(LOG_INFO, "video: Alt+F4 -> quit");
                exit(0);
            } else if (ev.key.keysym.sym == SDLK_F12 && !ev.key.repeat) {
                screenshotReq = 1;
            } else if (ev.key.keysym.sym == SDLK_F10 && !ev.key.repeat) {
                optionsOverlayToggle();   /* F10: port-layer options overlay */
            } else if (ev.key.keysym.sym == SDLK_ESCAPE && !ev.key.repeat) {
                /* Overlay open: ESC closes it (and is swallowed). Otherwise
                 * WI-1: in click-to-lock mode ESC frees the captured cursor
                 * (and is swallowed); else it falls through to input.c where
                 * it feeds the N64 B button (D145). */
                if (optionsOverlayIsOpen()) {
                    optionsOverlayToggle();
                } else {
                    inputReleaseCapture();
                }
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            /* WI-1: a click in the window (re)locks the cursor in
             * click-to-lock mode; a no-op otherwise. */
            if (!optionsOverlayIsOpen()) {
                inputNotifyClick();
            }
            break;
        case SDL_MOUSEWHEEL:
            if (optionsOverlayIsOpen()) {
                optionsOverlayScroll(ev.wheel.y);   /* move the selection */
            } else {
                inputPostWheel(ev.wheel.y);   /* weapon cycle */
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
            inputRescanPads();
            break;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_CLOSE) {
                sysLogPrintf(LOG_INFO, "video: window closed");
                exit(0);
            } else if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                gfx_sdl_update_cached_size();
            } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                inputSetMouseGrab(0);   /* free the cursor when alt-tabbed away */
            } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                inputSetMouseGrab(1);
            }
            break;
        default:
            break;
        }
    }

    /* Refresh the window title with the live FPS about once a second. */
    if (wmAPI && wmAPI->set_window_title) {
        static double lastTitle = 0.0;
        double now = wmAPI->get_time();
        if (now - lastTitle >= 1.0) {
            lastTitle = now;
            char title[64];
            snprintf(title, sizeof(title), "GoldenEye 007  -  %.0f fps", vidAvgFPS);
            wmAPI->set_window_title(title);
        }
    }
}

void videoSubmitCommands(Gfx *cmds)
{
    if (!initDone) {
        return;
    }
    gfx_run(cmds);
}

/* Runs from gfx_sdl_swap_buffers_begin with the composited frame still in the
 * back buffer, just before SDL_GL_SwapWindow. Reading the back buffer after
 * the swap is undefined on buffer-exchange drivers (Mesa/WSLg) -> black. */
static void videoPreSwapCapture(void)
{
    /* GE_PCDUMP="first-last" / "first-last:step" -> ./ppm/frame_NNNNNN.ppm.
     * Also honours [Debug] FrameDump in ge007.ini (env var wins). */
    const char *pcdump = configGetFrameDump();
    if (pcdump) {
        static int lo = -1, hi = 0, step = 1;
        if (lo < 0) {
            const char *v = pcdump;
            lo = 1; hi = 0x7fffffff; step = 1;
            sscanf(v, "%d-%d:%d", &lo, &hi, &step);
            if (sscanf(v, "%d-%d", &lo, &hi) != 2)
                hi = 0x7fffffff;
            GE_MKDIR("ppm");
        }
        if ((int)frames >= lo && (int)frames <= hi &&
            ((int)frames - lo) % step == 0) {
            char path[128];
            snprintf(path, sizeof(path), "ppm/frame_%06d.ppm", (int)frames);
            gfx_opengl_dump_bound_fbo((uint32_t)gfx_current_dimensions.width,
                                      (uint32_t)gfx_current_dimensions.height, path);
        }
    }

    if (screenshotReq) {
        screenshotReq = 0;
        static int shotNum = 0;
        char path[128];
        GE_MKDIR("ppm");
        snprintf(path, sizeof(path), "ppm/shot_%03d.ppm", shotNum++);
        if (gfx_opengl_dump_bound_fbo((uint32_t)gfx_current_dimensions.width,
                                      (uint32_t)gfx_current_dimensions.height, path)) {
            sysLogPrintf(LOG_INFO, "video: screenshot -> %s "
                         "(view with tools_pc/ppm2bmp.py)", path);
        } else {
            sysLogPrintf(LOG_WARNING, "video: screenshot failed");
        }
    }
}

void videoEndFrame(void)
{
    if (!initDone) {
        return;
    }
    gfx_end_frame();

    ++frames;
    ++fpsNumFrames;

    double now = wmAPI->get_time();
    if (fpsWindowStart == 0.0) {
        fpsWindowStart = now;
    }
    if (now - fpsWindowStart >= 1.0) {
        vidAvgFPS = (float)(fpsNumFrames / (now - fpsWindowStart));
        fpsNumFrames = 0;
        fpsWindowStart = now;
    }
}

float videoGetFPS(void)
{
    return vidAvgFPS;
}

/*
 * Snapshot the current window geometry into the [Window] / [Video] config
 * vars so the next configSave() persists it. Called from main.c's atexit
 * handler (runs on the host thread, which owns the window). A maximized or
 * fullscreen window keeps its last restored size/pos on disk; only the
 * flag is updated.
 */
void videoSaveWindowState(void)
{
    if (!initDone || !wmAPI) {
        return;
    }

    int32_t fs = wmAPI->get_fullscreen_state ? wmAPI->get_fullscreen_state() : 0;
    int32_t mx = wmAPI->get_maximized_state ? wmAPI->get_maximized_state() : 0;
    cfgFullscreen = fs ? 1 : 0;
    cfgWinMax = mx ? 1 : 0;

    if (!fs && !mx && wmAPI->get_dimensions) {
        uint32_t w = 0, h = 0;
        int32_t x = 0, y = 0;
        wmAPI->get_dimensions(&w, &h, &x, &y);
        if (w > 0 && h > 0) {
            cfgWinW = (int)w;
            cfgWinH = (int)h;
            cfgWinX = x < 0 ? 0 : x;
            cfgWinY = y < 0 ? 0 : y;
        }
    }
}

void videoUpdateNativeResolution(s32 w, s32 h)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    gfx_current_native_viewport.width = w;
    gfx_current_native_viewport.height = h;
    gfx_current_native_aspect = (float)w / (float)h;
}

s32 videoGetNativeWidth(void)  { return gfx_current_native_viewport.width; }
s32 videoGetNativeHeight(void) { return gfx_current_native_viewport.height; }

s32 videoCreateFramebuffer(u32 w, u32 h, s32 upscale, s32 autoresize)
{
    return gfx_create_framebuffer(w, h, upscale, autoresize);
}

void videoCopyFramebuffer(s32 dst, s32 src, s32 left, s32 top)
{
    /* assume immediate copies always read the front buffer */
    gfx_copy_framebuffer(dst, src, left, top, false);
}

void videoResetTextureCache(void)
{
    gfx_texture_cache_clear();
}
