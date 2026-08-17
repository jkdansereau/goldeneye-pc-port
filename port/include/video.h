#ifndef PORT_VIDEO_H
#define PORT_VIDEO_H

/*
 * Video: SDL2 window + OpenGL context + frame pacing.
 * Modelled on the PD port's port/include/video.h.
 *
 * The game's VI (osViSetMode / osViVSyncCallback / osViWaitVSync) is mapped
 * onto this layer. The software RSP (fast3d) renders into the GL context that
 * this layer owns.
 */

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the window + GL context. Returns 0 on success. */
int  videoInit(void);
void videoDestroy(void);

/* Frame boundary hooks, driven by the port scheduler (gesched.c). */
void videoStartFrame(void);
void videoSubmitCommands(Gfx *cmds);   /* runs the software RSP on the list */
void videoEndFrame(void);

/* VSync / framerate control. */
void videoSetVsync(int enabled);
void videoSetFramerateLimit(int fps);  /* 0 = unlimited */

/* Window state queries (kept in sync with the options menu). */
int  videoGetFullscreen(void);
void videoSetFullscreen(int enabled);
int  videoGetMaximize(void);
void videoSetMaximize(int enabled);

/* Current FPS (measured). */
float videoGetFPS(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_VIDEO_H */
