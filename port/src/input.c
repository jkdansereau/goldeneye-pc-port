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
 * BINDING SCHEME (documented in docs/PCPortResearch.md sec F "D118")
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
#define AIM_BAND            20    /* aim mode: usable stick range above the 60 gate */
#define AIM_GAIN            4.0   /* aim mode: px/poll -> counts into the band  */
#define AIM_MOVE_THRESH     0.5   /* aim mode: px/poll before the stick moves   */

static int numControllers = 1;
static int connectedMask   = 0x1;   /* controller 0 always present */

static SDL_GameController *pads[MAX_PADS];

static int mouseEnabled   = 1;
static int mouseGrabbed   = 1;      /* released while the window is unfocused */
static int mouseAimSpeed  = 50;     /* aim-mode sensitivity, percent */
static int mouseTurnSpeed = 100;    /* hipfire yaw sensitivity, percent */
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

    /* Relative mouse mode for mouse-look. ESC still quits (video.c). */
    mouseGrabbed = mouseEnabled;
    if (mouseEnabled) {
        if (mouseRawInput) {
            /* Feed the raw device delta straight through: no OS pointer
             * acceleration, no warp-based emulation. Must be set before
             * relative mode is enabled. */
            SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0");
            SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0");
            sysLogPrintf(LOG_INFO, "input: raw mouse input (no OS pointer accel)");
        }
        SDL_SetRelativeMouseMode(SDL_TRUE);
        /* Drain the initial jump. */
        SDL_GetRelativeMouseState(NULL, NULL);
    }

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
            keyDown(ks, SDL_SCANCODE_F))
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

            if (aimHeld) {
                double gx = fabs(edx)    * (mouseAimSpeed / 100.0) * AIM_GAIN;
                double gy = fabs(dyLook) * (mouseAimSpeed / 100.0) * AIM_GAIN;
                if (fabs(edx) >= AIM_MOVE_THRESH) {
                    int m = 61 + (int)gx; if (m > 60 + AIM_BAND) m = 60 + AIM_BAND;
                    sx += (edx > 0) ? m : -m;
                }
                if (fabs(dyLook) >= AIM_MOVE_THRESH) {
                    int m = 61 + (int)gy; if (m > 60 + AIM_BAND) m = 60 + AIM_BAND;
                    sy += (dyLook > 0) ? m : -m;   /* +stick_y = look down */
                }
            } else {
                sx += (int)(edx * (mouseTurnSpeed / 100.0) * MOUSE_TURN_GAIN);
                if (dyLook >= MOUSE_PITCH_THRESH)       button |= GE_CONT_E; /* C-up = look down */
                else if (dyLook <= -MOUSE_PITCH_THRESH) button |= GE_CONT_D; /* C-down = look up */
            }
        }

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

    if (getenv("GE_INPUTLOG") && (button || sx || sy)) {
        sysLogPrintf(LOG_NOTE, "GE_INPUTLOG cont%d: btn=%04x stick=(%d,%d)",
                     idx, button, sx, sy);
    }

    return button;
}

void inputSetMouseGrab(int on)
{
    int want = on && mouseEnabled;
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
    configRegisterInt("Input.MouseAimSpeed", &mouseAimSpeed, 1, 500);
    configRegisterInt("Input.MouseTurnSpeed", &mouseTurnSpeed, 1, 500);
    configRegisterInt("Input.MouseInvertY", &mouseInvertY, 0, 1);
    configRegisterInt("Input.MouseYScale", &mouseYScale, 1, 500);
    configRegisterInt("Input.MouseSmoothing", &mouseSmoothing, 0, 90);
    configRegisterInt("Input.MouseRawInput", &mouseRawInput, 0, 1);
    configRegisterInt("Input.PadDeadzone", &padDeadzone, 0, 30000);
    configRegisterInt("Input.PadTriggerPct", &padTriggerPct, 1, 99);
    configRegisterInt("Input.PadLookInvertY", &padLookInvertY, 0, 1);
}
