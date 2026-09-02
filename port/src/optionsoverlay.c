/*
 * F10 in-game options overlay -- approach (C) from docs/dev/OPTIONS-MENU-PLAN.md.
 *
 * Port-layer only. No src/ menu code is touched: the overlay draws its own
 * fast3d 2D display list (appended after the game DL in gfx_run) and edits the
 * port-owned config.c variables directly. Live knobs apply immediately; the
 * two that need an FBO/window rebuild (MSAA, Fullscreen) are tagged "(restart)".
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
#include <PR/gbi.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "video.h"
#include "optionsoverlay.h"

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

enum { ROW_TOGGLE, ROW_SLIDER, ROW_ENUM, ROW_MSAA };

static const char *const kOnOff[]     = { "OFF", "ON", NULL };
static const char *const kTexFilter[] = { "NEAREST", "BILINEAR", "3-POINT", NULL };
static const char *const kCapture[]   = { "ALWAYS GRAB", "CLICK-TO-LOCK", NULL };
static const int         kMsaaSeq[]   = { 1, 2, 4, 8 };

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
};

static struct Row rows[] = {
    { "Video.VSync",              "VSync",            ROW_TOGGLE, 1,    kOnOff,     0, 0, 0,   0,0,0,0,0 },
    { "Video.FpsCap",             "Frame cap",        ROW_SLIDER, 10,   NULL,       0, 0, 360, 0,0,0,0,0 },
    { "Video.MSAA",               "MSAA",             ROW_MSAA,   0,    NULL,       1, 0, 0,   0,0,0,0,0 },
    { "Video.TextureFilter",      "Texture filter",   ROW_ENUM,   1,    kTexFilter, 0, 0, 0,   0,0,0,0,0 },
    { "Input.MouseAimSpeed",      "Mouse aim speed",  ROW_SLIDER, 1,    NULL,       0, 0, 100, 0,0,0,0,0 },
    { "Input.MouseTurnSpeed",     "Mouse turn speed", ROW_SLIDER, 1,    NULL,       0, 0, 100, 0,0,0,0,0 },
    { "Input.MouseInvertY",       "Mouse invert Y",   ROW_TOGGLE, 1,    kOnOff,     0, 0, 0,   0,0,0,0,0 },
    { "Input.MouseCaptureMode",   "Mouse capture",    ROW_TOGGLE, 1,    kCapture,   0, 0, 0,   0,0,0,0,0 },
    { "Game.ScreenShakeIntensity","Screen shake",     ROW_SLIDER, 0.25, NULL,       0, 0, 3,   0,0,0,0,0 },
};
#define NUM_ROWS ((int)(sizeof(rows) / sizeof(rows[0])))

static int  s_inited = 0;
static volatile int s_open = 0;
static int  s_sel = 0;

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
        if (!rows[i].found) {
            sysLogPrintf(LOG_WARNING, "optionsoverlay: option '%s' not registered",
                         rows[i].key);
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
    if (strncmp(r->key, "Video.", 6) == 0 && !r->restart) {
        videoRequestLiveConfig();
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
        idx += dir;
        if (idx < 0) idx = 0;
        if (idx > 3) idx = 3;
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
    if (!s_open || dir == 0) {
        return;
    }
    s_sel += (dir > 0) ? -1 : 1;   /* wheel-up -> move up the list */
    if (s_sel < 0) s_sel = NUM_ROWS - 1;
    if (s_sel >= NUM_ROWS) s_sel = 0;
}

void optionsOverlayHandleInput(void)
{
    static int prev[6];   /* UP DOWN LEFT RIGHT RETURN MOUSE */
    if (!s_open) {
        memset(prev, 0, sizeof(prev));
        return;
    }

    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    Uint32 mb = SDL_GetMouseState(NULL, NULL);

    int cur[6];
    cur[0] = ks[SDL_SCANCODE_UP]     || ks[SDL_SCANCODE_KP_8];
    cur[1] = ks[SDL_SCANCODE_DOWN]   || ks[SDL_SCANCODE_KP_2];
    cur[2] = ks[SDL_SCANCODE_LEFT]   || ks[SDL_SCANCODE_KP_4];
    cur[3] = ks[SDL_SCANCODE_RIGHT]  || ks[SDL_SCANCODE_KP_6]
             || ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER];
    cur[4] = 0; /* reserved */
    cur[5] = (mb & SDL_BUTTON(SDL_BUTTON_LEFT)) ? 1 : 0;
    int rmb = (mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? 1 : 0;

    if (cur[0] && !prev[0]) {
        s_sel = (s_sel + NUM_ROWS - 1) % NUM_ROWS;
    }
    if (cur[1] && !prev[1]) {
        s_sel = (s_sel + 1) % NUM_ROWS;
    }
    if (cur[2] && !prev[2]) {
        rowAdjust(&rows[s_sel], -1);
    }
    if ((cur[3] && !prev[3]) || (cur[5] && !prev[5])) {
        rowAdjust(&rows[s_sel], +1);
    }
    static int prevRmb = 0;
    if (rmb && !prevRmb) {
        rowAdjust(&rows[s_sel], -1);
    }
    prevRmb = rmb;

    memcpy(prev, cur, sizeof(prev));
}

/* ------------------------------------------------------------------------ */

#define OV_BUF_CMDS 8192
static Gfx s_buf[OV_BUF_CMDS];

/* Layout (game 2D pixel space = viGetX() x viGetY(), ~320x240). */
#define OV_X0        26
#define OV_LABEL_X   34
#define OV_VALUE_X   176
#define OV_BAR_X     176
#define OV_BAR_W     84
#define OV_NUM_X     266
#define OV_TOP       24
#define OV_LINE      14

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
    if (r->kind == ROW_SLIDER && r->type == CONFIG_OPT_FLOAT) {
        snprintf(out, n, "%.2f", v);
        return;
    }
    if (r->kind == ROW_SLIDER && (int)lround(v) == 0 &&
        strcmp(r->key, "Video.FpsCap") == 0) {
        snprintf(out, n, "UNCAPPED");
        return;
    }
    snprintf(out, n, "%d", (int)lround(v));
}

static Gfx *drawText(Gfx *gdl, s32 x, s32 y, const char *str, u32 colour)
{
    s32 w = 0, h = 0;
    s32 px = x, py = y;
    textMeasure(&h, &w, (char *)str, ptrFontBankGothicChars, ptrFontBankGothic, 10);
    return textRender(gdl, &px, &py, (char *)str, ptrFontBankGothicChars,
                      ptrFontBankGothic, colour, w, h, 0, 10);
}

Gfx *optionsOverlayEmit(void)
{
    if (!s_inited) {
        overlayInit();
    }
    if (!s_open) {
        return NULL;   /* nothing appended -> golden dumps byte-identical */
    }

    const s32 W = viGetX();
    const s32 H = viGetY();
    const s32 panelBottom = OV_TOP + (NUM_ROWS + 3) * OV_LINE;
    Gfx *gdl = s_buf;

    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetTexturePersp(gdl++, G_TP_NONE);
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, W, H);

    /* ---- pass 1: all fills (G_CC_PRIMITIVE) ---- */
    gdl = fillRect(gdl, 0, 0, W, H, 0, 0, 0, 150);                       /* dim */
    gdl = fillRect(gdl, OV_X0 - 6, OV_TOP - 8, W - (OV_X0 - 6), panelBottom,
                   8, 10, 24, 205);                                     /* panel */

    for (int i = 0; i < NUM_ROWS; i++) {
        s32 rowY = OV_TOP + (i + 2) * OV_LINE;
        if (i == s_sel) {
            gdl = fillRect(gdl, OV_X0 - 3, rowY - 2, W - (OV_X0 - 3), rowY + OV_LINE - 3,
                           40, 46, 96, 220);
        }
        if (rows[i].kind == ROW_SLIDER && rows[i].found) {
            double lo = rowLo(&rows[i]), hi = rowHi(&rows[i]);
            double f = (hi > lo) ? (rowGet(&rows[i]) - lo) / (hi - lo) : 0.0;
            if (f < 0) f = 0; if (f > 1) f = 1;
            s32 by = rowY + 2;
            gdl = fillRect(gdl, OV_BAR_X, by, OV_BAR_X + OV_BAR_W, by + 6,
                           60, 60, 70, 220);
            gdl = fillRect(gdl, OV_BAR_X, by, OV_BAR_X + (s32)(OV_BAR_W * f), by + 6,
                           210, 200, 90, 255);
        }
    }

    /* ---- pass 2: text ---- */
    gdl = microcode_constructor(gdl);

    gdl = drawText(gdl, OV_X0, OV_TOP, "PC OPTIONS", 0xffe040ff);
    gdl = drawText(gdl, OV_X0 + 96, OV_TOP, "F10 close  arrows adjust", 0x9090a0ff);

    for (int i = 0; i < NUM_ROWS; i++) {
        s32 rowY = OV_TOP + (i + 2) * OV_LINE;
        u32 col = (i == s_sel) ? 0xffffffff : 0xc0c0c8ff;
        char val[48];

        if (!rows[i].found) {
            gdl = drawText(gdl, OV_LABEL_X, rowY, (char *)rows[i].label, 0x808080ff);
            gdl = drawText(gdl, OV_VALUE_X, rowY, "(n/a)", 0x808080ff);
            continue;
        }
        gdl = drawText(gdl, OV_LABEL_X, rowY, (char *)rows[i].label, col);

        valueText(&rows[i], val, sizeof(val));
        if (rows[i].kind == ROW_SLIDER) {
            gdl = drawText(gdl, OV_NUM_X, rowY, val, col);
        } else {
            gdl = drawText(gdl, OV_VALUE_X, rowY, val, col);
        }
        if (rows[i].restart) {
            gdl = drawText(gdl, OV_VALUE_X + 46, rowY, "(restart)", 0x909090ff);
        }
    }

    gDPPipeSync(gdl++);
    gSPEndDisplayList(gdl++);

    if ((gdl - s_buf) > OV_BUF_CMDS) {
        sysLogPrintf(LOG_ERROR, "optionsoverlay: DL overflow (%d)", (int)(gdl - s_buf));
    }
    return s_buf;
}
