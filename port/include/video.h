#ifndef PORT_VIDEO_H
#define PORT_VIDEO_H

/*
 * Video: SDL2 window + OpenGL context + frame pacing, on top of fast3d's
 * window-manager / rendering APIs (port/fast3d).
 *
 * The game's VI (osViSetMode / osViSwapBuffer / ...) is mapped onto this
 * layer by the libultra shims; the software RSP (fast3d) renders into the GL
 * context owned here.
 */

#include <SDL.h>

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the window + GL context. Returns 0 on success. */
/* D283 Steam Deck preset (hardware-detected; GE_FAKE_DECK overrides); runs
 * right after configLoad(), at most once per ini (see video.c). */
void videoApplySteamDeckPreset(void);
/* D440: "Original N64" / "Port defaults" settings presets. An
 * action, not a mode: writes the existing config keys (see the table in
 * video.c), queues the live-apply for Video.* keys, and returns how many
 * values changed. videoPresetIsActive reports whether every key currently
 * holds that preset's value. */
enum { VIDEO_PRESET_PORT = 0, VIDEO_PRESET_N64 = 1 };
int  videoApplyPreset(int which);
int  videoPresetIsActive(int which);
int  videoInit(void);
void videoDestroy(void);

/* Frame boundary hooks, driven by the SP task shim (libultra.c). These run
 * on the game's scheduler thread. */
void videoStartFrame(void);
void videoSubmitCommands(Gfx *cmds);   /* runs the software RSP on the list */
void videoEndFrame(void);

/* Host-thread SDL event pump: keeps the window responsive (Windows only
 * dispatches messages to the creating thread) and handles quit. Called in a
 * loop from main(). Exits the process on QUIT/ESC/close. */
void videoPumpEvents(void);

/* D344: orderly quit. Any thread may request it; the render thread parks at
 * the next frame boundary (glFinish + context released) and the host thread
 * then exits. Never call exit() directly for a normal quit: the render thread
 * could be inside the GL driver (the 0x119 bugchecks). */
void videoRequestQuit(const char *why);
int  videoQuitRequested(void);
/* D443: orderly quit + relaunch (main.c atexit reads videoRestartRequested). */
void videoRequestRestart(const char *why);
int  videoRestartRequested(void);
/* D443: horizontal FOV in degrees for a Video.FovScale percent, at the
 * projection aspect the game currently uses (menu display only). */
f32  portFovHorizDegrees(s32 pct);

/* The game's native video mode (NTSC 640x480, PAL 640x400). fast3d scales
 * N64 screen coordinates into window pixels using this. */
void videoUpdateNativeResolution(s32 w, s32 h);
u32  videoGetFrameCount(void);
s32  videoGetNativeWidth(void);
s32  videoGetNativeHeight(void);

/* Offscreen framebuffers (fast3d GL FBOs) + texture cache control. */
s32  videoCreateFramebuffer(u32 w, u32 h, s32 upscale, s32 autoresize);
void videoCopyFramebuffer(s32 dst, s32 src, s32 left, s32 top);
void videoResetTextureCache(void);

/* Current FPS (measured). */
float videoGetFPS(void);

/* Apply only the GL/SDL setting named by a changed options row at the next
 * frame start. Sprite/HUD and direct-read settings need no reconfiguration. */
void videoRequestLiveConfigForKey(const char *key);

/* F10 options overlay -> window/fullscreen changes. The overlay input handler
 * runs on the scheduler thread; SDL window ops must run on the thread that
 * created the window, so these only post a request that videoPumpEvents()
 * (host thread) applies. The Get* helpers are read-only and thread-safe. */
void videoRequestWindowSize(int w, int h);
void videoRequestFullscreen(int on);
void videoRequestFullscreenMode(int exclusive);   /* D511 */
void videoRequestCenterWindow(void);               /* D511 */
void videoGetWindowSize(int *w, int *h);
/* D447: the output rect as fractions of the window (0..1, top-left origin);
 * (0,0,1,1) unless Video.AspectMode = Original is letter/pillarboxing. */
void videoGetOutputRectFrac(double *x0, double *y0, double *x1, double *y1);
void videoGetDesktopSize(int *w, int *h);
int  videoIsFullscreen(void);

/* Snapshot live window geometry into the config vars (call before configSave
 * on a clean exit). No-op if the window isn't up. */
void videoSaveWindowState(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_VIDEO_H */
