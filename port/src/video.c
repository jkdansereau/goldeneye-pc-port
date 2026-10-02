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

#include "platform.h"
#include "system.h"
#include "config.h"
#include "video.h"
#include "input.h"
#include "optionsoverlay.h"
#include "frontoptions.h"

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
 * [Video] ge007.ini knobs. Draw/LOD and MSAA defaults favour a clean picture
 * at modern resolutions without requiring 4x anti-aliasing.
 */
static int cfgVSync         = 1;   /* swap interval: 0 = off, 1 = on            */
static int cfgFpsCap        = 60;  /* frame cap in fps; 0 = uncapped (vsync); menu exposes 30/60/120. Above the VI rate = D409 interpolation */
static int cfgMSAA          = 2;   /* 1/2/4/8 samples; 2x default is lighter on low-end GPUs */
static int cfgTexFilter     = 1;   /* 0 = nearest, 1 = bilinear (default), 2 = N64 3-point + trilinear */
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
    if (cfgCrosshairColor == 0 || !envCommand) return;
    gDPSetCombineLERP(envCommand + 1,
        ENVIRONMENT, 0, TEXEL0_ALPHA, 0, TEXEL0, 0, ENVIRONMENT, 0,
        ENVIRONMENT, 0, TEXEL0_ALPHA, 0, TEXEL0, 0, ENVIRONMENT, 0);
}

void portCrosshairTint(s32 *r, s32 *g, s32 *b)
{
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
    if (cfgCrosshairColor == 0) {
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
 * run. Known quirk when ON with a fresh save: audio volumes load as 0
 * (silence) because the patched save block is CRC-valid and skips the
 * game's BLANKSAVEDATA reset that normally seeds max volume — see D259. */
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
    if (fovy > 160.0f) { fovy = 160.0f; }
    if (fovy < 20.0f)  { fovy = 20.0f; }
    return fovy;
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
 * Clamped to <=4.0x -- pushing the far plane much further out risks
 * far-field z-fighting against the level's original near-plane precision;
 * raised again (M-121 live playtest: 2x FovScale still showed a "blue
 * glow" on far Dam tunnel geometry, so auto coupling is now 4x FovScale)
 * to give headroom (max portFovScale is 1.5 at Video.FovScale's registered
 * ceiling of 150, so 4x tops out at 6.0x -- ceiling raised to match).
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
    if (mult > 6.0f) { mult = 6.0f; }
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
 * to a [0.25, 4.0] distance multiplier (== effective LodDistance 25-400%) --
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
    if (pct > 400.0f) { pct = 400.0f; }
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
        if (scale > 2.0f) { scale = 2.0f; }
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
    t = sqrtf(portDrawDistanceMultiplier());
    if (t > scale) { scale = t; }

    t = sqrtf(1.0f / portLodDistanceMultiplier());
    if (t > scale) { scale = t; }

    if (scale > 2.0f) { scale = 2.0f; }
    if (scale < 1.0f) { scale = 1.0f; }
    return scale;
}

PD_CONSTRUCTOR static void videoConfigInit(void)
{
    configRegisterFloat("Game.ScreenShakeIntensity", &portScreenShakeScale, 0.0f, 10.0f);
    configRegisterInt("Game.SkipIntro", &portSkipIntro, 0, 1);
    configRegisterInt("Game.NoHitFlash", &portNoHitFlash, 0, 1);
    configRegisterInt("Video.CrosshairHide",  &portCrosshairHide, 0, 1);  /* v0.4.0 M2 (D373) */
    configRegisterInt("Video.CrosshairColor", &cfgCrosshairColor, 0, 8);  /* 8 = custom; old ini values unchanged */
    configRegisterInt("Video.CrosshairRed",   &cfgCrosshairRed,   0, 255);
    configRegisterInt("Video.CrosshairGreen", &cfgCrosshairGreen, 0, 255);
    configRegisterInt("Video.CrosshairBlue",  &cfgCrosshairBlue,  0, 255);
    configRegisterInt("Video.CrosshairSize",  &cfgCrosshairSize, 50, 200);
    configRegisterInt("Video.CrosshairStyle", &cfgCrosshairStyle, 0, 1);
    configRegisterInt("Game.AllUnlocked", &portAllUnlocked, 0, 1);
    configRegisterInt("Video.VSync",         &cfgVSync,      0, 1);
    configRegisterInt("Video.FpsCap",        &cfgFpsCap,     0, 1000);
    configRegisterInt("Video.MSAA",          &cfgMSAA,       1, 8);
    configRegisterInt("Video.TextureFilter", &cfgTexFilter,  0, 2);
    configRegisterInt("Video.FixMipTextures", &cfgFixMipTex, 0, 1);
    configRegisterInt("Video.DetailBaseTile", &cfgDetailBaseTile, 0, 1);
    configRegisterInt("Video.WrapFix", &cfgWrapFix, 0, 1);
    configRegisterInt("Video.FovScale", &cfgFovScale, 50, 150);
    configRegisterInt("Video.WidescreenAuto", &cfgWidescreenAuto, 0, 1);
    configRegisterInt("Video.NativeWidescreen", &cfgNativeWidescreen, 0, 1);   /* D334 */
    configRegisterInt("Game.HudScale", &cfgHudScale, 75, 150);   /* D226: capped at 150 (user: little benefit above) */
    configRegisterInt("Video.DrawDistance", &cfgDrawDistance, 100, 400);
    configRegisterInt("Video.DrawDistanceAutoFov", &cfgDrawDistanceAutoFov, 0, 1);
    configRegisterInt("Video.LodDistance", &cfgLodDistance, 25, 400);
    configRegisterInt("Video.LodDistanceAutoFov", &cfgLodDistanceAutoFov, 0, 1);
    configRegisterInt("Video.Anisotropy", &cfgAniso, 1, 16);
    configRegisterInt("Video.SafeAreaCrop", &cfgSafeAreaCrop, 0, 1);
    configRegisterInt("Video.Fullscreen",    &cfgFullscreen, 0, 1);
    configRegisterInt("Window.Width",        &cfgWinW,       0, 16384);
    configRegisterInt("Window.Height",       &cfgWinH,       0, 16384);
    configRegisterInt("Window.X",            &cfgWinX,      -1, 16384);
    configRegisterInt("Window.Y",            &cfgWinY,      -1, 16384);
    configRegisterInt("Window.Maximized",    &cfgWinMax,     0, 1);
}

/* Steam Deck / SteamOS first-run preset. Called from main() when STEAMOS is
 * set, BEFORE configLoad(): if no ge007.ini exists yet, configLoad's
 * first-run path saves these values to disk and every later launch reads the
 * file (user changes via F10 win outright); if an ini already exists its
 * values overwrite everything here. So this is a first-launch preset only.
 * 1280x800 is the Deck's native panel resolution; 2x MSAA + VSync keep
 * GPU cost modest. Draw/LOD distances use the standard midpoint (250%)
 * to avoid obvious N64-era pop-in on modern displays.
 * Note: the panel is 16:10 and the game renders 4:3, so this stretches
 * uniformly like any non-4:3 window today (letterboxing is the parked
 * WIDESCREEN-FOV-PLAN). */
void videoApplySteamOSDefaults(void)
{
    cfgFullscreen   = 1;
    cfgWinW         = 1280;
    cfgWinH         = 800;
    cfgVSync        = 1;
    cfgMSAA         = 2;
    cfgDrawDistance = 250;
    cfgLodDistance  = 250;
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

/* D409: the in-between frame fast3d presents on its own (gfx_pc.cpp). */
void videoNoteInterpFrame(void)
{
    ++fpsNumFrames;
}

/* D409: a cap above the console VI rate turns on fast3d's frame
 * interpolation (the sim itself stays at 60/50 Hz -- see gfx_pc.cpp) and
 * presents twice per VI tick. With VSync on, every swap waits for a
 * display refresh, so on a display slower than that the two presents per
 * tick would halve the SIM rate (D186); interpolation is held off there until
 * VSync is turned off or the window lands on a fast enough display. */
extern u32 osTvType; /* port/src/libultra.c: 1 = NTSC (60 Hz VI), else 50 Hz */

static void videoApplyFpsCap(void)
{
    static int lastState = -1;
    const int vi = (osTvType == 1) ? 60 : 50;
    const int want = cfgFpsCap > vi;
    const int hz = want ? gfx_sdl_get_refresh_rate() : 0;
    const int ok = want && (!cfgVSync || hz >= 2 * vi - 5);
    const int state = !want ? 0 : (ok ? 1 : 2);

    /* With interpolation on, fast3d paces its own two presents per tick
     * (gfx_pc.cpp gfx_run), so the window-level frame cap is off. */
    gfx_set_frame_interpolation(ok, vi, hz, cfgVSync);
    gfx_set_target_fps(ok ? 0 : (want ? vi : cfgFpsCap));

    if (state != lastState) {
        lastState = state;
        if (state == 1) {
            sysLogPrintf(LOG_INFO, "video: frame interpolation on (%d fps presents, %d Hz sim)", 2 * vi, vi);
        } else if (state == 2) {
            sysLogPrintf(LOG_WARNING, "video: FpsCap=%d needs a >=%d Hz display with VSync on "
                         "(this one reports %d Hz); holding at %d fps -- turn VSync off to force it",
                         cfgFpsCap, 2 * vi, hz, vi);
        }
    }
}

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
    /* D186: a low cap does not just drop frames -- the pacing wait blocks the
     * scheduler thread and throttles the sim with it. Normalise a bad
     * ge007.ini value (e.g. dinged to 10 via the options overlay) to uncapped
     * so it persists sane on the next configSave(). */
    if (cfgFpsCap > 0 && cfgFpsCap < 30) {
        sysLogPrintf(LOG_WARNING, "video: Video.FpsCap=%d too low (throttles the sim); using 0 (uncapped)", cfgFpsCap);
        cfgFpsCap = 0;
    }
    videoApplyFpsCap();   /* 0 = uncapped; > VI rate = D409 interpolation */

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
    exit(0);
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
    /* D344: enter the frame first, then check for a quit (see above). */
    SDL_AtomicSet(&s_inFrame, 1);
    if (SDL_AtomicGet(&s_quitReq)) {
        videoRenderPark();
    }

    /* Rendering runs on the game's scheduler thread; the GL context was
     * created on the host main thread. */
    gfx_sdl_make_context_current();

    int dirty = SDL_AtomicSet(&liveCfgDirty, 0);
    if (dirty) {
        if (dirty & VCFG_VSYNC) wmAPI->set_swap_interval(cfgVSync ? 1 : 0);
        if (dirty & (VCFG_FPS | VCFG_VSYNC)) videoApplyFpsCap();
        if (dirty & VCFG_FILTER) videoApplyTexFilter();
        if (dirty & VCFG_FOV) portFovScale = (f32)cfgFovScale / 100.0f;
        if (dirty & VCFG_ANISO) gfx_set_anisotropy_level(cfgAniso);
        if (dirty & VCFG_CROP) gfx_set_safe_area_crop(cfgSafeAreaCrop);
        sysLogPrintf(LOG_INFO, "video: live config applied mask=%02x "
                     "(vsync=%d fpscap=%d texfilter=%d fov=%d aniso=%d)",
                     dirty, cfgVSync, cfgFpsCap, cfgTexFilter, cfgFovScale, cfgAniso);
    }

    /* D409: the window may have moved to a display with another refresh
     * rate; re-check the interpolation guard about every two seconds. */
    if (cfgFpsCap > 60 && (frames % 120) == 0) {
        videoApplyFpsCap();
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
    inputApplyMouseRequests();   /* D287: mouse mode/cursor, same rule */
    videoHostExitIfRequested();  /* D344: quit posted last pump or by another thread */

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
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
                /* F10: port-layer options overlay -- not on the PC options
                 * screen (D343: one options UI at a time). */
                if (optionsOverlayIsOpen() || !frontOptionsBlocksOverlay()) {
                    optionsOverlayToggle();
                }
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
            inputRescanPads();
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
        extern void gfxD157Burst(void); /* D252 TEMP, inert without GE_D157 */
        gfxD157Burst();
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
