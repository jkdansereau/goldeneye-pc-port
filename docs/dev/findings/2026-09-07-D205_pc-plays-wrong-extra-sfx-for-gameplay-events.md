# D205

<!-- from the old section-F/H table row -->
| D205 | **PC plays wrong/extra SFX for gameplay events (gunfire slap layer, explosion→scream, armour pickup, melee→Klobb idx 106) — user A/B vs N64 (M-71→M-78).** — full `## D205` entry at file tail | CLOSED (M-78) — root cause was D206; all four symptoms resolved + user-confirmed by ear. |

## D205 — Wrong/extra SFX requested on PC vs N64: explosion→scream, armor pickup wrong sample, slap/glass layered over gunfire, melee→Klobb shot (M-71/M-72)

**Symptoms (user by-ear; N64 A/B confirms non-fidelity):**
1. General gunfire: a slap/glass-shatter plays *over* the correct gunshot on
   PC; silent on N64 for the same shot.
2. Explosion: plays a scream-like sample (the soldier-death voice) instead
   of / over the explosion.
3. Armor pickup: wrong sample.

**Exonerated — do not re-investigate:**
- Playback chain: M-68/M-70 exact match — every voiced request plays the
  exact ROM sample at the exact ROM pitch (204/204 voiced, zero wrong-sample).
- D37 bank converter: offline harness runs the real `romdataFixupAudioBank`
  on the same ROM bytes and diffs N64 vs PC images entry-by-entry —
  261/261 identical (base/len/type/keyMap/env), 0 mismatches
  (`scratchpad/banktest/harness.c` + `diff_bank.py`; gitignored, local-only).
- D154/D135 GBI raycast parsers ABI-correct; volume law
  (`sub_GAME_7F0537B8`: d≤200→max, sqrt falloff to 5000, linear to 6000)
  and suppression logic faithful (M-71 static audit).

**Therefore: PC requests different or extra sound indices than N64 in the
4. Unarmed melee: Bond's slap attack plays Klobb's shooting sound
   (`GUN_B1_MGUN3_3_SFX`, idx 106) instead of the slap effect (M-72).

**M-72 (controlled runtime traces, this session):** environment note first —
the earlier "flaky /GS crash" was an investigator artifact: `SDL2.dll` lives
in `C:\msys64\mingw64\bin`, which is NOT on the system PATH and not copied to
`build-pc/`; launches without it die instantly (exit 0xC0000105), with or
without any env vars. Prepending mingw64/bin to PATH in the launcher
(`build-pc/d205_*.ps1`, gitignored) gives clean 40–60 s runs. Results under
`GE_AUDIOTRACE=1`: (1) PPK fire at `-level_09` (unarmed start; first Z picks
up the spawn PPK, idx 232): every shot requests exactly {46 GUN_SILPPK,
one of 27/28/39/40 — all inside the 20-entry `ricochet_sounds_small` table,
gun.c:415 — so `rnd1 % 20` is IN RANGE, not OOB, 122 CART_SPENT}. No glass
(70), no slap (47–49) layering. (2) Unarmed melee at levels 01–08 (all start
unarmed): fire-press requests ONLY 105 PUNCHING_AIR (whiff); punch-HIT path
is the compiled constant `punch_sounds` = {47,48,49} (gunfire.c:2338), whiff
= 105 (chrprop.c:1465). No 106 anywhere. (3) idx 106 is referenced by NO
code constant — it enters only via `WeaponStats.Sound` (`skorpion_stats.
Sound = 0x6A`) through `bondwalkItemGetSound`; the player fire-sound path
(gunfire.c:3192–3200) runs only in `GUN_ANIM_STATE_FIRE` and a fist sets
PUNCH state with `.Sound = 0`, so Bond's own punch cannot request 106 — an
NPC guard firing its Skorpion (`chraction.c:5588`) can. **Conclusion:
all four symptoms' request paths are faithful constants/tables, and none
reproduces in controlled micro-scenarios; the leading suspect is now
mechanism (b) — extra/different EVENTS during real gameplay (a guard firing
as you punch → 106 layered on the slap; an NPC scream at the blast; a second
surface hit adding glass/slap to a shot).**

**M-73 (user combat capture) — ROOT CAUSE WITHDRAWN by M-74 below.** The
session analysed the user's full-combat `GE_AUDIOTRACE=1` capture
(`build-pc/audiotrace.log`) and concluded that guards were stuck
re-triggering weapon fire sound idx 109 (`GUN_B4_BOLTACTION_SFX`,
AK47/Spectre) "256x over 29.7 s with no corresponding bullets", pinning it on
`stanTestLineUnobstructed` guard->Bond LOS returning clear on converted
collision geometry. **M-74 re-counted the same file and every load-bearing
number was wrong** — see below. The code walk M-73 produced is still accurate
and worth keeping as reference: `chrlvFireWeaponRelated` (`chraction.c:6530`)
passes `phi_a2 = sp27C || sp278` to `sub_GAME_7F02BFE4` (:6902), so a
non-bullet auto tick still plays the weapon sound, gated by
`SoundTriggerRate`/`field_178[hand]` (and by `CHRHIDDEN_FIRE_TRACER` 0x80,
cleared once per `chrlvTriggerFireWeapon`); sustained attack needs
`seen_bond_time >= g_GlobalTimer - CHRLV_SEEN_RECENT_CHECK` (:6593),
refreshed by `setSeenBondTimeToNow` behind LOS queries (:3917/:3972,
`chrCanSeeBond` :3945). That mechanism is real. What M-74 shows is that
**nothing in the capture indicates it ever misfired.**

**M-74 (re-count of the SAME capture — M-73's evidence does not hold, this
session):** counting only the canonical one-per-call line
(`sndPlaySfx: t=... soundIndex=...`) in `build-pc/audiotrace.log`:

| M-73 claim | Actual |
|---|---|
| idx 109 requested **256x** | **128x** — the count was doubled |
| "~11 guards each re-triggering" | 128 / 29.73 s = **4.31/s aggregate**, ~0.4/s per guard |
| "sustained roar, gate blown open" | AK47 `SoundTriggerRate` = `RATE_AK47` = 4 `g_GlobalTimer` ticks (`assets/obseg/gun/gunWeaponStats.inc.c`) => design ceiling **15/s per guard**. Observed is **~6x UNDER** the weapon's own rate — `field_178` is not even the binding constraint |
| "no corresponding bullets/impacts anywhere near that rate" | In the same 29.7 s window: **~30 ricochet / wall-hit requests** (idx 19, 20, 22-27, 29, 31-33, 35-37, 40, 41), **69 x2** (flesh hit), **123-132** (12 body-falls), **134-147** (14 guard yelps). Guards were firing *and* hitting |
| "only ~32 non-109 requests in the window" | ~150 |

Also corrected:

- **The 128-vs-256 gap is the double-log artifact D204 already documented.**
  D204 corrected M-63's "idx 109 fired 94 times in 60 s" with exactly this:
  "the count is doubled by the probe (it logs two lines per call)". M-73
  re-made the same mistake on the same sound index. Count `sndPlaySfx: t=`
  lines, never bare `soundIndex=` matches.
- **The monotone index runs are ground-truth round-robin, not a broken
  selector.** The capture contains strictly increasing sweeps
  `134,135,...,147` (GET_HIT_MALE) and `123,124,...,132` (BODY_FALL), one
  request each, never repeating. This looks exactly like a `random() % n`
  gone wrong and was checked specifically: `chraction.c:2454-2470` selects
  `male_yelps[male_guard_yelp_counter]` and post-increments modulo 25 — a
  deliberate cycling counter in byte-matched game code. Faithful. Do not
  re-open.
- The trace file spans 1321.85 s wall (288 requests); the actual firefight is
  ~40 s of it (`t` in 715754-715800 s). M-73's "29.7 s" is the 109 span only.

**Net: the 109 traffic in that capture is normal, in-spec guard combat.** The
LOS / converted-geometry root cause is **withdrawn** — it was inferred from
counts that do not survive re-derivation, and M-73's Next steps 1-3 (probe
`setSeenBondTimeToNow`, decode pccg stan in Python, replay guard->Bond rays)
would have been a full session spent on a phantom. The only genuinely open
observation left from that capture is that the firefight lasted a long time,
which is a gameplay-fidelity question for an N64 A/B, not evidence of an
audio bug.

**Where that leaves D205.** Every layer upstream of presentation is now
proven correct: bank conversion (261/261 identical, M-71), requested indices
(182 explosion x1, 81 armor x1, 46 PPK, 47/48/49 punch hits, 105 whiffs — all
correct and in range, here and in M-72's controlled runs), and sample+pitch
(M-68/M-70 exact match). That leaves exactly one untested layer between "the
right sample" and "what the user hears": **spatial presentation — per-voice
volume, pan and concurrency.** It fits all four complaints in one shape: if a
sound's apparent *position* is wrong, a guard firing behind you is by ear
indistinguishable from "a gun sound came out of my punch", a nearby guard's
death scream reads as "the explosion screamed", and a ricochet reads as "a
slap over my gunshot". The user's ear reports *what and where*; only *where*
has never been verified. It is also the youngest code in the stack —
`sndCreatePostEvent` was stubbed out entirely (D138) until M-65 un-stubbed
it, so every distance/pan post-event has been live for only a few sessions
and has never been A/B'd against N64.

**Original M-71 framing (kept as background; DEMOTED by M-74 — every index
observed in the user's capture was correct, so "PC requests a different
index" is no longer the leading shape of the bug):** PC requests different or
extra sound indices than N64 in the same scenario. Two mechanisms, ranked:
(a) **Converted level/setup data** — sndID / explosion-type / item-sound
    fields in the pccg sidecars differ from ROM. Explosions play
    `sndPlaySfx(g_musicSfxBufferPtr, sp44->sndID, NULL)`
    (`src/game/explosion.c:285`) with `sp44->sndID` =
    `g_ExplosionTypes[type].sndid`; the *type* comes from level data.
    Explosion SFX are idx 169–183 (EXPLOSION_2A_SFX=169 … EXPLOSION_9_SFX=
    183, bondconstants.h). A scream is outside that range, so either the
    type index is wrong (→ a non-explosion sndID) or a second event fires.
    Armor: idx 81 verified exact in M-68 — a "wrong" armor sample means a
    different index is requested in the user's scenario (or a layered
    second sound).
(b) **Extra hit events** — converted collision geometry makes bullet
    raycasts or explosion proximity checks hit surfaces/NPCs the N64
    misses. The slap+glass-over-gunshot signature is exactly the
    non-penetrating-OBJ-hit path: `sub_GAME_7F064720`
    (`src/game/gunfire.c`) plays `HIT_BULLET_GLASS_SFX` (idx 70) for every
    non-penetrating hit; ricochet tables classify AFDM (27–30) = prop only,
    GBU/TAJ/RICO_4 (19–26, 31–33) = wall only.

**M-74 step 1 — static audit of the presentation layer: H-A FALSIFIED,
H-B narrowed to "faithful but shallow". Done this session.**

The pan machinery is complete and faithful end to end:
`src/snd.c:461/530` computes `pan = clamp(soundState->pan + sound->samplePan -
AL_PAN_CENTER)` → `alSynSetPan` (`synsetpan.c`, posts `AL_FILTER_SET_PAN`) →
`src/libultrare/audio/env.c:400-402` turns pan into independent L/R targets via
the `eqpower` table (`ltgt = volume*eqpower[pan]`, `rtgt =
volume*eqpower[LEN-pan-1]`) and emits `aSetVolume(A_LEFT|A_RATE)` /
`(A_RIGHT|A_RATE)` → `port/src/mixer.c` `aSetVolumeImpl` decodes A_LEFT
(0x02) into `volCur/volTgt[0|1]` and `aEnvMixerImpl` applies `vol[0]`/`vol[1]`
to `dry[0]`/`dry[1]` independently (:488-496). Nothing is dropped or collapsed
to mono. **The port's stereo path is correct.**

**But GE never uses it.** `AL_SNDP_PAN_EVT` (`snd.h:41`) is handled at
`snd.c:527` and **posted by nothing anywhere in the tree** — grep returns the
enum and the handler, no callers. So `soundState->pan` keeps the
`AL_PAN_CENTER` set at `snd.c:842` for every SFX's entire life, and the only
pan input is the static per-sound `samplePan` from the bank. Dumped from the
converted bank image this session: **252 of 261 sounds have `samplePan == 64`
(= `AL_PAN_CENTER`)**; the 9 exceptions are 4, 24, 44, 74, 84, 104, 123.
Every sound in the user's capture is centred — 46, 47, 48, 49, 81, 105, 106,
109, 182 all `samplePan = 64`.

**⇒ GoldenEye does not spatially pan SFX at all, on N64 or PC.** There is no
pan to get wrong. H-A is dead; do not re-open it, and do not write the
"pan varies with bearing" probe (M-74 step 2 as originally drafted) — the
answer is known and it is "never, by design".

Field-coverage gap in M-71's bank exoneration closed at the same time: the
converter writes `samplePan`/`sampleVolume`/`flags` at `+24/+25/+26`
(`port/src/romdata.c:890`), correct for the PC layout (3 × 8-byte pointers
before them), and `scratchpad/banktest/diff_bank.py` already diffs them as
`panvol` — re-run this session, **261/261, 0 mismatches**. (M-71's prose
listed only base/len/type/keyMap/env; its actual coverage was broader,
including a 24-byte sample-data fingerprint.) The volume formula
(`snd.c:453`, `:565`) also has no PC-side divergence: peak intermediate is
127 × 32767 × 127 ≈ 5.3e8, inside s32 on both platforms, and `vol` is `s16`
(`snd.h:90`) which holds the 32767 the `[VOL]` probe reports.

**What this leaves.** GE's only spatial cue for an SFX is distance volume, and
that law is shallow: the capture's `[DISTVOL]` lines show flat max out to
~200 units, then 0.60 of max still at dist 1794. So a guard firing across the
room plays **near-full volume, dead centre**, at a per-sample volume
comparable to the player's own actions (idx 109 `sampleVolume` = 90; punch
47/48 = 100, 49/105 = 110). **That is faithful, and it is also exactly the
perceptual condition that generates all four of the user's reports** — with no
spatial cue, a correct sample played at correct volume from across the room is
by ear a sound at your own position. This reframes D205: the complaints are
fully consistent with *correct* sounds overlapping, and the only remaining
question is whether the PC generates **more** such overlaps than the N64
(H-D). Nothing on the PC side can settle that; it needs the N64 A/B.

**Next (cheapest first; re-ranked by M-74 — M-73's list is withdrawn):**
0. ~~User capture~~ — DONE (M-73), but see M-74: it does NOT show a guard
   re-trigger bug. 182 and 81 were both requested correctly and exactly once.
1. ~~Static audit of the presentation layer~~ — **DONE (M-74 step 1 above).
   H-A falsified: the port's stereo path is correct, but GE never pans SFX at
   all (no `AL_SNDP_PAN_EVT` poster exists; 252/261 bank sounds are
   `samplePan == 64`). Bank `panvol` re-diffed 261/261 clean. Do not re-open.**
2. ~~Pan probe run~~ — **cancelled by step 1: pan cannot vary, by design.**
3. ~~The PC side is now exhausted~~ — **SUPERSEDED by D206.** The premise
   ("requested indices are correct") was wrong: the indices are correct as
   *values* but are resolved one slot too high, which D206 root-causes. What
   survives from this step: the pan audit (H-A dead) and the bank `panvol`
   re-diff. Original text: bank, playback, requested indices,
   sample+pitch, pan and per-sound volume are all verified faithful. The only
   remaining hypothesis is **H-D** — that the PC generates *more* overlapping
   combat events than the N64 — and nothing measurable on the PC alone can
   settle it. **The N64 A/B is now the gating step, not an optional
   corroboration.**
4. **User-side, batched, held until 1-3 report:** (a) ROM samples 106
   (Skorpion/"Klobb") vs 109 (AK47) as WAVs via `scratchpad/rom_sfx_decode.py`
   for the user to settle the Klobb identification by ear — their by-ear
   identification stands either way, this decides *which* sound, not whether
   they heard it; (b) N64 A/B of one moment: punch a guard while others fire —
   does the same combat sustain on N64?
5. After root-cause + fix: user re-test of melee / explosion / armor pickup
   vs N64.

**Ranked hypotheses after M-74:**
- **H-A (top) — pan/spatialisation wrong or absent in the mix.** Untested end
  to end; youngest code in the stack (D138/M-65 lineage).
- **H-B — distance law right, scale wrong.** The capture's `[DISTVOL]` lines
  do follow the documented curve (flat max to ~200, falloff after) but only
  reach 0.60 of max at dist 1794. Never compared to N64 loudness at the same
  distance.
- **H-C — voice-pool masking.** 8-voice soft limit vs 11 guards + music. Weak:
  M-70 measured alloc <= 5/8.
- **H-D — real content difference (PC guards engage where N64 guards don't).**
  Possible, but after M-74 it has *no* supporting evidence. Gated behind the
  N64 A/B.

**Probes added this session (live under `GE_AUDIOTRACE`, catalogued in
GE-ENV-PROBES.md):** `[VOL] t=… state=… rawVol=… playing=…` (`src/snd.c:
556`, per-VOL-event value) and `[DISTVOL] pos=(…) player=(…) dist=… vol=…`
(`src/game/propobj.c:12780`, the distance-attenuation funnel
`sub_GAME_7F053894`).


**M-78 (from the §F row, appended 2026-09-15 ctx-opt):** **CLOSED** — root cause was D206; all four symptoms resolved + user-confirmed by ear vs N64. No separate presentation-layer fix needed.
