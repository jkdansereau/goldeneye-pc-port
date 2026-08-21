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

/* The game's native video mode (NTSC 640x480, PAL 640x400). fast3d scales
 * N64 screen coordinates into window pixels using this. */
void videoUpdateNativeResolution(s32 w, s32 h);
s32  videoGetNativeWidth(void);
s32  videoGetNativeHeight(void);

/* Offscreen framebuffers (fast3d GL FBOs) + texture cache control. */
s32  videoCreateFramebuffer(u32 w, u32 h, s32 upscale, s32 autoresize);
void videoCopyFramebuffer(s32 dst, s32 src, s32 left, s32 top);
void videoResetTextureCache(void);

/* Current FPS (measured). */
float videoGetFPS(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_VIDEO_H */
