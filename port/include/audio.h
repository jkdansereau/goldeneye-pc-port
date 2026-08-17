#ifndef PORT_AUDIO_H
#define PORT_AUDIO_H

/*
 * Audio: SDL audio device + buffer queueing.
 * Modelled on the PD port's port/include/audio.h.
 *
 * libaudio (AL) runs on the CPU and produces mixed s16 stereo output. The AI
 * (osAiSetNextBuffer / osAiGetLength / osAiSetFrequency) is mapped onto this
 * layer, which queues the mixed samples to the SDL audio device.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the SDL audio device. Returns 0 on success. */
int  audioInit(void);
void audioDestroy(void);

/* Number of samples (stereo s16 frames) currently queued. */
s32  audioGetSamplesBuffered(void);

/* Queue the next block of mixed samples (len is in bytes). */
void audioSetNextBuffer(const s16 *buf, u32 len);

/* Called at the end of each frame to flush the queued buffer to the device. */
void audioEndFrame(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_AUDIO_H */
