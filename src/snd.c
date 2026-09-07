#include <ultra64.h>
#include <PR/libaudio.h>
#include <os_extension.h>
#include "music.h"
#include "snd.h"
#ifdef PORT
#include <stdio.h>
#include <stdlib.h>
#include "audiotrace.h"   /* D202/M-70: serialized trace writer */
#endif
//likely named gslibaudio.c from xbla
/**
 * EU .data, offset from start of data_seg : 0x3620
*/

/**
 * @file snd.c
 * This file contains code to deal with snd.
 */

#define DEFAULT_SETUP_PITCH_SHIFT (-0x1770)

#ifdef PORT
/* D202/M-66 (PC port only): ownerless infinite-loop SFX voices (sound 203,
 * METAL_SLIDE_CLOSE_SFX, is the one in the wild) can never be stopped by the
 * game -- played fire-and-forget, looped voices skip the decay->stop chain,
 * and stock preemption never touches flag-0x12 voices -- so on N64 they ring
 * until level exit. On PC we let them play for D202_EXPIRE_DELAY_US, then fade
 * over D202_EXPIRE_FADE_US (the stock STOP ramp uses the envelope releaseTime,
 * a few ms here, which would hard-cut the loop). ALMicroTime = microseconds.
 * See docs/dev/findings.md D202, M-66. */
#define D202_EXPIRE_DELAY_US 2000000
#define D202_EXPIRE_FADE_US  500000
#endif

/**
 * Based on \n64devkit\ultra\usr\src\pr\libsrc\libultra\audio\sndp.h
 * ALSndpEvent
 */
typedef union ALSndpEvent_u {

    struct {
        // offset 0
        u16             type;
        // offset 4
        ALSoundState    *state;
    } common;

    struct {
        u16             type;
        ALSoundState    *state;
        s32             vol;
    } vol;

    struct {
        u16             type;
        ALSoundState    *state;
        f32             pitch;
    } pitch;

    struct {
        u16             type;
        ALSoundState    *state;
        s32           pan32;
    } pan32;

    struct {
        u16             type;
        ALSoundState    *state;
        s32             mix32;
    } fx32;

    struct {
        u16 type;
        ALSoundState *state;
        s32 soundIndex;
        struct ALBankAlt_s *soundBank;
    } playSfx;

    struct {
        s16 type;
        ALSoundState *state;
        s32 val8;
        s32 valc;
    } unks32;

    struct {
        s32 unk0;
        s32 unk4;
        s32 unk8;
        s32 unkC;
    } align_size;

} ALSndpEvent;

union ALSndpSmallEvent_u {
    struct {
        u16 type;
        ALSoundState *state;
    } msg;

    union {
        s32 unk0;
        s32 unk4;
    } align_size;
};

// TODO: is this struct really the answer?
// 800243E4
struct D_800243E4_s {
    // address 800243E4 and 800243E8
    ALLink node;
    // address 800243EC
    struct ALSoundState_s *g_sndPlayerSoundStatePtr;
};

s32 g_sndUnused800243E0 = 0;

// // TODO: is this struct really the answer?
struct D_800243E4_s D_800243E4 = { {NULL, NULL}, NULL};

ALSndPlayer *g_sndPlayerPtr = &g_sndPlayer;

/**
 * Current number of allocated voices, via alSynAllocVoice
 */
s16 g_sndAllocatedVoicesCount = 0;

/**
 * Boot flag. If set, sound is disabled.
 */
s8 g_sndBootswitchSound = 0;

/**
 * Used in level load/setup, sound effect slot volume will be scaled by this amount.
 */
f32 g_sndSfxVolumeScale = 1.0;

// forward declarations

ALMicroTime sndPlayerVoiceHandler(void *node);
void sndHandleEvent(ALSndPlayer *sndp, ALSndpEvent *event);
void sndDisposeSound(ALSoundState *state);
void sndCreatePitchEvent(ALSoundState *state);
void sndRemoveEvents(ALEventQueue *evtq, ALSoundState *state, u16 eventType);
s32 sndCountAllocList(s16 *allocListCount, s16 *freeListCount);
ALSoundState *sndSetupSound(struct ALBankAlt_s *soundBank, ALSound* sound);
void sndUnlinkClearSound(ALSoundState *state);
void sndSetPriority(ALSoundState *state, u8 priority);
u8 sndGetPlayingState(ALSoundState *state);
void sndDeactivateAllSfxByFlag(u8 flag);
void sndDeactivateAllSfxByFlag_1(void);
void sndDeactivateAllSfxByFlag_11(void);
void sndDeactivateAllSfxByFlag_3(void);
u16 sndGetSfxSlotFirstNaturalVolume(void);
void sndApplyVolumeAllSfxSlot(u16 arg0);
void sndSetScalerApplyVolumeAllSfxSlot(f32 arg0);
u16 sndGetSfxSlotNaturalVolume(u8 arg0);
void sndSetSfxSlotVolume(u8 arg0, u16 arg1);

// end forward declarations

/**
 * 8720    70007B20
 *
 * Mostly identical to n64devkit\ultra\usr\src\pr\libsrc\libultra\audio\sndplayer.c
 * method alSndpNew.
 */
void sndNewPlayerInit(ALSeqpSfxConfig *sfxSeqpConfig)
{
    u8 *ptr;
    struct ALSoundState_s *sState;
    ALEvent evt;
    u32 i;

    /*
     * Init member variables
     */
    g_sndPlayerPtr->maxSounds = sfxSeqpConfig->maybeMaxSounds;
    g_sndPlayerPtr->target = 0;
    g_sndPlayerPtr->frameTime = AL_USEC_PER_FRAME_30FPS;
    sState = alHeapAlloc(sfxSeqpConfig->heap, 1, sfxSeqpConfig->maybeSndStateCount * sizeof(struct ALSoundState_s));
    g_sndPlayerPtr->sndState = sState;

    /*
     * init the event queue
     */
    ptr = alHeapAlloc(sfxSeqpConfig->heap, 1, sfxSeqpConfig->maxEvents * sizeof(ALEventListItem));
    alEvtqNew(&g_sndPlayerPtr->evtq, (ALEventListItem *)ptr, sfxSeqpConfig->maxEvents);

    D_800243E4.g_sndPlayerSoundStatePtr = g_sndPlayerPtr->sndState;

    for(i = 1; i < sfxSeqpConfig->maybeSndStateCount; i++)
    {
        // The compiler says this reassignment matters ...
        sState = (struct ALSoundState_s*)g_sndPlayerPtr->sndState;

        // this works because `ALLink node` is at offset zero.
        alLink((ALLink*)(&sState[i]), (ALLink*)(&sState[i]-1));
    }

    g_sndSfxSlotVolume = alHeapAlloc(sfxSeqpConfig->heap, sizeof(s16), SFX_SLOT_COUNT);
    g_sndSfxSlotNaturalVolume = alHeapAlloc(sfxSeqpConfig->heap, sizeof(s16), SFX_SLOT_COUNT);

    for(i = 0; i < SFX_SLOT_COUNT; i++)
    {
        g_sndSfxSlotNaturalVolume[i] = \
            g_sndSfxSlotVolume[i] = (s16)0x7FFF;
    }

    /*
     * add ourselves to the driver
     */
    g_sndPlayerPtr->drvr = &alGlobals->drvr;
    g_sndPlayerPtr->node.next = NULL;
    g_sndPlayerPtr->node.handler = &sndPlayerVoiceHandler;
    g_sndPlayerPtr->node.clientData = g_sndPlayerPtr;
    alSynAddPlayer(g_sndPlayerPtr->drvr, &g_sndPlayerPtr->node);

    /*
     * Start responding to API events
     */
    evt.type = AL_SNDP_API_EVT;
    alEvtqPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&evt, g_sndPlayerPtr->frameTime);
    g_sndPlayerPtr->nextDelta = alEvtqNextEvent(&g_sndPlayerPtr->evtq, &g_sndPlayerPtr->nextEvent);
}

/**
 * 89DC    70007DDC
 *
 * Almost identical to \n64devkit\ultra\usr\src\pr\libsrc\libultra\audio\sndplayer.c
 * method ALMicroTime _sndpVoiceHandler(void *node).
 */
ALMicroTime sndPlayerVoiceHandler(void *node)
{
    ALSndPlayer *sndp = (ALSndPlayer *) node;
    ALSndpEvent evt;

    do
    {
        switch (sndp->nextEvent.type)
        {
            case (AL_SNDP_API_EVT):
                evt.common.type = (s16)AL_SNDP_API_EVT;
                alEvtqPostEvent(&sndp->evtq, (ALEvent *)&evt, sndp->frameTime);
                break;

            default:
                sndHandleEvent(sndp, (ALSndpEvent *)&sndp->nextEvent);
                break;
        }

        sndp->nextDelta = alEvtqNextEvent(&sndp->evtq, &sndp->nextEvent);

    } while (sndp->nextDelta == 0);

    sndp->curTime += sndp->nextDelta;

    return sndp->nextDelta;
}


void sndHandleEvent(ALSndPlayer *sndp, ALSndpEvent *event) {
    ALVoiceConfig config;
    ALVoice *voice;  // dead but load-bearing. Do not remove.
    s32 delta;
    s32 limitReached;
    ALSndpEvent spAC;
    ALSndpEvent nextStateEvent;
    ALSound *sound;
    ALKeyMap *keyMap;
    s32 volume;
    s32 fxMix;
    s32 isEventForSingleSound;
    ALPan pan;
    s32 lastInSequence;
    s32 isVoiceAllocated;
    ALSoundState *soundState;
    ALSoundState *nextState;

    lastInSequence = TRUE;
    isVoiceAllocated = FALSE;
    nextState = NULL;

    do {
        if (nextState != NULL) {
            // NB: soundState is uninitialised on the first pass — original bug, preserved.
            nextStateEvent.common.state = soundState;
            nextStateEvent.common.type = event->common.type;
            nextStateEvent.vol.vol = event->vol.vol;
            event = &nextStateEvent;
        }

        soundState = event->common.state;
        sound = soundState->sound;
#ifdef PORT
        /* D202/M-67 diag (temporary): log every event the player processes,
         * so each VOICE- can be attributed to the exact killing event.
         * Remove once root-caused. */
        if (getenv("GE_AUDIOTRACE")) {
            extern uint64_t sysGetMicroseconds(void);
            geTracePrintf("audiotrace.log", "[EVT] t=%llu type=%d state=%p\n",
                    (unsigned long long)sysGetMicroseconds(),
                    (int)event->common.type, (void *)soundState);
        }
#endif

        if (sound == NULL) {
            s16 numFree, numAlloc;
            sndCountAllocList(&numFree, &numAlloc);
            return;
        }

        keyMap = sound->keyMap;
        nextState = (ALSoundState *) soundState->link.next;

        switch (event->common.type) {
            case AL_SNDP_PLAY_EVT:
                if (1) {} // fake to get s5/s6 swapped
                if (soundState->playingState != SOUND_STATE_INIT && soundState->playingState != SOUND_STATE_WAIT_VOICE) {
                    return;
                }

                config.fxBus = 0;
                config.priority = soundState->priority;
                config.unityPitch = 0;

                limitReached = sndp->maxSounds <= g_sndAllocatedVoicesCount;

                if (!limitReached || (soundState->unk3e & SOUND_FLAG_RETRIGGER)) {
                    // for retriggered sounds, ignore the limit
                    isVoiceAllocated = alSynAllocVoice(sndp->drvr, &soundState->voice, &config);
                }

                if (!isVoiceAllocated) {
                    // No free voices available, wait for another sound to stop.
                    if ((soundState->unk3e & (SOUND_FLAG_RETRIGGER | SOUND_FLAG_LOOPED)) || soundState->unk38 > 0) {
                        // Retry on the next frame.
                        // For looped and retriggered sounds, keep retrying on each frame.
                        soundState->playingState = SOUND_STATE_WAIT_VOICE;
                        soundState->unk38--;
                        alEvtqPostEvent(&sndp->evtq, (ALEvent *) event, DELTA_33_MS);
                    } else {
                        // Not a looped or retriggered sound, and all retries have been exhausted.
                        if (limitReached) {
                            // Check if we can preempt a lower-priority sound.
                            ALSoundState *iterState = (ALSoundState *) D_800243E4.node.prev;

                            do {
                                /* D202/M-65 (PORT, experimental): the stock scan refuses to
                                 * preempt any looped or retriggering voice (0x12). That is
                                 * right for a voice somebody still owns -- its owner will
                                 * sndDeactivate it. But a SOUND_FLAG_LOOPED voice whose
                                 * state->state is NULL was started with pendingState == NULL
                                 * (doorPlayCloseSound0/1 do exactly this for
                                 * METAL_SLIDE_CLOSE_SFX, whose envelope decayTime is -1), so
                                 * no owner exists, no STOP_EVT is ever posted for it, and it
                                 * holds one of the 8 voices until level exit. Measured: 7 of
                                 * 8 voices held by that one sound after 115 s of Bunker
                                 * attract. Allow reclaiming ONLY that provably-unstoppable
                                 * case; owned loops are still protected exactly as before.
                                 * Companion (M-66, option C): the AL_SNDP_PORT_EXPIRE_EVT
                                 * post in PLAY_EVT fades such voices out after a bounded
                                 * time; this guard only reclaims their SLOTS under pool
                                 * pressure so new sounds can still start. */
                                s32 ownerlessLoop = (iterState->unk3e & SOUND_FLAG_LOOPED) &&
                                                    !(iterState->unk3e & SOUND_FLAG_RETRIGGER) &&
                                                    iterState->state == NULL;
                                if ((!(iterState->unk3e & 0x12) || ownerlessLoop) &&
                                    (iterState->unk3e & 0x4) &&
                                    iterState->playingState != SOUND_STATE_PREEMPT) {
                                    // Found a lower-priority sound; it can be preempted
                                    ALSndpEvent interruptEvent;

                                    interruptEvent.common.type = AL_SNDP_END_EVT;
                                    interruptEvent.common.state = iterState;
                                    iterState->playingState = SOUND_STATE_PREEMPT;
                                    limitReached = FALSE;
                                    alEvtqPostEvent(&sndp->evtq, (ALEvent *) &interruptEvent, DELTA_1_MS);
                                    alSynSetVol(sndp->drvr, &iterState->voice, (soundState->playingState == 1) * 0, DELTA_1_MS); // FAKE
                                }
                                iterState = (ALSoundState *) iterState->link.prev;
                            } while (limitReached && iterState != NULL);

                            if (!limitReached) {
                                // Retry the sound that was preempted.
                                soundState->unk38 = 2;
                                alEvtqPostEvent(&sndp->evtq, (ALEvent *) event, DELTA_1_MS + 1);
                            } else {
                                // No lower-priority sound to preempt, so stop the sound.
                                sndDisposeSound(soundState);
                            }
                        } else {
                            // It seems the developers made a mistake with the logic here.
                            // Should we stop the sound immediately if the maximum number of sounds hasn't been reached?
                            // Perhaps it would be better to look for a sound to preempt, just like when the limit is
                            // reached. It's strange that we only check for sounds to preempt when the limit is reached,
                            // but not when it hasn't been.
                            sndDisposeSound(soundState);
                        }
                    }
                    return;
                }

                // Set volume
                soundState->unk3e |= SOUND_FLAG_PLAYING;
                alSynStartVoice(sndp->drvr, &soundState->voice, sound->wavetable);
                soundState->playingState = SOUND_STATE_PLAYING;
                g_sndAllocatedVoicesCount++;
#ifdef PORT
                /* D202/M-65 diag (temporary): pair every voice acquire with
                 * its release to find which sounds hold the 8 voices for
                 * good. Remove once root-caused. */
                if (getenv("GE_AUDIOTRACE")) {
                    geTracePrintf("audiotrace.log", "[VOICE+] sound=%p state=%p flags=%d count=%d\n",
                            (void *)sound, (void *)soundState, (int)soundState->unk3e,
                            (int)g_sndAllocatedVoicesCount);
                }
#endif
#ifdef PORT
                /* D202/M-66 (PORT deviation, option C): schedule the fade-out
                 * for a provably ownerless infinite-loop voice. Predicate:
                 * LOOPED (decayTime == -1) and FINAL_IN_SEQUENCE (single sound,
                 * so the handler's sequence walk stops at this state), not
                 * RETRIGGER (those are managed by PLAY_SFX/DEACTIVATE chains),
                 * no owner (state->state == NULL: doorPlayCloseSound0/1 plays
                 * with a NULL owner and nothing ever sndDeactivates it), and the
                 * wave itself carries an ADPCM loop with count == -1 (finite
                 * loops self-terminate in load.c and must not be cut short).
                 * Stale-event safety: any dispose of this state (preemption,
                 * natural end, deactivate) runs sndDisposeSound, which removes
                 * ALL pending events for it (sndRemoveEvents ... 0xffff), and a
                 * rebind always disposes first -- so this event can never fire
                 * against a new binding. */
                if ((soundState->unk3e & (SOUND_FLAG_LOOPED | SOUND_FLAG_RETRIGGER | SOUND_FLAG_FINAL_IN_SEQUENCE))
                        == (SOUND_FLAG_LOOPED | SOUND_FLAG_FINAL_IN_SEQUENCE) &&
                    soundState->state == NULL &&
                    sound->wavetable != NULL && sound->wavetable->type == AL_ADPCM_WAVE &&
                    sound->wavetable->waveInfo.adpcmWave.loop != NULL &&
                    sound->wavetable->waveInfo.adpcmWave.loop->count == -1) {
                    ALSndpEvent expireEvt;
                    expireEvt.common.type = AL_SNDP_PORT_EXPIRE_EVT;
                    expireEvt.common.state = soundState;
                    alEvtqPostEvent(&sndp->evtq, (ALEvent *) &expireEvt, D202_EXPIRE_DELAY_US);
                    if (getenv("GE_AUDIOTRACE")) {
                        geTracePrintf("audiotrace.log", "[EXPIRE] sound=%p state=%p delay=%dus fade=%dus (ownerless infinite loop; D202/M-66)\n",
                                (void *)sound, (void *)soundState, (int)D202_EXPIRE_DELAY_US, (int)D202_EXPIRE_FADE_US);
                    }
                }
#endif

                delta = sound->envelope->attackTime / soundState->pitch_2c / soundState->pitch_28;
                volume =
                    MAX(0, g_sndSfxSlotVolume[SOUND_PARAM_GROUP(keyMap)] *
                                   (sound->envelope->attackVolume * soundState->vol * sound->sampleVolume / 16129) /
                                   AL_SNDP_GROUP_VOLUME_MAX - 1);
                alSynSetVol(sndp->drvr, &soundState->voice, 0, 0);
                alSynSetVol(sndp->drvr, &soundState->voice, volume, delta);

                // Set pan
                pan = MIN(MAX((soundState->pan + sound->samplePan - AL_PAN_CENTER), AL_PAN_LEFT), AL_PAN_RIGHT);
                alSynSetPan(sndp->drvr, &soundState->voice, pan);

                // Set pitch
                alSynSetPitch(sndp->drvr, &soundState->voice, soundState->pitch_2c * soundState->pitch_28);

                // Set FX mix
                //!@bug: SOUND_PARAM_FXMIX is allocated only four bits, so it needs to be multiplied by 8
                // to scale it to a range of 0 to 127.
                // However, it's unclear why soundState->fxmix also needs to be multiplied by 8.
                // The same issue appears in the AL_SNDP_FX_EVT handler.
                fxMix = (soundState->fxMix + SOUND_PARAM_FXMIX(keyMap)) * 8;
                fxMix = MIN(127, MAX(0, fxMix));
                alSynSetFXMix(sndp->drvr, &soundState->voice, fxMix);

                // Queue the decay event
                spAC.common.type = AL_SNDP_DECAY_EVT;
                spAC.common.state = soundState;
                alEvtqPostEvent(&sndp->evtq, (ALEvent *) &spAC,
                                sound->envelope->attackTime / soundState->pitch_2c / soundState->pitch_28);
                break;
            case AL_SNDP_STOP_EVT:
            case AL_SNDP_DEACTIVATE_EVT:
            case AL_SNDP_UNKNOWN_12_EVT:
                // If any sound in the composite sound is in the release phase, ignore this event for all other sounds
                // in the sequence, because they haven't started yet.
                // However, if the other sound is looped, process the event anyway.
                //
                // The purpose of checking for a looped sound seems unclear.
                // Does it imply that a composite sound can't contain looped simple sounds?
                // It seems logical, but the check may still be redundant.
                if (event->common.type != AL_SNDP_UNKNOWN_12_EVT || (soundState->unk3e & SOUND_FLAG_LOOPED)) {
                    switch (soundState->playingState) {
                        case SOUND_STATE_PLAYING:
                            sndRemoveEvents(&sndp->evtq, soundState, AL_SNDP_DECAY_EVT);
                            delta = sound->envelope->releaseTime / soundState->pitch_28 / soundState->pitch_2c;
#ifdef PORT
                            /* D202/M-67 diag (temporary): log the release ramp a
                             * STOP/DEACTIVATE computes; delta==0 means the voice
                             * is disposed immediately. Remove once root-caused. */
                            if (getenv("GE_AUDIOTRACE")) {
                                geTracePrintf("audiotrace.log", "[STOP-EVT] type=%d state=%p playingState=%d deltaUs=%d\n",
                                        (int)event->common.type, (void *)soundState,
                                        (int)soundState->playingState, (int)delta);
                            }
#endif
                            alSynSetVol(sndp->drvr, &soundState->voice, 0, delta);
                            if (delta != 0) {
                                spAC.common.type = AL_SNDP_END_EVT;
                                spAC.common.state = soundState;
                                alEvtqPostEvent(&sndp->evtq, (ALEvent *) &spAC, delta);
                                soundState->playingState = SOUND_STATE_STOPPING;
                            } else {
                                sndDisposeSound(soundState);
                            }
                            break;
                        case SOUND_STATE_WAIT_VOICE:
                        case SOUND_STATE_INIT:
                            sndDisposeSound(soundState);
                            break;
                    }
                    if (event->common.type == AL_SNDP_STOP_EVT) {
                        event->common.type = AL_SNDP_UNKNOWN_12_EVT;
                    }
                }
                break;
            case AL_SNDP_PAN_EVT:
                soundState->pan = event->pan32.pan32;
                if (soundState->playingState == SOUND_STATE_PLAYING) {
                    pan = MIN(MAX((soundState->pan + sound->samplePan - AL_PAN_CENTER), AL_PAN_LEFT), AL_PAN_RIGHT);
                    alSynSetPan(sndp->drvr, &soundState->voice, pan);
                }
                break;
            case AL_SNDP_PITCH_EVT:
                soundState->pitch_2c = event->pitch.pitch;
                if (soundState->playingState == SOUND_STATE_PLAYING) {
                    alSynSetPitch(sndp->drvr, &soundState->voice, soundState->pitch_2c * soundState->pitch_28);
                    if (soundState->unk3e & SOUND_FLAG_PITCH_SLIDE) {
                        sndCreatePitchEvent(soundState);
                    }
                }
                break;
            case AL_SNDP_FX_EVT:
                soundState->fxMix = event->fx32.mix32;
                if (soundState->playingState == SOUND_STATE_PLAYING) {
                    //!@bug: unnecessary multiplication by 8, as in AL_SNDP_PLAY_EVT.
                    // The same issue appears in the AL_SNDP_PLAY_EVT handler.
                    fxMix = (soundState->fxMix + SOUND_PARAM_FXMIX(keyMap)) * 8;
                    fxMix = MIN(127, MAX(0, fxMix));
                    alSynSetFXMix(sndp->drvr, &soundState->voice, fxMix);
                }
                break;
            case AL_SNDP_VOL_EVT:
                soundState->vol = event->vol.vol;
#ifdef PORT
                if (getenv("GE_AUDIOTRACE")) { /* D202 diag: distance-vol value per VOL event; remove with the other probes */
                    extern uint64_t sysGetMicroseconds(void);
                    geTracePrintf("audiotrace.log", "[VOL] t=%llu state=%p rawVol=%d playing=%d\n",
                            (unsigned long long)sysGetMicroseconds(), (void *)soundState,
                            (int)event->vol.vol, (int)soundState->playingState);
                }
#endif
                if (soundState->playingState == SOUND_STATE_PLAYING) {
                    volume = MAX(
                        0, g_sndSfxSlotVolume[SOUND_PARAM_GROUP(keyMap)] *
                                   (sound->envelope->decayVolume * soundState->vol * sound->sampleVolume / 16129) /
                                   AL_SNDP_GROUP_VOLUME_MAX -
                               1);
                    alSynSetVol(sndp->drvr, &soundState->voice, volume, 1000);
                }
                break;
            case AL_SNDP_RELEASE_EVT:
                if (soundState->playingState == SOUND_STATE_PLAYING) {
                    delta = sound->envelope->releaseTime / soundState->pitch_28 / soundState->pitch_2c;
                    volume = MAX(
                        0, g_sndSfxSlotVolume[SOUND_PARAM_GROUP(keyMap)] *
                                   (sound->envelope->decayVolume * soundState->vol * sound->sampleVolume / 16129) /
                                   AL_SNDP_GROUP_VOLUME_MAX -
                               1);
                    alSynSetVol(sndp->drvr, &soundState->voice, volume, delta);
                }
                break;
            case AL_SNDP_DECAY_EVT:
                /*
                 * The voice has theoretically reached its attack velocity,
                 * set up callback for release envelope - except for a looped sound
                 */
                if (!(soundState->unk3e & SOUND_FLAG_LOOPED)) {
                    volume = MAX(
                        0, g_sndSfxSlotVolume[SOUND_PARAM_GROUP(keyMap)] *
                                   (sound->envelope->decayVolume * soundState->vol * sound->sampleVolume / 16129) /
                                   AL_SNDP_GROUP_VOLUME_MAX -
                               1);
                    delta = sound->envelope->decayTime / soundState->pitch_28 / soundState->pitch_2c;
                    alSynSetVol(sndp->drvr, &soundState->voice, volume, delta);

                    spAC.common.type = AL_SNDP_STOP_EVT;
                    spAC.common.state = soundState;
                    alEvtqPostEvent(&sndp->evtq, (ALEvent *) &spAC, delta);

                    // Start applying the pitch slide only when the decay phase is reached.
                    if (soundState->unk3e & SOUND_FLAG_PITCH_SLIDE) {
                        sndCreatePitchEvent(soundState);
                    }
                }
                break;
            case AL_SNDP_END_EVT:
                sndDisposeSound(soundState);
                break;
            case AL_SNDP_PLAY_SFX_EVT:
                if (soundState->unk3e & SOUND_FLAG_RETRIGGER) {
                    sndPlaySfx(event->playSfx.soundBank, event->playSfx.soundIndex, soundState->state);
                }
                break;
#ifdef PORT
            case AL_SNDP_PORT_EXPIRE_EVT:
                /* D202/M-66 (PORT deviation, option C): fade out an ownerless
                 * infinite-loop SFX voice. Re-validate the full predicate at
                 * fire time: if the state was disposed in the interim this
                 * event was already removed (sndDisposeSound); if it was
                 * rebound, the new binding fails the predicate and we no-op.
                 * Mirrors the stock STOP_EVT ramp-to-zero + END_EVT pattern,
                 * with a fixed fade length (see D202_EXPIRE_FADE_US). */
                if (soundState->playingState == SOUND_STATE_PLAYING &&
                    soundState->state == NULL &&
                    (soundState->unk3e & (SOUND_FLAG_LOOPED | SOUND_FLAG_RETRIGGER | SOUND_FLAG_FINAL_IN_SEQUENCE))
                        == (SOUND_FLAG_LOOPED | SOUND_FLAG_FINAL_IN_SEQUENCE) &&
                    sound->wavetable != NULL && sound->wavetable->type == AL_ADPCM_WAVE &&
                    sound->wavetable->waveInfo.adpcmWave.loop != NULL &&
                    sound->wavetable->waveInfo.adpcmWave.loop->count == -1) {
                    alSynSetVol(sndp->drvr, &soundState->voice, 0, D202_EXPIRE_FADE_US);
                    spAC.common.type = AL_SNDP_END_EVT;
                    spAC.common.state = soundState;
                    alEvtqPostEvent(&sndp->evtq, (ALEvent *) &spAC, D202_EXPIRE_FADE_US);
                    soundState->playingState = SOUND_STATE_STOPPING;
                }
                break;
#endif
            default:
                break;
        }

        soundState = nextState;
        isEventForSingleSound = event->common.type & (AL_SNDP_PLAY_EVT | AL_SNDP_PITCH_EVT | AL_SNDP_DECAY_EVT |
                                                      AL_SNDP_END_EVT | AL_SNDP_PLAY_SFX_EVT
#ifdef PORT
                                                      | AL_SNDP_PORT_EXPIRE_EVT /* D202/M-66: never sequence-walk */
#endif
                                                     );

        if (soundState != NULL && !isEventForSingleSound) {
            lastInSequence = soundState->unk3e & SOUND_FLAG_FINAL_IN_SEQUENCE;
        }

    } while (!lastInSequence && soundState != NULL && !isEventForSingleSound);
}


/**
 * 9548    70008948
 */
void sndDisposeSound(ALSoundState *state)
{
    if (state->unk3e & 4)
    {
        alSynStopVoice(g_sndPlayerPtr->drvr, &state->voice);
        alSynFreeVoice(g_sndPlayerPtr->drvr, &state->voice);
    }

    sndUnlinkClearSound(state);
    sndRemoveEvents(&g_sndPlayerPtr->evtq, state, 0xffff);
}

/**
 * 95C4    700089C4
 */
void sndCreatePitchEvent(ALSoundState *state)
{
    ALSndpEvent evt;
    f32 pitch;

    pitch = (f32) (alCents2Ratio(state->sound->keyMap->detune) * (f32)state->pitch_2c);
    evt.pitch.state = state;
    evt.pitch.type = AL_SNDP_PITCH_EVT;

    // TODO: surely there's a better way to match target, especially since there's already a union type used with f32 for pitch.
    evt.unks32.val8 = *(s32*)&pitch;

    alEvtqPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&evt, DELTA_33_MS);
}

/**
 * 9630     70008A30
 * Based on (almost identical to) the method
 * static void _removeEvents(ALEventQueue *evtq, ALSoundState *state)
 * from n64devkit\ultra\usr\src\pr\libsrc\libultra\audio\sndplayer.c
 */
void sndRemoveEvents(ALEventQueue *evtq, ALSoundState *state, u16 eventType)
{
    ALLink              *thisNode;
    ALLink              *nextNode;
    ALEventListItem     *thisItem;
    ALEventListItem     *nextItem;
    ALSndpEvent         *thisEvent;
    OSIntMask           mask;

    mask = osSetIntMask(OS_IM_NONE);

    thisNode = evtq->allocList.next;

    while(thisNode != NULL)
    {
	    nextNode = thisNode->next;
        thisItem = (ALEventListItem *)thisNode;
        nextItem = (ALEventListItem *)nextNode;
        thisEvent = (ALSndpEvent *)&thisItem->evt;

        if (thisEvent->common.state == state && (((u16)thisItem->evt.type & (u16)eventType) != 0))
        {
            if (nextItem != NULL)
            {
                nextItem->delta += thisItem->delta;
            }

            alUnlink(thisNode);
            alLink(thisNode, &evtq->freeList);
        }

	    thisNode = nextNode;
    }

    osSetIntMask(mask);
}

/**
 * 96F0     70008AF0
 * Has similarities to
 * void alEvtqPrintEvtQueue(ALEventQueue *evtq)
 * from n64devkit\ultra\usr\src\pr\libsrc\libultra\audio\event.c
 *
 * @param allocListCount Out param. Will contain the number of (next) nodes in the D_800243E4 allocList.
 * @param freeListCount Out param. Will contain the number of (next) nodes in the D_800243E4 freeList.
 * @return Number of (prev) nodes in the D_800243E4 freeList.
 */
s32 sndCountAllocList(s16 *allocListCount, s16 *freeListCount)
{
    u16 counter1;
    u16 counter2;
    u16 returnCounter;

    ALEventQueue *evtq = (ALEventQueue *)&D_800243E4;

    ALLink *freeListNodeForward = evtq->freeList.next;
    ALLink *allocListNodeForward = evtq->allocList.next;
    ALLink *freeListNodeBackward = evtq->freeList.prev;

    for (counter1 = 0; freeListNodeForward != NULL; freeListNodeForward = freeListNodeForward->next)
    {
         counter1++;
    }

    for (counter2 = 0; allocListNodeForward != NULL; allocListNodeForward = allocListNodeForward->next)
    {
         counter2++;
    }

    for (returnCounter = 0; freeListNodeBackward != NULL; freeListNodeBackward = freeListNodeBackward->prev)
    {
         returnCounter++;
    }

    *allocListCount = (s16) counter2;
    *freeListCount = (s16) counter1;

    return returnCounter;
}

/**
 * 9770    70008B70
 * initializes soundstate to sound based on global g_sndPlayerSoundStatePtr.
 *     accepts: A0=sound data offset?, A1=sample address?
 *
 * @param soundBank unused.
 * @param sound sound to use.
 */
ALSoundState *sndSetupSound(struct ALBankAlt_s *soundBank, ALSound* sound)
{
    s32 decayTimeFlag;
    ALKeyMap *keymap = sound->keyMap;
    ALSoundState *state = (ALSoundState *)D_800243E4.g_sndPlayerSoundStatePtr;
    OSIntMask mask;

    if (state != NULL)
    {
        mask = osSetIntMask(OS_IM_NONE);

        D_800243E4.g_sndPlayerSoundStatePtr = (void *)state->link.next;
        alUnlink(&state->link);

        if (D_800243E4.node.next != NULL)
        {
            state->link.next = (void *)D_800243E4.node.next;
            state->link.prev = NULL;
            D_800243E4.node.next->prev = (void *)state; // what?
            D_800243E4.node.next = (void *)state;
        }
        else
        {
            state->link.prev = NULL;
            state->link.next = NULL;
            D_800243E4.node.next = (void *)state;
            D_800243E4.node.prev = (void *)state;
        }

        osSetIntMask(mask);

        decayTimeFlag = (sound->envelope->decayTime == -1);
        state->priority = decayTimeFlag + 0x40;

        state->playingState = AL_UNKOWN_5;
        state->unk38 = 2;
        state->sound = sound;
        state->pitch_2c = 1.0f;
        state->unk3e = (keymap->keyMax & (u8)0xf0);
        state->state = NULL;

        if ((state->unk3e & 0x20) != 0)
        {
            state->pitch_28 = alCents2Ratio(((keymap->keyBase * 100) + DEFAULT_SETUP_PITCH_SHIFT));
        }
        else
        {
            state->pitch_28 = alCents2Ratio((((keymap->keyBase * 100) + keymap->detune) + DEFAULT_SETUP_PITCH_SHIFT));
        }

        if (decayTimeFlag)
        {
            state->unk3e |= 2;
        }

        state->fxMix = (u8)AL_DEFAULT_FXMIX;
        state->pan = (u8)AL_PAN_CENTER;
        state->vol = (u16)0x7fff;
    }

    return state;
}


/**
 * 9904    70008D04
 * some kind of dispose method, unlinks next/prev pointers.
 */
void sndUnlinkClearSound(ALSoundState *state)
{
    if (state == (ALSoundState *)D_800243E4.node.next)
    {
        D_800243E4.node.next = state->link.next;
    }

    if (state == (ALSoundState *)D_800243E4.node.prev)
    {
        D_800243E4.node.prev = state->link.prev;
    }

    alUnlink(&state->link);

    if (D_800243E4.g_sndPlayerSoundStatePtr != NULL)
    {
        state->link.next = (void *)D_800243E4.g_sndPlayerSoundStatePtr;
        state->link.prev = NULL;
        D_800243E4.g_sndPlayerSoundStatePtr->link.prev = (void *)state;
        D_800243E4.g_sndPlayerSoundStatePtr = state;
    }
    else
    {
        state->link.prev = NULL;
        state->link.next = NULL;
        D_800243E4.g_sndPlayerSoundStatePtr = state;
    }

    if ((state->unk3e & 4) != 0)
    {
        g_sndAllocatedVoicesCount--;
#ifdef PORT
        if (getenv("GE_AUDIOTRACE")) { /* D202/M-65 diag; remove once root-caused */
            geTracePrintf("audiotrace.log", "[VOICE-] sound=%p state=%p flags=%d count=%d\n",
                    (void *)state->sound, (void *)state, (int)state->unk3e,
                    (int)g_sndAllocatedVoicesCount);
        }
#endif
    }

    state->playingState = AL_STOPPED;

    if (state->state != NULL)
    {
        if (state == (ALSoundState *)state->state->link.next)
        {
            state->state->link.next = NULL;
        }

        state->state = NULL;
    }
}

/**
 * 99D8    70008DD8
 * Sets priority of ALSoundState.
 */
void sndSetPriority(ALSoundState *state, u8 priority)
{
    if (state != NULL)
    {
        state->priority = priority;
    }
}

/**
 * 99F0    70008DF0
 * Gets Playing State if a state is available
 * @param state: the state to check
 * @return AL_PLAYSTATE
 */
u8 sndGetPlayingState(ALSoundState *state)
{
    if (state != NULL)
    {
        return state->playingState;
    }

    return AL_STOPPED;
}

#ifdef DEBUG
#    define _sndPlaySfx(sbank, id, state) sndPlaySfx(sbank, id, state, g_sndSfxVolume, __FILE__, __LINE__)
ALSoundState *sndPlaySfx(struct ALBankAlt_s *soundBank, s16 soundIndex, ALSoundState *pendingState, f32 volume, char*file, int line)
#else
/**
 * 9A08    70008E08
 *     sets sound effect; used by sound effect routines
 *
 * Old comments:
 *
 *     accepts: A0=p->SE buffer, A1=SE #, A2=p->data
 *          data:    0x0    4    p->SE entry
 *              0x4    4    target volume
 *              0x8    4    audible range (timer)
 *              0xC    4    initial volume
 *              0x10    4    p->preset emitting sound
 *              0x14    4    p->object emitting sound
 *
 * // end old comments.
 *
 * @param soundBank sound bank
 * @param soundIndex index into sound bank: soundBank->instArray[0]->soundArray[soundIndex]
 * @param pendingState Optional pointer. If provided, its link.next pointer will be
 * to the newly created soundState.
 */
ALSoundState *sndPlaySfx(struct ALBankAlt_s *soundBank, s16 soundIndex, ALSoundState *pendingState)
#endif
{
    // declarations

    // declaration order doesn't seem to matter for these.

    ALMicroTime deltaTotal;
    ALSound *sound;
    ALSoundState *newState;
    ALSoundState *nextState;

    // declaration order matters:

    s16 eventSoundIndex;       // 110(sp)
    s16 unused_sp6c;           // 108(sp)
    ALMicroTime playSfxDelta;  // 104(sp)
    ALMicroTime deltaLoop; // 100(sp)

    // end declarations

    nextState = NULL;
    eventSoundIndex = 0;
    deltaTotal = 0;

    if(0); // debug?

    if (g_sndBootswitchSound)
    {
        return NULL;
    }

    if (soundIndex == 0)
    {
        return NULL;
    }

    do
    {
        ALKeyMap *keyMap;

        sound = (soundBank->instArray[0]->soundArray[soundIndex]);
#ifdef PORT
        /* D202 diag probe (temporary): trace which soundIndex resolves to
         * which ALSound*, to root-cause the M-52 "wrong sample plays"
         * playtest report. Remove once root-caused. */
        if (getenv("GE_AUDIOTRACE")) {
            extern unsigned long long audioDumpBytePos(void);
            geTracePrintf("audiotrace.log", "[AUDIOTRACE] dumppos=%llu sndPlaySfx: bank=%p soundIndex=%d -> sound=%p keyMap=%p wavetable=%p base=%p len=%d type=%d flags=%d book=%p\n",
                    (unsigned long long)audioDumpBytePos(),
                    (void *)soundBank, (int)soundIndex, (void *)sound,
                    sound ? (void *)sound->keyMap : NULL,
                    (sound && sound->wavetable) ? (void *)sound->wavetable : NULL,
                    (sound && sound->wavetable) ? (void *)sound->wavetable->base : NULL,
                    (sound && sound->wavetable) ? sound->wavetable->len : -1,
                    (sound && sound->wavetable) ? (int)sound->wavetable->type : -1,
                    (sound && sound->wavetable) ? (int)sound->wavetable->flags : -1,
                    (sound && sound->wavetable && sound->wavetable->type == AL_ADPCM_WAVE) ?
                        (void *)sound->wavetable->waveInfo.adpcmWave.book : NULL);
        }
        /* C7 / D127: the libaudio subsystem is Phase-3 parked (no real bank
         * playback yet).  On some levels (Surface1, -level_36) a requested
         * soundIndex resolves to a bogus ALSound* -- the converted bank tree
         * (port/src/romdata.c afFixupInst) has fewer/rearranged soundArray
         * slots than the N64 image -- and sndSetupSound() then faults on
         * sound->keyMap.  Until audio lands, skip a sound whose pointer is
         * plainly not a mapped address rather than crash the level.
         * ALSound lives in game DRAM (~0x7000_0000..) or the cart image
         * (0x1_4000_0000..); anything else (e.g. 0x0000_5622_0001_0001) is a
         * byte-scrambled / OOB read. */
        {
            uintptr_t sp = (uintptr_t)sound;
            if (sp < 0x10000 || sp >= 0x400000000ULL) {
                return NULL;
            }
        }
#endif

        newState = sndSetupSound(soundBank, sound);

#ifdef PORT
        /* D202 diag probe (temporary): log the ALSoundState* handed back per
         * soundIndex so a later sndDeactivate trace can be correlated back
         * to "which chain link was this". Remove once root-caused. */
        if (getenv("GE_AUDIOTRACE")) {
            ALKeyMap *kmp = sound ? sound->keyMap : NULL;
            /* D202/M-65: the retrigger machinery is what the user's three
             * symptoms all route through, and none of it was traced before.
             * Rare repurposed ALKeyMap: velocityMax is the retrigger PERIOD
             * in 33ms ticks, velocityMin is the NEXT soundIndex in the chain
             * this call is walking, and keyMax>>4 seeds unk3e (bit 0x10 =
             * SOUND_FLAG_RETRIGGER = "replay me forever until deactivated").
             * Bad values here produce, in order: wrong sound (bad chain
             * link), several sounds at once (chain too long), and a sound
             * that loops until level exit (period too short / flag stuck). */
            /* D202/M-65: flags bit 1 (SOUND_FLAG_LOOPED) is derived purely
             * from envelope->decayTime == -1, and a looped sound never posts
             * AL_SNDP_STOP_EVT -- it holds its voice until something
             * deactivates it. Print the envelope so a mis-assigned envelope
             * pointer can be told from genuine ROM data. */
            /* D202/M-65: an infinite ENVELOPE decay only stops the STOP_EVT
             * from being posted -- it does not by itself make a sample
             * repeat. If the WAVE also carries an ADPCM loop with count -1
             * the sound is audibly endless; if it has no loop, the sample
             * should run out and go silent, and anything still audible is a
             * port-side mixer fault rather than a lifecycle one. */
            if (sound && sound->wavetable && sound->wavetable->type == AL_ADPCM_WAVE) {
                ALADPCMloop *lp = sound->wavetable->waveInfo.adpcmWave.loop;
                geTracePrintf("audiotrace.log", "[WAVELOOP] soundIndex=%d wave=%p len=%d loop=%p start=%d end=%d count=%d\n",
                        (int)soundIndex, (void *)sound->wavetable, (int)sound->wavetable->len,
                        (void *)lp,
                        lp ? (int)lp->start : -1, lp ? (int)lp->end : -1,
                        lp ? (int)lp->count : -1);
            }
            if (sound && sound->envelope)
                geTracePrintf("audiotrace.log", "[ENVELOPE] soundIndex=%d env=%p attack=%d decay=%d release=%d aVol=%u dVol=%u\n",
                        (int)soundIndex, (void *)sound->envelope,
                        (int)sound->envelope->attackTime,
                        (int)sound->envelope->decayTime,
                        (int)sound->envelope->releaseTime,
                        (unsigned)sound->envelope->attackVolume,
                        (unsigned)sound->envelope->decayVolume);
            {
                extern uint64_t sysGetMicroseconds(void);
                geTracePrintf("audiotrace.log", "[AUDIOTRACE] sndPlaySfx: t=%llu soundIndex=%d -> newState=%p wavetable=%p flags=%d "
                    "keyMap=%p velMin(next)=%d velMax(period)=%d keyMin=0x%02x keyMax=0x%02x "
                    "keyBase=%d detune=%d retrig=%d deltaLoopUs=%d\n",
                    (unsigned long long)sysGetMicroseconds(),
                    (int)soundIndex, (void *)newState,
                    (void *)(sound ? sound->wavetable : NULL),
                    newState ? (int)newState->unk3e : -1,
                    (void *)kmp,
                    kmp ? (int)kmp->velocityMin : -1, kmp ? (int)kmp->velocityMax : -1,
                    kmp ? (unsigned)kmp->keyMin : 0u, kmp ? (unsigned)kmp->keyMax : 0u,
                    kmp ? (int)kmp->keyBase : -1, kmp ? (int)kmp->detune : -1,
                    (newState && (newState->unk3e & 0x10)) ? 1 : 0,
                    kmp ? (int)(kmp->velocityMax * DELTA_33_MS) : -1);
            }
            /* D202/M-65: only 8 voices exist (MUSIC_SFX_SEQ_MAYBE_MAX_SOUNDS).
             * A SOUND_FLAG_LOOPED voice never self-releases and is skipped by
             * the preemption scan, so each leaked one permanently costs a
             * voice. Once this hits maxSounds nothing new can ever sound. */
            {
                /* D202/M-71: also log evtq occupancy (64 slots; a full
                 * queue silently drops posts in alEvtqPostEvent). */
                ALLink *evn;
                s32 evAlloc = 0;
                OSIntMask evm = osSetIntMask(OS_IM_NONE);
                for (evn = g_sndPlayerPtr->evtq.allocList.next; evn; evn = evn->next)
                    evAlloc++;
                osSetIntMask(evm);
                geTracePrintf("audiotrace.log", "[VOICES] allocated=%d / max=%d evtq=%d/64\n",
                        (int)g_sndAllocatedVoicesCount,
                        (int)g_sndPlayerPtr->maxSounds, (int)evAlloc);
            }
        }
#endif

        if (newState != NULL)
        {
            ALSndpEvent playEvent;

            g_sndPlayerPtr->target = (s32)newState;
            playEvent.common.type = AL_SNDP_PLAY_EVT;
            playEvent.common.state = newState;
            deltaLoop = sound->keyMap->velocityMax * DELTA_33_MS;

            if (newState->unk3e & 0x10)
            {
                newState->unk3e &= (~(s16)(0x10));
                alEvtqPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&playEvent, deltaTotal + 1);
                playSfxDelta = deltaLoop + 1;
                eventSoundIndex = soundIndex;
            }
            else
            {
                alEvtqPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&playEvent, deltaLoop + 1);
            }

            nextState = newState;
        }

        deltaTotal += deltaLoop;

        keyMap = sound->keyMap;
        soundIndex = (s16)((s32)keyMap->velocityMin + ((s32)(keyMap->keyMin & 0xC0) * 4));
    } while (soundIndex != 0 && newState != NULL);

    if(!soundIndex)
    {
        // removed
    }

    if(!sound)
    {
        // removed
    }

    if (nextState != NULL)
    {
        nextState->unk3e |= 0x1;
        nextState->state = pendingState;

        if (eventSoundIndex != 0)
        {
            ALSndpEvent playSfxEvent;

            nextState->unk3e |= 0x10;

            playSfxEvent.playSfx.type = AL_SNDP_PLAY_SFX_EVT;
            playSfxEvent.playSfx.state = nextState;
            playSfxEvent.playSfx.soundIndex = eventSoundIndex; // types dont match
            playSfxEvent.playSfx.soundBank = soundBank;

#ifdef PORT
            /* D202/M-65 diag (temporary): this is the self-retrigger post --
             * the sound schedules itself to play again in playSfxDelta us,
             * forever, until sndDeactivate clears bit 0x10. A stuck looping
             * sound IS this event never stopping; a "piling up" mix is this
             * period being far too short. Remove once root-caused. */
            if (getenv("GE_AUDIOTRACE")) {
                geTracePrintf("audiotrace.log", "[AUDIOTRACE] RETRIGGER-POST: state=%p soundIndex=%d delayUs=%d (%.1f ms)\n",
                        (void *)nextState, (int)eventSoundIndex, (int)playSfxDelta,
                        (double)playSfxDelta / 1000.0);
            }
#endif
            alEvtqPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&playSfxEvent, playSfxDelta);
        }
    }

    if (pendingState != NULL)
    {
#ifdef PORT
        /* D202/M-65 diag (temporary): callers like doorPlayOpenSound0 pass
         * &door->openSoundState here (an ALSoundState** punned as an
         * ALSoundState*), relying on ALLink.next sitting at offset 0. This
         * write IS how a looping SFX gets an owner that can later stop it.
         * Log it so "slot never written" can be told apart from "slot
         * written then cleared". Remove once root-caused. */
        if (getenv("GE_AUDIOTRACE")) {
            geTracePrintf("audiotrace.log", "[SLOTWRITE] slot=%p <- state=%p (was %p)\n",
                    (void *)pendingState, (void *)nextState,
                    (void *)pendingState->link.next);
        }
#endif
        pendingState->link.next = (void*)nextState;
    }

    return nextState;
}

/**
 * 9C20    70009020
 *     decativates sound effect
 *     accepts: A0=p->SE buffer
 */
void sndDeactivate(ALSoundState *state)
{
    ALSndpEvent evt;

    evt.common.type = AL_SNDP_DEACTIVATE_EVT;
    evt.common.state = state;

#ifdef PORT
    /* D202 diag probe (temporary): does this ever fire for the stuck
     * door-loop voice? Correlate against the sndPlaySfx newState= trace.
     * Remove once root-caused. */
    if (getenv("GE_AUDIOTRACE")) {
        geTracePrintf("audiotrace.log", "[AUDIOTRACE] sndDeactivate: state=%p (%s)\n",
                (void *)state, state ? "non-null" : "NULL-noop");
    }
#endif

    if (state != NULL)
    {
        state->unk3e = (s8) (state->unk3e & (~(s16)(0x10)));

        alEvtqPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&evt, 0);
    }
}

/**
 * 9C6C    7000906C
 * Similar to sndDeactivate, but iterates the global list and deactivates
 * items with the same unk3e flag.
 *
 * @param flag flag bitmask to match item on.
 */
void sndDeactivateAllSfxByFlag(u8 flag)
{
    OSIntMask mask;
    ALSndpEvent evt;
    ALSoundState *item;

    mask = osSetIntMask(OS_IM_NONE);

    item = (ALSoundState *)D_800243E4.node.next;
    while (item != NULL)
    {
        evt.common.type = AL_SNDP_DEACTIVATE_EVT;
        evt.common.state = item;

        if ((item->unk3e & flag) == flag)
        {
            item->unk3e = (s8) (item->unk3e & (~(s16)(0x10)));
            alEvtqPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&evt, 0);
        }

        item = (ALSoundState *)item->link.next;
    }

    osSetIntMask(mask);
}

/**
 * 9D24    70009124
 *     redirect to 7000906C: A0=1
 */
void sndDeactivateAllSfxByFlag_1(void)
{
    sndDeactivateAllSfxByFlag(1);
}

/**
 * 9D44    70009144
 *     redirect to 7000906C: A0=11
 */
void sndDeactivateAllSfxByFlag_11(void)
{
    sndDeactivateAllSfxByFlag(0x11);
}

/**
 * 9D64    70009164
 *     redirect to 7000906C: A0=3
 */
void sndDeactivateAllSfxByFlag_3(void)
{
    sndDeactivateAllSfxByFlag(3);
}

/**
 * 9D84    70009184
 * Calls alEvtqPostEvent with the method parameters and delta=0.
 *
 * @param state sound state.
 * @param eventType type of event to post.
 * @param arg2 event data value (interpretation depends on eventType).
 */
void sndCreatePostEvent(ALSoundState *state, s16 eventType, s32 arg2)
{
    ALSndpEvent evt;

    /* D202/M-65: this was stubbed out on PORT by D138, back when libaudio was
     * Phase-3 parked and nothing drained g_sndPlayerPtr->evtq->allocList --
     * every positional-sound tick appended an item that was never consumed,
     * so alEvtqPostEvent's ordered insert walked an ever-growing list (O(n^2))
     * until the kernel-heartbeat watchdog tripped on Facility's ambience.
     * That premise expired when the Phase-3 audio thread landed (D198-D201):
     * amMain now drains the queue every audio frame, so the list stays short.
     *
     * Leaving the stub in place is not neutral. Every caller posts type 8
     * (AL_SNDP_VOL_EVT), so the stub silently removed ALL distance-based
     * volume attenuation, and with it the only per-tick contact the engine
     * has with a playing voice. See docs/dev/findings.md D202/M-65. */
    evt.common.type = eventType;
    evt.common.state = state;
    evt.unks32.val8 = arg2;

    if (state != NULL)
    {
        alEvtqPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&evt, 0);
    }
}

/**
 * 9DC8    700091C8
 *     redirect to 70009264: A0=0
 */
u16 sndGetSfxSlotFirstNaturalVolume(void)
{
    return sndGetSfxSlotNaturalVolume(0);
}

/**
 * 9DE8    700091E8
 */
void sndApplyVolumeAllSfxSlot(u16 volume)
{
    u8 i;
#ifdef PORT
    /* D152: collapse the per-slot sndSetSfxSlotVolume() lock acquisitions
     * into one recursive hold for the whole update (the fade-out calls this
     * every frame). One real lock acquire/release per frame instead of
     * SFX_SLOT_COUNT * (active sounds). No behaviour change. */
    OSIntMask mask = osSetIntMask(OS_IM_NONE);
#endif

    for (i = 0; i < SFX_SLOT_COUNT; i++)
    {
        sndSetSfxSlotVolume(i, volume);
    }
#ifdef PORT
    osSetIntMask(mask);
#endif
}

/**
 * 9E38    70009238
 */
void sndSetScalerApplyVolumeAllSfxSlot(f32 volumeScale)
{
    g_sndSfxVolumeScale = volumeScale;
    sndApplyVolumeAllSfxSlot(sndGetSfxSlotFirstNaturalVolume());
}

/**
 * 9E64    70009264
 *     V0= halfword A0 in table at [80063BA8]; fries T6,T7,T8,T9
 */
u16 sndGetSfxSlotNaturalVolume(u8 sfxIndex)
{
    return g_sndSfxSlotNaturalVolume[sfxIndex];
}

/**
 * 9E84    70009284
 */
void sndSetSfxSlotVolume(u8 sfxIndex, u16 volume)
{
    // Not sure if these are debug leftovers, or is the type `ALSndpEvent` wrong?
    u32 unused[2];

    ALSndpEvent evt;
    ALSoundState *item;
#ifdef PORT
    /* D152: this walks the live ALSoundState list and posts to the shared
     * ALEventQueue, exactly like its sibling sndDeactivateAllSfxByFlag() --
     * which DOES take OS_IM_NONE around its walk. The omission here is benign
     * on N64 (alEvtqPostEvent masks internally, and the audio job is a
     * cooperative RSP task) but not on PC: the audio manager (amMain) is a
     * real preemptible thread, so the unguarded walk both races a concurrent
     * list mutation AND takes/drops the interrupt-mask lock (D147/D152)
     * once per matching sound. During the mission-failed audio fade-out
     * (sndSetScalerApplyVolumeAllSfxSlot -> sndApplyVolumeAllSfxSlot -> here,
     * every frame, over every active sound) that is dozens of lock
     * acquire/release per frame on the game thread, each a fresh window to
     * collide with amMain and eat the 2 s steal-lock timeout -> the frame
     * never renders -> permanent black screen. Hold the mask once across the
     * whole walk: the nested alEvtqPostEvent() calls then hit the recursive
     * fast path (depth++) and open no new contention window, and the walk is
     * atomic w.r.t. amMain as the N64 "interrupts off" intent requires.
     * Concurrency-correctness only; no behaviour change. */
    OSIntMask mask = osSetIntMask(OS_IM_NONE);
#endif

    item = (ALSoundState *)D_800243E4.node.next;

    g_sndSfxSlotNaturalVolume[sfxIndex] = volume;
    g_sndSfxSlotVolume[sfxIndex] = (s16) ((f32) volume * g_sndSfxVolumeScale);

    while (item != NULL)
    {
        if (item->sound != NULL)
        {
            if ((item->sound->keyMap->keyMin & 0x3f) == sfxIndex)
            {
                evt.common.type = AL_SNDP_RELEASE_EVT;
                evt.common.state = item;

                alEvtqPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&evt, 0);
            }
        }

        item = (ALSoundState *)item->link.next;
    }
#ifdef PORT
    osSetIntMask(mask);
#endif
}
