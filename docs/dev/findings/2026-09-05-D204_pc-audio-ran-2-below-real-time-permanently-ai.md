# D204

<!-- from the old section-F/H table row -->
| D204 | **PC audio ran ~2 % below real time permanently (AI feedback loop's 3 ms setpoint vs PC jitter).** — full `## D204` entry at file tail | FIXED (`port/src/audio.c`, `port/src/libultra.c`, `port/include/audio.h`) — measured; a by-ear pass on a real playthrough still owed |

## D204 — Audio pipeline runs ~2 % below real time forever: GE's AI feedback loop has a 3 ms setpoint that OS jitter clears, so the DAC is padded with silence (M-64, FIXED + measured)

**Found while reviewing the D202 audio state, not by chasing D202's stated
symptom.** D202 itself is NOT resolved by this — see "What this does and does
not explain" below.

### The architecture (established, not assumed)

GE's audio is a closed-loop, DAC-backpressure-driven producer:

- `amMain` (`src/audi.c:433`) blocks on `OS_SC_RETRACE_MSG`. `__scMain`
  (`src/sched.c:334`) forwards a retrace to a client either every frame or,
  when the client registered with a non-zero flag, every *second* frame. The
  audio client registers with `1` (`audi.c:441`), so it wakes at **30 Hz**.
- Per wake, `amHandleFrameMessage` (`audi.c:531`) sizes the next block:
  `frameSamples = (u16)((g_FrameSize - (osAiGetLength() >> 2) + 16 + 0x25) & ~0xf)`,
  then clamps *below* at `g_MinFrameSize`.
- NTSC constants: `outputRate=22050`, `FRAMES_PER_FIELD_AS_POW2=1`,
  `MAYBE_FRAME_RATE=60` → `g_FrameSize=736`, `g_MinFrameSize=720`,
  `g_MaxFrameSize=789`. `info->data` is allocated exactly `g_MaxFrameSize*4`
  = 3156 bytes (`audi.c:388`).
- So `osAiGetLength()` is a **±64-sample trim**, and the loop only asks for
  more than 720 once the reported length falls under ~69 frames — **3 ms**.

### The defect

720 samples per 30 Hz block is **21600 samples/s against a 22050 Hz device**
— the nominal rate is structurally 2 % short, and the design depends on those
under-69-frames top-up blocks (784 samples) to make it back.

On N64 a 3 ms setpoint is fine: AI is double-buffered and the VI interrupt is
exact. On PC, `osAiGetLength()` maps onto the SDL queue, and ordinary OS
scheduling jitter empties a 3 ms cushion before the loop reacts. The queue
hits zero, SDL pads playback with silence, and **that lost time is
unrecoverable** — a queue-*depth* reading can never tell the regulator it has
already fallen behind. Measured, sustained, indefinitely:

```
GE_D204_OLD=1  rt=0.980  q=0..416   (q reaches 0 repeatedly)
```

`rt` = produced audio seconds / wall seconds. 0.980 is exactly 21600/22050.

### The fix (F5, port-side only — `src/audi.c` untouched, rule #2 clean)

`port/src/audio.c`'s `audioGetAiLengthBytes()` subtracts a target cushion
(`AUDIO_TARGET_FRAMES` = 1024 frames ≈ 46 ms) before reporting, so "the queue
holds the cushion" reads to the game as "the queue is empty". The loop then
tops up while ~46 ms of slack remains and settles just above the cushion
instead of oscillating into starvation. audi.c's own control law is unchanged.

```
after (same repro)   rt=1.000  q=368..672  drop=0   (q never reaches 0)
```

Also landed, all in `port/src/audio.c` / `port/src/libultra.c`:

- **F1** — `osAiGetLength()` now reports the residue of a *single* buffer
  (`min(queued, lastBufferBytes)`), matching AI_LEN_REG, instead of the whole
  SDL queue depth. **Robustness only: measured A/B shows zero behavioural
  change.** See the falsified hypothesis below.
- **F3** — `Audio.QueueLimit` default 8192 → 2880 frames (372 ms → 130 ms
  worst-case latency), and a full queue now logs a dropped block instead of
  discarding it silently. **Caveat: an existing `data/ge007.ini` pins
  `QueueLimit = 8192`, so this default only reaches fresh configs.**
- **F4** — invariant guard refusing any block larger than `g_MaxFrameSize*4`.
  Believed unreachable; costs one compare, versus a silent heap overrun.
- **`GE_D204=1`** — audio-health monitor, one line per 5 s: `rt=` real-time
  ratio, `q=` queue depth, `drop=`, `max=` largest block vs its allocation.
  Deliberately cheap (~30 clock reads/s) so it can be left on for a whole
  playtest. **`GE_D204_OLD=1`** restores pre-fix behaviour for in-binary A/B.

### Hypothesis raised and FALSIFIED — do not re-open

I predicted the u32 subtraction in `audi.c:531` wraps once the queue exceeds
`g_FrameSize + 0x35` = 789 frames, truncates to u16 (e.g. 58128), and drives a
74× heap overrun of the 3156-byte `info->data`. **The queue really does exceed
789 routinely — measured high-water 2064 frames over 5 minutes — and the wrap
really does happen, but it is harmless.** `frameSamples` is declared
**`s16`** (`audi.c:145`), so the wrapped value lands negative and audi.c's own
`(s32)frameSamples < (s32)(s16)g_MinFrameSize` clamp catches it, yielding a
nominal 720-sample block. Max block observed is 3136 bytes against the
3156-byte allocation, in both old and new modes, across every run. There is no
overrun. This is why F1 measures as a no-op.

### Two M-63 conclusions corrected

1. **`soundIndex=109` firing "94 times in 60 s with no AK47 present" is a red
   herring — closed, negative.** The count is doubled by the probe (it logs two
   lines per call); 47 real calls. All of them fall in tight bursts *after*
   t≈5.4 s, interleaved with ricochet indices 27/28/30/35/39/41 — **guards
   returning fire**, correct behaviour once the player starts shooting in
   Bunker. And the "suspiciously uniform ~32.6 ms cadence" is not a retrigger
   artifact at all: 32.6 ms **is** the audio-block quantum (720 frames × 4
   bytes = 2880 bytes), so *every* `dumppos` in the entire trace is quantised
   to it. Several sounds landing in one block read as "4-in-a-row".
2. **M-63's implied "audio produced at 13 % of real time" (708608 dump bytes
   = 8.03 s over a 60 s run) was a measurement artifact of its own probe.**
   That run had `GE_MIXERTRACE=1`, whose unbuffered per-opcode `fprintf`
   produced a 25 MB log and slowed the process enough to starve the audio
   thread. The identical repro without it produces 58.0 s of audio in 60 s.
   **Lesson: `GE_MIXERTRACE` is not safe for any timing-sensitive measurement**
   — that is exactly why `GE_D204` was written to be cheap. M-63's
   recommendation to have the user replay with `GE_MIXERTRACE=1` set would
   have induced the very starvation it was looking for.

### What this does and does not explain

**Does:** a permanent low-level stutter/gap artifact in all PC audio — ~2 % of
playback was silence, in every level, for every user, from the first frame.

**Does not:** D202's reported chain (wrong sound on the silenced PPK, correct
sound sometimes heard simultaneously, sounds piling up and glitching over a
session, eventual near-silence except one stuck loop). Nothing in this
session's headless runs degraded: a 5-minute `-level_09` soak held `rt=1.000`,
`drop=0`, no voice-pool growth. D202 stays OPEN. It needs a real playtest —
now instrumentable with `GE_D204=1` (plus `GE_AUDIOTRACE=1`, and **not**
`GE_MIXERTRACE`), where `rt` falling or `q` pinning at `queueLimit` with
`drop=` climbing would localise it immediately.

### Verification

Reproduce with **`.	ools_pcudiodebug.ps1 -AB -Fire`** (added this session;
`-Play -Trace` is the instrumented interactive run for D202). `-level_09`,
scripted PPK fire, MSYS2 MINGW64 build, full `data/` mirror. In-binary A/B via
`GE_D204_OLD` (same executable, no build-to-build variance):
`rt` 0.980 → 1.000, `q` min 0 → 368, `drop` 0, `max` 3136/3156 unchanged.
Not yet verified by ear — the artifact is a ~2 % silence rate, so a human
listening pass on a real playthrough is still owed before calling it closed.

### Harness gotchas found while building `audiodebug.ps1` (both cost a debug cycle)

1. **`Start-Process -RedirectStandardOutput` does not capture this game's
   stdout.** `CMakeLists.txt:577` sets `WIN32_EXECUTABLE TRUE` (`-mwindows`),
   so `ge007.x86_64.exe` is a GUI-subsystem binary with no console of its own;
   the redirect yields a 0-byte log while the output lands on the parent
   console. A shell that opens the file itself before `CreateProcess` (cmd's
   `>`, or a bash redirect) does work.
2. **MSYS2 `bash.exe` launched from PowerShell arrives with a stripped
   environment.** Neither variables inherited from PowerShell nor the script's
   own `export` reach the child process — verified with a minimal
   `export GE_PROBE=hello; env | grep GE_PROBE` probe, which prints under
   `sh script.sh` from MSYS but nothing when the same script is run by
   `powershell -Command "& bash.exe script.sh"`. Every `GE_*` probe silently
   read as unset, which looks exactly like "the probe is broken". `cmd.exe`
   inherits normally, so `audiodebug.ps1` generates a `.bat` and runs that.

### M-78b — user repro on Bunker: symptom is specific and permanent

Natural Bunker alarm trigger (spawn, go forward into the room ahead, shoot
one of the two guards → the other trips the alarm), then sustained firefight.
User by-ear result:

- Most SFX keep playing during the klaxon.
- **Eventually the *player's* gunshots stop being emitted — and stay muted
  even after the alarm ends.** Enemy gunfire and all other SFX continue fine
  during and after.

This is NOT generic pool starvation (that would drop random sounds
transiently). It is specific to the player-weapon fire sound and it is
*permanent* — a stuck-state bug, not a transient voice shortage.

**Structural lead (static, unconfirmed — needs the capture).** The player
fire sound is played through a **two-slot double buffer** in
`bondfirefunc` / `gunfire.c:3178-3202`: `handptr->audioHandle` and
`handptr->field_A48`. Each trigger interval: `sndDeactivate` whichever slot
is currently *playing*, then `sndPlaySfx(..., &slot)` into whichever slot is
*NULL* (`audioHandle` first, else `field_A48`). If **both** slots are ever
left non-NULL-but-not-playing at once, the `else if (field_A48 == 0)` guard
fails and **no further player gunshot can play, ever** — matches the symptom
exactly.

A slot is cleared only by `sndUnlinkClearSound` (via `sndDisposeSound`)
following the state's back-link `state->state->link.next`. `sndDeactivate`
is **asynchronous** — it posts `AL_SNDP_DEACTIVATE_EVT` to the 64-slot SFX
event queue (`MUSIC_SFX_SEQ_CONFIG_MAX_EVENTS = 0x40`). If that queue is
saturated (heavy firefight + the alarm's self-retrigger posts + every
ricochet/impact/yelp posting PLAY/PLAY_EVT/STOP/DEACTIVATE), `alEvtqPostEvent`
**drops the event silently** → the deactivate never runs → the state never
disposes → the slot never clears. Once both slots are stuck, permanent mute.
The `[VOICES] ... evtq=N/64` probe already in `snd.c` will show queue
saturation; `[SLOTWRITE]` + `[VOICE+]/[VOICE-]` + `sndPlaySfx t=` trace the
two player-gun slots. All three counts (8 voices / 64 states / 64 events) are
byte-match stock GE — so if this is the mechanism it is a port *timing* bug
(the SFX event queue drained too infrequently vs N64's RCP cadence, D204
family), not a value to bump.

**Next:** read the user's `build-pc/audiotrace.log` — confirm (1) `evtq`
approaches 64, (2) a `[AUDIOTRACE] sndDeactivate: state=X` with no following
`[VOICE-] state=X` / dispose, (3) two `[SLOTWRITE] slot=<audioHandle>` and
`slot=<field_A48>` with no clear afterwards. Then the fix is port-side: drain
the SFX event queue more often, or (cheaper, D202-family-consistent) a
bounded self-heal in the double-buffer — if a slot has pointed at a
non-playing state for > N ms, force-clear it.
