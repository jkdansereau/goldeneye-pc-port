/*
 * frontoptions.c -- the file-select "PC Options" entry.
 *
 * One PC options UI: the label at the right end of file select's
 * Select / Copy / Erase bar opens the F10 overlay (optionsoverlay.c) over
 * file select. The former MENU_PC_OPTIONS screen (D343) is gone; file select
 * stays underneath and is held off its idle timeout while the overlay is open.
 *
 * Game-code surface: the one existing #ifdef PORT call in
 * constructor_menu05_fileselect (front.c). Runs on the game thread.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <ultra64.h>
#include <bondgame.h>
#include <bondconstants.h>
#include <joy.h>
#include <snd.h>
#include <music.h>
#include "front.h"
#include "textrelated.h"
#include "language.h"
#include "platform.h"
#include "system.h"
#include "optionsoverlay.h"
#include "frontoptions.h"

extern struct rectbbox folder_option_ERASE_bound;   /* front.c */

/* ---- file-select label: right end of the Select / Copy / Erase bar ---- */
#define LABEL_X     351   /* D400: floor = NTSC Erase right (~323) + LABEL_GAP; JP's wider Erase pushes past it */
#define LABEL_CY    285   /* the bar's centre line, as Copy/Erase */
#define LABEL_GAP   28    /* D400: text-only, a plain word gap after Erase */
#define HIT_PAD     4

static const char kLabel[]   = "PC Options";  /* ASCII only: issue #87 / D295 */
static const char kLabelNL[] = "PC Options\n"; /* height measure only, D400 */

static void playSfx(s16 id)
{
    sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, id, NULL);
}

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
    if (optionsOverlayIsOpen()) {
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

    /* One-PC-options-UI: test hook -- open the F10 overlay over file select
     * without a click so headless runs can capture it. Diagnostic-only: never active without the env. */
    {   /* GE_FILESELECT_OVERLAY=<n>: open once; n > 1 also closes it n label-frames later */
        static int envN = -1, openedAt = -1, frames = 0;
        if (envN < 0) { const char *e = getenv("GE_FILESELECT_OVERLAY"); envN = e ? (atoi(e) > 0 ? atoi(e) : 1) : 0; }
        if (envN && menu_update == MENU_INVALID && folder_selected_for_deletion < 0) {
            frames++;
            if (openedAt < 0 && !optionsOverlayIsOpen()) { openedAt = frames; optionsOverlayToggle(); }
            else if (openedAt >= 0 && envN > 1 && frames == openedAt + envN && optionsOverlayIsOpen()) optionsOverlayToggle();
        }
    }

    {   /* GE_OVNAV="O,D,D,A,P,...": one token per 20 label-frames (harness: scripted overlay navigation) */
        static const char *nav = (const char *)-1; static int navI = 0, navF = 0;
        if (nav == (const char *)-1) nav = getenv("GE_OVNAV");
        if (nav && menu_update == MENU_INVALID && folder_selected_for_deletion < 0 && ++navF % 20 == 0) {
            while (nav[navI] == ',' || nav[navI] == ' ') navI++;
            if (nav[navI]) optionsOverlayTestStep(nav[navI++]);
        }
    }

    hot = !optionsOverlayIsOpen()
       && menu_update == MENU_INVALID
       && folder_selected_for_deletion < 0
       && cursor_h_pos >= (f32)(x - HIT_PAD) && cursor_h_pos <= (f32)(x + w + HIT_PAD)
       && cursor_v_pos >= (f32)(y - HIT_PAD) && cursor_v_pos <= (f32)(y + h + HIT_PAD);

    if (hot && joyGetButtonsPressedThisFrame(PLAYER_1, A_BUTTON | Z_TRIG | START_BUTTON)) {
        playSfx(DOOR_LOCK_SFX);   /* Copy/Erase's click */
        optionsOverlayToggle();
    }

    /* Same font, size and centre line as Copy/Erase; white idle, the
     * game's gold while the cursor is over it. */
    return textRender(gdl, &x, &y, (char *)kLabel, ptrFontZurichBoldChars,
                      ptrFontZurichBold, hot ? 0xEBD879FF : 0xFFFFFFFF,
                      viGetX(), viGetY(), 0, 0);
}
