/*
 * skyplus.c -- GE+ sky (Video.Visuals = Enhanced), game side. See port/include/skyplus.h.
 *
 * The look is GE's own, with depth added: the middle cloud layer is the
 * original one (same texture, height, scale, scroll and shade formula), two
 * more layers of the same texture sit above and below it, and the level's
 * mood adds a sun (dusk), stars and a moon (night). Every level parameter
 * comes from the game's sky record (bgfog.c); the only port data is the mood
 * table below.
 *
 * Calibration (2026-11-15, Dam in Ryujinx): GE's per-vertex horizon fade, done
 * per pixel, came out ~2.5x brighter than the game shows (GE interpolates it
 * between the screen corners); GAIN scales the fade so the GE layer matches the
 * capture (clouds peaking at ~(96,117,149) over (16,48,96)).
 */
#include <ultra64.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bondconstants.h"
#include "bondtypes.h"
#include "game/bgfog.h"
#include "game/bondview.h"
#include "game/player.h"
#include "game/matrixmath.h"
#include "game/image.h"
#include "game/unk_092E50.h"

#include "skyplus.h"
#include "system.h"

int portSkyPlus = 0;

extern s32 g_SkyStageNum;

#define GAIN 0.4f
/* One cloud height and size for every level (2026-11-15, console + Ryujinx):
 * tuned on Dam, where GE's 5000 read too low and too fast and 3x that, with
 * the scroll halved, was approved. With the record's own height (Surface:
 * 10000, then x3) the same texture repeated visibly. For a flat layer only the
 * height/texture ratio is visible, so the record's height is not used. */
#define CLOUD_HEIGHT 15000.0f
#define SPEED 0.5f

enum { MOOD_DAY, MOOD_DUSK, MOOD_EXOTIC, MOOD_NIGHT, MOOD_OVERCAST };

/* Hand-set from the colours of each level's record (see the sky sheet). */
static int skyPlusMood(const struct CurrentEnvironmentRecord *env)
{
    const float lum = (0.30f * env->Red + 0.59f * env->Green + 0.11f * env->Blue) / 255.0f;

    switch (g_SkyStageNum % 100) {
    case LEVELID_DAM:
    case LEVELID_FRIGATE:
        return MOOD_DAY;
    case LEVELID_EGYPT:
        return lum < 0.02f ? MOOD_NIGHT : MOOD_DAY;   /* the alt record is black */
    case LEVELID_STREETS:
    case LEVELID_SURFACE:
    case LEVELID_BUNKER1:   /* Surface 1's sky, see skyPlusEnv */
    case LEVELID_TEMPLE:
    case LEVELID_CRADLE:
        return MOOD_DUSK;
    case LEVELID_COMPLEX:
    case LEVELID_CITADEL:
        return MOOD_EXOTIC;
    case LEVELID_RUNWAY:
        return MOOD_OVERCAST;
    default:
        return lum < 0.10f ? MOOD_NIGHT : MOOD_DAY;
    }
}

static void dirFrom(float elDeg, float az, float out[3])
{
    const float e = elDeg * (float)M_PI / 180.0f;
    out[0] = cosf(e) * sinf(az);
    out[1] = sinf(e);
    out[2] = cosf(e) * cosf(az);
}

/* World ray through a point of the player's screen, as skyGetWorldPosFromScreenPos
 * without the WaterConcavity offset (the water fan's, not the camera's). */
static void screenRay(f32 x, f32 y, coord3d *out)
{
    coord2d c;
    c.x = getPlayer_c_screenleft() + x;
    c.y = getPlayer_c_screentop() + y;
    transformAndNormalizeByLength2Dto3D(&c, out, 100.0f);
    mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), out);
}

static unsigned char s_tex[SKYPLUS_TEX_W * SKYPLUS_TEX_H];
static int s_texGen;
static int s_texNum = -1;

/* The cloud image as 8-bit intensity. texLoad writes the loaded address back
 * into the word it is given, so it gets a copy of the image number (as
 * texLoadFromTextureNum does), never skywaterimages[].index. */
static int skyPlusTexture(s32 imageId)
{
    struct tex *t;
    const u8 *src;
    s32 x, y, pitch;
    s32 num;

    if (imageId != 0) {
        return 0;   /* every level uses image 0 (bg/clouds, IA8 64x64) */
    }
    num = IMAGE_CLOUDS_GRAYSCALE;
    if (num == s_texNum) {
        return 1;
    }
    texLoadFromTextureNum(num, NULL);
    t = texFindInPool(num, NULL);
    if (t == NULL || t->data == NULL || t->width != SKYPLUS_TEX_W || t->height != SKYPLUS_TEX_H) {
        return 0;
    }
    src = t->data;
    pitch = (t->width + 7) & ~7;   /* texChannelsToPixels IA8 row pitch */
    for (y = 0; y < SKYPLUS_TEX_H; y++) {
        for (x = 0; x < SKYPLUS_TEX_W; x++) {
            s_tex[y * SKYPLUS_TEX_W + x] = (unsigned char)((src[y * pitch + x] >> 4) * 17);   /* I of I4A4 */
        }
    }
    s_texNum = num;
    s_texGen++;
    return 1;
}

#if defined(PLATFORM_SWITCH) || defined(GE_VULKAN)
/* GE+ water (Switch, and the Vulkan backend on PC): the sea's wave heights, a tiling fbm (value noise,
 * octaves of 4..32 cells over the tile) made once. GE's own water texture
 * (bg/water1, I4) was tried first: mostly flat with scattered spots, it reads
 * as mud bubbles at any scale (Ryujinx, 2026-11-17). */
static unsigned char s_wtex[SKYPLUS_WTEX_W * SKYPLUS_WTEX_H * 4];
static int s_wtexGen;
static int s_wtexOk = -1;   /* -1 = not made yet */
#define WTEX_SLOPE_K 6.0f   /* slope scale in G, B (skycommon.glsl texSlopeAt decodes it) */

static float wnoiseLattice(int x, int y, int n)
{
    unsigned h = (unsigned)(((x % n + n) % n) * 374761393u + ((y % n + n) % n) * 668265263u + (unsigned)n * 2246822519u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}

static float wnoise(float x, float y, int n)   /* x, y in 0..n cells, wraps at n */
{
    const int ix = (int)x, iy = (int)y;   /* x, y >= 0 */
    float fx = x - (float)ix, fy = y - (float)iy;
    float a, b, c, d;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    a = wnoiseLattice(ix, iy, n);
    b = wnoiseLattice(ix + 1, iy, n);
    c = wnoiseLattice(ix, iy + 1, n);
    d = wnoiseLattice(ix + 1, iy + 1, n);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

/* The level's water settings: tint (x the water's colour), swell (the big
 * waves), chop (the small ones, and how much they blur the far reflections),
 * speed. Defaults below; $S/water/<level>.ini overrides them, read when the
 * level changes, so they can be tuned on the SD card without a rebuild:
 *     Tint = 1.0, 1.0, 1.0
 *     Swell = 1.0
 *     Chop = 1.0
 *     Speed = 1.0                                                          */
struct WaterSettings { float tint[3], swell, chop, speed; };

static const char *waterLevelName(int stage)
{
    switch (stage) {
    case LEVELID_DAM:     return "dam";
    case LEVELID_FRIGATE: return "frigate";
    case LEVELID_CAVERNS: return "caverns";
    case LEVELID_COMPLEX: return "complex";
    default:              return NULL;
    }
}

static void waterSettingsLoad(int stage, struct WaterSettings *w)
{
    char path[64], line[128];
    const char *name = waterLevelName(stage);
    FILE *f;

    w->tint[0] = w->tint[1] = w->tint[2] = 1.0f;
    w->swell = w->chop = w->speed = 1.0f;
    if (stage == LEVELID_FRIGATE) {   /* open sea: a real swell, not an oil-flat sea */
        w->swell = 2.0f;
        w->chop = 1.2f;
    } else if (stage == LEVELID_DAM) {   /* a lake: calm */
        w->swell = 0.3f;
        w->chop = 0.6f;
        w->speed = 0.7f;
    }
    if (name == NULL) {
        return;
    }
    snprintf(path, sizeof path, "$S/water/%s.ini", name);
    f = fopen(sysResolvePath(path), "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof line, f)) {
        char key[32];
        float a, b, c;
        if (sscanf(line, " %31[A-Za-z] = %f , %f , %f", key, &a, &b, &c) == 4 && !strcmp(key, "Tint")) {
            w->tint[0] = a; w->tint[1] = b; w->tint[2] = c;
        } else if (sscanf(line, " %31[A-Za-z] = %f", key, &a) == 2) {
            if (!strcmp(key, "Swell")) w->swell = a;
            else if (!strcmp(key, "Chop")) w->chop = a;
            else if (!strcmp(key, "Speed")) w->speed = a;
        }
    }
    fclose(f);
    sysLogPrintf(LOG_NOTE, "GE+ water: %s (tint %.2f,%.2f,%.2f swell %.2f chop %.2f speed %.2f)", path,
                 w->tint[0], w->tint[1], w->tint[2], w->swell, w->chop, w->speed);
}

static int skyPlusWaterTexture(void)
{
    float lo = 1e9f, hi = -1e9f;
    static float v[SKYPLUS_WTEX_W * SKYPLUS_WTEX_H];
    s32 x, y, o;

    if (s_wtexOk >= 0) {
        return s_wtexOk;
    }
    for (y = 0; y < SKYPLUS_WTEX_H; y++) {
        for (x = 0; x < SKYPLUS_WTEX_W; x++) {
            float sum = 0.0f, amp = 1.0f;
            int n = 4;
            for (o = 0; o < 4; o++, n *= 2, amp *= 0.5f) {
                sum += amp * wnoise((float)x * n / SKYPLUS_WTEX_W, (float)y * n / SKYPLUS_WTEX_H, n);
            }
            v[y * SKYPLUS_WTEX_W + x] = sum;
            if (sum < lo) lo = sum;
            if (sum > hi) hi = sum;
        }
    }
    /* R = height; G, B = its slope to the next texel in x, y (wrapping),
     * precomputed so the shader reads one texel per sample, not three (GPU
     * cost on the console: 36 fetches per sea pixel were too many) */
    for (x = 0; x < SKYPLUS_WTEX_W * SKYPLUS_WTEX_H; x++) {
        v[x] = (v[x] - lo) / (hi - lo + 1e-6f);
    }
    for (y = 0; y < SKYPLUS_WTEX_H; y++) {
        for (x = 0; x < SKYPLUS_WTEX_W; x++) {
            const float h = v[y * SKYPLUS_WTEX_W + x];
            const float dx = v[y * SKYPLUS_WTEX_W + (x + 1) % SKYPLUS_WTEX_W] - h;
            const float dy = v[((y + 1) % SKYPLUS_WTEX_H) * SKYPLUS_WTEX_W + x] - h;
            unsigned char *o = &s_wtex[(y * SKYPLUS_WTEX_W + x) * 4];
            float g = 127.5f + dx * WTEX_SLOPE_K * 255.0f, b = 127.5f + dy * WTEX_SLOPE_K * 255.0f;
            o[0] = (unsigned char)(h * 255.0f + 0.5f);
            o[1] = (unsigned char)(g < 0.0f ? 0.0f : g > 255.0f ? 255.0f : g + 0.5f);
            o[2] = (unsigned char)(b < 0.0f ? 0.0f : b > 255.0f ? 255.0f : b + 0.5f);
            o[3] = 255;
        }
    }
    s_wtexGen++;
    s_wtexOk = 1;
    return 1;
}
#endif

/* Per-player parameter blocks, double-buffered over frames: fast3d copies the
 * block when it decodes the command, which may be a frame after this call. */
#define SKYPLUS_SLOTS 16
static PortSkyPlus s_blocks[SKYPLUS_SLOTS];
static int s_next;

static float s_lastGeOfs = -1.0f;
static double s_time;   /* continuous GE scroll units (g_SkyCloudOffset unwrapped) */

static float wrapf(float v, float m)
{
    v = fmodf(v, m);
    return v < 0.0f ? v + m : v;
}

/* Bunker 1 has no sky record for one player (bgfog.c: only the 2-4 player
 * ones), so it falls back to fog_tables2's LEVELID_NONE: a flat navy fill, no
 * clouds, seen at its exit and through its windows. The bunker sits under
 * Surface 1 (the mission before it), so GE+ gives it Surface 1's sky: the
 * LEVELID_SURFACE record's sky and cloud colours and height (bgfog.c). Only
 * GE+ sees this copy; fog, draw distance and the game's own sky are unchanged. */
static const struct CurrentEnvironmentRecord *skyPlusEnv(const struct CurrentEnvironmentRecord *env)
{
    static struct CurrentEnvironmentRecord s_bunker1;

    if (g_SkyStageNum != LEVELID_BUNKER1 || env->Clouds || env->IsWater) {
        return env;
    }
    s_bunker1 = *env;
    s_bunker1.Red = 0x60;
    s_bunker1.Green = 0x60;
    s_bunker1.Blue = 0x80;
    s_bunker1.Clouds = 1;
    s_bunker1.CloudRepeat = 10000.0f;
    s_bunker1.SkyImageId = 0;
    s_bunker1.CloudRed = 240.0f;
    s_bunker1.CloudGreen = 120.0f;
    s_bunker1.CloudBlue = 30.0f;
    return &s_bunker1;
}

Gfx *skyPlusRender(Gfx *gdl)
{
    const struct CurrentEnvironmentRecord *env = skyPlusEnv(fogGetCurrentEnvironmentp());
    PortSkyPlus *p;
    coord3d r[5];
    coord3d *eye;
    float fwd[3], len;
    int i, mood;
    /* per extra layer: wind speed and size relative to the GE layer */
    static const float kSpeed[3] = { 0.35f, 1.0f, 1.9f };
    static const float kScale[3] = { 1.7f, 1.0f, 0.45f };

#ifdef SKYPLUS_TEST_ORIGINAL
    return NULL;   /* A/B test build: GE's sky whatever the ini says */
#endif
#if defined(PLATFORM_SWITCH)
    /* water levels: the sea is drawn by the same pass (GE+ water) */
    if (!portSkyPlus || gfx_skyplus_failed || !env->Clouds) {
        return NULL;
    }
#else
    /* PC: only a backend that draws the sea (Vulkan sets gfx_skyplus_sea) gets the water levels */
    if (!portSkyPlus || gfx_skyplus_failed || !env->Clouds || (env->IsWater && !gfx_skyplus_sea)) {
        return NULL;
    }
#endif
    if (!skyPlusTexture(env->SkyImageId)) {
        return NULL;
    }

    p = &s_blocks[s_next];
    s_next = (s_next + 1) % SKYPLUS_SLOTS;
    memset(p, 0, sizeof *p);

    /* corner rays, put on the image plane (distance 1 along the view axis) so
     * the GL pass can interpolate them linearly across the screen */
    screenRay(0.0f, 0.0f, &r[0]);
    screenRay(getPlayer_c_screenwidth(), 0.0f, &r[1]);
    screenRay(0.0f, getPlayer_c_screenheight(), &r[2]);
    screenRay(getPlayer_c_screenwidth(), getPlayer_c_screenheight(), &r[3]);
    screenRay(getPlayer_c_screenwidth() * 0.5f, getPlayer_c_screenheight() * 0.5f, &r[4]);
    len = sqrtf(r[4].x * r[4].x + r[4].y * r[4].y + r[4].z * r[4].z);
    if (len < 1e-6f) {
        return NULL;
    }
    fwd[0] = r[4].x / len;
    fwd[1] = r[4].y / len;
    fwd[2] = r[4].z / len;
    for (i = 0; i < 4; i++) {
        const float d = r[i].x * fwd[0] + r[i].y * fwd[1] + r[i].z * fwd[2];
        if (d < 1e-6f) {
            return NULL;
        }
        p->corner[i][0] = r[i].x / d;
        p->corner[i][1] = r[i].y / d;
        p->corner[i][2] = r[i].z / d;
    }

    eye = bondviewGetCurrentPlayersPosition();
    p->eye[0] = eye->x;
    p->eye[1] = eye->y;
    p->eye[2] = eye->z;
    p->cloudY = eye->y + CLOUD_HEIGHT;
    p->sky[0] = env->Red / 255.0f;
    p->sky[1] = env->Green / 255.0f;
    p->sky[2] = env->Blue / 255.0f;
    p->cloud[0] = env->CloudRed / 255.0f;
    p->cloud[1] = env->CloudGreen / 255.0f;
    p->cloud[2] = env->CloudBlue / 255.0f;
    p->gain = GAIN;
    {
        /* the horizon band's top: the elevation of the level's sky height
         * (the record's skyheight, GE's cloud plane) seen at the fog's far
         * distance (bgfog.c g_ScaledFarFogIntensity, Fog x2 included): nothing
         * is drawn beyond it, so nothing pops in above it */
        extern f32 g_ScaledFarFogIntensity;
        const float D = g_ScaledFarFogIntensity;
        const float Hs = env->CloudRepeat;
        /* no fog (D left at FLT_MAX: Frigate): nothing pops in, GE's clouds
         * reach the horizon, the band stays at its floor */
        float b = (D > 1.0f && D < 1e20f && Hs > 0.0f) ? Hs / sqrtf(D * D + Hs * Hs) : 0.0f;
        if (b < 0.02f) b = 0.02f;
        if (b > 0.22f) b = 0.22f;
        p->band = b;
    }
    p->texScale = 1.0f;   /* kept at the size tuned on Dam (a 25..2000 % slider settled on 100) */

    /* scroll: GE's layer follows g_SkyCloudOffset exactly; the others run at
     * their own speed on the unwrapped time (no jump at the 4096 wrap) */
    if (s_lastGeOfs >= 0.0f) {
        float d = g_SkyCloudOffset - s_lastGeOfs;
        if (d < -2048.0f) d += 4096.0f;
        if (d > 0.0f && d < 2048.0f) s_time += d;
    }
    s_lastGeOfs = g_SkyCloudOffset;
    p->geOffset = g_SkyCloudOffset;
    for (i = 0; i < 3; i++) {
        /* tc units -> texels (S10.5), then into the layer's own texel scale */
        const float tex = (float)fmod(s_time * SPEED * kSpeed[i] / 32.0, 64.0 * 4096.0) / kScale[i];
        p->layerOfs[i][0] = 0.0f;
        p->layerOfs[i][1] = wrapf(tex, 64.0f);
    }
    p->layerOfs[1][1] = wrapf(g_SkyCloudOffset * SPEED / 32.0f, 64.0f);   /* 4096 * 0.5 / 32 = 64: still seamless at the wrap */

    mood = skyPlusMood(env);
    if (mood == MOOD_DUSK || mood == MOOD_EXOTIC) {
        /* the redder the light, the lower the sun (yellow 20 deg, deep red 6: just
         * above the horizon band of the GL pass, its halo still shows) */
        float col[3], red, mx;
        col[0] = p->sky[0] + p->cloud[0] * (1.0f - p->sky[0]);
        col[1] = p->sky[1] + p->cloud[1] * (1.0f - p->sky[1]);
        col[2] = p->sky[2] + p->cloud[2] * (1.0f - p->sky[2]);
        mx = col[1] > col[2] ? col[1] : col[2];
        red = col[0] > 1e-3f ? (col[0] - mx) / col[0] : 0.0f;
        if (red < 0.0f) red = 0.0f;
        if (red > 1.0f) red = 1.0f;
        /* azimuth: Surface's shadows put its sun 90 deg to the right of the
         * default, 12 deg back left, 6 back right (console check 2026-11-15;
         * right-handed world: right = -) */
        dirFrom(20.0f + (6.0f - 20.0f) * red,
                (g_SkyStageNum % 100) == LEVELID_SURFACE ? 0.3f + (-90.0f + 12.0f - 6.0f) * (float)M_PI / 180.0f : 0.3f, p->sunDir);
        memcpy(p->sunCol, col, sizeof col);
        p->sunGlow = 0.4f + 0.35f * red;
    } else if (mood == MOOD_DAY) {
        /* Dam: 120 deg to the right of the default to match its shadows (2026-11-15) */
        dirFrom(50.0f, (g_SkyStageNum % 100) == LEVELID_DAM ? 0.6f - 120.0f * (float)M_PI / 180.0f : 0.6f, p->sunDir);
        p->sunCol[0] = 1.0f;
        p->sunCol[1] = 0.97f;
        p->sunCol[2] = 0.9f;
        if ((g_SkyStageNum % 100) == LEVELID_FRIGATE) {
            /* Frigate's navy sky with pale clouds is the blue hour: the sun has
             * already set, 4-6 deg under the horizon in photos of such skies, so
             * no disc and no glints, only a pale glow low over the horizon on
             * its side. Its light is the level's own lit-cloud colour (GE's
             * cloud shade at full fade), never a warm tint. */
            dirFrom(-5.0f, 0.6f, p->sunDir);
            for (i = 0; i < 3; i++) {
                p->sunCol[i] = p->sky[i] + p->cloud[i] * (1.0f - p->sky[i]);
            }
        }
        p->sunGlow = 0.35f;
    } else if (mood == MOOD_NIGHT) {
        p->night = 1;
        dirFrom(35.0f, -0.5f, p->moonDir);
    }

    p->texGen = s_texGen;
    p->tex = s_tex;
#if defined(PLATFORM_SWITCH) || defined(GE_VULKAN)
    p->time = (float)(s_time / 60.0);   /* GE ticks (60 per second) */
    {
        static int s_wsStage = -1;
        static struct WaterSettings s_ws;
        const int stage = g_SkyStageNum % 100;
        if (stage != s_wsStage) {
            waterSettingsLoad(stage, &s_ws);
            s_wsStage = stage;
        }
        p->swell = s_ws.swell;
        p->chop = s_ws.chop;
        p->speed = s_ws.speed;
        /* no sea: water[] is the tint of the level's own water draws */
        for (i = 0; i < 3; i++) {
            p->water[i] = s_ws.tint[i];
        }
        skyPlusWaterTexture();   /* the wave heights, for the sea and the water draws */
        p->waterTexGen = s_wtexGen;
        p->waterTex = s_wtex;
    }
    if (env->IsWater) {
        p->sea = 1;
        p->seaY = env->WaterRepeat;
        /* the sea's body: the sky's own colour, desaturated and darkened, as
         * open sea reads under such a sky (the film's harbour shots: slate
         * navy, darker than the sky, never GE's turquoise); its brightness
         * toward the horizon comes from the sky reflection (Fresnel) */
        {
            const float lum = 0.30f * p->sky[0] + 0.59f * p->sky[1] + 0.11f * p->sky[2];
            for (i = 0; i < 3; i++) {
                p->water[i] *= (p->sky[i] + (lum - p->sky[i]) * 0.4f) * 0.9f;
            }
        }
    }
#endif

    {   /* the far sea is baked on the level's first frame, before its sky, from the player's eye at the start (set when the level is
         * set up, before the intro): so no image is ever shown without it, and the intro's flying camera is not where it is baked from */
        extern enum CAMERAMODE g_CameraMode;
        static enum CAMERAMODE prev = CAMERAMODE_NONE;
        const coord3d *start = &g_CurrentPlayer->field_488.pos;
        p->bakeReady = g_CameraMode != CAMERAMODE_NONE && g_CameraMode != CAMERAMODE_FADE_TO_TITLE;
        p->bakeReset = g_CameraMode == CAMERAMODE_INTRO && prev != CAMERAMODE_INTRO;
        prev = g_CameraMode;
        p->bakeEye[0] = start->x; p->bakeEye[1] = start->y; p->bakeEye[2] = start->z;
    }
    gSPSkyPlusExt(gdl++, p);
    return gdl;
}
