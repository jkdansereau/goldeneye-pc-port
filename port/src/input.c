/*
 * Input: SDL2 keyboard/mouse/gamepad -> N64 controller structs.
 *
 * The game reads controllers via osContInit / osContStartReadData (src/joy.c).
 * This module populates the N64 controller state from SDL events.
 *
 * Modelled on the PD port's port/src/input.c (~1550 lines). Default bindings
 * follow the 1964GEPD / Xbox schemes (see the PD port README table).
 *
 * STATUS: scaffolding stub — implement during Phase 3.
 */

#include <SDL.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "input.h"

static int numControllers = 1;

int inputInit(void)
{
    /* TODO(Phase 3):
     *  - SDL_InitSubSystem(INPUT)
     *  - open gamepad(s)
     *  - set up relative mouse mode for mouse-look
     *  - load bindings from config
     */
    sysLogPrintf(LOG_INFO, "inputInit: TODO (Phase 3)");
    return 0;
}

void inputDestroy(void)
{
    /* TODO(Phase 3) */
}

void inputUpdate(void)
{
    /* TODO(Phase 3): poll SDL events, write into the controller state that
     * osContStartReadData / osContRead will return. */
}

int inputGetNumControllers(void)
{
    return numControllers;
}
