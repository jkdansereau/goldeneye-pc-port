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

#include "bondconstants.h"
#include "bondtypes.h"
#include "game/bgfog.h"
#include "game/bondview.h"
#include "game/matrixmath.h"
#include "game/image.h"
#include "game/unk_092E50.h"

#include "skyplus.h"

int portSkyPlus = 0;   /* = Video.Visuals >= Enhanced (set by video.c, D982) */

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
    if (!portSkyPlus || gfx_skyplus_failed || !env->Clouds || env->IsWater) {
        return NULL;
    }
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
        p->sunGlow = 0.35f;
    } else if (mood == MOOD_NIGHT) {
        p->night = 1;
        dirFrom(35.0f, -0.5f, p->moonDir);
    }

    p->texGen = s_texGen;
    p->tex = s_tex;

    gSPSkyPlusExt(gdl++, p);
    return gdl;
}
