#ifndef PORT_SKYPLUS_H
#define PORT_SKYPLUS_H

/* GE+ sky (Video.Visuals = Enhanced): a per-pixel replacement for sky.c's cloud plane.
 *
 * The game side (port/src/skyplus.c, called from the top of skyRender) fills a
 * PortSkyPlus with the level's sky record, the camera (the world rays through
 * the four corners of the player's screen, from the game's own
 * skyGetWorldPosFromScreenPos math) and the cloud texture, then emits one
 * G_SKYPLUS_EXT command. fast3d (gfx_pc.cpp) copies it with the current
 * viewport and aspect mapping and records a GL pass (gfx_opengl.cpp) that
 * draws the whole viewport in one fullscreen triangle:
 *   - layer 1 = GE's own cloud layer (same height, texture scale, scroll and
 *     shade formula: sky + cloud * (1 - sky) * (1 - frac));
 *   - layer 0 = the same texture higher, larger, slower, rotated, fainter;
 *   - layer 2 = lower, smaller, faster, fainter;
 *   - a light shading toward the sun/moon, a sun glow on dusk levels, stars
 *     and a moon on night levels, both hidden by the clouds in front of them.
 * Below the horizon the sky colour, as GE's clear colour.
 *
 * w0 = G_SKYPLUS_EXT << 24, w1 = const PortSkyPlus *. */
#define G_SKYPLUS_EXT 0x48

#define SKYPLUS_TEX_W 64
#define SKYPLUS_TEX_H 64

typedef struct PortSkyPlus {
    float corner[4][3];   /* world-space rays to the image plane: TL, TR, BL, BR (not normalised) */
    float eye[3];
    float cloudY;         /* the cloud shell's height at the eye (world y) */
    float sky[3];         /* sky = fog = horizon colour, 0..1 */
    float cloud[3];       /* CloudRed/Green/Blue, 0..1 */
    float geOffset;       /* g_SkyCloudOffset (Vtx.tc S10.5 units, wraps at 4096) */
    float layerOfs[3][2]; /* per-layer texture scroll in texels, wrapped (layer 1 = GE) */
    float gain;           /* GE horizon-fade calibration (see skyplus.c) */
    float texScale;       /* world -> texture scale factor */
    float sunDir[3];
    float sunCol[3];
    float sunGlow;        /* 0 = no sun */
    float moonDir[3];
    int night;            /* stars + moon */
    int texGen;           /* bumps when tex changes */
    const unsigned char *tex;   /* SKYPLUS_TEX_W x SKYPLUS_TEX_H intensity, 0..255 */
} PortSkyPlus;

#define gSPSkyPlusExt(pkt, ptr)                                       \
    do {                                                              \
        Gfx *_g = (Gfx *)(pkt);                                       \
        _g->words.w0 = ((uintptr_t)G_SKYPLUS_EXT << 24);              \
        _g->words.w1 = GBI_W1(ptr);                                   \
    } while (0)

#ifdef __cplusplus
extern "C" {
#endif

/* Video.Visuals = Enhanced (0 = GE's sky, 1 = GE+). */
extern int portSkyPlus;

#ifndef __cplusplus
/* sky.c: returns the advanced gdl when GE+ drew the sky, NULL to run GE's path
 * (option off, no clouds, a water level, an unknown cloud image). */
Gfx *skyPlusRender(Gfx *gdl);
#endif

/* fast3d -> GL backend: stage one draw (decoder thread) and replay it (GL
 * thread). vp = the GL viewport, aspA/aspB = the decoder's clip-x mapping
 * (x_out = aspA * x_game + aspB). */
int gfx_opengl_skyplus_stage(const PortSkyPlus *p, const int vp[4], float aspA, float aspB);
void gfx_opengl_skyplus_pass(int slot);
/* Set by the GL thread when the GE+ program fails to build: GE's sky is drawn instead. */
extern volatile int gfx_skyplus_failed;

#ifdef __cplusplus
}
#endif

#endif
