#include <ultra64.h>
#include "frametiming.h"

#ifdef PORT
/* Max frames of simulation catch-up per rendered frame (D155). 6 ~= 100 ms
 * at 60 Hz: covers a legitimately slow frame without letting a multi-second
 * stall spiral the anim/sim loops. */
#define FRAMETIMING_PORT_MAX_CATCHUP 6
#endif

// data
s32 lastFrameCounter = -1;
s32 currentFrameCounter = 0;

/**
 * Appears to be rendered framerate, or some kind of counter since the last frame update.
 */
s32 speedgraphframes = 1;

#if defined(BUGFIX_R1)
// EU address D_8004111C
f32 jpD_800484CC = 1.0f;

// EU address D_80041120
f32 jpD_800484D0 = 1.0f;
#endif

s32 previousFrameCounter = -1;
s32 halfFrameCounter = 0; // half of currentFrameCounter
s32 isFrameCounterOdd = 0; // is currentFrameCounter Odd
s32 halfMinusPreviousCounter = 0; // half - previousFrameCounter
u32 copy_of_osgetcount_value_0 = 0;
u32 copy_of_osgetcount_value_1 = 0;
s32 frameDelay = 1; //usually 1



/**
 * Stores the current OS count in the two global variables.
 */
void store_osgetcount(void)
{
    copy_of_osgetcount_value_1 = osGetCount();
    copy_of_osgetcount_value_0 = copy_of_osgetcount_value_1;
}


/**
 * Updates the timing-related counters and frame information based on the given argument.
 *
 * @param deltaFrames The number of frames to add to the current frame counter.
 */
void updateFrameCounters(s32 deltaFrames)
{
    copy_of_osgetcount_value_0 = (s32) copy_of_osgetcount_value_1;
    copy_of_osgetcount_value_1 = osGetCount();

    lastFrameCounter = currentFrameCounter;
    currentFrameCounter = (s32) (currentFrameCounter + deltaFrames);
    speedgraphframes = deltaFrames;

    #ifdef BUGFIX_R1
    jpD_800484CC = (f32) deltaFrames;
    #ifdef REFRESH_PAL
    jpD_800484D0 = (jpD_800484CC * 60.0f) / 50.0f;
    #else
    jpD_800484D0 = (f32) jpD_800484CC;
    #endif
    #endif

    previousFrameCounter = (s32) halfFrameCounter;
    halfFrameCounter = (s32) (currentFrameCounter / 2);
    isFrameCounterOdd = (s32) (currentFrameCounter & 1);
    halfMinusPreviousCounter = (s32) (halfFrameCounter - previousFrameCounter);
}


/**
 * Waits until the appropriate time has passed before updating the frame counters.
 * This function effectively controls the frame rate by waiting for the next tick.
 */
void waitForNextFrame(void) //maybe WaitForTick
{
  u32 nextFrameTime; //next frame time?
  
  do {
    #ifdef REFRESH_PAL
    nextFrameTime = ((osGetCount() - copy_of_osgetcount_value_1) + 465525) / 931050; 
    #else
    nextFrameTime = ((osGetCount() - copy_of_osgetcount_value_1) + 387937) / 775875; //current time + 1/5
    #endif
  } while (nextFrameTime < frameDelay);

  frameDelay = 1;

#ifdef PORT
  /* D155: osGetCount() is wall-clock on the PC port (D117), not a VI-locked
   * hardware counter. A real-time stall -- an asset load at a cutscene/stage
   * boundary, a host-scheduling hitch, another process hammering the machine
   * -- makes nextFrameTime balloon to hundreds or thousands of "frames". The
   * N64 was physically VI-bound and never produced more than a couple.
   * Feeding a huge deltaFrames downstream is catastrophic: it becomes
   * g_ClockTimer, which drives modelTickAnim()'s `while (numticks-- > 0)`
   * loop once per character per render AND dozens of `for (i = 0; i <
   * g_ClockTimer; i++)` sim loops (bondhead/bondview2/explosion/...). One
   * frame then takes seconds of catch-up -> the kernel-heartbeat watchdog
   * trips ("hang" at the Facility outro-cutscene end), and the slow frame
   * feeds an even larger delta next time -> unrecoverable spiral. Clamp to a
   * small catch-up bound; after a hitch the sim just resumes at roughly
   * real-time pace, exactly as the console did when it dropped frames under
   * load. Timing-compensation class, cf. D117/D134. */
  if (nextFrameTime > FRAMETIMING_PORT_MAX_CATCHUP) {
    nextFrameTime = FRAMETIMING_PORT_MAX_CATCHUP;
  }
#endif

  updateFrameCounters(nextFrameTime);
}


void setFrameDelay(s32 arg0) {
    #ifdef LEFTOVERDEBUG
    frameDelay = arg0;
    #endif
}

#ifdef VERSION_EU
void eu_sub_7f0c00a4(void)
{
  
}
#endif




