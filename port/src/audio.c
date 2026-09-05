/*
 * Audio: SDL audio device + buffer queueing.
 *
 * libaudio (AL) produces mixed s16 stereo output on the CPU. This module owns
 * the SDL audio device and queues the mixed samples. osAiSetNextBuffer /
 * osAiGetLength (libultra.c) route through here.
 *
 * Modelled on the PD port's port/src/audio.c (~75 lines).
 */

#include <SDL.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "audio.h"

static SDL_AudioDeviceID dev = 0;

static int  bufferSize = 512;
static int  queueLimit = 8192;

int audioInit(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        sysLogPrintf(LOG_ERROR, "audioInit: SDL_InitSubSystem: %s", SDL_GetError());
        return -1;
    }
    SDL_AudioSpec want = {0};
    want.freq     = 22050; /* GE's OUTPUT_RATE (src/audi.c) */
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = (u16)bufferSize;
    SDL_AudioSpec have;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) {
        sysLogPrintf(LOG_ERROR, "audioInit: SDL_OpenAudioDevice: %s", SDL_GetError());
        return -1;
    }
    SDL_PauseAudioDevice(dev, 0);
    sysLogPrintf(LOG_INFO, "audioInit: opened SDL audio device at %d Hz", have.freq);
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
    if (dev && buf && len && audioGetSamplesBuffered() < queueLimit) {
        SDL_QueueAudio(dev, buf, len);
    }
}

PD_CONSTRUCTOR static void audioConfigInit(void)
{
    configRegisterInt("Audio.BufferSize", &bufferSize, 0, 1 * 1024 * 1024);
    configRegisterInt("Audio.QueueLimit", &queueLimit, 0, 1 * 1024 * 1024);
}
