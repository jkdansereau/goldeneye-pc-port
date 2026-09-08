/*
 * Audio mixing — software implementation of GE's RSP audio ucode opcodes.
 *
 * On the N64 the RSP audio ucode (aspMain) executes an acmd list built by
 * libaudio/naudio to do ADPCM decode, resampling, and the final envelope
 * mix. The RSP is never run on PC (port/src/ucode.c); instead every aXxx
 * macro in include/PR/abi.h is redefined (port/include/mixer.h, included
 * under #ifdef PORT) to call one of the Impl functions below immediately,
 * against a small software "DMEM" scratch buffer — the Perfect Dark PC
 * port's macro-swap trick (docs/dev/AUDIO-PLAN.md), adapted to GE's ABI.
 *
 * GE uses the classic IDO libaudio ABI, not PD's differently-shaped naudio
 * "New" ABI — verified against every call site in src/libultra/audio/*.c
 * and src/libultrare/audio/{env,reverb}.c. The DSP algorithms below (ADPCM
 * decode, linear resample, linear envelope mix) are the same Nintendo/SGI
 * ucode math PD's port/src/mixer.c ported (scalar path); only the ABI/
 * addressing layer here is GE-specific.
 *
 * GE's aSetBuffer(flags, in, out, count) sets a small persistent context
 * that several opcodes read instead of taking DMEM addresses/counts
 * directly (see MixerCtx below) — reconstructed from reading every call
 * site (docs/dev/findings.md D199), not from RSP disassembly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <PR/abi.h>
#include <PR/os.h>
#include "system.h"

/* D202 diag (temporary): per-opcode DMEM address/context trace, gated by
 * GE_MIXERTRACE=1, to test the M-56 "concurrent voices share/clobber DMEM
 * context" hypothesis directly against a live repro. Remove once
 * root-caused. */
extern char *getenv(const char *);
static FILE *sMixerTraceFile = NULL;
static int   sMixerTraceChecked = 0;
static int mixerTraceOn(void)
{
    if (!sMixerTraceChecked) {
        sMixerTraceChecked = 1;
        if (getenv("GE_MIXERTRACE")) {
            sMixerTraceFile = fopen("mixertrace.log", "a");
            if (sMixerTraceFile) setvbuf(sMixerTraceFile, NULL, _IONBF, 0);
        }
    }
    return sMixerTraceFile != NULL;
}
#define MTRACE(...) do { if (mixerTraceOn()) fprintf(sMixerTraceFile, __VA_ARGS__); } while (0)

#include "platform.h"
#include "system.h"
#include "mixer.h"
#include "audio.h"

/* Largest DMEM address GE's audio call sites use is AL_AUX_R_OUT (2048) +
 * up to AL_MAX_RSP_SAMPLES (160) samples * 2 bytes; round up generously. */
#define DMEM_SIZE 4096

static u8 sDmem[DMEM_SIZE];

#define DMEM_U8(a)  (sDmem + (a))
#define DMEM_S16(a) ((s16 *)(sDmem + (a)))

static inline s16 mixerClamp16(s32 v)
{
    if (v < -0x8000) return -0x8000;
    if (v > 0x7fff) return 0x7fff;
    return (s16)v;
}

/*
 * Persistent context set by aSetBuffer, read by opcodes that don't carry
 * their own DMEM addresses/counts. Reconstructed from call-site pairs:
 *
 *   resample.c:   aSetBuffer(0, inp, *outp, outCnt<<1); aResample(...)
 *   load.c:       aSetBuffer(0, dmemAddr, 0, nbytes...); aLoadBuffer(dram)
 *   reverb.c:     aSetBuffer(0, 0, buff, count<<1);      aSaveBuffer(dram)
 *   mainbus.c:    aSetBuffer(0, 0, 0, outCount<<1); aMix(...); aMix(...)
 *   save.c:       aSetBuffer(0, 0, 0, outCount<<1); aInterleave(L, R)
 *   env.c:        aSetBuffer(A_MAIN, *inp, AL_MAIN_L_OUT+*outp, outCount<<1);
 *                 aSetBuffer(A_AUX, AL_MAIN_R_OUT+*outp, AL_AUX_L_OUT+*outp,
 *                            AL_AUX_R_OUT+*outp);
 *                 aEnvMixer(...)
 *
 * i.e. LoadBuffer/ADPCMdec/Resample read `in` as their DMEM source (or, for
 * LoadBuffer, as the load destination); Resample/EnvMixer read `out` as a
 * destination too; SaveBuffer/Interleave read `out` as their DMEM source;
 * Mix/DMEMMove/ClearBuffer take explicit addresses and don't touch this
 * context at all. The A_AUX-flagged aSetBuffer call packs three more
 * envmixer-only DMEM addresses (dry-R, wet-L, wet-R) — its third argument
 * is a DMEM address here, not a byte count, unlike every other opcode's use
 * of aSetBuffer's `count` field.
 */
static struct {
    u16 in;
    u16 out;
    u32 count;
    u16 dryR;
    u16 wetL;
    u16 wetR;
} sCtx;

void aSetBufferImpl(u32 flags, u16 i, u16 o, u16 c)
{
    if (flags & A_AUX) {
        sCtx.dryR = i;
        sCtx.wetL = o;
        sCtx.wetR = c;
    } else {
        sCtx.in = i;
        sCtx.out = o;
        sCtx.count = c;
    }
    MTRACE("[SETBUF] flags=%u i=%u o=%u c=%u\n", flags, i, o, c);
}

void aClearBufferImpl(u16 addr, u32 count)
{
    /* D202/M-65 diag (temporary): alMainBusPull's clear of AL_MAIN_L_OUT is
     * the first opcode of every audio frame, so it is an exact frame
     * boundary. GE_DMEMWIPE=1 zeroes the scratch region below AL_MAIN_L_OUT
     * (AL_TEMP_0/1/2, AL_DECODER_IN, AL_RESAMPLER_OUT) there. Nothing on real
     * hardware may read those without writing them first in the same command
     * list, so this must be a no-op; if it silences the stuck drone, some
     * opcode is reading scratch DMEM left over from the previous frame.
     * Remove once root-caused. */
    if (addr == 1088 /* AL_MAIN_L_OUT */ && getenv("GE_DMEMWIPE")) {
        memset(sDmem, 0, sizeof(sDmem));
    }
    memset(DMEM_U8(addr), 0, count);
}


void aLoadBufferImpl(u32 dramAddr)
{
    if (mixerTraceOn()) {
        const u8 *src = (const u8 *)osPhysicalToVirtual(dramAddr);
        u32 n = (sCtx.count > 72 ? 72 : sCtx.count);
        u32 k;
        fprintf(sMixerTraceFile, "[LOADBUF] dram=0x%08x -> in=%u count=%u bytes=", dramAddr, sCtx.in, sCtx.count);
        for (k = 0; k < n; k++) fprintf(sMixerTraceFile, "%02x", src[k]);
        fprintf(sMixerTraceFile, "\n");
    }
    memcpy(DMEM_U8(sCtx.in), osPhysicalToVirtual(dramAddr), sCtx.count);
}

void aSaveBufferImpl(u32 dramAddr)
{
    memcpy(osPhysicalToVirtual(dramAddr), DMEM_U8(sCtx.out), sCtx.count);

    (void)0;
}

void aDMEMMoveImpl(u16 in, u16 out, u32 count)
{
    memmove(DMEM_U8(out), DMEM_U8(in), count);
}

void aSegmentImpl(u32 seg, u32 base)
{
    /* No-op on PC: GE's audio DMA addresses are already resolved via
     * osVirtualToPhysical before reaching us (D199); segment/base never
     * factor into DMEM addressing here. */
    (void)seg; (void)base;
}

/* ------------------------------------------------------------------------ */
/* ADPCM                                                                     */
/* ------------------------------------------------------------------------ */

static s16 sAdpcmTable[8][2][8];
static ADPCM_STATE *sAdpcmLoopState;

void aLoadADPCMImpl(u32 count, u32 dramAddr)
{
    /* `count` is GE's bookSize in bytes (2 * order * npredictors * 8);
     * order=2 always for this format, matching sAdpcmTable's shape. */
    u32 n = count;
    void *src = osPhysicalToVirtual(dramAddr);
    if (n > sizeof(sAdpcmTable)) n = sizeof(sAdpcmTable);
    memcpy(sAdpcmTable, src, n);
    if (mixerTraceOn()) {
        int p, q, r;
        fprintf(sMixerTraceFile, "[LOADADPCM] count=%u dram=%p book=", count, src);
        for (p = 0; p < 8; p++) for (q = 0; q < 2; q++) for (r = 0; r < 8; r++)
            fprintf(sMixerTraceFile, "%d,", sAdpcmTable[p][q][r]);
        fprintf(sMixerTraceFile, "\n");
    }
}

void aSetLoopImpl(u32 stateAddr)
{
    sAdpcmLoopState = (ADPCM_STATE *)osPhysicalToVirtual(stateAddr);
}

void aADPCMdecImpl(u32 flags, u32 stateAddr)
{
    ADPCM_STATE *state = (ADPCM_STATE *)osPhysicalToVirtual(stateAddr);
    MTRACE("[ADPCMDEC] flags=%u state=%p in=%u out=%u count=%u book0=%d\n",
           flags, (void *)state, sCtx.in, sCtx.out, sCtx.count, sAdpcmTable[0][0][0]);
    u8 *in = DMEM_U8(sCtx.in);
    s16 *out = DMEM_S16(sCtx.out);
    s32 nbytes = (s32)((sCtx.count + 31) & ~31u); /* round up to 16-sample (32-byte) chunks */

    if (flags & A_INIT) {
        memset(out, 0, 16 * sizeof(s16));
    } else if (flags & A_LOOP) {
        memcpy(out, sAdpcmLoopState, 16 * sizeof(s16));
    } else {
        memcpy(out, state, 16 * sizeof(s16));
    }
    out += 16;

    while (nbytes > 0) {
        int shift = *in >> 4;
        int tableIndex = *in++ & 0xf;
        s16 (*tbl)[8] = sAdpcmTable[tableIndex];
        int i, j, k;

        for (i = 0; i < 2; i++) {
            s16 ins[8];
            s16 prev1 = out[-1];
            s16 prev2 = out[-2];

            for (j = 0; j < 4; j++) {
                ins[j * 2]     = (s16)((((*in >> 4) << 28) >> 28) << shift);
                ins[j * 2 + 1] = (s16)((((*in++ & 0xf) << 28) >> 28) << shift);
            }
            for (j = 0; j < 8; j++) {
                s32 acc = tbl[0][j] * prev2 + tbl[1][j] * prev1 + ((s32)ins[j] << 11);
                for (k = 0; k < j; k++) {
                    acc += tbl[1][(j - k) - 1] * ins[k];
                }
                acc >>= 11;
                *out++ = mixerClamp16(acc);
            }
        }
        nbytes -= 16 * (s32)sizeof(s16);
    }

    memcpy(state, out - 16, 16 * sizeof(s16));

    if (mixerTraceOn()) {
        s16 *dumpOut = DMEM_S16(sCtx.out) + 16; /* skip the 16-sample history/init lead-in */
        u32 n = (sCtx.count > 64 ? 64 : sCtx.count);
        u32 k;
        fprintf(sMixerTraceFile, "[PCMOUT] book0=%d first-decoded-frame[0..%u]=", sAdpcmTable[0][0][0], n / 2 - 1);
        for (k = 0; k < n / 2; k++) fprintf(sMixerTraceFile, "%d,", dumpOut[k]);
        fprintf(sMixerTraceFile, "\n");
    }
}

/* ------------------------------------------------------------------------ */
/* Resample (linear, N64 64-phase table)                                    */
/* ------------------------------------------------------------------------ */

static const s16 sResampleTable[64][4] = {
    {0x0c39, 0x66ad, 0x0d46, 0xffdf}, {0x0b39, 0x6696, 0x0e5f, 0xffd8},
    {0x0a44, 0x6669, 0x0f83, 0xffd0}, {0x095a, 0x6626, 0x10b4, 0xffc8},
    {0x087d, 0x65cd, 0x11f0, 0xffbf}, {0x07ab, 0x655e, 0x1338, 0xffb6},
    {0x06e4, 0x64d9, 0x148c, 0xffac}, {0x0628, 0x643f, 0x15eb, 0xffa1},
    {0x0577, 0x638f, 0x1756, 0xff96}, {0x04d1, 0x62cb, 0x18cb, 0xff8a},
    {0x0435, 0x61f3, 0x1a4c, 0xff7e}, {0x03a4, 0x6106, 0x1bd7, 0xff71},
    {0x031c, 0x6007, 0x1d6c, 0xff64}, {0x029f, 0x5ef5, 0x1f0b, 0xff56},
    {0x022a, 0x5dd0, 0x20b3, 0xff48}, {0x01be, 0x5c9a, 0x2264, 0xff3a},
    {0x015b, 0x5b53, 0x241e, 0xff2c}, {0x0101, 0x59fc, 0x25e0, 0xff1e},
    {0x00ae, 0x5896, 0x27a9, 0xff10}, {0x0063, 0x5720, 0x297a, 0xff02},
    {0x001f, 0x559d, 0x2b50, 0xfef4}, {0xffe2, 0x540d, 0x2d2c, 0xfee8},
    {0xffac, 0x5270, 0x2f0d, 0xfedb}, {0xff7c, 0x50c7, 0x30f3, 0xfed0},
    {0xff53, 0x4f14, 0x32dc, 0xfec6}, {0xff2e, 0x4d57, 0x34c8, 0xfebd},
    {0xff0f, 0x4b91, 0x36b6, 0xfeb6}, {0xfef5, 0x49c2, 0x38a5, 0xfeb0},
    {0xfedf, 0x47ed, 0x3a95, 0xfeac}, {0xfece, 0x4611, 0x3c85, 0xfeab},
    {0xfec0, 0x4430, 0x3e74, 0xfeac}, {0xfeb6, 0x424a, 0x4060, 0xfeaf},
    {0xfeaf, 0x4060, 0x424a, 0xfeb6}, {0xfeac, 0x3e74, 0x4430, 0xfec0},
    {0xfeab, 0x3c85, 0x4611, 0xfece}, {0xfeac, 0x3a95, 0x47ed, 0xfedf},
    {0xfeb0, 0x38a5, 0x49c2, 0xfef5}, {0xfeb6, 0x36b6, 0x4b91, 0xff0f},
    {0xfebd, 0x34c8, 0x4d57, 0xff2e}, {0xfec6, 0x32dc, 0x4f14, 0xff53},
    {0xfed0, 0x30f3, 0x50c7, 0xff7c}, {0xfedb, 0x2f0d, 0x5270, 0xffac},
    {0xfee8, 0x2d2c, 0x540d, 0xffe2}, {0xfef4, 0x2b50, 0x559d, 0x001f},
    {0xff02, 0x297a, 0x5720, 0x0063}, {0xff10, 0x27a9, 0x5896, 0x00ae},
    {0xff1e, 0x25e0, 0x59fc, 0x0101}, {0xff2c, 0x241e, 0x5b53, 0x015b},
    {0xff3a, 0x2264, 0x5c9a, 0x01be}, {0xff48, 0x20b3, 0x5dd0, 0x022a},
    {0xff56, 0x1f0b, 0x5ef5, 0x029f}, {0xff64, 0x1d6c, 0x6007, 0x031c},
    {0xff71, 0x1bd7, 0x6106, 0x03a4}, {0xff7e, 0x1a4c, 0x61f3, 0x0435},
    {0xff8a, 0x18cb, 0x62cb, 0x04d1}, {0xff96, 0x1756, 0x638f, 0x0577},
    {0xffa1, 0x15eb, 0x643f, 0x0628}, {0xffac, 0x148c, 0x64d9, 0x06e4},
    {0xffb6, 0x1338, 0x655e, 0x07ab}, {0xffbf, 0x11f0, 0x65cd, 0x087d},
    {0xffc8, 0x10b4, 0x6626, 0x095a}, {0xffd0, 0x0f83, 0x6669, 0x0a44},
    {0xffd8, 0x0e5f, 0x6696, 0x0b39}, {0xffdf, 0x0d46, 0x66ad, 0x0c39}
};

void aResampleImpl(u32 flags, u16 pitch, u32 stateAddr)
{
    RESAMPLE_STATE *stateBuf = (RESAMPLE_STATE *)osPhysicalToVirtual(stateAddr);
    s16 *state = (s16 *)stateBuf;
    MTRACE("[RESAMPLE] flags=%u pitch=%u state=%p in=%u out=%u count=%u\n",
           flags, pitch, (void *)stateBuf, sCtx.in, sCtx.out, sCtx.count);
    s16 tmp[16];
    s16 *inInitial = DMEM_S16(sCtx.in);
    s16 *in = inInitial;
    s16 *out = DMEM_S16(sCtx.out);
    s32 nbytes = (s32)((sCtx.count + 15) & ~15u);
    u32 pitchAccumulator;
    s32 i;

    if (flags & A_INIT) {
        memset(tmp, 0, 5 * sizeof(s16));
    } else {
        memcpy(tmp, state, 16 * sizeof(s16));
    }
    in -= 4;
    pitchAccumulator = (u16)tmp[4];
    memcpy(in, tmp, 4 * sizeof(s16));

    do {
        for (i = 0; i < 8; i++) {
            const s16 *tbl = sResampleTable[(pitchAccumulator * 64) >> 16];
            s32 sample = ((in[0] * tbl[0] + 0x4000) >> 15) +
                         ((in[1] * tbl[1] + 0x4000) >> 15) +
                         ((in[2] * tbl[2] + 0x4000) >> 15) +
                         ((in[3] * tbl[3] + 0x4000) >> 15);
            *out++ = mixerClamp16(sample);

            pitchAccumulator += (u32)pitch << 1;
            in += pitchAccumulator >> 16;
            pitchAccumulator %= 0x10000;
        }
        nbytes -= 8 * (s32)sizeof(s16);
    } while (nbytes > 0);

    state[4] = (s16)pitchAccumulator;
    memcpy(state, in, 4 * sizeof(s16));
    i = (s32)((in - inInitial + 4) & 7);
    in -= i;
    if (i != 0) i = -8 - i;
    state[5] = (s16)i;
    memcpy(state + 8, in, 8 * sizeof(s16));
}

/* ------------------------------------------------------------------------ */
/* Interleave / DMEM mix                                                    */
/* ------------------------------------------------------------------------ */

void aInterleaveImpl(u16 l, u16 r)
{
    const s16 *lp = DMEM_S16(l);
    const s16 *rp = DMEM_S16(r);
    s16 *d = DMEM_S16(sCtx.out);
    u32 n = sCtx.count >> 1; /* mono sample count */
    u32 i;

    for (i = 0; i < n; i++) {
        *d++ = *lp++;
        *d++ = *rp++;
    }
}

void aMixImpl(u32 flags, u16 gain, u16 in, u16 out)
{
    const s16 *inp = DMEM_S16(in);
    s16 *outp = DMEM_S16(out);
    u32 n = sCtx.count >> 1;
    u32 i;
    (void)flags;

    for (i = 0; i < n; i++) {
        s32 sample = ((s32)*outp * 0x7fff + (s32)*inp++ * (s16)gain + 0x4000) >> 15;
        *outp++ = mixerClamp16(sample);
    }
}

/* ------------------------------------------------------------------------ */
/* Volume / envelope mixer                                                  */
/* ------------------------------------------------------------------------ */

static struct {
    s16 volCur[2];
    s16 volTgt[2];
    s32 volRate[2];
    s16 dryamt, wetamt;
} sVol;

void aSetVolumeImpl(u32 flags, u16 v, u16 t, u16 r)
{
    if (flags & A_AUX) {
        sVol.dryamt = (s16)v;
        sVol.wetamt = (s16)r;
        /* D202/M-65 diag (temporary): GE_NOWET=1 stops any signal entering
         * the reverb send. The reverb delay lines are the only audio state
         * that survives across frames in DRAM, and aPoleFilterImpl -- the
         * damping in that feedback path -- is an unimplemented no-op here
         * (D199). If muting the send silences the stuck drone, the drone is
         * an undamped reverb feedback loop, not a stuck voice.
         * Remove once root-caused. */
        if (getenv("GE_NOWET")) {
            sVol.wetamt = 0;
        }
    } else if (flags & A_VOL) {
        int ch = (flags & A_LEFT) ? 0 : 1;
        sVol.volCur[ch] = (s16)v;
    } else {
        int ch = (flags & A_LEFT) ? 0 : 1;
        sVol.volTgt[ch] = (s16)v;
        sVol.volRate[ch] = ((u32)t << 16) | (u16)r;
    }
}

/* D202/M-67 diag (temporary): GE_VOICEDUMP=1 writes each voice's resampled
 * mono stream (the aEnvMixer input, i.e. post-resample/pre-envelope) to
 * voicedump.raw as records of [u32 stateAddr][u32 nSamples][u64 us]
 * [s16 x nSamples], where us is sysGetMicroseconds() at mix time so records
 * can be correlated with the timestamped [WIRE] (load.c) and [AUDIOTRACE]
 * (snd.c) lines. Offline, each record is matched against all 261 ROM SFX
 * decodes (pitch-resampled per keymap) to prove exactly which sample data a
 * voice played, with no music/reverb masking. Remove once D202 closes. */
static FILE *s_voiceDumpFile = NULL;

void aEnvMixerImpl(u32 flags, u32 stateAddr)
{
    struct {
        s32 t[2];
        s32 rate[2];
        s16 tgt[2];
        s16 voldry;
        s16 volwet;
    } *saved = (void *)osPhysicalToVirtual(stateAddr);

    MTRACE("[ENVMIX] flags=%u state=%p in=%u out=%u dryR=%u wetL=%u wetR=%u count=%u\n",
           flags, (void *)saved, sCtx.in, sCtx.out, sCtx.dryR, sCtx.wetL, sCtx.wetR, sCtx.count);
    const s16 *in = DMEM_S16(sCtx.in);

    if (getenv("GE_VOICEDUMP")) {
        if (!s_voiceDumpFile)
            s_voiceDumpFile = fopen("voicedump.raw", "wb");
        if (s_voiceDumpFile) {
            u32 hdr[2] = { stateAddr, (u32)(sCtx.count >> 1) };
            u64 us = sysGetMicroseconds();
            fwrite(hdr, 4, 2, s_voiceDumpFile);
            fwrite(&us, 8, 1, s_voiceDumpFile);
            fwrite(in, sizeof(s16), sCtx.count >> 1, s_voiceDumpFile);
        }
    }

    s16 *dry[2] = { DMEM_S16(sCtx.out), DMEM_S16(sCtx.dryR) };
    s16 *wet[2] = { DMEM_S16(sCtx.wetL), DMEM_S16(sCtx.wetR) };
    u32 nsamples = sCtx.count >> 1;

    s32 t[2], tgt[2], rate[2];
    s16 voldry, volwet;
    u32 i;
    int j;

    if (flags & A_INIT) {
        for (j = 0; j < 2; j++) {
            t[j] = sVol.volCur[j] << 16;
            rate[j] = sVol.volRate[j] >> 3;
            tgt[j] = sVol.volTgt[j] << 16;
        }
        voldry = sVol.dryamt;
        volwet = sVol.wetamt;
    } else {
        for (j = 0; j < 2; j++) {
            t[j] = saved->t[j];
            rate[j] = saved->rate[j];
            tgt[j] = saved->tgt[j] << 16;
        }
        voldry = saved->voldry;
        volwet = saved->volwet;
    }

    for (i = 0; i < nsamples; i++) {
        s16 gain[4];
        s16 vol[2];
        s16 insamp = in[i];

        for (j = 0; j < 2; j++) {
            t[j] += rate[j];
            if ((rate[j] <= 0 && t[j] <= tgt[j]) || (rate[j] > 0 && t[j] >= tgt[j])) {
                t[j] = tgt[j];
                rate[j] = 0;
            }
            vol[j] = (s16)(t[j] >> 16);
        }

        gain[0] = mixerClamp16(((s32)vol[0] * voldry + 0x4000) >> 15);
        gain[1] = mixerClamp16(((s32)vol[1] * voldry + 0x4000) >> 15);
        gain[2] = mixerClamp16(((s32)vol[0] * volwet + 0x4000) >> 15);
        gain[3] = mixerClamp16(((s32)vol[1] * volwet + 0x4000) >> 15);

        dry[0][i] = mixerClamp16(dry[0][i] + (((s32)insamp * gain[0]) >> 15));
        dry[1][i] = mixerClamp16(dry[1][i] + (((s32)insamp * gain[1]) >> 15));
        wet[0][i] = mixerClamp16(wet[0][i] + (((s32)insamp * gain[2]) >> 15));
        wet[1][i] = mixerClamp16(wet[1][i] + (((s32)insamp * gain[3]) >> 15));
    }

    for (j = 0; j < 2; j++) {
        saved->t[j] = t[j];
        saved->rate[j] = rate[j];
        saved->tgt[j] = (s16)(tgt[j] >> 16);
    }
    saved->voldry = voldry;
    saved->volwet = volwet;
}

/* ------------------------------------------------------------------------ */
/* Pole filter (reverb/chorus lowpass)                                      */
/* ------------------------------------------------------------------------ */

void aPoleFilterImpl(u32 flags, u16 gain, u32 stateAddr)
{
    /* PC port (D199): identity passthrough — reverb/chorus tone-shaping
     * (CUSTOM_FX_PARAMS_N) is not implemented. Affects reverb/chorus
     * character only, not silence/garbling (docs/dev/AUDIO-PLAN.md risk 1).
     * DMEM in-place buffer (sCtx set by the caller's aSetBuffer(buf,buf,..))
     * is left untouched, which is the correct no-op for this filter shape. */
    (void)flags; (void)gain; (void)stateAddr;
}

/* ------------------------------------------------------------------------ */

void mixerInit(void)
{
    memset(sDmem, 0, sizeof(sDmem));
    memset(sAdpcmTable, 0, sizeof(sAdpcmTable));
    memset(&sCtx, 0, sizeof(sCtx));
    memset(&sVol, 0, sizeof(sVol));
    sAdpcmLoopState = NULL;
}

void mixerDestroy(void)
{
}
