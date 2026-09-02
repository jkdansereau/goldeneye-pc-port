/*
 * Input: SDL2 keyboard/mouse/gamepad -> N64 controller state (Phase 3, D118).
 *
 * The game reads controllers via osContStartReadData / osContGetReadData
 * (src/joy.c). libultra.c's SI section calls into this module once per
 * controller poll: inputUpdate() refreshes SDL + accumulates the mouse-aim
 * delta, then inputComputePad() produces the 16-bit button mask + stick for
 * one controller. This is the single source of controller state on PC --
 * the old contSnapshotFromKeyboard() now delegates here.
 *
 * Deliberately NOT a full port of pd_port/port/src/input.c (1551 lines): GE's
 * menu/config code never calls that module's VK/bind-string API, so this is a
 * focused implementation matching port/include/input.h plus the two helpers
 * libultra.c needs.
 *
 * ------------------------------------------------------------------------
 * BINDING SCHEME (documented in docs/internals.md sec F "D118")
 *
 * GE's default "1.1" control style: analog stick = move/strafe, the four
 * C-buttons = aim/turn/look (DIGITAL on N64), R = aim mode, Z = fire.
 *
 * Keyboard + mouse (controller 0):
 *   W/S/A/D or arrows .. analog stick  (move / strafe)
 *   mouse motion ....... aim           (mode-aware -- see MOUSE-LOOK below)
 *   left mouse / LCtrl . Z trigger     (fire)
 *   right mouse / LShift R trigger     (aim mode)
 *   Space / Z / E ...... A button      (action / use)
 *   X / R / F ......... B button       (reload / cancel)
 *   Q ................. L trigger
 *   Enter / Tab ....... Start
 *
 * Xbox / SDL_GameController (controller 0 merges pad 0 with kbd/mouse;
 * pads 1-3 -> controllers 1-3):
 *   left stick ........ analog stick   (move / strafe)
 *   right stick ....... C-buttons      (digital, 50% threshold -- aim)
 *   right trigger ..... Z trigger      (fire)
 *   left trigger ...... R trigger      (aim mode)
 *   A / X ............. A button
 *   B / Y ............. B button
 *   LB ............... L trigger
 *   RB ............... B button        (reload)
 *   D-pad ............ N64 D-pad
 *   Start ............ Start
 *
 * MOUSE-LOOK (mode-aware, no src/ changes)
 *   GE's aim model (bondview2.c bondviewProcessInput / MoveData) is
 *   mode-dependent, so the mouse->pad mapping is too:
 *
 *   - Hipfire (!insightaimmode): yaw = analog stick-X ("natural turn");
 *     pitch = DIGITAL C-up/C-down (stick-Y here is move fwd/back and does
 *     not pitch). Mouse Y emits a C-button on polls where it moved.
 *   - Aim mode (R / RMB held): yaw AND pitch are analog -- the stick pushed
 *     past +/-60 gives proportional (stick-60)/10 aim speed. Mouse X/Y push
 *     the stick into the 61..80 band. We emit NO C-buttons in this mode
 *     because there they mean crouch / lean / zoom (this was D118c:
 *     aim + look-down -> crouch).
 *
 *   "aim button held" is read from our own RMB/LShift state -- exact for
 *   the default hold-to-aim scheme; a toggle-aim scheme would need a read
 *   of g_CurrentPlayer->insightaimmode (still no logic change).
 *
 *   GE's native pitch is inverted ("flight" style: C-up -> look down). We
 *   hide that: mouse-down looks down by default, MouseInvertY flips it.
 *
 *   Tunable via ge007.ini [Input]: MouseAimSpeed (aim mode), MouseTurnSpeed
 *   (hipfire yaw), MouseInvertY, MouseEnabled. Residual: in hipfire, yaw
 *   (analog) and pitch (digital) still feel different (D118a) -- a fully
 *   analog hipfire pitch needs an #ifdef PORT hook in bondview.c (TODO).
 * ------------------------------------------------------------------------
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "input.h"

/* N64 button bits (from PR/os.h -- duplicated here to avoid pulling os.h,
 * whose `u8 errno;` field collides with <errno.h>'s macro). */
#define GE_CONT_A      0x8000
#define GE_CONT_B      0x4000
#define GE_CONT_G      0x2000  /* Z trigger */
#define GE_CONT_START  0x1000
#define GE_CONT_UP     0x0800
#define GE_CONT_DOWN   0x0400
#define GE_CONT_LEFT   0x0200
#define GE_CONT_RIGHT  0x0100
#define GE_CONT_L      0x0020
#define GE_CONT_R      0x0010
#define GE_CONT_E      0x0008  /* C-up    */
#define GE_CONT_D      0x0004  /* C-down  */
#define GE_CONT_C      0x0002  /* C-left  */
#define GE_CONT_F      0x0001  /* C-right */

#define MAX_PADS            4
#define STICK_DEADZONE      7000
#define STICK_MAX          80

/* Front-end pointer mode: GE's menu cursor ("crosshair") is stick-driven
 * (front.c frontUpdateControlStickPosition reads joyGetStickX/Y and
 * integrates a screen position). During a running stage current_menu ==
 * MENU_RUN_STAGE (11, src/bondconstants.h enum MENU); every other value is
 * a front-end / briefing / debrief / cheat screen where the mouse should
 * feel like a pointer, not a look-axis. We read the game global directly
 * (enum MENU is ABI int) -- UI-context only, no logic change.
 * MENU_INVALID (-1) means the front end never ran (bare -level_XX boot):
 * treat that as in-game so direct-launch mouse-look is unaffected. */
extern int current_menu;
#define GE_MENU_RUN_STAGE  11
#define GE_MENU_INVALID    (-1)
/* D169: the front-end cursor lives in the game's virtual-screen field, which
 * is 440x330 in the front end -- not the 320x240 the pointer P-controller
 * assumed. Read the live field so the mouse pointer can reach the whole
 * mission-select grid. These are plain non-static engine accessors
 * (src/game/bondview.c) and globals (src/game/front.c); UI geometry only, no
 * logic change. */
extern float getPlayer_c_screenwidth(void);
extern float getPlayer_c_screenheight(void);
extern float getPlayer_c_screenleft(void);
extern float getPlayer_c_screentop(void);
extern float cursor_h_pos, cursor_v_pos;
#define MENU_POINTER_GAIN  1.5
#define TRIG_THRESHOLD     (30 * 256)
#define RSTICK_THRESHOLD   0x4000
/* Mouse-look tuning. GE's aim model is mode-dependent (bondview2.c
 * bondviewProcessInput):
 *   - Hipfire (!insightaimmode): yaw = analog stick-X ("natural turn"),
 *     pitch = DIGITAL C-up/C-down only (stick-Y is move fwd/back here).
 *   - Aim mode (R held):          yaw AND pitch = analog stick pushed past
 *     +/-60 -> proportional (stick-60)/10. C-up/C-down mean crouch/lean/
 *     zoom in this mode, NOT aim -- so we must NOT emit them while aiming
 *     (that was D118c: aim + mouse-down -> crouch).
 * GE's native pitch is inverted ("flight" style): C-up -> look down. We
 * hide that so mouse-down looks down by default; MouseInvertY flips it. */
#define MOUSE_TURN_GAIN     6.0   /* hipfire: raw px this poll -> stick-X counts */
#define MOUSE_PITCH_THRESH  1.5   /* hipfire: px/poll before a C-button fires  */
#define AIM_GAIN            4.0   /* aim mode: px/poll -> counts into the band  */
#define AIM_MOVE_THRESH     0.5   /* aim mode: px/poll before the stick moves   */
#define HIP_PITCH_FULL      6.0   /* hipfire pitch: |px/poll| for a solid C hold (D166) */

/* Item 1 (D165) — front-end 1:1 pointer. front.c frontUpdateControlStickPosition
 * INTEGRATES the stick as a velocity into a screen-pixel cursor position
 * (cursor_h_pos += (stickx*0.075 +/- 0.5) * delta, deadzone +/-5, clamp +/-70,
 * cursor clamped into the ~320x240 virtual screen rect minus a 20px margin).
 * Feeding it mouse *velocity* therefore gives velocity^2 feel. Instead we run a
 * P-controller: keep our own estimate of where the game cursor is (menuEst*,
 * integrated with the SAME recurrence as front.c), accumulate a target from
 * mouse motion, and emit stick = clamp(GAIN*(target-est)). The estimate re-syncs
 * to the real cursor whenever the target is held at a screen edge. */
#define MENU_CURSOR_LO      20.0
#define MENU_CURSOR_HI_H    300.0
#define MENU_CURSOR_HI_V    220.0
#define MENU_CURSOR_MID_H   160.0
#define MENU_CURSOR_MID_V   120.0
#define MENU_P_GAIN         6.0    /* stick = GAIN*(target-est); 0.075*GAIN<1 => no overshoot */
#define MENU_EST_DELTA      1.0    /* deltaEst per poll (front.c integrates ~1 tick/frame) */
#define MENU_MOUSE_TO_PX    1.0    /* device px -> virtual cursor px (x MenuPointerSpeed/100) */

static void applyGrab(int want);
static void reconcileGrab(int menuMode);
static void applyCursorVisibility(void);

static int numControllers = 1;
static int connectedMask   = 0x1;   /* controller 0 always present */

static SDL_GameController *pads[MAX_PADS];

static int mouseEnabled   = 1;
static int mouseGrabbed    = 1;     /* released while the window is unfocused */

/* WI-1: Quake-style click-to-lock cursor capture.
 *   MouseCaptureMode 0 (default) = legacy: cursor is grabbed whenever the
 *     window has focus and MouseEnabled is set (prior behaviour, byte-exact).
 *   MouseCaptureMode 1 = native-PC: the cursor is free until you click in the
 *     game window; ESC (or focus loss, or opening a menu) frees it again.
 *     Re-entering a stage while still "armed" re-locks automatically so
 *     unpausing / starting a level does not need a click.
 * Controller input is entirely independent of all of this. */
static int mouseCaptureMode = 1;   /* WI-1 default: Quake-style click-to-lock + 1:1 menu pointer. 0 = legacy always-grab. */
static int captureArmed     = 0;   /* user has clicked to lock (capture mode) */
static int windowFocused    = 1;
static int mouseAimSpeed  = 16;     /* aim-mode sensitivity, percent (B3: 50 -> 25 M-29 -> 16; still overshot at 25) */
static int aimBand        = 20;     /* aim mode: usable stick range above the 60 gate */
static int mouseTurnSpeed = 100;    /* hipfire yaw sensitivity, percent */
static int menuPointerSpeed = 100;  /* front-end cursor speed, percent */
static int mouseInvertY   = 0;      /* 1 = mouse-down looks up */
static int mouseYScale    = 100;    /* extra vertical (pitch) sensitivity, % */
static int mouseSmoothing = 0;      /* 0 = raw; 1..90 = low-pass strength (%) */
static int mouseRawInput  = 0;      /* 1 = bypass OS pointer accel for aim    */

/* Gamepad tuning -- defaults reproduce the old hardcoded constants exactly. */
static int padDeadzone    = STICK_DEADZONE;   /* left-stick deadzone, raw 0..32767 */
static int padTriggerPct  = 23;               /* trigger press point, % of travel (~30*256) */
static int padLookInvertY = 0;                /* 1 = invert right-stick (look) Y */

/* Smoothed mouse delta carried between polls when mouseSmoothing > 0. */
static double mouseSmDX = 0.0, mouseSmDY = 0.0;

/* Raw relative-mouse delta accumulated since the last inputComputePad(0).
 * NOT a persistent aim accumulator: mouse-look is a displacement device and
 * GE's aim is a rate device, so we consume the whole delta each poll and
 * reset -- stop moving and the stick/ C-button releases immediately. */
static double mouseDX = 0.0;
static double mouseDY = 0.0;

/* Mouse-wheel -> weapon cycle: a wheel notch queues a short A-button press
 * (GE's default scheme cycles the weapon forward on a fresh A edge, invButtons
 * = A_BUTTON, bondview2.c:5162/5326). Held for a couple of polls so the game
 * sees a clean press+release edge. */
#define WHEEL_PULSE_POLLS 2
static int wheelPulse = 0;

/* Item 1 (D165) — front-end pointer P-controller state. */
static int    menuPointerMode  = 1;    /* 0 = legacy velocity, 1 = 1:1 pointer */
static int    hipfirePitchSpeed = 100; /* D166: hipfire pitch pulse rate, percent */
static double menuEstH = 0.0, menuEstV = 0.0;   /* estimate of the game cursor (virtual px) */
static double menuTgtH = 0.0, menuTgtV = 0.0;   /* mouse-driven target (virtual px)         */
static int    menuPrevActive = 0;
static double hipPitchPhase = 0.0;              /* D166: hipfire pitch pulse phase 0..1     */
static int    lastMenuMouseX = -1, lastMenuMouseY = -1;  /* WI-2: last abs cursor seen in a menu */

/* Integrate one poll of our cursor estimate with front.c's exact recurrence
 * (frontUpdateControlStickPosition): the game receives `stick` as an s8, applies
 * a +/-5 deadzone with a -5 offset, clamps to +/-70, then
 * cursor += (stick*0.075 +/- 0.5) * delta, and clamps the cursor to [lo, hi]. */
static double menuCursorStep(double est, double stick, double lo, double hi)
{
    if (stick > 127.0)  stick = 127.0;
    if (stick < -128.0) stick = -128.0;
    double x = (double)(int)stick;
    if (x < -5.0)      x += 5.0;
    else if (x >= 6.0) x -= 5.0;
    else               x = 0.0;
    if (x >= 71.0) x = 70.0;
    else if (x < -70.0) x = -70.0;
    if (x > 0.0)      est += (x * 0.075 + 0.5) * MENU_EST_DELTA;
    else if (x < 0.0) est += (x * 0.075 - 0.5) * MENU_EST_DELTA;
    if (est > hi) est = hi;
    else if (est < lo) est = lo;
    return est;
}

/* ------------------------------------------------------------------------ */

static void inputOpenPads(void)
{
    connectedMask = 0x1;
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n && i < MAX_PADS; ++i) {
        if (!SDL_IsGameController(i)) {
            continue;
        }
        if (pads[i]) {
            continue;
        }
        pads[i] = SDL_GameControllerOpen(i);
        if (pads[i]) {
            connectedMask |= (1 << i);
            sysLogPrintf(LOG_NOTE, "input: opened gamepad %d '%s' as controller %d",
                         i, SDL_GameControllerName(pads[i]), i);
        }
    }
    for (int i = 0; i < MAX_PADS; ++i) {
        if (pads[i]) {
            connectedMask |= (1 << i);
        }
    }
    numControllers = 1;
    for (int i = 1; i < MAX_PADS; ++i) {
        if (connectedMask & (1 << i)) {
            numControllers = i + 1;
        }
    }
}

/* ------------------------------------------------------------------------
 * Scripted input (test harness, port-only). GE_INPUTSCRIPT lets a headless
 * run walk the front-end / pause menus with no human at the keyboard.
 *
 *   GE_INPUTSCRIPT="120:START;180:A;240:A;600:SDOWN;900:SNONE,A"
 *
 * Each entry is `<frame>:<tok>[,<tok>...]`. Buttons (A B Z START L R UP DOWN
 * LEFT RIGHT CUP CDOWN CLEFT CRIGHT) pulse for INPUTSCRIPT_PULSE controller
 * reads from <frame>. Analog-stick tokens (SUP SDOWN SLEFT SRIGHT) are
 * SUSTAINED: the stick stays deflected until a later entry changes it; SNONE
 * re-centres it. "Frame" = count of controller-0 reads since launch (roughly
 * 2 per rendered frame -- watch GE_INPUTLOG to calibrate). Unset env => no
 * effect; when set it is the ONLY controller-0 input source. */
#define INPUTSCRIPT_MAX     64
#define INPUTSCRIPT_PULSE   6

struct scriptEntry { long frame; unsigned mask; int sx, sy; int hasStick; };
static struct scriptEntry scriptEntries[INPUTSCRIPT_MAX];
static int  scriptCount   = -1;   /* -1 = not parsed yet, 0 = parsed empty */
static long scriptFrame   = 0;
static int  scriptCurSX   = 0;    /* stick set by the last scriptApply() */
static int  scriptCurSY   = 0;

/* Apply one token to `e`. Buttons: A B Z START L R UP DOWN LEFT RIGHT CUP
 * CDOWN CLEFT CRIGHT (D-pad/C-buttons). Analog stick: SUP SDOWN SLEFT SRIGHT
 * (full +/-80 deflection -- moves menu cursors). */
static void scriptApplyToken(struct scriptEntry *e, const char *s, int n)
{
    struct { const char *k; unsigned v; } btn[] = {
        {"A",GE_CONT_A}, {"B",GE_CONT_B}, {"Z",GE_CONT_G}, {"START",GE_CONT_START},
        {"L",GE_CONT_L}, {"R",GE_CONT_R}, {"UP",GE_CONT_UP}, {"DOWN",GE_CONT_DOWN},
        {"LEFT",GE_CONT_LEFT}, {"RIGHT",GE_CONT_RIGHT},
        {"CUP",GE_CONT_E}, {"CDOWN",GE_CONT_D}, {"CLEFT",GE_CONT_C}, {"CRIGHT",GE_CONT_F},
    };
    for (size_t i = 0; i < sizeof(btn)/sizeof(btn[0]); ++i) {
        if ((int)strlen(btn[i].k) == n && SDL_strncasecmp(btn[i].k, s, n) == 0) {
            e->mask |= btn[i].v;
            return;
        }
    }
    e->hasStick = 1;
    if (n == 3 && SDL_strncasecmp("SUP", s, 3) == 0)      { e->sy =  STICK_MAX; return; }
    if (n == 5 && SDL_strncasecmp("SDOWN", s, 5) == 0)    { e->sy = -STICK_MAX; return; }
    if (n == 5 && SDL_strncasecmp("SLEFT", s, 5) == 0)    { e->sx = -STICK_MAX; return; }
    if (n == 6 && SDL_strncasecmp("SRIGHT", s, 6) == 0)   { e->sx =  STICK_MAX; return; }
    if (n == 5 && SDL_strncasecmp("SNONE", s, 5) == 0)    { return; }  /* recentre */
    e->hasStick = 0;
    sysLogPrintf(LOG_WARNING, "GE_INPUTSCRIPT: unknown token '%.*s'", n, s);
}

static void scriptParse(void)
{
    scriptCount = 0;
    const char *env = getenv("GE_INPUTSCRIPT");
    if (!env || !*env) {
        return;
    }
    const char *p = env;
    while (*p && scriptCount < INPUTSCRIPT_MAX) {
        char *end = NULL;
        long fr = strtol(p, &end, 10);
        if (end == p || *end != ':') {
            sysLogPrintf(LOG_WARNING, "GE_INPUTSCRIPT: bad entry near '%s'", p);
            break;
        }
        p = end + 1;
        struct scriptEntry *e = &scriptEntries[scriptCount];
        e->frame = fr;
        e->mask = 0;
        e->sx = e->sy = 0;
        e->hasStick = 0;
        while (*p && *p != ';') {
            const char *tok = p;
            while (*p && *p != ',' && *p != ';') ++p;
            scriptApplyToken(e, tok, (int)(p - tok));
            if (*p == ',') ++p;
        }
        if (*p == ';') ++p;
        scriptCount++;
    }
    sysLogPrintf(LOG_INFO, "GE_INPUTSCRIPT: %d entr%s parsed",
                 scriptCount, scriptCount == 1 ? "y" : "ies");
}

static int scriptIsActive(void)
{
    if (scriptCount < 0) {
        scriptParse();
    }
    return scriptCount > 0;
}

/* When a script is loaded it is the SOLE source of controller-0 input: real
 * keyboard/mouse/pad is ignored so headless menu walks are deterministic
 * (a relative-mouse SDL window with no focus otherwise spews phantom deltas).
 * Returns the scripted button mask for the current frame; advances the frame
 * counter (call exactly once per controller-0 read). */
static unsigned scriptApply(unsigned button)
{
    if (!scriptIsActive()) {
        return button;
    }
    unsigned m = 0;
    long bestStickFrame = -1;
    for (int i = 0; i < scriptCount; ++i) {
        long d = scriptFrame - scriptEntries[i].frame;
        if (d >= 0 && d < INPUTSCRIPT_PULSE) {
            m |= scriptEntries[i].mask;
        }
        /* sustained stick: the latest-starting entry that carried a stick token */
        if (d >= 0 && scriptEntries[i].hasStick && scriptEntries[i].frame > bestStickFrame) {
            bestStickFrame = scriptEntries[i].frame;
            scriptCurSX = scriptEntries[i].sx;
            scriptCurSY = scriptEntries[i].sy;
        }
    }
    scriptFrame++;
    return m;
}

int inputInit(void)
{
    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
            sysLogPrintf(LOG_WARNING, "input: SDL_INIT_GAMECONTROLLER failed: %s",
                         SDL_GetError());
        }
    }

    for (int i = 0; i < MAX_PADS; ++i) {
        pads[i] = NULL;
    }
    inputOpenPads();

    /* Relative mouse mode for mouse-look. In click-to-lock mode we start
     * released and wait for a click in the window (video.c -> inputNotifyClick). */
    mouseGrabbed = mouseEnabled && !mouseCaptureMode;
    if (mouseEnabled) {
        if (mouseRawInput) {
            /* Feed the raw device delta straight through: no OS pointer
             * acceleration, no warp-based emulation. Must be set before
             * relative mode is enabled. */
            SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0");
            SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0");
            sysLogPrintf(LOG_INFO, "input: raw mouse input (no OS pointer accel)");
        }
        SDL_SetRelativeMouseMode(mouseGrabbed ? SDL_TRUE : SDL_FALSE);
        /* Drain the initial jump. */
        SDL_GetRelativeMouseState(NULL, NULL);
    }

    applyCursorVisibility();   /* hide the OS cursor if we start focused */

    sysLogPrintf(LOG_INFO, "input: ready (mask=0x%x, %d controller(s), aimSpeed=%d)",
                 connectedMask, numControllers, mouseAimSpeed);
    return connectedMask;
}

void inputDestroy(void)
{
    for (int i = 0; i < MAX_PADS; ++i) {
        if (pads[i]) {
            SDL_GameControllerClose(pads[i]);
            pads[i] = NULL;
        }
    }
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
}

/* Poll once per controller-read: refresh SDL device state and integrate the
 * mouse-aim delta into the accumulator. */
void inputUpdate(void)
{
    SDL_GameControllerUpdate();

    if (!mouseEnabled || !mouseGrabbed) {
        return;
    }

    int dx = 0, dy = 0;
    SDL_GetRelativeMouseState(&dx, &dy);

    /* Raw px; per-mode sensitivity is applied in inputComputePad(). Accumulate
     * in case inputUpdate() is polled more than once between pad reads. */
    mouseDX += dx;
    mouseDY += dy;
}

static int scaleAxis(int v)
{
    int dz = padDeadzone;
    if (dz < 0) dz = 0;
    if (dz > 30000) dz = 30000;
    if (v > -dz && v < dz) {
        return 0;
    }
    if (v < 0) v += dz; else v -= dz;
    int out = (int)((long)v * STICK_MAX / (32767 - dz));
    if (out >  STICK_MAX) out =  STICK_MAX;
    if (out < -STICK_MAX) out = -STICK_MAX;
    return out;
}

static int keyDown(const Uint8 *ks, SDL_Scancode sc)
{
    return ks && ks[sc];
}

/* Fill button mask + stick for controller idx. Returns the 16-bit mask. */
unsigned inputComputePad(int idx, signed char *stick_x, signed char *stick_y)
{
    unsigned button = 0;
    int sx = 0, sy = 0;

    if (idx < 0 || idx >= MAX_PADS) {
        if (stick_x) *stick_x = 0;
        if (stick_y) *stick_y = 0;
        return 0;
    }

    /* ---- keyboard + mouse: controller 0 only ---- */
    if (idx == 0) {
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        Uint32 mb = mouseEnabled ? SDL_GetMouseState(NULL, NULL) : 0;
        int menuMode = (current_menu != GE_MENU_RUN_STAGE &&
                        current_menu != GE_MENU_INVALID);

        reconcileGrab(menuMode);

        /* Click-to-lock, in a stage, cursor free: the mouse buttons must not
         * reach the game (no phantom fire) -- the first click only re-locks
         * (handled in video.c -> inputNotifyClick). Menus keep their buttons. */
        if (mouseCaptureMode && !mouseGrabbed && !menuMode) {
            mb = 0;
        }

        /* GE default control (1.1): stick Y = move fwd/back, stick X = turn,
         * C-left/right = sidestep, C-up/down = look. FPS layout: W/S move,
         * A/D strafe (C-buttons), mouse X turns (stick X), mouse Y looks. */
        if (keyDown(ks, SDL_SCANCODE_W) || keyDown(ks, SDL_SCANCODE_UP))    sy =  STICK_MAX;
        if (keyDown(ks, SDL_SCANCODE_S) || keyDown(ks, SDL_SCANCODE_DOWN))  sy = -STICK_MAX;
        if (keyDown(ks, SDL_SCANCODE_A))  button |= GE_CONT_C;   /* strafe left  */
        if (keyDown(ks, SDL_SCANCODE_D))  button |= GE_CONT_F;   /* strafe right */
        if (keyDown(ks, SDL_SCANCODE_LEFT))  sx = -STICK_MAX;    /* keyboard turn */
        if (keyDown(ks, SDL_SCANCODE_RIGHT)) sx =  STICK_MAX;

        if ((mb & SDL_BUTTON(SDL_BUTTON_LEFT)) || keyDown(ks, SDL_SCANCODE_LCTRL))
            button |= GE_CONT_G;
        int aimHeld = (mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) ||
                      keyDown(ks, SDL_SCANCODE_LSHIFT);
        if (aimHeld)
            button |= GE_CONT_R;
        if (keyDown(ks, SDL_SCANCODE_SPACE) || keyDown(ks, SDL_SCANCODE_Z) ||
            keyDown(ks, SDL_SCANCODE_E))
            button |= GE_CONT_A;
        if (wheelPulse > 0) {           /* mouse-wheel weapon cycle -> A pulse */
            button |= GE_CONT_A;
            wheelPulse--;
        }
        if (keyDown(ks, SDL_SCANCODE_X) || keyDown(ks, SDL_SCANCODE_R) ||
            keyDown(ks, SDL_SCANCODE_F) || keyDown(ks, SDL_SCANCODE_ESCAPE)) /* D145: ESC = B (back/cancel) */
            button |= GE_CONT_B;
        if (keyDown(ks, SDL_SCANCODE_Q))
            button |= GE_CONT_L;
        if (keyDown(ks, SDL_SCANCODE_RETURN) || keyDown(ks, SDL_SCANCODE_TAB))
            button |= GE_CONT_START;

        /* Mouse-look. Mode-dependent (see the tuning-constants comment):
         *   aim mode  -> push the analog stick past +/-60 for proportional
         *                yaw + pitch; emit NO C-buttons (they mean crouch here).
         *   hipfire   -> yaw on analog stick-X; pitch on digital C-up/C-down.
         * "look down" convention: mouse-down looks down by default; GE's
         * native pitch is inverted so hipfire down = C-up (GE_CONT_E) and
         * aim-mode down = +stick_y. MouseInvertY flips both. */
        if (mouseEnabled) {
            double invert = mouseInvertY ? -1.0 : 1.0;

            /* Optional exponential low-pass (mouseSmoothing = blend % of the
             * previous poll). Off (0) => edx/edy are the raw deltas. */
            double edx = mouseDX, edy = mouseDY;
            if (mouseSmoothing > 0) {
                double a = mouseSmoothing / 100.0;
                if (a > 0.90) a = 0.90;
                mouseSmDX = mouseSmDX * a + edx * (1.0 - a);
                mouseSmDY = mouseSmDY * a + edy * (1.0 - a);
                edx = mouseSmDX;
                edy = mouseSmDY;
            }
            edy *= mouseYScale / 100.0;

            double dyLook = edy * invert;   /* >0 => look down */

            if (menuMode && !menuPointerMode) {
                /* Legacy velocity mode (Input.MenuPointerMode = 0): mouse
                 * velocity -> stick. Kept as a fallback; integrates as
                 * velocity^2 through front.c (D165). */
                double g = MENU_POINTER_GAIN * (menuPointerSpeed / 100.0);
                sx += (int)(edx * g);
                sy -= (int)(dyLook * g);   /* front-end cursor: +sy = up */
            } else if (menuMode) {
                /* Front-end menu pointer.
                 *
                 * front.c frontUpdateControlStickPosition() is the game's cursor
                 * integrator: it reads joyGetStickX/Y, applies a +/-5 deadband,
                 * clamps the stick to +/-70, then does
                 *   cursor_h_pos += (stick*0.075 +/- 0.5) * timerDelta
                 * and clamps cursor_h/v_pos into [screenleft+20 ..
                 * screenleft+screenwidth-20] x [screentop+20 ..
                 * screentop+screenheight-20]. That integrator caps the cursor at
                 * ~5.75 virtual px per poll, so feeding it a stick derived from
                 * an absolute mouse position (D165/D169 P-controller) is
                 * unavoidably laggy/floaty and desyncs from the real cursor.
                 *
                 * With click-to-lock (MouseCaptureMode=1) the front end has a
                 * real free OS cursor, so we know exactly where the pointer is.
                 * Write cursor_h/v_pos DIRECTLY from the absolute mouse position
                 * whenever the mouse moved, and emit a zero stick so the game's
                 * integrator adds nothing (stick 0 -> deadband). When the mouse
                 * is idle we leave the cursor alone and let the keyboard stick
                 * (WASD/arrows, added above) drive it through the game as usual.
                 * cursor_h/v_pos are the plain front.c UI globals every menu
                 * hit-test already reads -- no logic change, just where the
                 * pointer is placed. */
                double loH = MENU_CURSOR_LO, hiH = MENU_CURSOR_HI_H;
                double loV = MENU_CURSOR_LO, hiV = MENU_CURSOR_HI_V;
                {
                    double sw = getPlayer_c_screenwidth(),  sh = getPlayer_c_screenheight();
                    double sl = getPlayer_c_screenleft(),   st = getPlayer_c_screentop();
                    if (sw > 200.0 && sw < 2000.0 && sh > 150.0 && sh < 2000.0) {
                        loH = sl + 20.0;  hiH = sl + sw - 20.0;
                        loV = st + 20.0;  hiV = st + sh - 20.0;
                    }
                }

                int haveAbs = 0;
                if (mouseCaptureMode && !mouseGrabbed) {
                    int mx = 0, my = 0;
                    SDL_GetMouseState(&mx, &my);
                    SDL_Window *w = SDL_GetMouseFocus();
                    int ww = 0, wh = 0;
                    if (w) SDL_GetWindowSize(w, &ww, &wh);
                    if (ww > 0 && wh > 0) {
                        haveAbs = 1;
                        if (!menuPrevActive) {
                            /* just entered a menu: adopt the current pointer as
                             * the baseline, don't yank the cursor this frame */
                            lastMenuMouseX = mx;
                            lastMenuMouseY = my;
                        }
                        if (mx != lastMenuMouseX || my != lastMenuMouseY) {
                            double fx = (double)mx / (double)ww;
                            double fy = (double)my / (double)wh;
                            if (fx < 0.0) fx = 0.0; else if (fx > 1.0) fx = 1.0;
                            if (fy < 0.0) fy = 0.0; else if (fy > 1.0) fy = 1.0;
                            cursor_h_pos = (float)(loH + fx * (hiH - loH));
                            cursor_v_pos = (float)(loV + fy * (hiV - loV));
                            sx = 0;   /* pointer owns the cursor this poll */
                            sy = 0;
                        }
                        lastMenuMouseX = mx;
                        lastMenuMouseY = my;
                    }
                }

                if (!haveAbs) {
                    /* Legacy relative path (MouseCaptureMode=0, cursor grabbed):
                     * P-controller onto a mouse-accumulated target (D165/D169).
                     * D180: clamp the estimate to the same live rect as the
                     * target -- D169 widened the target clamp but left the
                     * estimate pinned to the old 320x240 constants, so the
                     * right/bottom of the menu was unreachable and the
                     * controller wound up into never-settling drift. */
                    if (!menuPrevActive) {
                        menuEstH = menuTgtH = cursor_h_pos;
                        menuEstV = menuTgtV = cursor_v_pos;
                        hipPitchPhase = 0.0;
                    }
                    double s = (menuPointerSpeed / 100.0) * MENU_MOUSE_TO_PX;
                    menuTgtH += edx    * s;
                    menuTgtV += dyLook * s;
                    if (menuTgtH < loH) menuTgtH = loH;
                    if (menuTgtH > hiH) menuTgtH = hiH;
                    if (menuTgtV < loV) menuTgtV = loV;
                    if (menuTgtV > hiV) menuTgtV = hiV;

                    double effH = MENU_P_GAIN * (menuTgtH - menuEstH);
                    double effV = MENU_P_GAIN * (menuTgtV - menuEstV);
                    if (effH >  70.0) effH =  70.0; else if (effH < -70.0) effH = -70.0;
                    if (effV >  70.0) effV =  70.0; else if (effV < -70.0) effV = -70.0;

                    menuEstH = menuCursorStep(menuEstH, effH, loH, hiH);
                    menuEstV = menuCursorStep(menuEstV, effV, loV, hiV);

                    sx += (int)(effH + (effH >= 0.0 ? 0.5 : -0.5));
                    sy -= (int)(effV + (effV >= 0.0 ? 0.5 : -0.5));
                }

                if (configGetInputLog()) {
                    sysLogPrintf(LOG_NOTE,
                        "GE_INPUTLOG menuptr abs=%d cursor=(%.1f,%.1f) stick=(%d,%d)",
                        haveAbs, (double)cursor_h_pos, (double)cursor_v_pos, sx, sy);
                }
            } else if (aimHeld) {
                double gx = fabs(edx)    * (mouseAimSpeed / 100.0) * AIM_GAIN;
                double gy = fabs(dyLook) * (mouseAimSpeed / 100.0) * AIM_GAIN;
                if (fabs(edx) >= AIM_MOVE_THRESH) {
                    int m = 61 + (int)gx; if (m > 60 + aimBand) m = 60 + aimBand;
                    sx += (edx > 0) ? m : -m;
                }
                if (fabs(dyLook) >= AIM_MOVE_THRESH) {
                    int m = 61 + (int)gy; if (m > 60 + aimBand) m = 60 + aimBand;
                    sy += (dyLook > 0) ? m : -m;   /* +stick_y = look down */
                }
            } else {
                sx += (int)(edx * (mouseTurnSpeed / 100.0) * MOUSE_TURN_GAIN);
                /* D166: hipfire pitch as C-button pulses whose frequency scales
                 * with mouse-Y speed -- fast mouse = solid hold, slow = sparse
                 * taps -- so it feels closer to the analog yaw. Aim mode (above)
                 * is untouched. */
                double sp = fabs(dyLook);
                if (sp >= MOUSE_PITCH_THRESH) {
                    double duty = sp * (hipfirePitchSpeed / 100.0) / HIP_PITCH_FULL;
                    if (duty > 1.0) duty = 1.0;
                    hipPitchPhase += duty;
                    if (hipPitchPhase >= 1.0) {
                        hipPitchPhase -= 1.0;
                        button |= (dyLook > 0) ? GE_CONT_E : GE_CONT_D; /* E=C-up=look down */
                    }
                } else {
                    hipPitchPhase = 0.0;
                }
            }
        }

        if (menuMode && mouseEnabled) {
            /* Clicks are select / back in the front end, not fire / aim. */
            button &= ~(GE_CONT_G | GE_CONT_R);
            if (mb & SDL_BUTTON(SDL_BUTTON_LEFT))  button |= GE_CONT_A;
            if (mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) button |= GE_CONT_B;
        }
        menuPrevActive = menuMode;

        mouseDX = 0.0;
        mouseDY = 0.0;
    }

    /* ---- gamepad ---- */
    SDL_GameController *pad = pads[idx];
    if (pad) {
        int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        int ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        int rx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX);
        int ry = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY);

        int px = scaleAxis(lx);
        int py = -scaleAxis(ly);       /* SDL up = negative -> N64 up = positive */
        if (px) sx = px;
        if (py) sy = py;

        if (padLookInvertY) ry = -ry;
        if (rx >  RSTICK_THRESHOLD) button |= GE_CONT_F;
        if (rx < -RSTICK_THRESHOLD) button |= GE_CONT_C;
        if (ry >  RSTICK_THRESHOLD) button |= GE_CONT_D;
        if (ry < -RSTICK_THRESHOLD) button |= GE_CONT_E;

        int trigPt = padTriggerPct * 327;   /* % of the 0..32767 trigger travel */
        if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > trigPt)
            button |= GE_CONT_G;
        if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > trigPt)
            button |= GE_CONT_R;

        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) ||
            SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X))
            button |= GE_CONT_A;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B) ||
            SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y) ||
            SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))
            button |= GE_CONT_B;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))
            button |= GE_CONT_L;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START))
            button |= GE_CONT_START;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP))
            button |= GE_CONT_UP;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
            button |= GE_CONT_DOWN;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
            button |= GE_CONT_LEFT;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
            button |= GE_CONT_RIGHT;
    }

    if (sx > STICK_MAX)  sx = STICK_MAX;
    if (sx < -STICK_MAX) sx = -STICK_MAX;
    if (sy > STICK_MAX)  sy = STICK_MAX;
    if (sy < -STICK_MAX) sy = -STICK_MAX;

    if (stick_x) *stick_x = (signed char)sx;
    if (stick_y) *stick_y = (signed char)sy;

    if (idx == 0 && scriptIsActive()) {
        button = scriptApply(button);
        sx = scriptCurSX;
        sy = scriptCurSY;
    }

    if (configGetInputLog() && (button || sx || sy)) {
        sysLogPrintf(LOG_NOTE, "GE_INPUTLOG cont%d: btn=%04x stick=(%d,%d)",
                     idx, button, sx, sy);
    }

    return button;
}

static void applyGrab(int want)
{
    want = want && mouseEnabled;
    if (want == mouseGrabbed) {
        return;
    }
    mouseGrabbed = want;
    SDL_SetRelativeMouseMode(want ? SDL_TRUE : SDL_FALSE);
    if (want) {
        SDL_GetRelativeMouseState(NULL, NULL);   /* drain the accumulated jump */
    }
    mouseDX = mouseDY = 0.0;
}

/* Reconcile the SDL grab state with what the current mode wants. Called once
 * per controller-0 poll (menuMode known there) and from the notify hooks. */
static void reconcileGrab(int menuMode)
{
    int want;
    if (!mouseCaptureMode) {
        want = windowFocused;                              /* legacy */
    } else {
        want = captureArmed && windowFocused && !menuMode; /* click-to-lock */
    }
    applyGrab(want);
}

/* Hide the OS cursor while the game window is focused (normal PC-game
 * behaviour): in a stage the mouse drives the look axis, and in menus GE draws
 * its own crosshair that now tracks the pointer 1:1 -- a visible OS arrow on
 * top is just clutter. Show it again when focus is lost so the desktop behaves
 * normally. Relative-mouse mode hides the cursor too, but only while grabbed;
 * this covers the free-but-focused states (menus, pre-click stage). */
static void applyCursorVisibility(void)
{
    int hide = windowFocused && mouseEnabled;
    SDL_ShowCursor(hide ? SDL_DISABLE : SDL_ENABLE);
}

/* video.c focus events. In legacy mode this directly grabs/releases; in
 * capture mode it just records focus and lets reconcileGrab() decide. */
void inputSetMouseGrab(int on)
{
    windowFocused = on ? 1 : 0;
    reconcileGrab(0);   /* menuMode re-checked on the next poll anyway */
    applyCursorVisibility();
}

/* A mouse click landed in the game window (video.c). In click-to-lock mode
 * this arms + grabs; otherwise it is ignored (the game sees the click). */
void inputNotifyClick(void)
{
    if (mouseCaptureMode && mouseEnabled && windowFocused) {
        captureArmed = 1;
        reconcileGrab(0);
    }
}

/* ESC pressed (video.c). In click-to-lock mode, releases the cursor and
 * reports 1 so the caller can swallow the key; otherwise reports 0. */
int inputReleaseCapture(void)
{
    if (mouseCaptureMode && mouseGrabbed) {
        captureArmed = 0;
        applyGrab(0);
        return 1;
    }
    return 0;
}

int inputMouseCaptureActive(void) { return mouseCaptureMode && !mouseGrabbed; }

void inputPostWheel(int notches)
{
    if (notches < 0) notches = -notches;   /* both directions cycle forward */
    if (notches > 0) wheelPulse = WHEEL_PULSE_POLLS;
}

/* Called from the host event pump on SDL_CONTROLLERDEVICEADDED/REMOVED.
 * Closes every open pad and re-opens whatever is present now. `connectedMask`
 * bit 0 (keyboard/mouse) is always kept. Note: the game latches the mask at
 * osContInit (boot), so a pad added later still merges into controller 0 for
 * play -- it just won't appear as a separate controller channel. */
void inputRescanPads(void)
{
    for (int i = 0; i < MAX_PADS; ++i) {
        if (pads[i]) {
            SDL_GameControllerClose(pads[i]);
            pads[i] = NULL;
        }
    }
    inputOpenPads();
    sysLogPrintf(LOG_NOTE, "input: rescanned pads (mask=0x%x, %d controller(s))",
                 connectedMask, numControllers);
}

int inputConnectedMask(void)
{
    return connectedMask;
}

int inputGetNumControllers(void)
{
    return numControllers;
}

PD_CONSTRUCTOR static void inputConfigInit(void)
{
    configRegisterInt("Input.MouseEnabled", &mouseEnabled, 0, 1);
    configRegisterInt("Input.MouseCaptureMode", &mouseCaptureMode, 0, 1);
    configRegisterInt("Input.MouseAimSpeed", &mouseAimSpeed, 1, 500);
    configRegisterInt("Input.AimBand", &aimBand, 5, 40);
    configRegisterInt("Input.MouseTurnSpeed", &mouseTurnSpeed, 1, 500);
    configRegisterInt("Input.MenuPointerSpeed", &menuPointerSpeed, 10, 500);
    configRegisterInt("Input.MenuPointerMode", &menuPointerMode, 0, 1);
    configRegisterInt("Input.HipfirePitchSpeed", &hipfirePitchSpeed, 10, 500);
    configRegisterInt("Input.MouseInvertY", &mouseInvertY, 0, 1);
    configRegisterInt("Input.MouseYScale", &mouseYScale, 1, 500);
    configRegisterInt("Input.MouseSmoothing", &mouseSmoothing, 0, 90);
    configRegisterInt("Input.MouseRawInput", &mouseRawInput, 0, 1);
    configRegisterInt("Input.PadDeadzone", &padDeadzone, 0, 30000);
    configRegisterInt("Input.PadTriggerPct", &padTriggerPct, 1, 99);
    configRegisterInt("Input.PadLookInvertY", &padLookInvertY, 0, 1);
}
