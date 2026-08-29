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
 *   mouse motion ....... C-buttons     (proportional pulse -- see below)
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
 * MOUSE-LOOK -> C-BUTTON BRIDGE
 *   GE has no analog-aim hook we can reach without touching src/ (game
 *   logic). The only lever is OSContPad, so mouse motion is converted to
 *   digital C-button presses. inputUpdate() integrates the relative mouse
 *   delta (scaled by Input.MouseAimSpeed/100) into a signed accumulator per
 *   axis, clamped to +/-AIM_ACCUM_CLAMP. Each poll inputComputePad() emits
 *   the matching C-button while |accum| >= 0.5 and drains one unit. A large
 *   flick therefore holds the C-button down for several frames
 *   (proportional dwell) rather than a single blip.
 *
 *   Limitations: (a) it is still digital -- GE applies its own accel curve
 *   to a held C-button, so aim speed is not linear in mouse speed;
 *   (b) very fast flicks saturate at the clamp (~8 frames of turn);
 *   (c) diagonal precision is limited to the 8 C-button combinations.
 *   Tunable via ge007.ini [Input] MouseAimSpeed / MouseInvertY. A true
 *   analog aim path would need an #ifdef PORT hook in bondview.c and is
 *   left as a TODO.
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
#define AIM_ACCUM_CLAMP     8.0
#define AIM_EMIT_THRESHOLD  0.5
#define MOUSE_TURN_GAIN     6.0   /* per-poll aimDX units -> stick-X counts */

static int numControllers = 1;
static int connectedMask   = 0x1;   /* controller 0 always present */

static SDL_GameController *pads[MAX_PADS];

static int mouseEnabled  = 1;
static int mouseAimSpeed = 50;      /* /100 -> mouse-px to accumulator units */
static int mouseInvertY  = 0;

/* Per-poll mouse-look deltas. NOT a persistent accumulator: mouse-look is a
 * displacement device, GE's C-buttons are a rate device. Integrating and
 * draining turned a flick into a pegged, slowly-recovering "stuck looking
 * up/down". We now emit a C-button only on polls where the mouse actually
 * moved that poll; stop moving -> button releases -> GE auto-centres. */
static double aimDX = 0.0;
static double aimDY = 0.0;

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
    if (mouseEnabled) {
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

    if (!mouseEnabled) {
        return;
    }

    int dx = 0, dy = 0;
    SDL_GetRelativeMouseState(&dx, &dy);

    const double scale = (double)mouseAimSpeed / 100.0;
    aimDX = dx * scale;
    aimDY = dy * scale * (mouseInvertY ? -1.0 : 1.0);
}

static int scaleAxis(int v)
{
    if (v > -STICK_DEADZONE && v < STICK_DEADZONE) {
        return 0;
    }
    if (v < 0) v += STICK_DEADZONE; else v -= STICK_DEADZONE;
    int out = (int)((long)v * STICK_MAX / (32767 - STICK_DEADZONE));
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
        if ((mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) || keyDown(ks, SDL_SCANCODE_LSHIFT))
            button |= GE_CONT_R;
        if (keyDown(ks, SDL_SCANCODE_SPACE) || keyDown(ks, SDL_SCANCODE_Z) ||
            keyDown(ks, SDL_SCANCODE_E))
            button |= GE_CONT_A;
        if (keyDown(ks, SDL_SCANCODE_X) || keyDown(ks, SDL_SCANCODE_R) ||
            keyDown(ks, SDL_SCANCODE_F))
            button |= GE_CONT_B;
        if (keyDown(ks, SDL_SCANCODE_Q))
            button |= GE_CONT_L;
        if (keyDown(ks, SDL_SCANCODE_RETURN) || keyDown(ks, SDL_SCANCODE_TAB))
            button |= GE_CONT_START;

        /* Mouse X -> analog turn (stick X, GE yaw is analog, not a C-button).
         * Mouse Y -> C-up/C-down look (GE pitch has no analog hook). Per-poll,
         * no carryover: motion this poll only, so releasing stops immediately. */
        if (mouseEnabled) {
            sx += (int)(aimDX * MOUSE_TURN_GAIN);
            if (aimDY >= AIM_EMIT_THRESHOLD)       button |= GE_CONT_D;
            else if (aimDY <= -AIM_EMIT_THRESHOLD) button |= GE_CONT_E;
        }
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

        if (rx >  RSTICK_THRESHOLD) button |= GE_CONT_F;
        if (rx < -RSTICK_THRESHOLD) button |= GE_CONT_C;
        if (ry >  RSTICK_THRESHOLD) button |= GE_CONT_D;
        if (ry < -RSTICK_THRESHOLD) button |= GE_CONT_E;

        if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > TRIG_THRESHOLD)
            button |= GE_CONT_G;
        if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > TRIG_THRESHOLD)
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
    configRegisterInt("Input.MouseInvertY", &mouseInvertY, 0, 1);
}
