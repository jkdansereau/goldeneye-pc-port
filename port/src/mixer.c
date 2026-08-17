/*
 * Audio mixing.
 *
 * On the N64 the RSP audio ucode does final mix / sample handling before the
 * AI DMA. With the RSP emulated in software, this runs on the CPU.
 *
 * Modelled on the PD port's port/src/mixer.c (~720 lines).
 *
 * STATUS: scaffolding stub — implement during Phase 3. The exact interface
 * depends on how GE's libaudio output is wired (see src/audi.c).
 */

#include "platform.h"
#include "mixer.h"
#include "audio.h"

void mixerInit(void)
{
    /* TODO(Phase 3) */
}

void mixerDestroy(void)
{
    /* TODO(Phase 3) */
}

void mixerProcess(const s16 *in, u32 frames)
{
    /* TODO(Phase 3): process + queue via audioSetNextBuffer(). */
    (void)in; (void)frames;
}
