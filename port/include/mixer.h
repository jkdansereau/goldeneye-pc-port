#ifndef PORT_MIXER_H
#define PORT_MIXER_H

/*
 * Audio mixing — software implementation of GE's RSP audio ucode opcodes.
 *
 * include/PR/abi.h's aXxx macros normally pack RSP command words into an
 * Acmd list for the audio ucode (aspMain, never run on PC — port/src/ucode.c)
 * to execute later. Under #ifdef PORT, abi.h includes this header instead,
 * which redefines every aXxx macro to call an Impl function here immediately
 * — same macro-swap trick as the Perfect Dark PC port (docs/dev/AUDIO-PLAN.md).
 *
 * GE uses the classic IDO libaudio ABI (verified against every call site in
 * src/libultra/audio and src/libultrare/audio/{env,reverb}.c — NOT PD's
 * differently-shaped "naudio New" ABI). Several opcodes (aADPCMdec,
 * aResample, aEnvMixer, aLoadBuffer, aSaveBuffer) take only a state/DRAM
 * pointer; their DMEM source/dest addresses and byte counts come from the
 * most recent aSetBuffer call(s), tracked here as persistent context —
 * see the field comments on MixerCtx in mixer.c.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

void mixerInit(void);
void mixerDestroy(void);

/* Software RSP-audio-ucode opcode implementations (port/src/mixer.c). */
void aSetBufferImpl(u32 flags, u16 i, u16 o, u16 c);
void aClearBufferImpl(u16 addr, u32 count);
void aLoadBufferImpl(u32 dramAddr);
void aSaveBufferImpl(u32 dramAddr);
void aDMEMMoveImpl(u16 in, u16 out, u32 count);
void aLoadADPCMImpl(u32 count, u32 dramAddr);
void aSetLoopImpl(u32 stateAddr);
void aADPCMdecImpl(u32 flags, u32 stateAddr);
void aResampleImpl(u32 flags, u16 pitch, u32 stateAddr);
void aInterleaveImpl(u16 l, u16 r);
void aMixImpl(u32 flags, u16 gain, u16 in, u16 out);
void aSetVolumeImpl(u32 flags, u16 v, u16 t, u16 r);
void aEnvMixerImpl(u32 flags, u32 stateAddr);
void aPoleFilterImpl(u32 flags, u16 gain, u32 stateAddr);
void aSegmentImpl(u32 seg, u32 base);

#undef aSetBuffer
#undef aClearBuffer
#undef aLoadBuffer
#undef aSaveBuffer
#undef aDMEMMove
#undef aLoadADPCM
#undef aSetLoop
#undef aADPCMdec
#undef aResample
#undef aInterleave
#undef aMix
#undef aSetVolume
#undef aEnvMixer
#undef aPoleFilter
#undef aSegment

#define aSetBuffer(pkt, f, i, o, c)   aSetBufferImpl((u32)(f), (u16)(i), (u16)(o), (u16)(c))
#define aClearBuffer(pkt, d, c)       aClearBufferImpl((u16)(d), (u32)(c))
#define aLoadBuffer(pkt, s)           aLoadBufferImpl((u32)(s))
#define aSaveBuffer(pkt, s)           aSaveBufferImpl((u32)(s))
#define aDMEMMove(pkt, i, o, c)       aDMEMMoveImpl((u16)(i), (u16)(o), (u32)(c))
#define aLoadADPCM(pkt, c, d)         aLoadADPCMImpl((u32)(c), (u32)(d))
#define aSetLoop(pkt, a)              aSetLoopImpl((u32)(a))
#define aADPCMdec(pkt, f, s)          aADPCMdecImpl((u32)(f), (u32)(s))
#define aResample(pkt, f, p, s)       aResampleImpl((u32)(f), (u16)(p), (u32)(s))
#define aInterleave(pkt, l, r)        aInterleaveImpl((u16)(l), (u16)(r))
#define aMix(pkt, f, g, i, o)         aMixImpl((u32)(f), (u16)(g), (u16)(i), (u16)(o))
#define aSetVolume(pkt, f, v, t, r)   aSetVolumeImpl((u32)(f), (u16)(v), (u16)(t), (u16)(r))
#define aEnvMixer(pkt, f, s)          aEnvMixerImpl((u32)(f), (u32)(s))
#define aPoleFilter(pkt, f, g, s)     aPoleFilterImpl((u32)(f), (u16)(g), (u32)(s))
#define aSegment(pkt, s, b)           aSegmentImpl((u32)(s), (u32)(b))

#ifdef __cplusplus
}
#endif

#endif /* PORT_MIXER_H */
