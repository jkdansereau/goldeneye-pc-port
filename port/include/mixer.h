#ifndef PORT_MIXER_H
#define PORT_MIXER_H

/*
 * Audio mixing.
 * Modelled on the PD port's port/include/mixer.h.
 *
 * On the N64 the RSP audio ucode does the final mix / sample-rate handling
 * before the AI DMA. Since the RSP is emulated in software here, this module
 * performs that final processing on the CPU and hands the result to audio.c.
 *
 * NOTE: the exact responsibilities depend on how GE's libaudio output is
 * wired (see src/audi.c). This is a placeholder interface to be refined in
 * Phase 3 (audio).
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

void mixerInit(void);
void mixerDestroy(void);

/* Process one frame of audio. `in` is the libaudio-mixed buffer, `frames`
 * the number of stereo frames. Output is queued via audioSetNextBuffer. */
void mixerProcess(const s16 *in, u32 frames);

#ifdef __cplusplus
}
#endif

#endif /* PORT_MIXER_H */
