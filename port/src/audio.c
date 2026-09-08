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
#include <stdlib.h>
#include <stdio.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "audio.h"

static SDL_AudioDeviceID dev = 0;

/* src/audi.c: g_FrameSize + EXTRA_SAMPLES + 0x10, the exact sample count
 * info->data is allocated for (audi.c:388). Zero until amCreateAudioManager
 * has run. Used by the D204/F4 oversize guard below. */
extern u32 g_MaxFrameSize;

/* D202 diag: GE_AUDIODUMP=1 raw-dumps every mixed s16 stereo buffer at
 * 22050Hz to audiodump.raw in the CWD for offline waveform inspection.
 * Remove once D202 is root-caused. */
static FILE *s_audioDumpFile = NULL;
static int   s_audioDumpChecked = 0;
static u64   s_audioDumpBytesWritten = 0;

/* D202 diag: exact byte offset into audiodump.raw written so far, so a probe
 * elsewhere (snd.c sndPlaySfx) can log precisely which sample position a
 * request happened at, instead of guessing via an RMS scan. */
u64 audioDumpBytePos(void)
{
    return s_audioDumpBytesWritten;
}

static int  bufferSize = 512;

/* D204/F3: was 8192 frames (372 ms). GE's AI feedback loop (src/audi.c:531)
 * regulates to a very shallow queue -- it only asks for more than
 * g_MinFrameSize once the queue is under ~69 frames -- so a 372 ms drop
 * threshold is far past anything the game can steer out of, and just
 * converts overproduction into silent buffer loss at high latency.
 * 2880 frames (130 ms, four of GE's 720-sample blocks) sits comfortably above
 * the F5 cushion of AUDIO_TARGET_FRAMES while keeping worst-case latency in a
 * range the regulator can still steer out of.
 * NB: an existing data/ge007.ini pins QueueLimit, so this default only
 * applies to fresh configs -- see docs/dev/findings.md D204. */
static int  queueLimit = 2880;

/* D204/F1: size in bytes of the block most recently handed to the DAC. Used
 * to bound audioGetAiLengthBytes() to one buffer, like real AI hardware. */
static u32  lastBufferBytes = 0;

/* D204/F5: queue cushion the AI feedback loop steers toward, in stereo
 * frames. ~46 ms at 22050 Hz -- enough to absorb OS scheduling jitter and a
 * dropped retrace or two, small enough that input->sound latency stays well
 * under a frame of the game's own 30 Hz audio cadence. See the long comment
 * in audioGetAiLengthBytes(). */
#define AUDIO_TARGET_FRAMES 1024

/* D204/F4: rate-limited reporting for the two ways audio can be lost. */
static u32  dropCount = 0;
static u32  oversizeCount = 0;

/* D204 diag (temporary): GE_D204_OLD=1 restores the pre-fix behaviour --
 * osAiGetLength() reports the whole SDL queue depth and queueLimit goes back
 * to 8192 -- so before/after can be measured in ONE binary, without
 * build-to-build variance. Remove once D204 is closed. */
static int d204OldMode(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = getenv("GE_D204_OLD") ? 1 : 0;
        if (cached) queueLimit = 8192;
    }
    return cached;
}

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

/*
 * D204/F1 -- correct AI_LEN_REG semantics. ROBUSTNESS, not a bug fix:
 * measured A/B (M-64) shows this changes no observable behaviour today.
 *
 * src/audi.c:531 sizes every audio block as
 *     frameSamples = (u16)((g_FrameSize - (osAiGetLength() >> 2) + 16 + 0x25) & ~0xf)
 * clamped below at g_MinFrameSize. On the N64 osAiGetLength() reads
 * AI_LEN_REG: the bytes left in the ONE buffer the DAC is currently playing,
 * bounded by g_MaxFrameSize*4, so the term acts as a +-64-sample trim.
 *
 * Reporting SDL's whole queue depth instead lets that subtraction (done in
 * u32) wrap once the queue exceeds g_FrameSize + 0x35 frames, which it does
 * routinely -- measured high-water 2064 frames over a 5 min run. That wrap
 * is HARMLESS in practice and I verified it rather than assuming: the result
 * is stored into `s16 frameSamples` (audi.c:145), so it lands negative and
 * audi.c's own `(s32)frameSamples < g_MinFrameSize` clamp catches it, giving
 * a nominal 720-sample frame. Max block observed stays 3136 bytes against
 * the 3156-byte info->data allocation -- there is no heap overrun.
 *
 * Bounding the report to one buffer is still the right thing: it matches the
 * hardware the game was written against, keeps the arithmetic inside its
 * designed range instead of relying on a signed-overflow accident, and costs
 * one compare. AGENTS.md rule #2: it belongs here in port/, not in audi.c.
 */
u32 audioGetAiLengthBytes(void)
{
    u32 queued = dev ? SDL_GetQueuedAudioSize(dev) : 0;

    if (d204OldMode()) {
        return queued; /* pre-fix behaviour, for A/B measurement only */
    }

    /* D204/F5 -- shift the regulator's setpoint off zero. THIS is the part
     * that measurably changes behaviour.
     *
     * audi.c only asks for more than g_MinFrameSize (720) once the reported
     * length drops under ~69 frames. 720 samples per 30 Hz audio frame is
     * 21600/s against a 22050 Hz device, so at the nominal rate the pipeline
     * is structurally ~2 % short and relies on those top-up frames to make it
     * back. On the N64 a 69-frame setpoint is fine -- AI is double-buffered
     * and the VI interrupt is exact. On PC it means the loop does not react
     * until the SDL queue is 3 ms from empty, which OS scheduling jitter
     * clears easily: measured rt=0.980 sustained with q reaching 0, i.e. the
     * device is padding ~2 % of playback with silence, permanently. That lost
     * time is unrecoverable, because a queue-depth reading cannot tell the
     * regulator it has already fallen behind.
     *
     * Subtracting a target cushion makes "queue is at AUDIO_TARGET_FRAMES"
     * read as "queue is empty", so the loop tops up while there is still
     * ~46 ms of slack, and settles just above the cushion instead of
     * oscillating into starvation. Purely a port-side setpoint change: audi.c
     * still runs its own unmodified control law. */
    {
        const u32 targetBytes = AUDIO_TARGET_FRAMES * 4u;
        queued = (queued > targetBytes) ? (queued - targetBytes) : 0u;
    }

    if (lastBufferBytes && queued > lastBufferBytes) {
        queued = lastBufferBytes;
    }
    return queued;
}

void audioSetNextBuffer(const s16 *buf, u32 len)
{
    if (!s_audioDumpChecked) {
        s_audioDumpChecked = 1;
        if (getenv("GE_AUDIODUMP")) {
            s_audioDumpFile = fopen("audiodump.raw", "wb");
        }
    }
    (void)d204OldMode(); /* D204 diag: ensure the queueLimit override applies */

    /* D204 audio-health monitor: GE_D204=1 prints one line every 5 s of wall
     * clock with everything needed to see the pipeline degrade in real time:
     *
     *   rt=   produced audio seconds / wall seconds. 1.00 is healthy; a
     *         sustained value below 1 means the 30 Hz retrace that drives
     *         amMain is being starved, and audio is being generated slower
     *         than the DAC eats it (gaps, stutter).
     *   q=    SDL queue depth in frames. Should sit shallow (GE's regulator
     *         in audi.c:531 only asks for more than g_MinFrameSize once the
     *         queue is under ~69 frames). Climbing toward queueLimit means
     *         overproduction, and once it pins there every new block is
     *         dropped -- the "eventually no audio except a stuck loop" state.
     *   drop= blocks discarded because the queue was full (cumulative).
     *   max=  largest block audi.c has handed us, vs its g_MaxFrameSize*4
     *         allocation.
     *
     * Deliberately cheap (one clock read + one counter per block, ~30/s) so
     * it can be left on for a full playtest -- unlike GE_MIXERTRACE, whose
     * unbuffered per-opcode log slows the process enough to starve the audio
     * thread on its own (that artifact is what M-63 measured as "13 % of
     * real time"; see docs/dev/findings.md D204). Remove once D204 closes. */
    if (getenv("GE_D204")) {
        static u64 startUs = 0, nextReportUs = 0, produced = 0;
        static u32 maxLen = 0;
        u64 now = sysGetMicroseconds();
        if (!startUs) { startUs = now; nextReportUs = now + 5000000ull; }
        produced += len;
        if (len > maxLen) maxLen = len;
        if (now >= nextReportUs) {
            u64 wallUs = now - startUs;
            /* produced bytes / 4 = frames; / 22050 = seconds of audio */
            u64 rtx1000 = wallUs ? (produced / 4ull) * 1000000ull / 22050ull * 1000ull / wallUs : 0;
            sysLogPrintf(LOG_NOTE,
                "D204 audio health t=%llus rt=%llu.%03llu q=%d/%d drop=%u max=%u/%u",
                (unsigned long long)(wallUs / 1000000ull),
                (unsigned long long)(rtx1000 / 1000), (unsigned long long)(rtx1000 % 1000),
                (int)audioGetSamplesBuffered(), queueLimit,
                (unsigned)dropCount, (unsigned)maxLen,
                (unsigned)(g_MaxFrameSize * 4u));
            nextReportUs = now + 5000000ull;
        }
    }

    /* D204/F4: invariant guard, believed unreachable. info->data is allocated
     * for exactly g_MaxFrameSize samples (audi.c:388), so a longer block means
     * alAudioFrame() has already run off the end of it. audi.c's own signed
     * clamp should make that impossible (see audioGetAiLengthBytes above), and
     * no run has ever tripped this -- but "impossible" is worth one compare
     * per block when the alternative is a silent heap overrun. */
    if (g_MaxFrameSize && len > g_MaxFrameSize * 4u) {
        if (oversizeCount++ < 8) {
            sysLogPrintf(LOG_ERROR,
                "D204: oversized audio block %u bytes (info->data alloc %u) -- "
                "audi.c frameSamples escaped its clamp; heap already overrun",
                (unsigned)len, (unsigned)(g_MaxFrameSize * 4u));
        }
        return;
    }

    if (s_audioDumpFile && buf && len) {
        fwrite(buf, 1, len, s_audioDumpFile);
        s_audioDumpBytesWritten += len;
    }
    if (dev && buf && len) {
        if (audioGetSamplesBuffered() < queueLimit) {
            SDL_QueueAudio(dev, buf, len);
            lastBufferBytes = len;
        } else if (dropCount++ % 128 == 0) {
            /* D204/F3: dropping here used to be silent, so overproduction
             * surfaced only as unexplained dropouts. */
            sysLogPrintf(LOG_NOTE,
                "D204: audio queue full (%d/%d frames), dropped block #%u",
                (int)audioGetSamplesBuffered(), queueLimit, (unsigned)dropCount);
        }
    }
}

PD_CONSTRUCTOR static void audioConfigInit(void)
{
    configRegisterInt("Audio.BufferSize", &bufferSize, 0, 1 * 1024 * 1024);
    configRegisterInt("Audio.QueueLimit", &queueLimit, 0, 1 * 1024 * 1024);
}
