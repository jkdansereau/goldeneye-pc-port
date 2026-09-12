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

/* Mute-on-focus-loss (Audio.MuteOnFocusLoss, default on). Called from the
 * host event pump on SDL window focus events; while muted, mixed blocks are
 * dropped instead of queued. No-op when the toggle is off. */
void audioHandleFocus(int gained);

/* Number of samples (stereo s16 frames) currently queued. */
s32  audioGetSamplesBuffered(void);

/* D204/F1: emulate the N64 AI_LEN_REG -- bytes remaining in the buffer the
 * DAC is *currently* playing, NOT the whole queue depth. src/audi.c does
 * arithmetic on this value that only holds inside the N64's range. */
u32  audioGetAiLengthBytes(void);

/* Queue the next block of mixed samples (len is in bytes). */
void audioSetNextBuffer(const s16 *buf, u32 len);

#ifdef __cplusplus
}
#endif

#endif /* PORT_AUDIO_H */
