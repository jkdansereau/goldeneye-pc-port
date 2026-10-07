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
#include "audiotrace.h"
#ifdef PORT
#include "envflag.h"   /* cached getenv for hot-path probes */
#endif

static SDL_AudioDeviceID dev = 0;

/* D470: port-level master volume (0..100, 100 = bit-identical passthrough)
 * and output-device selection (empty = SDL's default device, the old
 * behaviour). Both are plain config-backed values read where they are used. */
static int  masterVolume = 100;
static char deviceName[256] = "";

/* D470: a UI/hot-plug request to re-open the device. Set from any thread,
 * consumed (and dev closed/reopened) only on the audio-producer thread at the
 * top of audioSetNextBuffer(), the sole thread that queues to / drains `dev`,
 * so no queue call can race the close. */
static SDL_atomic_t s_devReopenReq;

/* D470: SDL_AUDIODEVICEREMOVED (video.c's event pump) -> fall back to default. */
static SDL_atomic_t s_devRemovedId;

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

/* D470: open the configured device by name (NULL name = system default).
 * A named device that cannot be opened logs a warning and falls back to the
 * default, so a missing/removed interface never leaves the game silent. */
static SDL_AudioDeviceID audioOpenConfigured(const char *name, SDL_AudioSpec *have)
{
    SDL_AudioSpec want = {0};
    want.freq     = 22050; /* GE's OUTPUT_RATE (src/audi.c) */
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = (u16)bufferSize;
    SDL_AudioDeviceID d = 0;
    if (name && name[0]) {
        d = SDL_OpenAudioDevice(name, 0, &want, have, 0);
        if (d) {
            sysLogPrintf(LOG_INFO, "audioInit: opened audio device by name: \"%s\"", name);
        } else {
            sysLogPrintf(LOG_WARNING,
                "audioInit: audio device \"%s\" unavailable (%s); falling back to the default device",
                name, SDL_GetError());
        }
    }
    if (!d) d = SDL_OpenAudioDevice(NULL, 0, &want, have, 0);
    return d;
}

/* D470: enumeration for the options UI. Names are copied (SDL's pointers are
 * only valid until the next enumeration). */
#define AUDIO_MAXDEV 16
static char s_devNames[AUDIO_MAXDEV][128];
static int  s_devCount = 0;

int audioDeviceRefresh(void)
{
    s_devCount = 0;
    if (!SDL_WasInit(SDL_INIT_AUDIO)) return 0;
    int n = SDL_GetNumAudioDevices(0);
    for (int i = 0; i < n && s_devCount < AUDIO_MAXDEV; i++) {
        const char *nm = SDL_GetAudioDeviceName(i, 0);
        if (!nm) continue;
        snprintf(s_devNames[s_devCount], sizeof(s_devNames[0]), "%s", nm);
        s_devCount++;
    }
    return s_devCount;
}

const char *audioDeviceName(int i)
{
    return (i >= 0 && i < s_devCount) ? s_devNames[i] : NULL;
}

const char *audioDeviceCurrentName(void) { return deviceName; }

void audioDeviceRequest(const char *name)
{
    snprintf(deviceName, sizeof(deviceName), "%s", name ? name : "");
    SDL_AtomicSet(&s_devReopenReq, 1);
}

void audioNotifyDeviceRemoved(u32 which)
{
    SDL_AtomicSet(&s_devRemovedId, (int)which);
}

int audioInit(void)
{
#if defined(_WIN32) && defined(SDL_HINT_AUDIODRIVER)
    /* D322: prefer DirectSound over SDL2's WASAPI backend on Windows. Measured
     * side by side on the same USB interface (tools_pc/sdl_drain_monitor.c,
     * four simultaneous streams, 700 s): WASAPI fell to 81-88 % drain for
     * ~30-50 s every ~317 s and lost 1.4 % of playback overall, whichever
     * buffer size was used, while DirectSound drained at exactly real time
     * throughout. Each WASAPI shortfall overflowed our queue and dropped whole
     * blocks (the periodic "garbled audio" bursts). WASAPI stays as the
     * fallback, and the SDL_AUDIODRIVER environment variable still overrides
     * this (SDL_HINT_NORMAL yields to the environment). */
    SDL_SetHint(SDL_HINT_AUDIODRIVER, "directsound,wasapi");
#endif
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        sysLogPrintf(LOG_ERROR, "audioInit: SDL_InitSubSystem: %s", SDL_GetError());
        return -1;
    }
    {   /* D470: list the outputs so a name can be copied into Audio.Device */
        int nd = audioDeviceRefresh();
        for (int k = 0; k < nd; k++)
            sysLogPrintf(LOG_INFO, "audioInit: output device %d: \"%s\"", k, audioDeviceName(k));
    }
    SDL_AudioSpec have;
    dev = audioOpenConfigured(deviceName, &have);
    if (!dev) {
        sysLogPrintf(LOG_ERROR, "audioInit: SDL_OpenAudioDevice: %s", SDL_GetError());
        return -1;
    }
    SDL_PauseAudioDevice(dev, 0);
    sysLogPrintf(LOG_INFO, "audioInit: opened SDL audio device at %d Hz (driver %s)", have.freq,
                 SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?");
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

/* D470: runs on the producer thread only (see s_devReopenReq). The queued
 * audio of the old device is dropped (a few tens of ms at most). */
static void audioServiceDeviceRequests(void)
{
    int removed = SDL_AtomicSet(&s_devRemovedId, 0);
    if (removed && dev && (SDL_AudioDeviceID)removed == dev) {
        sysLogPrintf(LOG_WARNING,
            "audio: output device removed (id %d); falling back to the default device", removed);
        SDL_CloseAudioDevice(dev);
        dev = 0;
        deviceName[0] = 0;   /* the configured device is gone: default until re-picked */
        SDL_AtomicSet(&s_devReopenReq, 1);
    }
    if (SDL_AtomicSet(&s_devReopenReq, 0)) {
        if (dev) { SDL_CloseAudioDevice(dev); dev = 0; }
        SDL_AudioSpec have;
        dev = audioOpenConfigured(deviceName, &have);
        if (dev) {
            SDL_PauseAudioDevice(dev, 0);
            sysLogPrintf(LOG_INFO, "audio: device (re)opened at %d Hz", have.freq);
        } else {
            sysLogPrintf(LOG_ERROR, "audio: device reopen failed: %s", SDL_GetError());
        }
    }
}

void audioSetNextBuffer(const s16 *buf, u32 len)
{
    audioServiceDeviceRequests();
    if (!s_audioDumpChecked) {
        s_audioDumpChecked = 1;
        if (getenv("GE_AUDIODUMP")) {
            s_audioDumpFile = fopen("audiodump.raw", "wb");
        }
    }
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
    if (GE_ENVFLAG("GE_D204")) {
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

    /* D77 diag (temporary): GE_AUDIOTRACE=1 already logs csplayer.c
     * [MUSICNOTE] note-on events into audiotrace.log. That proves the
     * level-music trigger fires and posts notes to the synth, but says
     * nothing about whether those notes actually reach the final mixed
     * buffer handed to the SDL device below. Interleave a cheap (~1/s)
     * peak/RMS reading of *this* buffer into the SAME file/lock
     * (geTracePrintf) so the two can be correlated by line order without
     * per-line timestamps. Remove once D77 closes. */
    if (buf && len && GE_ENVFLAG("GE_AUDIOTRACE")) {
        static u64 lastReportUs = 0;
        u64 now = sysGetMicroseconds();
        if (now - lastReportUs >= 1000000ull) {
            const s16 *s = buf;
            u32 n = len / 2u;
            s32 peak = 0;
            s64 sumsq = 0;
            for (u32 i = 0; i < n; i++) {
                s32 v = s[i];
                if (v < 0) v = -v;
                if (v > peak) peak = v;
                sumsq += (s64)s[i] * (s64)s[i];
            }
            double rms = n ? __builtin_sqrt((double)sumsq / (double)n) : 0.0;
            geTracePrintf("audiotrace.log", "[AUDIOLVL] peak=%d rms=%.1f n=%u\n",
                          (int)peak, rms, (unsigned)n);
            lastReportUs = now;
        }
    }
    /* D322 (issue #87): long-session audio degradation -- voice-pool health
     * monitor, sibling of the GE_D204 pipeline monitor above. One line per 5 s
     * of wall clock with the three pool layers a multi-hour campaign can
     * exhaust: the 8-voice SFX soft cap (sfx), the 64-slot event queue (evtq),
     * and the 24 physical synth voices shared by SFX + music (pv). A sfx count
     * that ratchets up level over level, or pv alloc that never returns to
     * baseline after quiet sections, is the voice-leak signature; q pinning at
     * queueLimit with drop climbing is the overproduction/queue signature.
     * Cheap (one locked list walk per 5 s) -- safe for a full playtest. */
    if (GE_ENVFLAG("GE_D322")) {
        static u64 d322StartUs = 0, d322NextUs = 0;
        extern void sndD322PoolSummary(s32 *, s32 *, s32 *, s32 *, s32 *, s32 *);
        u64 now = sysGetMicroseconds();
        if (!d322StartUs) { d322StartUs = now; d322NextUs = now + 5000000ull; }
        if (now >= d322NextUs) {
            s32 sfx, sfxMax, ev, pf, pl, pa;
            sndD322PoolSummary(&sfx, &sfxMax, &ev, &pf, &pl, &pa);
            sysLogPrintf(LOG_NOTE,
                "D322 pool t=%llus sfx=%d/%d evtq=%d/64 pv free/lame/alloc=%d/%d/%d q=%d/%d drop=%u",
                (unsigned long long)((now - d322StartUs) / 1000000ull),
                (int)sfx, (int)sfxMax, (int)ev,
                (int)pf, (int)pl, (int)pa,
                (int)audioGetSamplesBuffered(), queueLimit,
                (unsigned)dropCount);
            d322NextUs = now + 5000000ull;
        }
    }

    /* D470: master volume. Applied to a scratch copy (the game's buffer is
     * never modified) AFTER the game's own music/FX volumes, so it only scales
     * the final mix. gain <= 100 means |s*gain/100| <= |s|: no clipping is
     * possible. 100 (default) passes `buf` through untouched, byte-identical. */
    {
        static s16 gainBuf[8192 * 2];
        int mv = masterVolume;
        if (mv < 0) mv = 0;
        if (mv < 100 && buf && len && len <= sizeof(gainBuf)) {
            u32 n = len / 2u;
            for (u32 i = 0; i < n; i++) gainBuf[i] = (s16)(((s32)buf[i] * mv) / 100);
            buf = gainBuf;
        }
    }

    /* D470 diag: GE_AUDIOGAINLOG=1 -> peak |sample| of the final (post-gain)
     * buffer, max over each 1 s window. Cached env flag, cheap. */
    if (buf && len && GE_ENVFLAG("GE_AUDIOGAINLOG")) {
        static u64 winStart = 0; static s32 winPeak = 0;
        u64 now = sysGetMicroseconds();
        const s16 *sp = buf;
        for (u32 i = 0; i < len / 2u; i++) {
            s32 v = sp[i]; if (v < 0) v = -v;
            if (v > winPeak) winPeak = v;
        }
        if (!winStart) winStart = now;
        if (now - winStart >= 1000000ull) {
            sysLogPrintf(LOG_NOTE, "D470 gain: master=%d peak=%d", masterVolume, (int)winPeak);
            winStart = now; winPeak = 0;
        }
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
    configRegisterInt("Audio.MasterVolume", &masterVolume, 0, 100);   /* D470 */
    configRegisterString("Audio.Device", deviceName, sizeof(deviceName)); /* D470 */
}
