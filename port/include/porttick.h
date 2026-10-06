#ifndef PORT_PORTTICK_H
#define PORT_PORTTICK_H

#include <ultra64.h>

/* D486 (RULE-2, D13 class): some decomp code advances a counter once per
 * rendered frame. The N64 rendered at 2 ticks per frame (more under load);
 * the port renders 1 tick per frame at 60 fps (0 when uncapped), so those
 * counters ran 2-3x faster in real time. portN64FrameStep() is true on every
 * call at >= 2 ticks per frame (identical to the N64 path) and on every
 * second tick below that, so a per-frame counter gated by it keeps the N64's
 * 2-ticks-per-frame pace -- the same reference D427/D451 use for auto-fire.
 * Stateless: it only looks at the tick counter crossing an even boundary. */
extern s32 g_ClockTimer;
extern s32 g_GlobalTimer;

static inline int portN64FrameStep(void)
{
    return g_ClockTimer >= 2 || ((g_GlobalTimer >> 1) != ((g_GlobalTimer - g_ClockTimer) >> 1));
}

#endif
