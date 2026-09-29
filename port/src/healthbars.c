/* healthbars.c -- compact health / armour bars (lower left), opt-in.
 * Finding D438 (presentation only).
 *
 *   Game.HealthBars  0 = N64: the watch arcs on hit (default, unchanged)
 *                    1 = Bars on hit: two compact bars in the N64 show window,
 *                        from the same delayed apparent* values (same drop)
 *                    2 = Bars always: the bars stay up, from the real values;
 *                        hidden when dead, while the watch / MP menu is up, in
 *                        cutscenes (NOCONTROL) and at game over
 *   GE_HEALTHBARS=<0|1|2>          env override of the mode (tests)
 *   GE_HEALTHBARS_TEST="<h>,<a>"  test knob: draw these values (0..1) whenever
 *                        the bars would draw, and in mode 1 draw regardless of
 *                        the show window (a hit cannot be scripted headless)
 *
 * Drawn inside the per-player HUD pass of maybe_mp_interface (bondview2.c), at
 * the spot where the N64 draws its arcs, so split-screen anchors are right and
 * nothing is written to game state. Values are read from g_CurrentPlayer.
 * 8 segments per bar, like the arc's 8 health units; the watch's colours. */
#include <stdio.h>
#include <stdlib.h>
#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include "platform.h"
#include "config.h"
#include "player.h"

extern s16 viGetViewLeft(void);
extern s16 viGetViewTop(void);
extern s16 viGetViewWidth(void);
extern s16 viGetViewHeight(void);
extern Gfx *microcode_constructor(Gfx *gdl);
extern Gfx *bondviewRenderGaugeBars(Gfx *gdl);
extern s32 g_gameOverFlag;
#define HB_GUNSIGHTREASON_NOCONTROL 0x04   /* bondconstants.h */

static int s_cfgMode = 0;
static int s_testParsed = 0, s_testOn = 0;
static float s_testH = 1.0f, s_testA = 0.0f;

PD_CONSTRUCTOR static void healthBarsConfigInit(void)
{
    configRegisterInt("Game.HealthBars", &s_cfgMode, 0, 2);
}

static int s_envMode = -2;   /* GE_HEALTHBARS=<0|1|2> wins over the ini (tests, loopback peers) */
int portHealthBarsMode(void)
{
    if (s_envMode == -2) {
        const char *e = getenv("GE_HEALTHBARS");
        s_envMode = (e && e[0] >= '0' && e[0] <= '2') ? e[0] - '0' : -1;
    }
    return s_envMode >= 0 ? s_envMode : s_cfgMode;
}

static void hbTest(void)
{
    if (!s_testParsed) {
        const char *e = getenv("GE_HEALTHBARS_TEST");
        s_testParsed = 1;
        if (e && sscanf(e, "%f,%f", &s_testH, &s_testA) >= 1) s_testOn = 1;
    }
}

/* Layout (canvas px, inside the player's viewport): 8 segments of 7 px with
 * 1 px gaps = 63 px wide, 5 px tall; health above armour, 3 px apart, 12 px
 * from the left edge, the armour bar 12 px above the bottom edge. */
#define HB_SEG_W 7
#define HB_SEGS 8
#define HB_H 5
#define HB_X 12
#define HB_BOTTOM 12

static Gfx *hbRect(Gfx *gdl, s32 x0, s32 y0, s32 x1, s32 y1, u8 r, u8 g, u8 b, u8 a)
{
    gDPSetPrimColor(gdl++, 0, 0, r, g, b, a);
    gDPFillRectangle(gdl++, x0, y0, x1, y1);
    return gdl;
}

static Gfx *hbBar(Gfx *gdl, s32 x, s32 y, float v, u8 r, u8 g, u8 b, u8 tr, u8 tg, u8 tb)
{
    int i;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    /* outline: a 1 px dark frame around the whole bar */
    gdl = hbRect(gdl, x - 1, y - 1, x + HB_SEGS * (HB_SEG_W + 1), y + HB_H + 1, 0, 0, 0, 160);
    for (i = 0; i < HB_SEGS; i++) {
        s32 sx = x + i * (HB_SEG_W + 1);
        float units = v * HB_SEGS - (float)i;      /* how much of this segment is filled */
        s32 fill = units >= 1.0f ? HB_SEG_W : units <= 0.0f ? 0 : (s32)(units * HB_SEG_W + 0.5f);
        gdl = hbRect(gdl, sx, y, sx + HB_SEG_W, y + HB_H, tr, tg, tb, 150);           /* track */
        if (fill > 0) gdl = hbRect(gdl, sx, y, sx + fill, y + HB_H, r, g, b, 235);     /* fill */
    }
    return gdl;
}

/* Draw both bars for the current player from `h` / `a` (0..1). */
static Gfx *hbDraw(Gfx *gdl, float h, float a)
{
    s32 x = viGetViewLeft() + HB_X;
    s32 bottom = viGetViewTop() + viGetViewHeight();
    s32 yA = bottom - HB_BOTTOM - HB_H;
    s32 yH = yA - HB_H - 3;
    gdl = microcode_constructor(gdl);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gdl = hbBar(gdl, x, yH, h, 255, 210, 48, 64, 52, 16);        /* health: the watch's yellow */
    if (a > 0.0f)
        gdl = hbBar(gdl, x, yA, a, 96, 144, 255, 20, 34, 72);    /* armour: the watch's blue */
    gDPPipeSync(gdl++);
    return gdl;
}

/* Hook A (bondview2.c, in place of the N64 gauge call inside its own show
 * gates: solo hit window, MP menu / 1 s pause display). */
Gfx *portHealthBarsGauge(Gfx *gdl)
{
    hbTest();
    if (portHealthBarsMode() == 0) return bondviewRenderGaugeBars(gdl);
    if (portHealthBarsMode() == 2) return gdl;   /* drawn by the always hook, not twice */
    return hbDraw(gdl, s_testOn ? s_testH : g_CurrentPlayer->apparenthealth,
                       s_testOn ? s_testA : g_CurrentPlayer->apparentarmour);
}

/* Hook B (bondview2.c, before the N64 gates): mode 2 draws every frame the
 * player is in control; the test knob also drives mode 1 outside a hit. */
Gfx *portHealthBarsAlways(Gfx *gdl)
{
    struct player *p = g_CurrentPlayer;
    const int mode = portHealthBarsMode();
    hbTest();
    if (mode == 0 || !p) return gdl;
    if (mode == 1 && !s_testOn) return gdl;
    if (p->bonddead || p->watch_animation_state != 0 || p->mpmenuon || g_gameOverFlag
        || (p->gunsightmode & HB_GUNSIGHTREASON_NOCONTROL)) return gdl;
    if (mode == 1 && p->healthshowtime > 0) return gdl;   /* the gate hook draws it then */
    return hbDraw(gdl, s_testOn ? s_testH : p->bondhealth, s_testOn ? s_testA : p->bondarmour);
}
