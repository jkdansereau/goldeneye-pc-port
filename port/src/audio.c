/*
 * Audio: SDL audio device + buffer queueing.
 *
 * libaudio (AL) produces mixed s16 stereo output on the CPU. This module owns
 * the SDL audio device and queues the mixed samples. osAiSetNextBuffer /
 * osAiGetLength (libultra.c) route through here.
 *
 * Modelled on the PD port's port/src/audio.c (~75 lines).
 *
 * STATUS: scaffolding stub — implement during Phase 3.
 */

#include <SDL.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "audio.h"

static SDL_AudioDeviceID dev = 0;
static const s16 *nextBuf = NULL;
static u32 nextSize = 0;

static int  bufferSize = 512;
static int  queueLimit = 8192;

int audioInit(void)
{
    /* TODO(Phase 3):
     *  - SDL_InitSubSystem(AUDIO)
     *  - open device: 22020 Hz (match GE's OUTPUT_RATE in src/audi.c),
     *    AUDIO_S16SYS, 2 channels
     *  - unpause
     */
    sysLogPrintf(LOG_INFO, "audioInit: TODO (Phase 3)");
    return 0;
}

void audioDestroy(void)
{
    if (dev) { SDL_CloseAudioDevice(dev); dev = 0; }
}

s32 audioGetSamplesBuffered(void)
{
    return dev ? (SDL_GetQueuedAudioSize(dev) / 4) : 0;
}

void audioSetNextBuffer(const s16 *buf, u32 len)
{
    nextBuf = buf;
    nextSize = len;
}

void audioEndFrame(void)
{
    if (nextBuf && nextSize && dev) {
        if (audioGetSamplesBuffered() < queueLimit) {
            SDL_QueueAudio(dev, nextBuf, nextSize);
        }
        nextBuf = NULL;
        nextSize = 0;
    }
}

PD_CONSTRUCTOR static void audioConfigInit(void)
{
    configRegisterInt("Audio.BufferSize", &bufferSize, 0, 1 * 1024 * 1024);
    configRegisterInt("Audio.QueueLimit", &queueLimit, 0, 1 * 1024 * 1024);
}
