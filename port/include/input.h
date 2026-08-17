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

#ifdef __cplusplus
}
#endif

#endif /* PORT_INPUT_H */
