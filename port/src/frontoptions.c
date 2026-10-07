/*
 * frontoptions.c -- D343: PC options as a GE front-end screen.
 *
 * GE's front end has no generic menu system, so PC settings get their own
 * screen, MENU_PC_OPTIONS (route B, docs/dev/OPTIONS-MENU-PLAN.md), built the
 * way GE builds its Cheat Options screen (front.c init/interface/constructor_
 * menu15_cheat): the open dossier with tabs and a blank CLASSIFIED page, black
 * Zurich text on the paper via frontPrintText, GE's translucent highlight
 * boxes, the PREVIOUS tab as "back", and the game's crosshair.
 *
 *   page 0: five functional categories; page 1: a category's settings;
 *   deeper pages: Input -> Bindings -> Movement/Actions and Gameplay -> HUD
 *
 * The sections and rows are the F10 overlay's row table, reached through the
 * row API in optionsoverlay.c, so both UIs edit the same settings.
 *
 * Game-code surface (Rule 2, user sign-off 2026-09-27): the MENU_PC_OPTIONS
 * enum value (bondconstants.h) and five one-line dispatch cases (front.c),
 * all #ifdef PORT. Entry is the "PC Options" label on file select, drawn by
 * the existing D343 hook in constructor_menu05_fileselect.
 *
 * Everything here runs on the game's main thread (the front-end tick and
 * constructor), like GE's own screens.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <ultra64.h>
#include <bondgame.h>
#include <bondconstants.h>
#include <fr.h>
#include <joy.h>
#include <music.h>
#include <snd.h>
#include "front.h"
#include "textrelated.h"
#include "language.h"

#include "platform.h"
#include "system.h"
#include "config.h"
#include "envflag.h"
#include "optionsoverlay.h"
#include "watchsettings.h"
#include "frontoptions.h"
#include "input.h"
#include "netgame.h"   /* D414: netgameOnlineActive (idle timer) */

/* front.c functions this screen shares with the cheat screen (not all are in
 * front.h). */
extern Gfx *frontSetupMenuBackground(Gfx *DL);
extern Gfx *frontAddPreviousTabText(Gfx *DL);
extern s32  frontCheckCursorOnPreviousTab(void);
extern Gfx *frontDrawCursor(Gfx *DL);
extern Gfx *frontPrintText(Gfx *gdl, s32 *x, s32 *y, s8 *text, s32 second_font_table,
                           s32 first_font_table, s32 arg6, s32 view_x, s32 view_y,
                           s32 arg9, s32 arga);
extern void load_walletbond(void);
extern void disable_all_switches(Model *arg0);                                   /* front.c:967 */
extern void set_item_visibility_in_objinstance(Model *objinstance, s32 item, s32 mode); /* front.c:968 */
extern s32  folder_selection_screen_option_icon;
extern struct rectbbox folder_option_ERASE_bound;   /* front.c:439 */

/* ---- colours on the paper (RRGGBBAA; 0xFF = opaque black, GE's ink) ---- */
#define INK        0x000000FFu
#define INK_ON     0xA00000FFu   /* the cheat screen's ON red */
#define INK_DIM    0x00000080u
#define HILITE     0x32          /* GE's menu highlight box (black, alpha 0x32) */

/* ---- layout, in the front end's 440x330 canvas (paper; tabs start at 390) ---- */
#define TITLE_Y    0x2B
#define ROW_X      0x37          /* the cheat list's x */
#define ROW_Y0     0x41          /* cheat list starts 0x35; +12 for the title line */
/* D346b: 20 -> 18 px so a full section fits the paper on one page. Hit bands
 * stay contiguous at any pitch >= 6; the 22px highlight box still clears the
 * next row (y+14 < y+17). D353: the largest section is now GRAPHICS at 11
 * rows (both auto-FOV toggles off); MAX_PROWS=15 keeps headroom, and
 * buildPages() logs any future overflow instead of silently truncating. */
#define ROW_DY     18
#define NUM_X      0x37          /* "1." on the section list */
#define SEC_X      0x4B
#define VAL_R      372           /* values right-aligned here */
#define BAR_X0     232
#define BAR_X1     316
#define ROW_HIT_X0 40.0f
#define ROW_HIT_X1 385.0f

/* ---- file-select label: right end of the Select / Copy / Erase bar ---- */
#define LABEL_X     351   /* D400: floor = NTSC Erase right (~323) + LABEL_GAP; JP's wider Erase pushes past it */
#define LABEL_CY    285   /* the bar's centre line, as Copy/Erase */
#define LABEL_GAP   28    /* D400: text-only now (icon removed), a plain word gap after Erase */
#define HIT_PAD     4

#define MAX_PAGES  8
#define MAX_PROWS  15            /* D346b: 12 -> 15 for the merged Video section (see ROW_DY) */
/* D406: content rows per page. The 440x330 paper ends at y=330; content row k
 * sits at y = ROW_Y0 + (k+2)*ROW_DY (the D356 profile row takes slot 1), so a
 * page fits while ROW_Y0 + (CAP+2)*ROW_DY + ~10px of text stays <= 330 (the
 * bottom line is reserved for the page hint): CAP = 11 at ROW_DY 18. D346b's 18px pitch was sized for 15 rows with no
 * profile row; D356's profile row plus the v0.4.0 Wave-A INPUT rows (17)
 * pushed the 14th row to y=335 -- off the bottom of the paper -- and rows
 * 15-17 (Crouch mode, Reset to defaults, Bindings...) were dropped by the
 * old silent page cap, unreachable from the front screen. Sections that
 * outgrow one page now page: the vertical stepper crosses page boundaries
 * and the title carries a (p/N) marker. The F10 overlay is unaffected
 * (it scrolls its own window, D345-D347). */
#define ROWS_PER_PAGE 11

static const char kLabel[]   = "PC Options";  /* ASCII only: issue #87 / D295 */
static const char kLabelNL[] = "PC Options\n"; /* height measure only, D400 */
/* D414: the online-play entry, one line under it (the bar has no room to its
 * right at 440 wide). Hit bands of the two labels never overlap. */
static const char kOnline[]   = "Online";
static const char kOnlineNL[] = "Online\n";
#define ONLINE_DY (14 + 2 * HIT_PAD + 2)

/* netui.c (its header pulls SDL; keep this TU game-only). */
extern int netuiIsOpen(void);
extern int netuiRequestOpen(void);

/* ---- screen state (game thread) ---- */
static int s_level = 0;          /* 0 = categories, 1 = category, 2/3 = nested pages */
static int s_page = 0;           /* current root category */
static int s_pageno = 0;         /* D406: page within the active section */
static int s_rowTotal = 0;       /* D406: visible rows in the active section */
static int s_subHeader = -1;     /* rows[] header index for the nested page */
static int s_hl = -1;            /* highlighted row / section, -1 = none */
static int s_dragRow = -1;       /* global row index being dragged */
static int s_repeatDir = 0, s_repeatTimer = 0;
/* D345(e): hold-to-repeat state for vertical row stepping (separate from
 * the left/right value-adjust repeat; both can be held at once). */
static int s_vrepeatDir = 0, s_vrepeatTimer = 0;

static int s_pageHdr[MAX_PAGES];
static int s_pageN = 0;
static int s_rowIdx[MAX_PROWS];
static int s_rowN = 0;
#define MAX_SECT_ROWS 48   /* D406: per-section gather buffer (INPUT = 17) */

/* ------------------------------------------------------------------------ */

static void playSfx(s16 id)
{
    sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, id, NULL);
}

static s32 measureW(const char *str)
{
    s32 h = 0, w = 0;
    textMeasure(&h, &w, (char *)str, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    return w;
}

static Gfx *ink(Gfx *DL, s32 x, s32 y, const char *str, u32 colour)
{
    return frontPrintText(DL, &x, &y, (s8 *)str, (s32)(uintptr_t)ptrFontZurichBoldChars,
                          (s32)(uintptr_t)ptrFontZurichBold, (s32)colour,
                          viGetX(), viGetY(), 0, 0);
}

static Gfx *inkR(Gfx *DL, s32 xr, s32 y, const char *str, u32 colour)
{
    return ink(DL, xr - measureW(str), y, str, colour);
}

/* D406b: per-row slider-bar origin. Long labels ("X axis look sensitivity
 * (controller)") used to run into the fixed 232px bar and overlap the track;
 * the bar now starts 10px past the label. Rows whose labels leave less than
 * 12px of track show the value only. */
static int rowBarX0(const char *label)
{
    int x0 = ROW_X + measureW(label) + 10;
    return x0 < BAR_X0 ? BAR_X0 : x0;
}

/* "MOUSE / AIM" -> "Mouse / Aim" (GE's menus use title case). */
static void titleCase(const char *in, char *out, int n)
{
    int j = 0, start = 1;
    for (int i = 0; in[i] && j < n - 1; i++) {
        char c = in[i];
        if (c >= 'A' && c <= 'Z' && !start) c = (char)(c - 'A' + 'a');
        if (c >= 'a' && c <= 'z' && start)  c = (char)(c - 'a' + 'A');
        start = (c == ' ' || c == '/');
        out[j++] = c;
    }
    out[j] = '\0';
}

static int rowY(int k)
{
    /* Every open section, including a nested section, has the profile row. */
    if (s_level >= 1) k++;
    return ROW_Y0 + k * ROW_DY;
}

static int activeHeader(void)
{
    return s_level >= 2 ? s_subHeader : s_pageHdr[s_page];
}

static void buildPages(void)
{
    int n = optionsRowCount();
    s_pageN = 0;
    for (int i = 0; i < n && s_pageN < MAX_PAGES; i++) {
        if (optionsRowIsHeader(i) && optionsRowHeaderParent(i) < 0)
            s_pageHdr[s_pageN++] = i;
    }
    if (s_page >= s_pageN) {
        s_page = 0;
    }
    s_rowN = 0;
    s_rowTotal = 0;
    if (s_pageN > 0) {
        /* D406: gather every visible row of the active section, then page it
         * (ROWS_PER_PAGE fits the paper, see above). The old code capped at
         * MAX_PROWS-1 and silently dropped the rest -- a warning, no access.
         * s_pageno is re-clamped on every build so rows that auto-hide
         * (aim range, auto-FOV) cannot strand the cursor past the end. */
        int tmp[MAX_SECT_ROWS];
        int tn = 0;
        for (int i = activeHeader() + 1; i < n && !optionsRowIsHeader(i); i++) {
            if (!optionsRowIsShown(i)) continue;
            if (tn < MAX_SECT_ROWS) tmp[tn++] = i;
        }
        s_rowTotal = tn;
        int pages = (tn + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
        if (pages < 1) pages = 1;
        if (s_pageno > pages - 1) s_pageno = pages - 1;
        int off = s_pageno * ROWS_PER_PAGE;
        for (int k = 0; k < ROWS_PER_PAGE && off + k < tn; k++)
            s_rowIdx[s_rowN++] = tmp[off + k];
    }
}

static int itemCount(void)
{
    /* D356: level 1 carries the top save-file row (item 0, not in rows[]) on
     * top of the page's content rows. */
    return s_level == 0 ? s_pageN : s_rowN + 1;
}

/* Put the crosshair on item k, unconditionally. */
static void cursorToItemRaw(int k)
{
    /* D345(g): snap into the left gutter (x=44), not onto the row text
     * (the old ROW_X+20 centred the sprite over the label/highlight box).
     * 44 stays inside [ROW_HIT_X0..1] so the highlight hit-test still
     * resolves row k, and clears the number column (NUM_X=55) / highlight
     * boxes (from x=53/73). The PREVIOUS-tab hitbox (x>390 && y>223) is
     * far away. Mouse users are unaffected: the pointer sits under the
     * real mouse, never at this snap position. */
    cursor_h_pos = 44.0f;
    cursor_v_pos = (f32)(rowY(k) + 6);
}

/* Put the crosshair on item k (entering a page / going back). */
static void cursorToItem(int k)
{
    /* D345(b): while the 1:1 pointer owns the crosshair (mouse recently
     * used), it is already under the OS pointer -- snapping would yank it
     * to the top-left item and it would stay there until the next mouse
     * movement. Keyboard/D-pad users (stale mouse) keep the snap. */
    if (inputMenuPointerLive()) return;
    cursorToItemRaw(k);
}

/* ------------------------------------------------------------------------ */
/* MENU_PC_OPTIONS: init / update / interface / constructor (front.c cases) */

void frontOptionsMenuInit(void)
{
    /* As init_menu15_cheat: reset the tab state and make sure the dossier
     * model is loaded (the background draws walletinst[0]). */
    tab_start_selected = FALSE;
    tab_next_selected = FALSE;
    tab_prev_selected = FALSE;
    tab_prev_highlight = FALSE;
    tab_next_highlight = FALSE;
    tab_start_highlight = FALSE;
    load_walletbond();

    /* frontSetupMenuBackground frames the folder at folderpositions[
     * selected_folder_num]; file select leaves it at -1. Use folder 1, as a
     * return from mode select would (file select's init does the same). */
    if (selected_folder_num < FOLDER1 || selected_folder_num >= MAX_FOLDER_COUNT) {
        selected_folder_num = FOLDER1;
    }
    folder_selection_screen_option_icon = 0;   /* crosshair, not Copy/Erase */

    s_level = 0;
    s_page = 0;
    s_pageno = 0;   /* D406 */
    s_subHeader = -1;
    s_hl = -1;
    s_dragRow = -1;
    s_repeatDir = 0;
    s_vrepeatDir = 0;
    buildPages();
    /* Isolated headless screenshot of a nested page. This diagnostic only
     * changes menu navigation; it never edits profile or config values. */
    const char *testPage = getenv("GE_FRONTOPTIONS_SECTION");
    if (testPage) {
        for (int i = 0; i < optionsRowCount(); i++) {
            if (!optionsRowIsHeader(i) || strcmp(optionsRowLabel(i), testPage)) continue;
            int root = i, depth = 1;
            while (optionsRowHeaderParent(root) >= 0) {
                root = optionsRowHeaderParent(root);
                depth++;
            }
            for (int p = 0; p < s_pageN; p++) {
                if (s_pageHdr[p] == root) {
                    s_page = p;
                    s_pageno = 0;
                    s_level = depth;
                    s_subHeader = depth > 1 ? i : -1;
                    buildPages();
                    sysLogPrintf(LOG_INFO, "frontoptions: diagnostic page %s (depth %d, %d rows)",
                                 testPage, depth, s_rowN);
                    break;
                }
            }
            break;
        }
    }
    cursorToItem(0);
    sysLogPrintf(LOG_INFO, "frontoptions: opened");
}

void frontOptionsMenuUpdate(void)
{
}

static void goBack(void)
{
    playSfx(DOOR_METAL_CLOSE2_SFX);
    if (s_dragRow >= 0) optionsRowCommit(s_dragRow);
    s_dragRow = -1;
    optionsResetClear();   /* D356: navigating away (or closing) disarms a pending reset */
    if (s_level >= 2) {
        int child = s_subHeader;
        int parent = optionsRowHeaderParent(child);
        s_pageno = 0;   /* D406: every fresh page view starts at page 1 */
        if (parent == s_pageHdr[s_page]) {
            s_level = 1;
            s_subHeader = -1;
        } else {
            s_level--;
            s_subHeader = parent;
        }
        buildPages();
        int selected = 0;
        for (int k = 0; k < s_rowN; k++)
            if (optionsRowChildHeader(s_rowIdx[k]) == child) selected = k + 1;
        cursorToItem(selected);
        s_hl = -1;
        sysLogPrintf(LOG_INFO, "frontoptions: back to %s", optionsRowLabel(activeHeader()));
        return;
    }
    if (s_level == 1) {
        s_level = 0;
        s_pageno = 0;   /* D406 */
        cursorToItem(s_page);
        sysLogPrintf(LOG_INFO, "frontoptions: back to section list");
        return;
    }
    configSave();
    sysLogPrintf(LOG_INFO, "frontoptions: closed");
    frontChangeMenu(MENU_FILE_SELECT, FALSE);
}

void frontOptionsMenuInterface(void)
{
    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(0);

    buildPages();   /* an auto toggle may have hidden or shown % rows */
    /* D383: modal key capture owns events on the host thread. Suppress
     * navigation until its one-shot key has been consumed and released. */
    if (optionsBindingCaptureTick()) return;

    /* D345(f): NO raw stick integration on this screen (the D345(c) call to
     * frontUpdateControlStickPosition() was removed): vertical input is the
     * discrete row stepper below, horizontal is the left/right block -- F10
     * model. Raw drift fought the pointer over cursor_h/v_pos. */

    /* Highlight: as the cheat screen, recomputed while A is not held. */
    if (joyGetButtons(PLAYER_1, A_BUTTON | Z_TRIG) == 0) {
        tab_prev_highlight = FALSE;
        s_hl = -1;
        if (frontCheckCursorOnPreviousTab()) {
            tab_prev_highlight = TRUE;
        } else if (cursor_h_pos >= ROW_HIT_X0 && cursor_h_pos <= ROW_HIT_X1) {
            for (int k = itemCount() - 1; k >= 0; k--) {
                if (cursor_v_pos >= (f32)(rowY(k) - 3) &&
                    cursor_v_pos <  (f32)(rowY(k) + ROW_DY - 3)) {
                    s_hl = k;
                    break;
                }
            }
            /* D407(b): past the top/bottom edge of the visible rows, clamp
             * the highlight to the edge row instead of dropping it (a
             * mouse-only user keeps a row selected at the page edge; wheel
             * / S then turns the page). */
            if (s_hl < 0 && itemCount() > 0) {
                if (cursor_v_pos < (f32)rowY(0))
                    s_hl = 0;
                else if (cursor_v_pos <= (f32)(rowY(itemCount() - 1) + ROW_DY))
                    s_hl = itemCount() - 1;
            }
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, A_BUTTON | Z_TRIG | START_BUTTON)) {
        if (tab_prev_highlight) {
            goBack();
        } else if (s_hl >= 0 && s_level == 0 && s_hl < s_pageN && optionsRowIsOnlineEntry(s_pageHdr[s_hl])) {
            /* D414: Online Multiplayer is not a settings page. Back to file
             * select (a screen a match can start from) with F9's overlay up. */
            playSfx(DOOR_LOCK_SFX);
            configSave();
            sysLogPrintf(LOG_INFO, "frontoptions: online play");
            frontChangeMenu(MENU_FILE_SELECT, FALSE);
            netuiRequestOpen();
        } else if (s_hl >= 0 && s_level == 0) {
            playSfx(DOOR_METAL_CLOSE2_SFX);
            s_page = s_hl;
            s_level = 1;
            s_pageno = 0;   /* D406 */
            sysLogPrintf(LOG_INFO, "frontoptions: section %d", s_page + 1);
            buildPages();
            cursorToItem(0);
            s_hl = -1;
        } else if (s_hl == 0 && s_level >= 1) {
            /* D356: the top save-file row -- A steps to the next file (the
             * D352 chooser; folders only, "none" retired, plan §5.5). */
            playSfx(DOOR_LOCK_SFX);
            watchSettingsChooseFile(1);
        } else if (s_hl >= 1 && s_hl <= s_rowN) {
            /* s_hl is item-numbered (0 = the save-file row); content row k
             * is item k+1, so the row index is s_hl - 1. */
            int i = s_rowIdx[s_hl - 1];
            if (getenv("GE_FRONTNAVLOG"))
                sysLogPrintf(LOG_INFO, "frontnav: select %s/%s (item %d)",
                             optionsRowLabel(activeHeader()), optionsRowLabel(i), s_hl);
            playSfx(DOOR_LOCK_SFX);
            int child = optionsRowChildHeader(i);
            int bx0 = optionsRowIsSlider(i) ? rowBarX0(optionsRowLabel(i)) : BAR_X0;
            if (child >= 0) {
                optionsResetClear();
                s_subHeader = child;
                s_level++;
                s_pageno = 0;   /* D406 */
                buildPages();
                sysLogPrintf(LOG_INFO, "frontoptions: opened %s (depth %d)",
                             optionsRowLabel(child), s_level);
                cursorToItem(0);
                s_hl = -1;
            } else if (optionsRowIsSlider(i) && bx0 <= BAR_X1 - 12 &&
                       cursor_h_pos >= bx0 - 4 && cursor_h_pos <= BAR_X1 + 4) {
                optionsRowSetFraction(i, ((double)cursor_h_pos - bx0) / (BAR_X1 - bx0));
                s_dragRow = i;
            } else if (optionsRowIsBind(i)) {
                /* D395: pad A/X may navigate the keyboard/mouse binding
                 * pages but cannot enter a modal that only a key can finish.
                 * Enter or a mouse click still starts ordinary key capture. */
                const Uint8 *ks = SDL_GetKeyboardState(NULL);
                if (ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER] ||
                    (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT)))
                    optionsRowBeginBind(i);
            } else if (optionsRowIsReset(i)) {
                /* D356: reset rows are two-step arm -> confirm (edge
                 * activation: joyGetButtonsPressedThisFrame fires once per
                 * press). The value column shows "Confirm" while armed. */
                optionsRowActivateReset(i);
            } else {
                optionsRowAdjust(i, +1);   /* toggle / cycle / step (wraps) */
            }
        }
    } else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON)) {
        goBack();
    }

    /* Drag a slider while A is held. */
    if (s_dragRow >= 0) {
        if (joyGetButtons(PLAYER_1, A_BUTTON | Z_TRIG)) {
            int bx0 = rowBarX0(optionsRowLabel(s_dragRow));
            optionsRowSetFraction(s_dragRow, ((double)cursor_h_pos - bx0) / (BAR_X1 - bx0));
        } else {
            optionsRowCommit(s_dragRow);
            s_dragRow = -1;
        }
    }

    /* Left/right (D-pad, C buttons; keyboard A/D) adjust the highlighted row,
     * with hold-to-repeat. D356: on the save-file row it cycles the file;
     * on a reset row a fresh press activates (arm/confirm) and the held
     * repeat is suppressed (the activation contract, plan §5.4). */
    if (s_level >= 1 && s_hl >= 0) {
        int dir = 0;
        /* D345(f): stick X joins the D-pad/C-buttons, so Left/Right arrows
         * (and the pad's left stick) adjust values like F10's left/right. */
        s8 vsx = joyGetStickX(PLAYER_1);
        if (joyGetButtons(PLAYER_1, L_JPAD | L_CBUTTONS) || vsx < -5) dir = -1;
        if (joyGetButtons(PLAYER_1, R_JPAD | R_CBUTTONS) || vsx > 5) dir = +1;
        if (dir != 0) {
            int fire = 0;
            if (dir != s_repeatDir) {
                fire = 1;
                s_repeatTimer = 18;
            } else if (--s_repeatTimer <= 0) {
                fire = 1;
                s_repeatTimer = 4;
            }
            if (fire) {
                if (s_hl == 0) {
                    watchSettingsChooseFile(dir);   /* save-file row */
                } else {
                    int i = s_rowIdx[s_hl - 1];
                    if (optionsRowIsReset(i))
                        optionsRowActivateReset(i);
                    else if (optionsRowChildHeader(i) < 0)
                        optionsRowAdjust(i, dir);
                }
            }
        }
        s_repeatDir = dir;
    } else {
        s_repeatDir = 0;
    }

    /* D356: reset arm state -- the screen's selected rows[] index (s_hl 0 is
     * the save row, not a rows[] entry); moving on disarms, goBack clears. */
    optionsResetMaintain(s_level >= 1 && s_hl > 0 ? s_rowIdx[s_hl - 1] : -1);

    /* The dossier: tabs + a blank CLASSIFIED page, as the cheat screen. */
    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_BLANK, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_CLASSIFIED, 1);

    /* D345(e): F10-style discrete vertical navigation. W/S / Up-Down arrows
     * (stick via D345(d)) or the pad's stick/D-pad step the highlight one
     * row at a time -- clamped at the ends, hold-to-repeat with the same
     * 18/4-frame cadence as left/right -- and snap the crosshair onto the
     * new row (ungated: explicit keyboard navigation owns the cursor even
     * right after mouse use). Replaces the raw stick drift of D345(c),
     * which fought the pointer over cursor_v_pos. The mouse is untouched:
     * any movement re-owns the cursor and the highlight hit-test above
     * follows it next frame. Skipped while a slider drag holds A. */
    if (s_dragRow < 0) {
        int vdir = 0;
        s8 vsy = joyGetStickY(PLAYER_1);
        if (joyGetButtons(PLAYER_1, U_JPAD) || vsy > 5)      vdir = -1;  /* up   */
        else if (joyGetButtons(PLAYER_1, D_JPAD) || vsy < -5) vdir = +1;  /* down */
        if (vdir != 0) {
            int fire = 0;
            if (vdir != s_vrepeatDir) {
                fire = 1;
                s_vrepeatTimer = 18;
            } else if (--s_vrepeatTimer <= 0) {
                fire = 1;
                s_vrepeatTimer = 4;
            }
            if (fire) {
                int n = itemCount();
                if (n > 0) {
                    int q = (s_hl < 0 ? 0 : s_hl) + vdir;
                    /* D406: stepping past the end of the visible page turns
                     * the page (cursor lands on the first row of the next
                     * page, or the last row of the previous one); with one
                     * page the ends simply clamp, as before. */
                    if (q >= n && s_pageno * ROWS_PER_PAGE + s_rowN < s_rowTotal) {
                        s_pageno++;
                        buildPages();
                        q = 1;
                    } else if (q < 0 && s_pageno > 0) {
                        s_pageno--;
                        buildPages();
                        q = itemCount() - 1;
                    } else {
                        if (q < 0) q = 0;
                        if (q >= n) q = n - 1;
                    }
                    if (q != s_hl) {
                        if (getenv("GE_FRONTNAVLOG"))
                            sysLogPrintf(LOG_INFO, "frontnav: %s -> item %d%s%s (page %d)",
                                         s_level ? optionsRowLabel(activeHeader()) : "categories",
                                         q, s_level && q > 0 ? " " : "",
                                         s_level && q > 0 ? optionsRowLabel(s_rowIdx[q - 1]) : "",
                                         s_level ? s_pageno + 1 : 0);
                        s_hl = q;
                        cursorToItemRaw(q);
                    }
                }
            }
        }
        s_vrepeatDir = vdir;
    } else {
        s_vrepeatDir = 0;
    }
}

Gfx *frontOptionsMenuDraw(Gfx *DL)
{
    char buf[48];

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
#ifdef VERSION_EU
    DL = viFillScreen(DL);
    DL = viFillScreen(DL);
    DL = viFillScreen(DL);
#endif
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);

    if (s_level == 0) {
        DL = ink(DL, ROW_X, TITLE_Y, "PC Options\n", INK);
        for (int k = 0; k < s_pageN; k++) {
            char name[32], num[8];
            titleCase(optionsRowLabel(s_pageHdr[k]), name, sizeof(name));
            strcat(name, "\n");
            snprintf(num, sizeof(num), "%d.\n", k + 1);
            if (k == s_hl) {
                DL = microcode_constructor_related_to_menus(DL, SEC_X - 2, rowY(k) - 1,
                        SEC_X + measureW(name) + 5, rowY(k) + 0xE, HILITE);
            }
            DL = ink(DL, NUM_X, rowY(k), num, INK);
            DL = ink(DL, SEC_X, rowY(k), name, INK);
        }
    } else {
        char title[32];
        titleCase(optionsRowLabel(activeHeader()), title, sizeof(title));
        int titleW = measureW(title);
        strcat(title, "\n");
        DL = ink(DL, ROW_X, TITLE_Y, title, INK);

        /* D356: scope annotation -- a section that carries per-file rows has
         * its title carry the active file ("GAMEPLAY (File 2)"), PD-style.
         * watchSettingsActiveFolder() is the context-aware accessor (front:
         * the resolved front target; -1 while no file is usable, which the
         * annotation renders as an unavailable state, never a number the
         * write path cannot use). */
        if (optionsRowIsSaveScoped(activeHeader())) {
            int f = watchSettingsActiveFolder();
            char note[16];
            if (f < 0) snprintf(note, sizeof(note), "(none)");
            else       snprintf(note, sizeof(note), "(Profile %d)", f + 1);
            DL = ink(DL, ROW_X + titleW + 8, TITLE_Y, note, INK_DIM);
        }

        /* D406b: page marker for sections that span pages -- "Page p/N"
         * right of the title row, dim, so page 2 is unmistakable. */
        if (s_rowTotal > ROWS_PER_PAGE) {
            int pages = (s_rowTotal + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
            char pg[24];
            snprintf(pg, sizeof(pg), "Page %d/%d", s_pageno + 1, pages);
            DL = inkR(DL, VAL_R, TITLE_Y, pg, INK_DIM);
        }

        /* D356: the top profile row (item 0, not in the row table; the label
         * reads "Profile" -- the end-user term for a per-player game file,
         * see D357 plan section 4): the single file control of the front
         * screen (F10 always targets the active file, so it has no file
         * row). L/R (and A) cycle folders; (none) is the unavailable state
         * until one is created. */
        {
            int y = rowY(0);
            char sv[32];
            if (s_hl == 0) {
                DL = microcode_constructor_related_to_menus(DL, ROW_X - 2, y - 1,
                        ROW_X + 140, y + 0xE, HILITE);
            }
            DL = ink(DL, ROW_X, y, "Profile\n", INK);
            int f = watchSettingsActiveFolder();
            if (f < 0) snprintf(sv, sizeof(sv), "(none)");
            else       snprintf(sv, sizeof(sv), "%d\n", f + 1);
            DL = inkR(DL, VAL_R, y, sv, INK);
        }

        for (int k = 0; k < s_rowN; k++) {
            int i = s_rowIdx[k];
            /* D406b: one-shot label-metrics log (layout diagnostics). */
            static int widthsLogged = 0;
            if (!widthsLogged && getenv("GE_FRONTOPTS_WIDTHS")) {
                widthsLogged = 1;
                for (int w2 = 0; w2 < s_rowN; w2++) {
                    int j = s_rowIdx[w2];
                    sysLogPrintf(LOG_INFO, "frontoptions: '%s' w=%d barx0=%d",
                                 optionsRowLabel(j), measureW(optionsRowLabel(j)),
                                 rowBarX0(optionsRowLabel(j)));
                }
            }
            /* Content row k is item k+1 (item 0 is the save-file row), so
             * it sits one line below the save row -- rowY() already shifts
             * level-1 items down by one. */
            int y = rowY(k + 1);
            char label[48];
            snprintf(label, sizeof(label), "%s\n", optionsRowLabel(i));
            if (k + 1 == s_hl) {
                DL = microcode_constructor_related_to_menus(DL, ROW_X - 2, y - 1,
                        ROW_X + measureW(label) + 5, y + 0xE, HILITE);
            }
            DL = ink(DL, ROW_X, y, label, INK);

            /* GAMEPLAY/HUD mix ini and profile options. AUDIO is wholly
             * profile-scoped and already has (Profile N) in its title. */
            if (optionsRowIsSaveScoped(i) &&
                (strcmp(optionsRowLabel(activeHeader()), "GAMEPLAY") == 0 ||
                 strcmp(optionsRowLabel(activeHeader()), "HUD") == 0)) {
                DL = ink(DL, ROW_X + measureW(label) + 4, y, "(per profile)\n", INK_DIM);
            }

            if (optionsRowIsSlider(i)) {
                int x0 = rowBarX0(optionsRowLabel(i));
                if (BAR_X1 - x0 >= 12) {
                    s32 fx = x0 + (s32)((BAR_X1 - x0) * optionsRowFraction(i) + 0.5);
                    DL = microcode_constructor_related_to_menus(DL, x0, y + 5, BAR_X1, y + 8, 0x00000040);
                    DL = microcode_constructor_related_to_menus(DL, x0, y + 5, fx, y + 8, 0xA00000C0);
                }
            } else if (optionsRowNeedsRestart(i)) {
                DL = ink(DL, BAR_X0, y, "(restart)\n", INK_DIM);
            }

            optionsRowValueText(i, buf, sizeof(buf) - 1);
            if (strcmp(optionsRowLabel(i), "Crosshair colour") == 0 && buf[0]) {
                extern void portCrosshairPreview(s32 *, s32 *, s32 *);
                s32 red, green, blue;
                portCrosshairPreview(&red, &green, &blue);
                s32 x = VAL_R - measureW(buf) - 17;
                DL = microcode_constructor_related_to_menus(DL, x, y + 2,
                         x + 10, y + 12,
                         ((u32)red << 24) | ((u32)green << 16) |
                         ((u32)blue << 8) | 0xff);
            }
            if (buf[0]) {
                u32 col = (buf[0] == 'O' && buf[1] == 'N') ? INK_ON : INK;
                /* D346: kOnOff is title-case; M7 (D387) tags the
                 * experimental AllUnlocked ON value "ON - UNSAFE". */
                strcat(buf, "\n");
                DL = inkR(DL, VAL_R, y, buf, col);
            }
        }
        if (strcmp(optionsRowLabel(activeHeader()), "MOVEMENT") == 0 ||
            strcmp(optionsRowLabel(activeHeader()), "ACTIONS") == 0) {
            DL = ink(DL, ROW_X, rowY(s_rowN + 2),
                     optionsBindingCaptureActive()
                         ? "Press key/mouse  B/ESC cancel  DEL clear\n"
                         : "Keys/mouse only  Enter: bind  B: back\n", INK_DIM);
        } else if (s_rowTotal > ROWS_PER_PAGE) {
            /* D406b: bottom hint on every page of a multi-page section (11
             * rows per page leaves the last line of the paper for it). */
            int last = s_pageno * ROWS_PER_PAGE + s_rowN >= s_rowTotal;
            int first = s_pageno == 0;
            /* D406d: one row below the last content row (rowY(s_rowN + 1))
             * -- pitch-aligned with the list; +2 sat ~26px clear of it and
             * looked orphaned at the paper's bottom edge. */
            DL = ink(DL, ROW_X, rowY(s_rowN + 1),
                     first ? "Down: next page\n"
                     : last ? "Up: previous page\n"
                            : "Up: previous page   Down: next page\n",
                     INK_DIM);
        }
    }

    DL = frontAddPreviousTabText(DL);
    DL = frontDrawCursor(DL);
    return DL;
}

/* F10 / Select must not stack the overlay on this screen. */
int frontOptionsBlocksOverlay(void)
{
    return current_menu == MENU_PC_OPTIONS;
}

/* ------------------------------------------------------------------------ */
/* File select: the "Options" entry, drawn from the D343 hook in           */
/* constructor_menu05_fileselect (game thread, after the interface tick).  */
/* ------------------------------------------------------------------------ */

Gfx *optionsFileSelectLabel(Gfx *gdl)
{
    s32 h = 0, w = 0;
    s32 x, y;
    int hot;

    /* The F10 overlay pads the controller away from front.c, so file select's
     * idle timer never resets and it dropped to the legal screen after 30 s.
     * This hook only runs on MENU_FILE_SELECT, the one screen where
     * g_MenuTimer is an idle timer (elsewhere it is the intro / cast-roll
     * clock). Same write front.c makes when a button is pressed. */
    /* D414: likewise while the F9 online overlay is up, or a lobby / online
     * match search is running with it closed -- a start must find this
     * screen, not the attract loop. */
    if (optionsOverlayIsOpen() || netuiIsOpen() || netgameOnlineActive()) {
        g_MenuTimer = 0;
    }

    /* D400 (user sign-off, "most straightforward design"): text-only label,
     * the D398 dot icon is gone. Exact Copy/Erase pattern from front.c:
     * measure the single-line label (no trailing newline -- the kLabelNL
     * trick measured a taller box and sat the text ~7px higher than the
     * bar's other words, the "different size/look" complaint), centre it
     * on the bar line, draw with the same font. Hot colour is the game's
     * own packed gold 0xEBD879FF, the very constant front.c passes to
     * textRender for its gold folder text -- no per-channel unpacking, so
     * no purple (the D398 dot bug came from unpacking that word for
     * gDPSetEnvColor in the wrong byte order). */
    /* NB: textMeasure's signature is (textheight, textwidth) -- height FIRST
     * (an earlier cut passed these swapped and drew the label 35px above the
     * bar, hit band transposed). Height is measured WITH a trailing newline:
     * Copy/Erase's localised strings end in a newline, so textMeasure gives
     * them a full 14px line height and y = 285 - 7 = 278 (PCDUMP tops
     * 279); measuring the bare label gives h=0 -> y=285, sitting 7px LOWER
     * than the bar's other words. Measuring kLabelNL matches their tops
     * exactly. (This was the D398 "kLabelNL trick"; the D400 rewrite
     * dropped it and that is what broke the vertical alignment.) */
    textMeasure(&h, &w, (char *)kLabelNL, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    /* Follow the Erase label's measured right edge (set by the constructor
     * just before this hook), so a wider localised "Erase" (JP glyphs)
     * pushes the label right instead of overlapping it. */
    x = (s32)folder_option_ERASE_bound.right + LABEL_GAP;
    if (x < LABEL_X) {
        x = LABEL_X;
    }
    /* Same centring front.c uses for Copy/Erase (front.c:2779/2791). */
    y = LABEL_CY - (h / 2);

    /* D406: test hook -- enter the screen without a mouse click so headless
     * runs (GE_INPUTSCRIPT D-pad stepping + GE_FRONTNAVLOG / diagnostics)
     * can exercise it. Diagnostic-only: never active without the env. */
    if (getenv("GE_FRONTOPTIONS_AUTO") && menu_update == MENU_INVALID &&
        folder_selected_for_deletion < 0) {
        static int autoEntered = 0;
        if (!autoEntered) {
            autoEntered = 1;
            playSfx(DOOR_LOCK_SFX);
            frontChangeMenu(MENU_PC_OPTIONS, FALSE);
        }
    }

    hot = !optionsOverlayIsOpen()
       && menu_update == MENU_INVALID
       && folder_selected_for_deletion < 0
       && cursor_h_pos >= (f32)(x - HIT_PAD) && cursor_h_pos <= (f32)(x + w + HIT_PAD)
       && cursor_v_pos >= (f32)(y - HIT_PAD) && cursor_v_pos <= (f32)(y + h + HIT_PAD);

    if (hot && joyGetButtonsPressedThisFrame(PLAYER_1, A_BUTTON | Z_TRIG | START_BUTTON)) {
        playSfx(DOOR_LOCK_SFX);   /* Copy/Erase's click */
        frontChangeMenu(MENU_PC_OPTIONS, FALSE);
    }

    /* D414: "Online" under it -- the same label, opening the F9 online-play
     * overlay. (With the overlay up, controller 0 belongs to it, so neither
     * label can be clicked through it.) */
    {
        s32 oh = 0, ow = 0, ox = x, oy;
        int ohot;
        textMeasure(&oh, &ow, (char *)kOnlineNL, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
        oy = y + ONLINE_DY;
        ohot = !optionsOverlayIsOpen() && !netuiIsOpen()
            && menu_update == MENU_INVALID
            && folder_selected_for_deletion < 0
            && cursor_h_pos >= (f32)(ox - HIT_PAD) && cursor_h_pos <= (f32)(ox + ow + HIT_PAD)
            && cursor_v_pos >= (f32)(oy - HIT_PAD) && cursor_v_pos <= (f32)(oy + oh + HIT_PAD);
        if (ohot && joyGetButtonsPressedThisFrame(PLAYER_1, A_BUTTON | Z_TRIG | START_BUTTON)) {
            playSfx(DOOR_LOCK_SFX);
            netuiRequestOpen();
        }
        /* Same font, size and centre line as Copy/Erase; white idle, the
         * game's gold while the cursor is over it. */
        gdl = textRender(gdl, &x, &y, (char *)kLabel, ptrFontZurichBoldChars,
                         ptrFontZurichBold, hot ? 0xEBD879FF : 0xFFFFFFFF,
                         viGetX(), viGetY(), 0, 0);
        return textRender(gdl, &ox, &oy, (char *)kOnline, ptrFontZurichBoldChars,
                          ptrFontZurichBold, ohot ? 0xEBD879FF : 0xFFFFFFFF,
                          viGetX(), viGetY(), 0, 0);
    }
}
