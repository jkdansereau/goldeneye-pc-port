#ifndef PORT_INPUT_H
#define PORT_INPUT_H

/*
 * Input: SDL2 keyboard/mouse/gamepad -> N64 controller structs.
 * Modelled on the PD port's port/include/input.h.
 *
 * The game reads controllers via osContInit / osContStartReadData (src/joy.c).
 * This layer populates the N64 controller state (buttons, stick) from SDL
 * events. Default bindings follow the 1964GEPD / Xbox schemes; see the PD
 * port README for the reference table.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize SDL input. Returns 0 on success. */
int  inputInit(void);
void inputDestroy(void);

/* Poll once per frame; updates the connected-controller state that
 * osContStartReadData / osContRead will return. */
void inputUpdate(void);

/* Number of controllers currently "connected" (1..4). */
int  inputGetNumControllers(void);

/* Bitmask of connected controllers (bit N = controller N). Bit 0 is always
 * set (keyboard/mouse). Consumed by libultra.c's osContInit. */
int  inputConnectedMask(void);

/* Compute the N64 button mask + analog stick for controller `idx`.
 * Returns the 16-bit CONT_* button mask; writes the stick (-80..80) through
 * the out params. Reads current SDL keyboard/mouse/gamepad state plus the
 * mouse-aim accumulator maintained by inputUpdate(). */
unsigned inputComputePad(int idx, signed char *stick_x, signed char *stick_y);

/* Grab/release the mouse (relative-mouse mode). The host event pump calls
 * this on window focus loss/gain so alt-tabbing frees the cursor. A release
 * also suspends mouse-aim reads until re-grabbed. No-op if the mouse is
 * disabled in config. */
void inputSetMouseGrab(int on);

/* WI-1 click-to-lock cursor capture (Input.MouseCaptureMode = 1). The host
 * event pump calls inputNotifyClick() when a mouse button goes down inside the
 * game window (arms + locks the cursor), and inputReleaseCapture() when ESC is
 * pressed (frees it; returns 1 if it consumed the key). inputMouseCaptureActive()
 * is true when capture mode is on and the cursor is currently free -- the pump
 * uses it to decide whether a click should be swallowed rather than passed on. */
void inputNotifyClick(void);
int  inputReleaseCapture(void);
int  inputMouseCaptureActive(void);

/* Queue a mouse-wheel weapon-cycle input (one short A-button press). Sign is
 * ignored -- GE only cycles forward on a bare A edge. */
void inputPostWheel(int notches);

/* Re-enumerate gamepads after a hotplug (SDL_CONTROLLERDEVICEADDED/REMOVED). */
void inputRescanPads(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_INPUT_H */
