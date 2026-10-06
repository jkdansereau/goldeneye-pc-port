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
#include <math.h>

#if defined(_WIN32)
#include <direct.h>
#define GE_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define GE_MKDIR(p) mkdir(p, 0777)
#endif

#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include <bondconstants.h>   /* LEVELID_TITLE (D416); MinGW's SDL.h pulled it in by accident, glibc's doesn't */

#include "platform.h"
#include "system.h"
#include "config.h"
#include "video.h"
#include "input.h"
#include "optionsoverlay.h"
#include "audio.h"

#include "../fast3d/gfx_api.h"
#include "../fast3d/gfx_sdl.h"
#include "../fast3d/gfx_opengl.h"

/* GE's internal resolution before the game's first osViSetMode (which then
 * sets the real CFB size): NTSC 640x480; PAL is taller, not shorter -- the EU
 * game renders 320x269 at 50 Hz with no borders (TCRF), so 2x = 640x538 (D487). */
#ifdef REFRESH_PAL
#define GE_NATIVE_W 640
#define GE_NATIVE_H 538
#else
#define GE_NATIVE_W 640
#define GE_NATIVE_H 480
#endif

static struct GfxWindowManagerAPI *wmAPI;
static struct GfxRenderingAPI *renderingAPI;
static int initDone = 0;

/*
 * [Video] ge007.ini knobs. Draw/LOD and MSAA defaults favour a clean picture
 * at modern resolutions without requiring 4x anti-aliasing.
 */
static int cfgVSync         = 1;   /* swap interval: 0 = off, 1 = on            */
static int cfgFpsCap        = 60;  /* frame cap in fps; 0 = uncapped (vsync); menu only exposes 30/60 */
static int cfgMSAA          = 2;   /* 1/2/4/8/16 samples; 2x default is lighter on low-end GPUs */
static int cfgTexFilter     = 1;   /* 0 = nearest, 1 = bilinear (default), 2 = N64 3-point + trilinear, 3 = trilinear (Trilinear option, playtest 2026-10-03) */
static int cfgFixMipTex     = 1;   /* RC2: clip mip-contaminated texture uploads to base height */
static int cfgDetailBaseTile = 1;  /* D236: TEXTURETYPE_DETAIL -> sample the base image, not the detail tile */
static int cfgWrapFix       = 0;   /* D74 sub-tile UV pre-wrap + RC3/D167 non-PoT mask-period wrap (opt-in; GE_WRAPFIX env overrides) */
static int cfgFovScale      = 100; /* D211: percent of the original vertical FOV; 100 = unchanged (byte-identical) */
static int cfgWidescreenAuto = 1;  /* WIDESCREEN-FOV-PLAN Phase 4: auto-scale vertical FOV by window aspect ratio; on by default, no-op at 4:3 */
static int cfgNativeWidescreen = 1; /* D334 (WIDESCREEN-FOV-PLAN Phase 2): project the world at the real window aspect (Hor+); no-op at 4:3 */
static int cfgHudScale = 100;        /* D226: HUD text/ammo scale %, 100 = original (no emission) */
static int cfgDrawDistance      = 250; /* % of authored far clip; 250% is the UI's 50/100 midpoint */
static int cfgDrawDistanceAutoFov = 0; /* legacy ini option, no longer exposed in the menu */
static int cfgLodDistance         = 250; /* % of authored geometry LOD distance; 50/100 in the UI */
static int cfgLodDistanceAutoFov  = 0; /* legacy ini option, no longer exposed in the menu */
static int cfgAniso         = 4;   /* D212: anisotropic filtering samples; 4 = the value fast3d already applied (no visual delta at default) */
static int cfgSafeAreaCrop  = 1;   /* crop the N64 TV-overscan safe-area margin (visible as black top/bottom bars on PC) instead of showing it; on by default */
static int cfgAspectMode    = 0;   /* D447: 0 = Window (fill the window), 1 = Original (pillar/letterbox to the console aspect: 4:3, or 16:9 while the game's Ratio option is 16:9). D508: 2 = 16:9, 3 = 21:9 (forced) */
static int cfgFullscreen    = 0;   /* 0 = windowed, 1 = borderless fullscreen   */
static int cfgFullscreenMode = 0;  /* D511: fullscreen flavour, 0 = borderless desktop, 1 = exclusive */
static int cfgDeckPresetApplied = 0; /* D283: 1 once the Steam Deck preset has been considered */
static int cfgLowEndConsidered = 0;  /* D482: 1 once the low-end GPU defaults have been considered */

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

/* D216: Game.SkipIntro — read once in src/game/lv.c at title-stage load.
 * 0 (default) = the legal screen + logo attract sequence plays as normal. */
s32 portSkipIntro = 0;

/* D232: Game.NoHitFlash — the community "no damage flash" toggle (route-(b)
 * hook in src/game/bondview2.c currentPlayerSetFadeColour). 0 (default) =
 * original damage flash. */
s32 portNoHitFlash = 0;

/* v0.4.0 M2 (D373/D379): in-game crosshair on/off + tint
 * (gunfire.c gunDrawSight, #ifdef PORT). Default keeps the original
 * authored red sprite; hide is opt-in. */
s32 portCrosshairHide = 0;
static int cfgCrosshairColor = 0;   /* 0 = authored sprite; 1..7 = presets; 8 = custom RGB */
static int cfgCrosshairRed = 255, cfgCrosshairGreen = 255, cfgCrosshairBlue = 255;
static int cfgCrosshairSize = 100;  /* 100% retains the original 32x32 drawing */
static int cfgCrosshairStyle = 0;   /* 0 = original; 1 = unused beta asset */
static int cfgCrosshairAlpha = 100; /* D511: % of the original sprite alpha (0x6E); 100 = untouched */
static int cfgCrosshairHealth = 0;  /* D511: 1 = tint follows health (PD ramp); 0 = Crosshair color rules */
extern float portCrosshairHealthRatio(void);   /* input.c: health + armour, 0..2 */

int portCrosshairStyle(void) { return cfgCrosshairStyle; }
float portCrosshairScale(void) { return cfgCrosshairSize / 100.0f; }

/* Index 0 leaves the authored RED sprite untouched (white env multiplier);
 * index 7 is actual white via its alpha silhouette. */
/* The authored RGBA32 reticle is RED (confirmed from the decoded 32x32
 * IMAGE_CROSSHAIR1 texture). G_CC_FADEA multiplies TEXEL0.rgb by env.rgb;
 * no amount of blue/cyan tint can recover channels absent from the texture.
 * For the opt-in colours, substitute a combiner that uses TEXEL0 alpha as
 * the grayscale reticle mask and ENVIRONMENT for the requested RGB. The
 * alpha expression is unchanged: texture alpha * env alpha (0x6e).
 * The call site points at display_image_at_position's env-color command;
 * its immediately following command is the combine mode. Original/0
 * skips this rewrite, keeping the original game's DL byte-identical. */
void portCrosshairApplyTintCombine(Gfx *envCommand)
{
    if (!envCommand) return;
    if (cfgCrosshairAlpha != 100) {
        /* D511: the env colour command carries the sprite alpha (0x6E in the
         * call site); scale just that byte, keep the tint. Original (100) skips this. */
        s32 r, g, b;
        portCrosshairTint(&r, &g, &b);
        gDPSetEnvColor(envCommand, r, g, b, (0x6E * cfgCrosshairAlpha + 50) / 100);
    }
    if (cfgCrosshairColor == 0 && !cfgCrosshairHealth) return;
    gDPSetCombineLERP(envCommand + 1,
        ENVIRONMENT, 0, TEXEL0_ALPHA, 0, TEXEL0, 0, ENVIRONMENT, 0,
        ENVIRONMENT, 0, TEXEL0_ALPHA, 0, TEXEL0, 0, ENVIRONMENT, 0);
}

/* D511: PD's health ramp (pd_port src/game/sight.c sightGetCrosshairHealthColor,
 * "on green" variant; MIT). ratio = health + armour, 0..2 (GE: both 0..1). */
static void crosshairHealthTint(float ratio, s32 *r, s32 *g, s32 *b)
{
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 2.0f) ratio = 2.0f;
    int red, green, blue;
    if (ratio < 0.2f)      { red = 255; green = 0; blue = 0; }
    else if (ratio < 0.6f) { red = 255; green = (int)(255.0f * ((ratio - 0.2f) / 0.4f)); blue = 0; }
    else if (ratio < 1.0f) { red = (int)(255.0f * ((ratio - 0.6f) / 0.4f)); green = 255; blue = 0; }
    else                   { red = 0; green = 255; blue = (int)(255.0f * (ratio - 1.0f)); }
    *r = red; *g = green; *b = blue;
}

void portCrosshairTint(s32 *r, s32 *g, s32 *b)
{
    /* D511: health colour wins over Crosshair color (PD: SIGHT_COLOUR uses the
     * health ramp instead of the chosen colour). Only reached from gunDrawSight
     * (live player) and the preview (which passes through here too: full health). */
    if (cfgCrosshairHealth) {
        crosshairHealthTint(portCrosshairHealthRatio(), r, g, b);
        return;
    }
    static const unsigned char kTints[8][3] = {
        { 0xFF, 0xFF, 0xFF }, /* Original red sprite (identity multiplier) */
        { 0x40, 0xFF, 0x40 }, /* Green */
        { 0xFF, 0x40, 0x40 }, /* Red */
        { 0x40, 0x40, 0xFF }, /* Blue */
        { 0xFF, 0xFF, 0x40 }, /* Yellow */
        { 0x40, 0xFF, 0xFF }, /* Cyan */
        { 0xFF, 0x40, 0xFF }, /* Magenta */
        { 0xFF, 0xFF, 0xFF }, /* Actual white, through alpha silhouette */
    };
    if (cfgCrosshairColor == 8) {
        *r = cfgCrosshairRed;
        *g = cfgCrosshairGreen;
        *b = cfgCrosshairBlue;
        return;
    }
    int index = (cfgCrosshairColor >= 0 && cfgCrosshairColor <= 7)
        ? cfgCrosshairColor : 0;
    *r = (s32)kTints[index][0];
    *g = (s32)kTints[index][1];
    *b = (s32)kTints[index][2];
}

/* The original sprite is red despite its white identity multiplier. Show a
 * representative red swatch for it rather than misleadingly showing white. */
void portCrosshairPreview(s32 *r, s32 *g, s32 *b)
{
    if (cfgCrosshairColor == 0 && !cfgCrosshairHealth) {
        *r = 255; *g = 40; *b = 40;
    } else {
        portCrosshairTint(r, g, b);
    }
}

/* D257: Game.AllUnlocked — everything-unlocked goodie, OFF by default
 * (faithful N64 progression: levels unlock as you complete them). Consumed
 * once at startup by main.c, which sets the game's own RAM unlock flags
 * (debug_enable_all_levels_flag / debug_007_unlock_flag in
 * src/game/debugmenu_handler.c, live because the PC build defines
 * LEFTOVERDEBUG) — port-layer memory writes only, no game-code edits.
 * 1 = every solo level selectable at every difficulty plus 007 mode from
 * the first launch. F10 'All unlocked' row toggles it; takes effect next
 * run. Cheats are unlocked at query time via fileGetIsCheatUnlocked
 * (src/game/file2.c, D442) -- the save file is never patched. */
s32 portAllUnlocked = 0;

/* D211: Video.FovScale as a multiplier on the render FOV. Applied game-side
 * at the guPerspectiveF chokepoint (src/fr.c) so it lands BEFORE the CPU
 * pre-multiplies projection x view into the combined world matrix — the
 * fast3d-side matrix hack only caught the handful of pure-perspective loads
 * (pause/watch model, sky) and left the world untouched. 1.0f = original. */
f32 portFovScale = 1.0f;

/* D222: the same widen-FOV math as the fr.c guPerspectiveF chokepoint,
 * factored out so cull-plane / LOD-scale call sites (currentPlayerSetCameraScale,
 * via currentPlayerSetPerspective) can feed the SAME effective FOV that is
 * actually rendered, instead of leaving them on the nominal value. Before this,
 * high FovScale widened what was drawn but left frustum-cull planes and the
 * fog/LOD distance scale (c_scalelod/c_lodscalez) calibrated for the narrower
 * nominal FOV, so on-screen geometry near the edges (and, via c_lodscalez,
 * the fog-based distance-visibility fade) got culled/faded as if the view
 * were still narrow. `isTitleScreen` mirrors fr.c's own
 * `lvlGetCurrentStageToLoad() != LEVELID_TITLE` guard -- the front end's
 * fixed-FOV 3D must not be touched. 1.0f/identity at FovScale=100 (default),
 * bit-for-bit no-op.
 *
 * WIDESCREEN-FOV-PLAN Phase 4: Video.WidescreenAuto (default on) applies an
 * automatic aspect-ratio-aware scale BEFORE the manual Video.FovScale
 * multiplier above, so the two compose rather than fight. Formula:
 * sqrt(aspect / 4:3) -- identity at 4:3 (the game's native aspect), widens
 * smoothly for 16:9/21:9/etc. Uses gfx_current_dimensions.aspect_ratio,
 * already tracked and updated on window resize (port/fast3d/gfx_pc.cpp).
 * The final clamp gained a symmetric floor (20 deg) alongside the existing
 * 160 deg ceiling -- covers narrow/portrait-ish window resizes, which the
 * auto-scale formula can otherwise degenerate toward as aspect -> 0; this
 * resolves WIDESCREEN-FOV-PLAN.md's open "horizontal extreme-aspect sanity
 * clamp" question. */
/* D334 (WIDESCREEN-FOV-PLAN Option B, Phase 2): the world-projection aspect
 * for native widescreen, or 0 when off. The game's own N64 16:9 mode
 * (bondview2.c bondviewMovePlayerUpdateViewport) projects at
 * viewport_ratio * 0.75 * 16/9 -- an anamorphic pre-squash the TV undid. The
 * port already stretches the 4:3 logical canvas to the window (fast3d
 * RATIO_X), exactly like that TV, so the same formula with 16/9 replaced by
 * the real window aspect renders the world undistorted at any aspect: vertical
 * FOV stays the game's, horizontal widens (Hor+, the PD standard). Canvas,
 * portal scissors and culling all stay in the game's 320x240 logical space and
 * see the same aspect, so there is no second "view width" to reconcile
 * (WIDESCREEN-FOV-PLAN s6). Clamped to [0.5, 4.0]. At exactly 4:3 the
 * formula is identity, so 4:3 renders are unchanged. */
/* D226: Game.HudScale percent (75..150); 100 = original size, nothing emitted. */
s32 portHudScalePercent(void)
{
    return cfgHudScale;
}

f32 portNativeAspect(void)
{
    f32 a = gfx_current_dimensions.aspect_ratio;
    if (!cfgNativeWidescreen || a < 0.01f) {
        return 0.0f;
    }
    if (a < 0.5f) { a = 0.5f; }
    if (a > 4.0f) { a = 4.0f; }
    return a;
}

f32 portScaleFovY(f32 fovy, s32 isTitleScreen)
{
    /* D484 (#136): the 20-degree floor below guards the port's own widening,
     * but it also raised the game's narrow zoom FOVs (sniper/camera down to
     * 7 degrees, the watch zoom) to 20, capping scope magnification at about
     * a third of the N64's. Never floor above the game's own value. */
    const f32 floorFovY = fovy < 20.0f ? fovy : 20.0f;
    if (!isTitleScreen) {
        /* D334: native widescreen already widens the horizontal FOV through
         * the projection aspect; the Phase-4 vertical boost below was the
         * stretch-era compensation and would double-count, so it is skipped
         * (vertical FOV stays the game's, as in the PD port). */
        if (cfgWidescreenAuto && !cfgNativeWidescreen &&
            gfx_current_dimensions.aspect_ratio > 0.01f) {
            fovy *= sqrtf(gfx_current_dimensions.aspect_ratio / (4.0f / 3.0f));
        }
        if (portFovScale > 0.4f && portFovScale < 2.01f && portFovScale != 1.0f) {
            fovy *= portFovScale;
        }
    }
    if (fovy > 160.0f)    { fovy = 160.0f; }
    if (fovy < floorFovY) { fovy = floorFovY; }
    return fovy;
}

/* D468: the factor portScaleFovY applies to an in-level fovy (clamps
 * ignored), so gameplay can recover the game's own FOV from the rendered one.
 * 1.0f at the defaults (native widescreen on, FovScale 100). */
f32 portFovYScaleFactor(void)
{
    f32 k = 1.0f;
    if (cfgWidescreenAuto && !cfgNativeWidescreen &&
        gfx_current_dimensions.aspect_ratio > 0.01f) {
        k *= sqrtf(gfx_current_dimensions.aspect_ratio / (4.0f / 3.0f));
    }
    if (portFovScale > 0.4f && portFovScale < 2.01f && portFovScale != 1.0f) {
        k *= portFovScale;
    }
    return k;
}

/* D468: Game.AIWideView -- 0 (default) = AI awareness keeps the cartridge's
 * widest view (16:9 at the game's own FOV) when the window is wider or
 * FovScale > 100; 1 = AI sees the full rendered view (the PD-port model). */
static int cfgAIWideView = 0;
int portAIWideView(void) { return cfgAIWideView; }

/* D443 (D357): horizontal FOV in degrees for a Video.FovScale percent, for the
 * settings display only. Mirrors portScaleFovY + the gameplay projection:
 * vertical FOV = FOV_Y_F (60) * [WidescreenAuto stretch-era boost] * pct/100,
 * clamped 20..160; the projection aspect is the window aspect under native
 * widescreen (portNativeAspect) or 4:3 otherwise (bondview2.c faspect for a
 * full-screen viewport). hfov = 2*atan(tan(vfov/2)*aspect). 100% at 4:3 = 75.0
 * deg, at 16:9 = 91.5 deg. */
f32 portFovHorizDegrees(s32 pct)
{
    const double kRad = 3.14159265358979323846 / 180.0;
    double v = 60.0;
    double asp = (double)portNativeAspect();
    if (asp <= 0.0) {
        asp = 4.0 / 3.0;
        if (cfgWidescreenAuto && gfx_current_dimensions.aspect_ratio > 0.01f) {
            v *= sqrt((double)gfx_current_dimensions.aspect_ratio / (4.0 / 3.0));
        }
    }
    if (pct > 40 && pct < 201) {
        v *= (double)pct / 100.0;
    }
    if (v > 160.0) v = 160.0;
    if (v < 20.0)  v = 20.0;
    return (f32)(2.0 * atan(tan(v * 0.5 * kRad) * asp) / kRad);
}

/* D218: Video.DrawDistance -- multiplier applied to a level's authored
 * Visibility.FarFog (src/game/bgfog.c fogLoadCurrentEnvironment), which is
 * both the far clip plane and the fog-saturation distance (levels are tuned
 * for the stock ~60deg FOV), plus the character/prop fog-visibility-fade
 * cutoff (src/game/propobj.c chrobjFogVisRangeRelated/sub_GAME_7F054C58).
 * Video.DrawDistanceAutoFov (legacy ini option, default off) couples the multiplier to
 * Video.FovScale so a wider FOV doesn't clip its own newly visible far
 * geometry -- or fade NPCs out early -- against distances tuned for the
 * narrower original view (D218's "blue artifacting"; the M-121 live
 * playtest found a straight 1:1 FovScale coupling still faded guards in
 * noticeably close on Dam, so the auto coupling is 2x FovScale, not 1x).
 * An explicit Video.DrawDistance != 100 overrides that coupling outright.
 * Clamp history: an explicit Video.DrawDistance goes up to 800 (8.0x cap,
 * raised 2026-10-04 at the maintainer's request). The auto-FOV coupling is
 * 4x FovScale (M-121 live playtest: 2x FovScale still showed a "blue glow"
 * on far Dam tunnel geometry); max portFovScale is 1.5 at Video.FovScale's
 * registered ceiling of 150, so the coupling still tops out at 6.0x.
 * The multiplier scales Visibility.FarFog (bgfog.c), so it moves the far
 * clip plane as well as the fog ramp, while the near plane stays fixed.
 * Pushing the far plane out risks far-field z-fighting against the level's
 * original depth precision; 8x halves the precision margin compared with
 * 4x. A by-eye check at 600-800 is owed (ROADMAP section 3).
 * Identity (1.0f) at DrawDistance=100 with auto-FOV off. NOTE: end-to-end
 * re-check of fogLoadCurrentEnvironment (bgfog.c) found the fog RAMP
 * itself (not just the far-clip cutoff) already scales correctly with
 * this multiplier -- g_ScaledFarFogIntensity/scaled_far_fog_dist both
 * derive from the same scaled far value. If a visible blue tint at range
 * persists even at a large multiplier, it may be Dam's tunnel sightline
 * simply exceeding whatever distance was tried, or a separate visual
 * element (skybox/backdrop) not gated by Visibility.FarFog at all --
 * worth a fresh screenshot-driven look before assuming another bug here. */
f32 portDrawDistanceMultiplier(void)
{
    f32 mult;
    if (cfgDrawDistance != 100) {
        mult = (f32)cfgDrawDistance / 100.0f;
    } else if (cfgDrawDistanceAutoFov && portFovScale != 1.0f) {
        mult = portFovScale * 4.0f;
    } else {
        return 1.0f;
    }
    if (mult > 8.0f) { mult = 8.0f; }
    if (mult < 1.0f) { mult = 1.0f; }
    return mult;
}

/* D249: Video.LodDistance -- multiplier on the *distance* term
 * modelUpdateDistanceRelations() (src/game/model.c) tests against each LOD
 * node's MinDistance/MaxDistance, composed on top of the game's own
 * g_ModelDistanceScale rather than replacing it. Smaller distance reads as
 * "closer", so this function returns the INVERSE of the requested percent:
 * Video.LodDistance=200 (keep full detail twice as far, more cost) ->
 * 0.5x on the distance term; =50 (drop to lower detail twice as soon, less
 * cost -- the perf lever) -> 2.0x. Video.LodDistanceAutoFov (legacy ini
 * option, default OFF) can couple it to Video.FovScale the same
 * direction as draw distance if ever wanted; off by default so this stays a
 * standalone dial and doesn't quietly add cost as FovScale widens. Clamped
 * to a [0.125, 4.0] distance multiplier (== effective LodDistance 25-800%) --
 * far outside that band either does nothing visible (LOD never triggers) or
 * thrashes every frame. Identity (1.0f) at Video.LodDistance=100
 * with auto-FOV off. */
f32 portLodDistanceMultiplier(void)
{
    f32 pct;
    if (cfgLodDistance != 100) {
        pct = (f32)cfgLodDistance;
    } else if (cfgLodDistanceAutoFov && portFovScale != 1.0f) {
        pct = portFovScale * 100.0f;
    } else {
        return 1.0f;
    }
    if (pct < 25.0f)  { pct = 25.0f; }
    if (pct > 800.0f) { pct = 800.0f; }
    return 100.0f / pct;
}

/* D294: the room-model pool (mema, sized per level by boss.c's
 * memallocstringtable `-maNNN` rows) was budgeted by Rare for the N64's fixed
 * ~60deg FOV / 4:3 / authored far-clip. The PC visibility knobs above widen
 * what is on screen at once (FovScale + WidescreenAuto widen the frustum;
 * DrawDistance pushes the far clip; LodDistance keeps high-detail meshes out
 * further), so more rooms stay resident simultaneously. When demand exceeds
 * the authored pool, memaAlloc() returns NULL and bgLoadRoomModelData() bails
 * silently -- the undrawn room's scissor region shows the framebuffer clear
 * colour: a transient solid-black patch of ground that self-recovers when the
 * player moves (D294, Statue monument plaza at FovScale 130 + max DD/Lod).
 *
 * This returns a pool-size multiplier for boss.c to apply to `-ma` on PC:
 *   - FOV term: manual FovScale composed with the WidescreenAuto aspect
 *     scale (same sqrt(aspect/4:3) factor portScaleFovY uses), since both
 *     widen the frustum horizontally and admit more side rooms;
 *   - DD/Lod terms: square-root of the distance multipliers -- room COUNT
 *     grows far slower than distance-squared, so sqrt keeps the bump modest;
 *     a linear-terms + 3.0-cap variant was tried (Statue's full room set is
 *     487 KB = 2.2x its authored pool and starves even 2x) but regressed
 *     the renderer on the menu-driven path -- see the in-function note;
 * capped at 2.0x (worst case `-ma350` level row -> 700 KB, well inside the
 * PC's ~6 MB STAGE bank; see D95). NOTE: a live instrumented Statue playtest
 * showed 2x still starves under whole-level room churn (the full set is
 * 487 KB = 2.2x the authored `-ma220` pool), and the attempted 3x/linear
 * fix REGRESSED the renderer on the menu-driven path -- reverted, see the
 * in-function note + findings D294. Identity (1.0) when every knob is at its
 * N64-faithful value, so all-100 settings keep the exact authored budget.
 * Called once per stage load from bossMainloop (src/boss.c).
 *
 * GE_ROOMPOOL=<float> overrides the computed scale outright (test hook for
 * before/after captures; also a user opt-out if the larger pool ever causes
 * trouble on a level). */
f32 portRoomPoolScale(void)
{
    const char *ov = getenv("GE_ROOMPOOL");
    f32 scale = 1.0f;
    f32 t;

    if (ov && *ov) {
        /* Test hook: values < 1.0 shrink the pool BELOW the authored size
         * to stress-test the exhaustion path (D294 verification). */
        scale = (f32)atof(ov);
        if (scale > 2.5f) { scale = 2.5f; }
        if (scale < 0.25f) { scale = 0.25f; }
        return scale;
    }

    t = portFovScale;
    /* D334: native widescreen widens the horizontal frustum too (via the
     * projection aspect instead of the vertical boost), so it admits the same
     * extra side rooms and keeps the pool bump. */
    if ((cfgWidescreenAuto || cfgNativeWidescreen) && gfx_current_dimensions.aspect_ratio > 0.01f) {
        t *= sqrtf(gfx_current_dimensions.aspect_ratio / (4.0f / 3.0f));
    }
    if (t > scale) { scale = t; }

    /* D294 follow-up (2026-09-22): linear terms + a 3.0 cap were tried to
     * cover Statue's full 487 KB room set, but that build corrupted model
     * anim state on the MENU-driven Statue path (D156 NaN frames; direct
     * -level_22 boots stayed clean headless). Reverted to sqrt + 2.0 cap,
     * the user-verified working state. Do not re-apply without the capture-bat
     * repro data (scratch/d294_capture.bat -> D156 hexdump + GE_D294BANK). */
    t = powf(portDrawDistanceMultiplier(), 0.66f);   /* D294 2026-09-30: was sqrtf; 4^0.66 = 2.5 */
    if (t > scale) { scale = t; }

    t = powf(1.0f / portLodDistanceMultiplier(), 0.66f);
    if (t > scale) { scale = t; }

    /* D294 (2026-09-30): cap 2.0 -> 2.5 so Statue's full 27-room set (487 KB
     * = 2.17x its 225 KB authored pool) fits at max DD/Lod. The old 3x
     * "regression" was D336 (uninitialised head-anim fields), not pool size.
     * Worst case -ma350 row -> 875 KB; boss.c still clamps to STAGE bank - 128 KB. */
    if (scale > 2.5f) { scale = 2.5f; }
    if (scale < 1.0f) { scale = 1.0f; }
    /* boss.c truncates scale to quarter steps ((s64)(scale*4)); round UP to a
     * quarter here so 4^0.66 = 2.497 lands on 2.5 instead of truncating to 2.25. */
    scale = ceilf(scale * 4.0f - 0.01f) / 4.0f;
    return scale;
}

PD_CONSTRUCTOR static void videoConfigInit(void)
{
    configRegisterFloat("Game.ScreenShakeIntensity", &portScreenShakeScale, 0.0f, 10.0f);
    configRegisterInt("Game.SkipIntro", &portSkipIntro, 0, 1);
    configRegisterInt("Game.NoHitFlash", &portNoHitFlash, 0, 1);
    configRegisterInt("Game.AIWideView", &cfgAIWideView, 0, 1);   /* D468 */
    configRegisterInt("Video.CrosshairHide",  &portCrosshairHide, 0, 1);  /* v0.4.0 M2 (D373) */
    configRegisterInt("Video.CrosshairColor", &cfgCrosshairColor, 0, 8);  /* 8 = custom; old ini values unchanged */
    configRegisterInt("Video.CrosshairRed",   &cfgCrosshairRed,   0, 255);
    configRegisterInt("Video.CrosshairGreen", &cfgCrosshairGreen, 0, 255);
    configRegisterInt("Video.CrosshairBlue",  &cfgCrosshairBlue,  0, 255);
    configRegisterInt("Video.CrosshairSize",  &cfgCrosshairSize, 50, 200);
    configRegisterInt("Video.CrosshairStyle", &cfgCrosshairStyle, 0, 1);
    configRegisterInt("Video.CrosshairAlpha", &cfgCrosshairAlpha, 0, 100);        /* D511 */
    configRegisterInt("Video.CrosshairHealthColor", &cfgCrosshairHealth, 0, 1);   /* D511 */
    configRegisterInt("Game.AllUnlocked", &portAllUnlocked, 0, 1);
    configRegisterInt("Video.VSync",         &cfgVSync,      0, 1);
    configRegisterInt("Video.FpsCap",        &cfgFpsCap,     0, 1000);
    configRegisterInt("Video.MSAA",          &cfgMSAA,       1, 16);   /* D443: 16x added */
    configRegisterInt("Video.TextureFilter", &cfgTexFilter,  0, 3);   /* Trilinear option (playtest 2026-10-03) */
    configRegisterInt("Video.FixMipTextures", &cfgFixMipTex, 0, 1);
    configRegisterInt("Video.DetailBaseTile", &cfgDetailBaseTile, 0, 1);
    configRegisterInt("Video.WrapFix", &cfgWrapFix, 0, 1);
    configRegisterInt("Video.FovScale", &cfgFovScale, 50, 150);
    configRegisterInt("Video.WidescreenAuto", &cfgWidescreenAuto, 0, 1);
    configRegisterInt("Video.NativeWidescreen", &cfgNativeWidescreen, 0, 1);   /* D334 */
    configRegisterInt("Game.HudScale", &cfgHudScale, 75, 150);   /* D226: capped at 150 (user: little benefit above) */
    configRegisterInt("Video.DrawDistance", &cfgDrawDistance, 100, 800);
    configRegisterInt("Video.DrawDistanceAutoFov", &cfgDrawDistanceAutoFov, 0, 1);
    configRegisterInt("Video.LodDistance", &cfgLodDistance, 25, 800);
    configRegisterInt("Video.LodDistanceAutoFov", &cfgLodDistanceAutoFov, 0, 1);
    configRegisterInt("Video.Anisotropy", &cfgAniso, 1, 16);
    configRegisterInt("Video.SafeAreaCrop", &cfgSafeAreaCrop, 0, 1);
    configRegisterInt("Video.AspectMode", &cfgAspectMode, 0, 3);   /* D447; D508 forced ratios 2..3 */
    configRegisterInt("Video.Fullscreen",    &cfgFullscreen, 0, 1);
    configRegisterInt("Video.FullscreenMode", &cfgFullscreenMode, 0, 1);   /* D511: 0 borderless, 1 exclusive */
    configRegisterInt("Window.Width",        &cfgWinW,       0, 16384);
    configRegisterInt("Window.Height",       &cfgWinH,       0, 16384);
    configRegisterInt("Window.X",            &cfgWinX,      -1, 16384);
    configRegisterInt("Window.Y",            &cfgWinY,      -1, 16384);
    configRegisterInt("Window.Maximized",    &cfgWinMax,     0, 1);
    configRegisterInt("Video.DeckPresetApplied", &cfgDeckPresetApplied, 0, 1);   /* D283 */
    configRegisterInt("Video.LowEndConsidered", &cfgLowEndConsidered, 0, 1);     /* D482 */
}

/* D283: Steam Deck preset. Called from main() right AFTER configLoad(), so
 * it sees the ini the user already has (the original preset only ran when no
 * ini existed and only on STEAMOS, so a first launch from Desktop Mode --
 * which does not export STEAMOS -- created a plain-Linux ini and the preset
 * never applied, D283).
 *
 * Detection is by hardware: DMI board/sys vendor "Valve" + product
 * "Jupiter" (LCD) or "Galileo" (OLED), read from /sys (Linux only); the
 * STEAMOS env var is kept as an extra signal. GE_FAKE_DECK=1 forces the
 * Deck path on any host (testing), GE_FAKE_DECK=0 forces it off.
 *
 * Applied at most once per ini (Video.DeckPresetApplied records it) and only
 * while the display keys are still at the generic windowed state
 * (Video.Fullscreen=0, Window.Maximized=0). Window.Width/Height are NOT a
 * usable "untouched" signal: videoSaveWindowState() rewrites them from the
 * live window on every clean exit. A user who already picked fullscreen keeps
 * their settings; the flag is still set, so later F10 choices always win.
 * The preset is display-only (borderless fullscreen, 1280x800 = the Deck
 * panel as the windowed size). VSync 1 / MSAA 2 / draw+LOD 250 were part of
 * the old first-run preset but are the generic defaults anyway, so they are
 * no longer written (an existing ini's choices there are never clobbered). */
#if defined(__linux__)
static int videoReadDmi(const char *name, char *out, int n)
{
    char path[128];
    FILE *f;
    snprintf(path, sizeof(path), "/sys/class/dmi/id/%s", name);
    out[0] = 0;
    f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(out, n, f)) out[0] = 0;
    fclose(f);
    for (int i = (int)strlen(out) - 1; i >= 0 && (out[i] == '\n' || out[i] == '\r' || out[i] == ' '); i--)
        out[i] = 0;
    return out[0] != 0;
}
#endif

static int videoDetectSteamDeck(const char **why)
{
    const char *fake = getenv("GE_FAKE_DECK");
    if (fake && *fake) {
        *why = "GE_FAKE_DECK";
        return atoi(fake) != 0;
    }
#if defined(__linux__)
    {
        char vendor[64], sysVendor[64], product[64];
        videoReadDmi("board_vendor", vendor, sizeof(vendor));
        videoReadDmi("sys_vendor", sysVendor, sizeof(sysVendor));
        videoReadDmi("product_name", product, sizeof(product));
        if ((!strncmp(vendor, "Valve", 5) || !strncmp(sysVendor, "Valve", 5)) &&
            (!strcmp(product, "Jupiter") || !strcmp(product, "Galileo"))) {
            *why = "DMI Valve Jupiter/Galileo";
            return 1;
        }
    }
#endif
    if (getenv("STEAMOS")) {
        *why = "STEAMOS";
        return 1;
    }
    *why = "";
    return 0;
}

void videoApplySteamDeckPreset(void)
{
    const char *why = "";
    if (!videoDetectSteamDeck(&why)) return;
    if (cfgDeckPresetApplied) {
        sysLogPrintf(LOG_INFO, "video: Steam Deck (%s); preset already considered for this ini", why);
        return;
    }
    if (!cfgFullscreen && !cfgWinMax) {
        cfgFullscreen = 1;
        cfgWinW       = 1280;
        cfgWinH       = 800;
        sysLogPrintf(LOG_INFO, "video: Steam Deck (%s); applying Deck preset (fullscreen, 1280x800)", why);
    } else {
        sysLogPrintf(LOG_INFO, "video: Steam Deck (%s); display already user-set (fullscreen=%d maximized=%d), preset skipped",
                     why, cfgFullscreen, cfgWinMax);
    }
    cfgDeckPresetApplied = 1;
    configSave();
}

/* D482: lighter defaults on Atom/Celeron-class GPUs (#92).
 * Measured (Lenovo X220 + the PD port on the same box): fast3d costs no more
 * per batch/triangle than PD's; the gap on very weak hardware is workload.
 * DrawDistance/LodDistance 250 send 2.5-3x the draw batches of the authored
 * distance and MSAA 2x adds ~25-30% GPU time -- PD ships authored distance and
 * no MSAA. On Intel HD 400-class GPUs (Bay Trail/Braswell/Apollo Lake/Gemini
 * Lake) and software renderers, lower those three to 100/100/1, but only where
 * the value is still the port default, and only once per ini
 * (Video.LowEndConsidered). Core-series iGPUs (HD 2000-6000, HD 5xx, UHD 6xx
 * except 600/605) keep the full defaults; an HD 3000 holds 60 with them.
 * The bare "Intel(R) HD Graphics" (no number) counts as low-end on purpose: it
 * is what Bay Trail and Celeron/Pentium parts report; no Core-series part
 * reports it, and a miss costs one reversible lightening. Don't tighten this
 * without measurements.
 * Must run after gfx_init (needs GL_RENDERER), so it cannot sit next to
 * videoApplySteamDeckPreset in main.c, which runs before any GL context. The
 * two touch disjoint keys (Deck: window/fullscreen; this: DD/LOD/MSAA).
 * GE_FAKE_LOWEND=1/0 forces the classification for testing. */
static int videoIsLowEndRenderer(const char *r, const char **why)
{
    static const char *const kTags[] = {
        "(BYT)", "(BSW)", "(CHV)", "(APL)", "(GLK)",
        "Bay Trail", "Braswell", "Cherryview", "Apollo Lake", "Gemini Lake",
        "llvmpipe", "softpipe", "SVGA3D", "Microsoft Basic Render", "GDI Generic",
    };
    static const int kModels[] = { 400, 405, 500, 505, 600, 605 };
    const char *p;

    for (int i = 0; i < (int)(sizeof(kTags) / sizeof(kTags[0])); i++) {
        if (strstr(r, kTags[i])) { *why = kTags[i]; return 1; }
    }
    p = strstr(r, "HD Graphics");
    if (p) {
        int n = 0, digits = 0;
        p += strlen("HD Graphics");
        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; digits++; }
        if (!digits) { *why = "numberless HD Graphics"; return 1; }
        for (int i = 0; i < (int)(sizeof(kModels) / sizeof(kModels[0])); i++) {
            if (n == kModels[i]) { *why = "HD Graphics 400-605 class"; return 1; }
        }
    }
    *why = "";
    return 0;
}

static void videoApplyLowEndDefaults(void)
{
    const char *r = gfx_opengl_renderer_string();
    const char *why = "";
    const char *fake = getenv("GE_FAKE_LOWEND");
    int low;

    if (cfgLowEndConsidered) return;
    if (fake && *fake) {
        low = atoi(fake) != 0;
        why = "GE_FAKE_LOWEND";
    } else {
        low = videoIsLowEndRenderer(r, &why);
    }
    if (low) {
        int changed = 0;
        if (cfgDrawDistance == 250) { cfgDrawDistance = 100; changed++; }
        if (cfgLodDistance == 250)  { cfgLodDistance = 100; changed++; }
        if (cfgMSAA == 2)           { cfgMSAA = 1; gfx_msaa_level = 1; changed++; }
        sysLogPrintf(LOG_INFO, "video: low-end renderer \"%s\" (%s); lowered %d default(s): DrawDistance=%d LodDistance=%d MSAA=%d",
                     r, why, changed, cfgDrawDistance, cfgLodDistance, cfgMSAA);
    } else {
        sysLogPrintf(LOG_INFO, "video: renderer \"%s\" (%s); not low-end, defaults kept",
                     r, why[0] ? why : "no match");
    }
    cfgLowEndConsidered = 1;
    configSave();
}

/* D440: "Original N64" preset + its inverse ("Port defaults").
 * One row per config key whose port default differs from what the N64
 * showed, plus the fidelity-identity keys a user may have moved away from
 * (so the N64 preset really snaps every presentation value back). Each
 * entry: key, N64 value, port default (the C initializer). The full audit,
 * including the keys deliberately left OUT (MSAA, VSync, FpsCap, window /
 * fullscreen, accuracy fixes, input feel, bindings, volumes, SkipIntro,
 * AllUnlocked), is in findings D440.
 * Culling needs no key of its own: frustum-cull planes, the fog/LOD scale
 * (D222) and the room-pool budget (D294, portRoomPoolScale) all derive from
 * FovScale / widescreen / draw / LOD distance and are identity when those
 * are at the N64 values below. */
static const struct { const char *key; double n64, port; } kVideoPresets[] = {
    { "Video.TextureFilter",         2,   1 },   /* N64 3-point vs bilinear */
    { "Video.Anisotropy",            1,   4 },   /* the RDP has no anisotropic filtering */
    { "Video.NativeWidescreen",      0,   1 },   /* 4:3 projection (D334) */
    { "Video.WidescreenAuto",        0,   1 },   /* stock vertical FOV at any window aspect */
    { "Video.SafeAreaCrop",          0,   1 },   /* show the VI frame as output, borders included */
    { "Video.AspectMode",            1,   0 },   /* D447: exact console aspect, bars around it */
    { "Video.DrawDistance",        100, 250 },   /* authored far clip / fog (D218) */
    { "Video.LodDistance",         100, 250 },   /* authored LOD switch distances (D249) */
    { "Video.DrawDistanceAutoFov",   0,   0 },   /* legacy coupling: off = identity */
    { "Video.LodDistanceAutoFov",    0,   0 },
    { "Video.FovScale",            100, 100 },   /* stock FOV (D211) */
    { "Game.HudScale",             100, 100 },   /* D226 */
    { "Game.ScreenShakeIntensity",   1,   1 },   /* D181 */
    { "Game.NoHitFlash",             0,   0 },   /* D232 */
    { "Video.CrosshairHide",         0,   0 },   /* D373 */
    { "Video.CrosshairColor",        0,   0 },   /* authored red sprite */
    { "Video.CrosshairSize",       100, 100 },
    { "Video.CrosshairStyle",        0,   0 },
    { "Input.AimMode",               0,   0 },   /* N64 aim (D337) */
    { "Input.AimRange",              1,   0 },   /* N64 aim limits (D338) */
};
#define NUM_VIDEO_PRESETS ((int)(sizeof(kVideoPresets) / sizeof(kVideoPresets[0])))

int videoApplyPreset(int which)
{
    int changed = 0;
    for (int i = 0; i < NUM_VIDEO_PRESETS; i++) {
        double want = which == VIDEO_PRESET_N64 ? kVideoPresets[i].n64 : kVideoPresets[i].port;
        double cur = 0.0;
        if (!configGetValue(kVideoPresets[i].key, &cur)) {
            sysLogPrintf(LOG_WARNING, "video: preset key %s not registered", kVideoPresets[i].key);
            continue;
        }
        if (cur == want) continue;
        configSetValue(kVideoPresets[i].key, want);
        if (initDone && !strncmp(kVideoPresets[i].key, "Video.", 6))
            videoRequestLiveConfigForKey(kVideoPresets[i].key);
        changed++;
    }
    sysLogPrintf(LOG_INFO, "video: applied %s preset (%d value(s) changed)",
                 which == VIDEO_PRESET_N64 ? "Original N64" : "port defaults", changed);
    return changed;
}

int videoPresetIsActive(int which)
{
    for (int i = 0; i < NUM_VIDEO_PRESETS; i++) {
        double want = which == VIDEO_PRESET_N64 ? kVideoPresets[i].n64 : kVideoPresets[i].port;
        double cur = 0.0;
        if (!configGetValue(kVideoPresets[i].key, &cur) || cur != want) return 0;
    }
    return 1;
}

/* D382: record precisely which live video setting changed. Reticle/FOV/
 * frame cap changes must not reset texture state (a shader/cache clear).
 * Option writes may arrive on the menu or scheduler thread; apply on the
 * render thread with the GL context bound, coalesced per frame. */
enum { VCFG_VSYNC = 1, VCFG_FPS = 2, VCFG_FILTER = 4,
       VCFG_FOV = 8, VCFG_ANISO = 16, VCFG_CROP = 32 };
static SDL_atomic_t liveCfgDirty;

/* D211/D212: push the port-only image knobs where they apply. FovScale is a
 * plain float the game re-reads each frame; anisotropy goes to fast3d. */
static void videoApplyImageOptions(void)
{
    portFovScale = (f32)cfgFovScale / 100.0f;
    gfx_set_anisotropy_level(cfgAniso);
    gfx_set_safe_area_crop(cfgSafeAreaCrop);
}

static void videoApplyTexFilter(void)
{
    /* Trilinear option (playtest 2026-10-03): value 3 = bilinear + generated mips. */
    if (cfgTexFilter == 3) {
        gfx_set_texture_filter(FILTER_TRILINEAR);
        gfx_set_mipmap_filter(MIPMAP_LINEAR);
    } else if (cfgTexFilter >= 2) {
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

/* Port-only sprite settings and direct-read world/HUD options are not GL
 * state. Keep the heavyweight texture reset only for the two texture knobs. */
void videoRequestLiveConfigForKey(const char *key)
{
    int mask = 0;
    if (!strcmp(key, "Video.VSync"))               mask = VCFG_VSYNC;
    else if (!strcmp(key, "Video.FpsCap"))          mask = VCFG_FPS;
    else if (!strcmp(key, "Video.TextureFilter"))   mask = VCFG_FILTER;
    else if (!strcmp(key, "Video.FovScale"))        mask = VCFG_FOV;
    else if (!strcmp(key, "Video.Anisotropy"))      mask = VCFG_ANISO;
    else if (!strcmp(key, "Video.SafeAreaCrop"))    mask = VCFG_CROP;
    /* Video.AspectMode is read every frame in videoStartFrame: no dirty bit. */
    if (mask) {
        int old;
        do {
            old = SDL_AtomicGet(&liveCfgDirty);
        } while (!SDL_AtomicCAS(&liveCfgDirty, old, old | mask));
    }
}

/* --- F10 overlay: window / fullscreen changes, deferred to the host thread ---
 * optionsOverlayHandleInput() runs on the scheduler thread; SDL_SetWindowSize /
 * SDL_SetWindowFullscreen pump the Win32 message loop and must run on the
 * window's creating thread. The overlay posts a request here; the host-thread
 * event pump drains it in videoDrainWindowRequests(). */
static volatile int winReqKind = 0;          /* 0 none, 1 resize, 2 fullscreen, 3 centre (D511), 4 fullscreen mode (D511) */
static volatile int winReqA = 0, winReqB = 0;

void videoRequestWindowSize(int w, int h)
{
    winReqA = w; winReqB = h; winReqKind = 1;
}

void videoRequestFullscreen(int on)
{
    winReqA = on ? 1 : 0; winReqKind = 2;
}

void videoRequestFullscreenMode(int exclusive)
{
    winReqA = exclusive ? 1 : 0; winReqKind = 4;
}

void videoRequestCenterWindow(void)
{
    winReqKind = 3;
}

void videoGetOutputRectFrac(double *x0, double *y0, double *x1, double *y1)
{
    int32_t rx = 0, ry = 0, rw = 0, rh = 0;
    double W = (double)gfx_current_window_dimensions.width;
    double H = (double)gfx_current_window_dimensions.height;
    gfx_get_output_rect(&rx, &ry, &rw, &rh);
    if (W < 1.0 || H < 1.0 || rw <= 0 || rh <= 0) {
        *x0 = 0.0; *y0 = 0.0; *x1 = 1.0; *y1 = 1.0;
        return;
    }
    *x0 = rx / W; *y0 = ry / H;
    *x1 = (rx + rw) / W; *y1 = (ry + rh) / H;
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
    } else if (kind == 3) {   /* D511: Center window (windowed only) */
        uint32_t ww = 0, hh = 0; int32_t cx = 0, cy = 0, px = 0, py = 0;
        if (wmAPI->get_fullscreen_state && wmAPI->get_fullscreen_state()) return;
        if ((wmAPI->get_maximized_state && wmAPI->get_maximized_state()) || cfgWinMax) {   /* set_dimensions on a maximized window is undefined across platforms */
            sysLogPrintf(LOG_INFO, "video: center window ignored (window is maximized)");
            return;
        }
        if (!wmAPI->get_dimensions || !wmAPI->get_centered_positions || !wmAPI->set_dimensions) return;
        wmAPI->get_dimensions(&ww, &hh, &cx, &cy);
        wmAPI->get_centered_positions((int32_t)ww, (int32_t)hh, &px, &py);
        wmAPI->set_dimensions(ww, hh, px, py);
        cfgWinX = px; cfgWinY = py;
        sysLogPrintf(LOG_INFO, "video: center window %ux%u: (%d,%d) -> (%d,%d)", ww, hh, cx, cy, px, py);
    } else if (kind == 4) {   /* D511: borderless vs exclusive; re-enters fullscreen when already on */
        int ex = winReqA;
        if (wmAPI->set_fullscreen_exclusive) wmAPI->set_fullscreen_exclusive(ex != 0);
        gfx_sdl_update_cached_size();
        cfgFullscreenMode = ex ? 1 : 0;
        sysLogPrintf(LOG_INFO, "video: fullscreen mode %s (fullscreen now %d)", ex ? "exclusive" : "borderless",
                     wmAPI->get_fullscreen_state ? (int)wmAPI->get_fullscreen_state() : -1);
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
    /* D440 CLI route: -n64preset / -portpreset apply the preset
     * once at startup (after configLoad, before any value reaches fast3d) and
     * persist like an F10 press (the atexit configSave). */
    if (sysArgCheck("-n64preset") || sysArgCheck("--n64preset")) {
        videoApplyPreset(VIDEO_PRESET_N64);
    } else if (sysArgCheck("-portpreset") || sysArgCheck("--portpreset")) {
        videoApplyPreset(VIDEO_PRESET_PORT);
    }

    wmAPI = &gfx_sdl;
    renderingAPI = &gfx_opengl_api;

    gfx_current_native_viewport.width = GE_NATIVE_W;
    gfx_current_native_viewport.height = GE_NATIVE_H;
    gfx_current_native_aspect = (float)GE_NATIVE_W / (float)GE_NATIVE_H;
    gfx_framebuffers_enabled = true;
    gfx_detail_textures_enabled = false;

    /* MSAA: snap the requested sample count down to a supported power of two. */
    gfx_msaa_level = cfgMSAA >= 16 ? 16 : cfgMSAA >= 8 ? 8 : cfgMSAA >= 4 ? 4 : cfgMSAA >= 2 ? 2 : 1;

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
            .fullscreen_is_exclusive = cfgFullscreenMode != 0,
            .maximized = cfgWinMax != 0,
            .centered = !havePos,
            .allow_hidpi = false,
        },
    };

    gfx_init(&set);
    videoApplyLowEndDefaults();   /* D482: needs GL_RENDERER; before the first frame */

    /* VSync + optional fps cap; fast3d paces the window itself. */
    wmAPI->set_swap_interval(cfgVSync ? 1 : 0);
    /* D186: a low cap does not just drop frames -- the pacing wait blocks the
     * scheduler thread and throttles the sim with it. Normalise a bad
     * ge007.ini value (e.g. dinged to 10 via the options overlay) to uncapped
     * so it persists sane on the next configSave(). */
    if (cfgFpsCap > 0 && cfgFpsCap < 30) {
        sysLogPrintf(LOG_WARNING, "video: Video.FpsCap=%d too low (throttles the sim); using 0 (uncapped)", cfgFpsCap);
        cfgFpsCap = 0;
    }
    gfx_set_target_fps(cfgFpsCap);   /* 0 = uncapped */

    /* Texture filtering. 1 = bilinear (default, matches prior behaviour),
     * 0 = crisp nearest, 2 = N64 3-point emulation + trilinear mips (opt-in;
     * more console-authentic but softens textures at normal distance -- did
     * NOT fix the Depot roof, see docs/BRIEF-B2-depot-textures.md). All keep
     * point-sampled tiles (HUD, G_TF_POINT) crisp via the per-tile flag. */
    gfx_set_fix_mip_textures(cfgFixMipTex);
    gfx_set_detail_base_tile(cfgDetailBaseTile);
    gfx_set_wrap_fix(cfgWrapFix);

    videoApplyTexFilter();
    videoApplyImageOptions();

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

/* ---- D344: orderly quit ------------------------------------------------
 * Rendering runs on the scheduler thread with the GL context bound; quit
 * events arrive on the host thread (or on fast3d's render-thread pump).
 * exit() used to run straight from whichever pump saw the event, tearing the
 * process down while the render thread could be inside the NVIDIA driver --
 * the likely trigger of the 0x119 VIDEO_SCHEDULER_INTERNAL_ERROR bugchecks
 * (findings D344). Now:
 *   1. anyone calls videoRequestQuit();
 *   2. the render thread, at its next frame boundary, glFinish()es, unbinds
 *      the context and parks for good (videoRenderPark);
 *   3. the host thread waits until the render thread is parked or outside a
 *      frame (max QUIT_WAIT_MS), then exit(0) (atexit saves config).
 * Frame entry sets s_inFrame BEFORE checking s_quitReq, and the host sets
 * s_quitReq BEFORE reading s_inFrame (SDL atomics are sequentially
 * consistent), so a frame can never start once the host has decided to exit. */
#define QUIT_WAIT_MS 2000
static SDL_atomic_t s_quitReq;
static SDL_atomic_t s_inFrame;
static SDL_atomic_t s_parked;
static int s_quitFrame = -1;   /* GE_QUITFRAME: harness self-quit, -1 = unread */

void videoRequestQuit(const char *why)
{
    if (SDL_AtomicCAS(&s_quitReq, 0, 1)) {
        sysLogPrintf(LOG_INFO, "video: quit requested (%s)", why ? why : "?");
    }
}

/* D443: "Restart game" = the same orderly quit, plus a flag main.c's atexit
 * handler reads (after config/window state are saved) to relaunch. */
static int s_restartReq = 0;
void videoRequestRestart(const char *why)
{
    s_restartReq = 1;
    videoRequestQuit(why);
}
int videoRestartRequested(void)
{
    return s_restartReq;
}

int videoQuitRequested(void)
{
    return SDL_AtomicGet(&s_quitReq) != 0;
}

/* Render thread only. Never returns. */
static void videoRenderPark(void)
{
    gfx_sdl_park_for_exit();
    SDL_AtomicSet(&s_inFrame, 0);
    SDL_AtomicSet(&s_parked, 1);
    for (;;) {
        sysSleep(100000);
    }
}

/* Host thread: exit once the render thread is out of the GL driver. */
static void videoHostExitIfRequested(void)
{
    if (!SDL_AtomicGet(&s_quitReq)) {
        return;
    }
    Uint32 start = SDL_GetTicks();
    while (!SDL_AtomicGet(&s_parked) && SDL_AtomicGet(&s_inFrame) &&
           SDL_GetTicks() - start < QUIT_WAIT_MS) {
        SDL_Delay(2);
    }
    sysLogPrintf(LOG_INFO, "video: exiting (render %s after %u ms)",
                 SDL_AtomicGet(&s_parked) ? "parked"
                 : (SDL_AtomicGet(&s_inFrame) ? "STILL IN FRAME (timeout)" : "idle"),
                 (unsigned)(SDL_GetTicks() - start));
    {
        extern void mempRedzoneCheck(const char *why);
        mempRedzoneCheck("orderly quit");
    }
    exit(0);
}

void videoDestroy(void)
{
    if (initDone) {
        gfx_destroy();
        initDone = 0;
    }
}

/* D447: target aspect of the output rect for the current frame. Original mode
 * does what the console did: 4:3, or 16:9 while the watch-menu Ratio option is
 * 16:9 (the game pre-squashes for a stretching widescreen TV). get_screen_ratio()
 * is read only, never written. Returns 0 for Window mode (fill the window). */
extern int get_screen_ratio(void);
static float videoOutputAspect(void)
{
    switch (cfgAspectMode) {
    case 1:   /* Original: the console's own shape */
        return get_screen_ratio() == 1 /* SCREEN_RATIO_16_9 */ ? (16.0f / 9.0f) : (4.0f / 3.0f);
    case 2: return 16.0f / 9.0f;   /* D508: forced ratios; with Native widescreen the world
                                      projects at this aspect (it is the output rect's own) */
    case 3: return 21.0f / 9.0f;   /* also stands in for 2.35:1 cinema */
    default: return 0.0f;          /* Window: fill the window */
    }
}

void videoStartFrame(void)
{
    if (!initDone) {
        return;
    }
    /* D344: enter the frame first, then check for a quit (see above). */
    SDL_AtomicSet(&s_inFrame, 1);
    /* D464: GE_MEMPREDZONE periodic stage-bank red-zone check (cached env). */
    if (frames % 60 == 59) {
        extern void mempRedzoneCheck(const char *why);
        mempRedzoneCheck("videoEndFrame/60");
    }
    if (SDL_AtomicGet(&s_quitReq)) {
        videoRenderPark();
    }

    /* Rendering runs on the D481 render worker (or the scheduler thread with
     * GE_RENDERINLINE/GE_DETERM); the GL context was created on the host
     * main thread. */
    gfx_sdl_make_context_current();

    int dirty = SDL_AtomicSet(&liveCfgDirty, 0);
    if (dirty) {
        if (dirty & VCFG_VSYNC) wmAPI->set_swap_interval(cfgVSync ? 1 : 0);
        if (dirty & VCFG_FPS) gfx_set_target_fps(cfgFpsCap);
        if (dirty & VCFG_FILTER) videoApplyTexFilter();
        if (dirty & VCFG_FOV) portFovScale = (f32)cfgFovScale / 100.0f;
        if (dirty & VCFG_ANISO) gfx_set_anisotropy_level(cfgAniso);
        if (dirty & VCFG_CROP) gfx_set_safe_area_crop(cfgSafeAreaCrop);
        sysLogPrintf(LOG_INFO, "video: live config applied mask=%02x "
                     "(vsync=%d fpscap=%d texfilter=%d fov=%d aniso=%d)",
                     dirty, cfgVSync, cfgFpsCap, cfgTexFilter, cfgFovScale, cfgAniso);
    }

    gfx_set_output_aspect(videoOutputAspect());
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
    inputApplyMouseRequests();   /* D287: mouse mode/cursor, same rule */
    videoHostExitIfRequested();  /* D344: quit posted last pump or by another thread */

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_AUDIODEVICEREMOVED:
            /* D470: an open output device vanished; audio.c falls back to default. */
            if (!ev.adevice.iscapture) audioNotifyDeviceRemoved(ev.adevice.which);
            break;
        case SDL_QUIT:
            videoRequestQuit("quit event");
            break;
        case SDL_KEYDOWN:
            /* D383: host owns SDL key events; the capture modal consumes the
             * next scancode before F10/ESC can close the UI or navigate. */
            if (optionsBindingKeyDown(&ev.key)) break;
            /* D145: bare ESC used to exit(0). On the front-end / debrief
             * screens ESC is the natural "back" key, so a player pressing it
             * to page back instead quit the whole game (looked like a crash --
             * clean exit, no crash log). ESC now feeds the N64 B button
             * (back / cancel) via input.c; quitting is window-close (the X) or
             * Alt+F4 only. */
            if ((ev.key.keysym.sym == SDLK_F4) && (ev.key.keysym.mod & KMOD_ALT)) {
                videoRequestQuit("Alt+F4");
            } else if (ev.key.keysym.sym == SDLK_F12 && !ev.key.repeat) {
                screenshotReq = 1;
            } else if (ev.key.keysym.sym == SDLK_F10 && !ev.key.repeat) {
                /* F10: port-layer options overlay (the one PC options UI). */
                optionsOverlayToggle();
            } else if (ev.key.keysym.sym == SDLK_ESCAPE && !ev.key.repeat) {
                /* Overlay open: ESC backs out of a category, then closes (swallowed). Otherwise
                 * WI-1: in click-to-lock mode ESC frees the captured cursor
                 * (and is swallowed); else it falls through to input.c where
                 * it feeds the N64 B button (D145). */
                if (optionsOverlayIsOpen()) {
                    optionsOverlayBack();
                } else {
                    inputReleaseCapture();
                }
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (optionsBindingMouseDown(&ev.button)) break;
            /* WI-1: a click in the window (re)locks the cursor in
             * click-to-lock mode; a no-op otherwise. */
            if (!optionsOverlayIsOpen() && !optionsBindingCaptureActive()) {
                inputNotifyClick();
            }
            break;
        case SDL_MOUSEWHEEL:
            if (optionsBindingCaptureActive()) break;
            if (optionsOverlayIsOpen()) {
                optionsOverlayScroll(ev.wheel.y);   /* move the selection */
            } else {
                inputPostWheel(ev.wheel.y);   /* weapon cycle */
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
            inputRequestRescan();   /* D450: the rescan runs on the pad-reading thread */
            break;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_CLOSE) {
                videoRequestQuit("window closed");
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

    /* D287: apply anything the events above (click-to-lock, focus) queued. */
    inputApplyMouseRequests();
}

/* D416: split-screen viewports are sub-rects; tell fast3d so its safe-area
 * crop stays off. Called before every gfx_run (scheduler + videoSubmitCommands). */
void videoSyncSplitScreen(void)
{
    {
        extern s32 getPlayerCount(void);
        extern s32 lvlGetCurrentStageToLoad(void);
        static int lastSplit = -1;
        int split = getPlayerCount() >= 2 && lvlGetCurrentStageToLoad() != LEVELID_TITLE;
        if (split != lastSplit) { sysLogPrintf(LOG_NOTE, "D416 split=%d players=%d stage=%d", split, (int)getPlayerCount(), (int)lvlGetCurrentStageToLoad()); lastSplit = split; }
        gfx_set_split_screen(split);
    }
}

void videoSubmitCommands(Gfx *cmds)
{
    if (!initDone) {
        return;
    }
    videoSyncSplitScreen();
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
            gfx_opengl_dump_bound_fbo((uint32_t)gfx_current_window_dimensions.width,
                                      (uint32_t)gfx_current_window_dimensions.height, path);   /* D447: whole window, bars included */
        }
    }

    if (screenshotReq) {
        screenshotReq = 0;
        static int shotNum = 0;
        char path[128];
        GE_MKDIR("ppm");
        snprintf(path, sizeof(path), "ppm/shot_%03d.ppm", shotNum++);
        if (gfx_opengl_dump_bound_fbo((uint32_t)gfx_current_window_dimensions.width,
                                      (uint32_t)gfx_current_window_dimensions.height, path)) {
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

    /* D344: the frame (and its swap) is done. Park here on a quit request, so
     * the host never exits under an in-flight frame. GE_QUITFRAME=<n> is the
     * harness's clean self-quit (replaces `timeout` kills). */
    if (s_quitFrame == -1) {
        const char *q = getenv("GE_QUITFRAME");
        s_quitFrame = (q && atoi(q) > 0) ? atoi(q) : 0;
    }
    if (s_quitFrame > 0 && frames + 1 >= (u32)s_quitFrame) {
        videoRequestQuit("GE_QUITFRAME");
    }
    if (SDL_AtomicGet(&s_quitReq)) {
        videoRenderPark();
    }
    SDL_AtomicSet(&s_inFrame, 0);

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

u32 videoGetFrameCount(void)   { return frames; }
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
