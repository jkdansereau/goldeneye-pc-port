#ifndef PORT_OPTIONSOVERLAY_H
#define PORT_OPTIONSOVERLAY_H

/*
 * F10 in-game options overlay (approach C from docs/dev/OPTIONS-MENU-PLAN.md).
 *
 * A self-contained port-layer immediate-mode overlay: it draws its own 2D
 * display list on top of the game's frame (appended in fast3d's gfx_run,
 * after the game DL) and handles its own keyboard/mouse nav. It edits the
 * port-owned config.c options directly, so changes apply live for the live
 * ones. When closed it renders NOTHING (zero bytes appended) -- golden dumps
 * stay byte-identical.
 *
 * Hooks:
 *   video.c   videoPumpEvents  : F10 -> optionsOverlayToggle(); ESC closes;
 *                                wheel -> optionsOverlayScroll()
 *   input.c   inputComputePad  : controller 0 swallowed while open;
 *                                nav routed to optionsOverlayHandleInput()
 *   gfx_pc.cpp gfx_run          : optionsOverlayEmit() appended after the game DL
 *
 * D346 (M2): the row API below is the shared row table the front MENU_PC_
 * OPTIONS screen (frontoptions.c) renders from, so one option set in one
 * place.
 */

#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include <config.h>
#include <video.h>
#include <watchsettings.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Toggle open/closed. On the closing edge the config is saved. */
void optionsOverlayToggle(void);
void optionsOverlayTestStep(char c);   /* GE_OVNAV harness (frontoptions.c) */
/* ESC: back to categories, or close from the category list (host-thread request). */
void optionsOverlayBack(void);

/* 1 while the overlay is on screen. */
int optionsOverlayIsOpen(void);

/* D567: 1 once the mouse moved or clicked since the overlay opened or the
 * last pad/keyboard navigation (the pointer is then the active input). */
int optionsOverlayMouseActive(void);

/* Called from inputComputePad(0) while open: reads SDL keyboard edges and
 * drives the cursor / value adjustments. */
void optionsOverlayHandleInput(void);

/* Mouse-wheel notch -> move the selection (host event pump). */
void optionsOverlayScroll(int dir);

/* D383/D385 keyboard + mouse rebinding modal, shared by both options UIs.
 * Host event pump hands off one key/button; menu thread commits it. */
int optionsBindingCaptureActive(void);
int optionsBindingInputBlocked(void); /* active or captured key still held */
int optionsBindingKeyDown(const SDL_KeyboardEvent *ev); /* 1 = consumed */
int optionsBindingMouseDown(const SDL_MouseButtonEvent *ev); /* 1 = consumed */
int optionsBindingCaptureTick(void); /* 1 = swallow menu nav this tick */

/* Build the overlay's 2D display list for this frame, or return NULL when the
 * overlay is closed. Called by fast3d after running the game DL. */
Gfx *optionsOverlayEmit(void);

/* D343 (M2): row API for the file-select options screen (frontoptions.c).
 * Index i runs over the same row table as the F10 overlay; headers split it
 * into the screen's sections. */
int         optionsRowCount(void);
int         optionsRowIsHeader(int i);
int         optionsRowHeaderParent(int i); /* -1 for root sections */
int         optionsRowChildHeader(int i);  /* -1 unless this row opens a nested section */
int         optionsRootHeader(int k);      /* k-th top-level page in PD hub / tab order, rows[] header index; -1 past the end */
int         optionsRowIsBindingsPage(int hdr);   /* Key Bindings page (front help line) */
int         optionsRowIsPadPage(int hdr);        /* a seat's Controller page (front help line) */
int         optionsRowSectionMixedScope(int hdr); /* page mixes per-file and global rows ("(per profile)" tags) */
void        optionsRowTitle(int i, char *out, int n); /* header title, seat-aware ("PLAYER 2 CONTROLLER") */
void        optionsRowLinkOpened(int i);   /* call before opening link row i's child: a Select-player row sets the seat */
int         optionsRowSeat(int i);         /* 0-3 for a "Player N" row, else -1 */
int         optionsPadSeat(void);          /* seat the Controller / Key bindings pages edit (0-based) */
int         optionsRowIsShown(int i);
const char *optionsRowLabel(int i);
/* D507: one-line plain-language description of row i, or NULL. Shown under the
 * F10 box and on the front-end options page for the focused row. */
const char *optionsRowHelp(int i);
int         optionsRowIsSlider(int i);
int         optionsRowIsBind(int i);
void        optionsRowBeginBind(int i);
int         optionsRowIsPadBind(int i);   /* D469: gamepad-capture row (pad A may start it) */
int         optionsRowIsBondChooser(int i); /* D353: the Bond-file chooser row
                                              (front options screen only) */
/* D356: 1 when this row's value lives in the selected save file (content
 * rows); on a header, when its section contains such rows -- the front page
 * uses the header form for its "(File N)" title annotation. */
int         optionsRowIsSaveScoped(int i);
int         optionsRowNeedsRestart(int i);
double      optionsRowFraction(int i);
double      optionsRowGetValue(int i); /* D356: raw value (fraction is bar geometry) */
void        optionsRowSetFraction(int i, double f);
void        optionsRowCommit(int i); /* slider release; watch rows persist once */
void        optionsRowValueText(int i, char *out, int n);
void        optionsRowAdjust(int i, int dir);
void        optionsAdjustCommitPending(void); /* D489: save a stepped watch slider on release */

/* D356: per-section "Reset to defaults" rows (ROW_ACTION, keys
 * "__Reset*"). Activation is edge-triggered and two-step (arm -> confirm
 * within 3 s, one commit per confirmed activation; timeout or navigating
 * away disarms) -- optionsRowIsReset lets the front screen suppress its
 * held-key repeat for them. optionsResetMaintain runs each frame from the
 * open UI (selRow = its selected rows[] index, -1 = none); optionsResetClear
 * when a UI closes. Identical contract in F10 and on the front options
 * screen (they are never open at the same time). Front: the confirmed reset
 * writes the section's file rows through watchSettingsSet (direct commit)
 * and the ini rows live (persisted on screen close, like any ini edit).
 * In stage: the file rows enter the D352 queue (the game thread applies +
 * persists them) and the ini rows are applied live immediately. */
int  optionsRowIsReset(int i);
void optionsRowActivateReset(int i);
void optionsResetMaintain(int selRow);
void optionsResetClear(void);

/* D356 GE_WSPROBE_RESET dev hook (env-gated, driven from
 * watchSettingsGameTick): runs the real arm -> confirm -> dispatch for
 * EVERY section's reset row and verifies each section's values after its
 * own dispatch (plan §5.8). Front is one-shot; the stage pair runs on the
 * tick after the stage drain. */
void optionsResetProbePrepare(void);
void optionsResetProbeDispatchStage(void);
void optionsResetProbeVerifyStage(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_OPTIONSOVERLAY_H */
