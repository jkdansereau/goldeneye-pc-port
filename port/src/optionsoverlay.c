/*
 * F10 in-game options overlay -- approach (C) from docs/dev/OPTIONS-MENU-PLAN.md.
 *
 * Port-layer only. No src/ menu code is touched: the overlay draws its own
 * fast3d 2D display list (appended after the game DL in gfx_run) and edits the
 * port-owned config.c variables directly. Live knobs apply immediately; the
 * two that need an FBO/window rebuild (MSAA, Fullscreen) are tagged "(restart)".
 *
 * The panel adapts to whatever 2D space it is drawn in (320x240 in-game vs
 * 440x330 on front-end screens -- viSetXY differs) and scrolls when the row
 * list outgrows the viewport (wheel / arrows at the edges). Cyclic rows
 * (MSAA, texture filter, resolution, toggles) wrap in both directions; the
 * manual % rows (draw/LOD distance) are hidden while their "auto" toggle is on.
 *
 * Text + fill helpers are the game's own (textRender / microcode_constructor /
 * gDPFillRectangle) reached by extern -- same pattern input.c uses to read
 * current_menu / cursor_h_pos. This is a rendering/UI view, not a logic change.
 *
 * Diagnostic: set GE_OPTIONSOVERLAY=1 to auto-open at boot (headless layout
 * check). Env-gated, harmless when unset.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include <PR/ultratypes.h>
/* gbi.h's gDP* DL macros use _SHIFTL/_SHIFTR but do not define them -- game TUs
 * get them from <ultra64.h>/<PR/mbi.h>, which also drags in N64 OS headers that
 * shadow libc here. Define the two pure macros locally (verbatim from mbi.h) so
 * this stays a plain port TU. Without them GCC/ld fails "undefined reference to
 * _SHIFTL" (MinGW's chain happens to provide it). */
#ifndef _SHIFTL
#define _SHIFTL(v, s, w) ((u32)(((u32)(v) & ((0x01 << (w)) - 1)) << (s)))
#define _SHIFTR(v, s, w) ((u32)(((u32)(v) >> (s)) & ((0x01 << (w)) - 1)))
#endif
#include <PR/gbi.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "video.h"
#include "input.h"
#include "optionsoverlay.h"
#include "../fast3d/gfx_api.h"

/* ---- game symbols (rendering/UI only; see input.c for the same pattern) ---- */
struct font;
struct fontchar;
extern struct font     *ptrFontBankGothic;
extern struct fontchar *ptrFontBankGothicChars;
extern Gfx  *microcode_constructor(Gfx *gdl);
extern Gfx  *textRender(Gfx *gdl, s32 *x, s32 *y, char *text, struct fontchar *chars,
                        struct font *font, u32 colour, s32 width, s32 height,
                        u32 yOffset, s32 lineheight);
extern void  textMeasure(s32 *textheight, s32 *textwidth, char *text,
                         struct fontchar *chars, struct font *font, s32 lineheight);
extern s16   viGetX(void);
extern s16   viGetY(void);

/* ------------------------------------------------------------------------ */

enum { ROW_TOGGLE, ROW_SLIDER, ROW_ENUM, ROW_MSAA, ROW_RES, ROW_ACTION, ROW_FPSCAP, ROW_BIND };

static const char *const kOnOff[]     = { "OFF", "ON", NULL };
static const char *const kTexFilter[] = { "NEAREST", "BILINEAR", "3-POINT", NULL };
static const int         kMsaaSeq[]   = { 1, 2, 4, 8 };
/* D186: the sim's own tick pacemaker is hardcoded to the console's native VI
 * rate (60Hz NTSC / 50Hz PAL, port/src/libultra.c) -- Video.FpsCap can only
 * throttle down from there, never past it, and throttling it below 30
 * throttles game logic itself (video.c already force-uncaps anything under
 * 30). A free 0-360 slider therefore had a huge dead zone (every value above
 * the console rate is a no-op, every value 1-29 silently snaps to 0) with
 * only two states that actually do anything. Exposed as a plain 30/60 toggle
 * instead (user ask, 2026-09-18). A third "uncapped" (0, skip the port's own
 * frame-pacing wait) state exists at the config level and old inis may still
 * have it, but it's dropped from the menu: with VSync on (the default) it's
 * indistinguishable from 60, and with VSync off it just burns GPU time
 * re-presenting the same simulated frame -- confusing for no real benefit. */
static const int         kFpsCapSeq[] = { 30, 60 };

/* Windowed-mode resolution presets. Filtered at init to those that fit the
 * desktop; the Resolution row cycles the surviving list. */
static const int kResList[][2] = {
    {  640,  480 }, {  800,  600 }, {  960,  720 }, { 1024,  768 },
    { 1152,  864 }, { 1280,  720 }, { 1280,  800 }, { 1280,  960 },
    { 1366,  768 }, { 1440,  900 }, { 1600,  900 }, { 1600, 1200 },
    { 1680, 1050 }, { 1920, 1080 }, { 1920, 1200 }, { 2560, 1440 },
    { 3200, 1800 }, { 3840, 2160 },
};
#define NUM_RES ((int)(sizeof(kResList) / sizeof(kResList[0])))
static int s_resFit[NUM_RES];   /* indices into kResList that fit the desktop */
static int s_resFitN = 0;
static int s_resSel  = 0;       /* index into s_resFit */

struct Row {
    const char        *key;
    const char        *label;
    int                kind;
    double             step;
    const char *const *names;    /* ROW_TOGGLE / ROW_ENUM value names */
    int                restart;  /* value change needs a restart      */
    double             uiMin, uiMax; /* 0,0 -> use the registered clamp */

    /* resolved from config.c at init */
    int                found;
    int                type;     /* CONFIG_OPT_*    */
    void              *ptr;
    double             cfgMin, cfgMax;

    /* Hidden while the option named here is nonzero (a manual % row
     * disappears while its "auto" toggle is on). Resolved to hidePtr at init. */
    const char        *hiddenIfOn;
    int               *hidePtr;
};

static struct Row rows[] = {
    { "Video.Fullscreen",         "Fullscreen",       ROW_TOGGLE, 1,    kOnOff,     0, 0, 0,   0,0,0,0,0 },
    { "__Resolution",             "Resolution",       ROW_RES,    0,    NULL,       0, 0, 0,   0,0,0,0,0 },
    { "Video.VSync",              "VSync",            ROW_TOGGLE, 1,    kOnOff,     0, 0, 0,   0,0,0,0,0 },
    { "Video.FpsCap",             "Frame cap",        ROW_FPSCAP, 0,    NULL,       0, 0, 0,   0,0,0,0,0 },
    { "Video.MSAA",               "MSAA",             ROW_MSAA,   0,    NULL,       1, 0, 0,   0,0,0,0,0 },
    { "Video.TextureFilter",      "Texture filter",   ROW_ENUM,   1,    kTexFilter, 0, 0, 0,   0,0,0,0,0 },
    { "Video.Anisotropy",         "Anisotropic",      ROW_SLIDER, 1,    NULL,       0, 0, 0,   0,0,0,0,0 },
    { "Video.FovScale",           "FOV scale %",      ROW_SLIDER, 5,    NULL,       0, 0, 0,   0,0,0,0,0 },
    { "Video.WidescreenAuto",     "Widescreen auto FOV",ROW_TOGGLE,1,   kOnOff,     0, 0, 0,   0,0,0,0,0 },
    { "Video.SafeAreaCrop",       "Crop overscan bars", ROW_TOGGLE,1,   kOnOff,     0, 0, 0,   0,0,0,0,0 },
    { "Video.DrawDistance",       "Draw distance %",  ROW_SLIDER, 25,   NULL,       0, 0, 0,   0,0,0,0,0, "Video.DrawDistanceAutoFov" },
    { "Video.DrawDistanceAutoFov","Draw dist. auto",  ROW_TOGGLE, 1,    kOnOff,     0, 0, 0,   0,0,0,0,0 },
    { "Video.LodDistance",        "LOD distance %",   ROW_SLIDER, 25,   NULL,       0, 0, 0,   0,0,0,0,0, "Video.LodDistanceAutoFov" },
    { "Video.LodDistanceAutoFov", "LOD dist. auto",   ROW_TOGGLE, 1,    kOnOff,     0, 0, 0,   0,0,0,0,0 },
    /* Aim row edits Input.AimModeSens -- the knob the default GEPD aim path
     * actually uses (Input.MouseAimSpeed only feeds the legacy velocity-stick
     * fallback, so it was inert here). D304: both this row and the turn-speed
     * row below now use the (0,0) "inherit the registered clamp" sentinel
     * instead of hardcoded uiMin/uiMax -- they were previously (1,80) and
     * (0,100), two arbitrary, DIFFERENT, and (for turn speed) outright wrong
     * ceilings that didn't even match either row's own backing config clamp
     * (both are actually registered 1..500 in input.c) -- fixed as an earlier
     * part of this same D304 pass. **Follow-up, same day, user feedback after
     * trying the widened sliders live:** exposing BOTH per-mode knobs still
     * let a player decouple them into a bad, hard-to-diagnose state (e.g. aim
     * mode very slow, hipfire very fast, or vice versa) with no indication
     * anything was wrong -- worse than a narrow range. Per the user's request,
     * these two rows and the "Link aim/turn sens" toggle that tried to paper
     * over the same risk are pulled from the menu entirely; only the single
     * master `Input.MouseSensitivity` row below remains player-facing, exactly
     * mirroring the reference GEPD/PD injector model this input was ported
     * from (GEPD-Edition Mouse Injector's goldeneye.c -- source no longer
     * vendored in the public repo, see reference/mouse-injector/README.md) --
     * ONE user-facing
     * sensitivity value, with the two per-mode constants staying fixed at
     * their calibrated (38/50) defaults, never independently player-tunable.
     * Config vars + SensLink logic stay in `input.c`/`rowSet()` untouched
     * (ini power users can still hand-edit them; existing ini files with
     * either key keep working) -- only the menu surface changes, same
     * established pattern as the D181/D216 pulled rows below. */
    /* { "Input.AimModeSens",        "Mouse aim speed",  ROW_SLIDER, 5, NULL, 0, 0, 0, 0,0,0,0,0 }, */
    /* { "Input.MouseTurnSpeed",     "Mouse turn speed", ROW_SLIDER, 5, NULL, 0, 0, 0, 0,0,0,0,0 }, */
    /* { "Input.SensLink",           "Link aim/turn sens",ROW_TOGGLE,1, kOnOff, 0, 0, 0, 0,0,0,0,0 }, */
    { "Input.MouseSensitivity",   "Mouse sensitivity",ROW_SLIDER, 5,    NULL,       0, 0, 0,   0,0,0,0,0 },
    { "Input.MouseInvertY",       "Mouse invert Y",   ROW_TOGGLE, 1,    kOnOff,     0, 0, 0,   0,0,0,0,0 },
    /* D181/Game.ScreenShakeIntensity: user testing (v0.2.1) found the slider
     * "basically useless" -- viShake() is only called from explosion.c, so it
     * scales explosion shake alone; it never touches the always-on walking
     * head-bob or any getting-shot reaction, which is what "Screen shake"
     * reads as to a player. Pulled from the menu until it covers all
     * screen-shake/view-bob sources, not just explosions. Config var + fr.c
     * hook stay in place. */
    /* D232: the community "no damage flash" toggle (suppresses the red/green
     * hit-flash overlay in bondview2). */
    { "Game.NoHitFlash",          "No hit flash",     ROW_TOGGLE, 1,    kOnOff,     0, 0, 0,   0,0,0,0,0 },
    /* D216/Game.SkipIntro: user report (v0.2.1 testing) that it breaks audio
     * -- pulled from the menu until root-caused. Not exposed to players; the
     * config var + lv.c hook stay in place (dead unless an existing ini has
     * it set, which no menu path can do any more). Do not re-add without
     * fixing the underlying issue first. */
    /* D257: everything-unlocked goodie (default ON). Consumed at startup by
     * main.c -- applies from the next launch. */
    { "Game.AllUnlocked",         "All unlocked",     ROW_TOGGLE, 1,    kOnOff,     0, 0, 0,   0,0,0,0,0 },
    /* D293: only quit path used to be the OS window-close / Alt+F4 -- no
     * discoverable in-game way to exit, a real gap on Deck/controller-only
     * setups. Not config-backed (like __Resolution); activating it exits
     * the same way video.c's SDL_QUIT/Alt+F4 handlers already do. */
    /* Rebinding rows: step = index into input.c's bind table. Enter/A
     * starts a capture, Left/B restores the default. */
    { "__Bind0", "Key: Forward", ROW_BIND, 0, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind1", "Key: Back", ROW_BIND, 1, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind2", "Key: Strafe left", ROW_BIND, 2, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind3", "Key: Strafe right", ROW_BIND, 3, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind4", "Key: Turn left", ROW_BIND, 4, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind5", "Key: Turn right", ROW_BIND, 5, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind6", "Key: Fire", ROW_BIND, 6, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind7", "Key: Aim", ROW_BIND, 7, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind8", "Key: Action/Use", ROW_BIND, 8, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind9", "Key: Cancel/Back", ROW_BIND, 9, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind10", "Key: Lean left", ROW_BIND, 10, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind11", "Key: Start", ROW_BIND, 11, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind12", "Pad: Fire", ROW_BIND, 12, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind13", "Pad: Aim", ROW_BIND, 13, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind14", "Pad: Action/Use", ROW_BIND, 14, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind15", "Pad: Crouch", ROW_BIND, 15, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind16", "Pad: Next weapon", ROW_BIND, 16, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind17", "Pad: Prev weapon", ROW_BIND, 17, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__Bind18", "Pad: Start", ROW_BIND, 18, NULL, 0, 0, 0, 0,0,0,0,0 },
    { "__QuitToDesktop",          "Quit to desktop",  ROW_ACTION, 0,    NULL,       0, 0, 0,   0,0,0,0,0 },
};
#define NUM_ROWS ((int)(sizeof(rows) / sizeof(rows[0])))

static int  s_inited = 0;
static volatile int s_open = 0;
static int  s_sel = 0;        /* selection, index into s_visIdx (visible list) */

/* Visible-row list: rows whose hiddenIfOn option is nonzero are omitted
 * (manual % rows hide while their auto toggle is on). Rebuilt every frame in
 * overlayUpdateVisible(); s_scroll is the first visible-list entry drawn. */
static int  s_visIdx[NUM_ROWS];
static int  s_visN = 0;
static int  s_scroll = 0;

/* D213: optional on-screen FPS readout (PD parity: Video.DisplayFPS). Drawn
 * top-right whenever enabled, independent of the F10 panel. Config-only knob
 * (kept off the 13-row panel, which is at its layout limit) -- matches PD,
 * whose DisplayFPS is also file-only. Default 0 => emit path unchanged =>
 * golden dumps byte-identical. */
static int      s_showFps = 0;
static char     s_fpsText[16] = "";

PD_CONSTRUCTOR static void overlayConfigInit(void)
{
    configRegisterInt("Video.DisplayFPS", &s_showFps, 0, 1);
}

/* Sampled once per emitted frame; recomputes the string every ~0.5 s. */
static void fpsTick(void)
{
    static uint64_t winStartUs = 0;
    static int      frames = 0;

    uint64_t nowUs = sysGetMicroseconds();
    if (winStartUs == 0) {
        winStartUs = nowUs;
        return;
    }
    frames++;
    uint64_t dtUs = nowUs - winStartUs;
    if (dtUs >= 500000) {
        int fps = (int)((double)frames * 1e6 / (double)dtUs + 0.5);
        snprintf(s_fpsText, sizeof(s_fpsText), "%d FPS", fps);
        winStartUs = nowUs;
        frames = 0;
    }
}

/* Layout (game 2D pixel space = viGetX() x viGetY(), ~320x240). Shared by the
 * emit path and the mouse hit-testing in optionsOverlayHandleInput().
 * BankGothic caps are ~9 units tall here, so rows need ~16 units of pitch and
 * values are right-aligned to the panel edge to survive the wide font. */
#define OV_X0        20
#define OV_LABEL_X   28
#define OV_TOP       12
#define OV_LINE      15                       /* row pitch (13 rows must fit ~240) */
#define OV_HDR       2                        /* header rows above row 0 (title+hint) */
/* i is a VISIBLE-POSITION (already scroll-adjusted by the caller). */
#define OV_ROW_Y(i)  (OV_TOP + ((i) + OV_HDR) * OV_LINE)

/* How many rows fit between the header and the bottom edge of whatever 2D
 * space we are in right now (320x240 in-game, 440x330 on front-end screens --
 * viSetXY differs, see src/game/front.c). More rows than this => scroll. */
static int maxVisibleRows(void)
{
    int n = (viGetY() - 6 - OV_TOP - OV_HDR * OV_LINE) / OV_LINE;
    if (n < 4) {
        n = 4;
    }
    return n;
}
#define OV_RIGHT     (viGetX() - OV_X0)       /* right edge for right-aligned text */
#define OV_NUM_W     36                       /* reserved width for a slider's number */
#define OV_BAR_X     150

/* Slider fill bar span in overlay space (shared by emit + hit-testing). */
static void sliderBarSpan(s32 *x0, s32 *x1)
{
    *x0 = OV_BAR_X;
    *x1 = OV_RIGHT - OV_NUM_W;
    if (*x1 < *x0 + 16) {
        *x1 = *x0 + 16;
    }
}

/* Visible position (0..s_visN-1) the given overlay-space y falls in, or -1.
 *
 * D304 fix: this used to loop over ALL s_visN entries (0..s_visN-1)
 * regardless of which ones are actually scrolled into view and drawn this
 * frame -- OV_ROW_Y(p - s_scroll) is a plain linear function of p, so a row
 * sitting just past the last visibly-drawn one (pLast, see the emit path)
 * still produces a geometrically valid, in-range Y band immediately below
 * the panel's real bottom edge. A click near that boundary (real-pixel-to-
 * virtual-2D-space rounding in the ox/oy scale-up, or simply a slightly low
 * click on the last visible row) could therefore resolve to the NEXT,
 * invisible, scrolled-off-the-bottom row instead of the one actually drawn
 * there. Concretely: with "All unlocked" sitting in the last visible slot,
 * this let a click on it silently hit "Quit to desktop" (the very next row)
 * instead -- reported as "I clicked unlock all and I think it crashed"
 * (user, 2026-09-18): the game didn't crash, __QuitToDesktop's ROW_ACTION
 * fired and closed it via the game's normal exit path. Fix: bound the scan
 * to the same [s_scroll, pLast] range the draw loop actually renders.
 *
 * D316 fix: the band used to be [Y-3, Y+12) -- 3 units above OV_ROW_Y but
 * 12 below it, a systematic downward bias baked in independent of the
 * mouse-mapping bug above. Centered here on OV_ROW_Y so a borderline click
 * no longer favors the row below. */
static int overlayRowAtY(double oy)
{
    int maxV = maxVisibleRows();
    int pLast = (s_visN - s_scroll < maxV) ? s_visN - 1 : s_scroll + maxV - 1;
    for (int p = s_scroll; p <= pLast; p++) {
        double top = OV_ROW_Y(p - s_scroll) - OV_LINE / 2;
        if (oy >= top && oy < top + OV_LINE) {
            return p;
        }
    }
    return -1;
}

/* Rebuild the visible-row list and keep the selection in range. */
static void overlayUpdateVisible(void)
{
    s_visN = 0;
    for (int i = 0; i < NUM_ROWS; i++) {
        if (!(rows[i].hidePtr && *rows[i].hidePtr != 0)) {
            s_visIdx[s_visN++] = i;
        }
    }
    if (s_visN == 0) {   /* cannot happen (toggles have no hide source) */
        s_visIdx[s_visN++] = 0;
    }
    if (s_sel < 0) {
        s_sel = 0;
    }
    if (s_sel >= s_visN) {
        s_sel = s_visN - 1;
    }
}

/* Keep the selected row on screen: shift the window ONLY when the selection
 * is actually outside it, by the minimum amount needed.
 *
 * D304 fix: this used to unconditionally set `s_scroll = s_sel - (maxV-1)`
 * -- i.e. bottom-anchor the selected row -- on every single call, including
 * every mouse click. A click on a row already visible (anywhere but the very
 * last slot) still forced the whole list to re-scroll so that row landed at
 * the bottom, shifting every row's on-screen position for the rest of that
 * same frame -- so whatever row the user then saw/clicked at that same
 * screen position was a DIFFERENT (usually the next, i.e. "below") setting.
 * Reported as "the F10 menu keeps jumping to the setting below when I left
 * click" (user, 2026-09-18) -- clicking any row not already at the bottom
 * slot reproduced it every time. Fix: only move the window when the
 * selection is above the top or below the bottom of the current view. */
static void overlayUpdateScroll(void)
{
    int maxV = maxVisibleRows();
    if (s_visN <= maxV) {
        s_scroll = 0;
        return;
    }
    if (s_sel < s_scroll) {
        s_scroll = s_sel;
    } else if (s_sel > s_scroll + maxV - 1) {
        s_scroll = s_sel - (maxV - 1);
    }
    if (s_scroll < 0) {
        s_scroll = 0;
    }
    if (s_scroll > s_visN - maxV) {
        s_scroll = s_visN - maxV;
    }
}

/* The close box brackets the title row at the panel's right edge. */
#define OV_CB_X0   (OV_RIGHT - 14)
#define OV_CB_X1   (OV_RIGHT + 7)
#define OV_CB_Y0   (OV_TOP - 3)
#define OV_CB_Y1   (OV_TOP + 12)
static int overlayInCloseBox(double ox, double oy)
{
    return ox >= OV_CB_X0 && ox <= OV_CB_X1 &&
           oy >= OV_CB_Y0 && oy <= OV_CB_Y1;
}

/* ------------------------------------------------------------------------ */

static void resolveCb(const char *key, int type, void *ptr, double min, double max,
                      double step, const char *label, const char *const *names,
                      void *ctx)
{
    (void)step; (void)label; (void)names; (void)ctx;
    for (int i = 0; i < NUM_ROWS; i++) {
        if (strcmp(rows[i].key, key) == 0) {
            rows[i].found  = 1;
            rows[i].type   = type;
            rows[i].ptr    = ptr;
            rows[i].cfgMin = min;
            rows[i].cfgMax = max;
            return;
        }
    }
}

static void overlayInit(void)
{
    if (s_inited) {
        return;
    }
    s_inited = 1;

    /* Publish display metadata so config.c / future consumers can see it,
     * without config.c knowing any specific key. */
    for (int i = 0; i < NUM_ROWS; i++) {
        configSetOptionMeta(rows[i].key, rows[i].label, rows[i].step, rows[i].names);
    }
    configForEachOption(resolveCb, NULL);

    for (int i = 0; i < NUM_ROWS; i++) {
        if (rows[i].kind == ROW_RES || rows[i].kind == ROW_ACTION || rows[i].kind == ROW_BIND) {
            rows[i].found = 1;   /* not config-backed */
            continue;
        }
        if (!rows[i].found) {
            sysLogPrintf(LOG_WARNING, "optionsoverlay: option '%s' not registered",
                         rows[i].key);
        }
    }

    /* Resolve the hidden-while-on sources (manual % rows vs their auto
     * toggles), then build the initial visible list. */
    for (int i = 0; i < NUM_ROWS; i++) {
        if (!rows[i].hiddenIfOn) {
            continue;
        }
        for (int j = 0; j < NUM_ROWS; j++) {
            if (strcmp(rows[j].key, rows[i].hiddenIfOn) == 0 && rows[j].found) {
                rows[i].hidePtr = rows[j].ptr;
                break;
            }
        }
    }
    overlayUpdateVisible();

    /* Build the windowed-resolution preset list: presets that fit the desktop,
     * plus the current window size snapped to the nearest surviving entry. */
    {
        int dw = 1920, dh = 1080;
        videoGetDesktopSize(&dw, &dh);
        s_resFitN = 0;
        for (int i = 0; i < NUM_RES; i++) {
            if (kResList[i][0] <= dw && kResList[i][1] <= dh) {
                s_resFit[s_resFitN++] = i;
            }
        }
        if (s_resFitN == 0) {
            s_resFit[s_resFitN++] = 0;
        }
        int cw = 0, ch = 0;
        videoGetWindowSize(&cw, &ch);
        long best = -1;
        for (int k = 0; k < s_resFitN; k++) {
            int i = s_resFit[k];
            long d = labs((long)kResList[i][0] - cw) +
                     labs((long)kResList[i][1] - ch);
            if (best < 0 || d < best) { best = d; s_resSel = k; }
        }
    }

    const char *e = getenv("GE_OPTIONSOVERLAY");
    if (e && atoi(e) != 0) {
        s_open = 1;
        sysLogPrintf(LOG_INFO, "optionsoverlay: auto-opened (GE_OPTIONSOVERLAY)");
    }
}

static double rowGet(const struct Row *r)
{
    if (!r->found || !r->ptr) {
        return 0.0;
    }
    switch (r->type) {
    case CONFIG_OPT_INT:   return (double)*(int *)r->ptr;
    case CONFIG_OPT_UINT:  return (double)*(unsigned int *)r->ptr;
    case CONFIG_OPT_FLOAT: return (double)*(float *)r->ptr;
    default:               return 0.0;
    }
}

static double rowLo(const struct Row *r)
{
    return (r->uiMin != r->uiMax) ? r->uiMin : r->cfgMin;
}
static double rowHi(const struct Row *r)
{
    return (r->uiMin != r->uiMax) ? r->uiMax : r->cfgMax;
}

static struct Row *rowByKey(const char *key)
{
    for (int i = 0; i < NUM_ROWS; i++) {
        if (strcmp(rows[i].key, key) == 0) {
            return &rows[i];
        }
    }
    return NULL;
}

static int s_linkDepth = 0;   /* re-entrancy guard for the sens link below */

static void rowSet(struct Row *r, double v)
{
    double lo = rowLo(r), hi = rowHi(r);
    if (lo != hi) {
        if (v < lo) v = lo;
        if (v > hi) v = hi;
    }
    switch (r->type) {
    case CONFIG_OPT_INT:   *(int *)r->ptr = (int)lround(v); break;
    case CONFIG_OPT_UINT:  *(unsigned int *)r->ptr = (unsigned int)(v < 0 ? 0 : lround(v)); break;
    case CONFIG_OPT_FLOAT: *(float *)r->ptr = (float)v; break;
    default: return;
    }

    /* Live-apply the video knobs that need a fast3d/SDL call. Everything else
     * is read straight off the pointer by its owner every frame/poll. */
    if (strcmp(r->key, "Video.Fullscreen") == 0) {
        videoRequestFullscreen((int)lround(v));
    } else if (strncmp(r->key, "Video.", 6) == 0 && !r->restart) {
        videoRequestLiveConfig();
    }

    /* Linked aim/turn sensitivity (Input.SensLink, default on): moving either
     * knob scales the other to hold the stock default ratio -- AimModeSens 38
     * : MouseTurnSpeed 50 (the D194/D238 calibrated defaults). */
    if (!s_linkDepth && strcmp(r->key, "Input.SensLink") != 0) {
        struct Row *lk = rowByKey("Input.SensLink");
        if (lk && lk->found && *(int *)lk->ptr != 0) {
            struct Row *o = NULL;
            double nv = 0.0;
            if (strcmp(r->key, "Input.MouseTurnSpeed") == 0) {
                o = rowByKey("Input.AimModeSens");
                nv = v * 38.0 / 50.0;
            } else if (strcmp(r->key, "Input.AimModeSens") == 0) {
                o = rowByKey("Input.MouseTurnSpeed");
                nv = v * 50.0 / 38.0;
            }
            if (o && o->found) {
                s_linkDepth = 1;
                rowSet(o, nv);
                s_linkDepth = 0;
            }
        }
    }
}

static void rowAdjust(struct Row *r, int dir)
{
    if (!r->found) {
        return;
    }
    double v = rowGet(r);
    switch (r->kind) {
    case ROW_TOGGLE:
        rowSet(r, (v != 0.0) ? 0.0 : 1.0);
        break;
    case ROW_MSAA: {
        int idx = 0;
        for (int i = 0; i < 4; i++) {
            if (kMsaaSeq[i] == (int)lround(v)) idx = i;
        }
        /* Wrap like a normal settings-menu cycle: OFF->2x->4x->8x->OFF, in
         * both directions (left/right click and arrows all roll). */
        idx = (idx + dir + 4) % 4;
        rowSet(r, (double)kMsaaSeq[idx]);
        break;
    }
    case ROW_ENUM: {
        double lo = r->cfgMin, hi = r->cfgMax;
        v += dir;
        if (v < lo) v = hi;
        if (v > hi) v = lo;
        rowSet(r, v);
        break;
    }
    case ROW_FPSCAP: {
        int idx = 0;
        for (int i = 0; i < 2; i++) {
            if (kFpsCapSeq[i] == (int)lround(v)) idx = i;
        }
        idx = (idx + dir + 2) % 2;
        rowSet(r, (double)kFpsCapSeq[idx]);
        break;
    }
    case ROW_RES: {
        if (s_resFitN <= 0 || videoIsFullscreen()) {
            break;   /* resolution is windowed-only */
        }
        s_resSel += (dir >= 0) ? 1 : -1;
        if (s_resSel < 0) s_resSel = s_resFitN - 1;
        if (s_resSel >= s_resFitN) s_resSel = 0;
        int i = s_resFit[s_resSel];
        videoRequestWindowSize(kResList[i][0], kResList[i][1]);
        break;
    }
    case ROW_BIND:
        if (dir > 0) inputBindBegin((int)r->step);
        else         inputBindReset((int)r->step);
        break;
    case ROW_ACTION:
        /* D293: same exit path as SDL_QUIT / Alt+F4 (video.c), just reachable
         * without OS window chrome or a keyboard. */
        sysLogPrintf(LOG_INFO, "optionsoverlay: quit to desktop requested");
        configSave();
        exit(0);
        break;
    default: /* ROW_SLIDER */
        rowSet(r, v + dir * r->step);
        break;
    }
}

/* ------------------------------------------------------------------------ */

void optionsOverlayToggle(void)
{
    overlayInit();
    s_open = !s_open;
    sysLogPrintf(LOG_INFO, "optionsoverlay: %s", s_open ? "opened" : "closed");
    if (!s_open) {
        configSave();
    }
}

int optionsOverlayIsOpen(void)
{
    if (!s_inited) {
        overlayInit();
    }
    return s_open;
}

void optionsOverlayScroll(int dir)
{
    if (!s_inited) {
        overlayInit();
    }
    if (!s_open || dir == 0) {
        return;
    }
    overlayUpdateVisible();
    s_sel += (dir > 0) ? -1 : 1;   /* wheel-up -> move up the list */
    /* Clamp at both ends like a normal PC settings list -- wrapping made the
     * selection "repeat" from the far edge, which read as a duplicate. */
    if (s_sel < 0) s_sel = 0;
    if (s_sel >= s_visN) s_sel = s_visN - 1;
}

/* Set a slider row from an overlay-space x inside its value bar, snapped to
 * the row's step. */
static void sliderSetFromX(struct Row *r, double ox)
{
    double lo = rowLo(r), hi = rowHi(r);
    if (hi <= lo) {
        return;
    }
    s32 bx0, bx1;
    sliderBarSpan(&bx0, &bx1);
    double f = (ox - bx0) / (double)(bx1 - bx0);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    double v = lo + f * (hi - lo);
    double step = (r->step > 0.0) ? r->step : 1.0;
    v = lround(v / step) * step;
    rowSet(r, v);
}

/* D314 (findings.md): F10 menu items flicker/mis-land on the file-select
 * screen, mechanism unpinned. Ruled out: viGetX/viGetY (stable per-screen,
 * set once by front.c) and non-determinism in the display list. This logs
 * the remaining suspects -- viGetY(), s_visN, s_scroll, s_sel -- once per
 * frame while the overlay is open, to catch whichever one oscillates during
 * a file-select repro. Env-gated, cached once (not re-queried per frame --
 * see D302: a hot-path getenv() regression cost a real bug before). */
static int s_d314Enabled = -1;   /* -1 = not yet resolved */

void optionsOverlayHandleInput(void)
{
    static int prevUp, prevDn, prevLf, prevRt, prevLmb, prevRmb;
    static int dragRow = -1;

    if (!s_open) {
        prevUp = prevDn = prevLf = prevRt = prevLmb = prevRmb = 0;
        dragRow = -1;
        return;
    }

    if (s_d314Enabled < 0) {
        s_d314Enabled = getenv("GE_D314") ? 1 : 0;
    }
    if (s_d314Enabled) {
        fprintf(stderr, "GE_D314 viGetY=%d visN=%d scroll=%d sel=%d\n",
                (int)viGetY(), s_visN, s_scroll, s_sel);
    }

    /* The overlay owns the mouse while it is open: force the OS cursor free +
     * visible (a stage poll would otherwise leave it locked/hidden). */
    inputSuspendForOverlay();

    if (inputBindPoll()) {
        return;   /* capturing a key/pad input for a rebind */
    }

    overlayUpdateVisible();   /* % rows may have appeared/vanished (auto toggles) */
    overlayUpdateScroll();

    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    int mx = 0, my = 0;
    Uint32 mb = SDL_GetMouseState(&mx, &my);
    int lmb = (mb & SDL_BUTTON(SDL_BUTTON_LEFT))  ? 1 : 0;
    int rmb = (mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? 1 : 0;

    /* Gamepad navigation (controller-only machines, e.g. Steam Deck): the
     * D-pad or left stick moves the selection; A/X step forward (the Enter
     * equivalent), B/Y step back; Start closes. OR-ed into the same edge
     * logic as the keyboard, so repeat/clamp/scroll behaviour is identical.
     * input.c swallows the pad while we are open, so none of this reaches
     * the game. (Select also closes -- handled in input.c's toggle, which
     * runs before this one.) */
    int gUp = inputPadButton(0, SDL_CONTROLLER_BUTTON_DPAD_UP)
           || inputPadAxis(0, SDL_CONTROLLER_AXIS_LEFTY) < -12000;
    int gDn = inputPadButton(0, SDL_CONTROLLER_BUTTON_DPAD_DOWN)
           || inputPadAxis(0, SDL_CONTROLLER_AXIS_LEFTY) > 12000;
    int up = ks[SDL_SCANCODE_UP]    || ks[SDL_SCANCODE_KP_8] || gUp;
    int dn = ks[SDL_SCANCODE_DOWN]  || ks[SDL_SCANCODE_KP_2] || gDn;
    int lf = ks[SDL_SCANCODE_LEFT]  || ks[SDL_SCANCODE_KP_4]
          || inputPadButton(0, SDL_CONTROLLER_BUTTON_B)
          || inputPadButton(0, SDL_CONTROLLER_BUTTON_Y);
    int rt = ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_KP_6]
          || ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER]
          || inputPadButton(0, SDL_CONTROLLER_BUTTON_A)
          || inputPadButton(0, SDL_CONTROLLER_BUTTON_X);

    static int prevStart = 0;   /* not reset while closed: a Start held across
                                  close must not re-close on the next open */
    int startNow = inputPadButton(0, SDL_CONTROLLER_BUTTON_START);
    if (startNow && !prevStart) optionsOverlayToggle();   /* Start closes */
    prevStart = startNow;

    /* ---- keyboard / D-pad nav (clamped at the ends; scroll follows) ---- */
    if (up && !prevUp && s_sel > 0) s_sel--;
    if (dn && !prevDn && s_sel < s_visN - 1) s_sel++;
    overlayUpdateScroll();
    if (lf && !prevLf) rowAdjust(&rows[s_visIdx[s_sel]], -1);
    if (rt && !prevRt) rowAdjust(&rows[s_visIdx[s_sel]], +1);

    /* ---- mouse ----
     * D316: mx/my are raw window pixels, but the overlay's own 2D content
     * is drawn into whatever on-window rect the safe-area crop currently
     * maps the logical (viGetX() x viGetY()) canvas to -- NOT the full
     * window whenever that rect is inset (default-on: any in-game "Full"
     * viewport insets it). Map through the real forward transform's rect
     * (gfx_get_ui_screen_rect) instead of a naive window-size scale so a
     * click lands on the same row it visually appears over. */
    int32_t rx = 0, ry = 0, rw = 0, rh = 0;
    gfx_get_ui_screen_rect(&rx, &ry, &rw, &rh);
    if (rw > 0 && rh > 0) {
        double ox = (double)(mx - rx) * (double)viGetX() / rw;
        double oy = (double)(my - ry) * (double)viGetY() / rh;
        int hoverVis = overlayRowAtY(oy);
        int onClose  = overlayInCloseBox(ox, oy);

        /* No hover-to-highlight: merely moving the mouse must not move the
         * selection or scroll the window (hovering near a list edge fed the
         * new row back into the cursor and made the bottom twitch/echo).
         * Selection moves only by click, wheel, or arrows. */

        /* left press. A click in the label column only focuses the row; a
         * click in the value/control column (>= the bar-span start) changes
         * it -- so clicking to select a toggle doesn't also flip it. */
        s32 bx0, bx1;
        sliderBarSpan(&bx0, &bx1);
        if (lmb && !prevLmb) {
            if (onClose) {
                optionsOverlayToggle();   /* close + configSave */
                return;
            }
            if (hoverVis >= 0) {
                s_sel = hoverVis;         /* explicit click -> select */
                overlayUpdateScroll();
                if (ox >= bx0) {
                    struct Row *r = &rows[s_visIdx[hoverVis]];
                    if (r->kind == ROW_SLIDER && r->found) {
                        sliderSetFromX(r, ox);
                        dragRow = s_visIdx[hoverVis];
                    } else {
                        rowAdjust(r, +1);   /* toggle / cycle forward (wraps) */
                    }
                }
            }
        }
        /* drag a slider */
        if (lmb && dragRow >= 0 && rows[dragRow].kind == ROW_SLIDER) {
            sliderSetFromX(&rows[dragRow], ox);
        }
        if (!lmb) {
            dragRow = -1;
        }
        /* right press in the value column: cycle back / decrement */
        if (rmb && !prevRmb && hoverVis >= 0 && !onClose && ox >= bx0) {
            s_sel = hoverVis;
            overlayUpdateScroll();
            rowAdjust(&rows[s_visIdx[hoverVis]], -1);
        }
    }

    prevUp = up; prevDn = dn; prevLf = lf; prevRt = rt;
    prevLmb = lmb; prevRmb = rmb;
}

/* ------------------------------------------------------------------------ */

#define OV_BUF_CMDS 8192
static Gfx s_buf[OV_BUF_CMDS];

static Gfx *fillRect(Gfx *gdl, s32 x0, s32 y0, s32 x1, s32 y1,
                     u8 r, u8 g, u8 b, u8 a)
{
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gdl++, 0, 0, r, g, b, a);
    gDPFillRectangle(gdl++, x0, y0, x1, y1);
    return gdl;
}

static void valueText(const struct Row *r, char *out, int n)
{
    double v = rowGet(r);
    if (r->kind == ROW_RES) {
        if (videoIsFullscreen()) {
            snprintf(out, n, "(fullscreen)");
        } else if (s_resFitN <= 0) {
            snprintf(out, n, "n/a");
        } else {
            int i = s_resFit[s_resSel];
            snprintf(out, n, "%d x %d", kResList[i][0], kResList[i][1]);
        }
        return;
    }
    if ((r->kind == ROW_TOGGLE || r->kind == ROW_ENUM) && r->names) {
        int idx = (int)lround(v);
        int cnt = 0;
        while (r->names[cnt]) cnt++;
        if (idx >= 0 && idx < cnt) {
            snprintf(out, n, "%s", r->names[idx]);
            return;
        }
    }
    if (r->kind == ROW_MSAA) {
        if ((int)lround(v) <= 1) snprintf(out, n, "OFF");
        else                     snprintf(out, n, "%dx", (int)lround(v));
        return;
    }
    if (r->kind == ROW_BIND) {
        inputBindText((int)r->step, out, n);
        return;
    }
    if (r->kind == ROW_ACTION) {
        snprintf(out, n, "[ENTER]");
        return;
    }
    if (r->kind == ROW_SLIDER && r->type == CONFIG_OPT_FLOAT) {
        snprintf(out, n, "%.2f", v);
        return;
    }
    if (r->kind == ROW_FPSCAP) {
        int fps = (int)lround(v);
        if (fps <= 0) snprintf(out, n, "Uncapped");
        else          snprintf(out, n, "%d FPS", fps);
        return;
    }
    snprintf(out, n, "%d", (int)lround(v));
}

static Gfx *drawText(Gfx *gdl, s32 x, s32 y, const char *str, u32 colour)
{
    s32 px = x, py = y;
    /* width/height are the on-screen CLIP rect textRenderGlyph tests against
     * (clipX=start x, clipY=start y, +clipWidth/+clipHeight), NOT the text's
     * own measured size -- passing the measured w/h clipped every glyph out
     * (baseline+height > measured h => nothing drawn).  Match the game: pass
     * the full 2D viewport, like bondview2.c's debug-text path. */
    return textRender(gdl, &px, &py, (char *)str, ptrFontBankGothicChars,
                      ptrFontBankGothic, colour, viGetX(), viGetY(), 0, 0);
}

static s32 measureText(const char *str)
{
    s32 h = 0, w = 0;
    textMeasure(&h, &w, (char *)str, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    return w;
}

/* Right-aligned: the string ends at xr. */
static Gfx *drawTextR(Gfx *gdl, s32 xr, s32 y, const char *str, u32 colour)
{
    return drawText(gdl, xr - measureText(str), y, str, colour);
}

Gfx *optionsOverlayEmit(void)
{
    if (!s_inited) {
        overlayInit();
    }

    fpsTick();

    if (!s_open) {
        if (!s_showFps || !s_fpsText[0]) {
            return NULL;   /* nothing appended -> golden dumps byte-identical */
        }
        /* D213: FPS-only mini DL (top-right), panel closed. */
        const s32 fw = viGetX();
        const s32 fh = viGetY();
        Gfx *fgdl = s_buf;
        gDPPipeSync(fgdl++);
        gDPSetCycleType(fgdl++, G_CYC_1CYCLE);
        gDPSetTexturePersp(fgdl++, G_TP_NONE);
        gDPSetScissor(fgdl++, G_SC_NON_INTERLACE, 0, 0, fw, fh);
        fgdl = microcode_constructor(fgdl);
        fgdl = drawTextR(fgdl, fw - 6, 6, s_fpsText, 0x40ff60ff);
        gDPPipeSync(fgdl++);
        gSPEndDisplayList(fgdl++);
        return s_buf;
    }

    overlayUpdateVisible();
    overlayUpdateScroll();

    const s32 W = viGetX();
    const s32 H = viGetY();
    const s32 right = OV_RIGHT;
    const s32 panelTop = OV_TOP - 9;
    /* Last visible position actually drawn (window may be shorter than the
     * list at small 2D viewports -- the rest is reached by scrolling).
     * D304 fix: the "everything fits" branch was `s_visN - s_scroll`, which
     * is a COUNT, not the last valid 0-based index -- since that branch only
     * runs when s_scroll==0 (overlayUpdateScroll's own invariant), this
     * evaluated to `s_visN`, one past the last valid entry, so both draw
     * loops below (`for (p = s_scroll; p <= pLast; p++)`) read one row past
     * the end of `s_visIdx[]`/`rows[]` on any 2D viewport large enough to fit
     * the whole list without scrolling (e.g. the larger front-end 440x330
     * screen once a couple of rows are pulled from the menu, as just
     * happened above). Should be `s_visN - 1`, matching overlayRowAtY's
     * identical fix. */
    const int maxV  = maxVisibleRows();
    const int pLast = (s_visN - s_scroll < maxV) ? s_visN - 1
                                                 : s_scroll + maxV - 1;
    const s32 panelBottom = OV_ROW_Y(pLast) + OV_LINE / 2 + 3;
    s32 bx0, bx1;
    sliderBarSpan(&bx0, &bx1);
    Gfx *gdl = s_buf;

    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetTexturePersp(gdl++, G_TP_NONE);
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, W, H);

    /* ---- pass 1: all fills (G_CC_PRIMITIVE) ---- */
    gdl = fillRect(gdl, 0, 0, W, H, 0, 0, 0, 150);                       /* dim */
    gdl = fillRect(gdl, OV_X0 - 8, panelTop, W - (OV_X0 - 8), panelBottom,
                   8, 10, 24, 210);                                     /* panel */
    gdl = fillRect(gdl, OV_CB_X0, OV_CB_Y0, OV_CB_X1, OV_CB_Y1,
                   150, 40, 40, 235);                                   /* close */

    for (int p = s_scroll; p <= pLast; p++) {
        const struct Row *r = &rows[s_visIdx[p]];
        s32 rowY = OV_ROW_Y(p - s_scroll);
        if (p == s_sel) {
            gdl = fillRect(gdl, OV_X0 - 4, rowY - 3, W - (OV_X0 - 4),
                           rowY + OV_LINE - 4, 40, 46, 96, 220);
        }
        if (r->kind == ROW_SLIDER && r->found) {
            double lo = rowLo(r), hi = rowHi(r);
            double f = (hi > lo) ? (rowGet(r) - lo) / (hi - lo) : 0.0;
            if (f < 0) f = 0; if (f > 1) f = 1;
            s32 by = rowY + 3;
            gdl = fillRect(gdl, bx0, by, bx1, by + 5, 60, 60, 70, 220);
            gdl = fillRect(gdl, bx0, by, bx0 + (s32)((bx1 - bx0) * f), by + 5,
                           210, 200, 90, 255);
        }
    }

    /* ---- pass 2: text ---- */
    gdl = microcode_constructor(gdl);

    gdl = drawText(gdl, OV_X0, OV_TOP, "PC OPTIONS", 0xffe040ff);
    gdl = drawText(gdl, OV_X0, OV_TOP + OV_LINE,
                   "select: scroll/click, change: mouse/arrows",
                   0x8890a0ff);                                        /* hint line */
    gdl = drawText(gdl, (OV_CB_X0 + OV_CB_X1) / 2 - measureText("X") / 2,
                   OV_TOP, "X", 0xffffffff);                            /* close glyph */

    for (int p = s_scroll; p <= pLast; p++) {
        const struct Row *r = &rows[s_visIdx[p]];
        s32 rowY = OV_ROW_Y(p - s_scroll);
        u32 col = (p == s_sel) ? 0xffffffff : 0xc0c0c8ff;
        char val[48];

        gdl = drawText(gdl, OV_LABEL_X, rowY, (char *)r->label,
                       r->found ? col : 0x808080ff);
        if (!r->found) {
            gdl = drawTextR(gdl, right, rowY, "(n/a)", 0x808080ff);
            continue;
        }

        valueText(r, val, sizeof(val));
        if (r->restart) {
            /* value left of the bar span, "(restart)" pinned to the edge */
            gdl = drawText(gdl, bx0, rowY, val, col);
            gdl = drawTextR(gdl, right, rowY, "(restart)", 0x909090ff);
        } else {
            gdl = drawTextR(gdl, right, rowY, val, col);
        }
    }

    gDPPipeSync(gdl++);
    gSPEndDisplayList(gdl++);

    if ((gdl - s_buf) > OV_BUF_CMDS) {
        sysLogPrintf(LOG_ERROR, "optionsoverlay: DL overflow (%d)", (int)(gdl - s_buf));
    }
    return s_buf;
}
