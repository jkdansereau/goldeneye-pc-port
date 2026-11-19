# D207

<!-- from the old section-F/H table row -->
| D207 | **Alarm klaxon starves combat SFX and doesn't recover after it stops (surfaced by D206, M-77).** — full `## D207` entry at file tail | **CLOSED (2026-09-28 bookkeeping: no drops in the M-201 live captures; sibling D241 closed as the D207 family in the v0.4.0 campaign sign-off. Re-open with a capture if drop-outs recur).** Earlier: MONITORING — user 2026-09-26: "fixed possibly"; M-201 live captures showed no drops (see D241). Earlier: MONITORING (M-77c: user reports the alarm is acceptable in play after D206; no fix shipped) — voice-starvation hypothesis did NOT hold up (M-77b); designed pass reverted. |

## D207 — **Alarm klaxon starves combat SFX (surfaced by D206)** (M-77, root-caused, fix designed & not applied)

### How D206 caused it

`ALARM3_SFX` (SFX_ID 163) is played by `handle_alarm_gas_timer_calldamage()`
(`src/game/propobj.c:14457`) whenever `alarmIsActive()`, owner `&ptr_alarm_sfx`.

- **Pre-D206:** resolved to on-disk bank slot **163** — a **one-shot** sample
  (`decayTime` finite, no ADPCM loop). Played once, released its voice. The
  alarm was audibly the *wrong* sound but cost the voice pool nothing.
- **Post-D206:** correctly resolves to slot **162** — the real klaxon:
  `envelope.decayTime == -1` (→ `SOUND_FLAG_LOOPED`, `sndSetupSound`
  `snd.c:~836`), wavetable `adpcmWave.loop.count == -1` (infinite ADPCM loop).
  It now holds a voice for the whole time the alarm is up.

### Why it dominates

The SFX player has **8 voices** (`MUSIC_SFX_SEQ_MAYBE_MAX_SOUNDS = 8`,
`src/music.c:76`; the synth has 24 physical voices but the SFX soft cap is 8
and is what `snd.c:328` gates on).

`sndSetupSound` sets `state->priority = (decayTime == -1) + 0x40` → the alarm
gets **0x41**, one-shots get **0x40**.

When `g_sndAllocatedVoicesCount == 8`, `snd.c` PLAY_EVT does **not** call
`alSynAllocVoice` (which *would* do libultra priority-stealing); it runs the
manual preemption scan at `snd.c:~347-392`. That scan:
- refuses to steal any `unk3e & 0x12` voice (LOOPED | RETRIGGER) — the M-65
  carve-out only exempts *ownerless* loops (`state == NULL`), and the alarm
  is **owned**;
- ignores priority entirely — it steals the first non-looped PLAYING
  non-PREEMPT voice it finds;
- if it finds none → `sndDisposeSound(soundState)` → **the new SFX is
  dropped** (`snd.c:391`).

So an active alarm removes one of the 8 voices, and in a firefight (gunshots +
ricochets + impacts + enemy fire + ambient door/machine loops) the remaining
non-looped voices are all either already PREEMPT-marked from the same burst or
themselves looped → gunshots drop.

### Measured (BUNKER1, `-level_09`)

Temporary probe `GE_FORCEALARM=1` (added to `handle_alarm_gas_timer_calldamage`,
`#ifdef PORT` / `getenv`) pins `alarm_timer` so the klaxon stays up, plus a
scripted `GE_INPUTSCRIPT` firing Z every 8 reads:

| condition | SFX requests | dropped (no voice) |
|---|---|---|
| no alarm | 52 | 1 (~2%) |
| alarm active | 137 | 28 (~20%) |

Dropped indices were gunshot/impact/ricochet sounds (29, 30, 41, 90, 36, 109…).
`ptr_alarm_sfx` stayed stable while active and was cleared on
`alarmDeactivate()`; the alarm voice was freed and the pool recovered — the
**"doesn't recover after the alarm stops" half of the user report was NOT
reproduced** in any trace (light BUNKER1 combat; may need a heavier level, or
it is perceptual).

### The 8-cap and the scan are faithful decomp

`MUSIC_SFX_SEQ_MAYBE_MAX_SOUNDS = 8` is unmodified game code; the manual
preemption scan is stock `sndHandleEvent` (M-65 modified it in place). Upstream
GE has the same. So this is not purely a port regression — it is partly the
open **D202** voice-pool-margin problem (ambient infinite-loops holding voices
longer than they should on the port) becoming audible because the alarm now
legitimately consumes a slot.

### Designed fix (PORT, not yet applied)

In the `snd.c` preemption scan, add a **last-resort second pass**: after the
normal pass (non-looped victims + the M-65 ownerless-loop carve-out) finds
nothing and `limitReached` is still true, and **the incoming `soundState` is
itself not LOOPED**, run one more pass that will preempt a stoppable LOOPED,
non-RETRIGGER voice (the alarm klaxon; also the D202 door loop as a bonus).

Self-healing: preempting the alarm posts `AL_SNDP_END_EVT` → `sndDisposeSound`
→ `sndUnlinkClearSound` clears `state->state->link.next`, i.e. sets
`ptr_alarm_sfx = 0`; the very next frame `handle_alarm_gas_timer_calldamage`
sees `ptr_alarm_sfx == 0` and re-issues `sndPlaySfx(ALARM3_SFX, &ptr_alarm_sfx)`,
which re-acquires a voice (or WAIT_VOICE-retries at 33 ms). Net effect: the
klaxon briefly yields to a gunfire burst and comes right back — gameplay audio
wins, which matches player expectation.

Alternatives weighed and rejected for now: (a) raise `maxSounds` > 8 on the
port — game-code change, rule #2 risk, changes mix loudness; (b) properly fix
the D202 ambient-loop lifetimes — the correct fix but the hard open problem.

### M-77b — designed fix tried, hypothesis not confirmed, reverted

The last-resort LOOPED-preemption pass was implemented in the `snd.c` scan,
plus `[D207-DROP]` counters at both no-voice `sndDisposeSound` sites and a
`[D207-YIELD]` trace on the new pass. BUNKER1 test (`GE_FORCEALARM=1` +
`GE_INPUTSCRIPT` firing every ~10 controller reads for ~85 s, alarm cycled
on/off/on):

- **`[D207-DROP]` = 0** — not a single SFX actually dropped.
- **`[D207-YIELD]` = 0** — the new pass never even ran; pass 1 always found a
  non-looped voice to preempt.
- Per-frame voice count: **1** during quiet alarm, spikes to 3–5 under fire,
  back to **0** after every alarm-off. No leak, no pegged pool.
- The earlier "≈20 % dropped" number was bogus: it counted requests whose
  `[VOICE+]` came more than 40 log-lines later. Cross-checking "states that
  never got a voice at all" = **0 / 136**. Non-looped sounds get 2 `unk38`
  retries then the scan, which nearly always places them a frame or two late —
  audible as slightly loose timing, not silence.

So on BUNKER1 the alarm does **not** starve the pool. The change was reverted
(`snd.c` now carries only the D206 `-1`). Either BUNKER1 combat is too light to
reach 8/8, or the user's symptom is a different mechanism.

### Owed next session — needs a real repro from the user

The BUNKER1 forced-alarm harness cannot reproduce this. Get from the user:
**which level, which weapon/situation**, and a `GE_AUDIOTRACE=1` capture (or a
short clip) of the failing alarm. Then decide between:

1. **Genuine 8/8 starvation on a heavy level** — Facility / Silo / Statue
   firefights have many more guards firing. Soak one with `GE_FORCEALARM` +
   sustained scripted fire (or the user's own capture) and watch for
   `[D207-DROP]` > 0 / `count=8` pinned. If real, the reverted last-resort
   pass is the fix — re-apply it.
2. **A leak on a specific alarm-stop path** — the `alarm_timer` auto-timeout
   (`CHROBJ_GAS_TIMER`) vs `AI_AlarmOff` vs `deactivate_alarm_sound_effect`
   from a cutscene. Trace `g_sndAllocatedVoicesCount` across each.
3. **Perceptual** — the klaxon is a *continuous* infinite loop (`slot 162`,
   no gaps). A real GE alarm pulses. Check: is `ALARM3` supposed to retrigger
   (keyMap velMax=7 was noted but `SOUND_FLAG_RETRIGGER` needs `keyMax & 0xf0`,
   and keyMax=4 → not set)? And compare slot-162 playback **volume** to N64 —
   if the drone is just mixed loud it will mask everything without dropping a
   single voice.

**Temp probe kept:** `GE_FORCEALARM` in `handle_alarm_gas_timer_calldamage`
(`src/game/propobj.c`, `#ifdef PORT` / `getenv`).



**M-77c (from the §F row, appended 2026-09-15 ctx-opt):** user reports the alarm is acceptable in play after D206; no fix shipped. Disposition: **MONITORING**.
