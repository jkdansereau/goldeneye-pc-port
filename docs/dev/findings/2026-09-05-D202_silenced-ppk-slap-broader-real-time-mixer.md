# D202

<!-- from the old section-F/H table row -->
| D202 | **Silenced PPK "slap" + broader real-time mixer corruption (M-52→M-67).** — full `## D202` entry at file tail | FIXED (2026-09-26, user by-ear: silenced PPK sounds good, no slap or lingering loop; M-66b expiry + later audio fixes). Earlier: OPEN — by-ear check owed (2026-09-26): fire the silenced PPK a lot, listen for a slap/crackle or a lingering loop. Earlier: ROOT CAUSE ESTABLISHED (M-66); Disposition C implemented + measured (M-66b) — stuck loop is sound 203, a faithful N64 quirk; PC-side expiration fades it. M-67 static analysis exhausted. Full trail in the section. |

## D202 — Phase-3 audio: silenced PPK plays "slap" not gunshot, PLUS runtime mixer corruption (piling up / glitching / eventual silence) (M-52; reclassified M-59; REOPENED M-60 on reference clip; M-61/M-62 closed the data/index/decode chain and recommended close; M-63 REOPENED again; M-65 PARTIALLY resolved — voice-leak cascade root-caused + guarded, `sndCreatePostEvent` un-stubbed; **M-66 ROOT CAUSE ESTABLISHED for the audible stuck door loop — sound 203 behaving exactly as ROM + ground-truth code specify: faithful N64 behaviour / original quirk; M-66b DISPOSITION C IMPLEMENTED + MEASURED — port-side expiration fades ownerless infinite loops out ~2.5 s after start, awaiting user by-ear pass**)

**Symptom (M-52 playtest, first listening pass on the Phase-3 software mixer,
post-D200).** In-level (Bunker1, Dam — not menu/intro, which is music-only and
correct): (a) weapon fire consistently plays the WRONG but always-the-same
clip per weapon (e.g. Bond's silenced PPK plays what sounds like a melee/punch
hit, every single shot — fully deterministic, not intermittent garble); (b)
level music never plays — a looping SFX-like sound plays in its place from
level start.

**Ruled out, with hard evidence (do not re-investigate these):**
- `soundIndex -> ALSound*` resolution (`snd.c sndPlaySfx`, `soundBank->instArray[0]->soundArray[soundIndex]`):
  traced live with a `GE_AUDIOTRACE` probe against real gameplay (`-level_09`,
  repeated PPK fire). `soundIndex=46` (confirmed via `bondconstants.h`'s SFX
  name table = `46_GUN_SILPPK_A_SFX`) resolves to the same `ALSound*`/`ALWaveTable*`
  every time; 13+ distinct indices captured across one session, all distinct,
  all valid.
- D37's bank re-layout (`port/src/romdata.c romdataFixupAudioBank`): preserves
  `soundArray`/`instArray` order; not a re-ordering bug.
- The `ALWaveTable.base`/`.book` pointer chain: traced all the way to
  `alLoadParam(AL_FILTER_SET_WAVETABLE, ...)` (`src/libultra/audio/load.c`) —
  the exact same `table`/`base`/`book` addresses `sndPlaySfx` resolved are the
  ones wired into the physical decode filter. No swap between resolution and
  voice setup.
- `ALStartParamAlt` (40B on x86-64) vs the generic `ALParam` pool slot (32B):
  already fixed pre-M-52 (D54, `synthesizer.c:127-140`, `#ifdef PORT` sizes
  each slot for the largest variant). Re-confirmed still in place, not a
  regression.
- Per-voice synth processing (`synthesizer.c`'s client-list handler loop) is
  strictly sequential — one client's full setup+decode completes before the
  next starts. Ruled out global-mixer-scratch (`sCtx`/`sAdpcmTable`/`sVol` in
  `port/src/mixer.c`) cross-talk between concurrently active voices.
- `ALSndpEvent` (snd.c-local union, force-cast to `ALEvent*` through
  `alEvtqPostEvent`/`alEvtqNextEvent`, which copy `sizeof(ALEvent)` bytes)
  vs `ALEvent` (libaudio.h): measured directly with a throwaway compile —
  both are exactly 32 bytes on x86-64. Not a truncating-copy bug.
- `sndCountAllocList`'s `(ALEventQueue *)&D_800243E4` cast (a real, compiler-flagged
  `-Warray-bounds` OOB read — `D_800243E4_s` is 24B, aliased as 40B `ALEventQueue`):
  confirmed genuine but functionally inert — its output (`numFree`/`numAlloc`)
  is computed and immediately discarded in `sndHandleEvent`, never used.

**Not yet ruled out — pick up here:**
1. **`aADPCMdecImpl`'s decode math** (`port/src/mixer.c`, this session's new
   Phase-3 code, never verified against real N64 audio output). Structurally
   matches the standard VADPCM order-2 predictor algorithm on inspection
   (9 input bytes -> 16 output samples, intra-frame reverb-of-already-decoded-
   samples loop) but no reference trace/audio diff exists yet.
2. **The raw PCM bytes embedded in the exe at the resolved ROM offset being
   wrong at the source** (asset-embedding/segment-placement issue, unrelated
   to anything traced above) — would need a byte-level compare of the
   PC-embedded `_sfxtblSegmentRomStart` segment against the source `.z64`.

**Key diagnostic fact:** the wrong sample is 100% deterministic per weapon
(same wrong clip every shot, confirmed by user) — this argues against a
stateful/interleaving bug (already the least-likely explanation given the
sequential-processing proof above) and toward either (1) or (2) above, both
of which would consistently mis-decode/mis-fetch the same bytes every time
for the same `ALWaveTable`.

**Diagnostic tooling added this session (still in the tree, `#ifdef`/env-gated,
harmless if left in, should be stripped once root-caused):**
- `src/snd.c` `sndPlaySfx`: `GE_AUDIOTRACE=1` env var → appends resolved
  `soundIndex`/`sound`/`wavetable`/`base`/`len`/`type`/`flags`/`book` to
  `audiotrace.log` (cwd-relative, unbuffered).
- `src/libultra/audio/load.c` `alLoadParam` `AL_FILTER_SET_WAVETABLE`:
  same env var → appends the filter/table/base/len/book actually wired into
  a voice to `audiotrace_wire.log`.

**Also noticed, not yet a confirmed bug:** `alLoadParam`'s
`a->memin = (s32) a->table->base;` (`load.c:382,438`) truncates a real 64-bit
`u8 *` to `s32` — currently harmless only because the ROM is deliberately
mapped at a low, sub-2GB virtual address (`0x10000000`, 12MB window per the
boot log), so no real address in range overflows 32 bits. Fragile; worth a
`(s64)`/`uintptr_t` widening pass regardless of whether it's D202's cause.

**M-53 update — both of the two remaining suspects now independently checked,
both look clean (static analysis only, no listening pass possible this
session):**

1. **`aADPCMdecImpl` decode math (`port/src/mixer.c:148-192`) — compared
   line-by-line against the local Perfect Dark PC port's scalar reference
   (`pd_port/port/src/mixer.c:333-352`, the `#else` non-SIMD path).**
   Identical: same nibble unpack (`(((*in >> 4) << 28) >> 28) << shift` /
   same for the low nibble), same `prev1`/`prev2` history read from `out[-1]`/
   `out[-2]`, same accumulator (`tbl[0][j]*prev2 + tbl[1][j]*prev1 +
   (ins[j]<<11)` plus the `k<j` intra-frame correction term), same `>>11`
   scale-down + clamp, same 16-sample/32-byte state carry via
   `memcpy(state, out-16, ...)`. No discrepancy found. This was PD's
   ground-truth non-vectorized fallback, i.e. the reference the SIMD paths
   are themselves checked against — a strong match.
2. **`_sfxtblSegmentRomStart`'s ROM offset (`0x102F19A0`,
   `port/src/romassets_u.s:2948`) — cross-validated three independent ways,
   all agree:**
   - `scripts/filelist.u.csv` row 28: `3086752,797360,assets/music/sfx.tbl` →
     `0x10000000 + 3086752 = 0x102F19A0` exactly.
   - Row 27 (`sfx.ctl`, `3063264,23488`) is contiguous with row 28:
     `3063264 + 23488 = 3086752` — matches `music.c:686`'s assumption
     (`size = &_sfxtblSegmentRomStart - &_sfxctlSegmentRomStart`, i.e. ctl
     immediately precedes tbl in ROM with no gap).
   - `ge007.ld:164-165` (N64 ground truth, untouched) places
     `sfx.ctl.o (.data)` immediately followed by `sfx.tbl.o (.data)` in the
     `musicfiles` segment — same adjacency, independently, from the linker
     script rather than the CSV scanner.
   - Read the real bytes from `data/ge007.ntsc-final.z64` at both offsets
     (this session, ad hoc Python): `sfx.ctl` decodes as a plausible
     `ALBankFile` (`bankCount=1`, one bank offset `0x5ba0` inside the 23488B
     ctl blob); no structural red flag. Byte-level content correctness
     beyond that needs a decode+listen pass, not available in this session.

   Net: the segment-offset math is corroborated by 3 independent sources
   (scanner CSV, N64 linker script, raw ROM read) and is very unlikely to be
   a simple wrong-base bug. Downgrading this suspect's priority.

**Also re-confirmed clean, not previously called out explicitly:** the ADPCM
book coefficients (`ALADPCMBook`, referenced off `ALWaveTable.waveInfo.
adpcmWave.book`) go through `afFixupBook()` (`port/src/romdata.c:704-735`),
which correctly `afRd16`/`afWr16` byte-swaps every predictor coefficient
during the D37 bank re-layout — not just the offset/pointer fields. And
`_bnkfPatchWaveTable()`'s `w->base += table;` (`bnkf.c:140`) is `uintptr_t`
end-to-end post-D201, so no truncation there either.

**Net effect: the M-52 write-up's two recommended suspects are now the
LEAST likely explanations, not the most likely.** The bug is probably
further downstream, in a stage neither this nor the M-52 session traced:
**the resample/pitch pipeline** (`aResampleImpl` / whatever computes a
voice's playback rate from `ALKeyMap`/`unityPitch` — `synallocvoice.c`,
`seqplayer.c`, `synport.c`/equivalent — not yet audited this session). A
wrong pitch/rate calculation would (a) make a correctly-decoded SFX sample
sound like a completely different sound if pitch-shifted far enough to be
mistaken for one, matching "PPK sounds like a melee hit" better than a
literal wrong-sample-content theory would, since sample *content* tracing
(D202 original session) already came back clean; and (b) explain "music
never plays, an SFX-like loop plays instead" if the same mis-pitched/
mis-selected-voice bug applies to sequence-player note events too, not just
raw `sndPlaySfx()` calls. **Recommended next step:** trace one PPK-fire note
event's computed pitch/rate value (another `GE_AUDIOTRACE`-style probe, this
time in the resample-rate / `ALKeyMap` lookup path) against what the N64
build would compute for the same key/velocity, rather than re-auditing the
decode or offset math further. Not yet attempted — no build/run done this
session (static-analysis-only pass); still needs the real listening-pass
sign-off per the standing M-52 note.

**M-53 cont. (build+run, same session) — user rebuilt off the M-53 doc-only
commit and confirmed no behavior change (expected — no code changed yet);
then supplied two new concrete data points that reframe symptom (b) and
partially (a):**

1. **"the looping sound [that plays instead of music] is actually a door
   sound in the attract screen getting looped and continuing to play."**
   This is not a wrong-sample or wrong-pitch bug — it's confirmed to
   literally be a real, correctly-identified SFX (a door slide-loop) that
   fails to stop. Traced the mechanism live with `GE_AUDIOTRACE`
   (`-level_09`, scripted door-interact input): a single player action
   fires a **chained** `sndPlaySfx` sequence — soundIndex 202
   (`METAL_SLIDE_OPEN_SFX`) → 204 (`METAL_SLIDE_LOOP_SFX`) → 203
   (`METAL_SLIDE_CLOSE_SFX`) — via the `do…while` chain in `sndPlaySfx`
   (`snd.c:927-928`, each `ALSound`'s `keyMap->velocityMin`/`keyMin` fields
   double as a "next soundIndex" link; this chain mechanic is stock N64
   design, not a port bug).
   - The game-code side that's supposed to **stop** a stuck door loop —
     `door7F053B10()` (`propobj.c:12835`, marked `//#MATCH`, i.e. decompiled
     byte-exact to the N64 binary, so its logic is ground truth and NOT a
     candidate for the bug itself) — calls `sndDeactivate(door->
     openSoundState)` whenever the door's *open* or *close* sound is still
     playing. `door->openSoundState` is already a real `ALSoundState *`
     in `bondtypes.h:3042` (not a narrowed/truncated field — checked, this
     is NOT another D3x pointer-width bug).
   - So the bug is downstream of this game-logic call: either (a)
     `sndDeactivate`'s posted `AL_SNDP_DEACTIVATE_EVT` (delta 0) isn't
     being serviced/dequeued reliably by the PC event-queue/mixer tick, or
     (b) (more likely given the user specifically saw this originate **in
     the attract screen** and persist **past it, into gameplay**) whatever
     N64 does to reset/clear all playing SFX at an attract-demo → real-game
     mode transition isn't happening equivalently on PC, orphaning the
     loop's voice with no `DoorRecord` left alive to ever call
     `sndDeactivate` on it again. **Not yet located** — haven't found the
     mode-transition audio-reset call (candidate: `sndDeactivateAllSfxByFlag`
     family, `snd.c:996`, or an equivalent full-stop at level-load).
2. **"ammo pick up is one of the knife sounds."** Checked against
   `bondconstants.h`: `PICKUP_AMMO_SFX`=234, `PICKUP_KNIFE_SFX`=233 — adjacent
   indices. Combined with the M-53 PPK report (`GUN_SILPPK_A_SFX`=46 sounding
   like `PUNCH1_SFX`=47, also adjacent but in the OPPOSITE direction), this
   does **not** fit a simple constant index-shift theory (would need to be
   the same sign both times) — leaves either (a) two unrelated single-entry
   bugs, or (b) the "wrong sound" being a badly-mispitched *correct* sample
   that a listener reasonably mistakes for a neighboring, timbrally-similar
   entry (a hard door clank at the wrong pitch could pass for a knife sound;
   a gunshot pitched down for a punch). Not resolved this session — would
   need an actual decoded-audio comparison (dump the decoded PCM for a
   known-bad play and listen/compare band energy against the two
   candidates), which the environment can produce (`GE_AUDIOTRACE` gives the
   exact `base`/`len`/`book` addresses) but wasn't attempted here.

**Still OPEN. Build/run environment confirmed available this session**
(`./build-pc.sh ntsc-final` after `export PATH=/c/msys64/mingw64/bin:$PATH`,
`GE_INPUTSCRIPT` with `"A"`=`GE_CONT_A`, `"Z"`=`GE_CONT_G` per
`port/src/input.c:311` — note **"Z" triggers the door/action interact in
this control scheme in `-level_09`, not weapon fire**; hadn't yet found the
right button/level combo to headless-repro an actual gunshot before running
out of session time). **Next step:** locate and instrument the attract→game
(or menu→level) audio-reset call to confirm/deny the orphaned-voice theory
for symptom (b); separately, dump+listen-compare decoded PCM for one
mis-sounding SFX to settle symptom (a)/(2)'s index-vs-pitch question.

**Follow-up (same session) — checked the level-transition "deactivate all"
sweep, found it's flag-filtered, which narrows the orphaned-voice theory
to a specific, already-catalogued bug family.** `lv.c:1443/1473` calls
`sndDeactivateAllSfxByFlag_1()` at level transitions → `flag=1`
(`SOUND_FLAG_FINAL_IN_SEQUENCE` only) → `sndDeactivateAllSfxByFlag`
(`snd.c:996`) only touches sounds whose `unk3e` has **every** bit in `flag`
set (`(item->unk3e & flag) == flag`). A door's loop-tail sound is very
unlikely to carry `FINAL_IN_SEQUENCE` (it's a middle link in the
open→loop→close chain, not the end), so this sweep would **not** catch an
orphaned door loop even on real N64 — meaning N64 must instead rely on the
attract-mode demo's scripted input naturally executing its "close door"
press before the demo ends, letting `door7F053B10()` clean up the loop
normally. **This reframes the theory:** if the attract-mode demo's
canned input is driven by a fixed *frame count* but the PC build's
sim-time-per-frame differs from N64's (the same `g_GlobalTimerDelta`/
`g_ClockTimer` wall-clock-vs-sim-time family already root-cause-pending
for **D193**, the AI-locomotion-too-slow bug), the demo could get cut off
before its scripted door-close input fires on PC even though it always
does on N64 — orphaning the loop's voice with no code path left to stop
it. **If true, D202(b) (the stuck-loop symptom) may not be a distinct
audio bug at all — it may be a second visible symptom of D193's root
cause once that's found**, not something to fix in the audio code. Worth
confirming/ruling out before spending more time in `snd.c`/`propobj.c`.

**M-54 update — checked the D193-linkage theory's timing mechanics
directly; it's weaker than M-53 framed it, and got two direct
experimental data points instead.**

1. Read `frametiming.c`/`libultra.c`'s timing chain end to end.
   **The ramrom-demo path (`iterate_ramrom_entries_handle_camera_out`,
   `ramromreplay.c:355`) does NOT go through `waitForNextFrame()`** — it
   calls `updateFrameCounters(ramrom_blkbuf_2->speedframes)` directly
   (`ramromreplay.c:400`), feeding the **recorded** N64 speedframes value,
   not a value derived from PC wall-clock elapsed time. The D155
   catch-up clamp (`FRAMETIMING_PORT_MAX_CATCHUP`) lives inside
   `waitForNextFrame()` and is irrelevant here. The call site
   (`boss.c:502-533`) gates *when* this fires on real elapsed ticks
   (`mainTickElapsed >= MAIN_LOOP_TICK_INTERVAL`) but that gate
   self-throttles correctly even if `gfxFrameMsgQ`'s retrace messages
   back up during a stall (checked: `osCreateMesgQueue(&gfxFrameMsgQ,
   ..., 32)`, `init.c:236` — 32-deep, so a stall *can* queue up dozens
   of retrace messages) — the very first drained message after a stall
   recomputes a big `mainTickElapsed` and fires exactly once, which
   immediately refreshes `copy_of_osgetcount_value_1`, so every
   subsequent backlogged message in the same burst sees a near-zero
   elapsed and does nothing. **No runaway/duplicate advance mechanism
   found.** This weakens (doesn't fully rule out) the D193-linkage
   theory: the demo's sim-time progression looks insulated from PC
   real-time hiccups by design, not just by luck. The D193 tie-in is
   still plausible in principle (if the *count* of real retrace ticks
   over the whole demo differs from N64's, e.g. via `osGetCount()`'s
   scaling formula, D117/D134) but the specific "backlog causes runaway
   catch-up" mechanism I'd hoped to point to isn't there. Downgrading
   this from "leading theory" to "one of several still-open".
2. Extended the existing `GE_AUDIOTRACE` probes: `sndPlaySfx` now also
   logs the `newState` pointer per `soundIndex` link (not just the
   `ALSound*`), and `sndDeactivate` now logs every call with its `state`
   arg (`snd.c`, both `#ifdef PORT`/`getenv("GE_AUDIOTRACE")`-gated,
   still uncommitted). Ran two headless captures
   (`GE_AUDIOTRACE=1 GE_D87=1 ./build-pc/ge007.x86_64.exe`, no level arg,
   no input, 100s then 240s, idling at the front end so the >=30s idle
   timer — `front.c:2493`, `MENU_TIMER >= 1801` at NTSC 60fps — kicks
   attract mode on). Findings:
   - **`sndDeactivate` DOES fire and DOES work** when called: trace
     shows `sndPlaySfx: soundIndex=260 -> newState=...706f4e18` followed
     later by `sndDeactivate: state=...706f4e18 (non-null)`, and that
     same `ALSoundState*` slot gets legitimately reused for a new
     `soundIndex` right after. This rules out "the event-queue doesn't
     service `AL_SNDP_DEACTIVATE_EVT` reliably" (branch (a) from the
     M-53 writeup) as a *general* mechanism — deactivation is not
     structurally broken.
   - Both captures only ever logged front-end/menu-click `soundIndex`
     values (258, 260, 111, 232, 109) — despite `GE_D87` confirming
     `iterate_ramrom_entries_handle_camera_out`/`ramrom_replay_handler`
     WERE firing repeatedly (159 D87 log lines in the 240s run, i.e. the
     attract-mode camera flythrough demo was genuinely playing), **no
     door-chain soundIndex (202/203/204) appeared in either capture.**
     Root cause of the miss, found by reading `ramromreplay.c:631-634`:
     which of the 14 `ramrom_table[]` demo files plays is
     `randomGetNext() % i` — **randomly selected per attract cycle**, not
     a fixed sequence. A short capture has no guarantee of landing on a
     demo that triggers a door open/loop at all (most of the 14 clips
     — Dam/Facility/Runway/Silo/Frigate/Train camera paths — may not
     pass a metal sliding door in-frame). Neither capture is long enough
     to be conclusive either way for symptom (b); this needs either (a)
     a much longer unattended capture (several attract cycles, each
     clip's `totaltime_ms` likely 30-90s, so plausibly 10+ min for good
     odds of hitting a door-bearing clip), or (b) a temporary
     `PORT`-gated env-var override pinning `ramrom_table` selection to a
     specific known door-bearing index for fast, deterministic repro
     (not yet added).
   - Separately confirmed the `romdataFixupMusicSeqTable: seqCount 63
     exceeds blob capacity 1` `[ERROR]` log line seen at boot is a
     **known, benign, single-fire false alarm** — it's the deliberate
     D35 header-only peek (`music.c:731-736`, buffer intentionally sized
     16 bytes to decode just the `seqCount` field before the real,
     correctly-sized allocation+decode at `music.c:738-742`) — confirmed
     it fires exactly once per run and the real table load that follows
     doesn't. Not a new lead; noting it here only so a future session
     doesn't re-flag it as one.

**M-54 cont. — got the live repro, and it's NOT a door.** Re-ran headless
with a `GE_INPUTSCRIPT` pressing A every second for the first ~20s (to
clear the Rare/Nintendo/legal/cast-intro screens fast) then idling, 280s
total. Landed on the `ramrom_Train` attract demo (confirmed: `GE_D87`
shows `iterate_ramrom_entries_handle_camera_out`/`ramrom_replay_handler`
actively firing — real attract-mode canned-input playback, not the boot
cinematic, which is a separate unrelated state machine in `front.c`).
`audiotrace.log` (full, unedited):

```
soundIndex=64 -> newState=706f4db0 flags=2   (TRAIN_GO_SFX, first voice)
soundIndex=64 -> newState=706f4f50 flags=2   (TRAIN_GO_SFX, second voice)
...
sndDeactivate: state=706f4e80 (non-null)
sndDeactivate: state=706f4ee8 (non-null)
```

`soundIndex=64` = `TRAIN_GO_SFX` (`bondconstants.h`), `flags=2` =
`SOUND_FLAG_LOOPED` — the **only** looped sound requested in the entire
run. **Neither `706f4db0` nor `706f4f50` ever appears in a
`sndDeactivate` line, anywhere in the log** — confirmed by grepping the
full file for both pointers. Meanwhile two *other*, non-looped states
(`706f4e80`, `706f4ee8`) DO get swept and deactivated, at exactly the
point the demo cycle ends and the sequence rolls back to the boot logos
(`RARELOGO_SFX`/`RARELOGO_FAINT_SFX`, indices 258/260, reappear
immediately after). **This is a direct trace-level confirmation of the
M-53 "Follow-up" theory**: whatever cleanup sweep runs at the
demo-cycle/mode-transition boundary deactivates some voices but
specifically skips the looped one(s) — consistent with
`sndDeactivateAllSfxByFlag`'s `FINAL_IN_SEQUENCE`-only flag filter
(`snd.c:996`, see M-53 above) never matching a genuinely-looped ambience
voice. Reframes the bug from "door-specific" to **"any `SOUND_FLAG_
LOOPED` voice started during a `ramrom` attract demo has no code path
that ever stops it once the demo cycle moves on"** — the door report and
this train report are the same bug, different SFX.

**New candidate mechanism for why N64 doesn't audibly show this**:
`src/libultra/audio/synallocvoice.c`'s `_allocatePVoice`/`alSynAllocVoice`
implement real N64 hardware **voice-stealing** — a small fixed pool of
physical `PVoice`s (`ALSynConfig.maxPVoices`, set from
`MUSIC_SYN_CONFIG_MAX_P_VOICES` in `music.c:772`, unchanged decompiled
value) where a new voice request silently steals and ramps out the
lowest-priority already-playing physical voice once the pool is full
(lines 100-131). On real hardware this would eventually silence an
orphaned looped voice anyway, once enough *other* sounds compete for
the same small voice pool — even though its `ALSoundState` bookkeeping
never gets told to stop. **Not yet checked**: whether the PC port's
mixer (`port/src/mixer.c`) and the synth's free/alloc/lame-list
machinery actually enforce the same `maxPVoices` cap end-to-end, or
whether something in the Phase-3 PC mixer path effectively gives every
voice a free physical channel (never triggers stealing), which would
make an orphaned loop audible forever on PC even in scenes where N64
would eventually have silently stolen it away. This is the most
promising next-step lead — cheaper to check (audit `mixer.c`'s voice
dispatch / count) than adding more ramrom-timing probes.

**Still OPEN.** Next session, in priority order: (1) audit whether
`port/src/mixer.c` honors `maxPVoices`/voice-stealing identically to
`synallocvoice.c`'s original algorithm — if it doesn't, that's the fix,
and it would explain both the door and train loop reports without
touching `snd.c`/`propobj.c`/`front.c` game logic at all; (2) if voice
stealing checks out fine, look at whether N64 has some OTHER explicit
stop call for these ambience loops that isn't in the `Dxx`-traced code
paths yet (e.g. a `lvlManageMpGame`-adjacent full-audio-reset at
attract-cycle boundaries not yet located). Symptom (a)/(2)
(wrong-sample-per-weapon) still untouched — still needs the
decoded-PCM listen-compare. The `GE_AUDIOTRACE` trace additions to
`snd.c` (now covering `sndPlaySfx`'s `newState=`/`flags=` and every
`sndDeactivate` call) are left in place, uncommitted, for reuse.

**M-55 — priority-(1) voice-stealing audit done; result is negative, and
narrows the bug further.** Static/read-only session (local Qwen `triage`
dispatch for the grep/citation legwork, spot-checked line-for-line
against the real files before trusting it).

- **`port/src/mixer.c` doesn't need to honor `maxPVoices` — it never
  participates in voice allocation at all.** Grepped it for
  `maxPVoices`/`pAllocList`/`pFreeList`/`pLameList`/`numPVoices`: zero
  hits. `mixer.c` (~436 lines) only implements the low-level `aXxx`
  RSP-acmd DSP commands (envmixer/resample/etc `Impl` functions) that
  `synallocvoice.c`'s already-allocated `PVoice`s get pointed at — the
  lame-list/free-list/steal-lowest-priority bookkeeping (D202 M-54's
  suspect) lives entirely in **unmodified ground-truth**
  `src/libultra/audio/synallocvoice.c` (`alSynAllocVoice`/
  `_allocatePVoice`, lines 27-131, byte-identical to decomp) plus
  `src/libultra/audio/synthesizer.c`'s `alSynNew` (`numPVoices =
  c->maxPVoices`, `music.c:772`'s `MUSIC_SYN_CONFIG_MAX_P_VOICES`,
  unchanged). **The M-54 "maybe the PC mixer gives every voice a free
  channel" theory is ruled out — there's no PC-specific voice-pool code
  to have that bug in.** Since this pool logic is unmodified decomp
  code, its steal behaviour on PC is provably identical to N64's.
- **Priority (2) has an answer too, and it's negative in a useful way.**
  Traced whether the `ALSynth` driver (or anything else audio-wide) gets
  reset/reinitialized at the attract-demo → boot-logo transition:
  `alInit` (`src/libultra/audio/sl.c:28-33`) is a **hard singleton**
  (`if (!alGlobals) { alSynNew(...); }`) called exactly once, transitively
  from `bossEntry()` → `musicSeqPlayerInit()` (`src/boss.c:312`,
  `src/music.c:648`) → `amCreateAudioManager()` (`src/music.c:781`) →
  `alInit()` (`src/audi.c:374`/`378`) → `alSynNew()`. The only teardown,
  `alClose()` (`src/audi.c:481`), fires solely on audio-thread
  `MAIN_QUIT_MESSAGE` (process exit), not at a demo boundary. The actual
  attract-demo stop path, `stop_demo_playback()`
  (`src/game/ramromreplay.c:604-616`), touches only joystick-playback
  bookkeeping (`joySetPlaybackFunc`/`joySetContDataIndex`/
  `ramrom_demo_related_3`/`is_ramrom_flag`) — **no audio call of any
  kind.** All of `sl.c`, `audi.c`, `boss.c` here are unmodified
  ground-truth, so **N64 has exactly the same absence of a
  demo-boundary audio reset** — confirming there is no missing-on-PC
  system-reset bug to find in this direction either.
- **Net effect: both of M-54's priority leads are now closed, and
  neither explains the bug.** This pushes the M-53 "Follow-up" theory
  back to the front — a genuinely `SOUND_FLAG_LOOPED` voice has *no*
  code path that stops it at a `ramrom` attract-cycle boundary on
  **either** platform; whatever the real fix is, it is a **pre-existing
  N64 behavior being newly exposed**, not a PC regression, and per rule
  #2 (game logic is ground truth) is very unlikely to be something we
  should "fix" by adding a stop call the original game never had. The
  live-repro data (M-54 cont.) already showed this exact voice audibly
  looping forever in the PC build; whether real N64 hardware's `PVoice`
  stealing (now confirmed byte-identical logic) actually does silence it
  in practice depends on whether some *other* sound eventually claims
  that priority slot during/after the demo cycle — untested acoustically
  either way.

**Still OPEN. Next session:** stop auditing the voice-pool/reset code —
both candidate mechanisms are now cross-validated clean/absent on both
platforms. The two live options are (a) accept this as a real, harmless-
on-N64-in-practice edge case (the orphaned voice gets stolen once enough
other SFX compete for the small `PVoice` pool during normal gameplay,
just not during an idle unattended attract-mode capture) and downgrade
D202(b) priority accordingly, or (b) do the acoustic check directly —
run a longer capture (10+ min, several attract cycles) or a temporary
`PORT`-gated `ramrom_table` index override to force a door/train demo
reliably, then listen past the demo boundary to hear whether the loop
in fact does eventually cut out once other SFX start competing for
voices (would confirm (a) and close D202(b) as WONTFIX/expected).
Symptom (a)/(2) (wrong-sample-per-weapon, PPK→PUNCH1) is still fully
untouched and is now the higher-value remaining D202 thread — needs the
decoded-PCM listen-compare, not more voice-pool tracing.

**M-56 — D202(a) got the decoded-PCM listen-compare, live user confirmation,
and the index/pointer/ROM-data chain is now independently proven byte-exact
against the real ROM (not just "ruled out" by code audit). Root cause is
narrowed to the actual decode/mix runtime.**

- Added a temporary `GE_AUDIODUMP=1` probe (`port/src/audio.c`,
  `audioSetNextBuffer`) that raw-dumps every mixed s16 stereo buffer
  reaching the SDL device to `audiodump.raw` — the true final output, same
  path as what plays in-game. Ran a headless repro (`-level_33` Dam,
  `GE_INPUTSCRIPT` holding aim+fire for 8 trigger-pulls starting ~10s in),
  converted the dump to WAV, located the transients via a 50ms-window RMS
  scan, and sent the clip to the user. **Exact repro command** (run from
  repo root, `export PATH=/c/msys64/mingw64/bin:$PATH` first):
  ```
  rm -f audiotrace.log audiodump.raw
  GE_AUDIODUMP=1 GE_AUDIOTRACE=1 GE_INPUTSCRIPT="600:R,Z;601:R,Z;650:R,Z;651:R,Z;700:R,Z;701:R,Z;750:R,Z;751:R,Z;800:R,Z;801:R,Z;850:R,Z;851:R,Z;900:R,Z;901:R,Z;950:R,Z;951:R,Z" \
    timeout 30 ./build-pc/ge007.x86_64.exe -level_33
  ```
  `audiotrace.log` (per-call soundIndex/pointer trace) and `audiodump.raw`
  (raw s16le stereo @ 22050Hz, no header — wrap with Python's `wave` module
  or `ffmpeg -f s16le -ar 22050 -ac 2 -i audiodump.raw out.wav`) land in the
  repo root. **User confirmation: "it's playing
  the ricochet sound effect and the slapping melee attack sound effect on
  each gunshot"** — i.e. two distinct wrong-but-real game sounds, not
  silence/noise/a single mislabeled clip.
- The `GE_AUDIOTRACE` probe (already in tree) on that same run showed the
  fire event genuinely requests **soundIndex=46 (`GUN_SILPPK_A_SFX`)**
  paired with **soundIndex=122 (`CART_SPENT_SFX`, the shell-casing sound —
  `gunfire.c:5582`)** — both legitimate, correct call sites for this
  weapon/action, resolving to non-null, structurally-valid
  sound/keyMap/wavetable/book pointers. This is a **live, real-gameplay
  confirmation** of what M-52 had only checked structurally.
- **Independently re-parsed the raw ROM bytes from scratch in Python**
  (no reuse of any project code — a from-first-principles walk of
  `ALBankFile → ALBank → ALInstrument → ALSound → ALWaveTable` against
  `data/ge007.ntsc-final.z64` at the `_sfxctlSegmentRomStart`/
  `_sfxtblSegmentRomStart` addresses) to check the PC port's custom
  `port/src/romdata.c` re-layout tool (`romdataAudioBankPcSize`/
  `romdataFixupAudioBank`, the `afFixup*` family — hand-written PORT code,
  not decomp, and the one part of this chain not previously scrutinized
  this closely). **Result: exact match.** Index 46's wavetable resolves to
  `base=0x103128A0 len=2242`, index 122's to `base=0x10351DF8 len=2196` —
  **both bit-for-bit identical to the live runtime trace.** Also pulled
  each sound's ADPCM book (order/npredictors/coefficients) and `ALKeyMap`
  directly from ROM: indices 46/47/122 all have distinct, non-aliased book
  coefficients (not a copy-paste/dedup collision), though 46 and 47
  (`PUNCH1_SFX`) do share an identical `keyBase=54/detune=50` keymap —
  coincidental (both are attack-transient sounds tuned the same), not by
  itself an index confusion.
- **This closes out the index/pointer/ROM-offset chain for good** — it is
  now proven correct by independent reconstruction, not just "audited and
  believed clean" (M-52's original phrasing). Since each sound's raw
  ROM-embedded ADPCM data is objectively distinct (different books), the
  wrong-sounding output can only be happening in the **decode or mixing
  runtime itself** — i.e. the actually executed `port/src/mixer.c` path,
  not the data feeding it.
- **New leading suspect, not yet tested**: **D199's flagged risk** — the
  Phase-3 mixer's `aSetBuffer` **persistent-DMEM-context inference**
  (macro-swap `aXxx`→`aXxxImpl` execution with no RSP disassembly to
  verify against, `findings.md` D199) — if two voices active in the same
  audio frame (e.g. the continuous Dam wave-ambience loop already seen
  looping throughout this capture, soundIndex 65/67, plus the fire SFX)
  end up sharing/clobbering the same inferred DMEM decode-context instead
  of each getting its own, one voice's ADPCM decoder state could bleed
  into another's output — producing exactly this "correct data in, wrong
  but real-sounding-like-something-else data out" symptom, deterministically
  per concurrent-voice pairing. This is a hypothesis, not yet verified.

**Still OPEN. Next session:** audit `port/src/mixer.c`'s `aSetBuffer`/DMEM
context handling for whether concurrently-active voices (not just
back-to-back `Impl` calls for the *same* voice) get isolated decode state —
trace which physical DMEM buffer/context each of the fire SFX and the
continuous ambience loop actually uses per frame during the same repro
(`GE_AUDIODUMP`+`GE_AUDIOTRACE`, `-level_33`, aim+fire script above already
gives a clean repro). Do NOT re-check the ROM offset/pointer/index chain
again — independently re-derived byte-exact this session, closed for good.

**M-57 — the M-56 concurrent-voice-DMEM hypothesis is FALSIFIED by a live
opcode-level trace, not just re-audited. A new (currently dormant)
pointer-truncation bug was found in the same family as D198/D201, and a
full sibling audit was produced, but NEITHER explains D202 yet — still
OPEN.** Interactive session, build+run environment confirmed working
(`export PATH=/c/msys64/mingw64/bin:$PATH` from repo root).

- Added a temporary `GE_MIXERTRACE=1` probe (`port/src/mixer.c`) that logs
  every `aSetBuffer`/`aLoadADPCM`/`aADPCMdec`/`aResample`/`aEnvMixer` call
  with its DMEM addresses and state pointer, in call order, to
  `mixertrace.log`. Reran the exact M-56 repro
  (`GE_AUDIODUMP=1 GE_AUDIOTRACE=1 GE_MIXERTRACE=1 GE_INPUTSCRIPT=... -level_33`)
  and read the trace around the soundIndex 46/122 fire event (cross-referenced
  via `audiotrace.log` and the `alLoadParam` wire probe's `audiotrace_wire.log`,
  both already in tree from M-52/M-56).
- **Result: every voice's full chain — `aLoadADPCM` → `aSetBuffer`(decode) →
  `aADPCMdec` → `aSetBuffer`(resample) → `aResample` → `aSetBuffer`(main) →
  `aSetBuffer`(aux) → `aEnvMixer` — runs to completion, strictly sequentially,
  before the next voice's `aLoadADPCM` starts.** No interleaving between
  voices was observed anywhere in ~62k logged opcodes spanning the fire
  event. The single shared `sDmem`/`sAdpcmTable`/`sCtx` globals are safe
  under this call pattern for exactly the same reason the real RSP's single
  physical DMEM is safe: one voice's full pipeline finishes and its result is
  accumulated into the persistent `AL_MAIN_L_OUT`/`AL_AUX_*` buffers before
  the scratch region (`AL_DECODER_IN`=0) is reused by the next voice. **D199's
  "concurrent-voice DMEM clobber" risk is closed, negative** — falsified by
  direct trace, not just re-reasoned about.
- Also noticed `a->table->len` visibly changes (2242 → 2241) between the
  first and later `alLoadParam` wire-probe log lines for the *same* shared
  `ALWaveTable`. Traced this to `load.c:396`
  (`a->table->len = ADPCMFBYTES * ((s32)(a->table->len/ADPCMFBYTES));`) —
  **unmodified N64 ground-truth code**, rounds the table length down to a
  whole-ADPCM-frame multiple. It mutates the shared table in place but is
  idempotent (rounds to the same value on every call after the first) and
  behaves identically on N64. **Not a bug — ruled out.**
- **New finding (not yet root-caused as D202's cause, likely a separate
  latent issue): `src/libultra/audio/load.c` lines 111, 173, 259, 320, 382,
  438 all do `f->memin = (s32) f->table->base + ...` / `a->memin = (s32)
  a->table->base` — narrowing a real 64-bit `u8 *base` pointer
  (`ALWaveTable.base`, `include/PR/libaudio.h:217`) into the `s32 memin`
  field** (matches N64 ground truth's struct layout, `synthInternals.h`).
  This is the same truncation class as the already-fixed D198
  (`K0_TO_PHYS`) and D201 (`bnkf.c` relocation offsets), but **currently
  dormant in this session's build**: the observed `ALWaveTable.base` values
  (e.g. `0x103128a0`) sit comfortably under 4GB (this exe loads its ROM-data
  blob at a low, non-ASLR'd address), so the `(s32)` truncation followed by
  `osPhysicalToVirtual`'s `u32`→pointer zero-extension round-trips exactly —
  no data corruption observed in this run. Flagging as a **hardening item**,
  not a confirmed D202 cause; would need a build/environment where the
  ROM-data blob's heap address exceeds 4GB to prove it live. Not fixed this
  session (out of scope for this investigation; needs its own D-number if
  picked up).
- Delegated a read-only sibling-audit of this cast pattern across the whole
  audio subsystem to the local Qwen `repo-mapper` agent (grep-only, no
  edits, checked against real files). Full hit list, confirmed genuine
  pointer-truncation sites beyond `load.c`:
  - `src/music.c:879,1073,1266` — `(u8*)((t3 + (s32)thing.seqData) -
    trackSizeBytes)` — same class, narrows a seq-data pointer before a ROM→RAM
    copy. Worth checking next alongside `load.c`.
  - `src/libultra/audio/heapinit.c:26` — narrows the audio heap `base`
    pointer to `s32` but immediately masks with `& AL_CACHE_ALIGN` (only
    alignment low-bits survive) — low risk in practice, still worth a
    `uintptr_t` pass for hygiene.
  - Every other `(s32)`/`(int)` cast in `src/libultra/audio/*.c` and
    `src/libultrare/audio/*.c` (`resample.c`, `reverb.c`, `env.c`,
    `seqplayer.c`, `csplayer.c`, `save.c`, `synthesizer.c`, `drvrNew.c`)
    narrows a genuine small integer (byte/sample counts, pitch ratios,
    indices) — not pointer truncation, nothing to fix.

**M-58 — priority (1) from the M-57 write-up (decoded-PCM-per-voice-slot dump)
executed. Result: address resolution AND the ADPCM decode math are BOTH
independently verified bit-exact correct for a live fire-SFX occurrence.
D202 is NOT in `aLoadBuffer`'s DMA/address plumbing and NOT in
`aADPCMdecImpl`'s decode arithmetic. Still OPEN — the bug is downstream of
decode or in voice/output routing.**

- Extended the `GE_MIXERTRACE` probe (env-gated, zero cost when unset)
  three ways: (a) `port/src/mixer.c` `aLoadBufferImpl`/`aLoadADPCMImpl`/
  `aADPCMdecImpl` now log the actual DMEM source bytes, the loaded ADPCM
  book coefficients, and the first decoded 16-sample frame (skipping the
  16-sample history lead-in) into the *same* `mixertrace.log` as the M-57
  opcode trace; (b) `src/libultra/audio/load.c` `_decodeChunk` logs
  `[DMAREQ] filter=<ALLoadFilter*> memin=<addr>` right before calling
  `f->dma(...)`, and `alLoadParam`'s existing `AL_FILTER_SET_WAVETABLE` case
  now also emits a `[BINDTABLE] filter=<ptr> <- table=... base=...` line into
  `mixertrace.log` (previously only in the separate `audiotrace_wire.log`)
  so table-bind, DMA, and decode events for the same physical voice filter
  interleave in one chronologically-ordered file instead of needing
  cross-file correlation by line count (a real mistake made mid-session:
  comparing `wavetable->base` from one run's `audiotrace.log` against
  `aLoadBuffer` addresses from a *different* run's `mixertrace.log` produced
  a spurious "addresses never match" false alarm — resolved by getting both
  probes into one file in one run); (c) `src/audi.c` `amDmaCallback` now
  logs `[DMAHIT]`/`[DMAMISS]` with the input ROM-side address, matched
  buffer's `startAddr`, and the returned staging-buffer address.
- Reran the exact M-56/M-57 repro
  (`GE_AUDIODUMP=1 GE_AUDIOTRACE=1 GE_MIXERTRACE=1 GE_INPUTSCRIPT=... -level_33`,
  see M-56 for the exact command). Picked one live fire event
  (`soundIndex=46`, `wavetable->base=0x103128a0`) and followed its physical
  voice filter (`ALLoadFilter*`) end to end in one linear read of
  `mixertrace.log`:
  - `[BINDTABLE] filter=...706d7230 <- table=...706c4cc8 base=0x103128a0`
  - `[DMAREQ] filter=...706d7230 memin=0x103128a0` — **exactly matches** the
    bound table's own base. No cross-voice/cross-heap address confusion.
  - `[DMAMISS] addr=0x103128a0 ... ret=0x706dd490` — legitimate cold DMA
    (N64-analogous ROM→staging-buffer copy via `port/src/libultra.c`'s
    `piServiceDma`, which correctly `memcpy`s from the cart-mapped `.z64`
    region at `srcPA=0x103128a0`, a real, valid cart address under
    `romdata.c`'s mapping — not a stray heap pointer as first suspected
    mid-session before the single-file trace fix above).
  - `[LOADBUF] dram=0x706dd490 ... bytes=900de113224092ecde90f602424ae1bf` —
    the exact ADPCM-encoded source bytes now loaded into DMEM.
  - `[ADPCMDEC] ... book0=-881` and the dumped book coefficients match
    `sndPlaySfx`'s own `book=00000000706c4ca0` trace for soundIndex 46
    (`+8` byte print-pointer offset, same object) — the correct predictor
    table for this sound, not a stale/aliased one.
  - `[PCMOUT] book0=-881 first-decoded-frame[0..15]=0,-1536,-2475,-1165,...`
- **Independently hand-decoded the same 9 input bytes against the same
  dumped book coefficients using the textbook VADPCM algorithm** (order-2
  predictor, `header byte -> shift+tableIndex`, nibble sign-extension,
  cumulative predictor sum), entirely outside this codebase, no reuse of
  `mixer.c`'s logic. First three samples worked by hand: `0`, `-1536`,
  `-2475` — **bit-exact match** against the C implementation's dumped
  output. This independently confirms `aADPCMdecImpl`'s math is a correct
  VADPCM decoder, not just internally self-consistent.
- Also noticed (not a bug, recorded for the record): `sAdpcmTable` is a
  single static global reused across sound switches without zeroing unused
  predictor slots, so predictor indices beyond a sound's own `npredictors`
  hold stale coefficients from a previous sound's book load. Harmless in
  practice — a sound's own ADPCM-encoded stream only ever emits
  `tableIndex` values within its own encoded `npredictors` range (confirmed
  here: soundIndex 46's book is `bookSize=32` bytes = 1 predictor via
  `2*order*npredictors*ADPCMVSIZE`, and its stream's header nibble was `0`,
  the only valid index) — but it is fragile precisely because it depends on
  the encoder never emitting an out-of-range index; a future corrupted/
  malformed bank could read garbage predictor coefficients silently. Not
  pursued further this session (not shown to relate to D202).
- **This closes out the entire `aSetBuffer`→`aLoadBuffer`→`aLoadADPCM`→
  `aADPCMdec` chain for the cases actually traced.** Combined with M-56
  (ROM data/index chain closed) and M-57 (DMEM concurrency closed), the
  first three links of the pipeline — request routing, ROM data integrity,
  and decode — are now all independently verified correct for at least one
  live fire-SFX instance. The mid-session false alarm (see above) is a
  useful lesson: **always put every new correlated probe into the same
  linearly-ordered trace file**; cross-file correlation by line count
  across separate runs is not valid even when the repro is "the same"
  command, because per-run nondeterminism (already catalogued, D117) shifts
  line counts and, this session, briefly looked exactly like a real bug.

**Still OPEN. Next session, in priority order:** (1) the remaining untraced
links are **resample → envmix → final-mix routing**, and **physical-voice
(PVoice) allocation/identity** — whether the correctly-decoded PCM for one
logical `ALSoundState` ends up accumulated into the *wrong* voice's/output's
persistent envelope-mixer volume-ramp state (`sVol`/`saved->t[]` in
`aEnvMixerImpl`) or gets attributed to the wrong output buffer, which would
produce exactly "correct audio computed, wrong audio heard." Trace the same
way as this session (one physical filter/voice followed end-to-end in one
`mixertrace.log`, `[ENVMIX] state=...` pointer identity checked against which
logical sound it's supposed to belong to) rather than assuming and re-deriving.
(2) If that's clean too, suspect PVoice allocation/identity higher up (the
synth's voice-slot assignment, independent of DMEM/decode). (3) Only after
(1)/(2): consider fixing the `load.c`/`music.c` pointer-truncation family as
a hardening pass (own D-number) — still not shown to cause D202. Do NOT
re-check the ROM offset/index/pointer chain (M-56), DMEM/`aSetBuffer`
concurrent-voice sharing (M-57), or the decode arithmetic/addressing
(M-58, this session, verified bit-exact) — all closed.

**M-59 — likely resolution: D202 is NOT a decode/mixer bug. The "ricochet +
melee slap" audio the M-56 listening test heard is a SEPARATE, entirely
legitimate SFX request made by unmodified ground-truth game logic on every
shot in this specific repro, because the scripted aim direction hits a wall/
prop, not open air. Recommend a follow-up open-air listening test before
resuming the M-58 priority-1 envmix/PVoice trace.**

- Two more concrete checks, continuing directly from M-58's decode-chain
  verification (which stays valid and closed): (a) independently confirmed
  the actual DMEM-loaded ADPCM byte string M-58 dumped for soundIndex 46
  (`900de113224092ecde90f602424ae1bf...`) is byte-for-byte identical to the
  **raw, untouched `data/ge007.ntsc-final.z64` file** at the corresponding
  cart offset (`0x03128a0`, i.e. `base 0x103128a0` minus `CART_BASE
  0x10000000`) — read directly with a throwaway Python script, no project
  code reused. Traced why this address is meaningful at all:
  `_sfxtblSegmentRomStart` (`port/src/romassets_u.s:2948`) is a linker
  constant equal to the literal cart address `0x102F19A0`, used as-is by
  `alBnkfNew`'s `table` argument (`src/music.c:703`) → `_bnkfPatchWaveTable`
  (`src/libultra/audio/bnkf.c:140`, `w->base += table`) — i.e. GE's
  wavetable sample data is **never relaid-out or copied by
  `port/src/romdata.c`'s bank-structure fixup at all**; `base` ends up
  pointing straight into the same live-mapped `.z64` image `port/src/audi.c`
  reads cart data from generally (`docs/dev/findings.md` cart-mapping notes,
  M-56). This closes the last theoretical gap in the M-56/M-58 chain: ROM
  data integrity is now verified at the raw file-byte level, not just
  structurally.
  - With the entire request→ROM-offset→DMA→decode chain now proven correct
    end to end, (b) re-examined the *complete* `audiotrace.log` from the
    M-58 repro (`-level_33` Dam, aim+fire `GE_INPUTSCRIPT`) rather than
    filtering to just soundIndex 46/122, and cross-referenced every other
    soundIndex against `src/bondconstants.h`'s `"NN_NAME_SFX"` string table
    (which encodes the index directly in the name — e.g. `"46_
    GUN_SILPPK_A_SFX"`). **Every single `soundIndex=46` (fire) request in
    the trace is immediately followed by a `soundIndex` from `{23, 24, 25,
    37}`** — `"23_RICO_6_TAJ_A_SFX"`, `"24_RICO_6_TAJ_B_SFX"`,
    `"25_RICO_6_TAJ_C_SFX"`, `"37_RICO_5_C_SFX"` — i.e. **ricochet/bullet-
    impact sounds**, cycling per shot, plus the already-known `122` =
    `"122_CART_SPENT_SFX"` (cartridge eject).
  - Found the call site: `src/game/gunfire.c`
    `recall_joy2_hits_edit_detail_edit_flag()` (line ~2270, unmodified
    ground-truth decomp function, original obfuscated name) is the bullet
    hit-detection handler. For a hitscan that strikes a non-character prop
    (`prop->type != PROP_TYPE_CHR && != PROP_TYPE_VIEWER`, i.e. scenery/
    wall, not an enemy), it unconditionally does:
    `ricochet_sounds_small_copy = ricochet_sounds_small;
    sndPlaySfx(g_musicSfxBufferPtr,
    ricochet_sounds_small_copy.arr[randomGetNext() % 20], sound_state);`
    — **every bullet that hits a wall/prop plays a randomly-chosen ricochet
    SFX from a 20-entry table, by design, on real N64 too.** This is not a
    port artifact; it is exactly what GoldenEye does when you shoot a wall.
  - **Conclusion**: the M-52/M-56 repro's fixed aim direction
    (`-level_33`, default spawn facing, held-down aim+fire) has the player
    shooting directly into a nearby wall/prop the entire time, so *every*
    trigger pull legitimately plays fire (46) + cartridge-eject (122) +
    a random ricochet impact (23/24/25/37) simultaneously — three real,
    correct SFX per shot, not one wrong one. The M-52 playtest's original
    verbal description ("sounds like a melee/punch hit") and the M-56
    listening test's ("ricochet sound effect and the slapping melee attack
    sound effect") both match a ricochet impact's percussive/metallic
    character far better than a coincidental decode bug that happens to
    always produce two *other real, nameable* game sounds. No corrupted or
    misattributed audio has been found anywhere in the chain despite three
    full sessions (M-56/M-57/M-58) of independent, bit-exact and byte-exact
    verification at every single link — address resolution, ROM data
    integrity, book/coefficient selection, ADPCM decode arithmetic, and now
    the request pattern itself.
  - **This does not yet prove the gunfire (46) sound itself is
    perceptually correct/prominent** — only that it is being computed
    correctly and is not what's "wrong" in what the tester heard. It's
    possible the fire SFX is simply quiet/subtle by design and gets
    acoustically masked by the louder ricochet transient, which would be
    expected N64 behavior too, or (lower probability, unverified) there is
    a genuine separate volume/mix-balance issue between simultaneous
    voices. Not fixed or further diagnosed this session — needs a
    clean listening test to settle it (see below).

**Recommended next session (before resuming the M-58 priority-1 envmix/
PVoice trace, which may now be unnecessary): re-run the exact same
`GE_AUDIODUMP` capture but with an `GE_INPUTSCRIPT` aim direction that
points at open air / the sky instead of a wall (or simply hold fire without
the aim-lock at a fixed wall-facing spawn), confirm via `audiotrace.log`
that NO `ricochet_sounds_small`-family soundIndex fires, and have the user
listen to that clip specifically. If the fire (46) + cartridge (122) sounds
alone are now clearly audible, correct, and recognizable as gunfire, D202
should be closed as a repro artifact (aim-at-wall), not a bug — with a note
that the original M-52 playtest report may itself have simply been the
player firing at nearby geometry during normal play, which is entirely
expected. Only if the open-air clip STILL sounds wrong should the M-58
priority-1 envmix/PVoice-identity trace be resumed.

**Follow-up done same session (M-59 cont.):** turned the character with
`GE_INPUTSCRIPT="1:SRIGHT;400:SNONE;600:R,Z;..."` (sustained analog-stick
turn before the aim+fire sequence) and reran the `GE_AUDIOTRACE`+
`GE_AUDIODUMP` capture on `-level_33`. Result: the *first* shot in this run
requests **only** `soundIndex=46` (fire) + `122` (cartridge) — no
`ricochet_sounds_small` index at all — confirming the turn moved the aim
off whatever wall/prop the original fixed-facing repro was hitting. The
*second* shot (moments later, once the turn brought the aim back toward
nearby geometry) requests `46` + `122` + `23` (`RICO_6_TAJ_A_SFX`) again,
giving a same-run A/B pair. Located both transients in `audiodump.raw` via
a 50ms-window RMS scan (clean shot ≈ t=11.0–11.7s, ricochet shot ≈
t=11.7–12.6s), exported both as WAV (`scratchpad/
d202_clean_shot_no_ricochet.wav` / `scratchpad/d202_shot_with_ricochet.wav`,
not committed — scratch), and sent both to the user for a direct listening
comparison. **Awaiting user confirmation** on whether the ricochet-free
clip sounds like correct, recognizable gunfire — that answer settles
whether D202 closes outright as a repro artifact or the envmix/PVoice trace
needs to resume.

**M-59 result: user says BOTH clips are the ricochet/slap sound, just at
different volumes — the "clean" clip is not clean.** This falsifies the
"aim-at-wall repro artifact" theory as originally framed. Investigated
further:

- The two exported clips both came from `dumppos`-adjacent events with
  soundIndex 46 and a ricochet index at the **same audio-frame position**
  in this run's `audiotrace.log` (a new `dumppos=<byte offset into
  audiodump.raw>` field was added to the existing `GE_AUDIOTRACE` probe —
  `port/src/audio.c` now exposes `audioDumpBytePos()`, an exact running
  byte counter, so future clip extraction can key off a logged sample
  position instead of an RMS-scan guess). Confirmed the fire (46) and
  ricochet requests land at the *same* `dumppos`, i.e. they are requested
  in the same tick and their audio overlaps in time rather than being
  sequential — so the RMS-peak-picking method used for the M-59 clips could
  not actually isolate a ricochet-free window; both exported clips likely
  had some ricochet content, just at different relative volume (distance-
  dependent ricochet gain, or a different ricochet variant per shot).
- To get a **guaranteed** ricochet-free sample, decoded `soundIndex 46`
  **entirely offline**: read `data/ge007.ntsc-final.z64` directly at ROM
  offset `0x03128a0`, len 2242, ran the same hand-verified VADPCM decode
  used in M-58 (order-2, single predictor, coefficients from the M-58
  trace dump) with no live game/mixer involved at all — zero possibility of
  a simultaneous ricochet voice. Rendered to `scratchpad/
  d202_soundindex46_isolated.wav`, sent to the user. **User confirmed: "yeah
  that is the slap effect that in the original game plays when you do a
  melee attack."** This is unambiguous — soundIndex 46's actual ROM sample
  content genuinely is (or matches) the melee-slap SFX, independent of any
  mixing/overlap question.
- **Traced the request chain one level further up, past `sndPlaySfx`, to
  find out why soundIndex 46 is requested for the silenced PPK's fire at
  all.** `bondwalkItemGetSound(item)` (`src/game/gun.c:1345`) returns
  `get_ptr_item_statistics(item)->Sound` — a `u16 Sound` field on the
  per-weapon `WeaponStats` struct (`src/game/gun.h:87`, doc comment:
  "Sound effect played when gun is shot. There are 261 sound effects, or
  0 - 105h."). `get_ptr_item_statistics` (`gun.c:721`) returns
  `gitem_structs[item].item_weapon_stats` — **not a runtime ROM-converted
  pointer**; `gitem_structs[]` (`assets/obseg/gun/gunModelFileRecord.inc.c`)
  is a **static, compiled C data table**, decompiled directly from the
  ROM's `.data` segment (each `GUNFILERECORD(...)` entry pulling in a
  `GUNSTATS(name)` initializer). The silenced PPK's is
  `assets/obseg/gun/wppksil/gunWeaponStat.inc.c`
  (`WeaponStats wppksil_stats = {1.0, 11.0, ..., 0x2E, ...}`, tagged with a
  decompilation RAM-address comment `//D:800326C4`) — **`.Sound` is
  positionally the 14th field, value `0x2E` = decimal **46**.** This is
  unmodified, hand-decompiled ground truth, not a port-side conversion —
  there is no PC-specific fixup in this chain at all (unlike the wavetable
  data, this table is a plain compiled literal, identical on N64 and PC by
  construction).
- **Conclusion: on real N64 hardware, firing the silenced PPK requests
  the exact same soundIndex 46 this port does.** Every link — decompiled
  weapon-stat data, SFX-bank index resolution (M-56/M-59), ROM sample
  bytes (M-58/M-59), and ADPCM decode (M-58) — is now independently
  verified correct and, more importantly, **identical to what N64 hardware
  would do with the same ROM**. If soundIndex 46 truly sounds like a melee
  slap and not a silenced pistol, that is either (a) the original,
  shipped game's actual designed audio (silenced firearms can have a soft,
  percussive "thwack" character that isn't unreasonable to mistake for a
  melee sound, and reusing one SFX asset across two unrelated actions to
  save ROM space is a common technique of this era) — in which case there
  is **no bug anywhere in this port to fix**, this is how GoldenEye 007
  actually sounds; or (b) a very rare decompilation transcription error in
  `wppksil_stats`'s `.Sound` field specifically — but this project's
  entire premise is byte-verified matching against the real ROM's `.data`
  segment (README/AGENTS.md), so an error surviving in a byte-matched
  build would be surprising, though not provably impossible without
  independently re-reading the literal ROM bytes at this struct's true RAM/
  ROM address (not attempted this session — would need the N64 build's
  linker map to translate the `//D:800326C4` RAM comment to a ROM file
  offset).
- **Not fixed, not committed — this is a research conclusion, not a code
  change.** No `port/`, `src/`, or `assets/` change is warranted unless (b)
  above is confirmed; per AGENTS.md, `assets/obseg/gun/wppksil/
  gunWeaponStat.inc.c` is decompiled ROM data (ground truth) and must not
  be "corrected" speculatively.

**Recommended definitive tie-breaker (next session, if the user wants to
keep pursuing this rather than close it):** run the **same ROM** in a
known-good N64 emulator (or, if available, real hardware) and fire the
silenced PPK. If it also sounds like the melee-slap effect there, D202 is
closed for good as a genuine original-game characteristic (or a very
deep, pre-existing GoldenEye quirk) — not a decompilation or port bug, and
no further engineering time should go into it. If the emulator's silenced
PPK sounds like a normal muted gunshot, the bug is almost certainly the
rare decompilation transcription error in (b), and the fix would be a
single-field data correction in `gunWeaponStat.inc.c` guarded by the
project's existing byte-match verification (which would need to still
pass — if it doesn't, this table is confirmed byte-correct after all and
the "bug" is upstream of Bond's silenced-PPK path in some other way not
yet identified).

**M-60 — user explicitly rejects the "ground truth, not a bug" conclusion**
("I know for a fact its the wrong sound playing in the PC port
specifically") and supplies a real reference clip instead of an emulator
A/B: `scratchpad/B00I00S2D.wav` (untracked/scratch), a genuine recording of
the silenced PPK's real fire sound, dropped directly by the user (source
not yet stated — presumably real hardware/video capture, not this repo).
Measured: mono, 16-bit, 16 kHz, 3264 samples, 0.204 s. Its 10-bucket RMS
envelope (`12645, 11548, 15152, 16172, 14006, 12766, 15106, 15669, 8280,
6608, 8032, 9409, 9812, 8849, 7745, 6819, 4299, 1477, 496, 176` over 20
buckets) is **sustained** — energy stays in the 6000-16000 range for
roughly the first 80% of the clip's duration before decaying in the last
~20% — structurally different from `d202_soundindex46_isolated.wav`'s
envelope, which peaks early (`11209, 19213, 17387, 16284, 11296, 10047,
6116, 3801, 2238, 1059` over 10 buckets) and decays smoothly and quickly
from the first bucket onward. This is consistent with the user's "slap"
identification (a single sharp decaying transient) vs. a real gunshot's
more sustained buzz/reverb character, and is independent corroborating
evidence — not proof by itself, since envelope shape alone doesn't rule out
a resample/pitch difference distorting the comparison (see below).

**Re-opened the investigation on the user's instruction; do not close D202
as WONTFIX.** Two concrete threads were started this session, neither
finished:

1. **The debug-name label itself is suspicious independent of any
   listening test.** `bondconstants.h`'s soundIndex 46 is *named*
   `"46_GUN_SILPPK_A_SFX"` — a name that specifically claims to be the
   silenced-PPK gunshot — yet its decoded ROM content is what the user
   twice identified as the melee-slap effect (which has its own, different,
   adjacent indices: `47_PUNCH1_SFX`/`48_PUNCH2_SFX`/`49_PUNCH3_SFX`). A
   correctly-decoded sound whose content doesn't match its own debug label
   points at a **sound-bank navigation/indexing bug** (wrong bank, wrong
   instrument, or an off-by-one somewhere in the ALBank -> ALInstrument ->
   ALSound walk) rather than at "GoldenEye's SFX design reuses one asset
   for two things" (M-59's charitable reading (a)). `src/snd.c`'s
   `sndPlaySfx` doc comment is explicit about the resolution path:
   `soundBank->instArray[0]->soundArray[soundIndex]` — i.e. `soundIndex` is
   an index into **instrument 0's** sound array, not a bank-global flat
   index. Worth checking directly: does `romdata.c`'s bank re-layout
   (`afCtx`/`_bnkfPatchWaveTable`, `port/src/romdata.c` ~L582-780) walk
   instruments/sounds in the same order the ROM's `ALBankFile` stores them,
   or could the PC conversion silently shift indices by one somewhere in
   that re-layout pass? Not yet checked line-by-line against the ROM bytes
   for THIS specific bank/instrument.
2. **Started `scratchpad/rom_sfx_decode.py`** (untracked, WIP, not yet
   working correctly) — a fresh, from-scratch offline ROM bank parser
   (independent of the live game) intended to decode a whole *range* of
   `soundIndex` values (44-50, 107, extensible) directly from
   `_sfxctlSegmentRomStart`/`_sfxtblSegmentRomStart` and compare each one's
   envelope/waveform against the new `B00I00S2D.wav` reference, to find
   which soundIndex (if any) actually matches the real gunshot — which
   would prove a specific off-by-N in the bank walk rather than relying on
   ear-only comparisons. **This script's ADPCM predictor loop is currently
   WRONG and must not be trusted as-is** — it stubs out the intra-subframe
   accumulation term with a dead `if False` branch. Confirmed the *exact*
   correct algorithm by re-reading `port/src/mixer.c`'s `aADPCMdecImpl`
   (L185-239, ground truth, this is what the live port actually runs):
   table shape `sAdpcmTable[predictor][2][8]`; per output sample `j` (0-7)
   within a subframe, `acc = tbl[0][j]*prev2 + tbl[1][j]*prev1 +
   (ins[j]<<11) + sum_{k=0}^{j-1} tbl[1][j-k-1]*ins[k]`, `ins[]` being the
   8 sign-extended-then-shifted nibbles of that subframe, `prev1`/`prev2`
   the last two output samples (carried from the previous subframe/frame).
   **Next session: fix `rom_sfx_decode.py`'s decode loop to match this
   exactly** (the M-58/M-59 "isolated decode" script that the user's slap
   identification was based on was verified byte-exact against this same
   algorithm in a prior session, so that specific result stands — only the
   new range-scanning script is unverified), then run it across a wider
   soundIndex range and diff each envelope against `B00I00S2D.wav`.
3. **Also flagged, not yet resolved:** the M-59 "correctpitch" resample
   experiment (`scratchpad/d202_soundindex46_correctpitch.wav`, pitch=23849
   from the M-58 live trace) produced *fewer* output samples (1459) than
   input (3984) at a pitch ratio ~0.364. If pitch < 1.0 means "advance less
   than one input sample per output sample" (slow down / stretch), output
   sample count should be `input / ratio` (more samples), not `input *
   ratio` (fewer) — the direction looks inverted. This resample script's
   formula needs re-deriving against `port/src/mixer.c`'s actual
   `aResampleImpl`/`sResampleTable` before its output is trusted either
   way; it has NOT been sent to the user and should not be used as evidence
   yet.

**Status at handoff: D202 is OPEN, user-confirmed-wrong-in-PC-port, actively
being re-investigated.** Do not re-close as "ground truth, not a bug" —
that conclusion was explicitly rejected by the user with a real reference
recording now in hand (`scratchpad/B00I00S2D.wav`). No code changes made
this session (diagnostic-only), nothing to revert.

**M-61 — both M-60 threads resolved (one closed, one fixed+run); the
decisive question is now "what byte is actually in the original ROM", and a
new logical constraint makes that the keystone fact.** Diagnostic-only
session again; no game/port code touched. Results:

1. **Thread 1 (bank-navigation off-by-N) CLOSED — negative.** Line-by-line
   review of `port/src/romdata.c`'s bank re-layout (`afCtx` /
   `_bnkfPatchWaveTable`, ~L582-780) confirms the PC conversion walks
   `ALBankFile -> ALInstrumentFile -> ALSoundFile` in exact ROM order with
   no index shift; combined with M-56's byte-exact runtime trace, PC
   soundIndex N resolves to the same ROM sample as N64 soundIndex N. The
   "wrong bank / off-by-one in the walk" hypothesis is dead. Do not
   re-litigate it.
2. **Thread 2 (offline decoder) FIXED and VALIDATED.**
   `scratchpad/rom_sfx_decode.py`'s `adpcm_decode()` was rewritten to match
   `port/src/mixer.c`'s `aADPCMdecImpl` exactly. Three bugs fixed: (a)
   tbl0/tbl1 indexing was swapped, (b) the intra-subframe accumulation term
   `sum_{k<j} tbl[1][j-k-1]*ins[k]` was missing, (c) wrong `ncoef` count.
   Validation: full-bank scan of all 261 sounds (gun.h L85: "There are 261
   sound effects, or 0 - 105h") decoded cleanly; **index 45
   (`45_DROP_GUN_SFX`) is the ONLY strong match for the user's reference
   clip `B00I00S2D.wav` (envelope correlation +0.999)**; index 46
   (`46_GUN_SILPPK_A_SFX`) decodes to the early-peak fast-decay "slap".
   Candidates saved as `scratchpad/romsfx_44..60.wav`. The decoder is now
   trustworthy for future soundIndex lookups.
3. **Fire path traced end-to-end in game code — no runtime remapping of the
   index exists.** `src/game/gunfire.c` L3196/L3200:
   `sndPlaySfx(g_musicSfxBufferPtr, bondwalkItemGetSound(var_s1), ...)` →
   `src/game/gun.c` L1343-1345: `bondwalkItemGetSound()` returns
   `get_ptr_item_statistics(item)->Sound` — the raw u16
   `WeaponStats.Sound` field (offset 38; layout confirmed from
   `src/game/gun.h`: 7×f32, s32 AmmoType, s16 MagSize, u8 AutoFireRate,
   s8 SingleFireRate, u8 ObjectsShootThrough, u8 SoundTriggerRate, u16
   Sound, ptr cartridge, …). The silenced PPK's fire sound IS
   `wppksil_stats.Sound`, used verbatim. (Unsilenced PPK uses 0x6B=107.)
4. **Build wiring: both N64 and PC builds compile the same initializer from
   source.** `src/game/gun.c:135` includes the aggregate
   `assets/obseg/gun/gunWeaponStats.inc.c`, which defines `wppksil_stats`
   inline at `//D:800326C4` with `.Sound = 0x2E` (it only #includes the
   per-weapon files for fist/knife; everything else is inline). The
   per-weapon file `assets/obseg/gun/wppksil/gunWeaponStat.inc.c` carries a
   second, IDENTICAL copy (also `0x2E`, same `//D:800326C4`). The PC build
   exclusion list covers only `assets/obseg/{bg,brief,setup,stan}`
   (CMakeLists ~L473), so gun.c's compiled data is identical in both
   builds. **The decompiled source is internally consistent: 0x2E in both
   copies.**
5. **KEY LOGICAL CONSTRAINT (new, this is the keystone):** the project's
   premise is that the N64 build byte-matches the original ROM. Compiled
   data cannot silently diverge — if `gunWeaponStat.inc.c`'s `.Sound`
   were a transcription error, the built ROM would differ from the
   original at exactly those 2 bytes and the match would fail. Therefore
   **0x2E almost certainly IS in the original ROM**, i.e. real N64 hardware
   also requests sound index 46 for the silenced PPK. That contradicts the
   reference clip matching index 45 at +0.999. Exactly one of these must be
   false:
   (a) the byte-match verification does not actually cover this data
       segment (check the verify tooling/output!);
   (b) `B00I00S2D.wav` is not an in-game capture from real N64 hardware —
       its filename looks like a media-library asset ID, provenance was
       never stated; if it's e.g. film/marketing audio, the "N64 plays 45"
       premise collapses and M-59's "faithful reproduction" conclusion
       returns WITH evidence;
   (c) N64 runtime resolution of index 46 differs from ROM-order index 46
       (Thread 1 closed *ordering* in the PC re-layout, not real-hardware
       ALBank content — lower priority given (a)/(b));
   (d) the +0.999 envelope correlation with index 45 is misleading (room
       reverb tail can inflate envelope matches).
6. **Direct ROM-byte read of `wppksil_stats.Sound` FAILED — do NOT repeat
   the same searches (they looped 40+ times this session, all 0 hits):**
   - Any pattern containing a decompiled float literal is unreliable: the
     literals are decimal approximations that don't round-trip to exact
     IEEE-754 bits (e.g. `-20.799999` → `c1a66666`, zero occurrences in
     ROM). Integer-only tail patterns (`AmmoType=1, MagSize=7, 0xFF, 0x10,
     …`) also returned 0 hits — either the byte layout at that ROM location
     differs from assumption or the data sits where the scan didn't look.
   - The hand-computed RAM→ROM mapping produced ROM offset `0xC11934` >
     file size `0xC00000` — WRONG. Note `ge007.ld` puts `.csegment` at ROM
     `0xC00000` (= exactly EOF), so `wppksil_stats` (vaddr `0x800326C4`)
     cannot live there; the correct vaddr→ROM mapping must come from the
     N64 build's map file/symbol table, not arithmetic.
   - No N64 `.map` found at maxdepth 3 (only the PC CMake tree
     `build-linux/`). Search DEEPER: `assets/obseg/Makefile.*`, `ld/`,
     `dist/`, or any N64 ELF in the repo; `nm`/`objdump` on it yields the
     exact vaddr+section, and the ROM offset then follows from the linker
     script.
7. **Next session, in priority order:**
   1. **Establish what byte the original ROM holds at
      `wppksil_stats.Sound`** — find the N64 build map/ELF (search deeper
      than maxdepth 3) and read the 2 bytes. This single fact splits the
      tree: ROM=0x2D → transcription error (then audit why byte-match
      verification missed it); ROM=0x2E → data is ground truth, and the
      discrepancy lives in reference-clip provenance (b) or N64 runtime
      resolution (c).
   2. **Check the byte-match verification tooling** — does it compare data
      segments or code only? Locate the last full-verify output.
   3. **Ask the user for `B00I00S2D.wav`'s provenance** (one question; see
      (b) above).
   4. Cheap corroboration: live PC trace logging the soundIndex actually
      passed to `sndPlaySfx` on PPK fire (expected 46) — confirms the PC
      side behaves exactly per its data.

**Status at handoff (M-61): D202 OPEN.** Threads 1+2 from M-60 are closed
(negative / fixed). No code changes; only `scratchpad/rom_sfx_decode.py`
modified (now correct + validated). Do not re-run the failed ROM byte-
pattern searches — go for the N64 map file instead.

### M-62 — item (2) resolved without a build: byte-match verification DOES cover this exact struct, and its checksum was captured after the source's last edit. `.Sound = 0x2E` is very likely ground truth. Pivoting to (b)/(d).

Static/read-only session. Skipped the N64-map/ELF route (item 1) — no MIPS
toolchain is installed in this environment (`mips-linux-gnu-*` absent,
`docs/building.md` says extraction needs `binutils-mips-linux-gnu`; building
the full N64 target to get a map file is a real toolchain-install task, not
attempted this session) — and went straight for the cheaper item (2) instead.

**Found: this repo's own CI (`.github/workflows/ci.yml`) never builds the
N64 target at all.** `validate`/`linux-build`/`windows-build` are all PC-port
jobs; there is no job invoking the N64 `Makefile` or
`scripts/test_files.sh`. So "byte-matches US/EU/JP ROMs" (AGENTS.md's
opening claim) is inherited from the upstream `n64decomp/007` project's own
history, not continuously re-verified here — worth knowing, but a separate
concern from D202.

**The verification tooling itself does cover `.data`, and gun.o specifically:**
`scripts/test_files_readme.md` — `test_files.sh` extracts `.text`/`.code`/
`.bss`/`.data`/`.rodata` from every `.o` under `src/`, `src/game`, and the
four asset dirs, and diffs each against a checked-in known-good md5
(`scripts/ge007.u-test_basis.csv`). Confirmed by direct grep:
`scripts/ge007.u-test_basis.csv:946`: `0289c36e967840bba7f6fbd026dda001,.data,build/u/src/game/gun.o`
— `gun.o`'s `.data` section (which is where `wppksil_stats` lives, via
`gun.c`'s `#include` of `gunWeaponStat.inc.c`) has a recorded checksum.
This directly falsifies alternative (a) from M-61 — the verification does
cover this exact data.

**Timing check, so the checksum can't be stale relative to a since-edited
source:** `git blame -L946,946 scripts/ge007.u-test_basis.csv` →
`cc14d64e7 "for england james?"` (2026-08-16, upstream sync commit).
`git log -- assets/obseg/gun/wppksil/gunWeaponStat.inc.c` → last touched
`9fbe1fd5` ("sync. run make forceextractassets..."), which is an ancestor of
`cc14d64e7` in this file's history. **So the checksum was captured AFTER
the last edit to this exact source file** — it isn't testing stale content.
Re-read the file directly: `.Sound` is still `0x2E` (14th positional field,
`assets/obseg/gun/wppksil/gunWeaponStat.inc.c:3`), unchanged.

**Conclusion (not a rebuild-confirmed pass, but strong circumstantial
evidence):** the upstream decomp project's own generated fingerprint of a
known-good `gun.o` build covers this exact struct, was captured against
this exact (still-current) source content, and this repo's non-negotiable
rule #2 (game logic/decomp source frozen, PC-port work never touches
`src/game/gun.c` or `assets/obseg/gun/**`) means nothing has drifted since.
**Actually running `test_files.sh` to get a live "pass" line still requires
installing a MIPS toolchain and doing a real N64 build — not done this
session; flagged as the one remaining way to make this airtight rather than
strongly-inferred.** Barring that, alternative (a) is now the *least* likely
of the four M-61 alternatives, not the most. Combined with M-61's already-
closed alternative (c) (traced fire path, no runtime remapping), the two
live leads are now **(b) reference-clip provenance** and **(d) envelope-
match reliability** — both point away from a port bug and toward a
misidentified reference clip.

**Next session, in order:**
1. **Ask the user the one open question: where did `scratchpad/
   B00I00S2D.wav` come from?** (Real N64-hardware capture of this exact
   weapon on this exact ROM region? An emulator? A sound-effects library
   asset, given the filename reads like a media-asset ID, not a capture
   log?) This is the cheapest possible next step and was never actually
   asked — do it before any more code/data spelunking.
2. If the user can't confirm hardware provenance: run the same ROM in a
   known-good N64 emulator (the M-59 handoff's original recommended
   tie-breaker, never done), fire the silenced PPK, listen directly —
   settles (b)/(d) without needing the reference clip at all.
3. Only if both of those come back still contradicting `.Sound = 0x2E`:
   install a MIPS toolchain (`binutils-mips-linux-gnu` equivalent) and
   actually run `scripts/test_files.sh` for real, to convert this
   session's strong-inference close of alternative (a) into a hard pass/fail.
4. Cheap corroboration, still not done: live PC trace of the soundIndex
   passed to `sndPlaySfx` on PPK fire (expected 46) — confirms PC-side
   behavior matches its own data, independent of what the data *should* be.

**Status at handoff (M-62): D202 OPEN**, but the center of mass has shifted
firmly away from "port bug in the data/decode chain" (four independent
sessions, M-56 through M-62, now corroborate `.Sound = 0x2E` end to end)
and toward "reference clip may not be what it was assumed to be." No code
changes this session — read-only investigation, one `findings.md` write-up.

**Same session, follow-up: asked the user directly where `B00I00S2D.wav`
came from. Answer: "Not sure / found online"** — the user does not know its
provenance and it was sourced from the internet, not recorded from real N64
hardware or a verified emulator run against this ROM. This closes item (1)
of the M-62 next-session list and directly confirms alternative **(b)**
from M-61 (reference clip isn't a verified in-game N64 capture) as the true
explanation — an unverified internet clip was compared against a
byte-verified ROM data table and, unsurprisingly, didn't match.

**Reassessment: five independent sessions (M-56 through M-62) have now
verified every link in the actual chain** — soundIndex resolution, bank
re-layout order, the ALWaveTable/ALSound pointer chain, the ADPCM decode
math (hand-verified bit-exact against the C output), the ROM segment
offset (verified 3 ways), and now the weapon-stats source data itself
(byte-match checksum, captured post-edit) — **all say the silenced PPK
correctly requests and plays soundIndex 46 exactly as real N64 hardware
would.** The only unverified link left is the thing D202 was reopened over:
an internet-sourced reference clip of unknown origin. That is no longer a
credible basis to keep this open as a port bug.

**Recommendation: close D202 as NOT-A-BUG** (same conclusion M-59 reached,
now on much stronger evidence) **unless the user wants the belt-and-braces
emulator A/B** (fire the silenced PPK on the actual ROM in a known-good N64
emulator, listen directly) — cheap, and the only remaining way to fully
rule out a genuine transcription error without installing a MIPS toolchain
and running `scripts/test_files.sh` for a hard pass. Do not spend further
session time on the data/decode/index chain — it is exhaustively verified.

### M-63 — SUPERSEDES the M-62 "recommend close": user reports the bug is broader than a single wrong sample — real-time mixer/voice corruption, not a data-table issue. REOPENED for real, live repro attempted. Root cause NOT found; one concrete anomaly banked for next session.

**User's report (verbatim symptom, not the M-60 reference-clip framing):** the
wrong ("slap") sound always plays on the silenced PPK; the correct suppressed
shot sometimes plays **too** (both heard together, not one-or-the-other);
and over a play session sounds "pile up and spirally glitch out," eventually
leaving **no audio except a looping sound (e.g. Bunker's door) stuck playing
since level start.** This is categorically different from "wrong sample
selected" — it describes runtime mixer/voice-lifecycle corruption. The M-56
through M-62 chain (which verified the index/data/decode path bit-exact) does
**not** cover this; it only proves soundIndex 46 is the *correctly requested*
sound, not that the mixer plays it *cleanly* once requested. Reopening.

**Build+run environment IS available this session** (`export
PATH=/c/msys64/mingw64/bin:$PATH`; MSYS2 MINGW64 gcc/cmake/SDL2 all present).
No MIPS toolchain (confirmed absent again, unrelated to this thread).

**False-alarm detour (banked so it isn't repeated): a "crash in `load_bg_file`
(`bg.c:865`) on every `-level_09` boot" is NOT a real bug** — it was this
session's own test-setup gap. A throwaway `build-pc/data/` with only the
`.z64` copied in (no `pccg-ntsc-final/`, `pcmodels-ntsc-final/`) leaves
`file_resource_table` pointing at raw big-endian ROM instead of the converted
PC sidecars (`obInit()`'s own comment describes exactly this failure mode);
the two `[WARN]` lines ("pcmodels.bin not found", "pccg.bin not found —
stage loads will fault") were sitting right in the log and should have been
read before chasing a gdb backtrace. Copying the real `data/pccg-ntsc-final/`
+ `data/pcmodels-ntsc-final/` (+ the repo's real `ge007.eep`/`ge007.ini`)
into `build-pc/data/` makes it load clean, 60s+, no crash, both with and
without the uncommitted audio-diagnostic files. **Next session: always mirror
the full `data/` dir into `build-pc/data/`, not just the ROM, before treating
any level-load failure as real.**

**Repro built:** `GE_MIXERTRACE=1 GE_AUDIOTRACE=1 GE_AUDIODUMP=1
GE_INPUTSCRIPT="600:R,Z;601:R,Z;690:R,Z;691:R,Z;...;3210:R,Z;3211:R,Z"`
(30 fire pulses at ~90-frame/~0.75s spacing) `timeout 60
./ge007.x86_64.exe -level_09`, run against the real save
(`data/ge007.eep`) copied into `build-pc/data/`. Ran clean 60s, no crash.
Logs preserved: `scratchpad/d202-m63/{audiotrace.log,mixertrace.log,
audiodump.raw}` (mixertrace.log is 25MB — grep it, don't read it whole).

**Checked and RULED OUT this session, from the trace:**
- **Voice-pool exhaustion / leak:** only **16 distinct `newState` voice-slot
  pointers** were used across all 153 `sndPlaySfx` calls in the 60s window
  (highest reuse count: 20), and `sndDeactivate` count (159) tracks
  `sndPlaySfx` count (153) almost 1:1. The pool is being recycled
  continuously, not growing unbounded — the "voices never freed" theory
  (the D199/M-53–M-55 lineage) does **not** reproduce in this repro.
- Firing the silenced PPK produced exactly the expected `soundIndex=46`
  requests (58 of them, one burst per scripted trigger pull, occasionally
  2 back-to-back — plausibly dual-wield, not investigated further) with no
  other unexpected soundIndex interleaved in the same burst.

**New anomaly found, not yet explained — the most concrete lead for next
session:** `soundIndex=109` (`bondconstants.h:2546`,
`"109_GUN_B4_BOLTACTION_SFX", //used for AK47`) fires **94 times in 60
seconds** despite the player never touching an AK47 in this repro (silenced
PPK only). A chunk of those requests hit the *same* voice slot
(`newState=00000000706f4fb8`) at a suspiciously uniform **2880-byte spacing
in the mixed output stream** — at 22050Hz stereo 16-bit that's exactly
**720 samples / ~32.6ms between re-triggers**, four-in-a-row
(`dumppos=688576,691456,694336,697216,700096`). ~30 rounds/sec is far
faster than any plausible automatic-weapon cadence in this game (real
full-auto weapons here fire far slower) — this reads like something is
re-issuing `sndPlaySfx` for the same logical sound every audio tick/frame
instead of once per actual game event, which would exactly explain "sounds
piling up." **Not yet confirmed as related to the user's PPK-slap complaint
specifically** (soundIndex 109 isn't the PPK's sound) — could be a
*second*, independent SFX-retrigger bug that happens to share the same
mixer, and its presence would still explain general "audio feels broken."

**Not yet reproduced: the user's exact reported chain** (piling
up → spiraling glitch → eventual total silence except one stuck loop). This
60s/58-shot synthetic burst didn't collapse the mixer — either it needs a
longer, more realistic session (real human play, many minutes, mixed
NPC/player/ambient audio denser than this script), or the soundIndex=109
retrigger-storm anomaly above is the seed that, given more real playtime,
compounds into what the user described (worth designing a longer repro
around forcing that specific retrigger condition first, cheaper than a raw
extended random playtest).

**Next session, in order:**
1. Find what actually calls `sndPlaySfx(109, ...)` (search `src/game/*.c`
   for callers keyed to soundIndex 109 / `GUN_B4_BOLTACTION_SFX`) and
   determine whether the ~32.6ms retrigger cadence is a real per-shot
   automatic-fire loop (legitimate, if unusually fast) or a bug re-firing
   once per audio buffer/tick instead of once per game event. This is the
   single most concrete, checkable lead from this session.
2. If (1) doesn't explain it: build a much longer repro (5+ minutes,
   `GE_INPUTSCRIPT` walking + intermittent firing near guards, or just ask
   the user to reproduce with `GE_MIXERTRACE=1 GE_AUDIOTRACE=1
   GE_AUDIODUMP=1` set and hand back the logs) aimed specifically at
   reaching the "eventual total silence except one stuck loop" end state,
   then diff voice-slot reuse counts/timing against this session's healthy
   60s baseline to see where it diverges.
3. Listen to `scratchpad/d202-m63/audiodump.raw` directly (raw s16 stereo
   22050Hz PCM) for audible garbling/overlap around the soundIndex=109
   retrigger-storm timestamps — not done this session (no audio playback
   attempted, log-analysis only).
4. Do NOT re-litigate the index/data/decode/ROM-offset chain (M-56–M-62,
   exhaustively verified) — this session's finding is orthogonal to that
   one; D202 is reopened on a *different* mechanism than the M-60 reference
   clip.

**Status at handoff (M-63): D202 REOPENED, root cause not found.** The
M-62 "recommend close" no longer stands — the user's actual complaint is a
runtime mixer/voice issue, not (only) a data-correctness question. One
concrete, unexplained retrigger anomaly (soundIndex 109, ~32.6ms cadence)
is banked as the top lead. No code changes this session — build+run+trace
only; the pre-existing uncommitted diagnostic files (`port/src/audio.c`,
`port/src/mixer.c`, `src/audi.c`, `src/libultra/audio/load.c`, `src/snd.c`)
were exercised, not edited.

### M-65 — Root-caused the "eventual silence" half: an ownerless `SOUND_FLAG_LOOPED` SFX permanently consumes one of only 8 voices. Guard measured. The "wrong/multiple sounds" half gains a second, independent cause (`sndCreatePostEvent` was stubbed out, killing ALL distance attenuation). The audible stuck loop is still NOT reproduced.

**User's report this session:** "wrong sound playing or multiple sounds playing
when there should be one", and "on bunker if the attract shows the door open,
it plays a sound and the sound gets stuck looping forever until you close the
game/level", later refined to "the double door sound by the computer room still
infinitely loops after they begin playing" and "a good chance it just happens
when the level starts if you get that attract intro with them opening".

#### (1) CONFIRMED + MEASURED: the voice leak that produces "eventual silence"

Chain, each link verified rather than argued:

- `soundIndex=203` (`METAL_SLIDE_CLOSE_SFX`) has `envelope->decayTime == -1`.
  Verified at the **source bank level**, not just at runtime, with a new
  `GE_BANKDUMP` probe walking `instArray[0]->soundArray[i]` in the ROM-layout
  blob: `BANKSRC inst@0x005778 soundIndex=203 sound@0x0046B8 env@0x000008
  decay=-1`. `GE_KEYMAPDUMP` additionally dumps every source `ALEnvelope`
  (`ENVSRC src@0x000008 raw=00000000 FFFFFFFF 000007D0`). **Genuine ROM data,
  faithfully converted** — 19 of the SFX instrument's 261 sounds are
  deliberately sustained this way.
- `decayTime == -1` sets `SOUND_FLAG_LOOPED` (`sndSetupSound`, via
  `decayTimeFlag`). A looped sound **never posts `AL_SNDP_STOP_EVT`**
  (`snd.c` `AL_SNDP_DECAY_EVT` skips it), *and* the preemption scan
  **explicitly refuses to steal looped voices** (`!(iterState->unk3e & 0x12)`).
  So the only release is `sndDeactivate`, which needs an owner.
- `doorPlayCloseSound0/1` play it as `sndPlaySfx(..., METAL_SLIDE_CLOSE_SFX,
  NULL)` — **no `pendingState`**, so nothing stores the state and nothing can
  ever deactivate it. (NB the decomp names mislead: `doorStartClose` calls
  `doorPlayOpenSound1`; `doorFinishOpen`/`doorFinishClose` call
  `doorPlayCloseSound0`/`1`. The *Open* pair own their slide loop via a slot;
  the *Close* pair are unowned terminators.)
- `maxSounds` is **8** (`MUSIC_SFX_SEQ_MAYBE_MAX_SOUNDS`, `music.c:76`).

**Measured** (115 s headless `-level_09`, new `[VOICE+]`/`[VOICE-]` probes
pairing every `g_sndAllocatedVoicesCount++` with its decrement):
**7 of the 8 voices permanently held, every one of them `sound=706c8e48`
(soundIndex 203) with `flags=7` (`LOOPED|PLAYING`)**, never released. The
64-entry *state* pool stays healthy (18 slots recycling, never NULL) — which is
why this is silence, not a crash: later `sndPlaySfx` calls get a state but no
voice and are dropped via `sndDisposeSound`.

**Guard (port-side deviation, `src/snd.c` preemption scan).** Allow reclaiming
a voice only when it is `SOUND_FLAG_LOOPED`, not `SOUND_FLAG_RETRIGGER`, and
`state->state == NULL` — i.e. provably ownerless, so nothing could ever have
stopped it. Owned loops keep their existing protection exactly.

| metric (115 s `-level_09`) | before | with guard |
|---|---|---|
| `g_sndAllocatedVoicesCount` | **pinned at 8** | oscillates 1–8 |
| voices acquired over the run | 171 | **629** |
| residual stuck 203 voices at exit | **7** | **1** |

User confirmed the symptom improves ("still happens but less cascading
perhaps"). **This is a game-logic change (AGENTS.md rule #2) justified by
measurement, not by ground truth — it is NOT known why the N64 does not leak
here.** Keep it flagged as a documented deviation, not a silent fix.

#### (2) FIXED, independent: `sndCreatePostEvent` was stubbed out, removing all distance attenuation

`sndCreatePostEvent` returned immediately under `#ifdef PORT` (D138), from when
"libaudio is Phase-3 parked" and nothing drained
`g_sndPlayerPtr->evtq->allocList`, so every positional tick appended an item
that was never consumed and `alEvtqPostEvent`'s ordered insert went O(n^2)
until the kernel-heartbeat watchdog tripped on Facility ambience.

**That premise expired when the Phase-3 audio thread landed (D198-D201)** —
`amMain` now drains the queue every audio frame. Every caller posts type 8 =
`AL_SNDP_VOL_EVT` (`bondview2.c`, `chrai.c`, `gunfire.c`, `propobj.c`), so the
stub had been silently removing **all distance-based volume**: every sound
played at full volume regardless of range. That is very likely a large part of
the "wrong sound / multiple sounds / too loud" impression, independent of (1).

Stub removed. Re-measured: `rt=0.996`, `drop=0`, frame times 91-329 us, zero
watchdog trips over 115 s — the O(n^2) regression does not return.

#### (3) STILL OPEN: the audible endless door loop is NOT reproduced

Not reproduced in any headless run. Everything that could make a sound audibly
repeat was measured and excluded — see the ruled-out list below.

#### RULED OUT this session (measured — do not re-walk)

- **Wave-level looping.** New `[WAVELOOP]` probe: `loop == NULL` on **every**
  sound in the SFX bank. No sample can repeat itself.
- **Decoding past the end of a wave.** New `GE_PULLTRACE` `[PASTEND]` assertion
  in `alAdpcmPull` (fires when `f->sample` exceeds the wave's total samples
  while `nOver == 0`): **0 hits**. `aClearBuffer` zero-fill works; an exhausted
  voice goes silent.
- **Stale / accumulating software DMEM.** `GE_DMEMWIPE` zeroes all 4 KB of
  `sDmem` at the exact frame boundary (`alMainBusPull`'s `AL_MAIN_L_OUT`
  clear is the first opcode of every frame). Audio still 99 % non-silent and
  unchanged — no audio state carries across frames in DMEM.
- **Reverb feedback self-oscillating** (plausible because `aPoleFilterImpl`,
  the damping in the feedback path, is an unimplemented no-op — D199).
  `GE_NOWET` mutes the reverb send entirely: signal unchanged.
- **The `AL_SNDP_PLAY_SFX_EVT` retrigger machinery.** 0 retrigger posts in the
  user's trace. (Also independently confirms D204's closure of the M-63
  `soundIndex=109` anomaly: measured here as 184 plays, **owned 184 / orphaned
  0** — a properly-owned repeated sound, not a retrigger storm.)
- **Doors oscillating.** ~4 open/close cycles per door per 110 s across 4 doors;
  203 played ~once per door state change. Normal, not thrashing.
- **Doors orphaning their loop because both slots are busy.** 0 occurrences
  (`[DOORSND] ... ORPHAN-both-slots-busy` never fired). The orphan is real but
  arises differently: the call site passes `NULL` by design.
- **`ALKeyMap` conversion.** `GE_KEYMAPDUMP` dumps all 220 source keymaps from
  the ROM-layout blob; runtime values match, including entries that are
  legitimately all-zero in the fields Rare repurposed.
- **`DoorRecord` 64-bit overlap.** `sizeof(DoorRecord)=0x128`, observed
  allocation stride exactly `0x128`; `openSoundState@0x110`,
  `closeSoundState@0x118`. No overlap.
- **The `ALSoundState`-pointer-to-pointer pun** in `sndPlaySfx`
  (`pendingState->link.next = nextState`, relied on by `doorPlayOpenSound0/1`
  and `audioPlayFromProp`). Survives the 64-bit transition — `ALLink.next` is
  at offset 0 and pointer-sized on both. New `[SLOTWRITE]` probe confirms the
  slot receives the state.
- **`ALWaveTable::len` drifting at runtime** (observed 6939 -> 6940 etc.).
  Benign: `alLoadParam` rounds `len` down to a whole number of 9-byte ADPCM
  frames on first bind, so the pre-round value is simply seen once.

#### Corrections banked (mistakes made this session — do not repeat)

- **`romdataFixupMusicSeqTable: seqCount 63 exceeds blob capacity 1` is
  BENIGN**, not a music failure. The function is called twice and the first
  call deliberately passes a 16-byte header slice (capacity `(16-4)/8 = 1`);
  the full-size second call succeeds. Music loads and plays.
- **A continuous ~1400 RMS "drone" in `GE_AUDIODUMP` captures is the
  background music, not a stuck sound.** It was briefly mistaken for the bug
  because only *SFX* events carry `dumppos` timestamps, so music looks like
  sound with nothing playing it. Confirmed music by pointer range: the heavily
  rebound wavetables (`706cbaf8`, `706ccfe8`) lie outside the SFX bank's
  `706c3e70`-`706ca430` range and never appear in any `sndPlaySfx`; they are
  driven by `csplayer.c:535` (`alSynStartVoiceParams`). The ~28 k
  `AL_FILTER_SET_WAVETABLE` binds per 60 s are ordinary note-ons, and
  `f->sample` resetting per note is correct.
- **Multiple `audiotrace.log` files exist** (repo root vs `build-pc/`) and a
  shell whose cwd shifts between commands will silently analyse the wrong one.
  Always use an absolute path, and check `ls -la` timestamps before trusting a
  log's provenance.

#### Diagnostics added (all `#ifdef PORT` / env-gated; remove when D202 closes)

| env var | file | what it prints |
|---|---|---|
| `GE_AUDIOTRACE` | `src/snd.c` | `[ENVELOPE]`, `[WAVELOOP]`, `[VOICES]`, `[VOICE+]`/`[VOICE-]`, `[SLOTWRITE]`, `[RETRIGGER-POST]`, keymap fields on every `sndPlaySfx` |
| `GE_AUDIOTRACE` | `src/game/propobj.c` | `[DOORSND]` — every door sound state change, both slots, and an `ORPHAN-both-slots-busy` verdict |
| `GE_KEYMAPDUMP` | `port/src/romdata.c` | `KEYMAP` + `ENVSRC` — source ROM bytes of every `ALKeyMap` / `ALEnvelope` |
| `GE_BANKDUMP` | `port/src/romdata.c` | `BANKSRC` — the source bank tree per `soundIndex` |
| `GE_PULLTRACE` | `src/libultra/audio/load.c` | `[PASTEND]` — asserts no voice decodes past its wave |
| `GE_DMEMWIPE` | `port/src/mixer.c` | zeroes software DMEM at the frame boundary |
| `GE_NOWET` | `port/src/mixer.c` | mutes the reverb send |

**Caution:** the `[VOICE+]`/`[VOICE-]` probes are written from both the game
thread and `amMain` to one unbuffered `FILE*`, so occasional lines are torn.
Parse defensively (filter to well-formed hex) or add a lock before relying on
exact counts.

#### Next step

Needs a user capture at the failing door (computer-room double doors, or a
level start whose attract intro shows them opening) with **both**
`GE_AUDIOTRACE=1` and `GE_AUDIODUMP=1`. The dump allows isolating the stuck
sound numerically and separating it from music without listening, and
`dumppos` pins it to the exact door event.

**Status at handoff (M-65): D202 PARTIALLY resolved.** The "eventual silence"
cascade is root-caused and mitigated (measured); the missing distance
attenuation is fixed; the audible stuck loop is still not reproduced.

### M-66 — the audible stuck door loop IS root-caused: it is faithful N64 behaviour (an original-game quirk), not a port bug

The user capture M-65 asked for arrived (`build-pc/audiodump.raw`, 31,905,536 B
= 361.7 s stereo s16 @22050; `build-pc/audiotrace.log`, same run, `-level_09`
level start whose attract intro shows the computer-room double doors). The
stuck loop is **sound 203 (`METAL_SLIDE_CLOSE_SFX`) doing exactly what the ROM
tells it to do**, and every link of the chain is ground truth:

1. **ROM data:** wavetable loop = `(start=2471, end=7719, count=-1)`;
   `src/libultra/audio/load.c` implements `count` with the explicit comment
   "-1 is loop forever" (decrements only when > 0). Envelope:
   `attack=0 decay=-1 release=2000 aVol=127 dVol=127`.
2. **Game code (byte-matched decompilation):** `doorPlayCloseSound0/1`
   (`src/game/propobj.c`) plays 203 fire-and-forget with a `NULL` owner state.
   The `[SLOTWRITE]` probe confirms the close-sound state is **never stored in
   any door slot**, so the door's stop-sounds path can never reach it, and no
   other code calls `sndDeactivate` on it. Nothing in the game will ever stop
   it except level exit.
3. **sndp (ground truth):** the preemption scan (`src/snd.c` ~line 321) skips
   any voice with flag `0x12` (`SOUND_FLAG_LOOPED|SOUND_FLAG_RETRIGGER`).
   `AL_SNDP_DECAY_EVT` schedules the release→stop envelope "except for a looped
   sound" (`src/snd.c:509`) — a looped voice gets no stop event at all.
   With `decayTime=-1`, `sndCreateSound` sets `state->priority = 0x41`
   (vs `0x40` normal), and libultra's `_allocatePVoice`
   (`src/libultra/audio/synallocvoice.c`) steals only voices with
   `priority <= new voice priority` — so a 0x41 leaked loop is unstealable by
   any ordinary 0x40 SFX or music note.
4. **Pool geometry:** GE's soft limit is 8 (`maxSounds =
   MUSIC_SFX_SEQ_MAYBE_MAX_SOUNDS`, `src/music.c:76`); the physical PVoice pool
   is 24 (`MUSIC_SYN_CONFIG_MAX_P_VOICES = 0x18`, `src/music.c:67`). Leaked
   loops therefore don't deadlock the synth, but they do eat GE's 8-slot soft
   limit — which is where M-65's guard bites.

**Consequence (identical on N64):** every metal-door close leaks one looping
voice that persists until level exit. During attract idle no new sounds arrive,
so nothing ever evicts them and they play forever — exactly the user's report:
"the double door sound by the computer room still infinitely loops after they
begin playing". Every code path in this chain is shared original source
(`snd.c`, `propobj.c`, libultra audio); none of it is port-specific. **This is
an original-game quirk, faithfully reproduced — rule #2 says leave it.**

#### The capture, timeline (corrected)

`dumppos` is the dump-file byte offset; time = `dumppos/88200` s.

- t=0–21 s: level intro — heavy SFX, door cycles on five doors (…7d2c, …7c04,
  …79b4, …7adc, …80a4). 203 plays at **1.3 / 3.3 / 11.3 / 17.2 / 21.0 s**.
- t≈39.1 s: door …80a4 closes → 203 (state …5020). Then a long quiet stretch:
  the same door's next close comes at **t≈360.7 s** (another 203, state
  …4f50, still looping when the capture ends at 361.7 s). The attract demo
  keeps running the whole time — it just gets sparse.
- Leaked 203 voices alive by t≈40 s: four (states …4db0 from 1.3 s, …50f0 from
  17.2 s, …4e80 from 21.0 s, …5020 from 39.1 s). Two earlier ones WERE
  reclaimed — but only because M-65's guard fired under pool pressure:
  `VOICE- snd8e48/st4e18` at t=14.8 s and `st4fb8` at t=16.4 s, each in the
  same block as a new-sound play (the preemption path). Without the guard both
  would persist and the 8-slot limit would fill by ~t=21 s.
- Steady state from t≈39 s: RMS ≈1560, strong periodicity at **327.0±0.1 ms**.

#### The 327 ms mystery (solved — it was the pitch)

The loop segment is 5248 samples; at the bank rate (22050) that is 238 ms, not
327. But the voice does **not** play at unity pitch: keymap `keyBase=54`,
`detune=50`, and `DEFAULT_SETUP_PITCH_SHIFT = -0x1770 = -6000` (`src/snd.c:20`)
give `alCents2Ratio(54*100 + 50 - 6000) = alCents2Ratio(-550)` = **0.72783**.
Loop period at playback = 5248 / (22050 × 0.72783) = **327.007 ms** — matching
the observed 327.0±0.1 ms exactly. The spectral fingerprint agrees: FFT peaks
of the decoded loop segment at 355.3 / 371.4 / 694.4 / 710.6 Hz × 0.7278 →
259 / 271 / 506 / 518 Hz vs observed steady-state lines 256.7 / 272 / 504.7 /
513.8 Hz. Cross-correlating the **pitch-stretched** loop template against the
t=100 s window gives r = 0.835 (the unpitched template gave ~0.10 — which is
why every earlier template test "failed"; see corrections below).

#### Corrections to M-65 (banked)

- **M-65's correction "the continuous ~1400 RMS drone in captures is the
  background music, not a stuck sound" is wrong (or at best incomplete).** The
  steady-state signal's spectral lines and 327 ms period are the leaked 203
  loop, pitch-shifted as above; r=0.835 template match. Music is present in the
  mix too (M-65's pointer-range argument that *those particular* rebound
  wavetables are music-driven still stands) — the steady state is a composite,
  but the *stuck* component is 203, and it dominates the fingerprint.
- **"The audible stuck loop is still not reproduced"** — it is now: user
  capture + complete mechanism, every link grounded in ROM data / byte-matched
  code / libultra source.
- The earlier "full library correlation: best 0.28, sfx203 only 0.10" was an
  artifact of correlating **unpitched** decodes against pitched playback. Do
  not re-run template tests without applying `keyBase/detune/shift` → ratio.
- M-65's guard comment "only 8 voices exist" is imprecise: there are 24
  PVoice plus the 8-slot GE soft limit; the binding constraint for SFX is the
  soft limit, and the guard operates on exactly that boundary.

#### Guard status and decision pending (user)

M-65's ownerless-loop preemption guard (`src/snd.c` ~line 325) fired twice in
this capture and demonstrably prevents pool exhaustion — but it can only fire
when a *new* sound arrives and the limit is hit. During attract idle nothing
arrives, so the audible loop persists with or without the guard. Options:

- **A) Pure fidelity:** remove the guard; document D202 as "faithful
  reproduction of an original quirk". Consequence (also faithful): after 8
  leaked loops in one level session, new SFX are silently dropped until level
  exit — that is what N64 does.
- **B) Keep the guard (status quo, recommended):** a small, precisely scoped,
  documented port-side deviation. Prevents the SFX-deadlock; the idle loop
  remains audible exactly as on N64.
- **C) Extended deviation:** cap/expire ownerless loops in the port layer to
  silence the idle loop. Fixes the user's complaint but is a larger departure
  from ground truth than B.

Diagnostics: with the root cause established, the M-65/M-66 probe set
(`GE_AUDIOTRACE` lines, `[DOORSND]`, `[SLOTWRITE]`, `GE_PULLTRACE`,
`GE_DMEMWIPE`, `GE_NOWET`, `GE_BANKDUMP`, `GE_KEYMAPDUMP`) may be removed once
the A/B/C decision lands and D202 closes.

**Status (M-66): D202 root cause ESTABLISHED — the audible stuck door loop is
sound 203 behaving exactly as the ROM + ground-truth code specify; faithful
N64 behaviour, original-game quirk. Disposition: user chose C (M-66b below).**

### M-66b — disposition C implemented and measured: ownerless infinite-loop SFX now expire (fade out) on PC

The user chose **C** (extended deviation) as part of the PC port's audio
implementation. M-65's guard stays (slot reclamation under pool pressure);
this adds the audibility fix.

**Placement — why `src/snd.c` and not `port/src/mixer.c`.** The predicate that
distinguishes a *leaked* loop from a legitimate one is **ownership**
(`soundState->state == NULL`), which only sndp sees. The mixer iterates
ALVoice/PVoices with no ownership field; scoping there would force either
hardcoding region-fragile SFX-bank DRAM pointer ranges or capping *all*
infinite-loop voices (music included). snd.c already carries the two documented
D202 port-side deviations (M-65 guard, D138 un-stub), all `#ifdef PORT` — every
D202 port-side change stays in one auditable place. N64 build: zero diff.

**Mechanism.** New PC-only event `AL_SNDP_PORT_EXPIRE_EVT` (reuses the
`AL_SNDP_UNUSED_13_EVT` = 1<<13 slot, defined under `#ifdef PORT` in
`src/snd.h`; no shared-enum change). In PLAY_EVT, immediately after a voice
starts, if the state is LOOPED + FINAL_IN_SEQUENCE + not RETRIGGER +
`state->state == NULL` **and** the wave carries an ADPCM loop with `count == -1`
(finite loops self-terminate in load.c and must not be cut short), post the
expire event at `D202_EXPIRE_DELAY_US` (2 s). Handler: re-validate the full
predicate at fire time, then ramp volume to 0 over `D202_EXPIRE_FADE_US`
(500 ms) and post END_EVT — the stock fade/teardown path. Constants are in
**ALMicroTime = microseconds** (the event queue is µs-driven: `DELTA_1_MS` is
1000, `DELTA_33_MS` is 33333).

- **Not a reused STOP_EVT:** the stock STOP ramp uses the envelope's
  releaseTime — ~2.7 ms for sound 203 (release 2000 / pitch ratio 0.7278) —
  which would hard-cut the loop (click). The dedicated event gives a clean
  500 ms fade.
- **Stale-event safety:** `sndDisposeSound` already removes *all* pending
  events for a state (`sndRemoveEvents(..., 0xffff)`), and any rebind disposes
  first — so an expire can never fire against a new binding. The fire-time
  predicate re-check is belt-and-braces (also no-ops if the state was rebound
  to the *same* sound).
- **Sequence-walk safety:** the type is added to the `isEventForSingleSound`
  mask under `#ifdef PORT`, and the post-site requires FINAL_IN_SEQUENCE, so
  the handler's do-while can never walk into an unrelated alloc-list neighbor.
- **Owned loops are untouched** (`state != NULL` fails the predicate) — they
  behave exactly as on N64. Retrigger sounds (managed by PLAY_SFX/DEACTIVATE
  chains) are excluded.

**Files:** `src/snd.h` (PORT-only event define), `src/snd.c` (two constants,
post-site in PLAY_EVT, handler case, mask token, guard-comment cross-ref).
New `[EXPIRE]` line under `GE_AUDIOTRACE` (see GE-ENV-PROBES.md).

**Measured.** 134 s headless `-level_09` run with `GE_AUDIOTRACE=1
GE_AUDIODUMP=1`. Trace: every sound-203 play posts `[EXPIRE]`; each leaked
voice gets its VOICE- ~2 s after its VOICE+; run ends at
`[VOICES] allocated=0 / max=8` (no survivors). Spectral check (per-1-s
Goertzel at 256.7 Hz, the strongest 203-loop line × 0.7278):

| capture | 256.7 Hz line |
|---|---|
| user pre-fix (361 s) | **flat at max from t≈39 s to end** — the complaint, verbatim |
| post-fix (134 s) | transient ~2–4 s bursts per door close, **full silence between** (gaps at t=60–62, 85–87, 94–98, 110–112, 128–131) |

The stuck infinite loop is gone; each close now rings ~2 s + 0.5 s fade, as a
door clunk naturally would. Scripts + archived user capture:
`scratchpad/d202-m66/` (`check_dump.py`, `line_track.py`,
`user-capture-audiodump.raw`, `user-capture-audiotrace.log`).

**Remaining:** user by-ear pass on the new binary (window is open — probes are
still in). On sign-off: remove the D202 probe set (`GE_AUDIOTRACE` additions,
`[DOORSND]`, `[SLOTWRITE]`, `GE_PULLTRACE`, `GE_DMEMWIPE`, `GE_NOWET`,
`GE_BANKDUMP`, `GE_KEYMAPDUMP`) and close D202.

**Status (M-66b): disposition C IMPLEMENTED + measured — ownerless
infinite-loop SFX fade out ~2.5 s after starting on PC; faithful N64 behaviour
otherwise. Awaiting user by-ear verification, then probe removal + close.**

---

### M-67 — Static analysis exhausted: full-bank data scan clean, DSP audit complete, reference clip REFUTED as an in-game capture; new `GE_VOICEDUMP` per-voice probe added for the decisive runtime A/B (this session)

**1. Full-bank SFX data scan — all 261 sounds clean.**
`scratchpad/sfx_bank_scan.py` walks every sound's wavetable/book/data in the
ROM: no out-of-range ADPCM table indices, all books valid (order=2, npred 1–4),
all data within segment bounds, all type-0. The old decoder's silent clamp
(`if tableIndex >= npred: tableIndex = 0`) had been masking this check — with
it removed as a hard error, the whole bank passes. No stale/corrupt-table
hypothesis is possible; the data side of "wrong sample" is closed.

**2. DSP audit complete (vs PD ground truth).** ADPCM scalar decode
(nibble→ins, predictor loop, clamp), the resample algorithm + 256-entry table,
the envelope mixer, and the pitch path (`alCents2Ratio` pure/correct; keymap
formula `keyBase*100 + detune - 6000`, snd.c:803/807) all match the PD port
line-for-line. The `A_LOOP` (flags&2) branch in `aResampleImpl` is **dead code
in GE**: the resampler filter's `first` is only ever 0 or 1 (load.c:461/530);
A_LOOP(2) is never passed to aResample. Not a bug — closed.

**3. The reference clip is refuted as an in-game capture.**
`scratchpad/ref_match_all.py`: `B00I00S2D.wav` does NOT waveform-match any of
the 261 SFX decodes (best single CC = 0.19 ≈ noise; index 45's waveform CC is
0.048), and `ref_match_mix.py` shows no PPK mixture (46+122+ricochet) matches
either (best 0.09). M-61's "+0.999 with index 45" was purely an
**envelope-correlation artifact** (two percussive sounds with similar decay
envelopes). The "N64 plays index 45 for the PPK" premise is dead; the clip's
provenance is unknown and must be asked of the user.

**4. M-63 final-mix cross-check was inconclusive, not exonerating.**
`scratchpad/m63_crosscheck.py`: door events (dumppos 23296/54976, index 203)
show CC≈0 vs offline decode, but those segments are dominated by music
(broadband RMS 2000–8000, no SFX periodicity). CC≈0 in a music-masked final
mix proves nothing about which sample the voice played. A per-voice dump (item
5) or a music-off capture is required.

**5. New probe: `GE_VOICEDUMP=1`** (`port/src/mixer.c`, temporary, D202 diag).
Writes each voice's resampled mono stream — the `aEnvMixer` input, i.e.
post-resample/pre-envelope, per-voice, no music/reverb masking — to
`voicedump.raw` as records `[u32 stateAddr][u32 nSamples][u64 us][s16 × n]`.
The `[WIRE]` lines (load.c `alLoadParam(SET_WAVETABLE)`) and the first
`[AUDIOTRACE] sndPlaySfx` line now also carry `t=<µs>` (sysGetMicroseconds)
so all three streams correlate in time. Offline matcher:
`scratchpad/voicedump_match.py` (each record vs all 261 pitch-resampled
decodes). Self-test: `scratchpad/d202-m67/` (25 s `-level_09`, 5.7 MB dump,
clean parse, records match their sources once the item-6 direction fix is
applied).

**6. Two tooling bugs found in the self-test (my scripts, not the game):**

- **Resample direction was inverted in every offline model so far.** The
  hardware playhead advances `pitch/16384 = ratio` input samples per output
  sample (`aResampleImpl`: `pitchAccumulator += pitch<<1; in += accum>>16`),
  i.e. `y[k] = x[ratio·k]` — ratio < 1 plays SLOWER and lower. The correct
  offline model is `resample_linear(native, RATE*ratio, RATE)`, not
  `(native, RATE, RATE*ratio)` as used in M-63's cross-check and the first
  matcher pass. **All prior CC results at pitch ≠ 1 are suspect** (the M-63
  door check doubly so).
- **WIRE bases are absolute; bank offsets are segment-relative.** The
  in-memory wavetables hold `base = SFXTBL_START(0x102F19A0) + bank offset`;
  matching WIRE entries against raw bank offsets finds nothing.

**7. GE's "music" is an SFX-sequence system with its own sample set.**
`sndNewPlayerInit(ALSeqpSfxConfig*)` (snd.c:168; config in src/music.c:823,
`MUSIC_SFX_SEQ_MAYBE_MAX_SOUNDS = 8`): music notes are sequenced SFX played
through the same 8-voice alSynth pool. The WIRE log shows ~8 music samples
whose bases sit at ROM 0x103ed1e8–0x10407860 — 240–350 KB **past** the last
SFX-bank sample — with wavetable structs allocated contiguously after the SFX
bank's. Music notes do not pass through `sndPlaySfx`, so they are invisible to
the `[AUDIOTRACE]` request probe: persistent voices in a voicedump are usually
music, and their "best match" against the 261-SFX bank is coincidental
(music samples reuse field recordings; free-ratio sweep found e.g. CC 0.92 vs
index 122 at transposed pitch). Any per-voice analysis must subtract music
first (or use the timestamped WIRE assignments to identify each voice's sample).

**8. Open anomaly parked for the next session: sound 232 (PICKUP_GUN_SFX)
requested at `-level_09` start produced no audible audio.**
Self-test trace: `sndPlaySfx(232)` at dumppos=0 → VOICE+ (count 1/8) →
VOICE- within ~1 frame; the WIRE-assigned filter was recycled to music
immediately; no env state in the dump matches decode(232) (max CC 0.48 ≈
noise, even with the corrected resample direction). Either the level script
stops it instantly (faithful N64 behaviour — needs an N64 A/B to confirm)
or something disposes it early on PC. Not chased this session.

**Status (M-67): static analysis has hit diminishing returns — every pipeline
stage (index resolution, bank data integrity, ADPCM, resample, envelope,
pitch, loop mechanism) is verified against ROM + PD ground truth, and the
reference clip is refuted. The remaining questions (PPK/door/armor "wrong
sound" by ear) require a user runtime A/B capture with the new
`GE_VOICEDUMP` probe: `GE_AUDIOTRACE=1 GE_AUDIODUMP=1 GE_VOICEDUMP=1`,
reproduce each scenario, send `audiotrace.log` + `audiotrace_wire.log` +
`voicedump.raw`. Also owed: provenance of `B00I00S2D.wav`, and the item-8
sound-232 anomaly.**

### M-68 — Decisive runtime A/B (user captures, 3× level_09): all three complaint classes play the exact ROM samples at the exact ROM pitches; no port bug found (this session)

Three interactive user captures (`audiodebug.ps1 -Play -Trace -Dump
-VoiceDump`), analysed with full-slide correlation of `voicedump.raw`
per-voice records against `decode_sound(idx)` resampled at the keymap ratio
(correct direction, M-67 item 7: `y[k] = x[ratio·k]`). A new `[EVT] t=µs
type=N state=P` probe (PORT-gated, `sndHandleEvent` top) plus a
`[STOP-EVT] … deltaUs=N` probe (release-ramp site) were added this session;
event types decode per `src/snd.h`: 1=PLAY, 2=STOP, 8=VOL, 64=DECAY,
128=END.

**1. Doors — correct.** `METAL_SLIDE_OPEN` 202: CC **0.957** @ ratio 0.500;
`METAL_SLIDE_CLOSE` 203: CC **0.906** @ 0.728; `METAL_SLIDE_LOOP` 204: CC
**0.993** @ 0.728. Event flow is textbook: PLAY → DECAY (attack=0, fires
immediately) → self-stop at the decay-scheduled STOP (204: +154 ms =
112197 µs ÷ 0.7278; 202: ~450 ms) → release ramp (`[STOP-EVT] deltaUs` =
releaseTime ÷ ratio, e.g. 1373/3999 µs) → END → dispose.

**2. PPK shot — correct.** `GUN_SILPPK_A` 46: CC **0.90–0.97** @ ratio 0.728
across four separate shots (full-slide, any offset). Combined with M-59
(46 + 122 CART_SPENT + a ricochet is exactly what the ROM scripts for a PPK
hit), the user's "gunshot plays broken glass/slap/ricochet" is the original
design, reproduced faithfully.

**3. Body armor pickup — correct, full length.** `ARMOUR_COLLECT` 81:
voice lifetime measured **833 ms** from `[EVT]` timestamps (PLAY → decay-
scheduled STOP), against ROM prediction 469649 µs ÷ 0.5612 = **837 ms**;
release ramp 3563 µs; CC **0.828** vs decode(81) @ ratio (window contains
other concurrent voices, hence <0.95).

**The M-67-era "instant death" metric was a line-count artifact.** Log lines
≠ time: a voice stopped by the door script's `stop-sounds`
(`door7F053B10`, `src/game/propobj.c` — stock logic, identical on N64) when
the player shuts a door quickly produces only a few log lines between birth
and death even when it plays its full ~20 ms. With µs timestamps the STOPs
land exactly at their scheduled times; no voice is disposed earlier than its
decay/stop schedule in any of the three captures.

**13 `[EXPIRE]` events in the final capture:** the M-66b guard expired 13
ownerless `METAL_SLIDE_CLOSE_SFX` (203) loops, each at exactly 2.5 s (2 s
delay + 0.5 s fade). This remains the single outstanding PORT deviation;
whether the N64 lets that loop run until level exit is the open by-ear A/B
(see M-66b).

**Status (M-68): D202's "wrong sample" hypothesis is closed negative for all
three user complaint classes — PC plays exactly what the ROM specifies,
verified per-voice at runtime. Remaining: (a) N64 A/B on door-close-loop
duration to confirm or retract the M-66b cap; (b) the parked sound-232
anomaly (M-67 item 8); (c) once the user confirms by ear, remove the D202
probe set (`GE_AUDIOTRACE`/`GE_AUDIODUMP`/`GE_VOICEDUMP` probes in
`src/snd.c`, `src/libultra/audio/load.c`, `port/src/mixer.c`,
`src/game/propobj.c`) and decide the M-66b guard's fate.**

### M-69 — Full-corpus exact match (scripted `-level_09`, 320 SFX requests): 286/320 OK after tooling fixes; the remaining failures are *dropped allocations*, not corruption (this session)

New offline tool `scratchpad/exact_match.py` (gitignored scratchpad; helpers
`rom_sfx_decode.py`, `m63_crosscheck.py`) matches **every** `sndPlaySfx`
request in a full trace to its voice and slides the ROM-decoded PCM
(resampled at the keymap ratio, M-67 item 7 direction) against that voice's
`GE_VOICEDUMP` stream. Corpus: `scratchpad/exact_run/` (scripted `-level_09`,
61 input entries, `GE_AUDIOTRACE` + WIRE + `GE_VOICEDUMP`) — **320 requests**.
Chain: request → its own `[EVT] type=1` PLAY (states are recycled across
requests, so the wire window must anchor on *that* PLAY) → WIRE record with
the same wavetable in [play−10 µs, play+50 ms] → decoder index → env-state
slot address → voicedump records for that slot.

**Result: 286/320 OK (CC ≥ 0.85).** The initial 241 OK / 35 MID / 44 BAD split
was almost entirely tooling artifacts, not audio:

1. **`GE_VOICEDUMP` records are rendered ahead — the µs stamp is the END of
   the block.** Each record's n samples end at its stamp, spaced 1/22050 s.
   Treating stamps as start offsets misplaces content by up to one block and
   produced a phantom "stale content" in pv20's slot (sound 29 looked ~180 ms
   early). With per-sample record times, sound 29's onset lands at **+263.8 ms
   vs its WIRE timestamp +264 ms — exact**. Cross-slot correlation confirmed
   the dump attribution was never wrong (max CC vs neighbouring slots ≤ 0.076;
   slot content is unique per voice).
2. **`slide()` needed step-1 refinement around the coarse best hit.** Coarse
   stepping misses sharp onsets: sound 29 measured CC 0.590 at the coarse
   argmax and **0.970 one sample later**. (Also fixed a bug: the best-offset
   tracker was only updated in the last coarse loop, so refinement searched
   around offset 0.)
3. **Clip the reference at voice death.** Requests whose voice is cut by a
   DEACTIVATE/STOP after PLAY (e.g. idx=109, killed at +131 ms) cannot match
   the full ROM decode (CC 0.016); clipping the ref to the live window gives
   CC **0.999** at offset +0 ms. The matcher now parses `[EVT]` per state and
   clips accordingly.

**The remaining ~34 failures are not corruption:**
- **"NO WIRE AT PLAY" / DROPPED (no PLAY evt):** the request was processed but
  `alSynAllocVoice` never gave it a voice (no `[VOICE+]`, no DECAY) — e.g.
  t=567204976 idx=46. Leading suspect: the 8-voice pool is exhausted under
  scripted rapid fire and low-priority sounds are dropped by design (`snd.c`
  threads `state->priority` into `config.priority`; `alSynAllocVoice`'s steal
  path has a ~512-sample delay). **Not yet verified — the next step.**
- **pv10 mismatches:** consecutive requests share wavetables, so the wire
  window can pick up the *next* request's wire and attribute the wrong slot.
  Tighten: require the WIRE to fall between this PLAY and the next PLAY on the
  same wavetable.
- Log-interleaving gotcha: AUDIOTRACE lines from multiple file handles
  interleave and corrupt some lines (a missing/corrupt PLAY line sends the
  matcher to its wide-window fallback).

**Status (M-69):** every request that *got* a voice plays the exact ROM sample
at the exact ROM pitch — this extends M-68's per-class verification to the
whole corpus. D202 remains "no port bug found"; open items are M-68's plus:
(a) confirm the dropped requests are pool-exhaustion drops that N64 would drop
identically (check `[VOICES] allocated=N / max=8` around each drop;
`src/snd.c:916`, `alSynAllocVoice`); (b) fix the shared-wavetable wire
attribution; then M-68's by-ear + N64 A/B, probe removal, close.

### M-70 — Decisive full-corpus exact match on a clean re-capture (209 requests, scripted `-level_09`): every voiced request plays the exact ROM sample; D202's wrong-sample hypothesis is refuted (this session)

The prior 316-request corpus was contaminated mid-capture (pause menu hit),
so a fresh 75 s scripted capture was taken (`GE_AUDIOTRACE=1 GE_VOICEDUMP=1`,
`-level_09`, `scratchpad/exact_run3_script.txt` fire burst) and the matcher
(`scratchpad/exact_match.py`) was fixed in three places before the decisive run:

1. **Death-clip both sides.** Clip *stream* as well as *ref* at the voice's
downstream death event (STOP/END/DEACTIVATE). M-69 only clipped `ref`; when a
slot is reused ~200–400 ms later, the unclipped 1.5 s stream window is mostly
the *next* occupant's samples.
2. **`slide()` sparse-tail bug (the big one).** The offset search was dense
only up to 9000 samples (~408 ms) and used step = span/40 beyond that. Delayed
voicing — `sndPlaySfx` queue wait, observed up to **+1.2 s** between request
and PLAY — pushes the onset into the sparse zone, where the narrow CC peak
falls between grid points: idx=134 (onset +532 ms) scored **CC 0.154** in the
matcher but **0.973** under a full-range step-4 scan of the identical data.
   Fixed: step-1 near the window start, step-4 across the *full* span, ±8
   step-1 refine around the best hit. idx=136 went 0.285 → ≥0.85 the same way.
3. **PLAY search window 600 ms → 2 s**, catching two idx=175 instances voiced
   at +1196 ms (their WIREs land with the PLAY, so pairing is unambiguous).

**Result — 209 requests:**

| bucket | count | disposition |
|---|---|---|
| OK (CC ≥ 0.85) | **200** | exact sample at exact keymap pitch |
| MID (0.6–0.85) | 4 | all idx=109, CC 0.744–0.811 — see below |
| NO WIRE AT PLAY | 5 | 1 matcher artifact + 4 never voiced — see below |

- **The 4 MID (idx=109):** manual verification on the tight `[play, death]`
   window with a ratio sweep gives **CC 0.946 at the nominal keymap ratio**
   (0.7236 = keyBase 54, detune +40, −6000 shift) — correct sample *and*
   pitch. The residual is measurement, not audio: the voicedump records are
   stamped with **wall-clock time at mix time**, and the scheduler's catch-up
   delivers them in ~32 ms bursts of ≤160-sample records with clustered
   stamps, so bisect windowing on those stamps is fuzzy by tens of ms — and
   these voices die young (killed at +67…+131 ms), leaving little clean
   signal to average over.
- **NO WIRE ×5:** idx=109@610342131 was in fact voiced — its WIRE lands at
   **+133 ms** after PLAY, outside the matcher's 50 ms wire window (all other
   idx=109 instances match fine). The other four (idx 8/40/123/202) show no
   voicing evidence anywhere: no `VOICE+` on their `newState` after t0, and no
   WIRE for their wavetable within 2–3 s *in any state*. **Not pool
   exhaustion** — `[VOICES] allocated` was 0–5/8 at each drop (run max 7/8).
   Mechanism unverified; consistent with game-logic queue/priority timing in
   code identical to the N64 build, and orthogonal to D202 (no samples were
   played, so nothing could be wrong).

**Verdict:** across 204 voiced requests spanning ~30 distinct sound indices,
**zero** play a wrong sample. Combined with M-68 (per-class runtime A/B) and
M-66 (stuck door loop = faithful N64 quirk), the D202 "wrong sample / mixer
corruption" hypothesis is refuted: what the user heard is the original design
(overlapping SFX, short decays, quick-stop truncation, the ownerless infinite
door loop).

**Status (M-70):** D202 data-closed. Remaining before final close + probe
removal: user by-ear A/B on the M-66b expiration behaviour (does the stuck
drone now fade out ~2.5 s after a door close, vs N64's forever-drone). Probes
(`GE_AUDIOTRACE`, `GE_VOICEDUMP`, `[WIRE]`, `[EVT]`, `[VOICE±]`, `[SLOTWRITE]`,
`[ENVELOPE]`, `[VOICES]`) stay in place until that pass is done; they are
`#ifdef PORT`-gated and env-var-off by default.

### M-71 — User by-ear pass on M-66b: PASS. D202 ready to close. New complaint class split out as D205 (this session)

**By-ear verdict (user, PC vs N64 side-by-side):** "the metal door sound
does not loop forever now" — the M-66b ownerless-infinite-loop expiration
behaves as intended; Disposition C validated. That was the last gate on
D202: probe removal + close is all that remains (see HANDOFF).

**New user complaint class (NOT D202 — split out to D205).** With M-70's
per-voice exact-match result standing (every voiced request plays the exact
ROM sample), the user reports by ear, and confirms against an N64 A/B that
the PC is *not* faithful: (1) general gunfire has a slap/glass-shatter
layered over the correct gunshot — audible on PC, silent on N64 for the
same shot; (2) an explosion plays a scream-like sample (the soldier-death
voice); (3) armor pickup plays a wrong sample. This session exonerated the
two remaining static suspects: the D37 bank converter offline (`scratchpad/
banktest/`: real `romdataFixupAudioBank` harness + `diff_bank.py` → all 261
entries map identically N64→PC, 0 mismatches) and the D154/D135 GBI raycast
parsers + volume law (static re-audit, ABI-correct). The remaining
explanation is that PC **requests different or extra sound indices** than
N64 in the same scenario. See D205.

### M-72 — Fourth symptom reported (unarmed melee → Klobb shot idx 106); controlled `GE_AUDIOTRACE` runs exonerate every request path; mechanism (b) now the leading suspect (this session)

User added a fourth D205 symptom: Bond's unarmed slap attack plays
Klobb's shooting sound (`GUN_B1_MGUN3_3_SFX`, idx 106) instead of the slap
effect. Also fixed the test environment: the "flaky /GS crash" of earlier
attempts was `SDL2.dll` missing from PATH (it lives in
`C:\msys64\mingw64\bin`; the exe is `-mwindows`, so a failed DLL load just
dies with 0xC0000105 and no console). Launchers now prepend it.

Controlled traces (`build-pc/d205_*.ps1`, gitignored): PPK fire at
`-level_09` requests exactly {46, one of the 20-entry ricochet table,
122} per shot — no glass/slap layer; unarmed fire-presses at levels 01–08
(all start unarmed) request only 105 PUNCHING_AIR. Static: punch hit =
constant {47,48,49}, idx 106 is data-only (`skorpion_stats.Sound`), and the
player fire-sound path cannot run for a fist — so Bond's own punch cannot
request 106; an NPC guard firing its Skorpion can. No symptom reproduces in
micro-scenarios → the layering must come from **extra events during real
gameplay** (mechanism b). Next: user `GE_AUDIOTRACE=1` capture at the exact
failing moment (splits per-symptom), then controlled explosion + armor.
Full detail in D205.
