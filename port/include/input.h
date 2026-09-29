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
#include <SDL.h>   /* API types (SDL_GameControllerButton/Axis); cf. video.h */

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
/* D287: apply queued mouse-mode/cursor changes; host (window) thread only. */
void inputApplyMouseRequests(void);

/* WI-1 click-to-lock cursor capture. The host event pump calls
 * inputNotifyClick() when a mouse button goes down inside the game window
 * (arms + locks the cursor), and inputReleaseCapture() when ESC is pressed
 * (frees it; returns 1 if it consumed the key). */
void inputNotifyClick(void);
int  inputReleaseCapture(void);

/* F10 options overlay: force the OS cursor free + visible while the overlay
 * owns the mouse. Safe to call every poll. */
void inputSuspendForOverlay(void);

/* Raw gamepad state for controller idx (the F10 options overlay's gamepad
 * navigation). Unaffected by the overlay's pad-swallow, which happens at our
 * logic layer, not SDL's. No pad open -> 0. */
int   inputPadButton(int idx, SDL_GameControllerButton b);
short inputPadAxis(int idx, SDL_GameControllerAxis a);

/* Queue a mouse-wheel weapon-cycle input (one short A-button press). Sign is
 * ignored -- GE only cycles forward on a bare A edge. */
void inputPostWheel(int notches);

/* v0.4.0 M3: re-derive the keyboard binds after an F10 change to
 * Input.CrouchMode or a future binding capture (optionsoverlay hook).
 * Also drops the latched-crouch state. Scheduler thread only. */
void inputBindingsApply(void);
/* D384/D385: Primary/Secondary each accept a key or Mouse 1..5.
 * Legacy extra ini tokens remain on disk but are ignored until action edit. */
#define INPUT_BIND_SLOTS 2
#define INPUT_BIND_MOUSE(button) (SDL_NUM_SCANCODES + (button))
const char *inputBindingSlot(const char *key, int slot);
int inputBindingSetSlot(const char *key, int slot, int code);
int inputBindingResetKey(const char *key);

/* D345(b): 1 while the 1:1 menu pointer owns cursor_h/v_pos (in a menu,
 * abs pointer available, mouse used within the re-assert window). Port
 * screens that teleport the crosshair should skip their snap when set. */
int  inputMenuPointerLive(void);

/* Re-enumerate gamepads after a hotplug (SDL_CONTROLLERDEVICEADDED/REMOVED). */
void inputRescanPads(void);
void inputRequestRescan(void);   /* D435: from either SDL event pump; consumed by inputUpdate() */

/* D401: Rumble Pak -> real gamepad haptics. The osMotor* shims in
 * libultra.c (game's src/joy.c rumble state machine) route here.
 * idx is the N64 controller channel (0..3; out-of-range is a no-op).
 * strength is 0..1, time seconds; 0/0 stops an active rumble. The scale
 * is the single global Input.RumbleScale config value (0 = silent),
 * applied to every pad. */
int  inputRumbleSupported(int idx);
void inputRumble(int idx, f32 strength, f32 time);
f32  inputRumbleGetScale(void);
void inputRumbleSetScale(f32 v);

#ifdef __cplusplus
}
#endif

#endif /* PORT_INPUT_H */
