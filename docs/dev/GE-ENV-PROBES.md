# GE_* environment probes — consolidated list

Every `getenv("GE_…")` in the tree. The table prose is hand-curated;
**`tools_pc/gen_env_probes.py`** re-greps the live sites and reports drift
(NEW / GONE vars + a fresh file:line map) — run it before trusting the
File:line cells. Last reconciled 2026-09-04 (M-51). All are **env-gated**: unset = zero behavior
change, so they are safe to leave in a release build (they only cost a
`getenv` on the guarded path). "Dead" below means *the finding it was built
for is closed* — the probe is strip-candidate scaffolding, not that it does
anything harmful.

Two classes:
- **Infra / live tooling** — deliberately kept, used by the playtest harness
  (`tools_pc/debug.ps1`, `playtest.sh`, `level_sweep.sh`) or by an
  investigation still in progress.
- **Dxx diagnostic** — added to chase one finding; most are now closed.

## Infra / live tooling

| Env var | File:line | What it does | Status |
|---|---|---|---|
| `GE_PCDUMP` | `port/fast3d/gfx_opengl.cpp:1079` (`is_pcdump`), `gfx_opengl.cpp:700`, `port/src/video.c:188`, `port/src/config.c:46` (`configGetFrameDump`) | Per-frame framebuffer PPM dump to `$PCDUMP` dir (verification-ritual golden captures, `tools_pc/framediff.py`/`pixcount.py`). Also settable via `[Debug] FrameDump` ini (env wins). **M-33/D168: the writer now emits rows top-to-bottom — captures before that fix are vertically flipped.** | **live** |
| `GE_INPUTSCRIPT` | `port/src/input.c:306` | Headless scripted controller-0 input: `"<frame>:<tok>,…;…"` (`SUP/SDOWN/SLEFT/SRIGHT/SNONE` sustain, buttons pulse). Sole input source when set. | **live** |
| `GE_INPUTLOG` | `port/src/input.c:329`, `port/src/config.c:53` (`configGetInputLog`) | Logs computed pad state (buttons + stick) each poll when non-zero. Also `[Debug] InputLog` ini. Also traces D165 `menuptr est/tgt/eff/stick`. | **live** |
| `GE_STARTMENU` (+ `GE_STARTMENU_PAGE`, `GE_STARTMENU_DIFF`) | `src/game/lv.c:398-402` (`#ifdef PORT`) | Boot straight into a front-end menu id (13=MISSION_COMPLETE, 10=BRIEFING, 7=MISSION_SELECT, 12=MISSION_FAILED, 6=MODE_SELECT); `_PAGE`=folder row (def 1=Dam), `_DIFF`=0..3. Crash-test a screen fast. | **live** |
| `GE_UNLOCK_ALL` | `src/game/debugmenu_handler.c:1098` (`get_debug_enable_all_levels_flag`, `#ifdef PORT`) | Mission-select shows every solo level (playtest jump-to-any-level aid). Cached in a `static int c`. | **live** |
| `GE_OPTIONSOVERLAY` | `port/src/optionsoverlay.c:231` | Force-enable the F10 port-layer options overlay (D184 / PR #10) regardless of ini. `atoi != 0`. | **live** |
| `GE_D204` | `port/src/audio.c` (`audioSetNextBuffer`) | **Audio-health monitor (D204).** One line per 5 s of wall clock: `rt=` produced-audio/wall ratio (1.000 healthy; below 1 = the 30 Hz audio retrace is starving and the device is padding with silence), `q=` SDL queue depth / `queueLimit` (pinning at the limit with `drop=` climbing = overproduction, the "eventually no audio" state), `drop=` blocks discarded, `max=` largest block vs its `g_MaxFrameSize*4` allocation. **Deliberately cheap (~30 clock reads/s) — safe to leave on for a full playtest, unlike `GE_MIXERTRACE`.** The instrument of record for D202. | **live** |
| `GE_D204_OLD` | `port/src/audio.c` (`d204OldMode`) | Restores pre-D204 behaviour (`osAiGetLength()` reports the whole SDL queue depth; `queueLimit` back to 8192) so before/after can be A/B'd in **one binary**, without build-to-build variance. Temporary — remove when D204 closes. | **live** (D204 A/B) |
| `GE_AUDIOTRACE` | `src/snd.c` (`sndPlaySfx`, `sndDeactivate`), `src/game/propobj.c` (door sounds), `src/libultra/audio/load.c` (`alLoadParam`) | Per-`sndPlaySfx` resolution trace to `audiotrace.log`: `soundIndex` → `ALSound*`/keyMap/wavetable/base/len/book, the returned `ALSoundState*`, every `sndDeactivate`, and wavetable bind events. M-65 additions: `[VOICE+]`/`[VOICE-]` pairing every voice acquire/release with sound/state/flags (NB: written from two threads to one unbuffered FILE* — occasional torn lines, parse defensively), `[SLOTWRITE]` door slot ownership, `[RETRIGGER-POST]`, `[WAVELOOP]` (per-sound `ALADPCMloop` presence), `[DOORSND]` every door sound state change incl. an `ORPHAN-both-slots-busy` verdict. M-66b addition: `[EXPIRE]` — posted whenever the port-side ownerless-infinite-loop expiration is scheduled (sound/state/delay/fade); this line is **permanent** (part of the D202 option-C fix), unlike the rest of the probe set which is removable on close. M-67/M-69 additions: `[WIRE] t=µs filter=… <- table=…` (`src/libultra/audio/load.c`, every wavetable→decoder bind, µs-timestamped) and `[VOICEMAP]` (`src/libultra/audio/synthesizer.c`, full PVoice→decoder/resampler/env-state address map at synth init — this is how offline voicedump slot addresses tie to WIRE filter addresses). M-68 additions: `[EVT] t=µs type=N state=P` + `[STOP-EVT] … deltaUs=N` (event types per `src/snd.h`: 1=PLAY, 2=STOP, 8=VOL, 64=DECAY, 128=END). M-71 additions: `[VOL] t=µs state=… rawVol=… playing=…` (`src/snd.c`, per-VOL-event value before the slot-volume law) and `[DISTVOL] pos=(…) player=(…) dist=… vol=…` (`src/game/propobj.c`, the distance-attenuation funnel `sub_GAME_7F053894` — this is how an SFX's final volume is computed from hit position). NB: lines from the multiple file handles interleave — occasional torn/corrupt lines, parse defensively. `dumppos=` gives the exact byte offset into `audiodump.raw` (quantised to the 2880-byte audio block). Low cost. | **live** (D202/D205) |
| `GE_PULLTRACE` | `src/libultra/audio/load.c` (`alAdpcmPull`) | `[PASTEND]` assertion: fires if a voice's `f->sample` exceeds its wave's total samples while `nOver == 0` (i.e. an unstopped voice keeps decoding past the end instead of zero-filling). Rate-limited to 40 lines. M-65: 0 hits — exhausted voices go silent correctly. | **live** (D202) |
| `GE_DMEMWIPE` | `port/src/mixer.c` (`aClearBufferImpl`) | Zeroes the whole software DMEM at the frame boundary (detected as `alMainBusPull`'s clear of `AL_MAIN_L_OUT`, addr 1088). Tests whether audio state survives across frames in DMEM. M-65: no-op on output — ruled out. | **live** (D202) |
| `GE_NOWET` | `port/src/mixer.c` (`aSetVolumeImpl`) | Mutes the reverb send (`wetamt = 0`), isolating the reverb delay lines as a possible source of persistent signal. M-65: drone unchanged — ruled out (NB `aPoleFilterImpl` is still an unimplemented no-op, D199). | **live** (D202) |
| `GE_BANKDUMP` | `port/src/romdata.c` | `BANKSRC` lines: the source ROM-layout bank tree per `soundIndex` (instrument → sound → envelope pointers + `decayTime`). Used to prove sound 203's `decay=-1` is genuine ROM data, not a conversion bug. One-shot at load. | **live** (D202) |
| `GE_FORCEALARM` | `src/game/propobj.c` (`handle_alarm_gas_timer_calldamage`, `#ifdef PORT`) | **D207 diag (temporary).** Forces the in-game alarm klaxon (`alarmActivate`) on a frame counter — on ~2–10 s, off, on again — and emits a per-frame `[D207] f=… active=… ptr_alarm_sfx=… playstate=… locked=…` line to `audiotrace.log`. Repro harness for the "alarm SFX overrides other audio" report when a level's own alarm can't be triggered headlessly. Remove when D207 closes. | **live** (D207) — temporary |
| `GE_KEYMAPDUMP` | `port/src/romdata.c` | `KEYMAP` + `ENVSRC` lines: raw source bytes of every `ALKeyMap` / `ALEnvelope` in the ROM-layout blob, for byte-wise comparison against runtime values. One-shot at load. | **live** (D202) |
| `GE_AUDIODUMP` | `port/src/audio.c` (`audioSetNextBuffer`) | Raw-dumps every mixed s16/stereo/22050 Hz buffer to `audiodump.raw` in the CWD for offline waveform inspection. Length ÷ 88200 = seconds of audio produced. | **live** (D202) |
| `GE_VOICEDUMP` | `port/src/mixer.c` (`aEnvMixerImpl`) | Dumps each voice's resampled mono stream (post-resample/pre-envelope, i.e. the env-mixer input) to `voicedump.raw` as records of `[u32 stateAddr][u32 nSamples][u64 us][s16 × n]`. **The µs stamp is when the block was MIXED — records are rendered ahead, so a record's samples END at its stamp** (M-69: treating stamps as start offsets misplaces content by up to one ~33 ms block). `stateAddr` = the env-mixer state address from `[VOICEMAP]`. Used with `scratchpad/exact_match.py` for full-corpus per-voice sample verification. | **live** (D202) |
| `GE_MIXERTRACE` | `port/src/mixer.c`, `src/audi.c` (`amDmaCallback`), `src/libultra/audio/load.c` (`_decodeChunk`, `alLoadParam`) | Per-opcode mixer trace to `mixertrace.log` (DMEM addresses, ADPCM books, decoded frames, `[DMAREQ]`/`[DMAHIT]`/`[DMAMISS]`/`[BINDTABLE]`). **WARNING: unbuffered per-opcode `fprintf`, ~25 MB/min. It slows the process enough to starve the audio thread — M-63 measured "audio at 13 % of real time" that was entirely this probe's own artifact. NEVER use it for timing-sensitive measurement; use `GE_D204`.** Grep it, never read it whole. | **live** (D202) — timing-unsafe |
| `GE_TEXPITCH` | `port/fast3d/gfx_pc.cpp:924` (`gfx_tex_pitch_fix`, cached) | `=0` disables the D183 defensive de-stride in `import_texture` (default on). RC2/RC3 texture-shear A/B knob. | **live** (RC3 not finished) |
| `GE_TEXRAW` | `port/fast3d/gfx_pc.cpp:1079` | With `GE_TEXDUMP`, also dumps the raw pre-import source bytes (`texdump/rNNN_f…_s…_WxH.bin`) so a decode bug can be told from a source-data bug offline (D183). | **live** (RC3 not finished) |
| `GE_WRAPFIX` | `port/fast3d/gfx_pc.cpp:3166` (`gfx_set_wrap_fix`) | RC3 test override for the `Video.WrapFix` D74/D167 wrap-block path (`atoi != 0` wins over the ini). | **live** (RC3 not finished) |
| `GE_SAVELOG` | `src/game/file.c:46`, `src/game/file2.c:16` (`SAVELOG` macro), `src/boss.c:749`, `src/game/objective_status.c:297,343` | Traces the campaign save/unlock chain (objective completion → `end_of_mission_briefing` → `fileWriteSave` → EEPROM). Used to confirm D157. `g_savelogObjOnce` gates the per-criterion dump to the `bossReturnTitleStage` call. | **live** (strip once a few more level boundaries are playtested) |
| `GE_D160` | `src/boss.c:744`, `src/game/bondview2.c:791`, `src/aicommands.def:10055,10181,10236,10242,10247` (all `#ifdef PORT`) | Dam exit-cutscene (D148/D160) diagnostic: trace points in `bossReturnTitleStage`, `bondviewSetCameraMode`, and AI cmds `EndLevel`/`exit_level`/`CameraLookAtBondFromPad`/`CameraSwitch`. **Investigation in progress** — user owes a `GE_D160=1` Dam run. | **live** |
| `GE_D193` | `src/game/frametiming.c` (`waitForNextFrame`, `#ifdef PORT`) | Per-wall-second line: `render=` rendered frames, `sim=` sim ticks delivered (Σ of the `deltaFrames`/`g_ClockTimer` fed downstream), `clamped=` frames whose catch-up hit `FRAMETIMING_PORT_MAX_CATCHUP`, `maxraw=` largest pre-clamp delta. Healthy steady state = `render=30 sim=60 clamped=0 maxraw=2` (measured M-80, all levels idle). Use to check whether the wall-clock timer under-advances under real AI load (D193). Cheap; safe for a full playtest. | **live** (D193) |
| `GE_D193A` | `src/game/chr.c` (`chrTick` → `d193aSample`, plus `d193aMoved` in `chrUpdateAnim`; `#ifdef PORT`) | Per-wall-second per-ticked-chr locomotion telemetry (D193/D209): `chr=` chrnum, `act=` ACT_TYPE, `speed=` world-units travelled/s, `gtd=` g_GlobalTimerDelta, `tier=` `act_gopos.unk59` SPEED tier (0 walk / 1 run / 2 sprint; `-1` when not ACT_GOPOS), `gspd=` `act_gopos.speed` (**a turn rate, not a travel rate** — 0 means running straight), `typ=` PROP_TYPE (3=CHR, 6=VIEWER), `tick=`/`move=`/`frz=` chrTick entries vs `chrUpdateAnim` calls vs freeze-gated (move « tick = the chr is being visibility-culled out of its own locomotion; `move=0` at high `speed` = WAYMODE_MAGIC), `hid=` CHRFLAG_HIDDEN, then the root-motion scalars `scale` / `xlscale` / `animrate` / `playspeed` / `mspeed` (model->speed) / `endframe` / `anim` ptr / `aidx=` that pointer reverse-resolved to its `animation_table_ptrs1` index (40 walking, 41 sprinting, 42 running, 107 walking_unarmed — map via `src/game/initanitable.c`). `GE_D193A_CHR=<n>` restricts to one chrnum. **This probe found D209**: Cradle showed `tier=1` (RUN) with `aidx=40/107` (walk anims) on every sample. | **live** (D193/D209) |
| `GE_D193B` | `src/game/model.c` (`sub_GAME_7F06D3F4`, `#ifdef PORT`) | Rate-limited (1/97) raw anim root-motion decode trace (D193): `joint=` / `flip=` / `base=` mtx channel index / `frame=` / `tmp=` decoded s16 translation triple from the anim bitstream / `angle=` / `anim` ptr / `bd=`+`bs=` the D32 u32 bitDescriptors/bitStream addresses / `njoints`. M-80 Facility: `base=0`, `angle=0` **always** (root-motion facing delta never non-zero — top lead), `tmp` ≈ (±small, ~1086 hip-height, ±small). Compare `tmp`/`angle` magnitudes against a clean N64 decomp build of the same anim+frame. | **live** (D193) |
| `GE_D172` | `port/fast3d/gfx_pc.cpp` (`gfx_sp_tri1` — SETCOMBINE dedup log + non-LOD multitex tri dump; `#ifdef PORT`) | Particle magenta/cyan colour (D172). Logs each distinct SETCOMBINE + active cycletype, and — for a 2-cycle tri that consumes TEXEL1 with `tex_lod=0` and a distinct 2nd tile — the tile fast3d *samples* for each texunit vs what the DL *configured*. **Found D172**: 40/40 particle tris configured tile 1 @ tmem 392 (fire) but sampled tile 0 (smoke) → `smoke*smoke`. FIXED (`gfx_lod_tile_offset` `return rdp.tex_lod ? 0 : i`). Probe kept inert. | **live** (D172 FIXED, probe kept) |
| `GE_DTEX` | `port/fast3d/gfx_pc.cpp:1001` | Per-`import_texture` param log (dims / line / size / lod / gen_mipmaps). Used to root-cause RC2 and D159. Kept as an inert diagnostic. | **live** (RC2/RC3 not finished) |
| `GE_TEXDUMP` | `port/fast3d/gfx_pc.cpp:1020`, `port/fast3d/gfx_opengl.cpp:700` | PPM dump of every uploaded texture + a `fmt/siz/palfmt/palidx/pal[0..3]` line. Root-caused D161 (Depot ceiling). Kept as an inert diagnostic. | **live** (RC3 not finished) |
| `GE_D116` | `port/fast3d/gfx_pc.cpp:1710,2362`, `src/game/textrelated.c:265,541` (cached `ge_d116`) | HUD/text "X-mirror" (D114/D116) vbo/uv trace. **D114/D116 CLOSED — NOT A BUG (M-33/D168): the captures were upside-down, not mirrored.** Probe is now dead scaffolding — strip on the next cleanup pass. | dead (D114/D116 closed) |

## Dxx diagnostic probes — finding closed unless noted

| Env var | File:line | Finding / what it logged | Status |
|---|---|---|---|
| `GE_D51` | `port/src/libultra.c:496`, `src/game/model.c:125,244,544` | msgQ 32-slot overflow watch / ModelSlot layout | dead (D51 closed) |
| `GE_D54` | — (all blocks stripped M-32, commit `49ce620a`) | music seq-table ABI / endianness (`ALMidiHdr`) | dead (D54 closed) — fully removed (kept here as a tombstone; the generator flags it GONE) |
| `GE_D154` | `src/game/bg.c:3418,3447,3632` (capped 64 calls) | bg room-GDL call trace (room / gdlidx / vtxoff / op / raw hdr words) | dead (D154 closed) |
| `GE_D176` | `src/game/bgfog.c:459,471`, `src/game/sky.c:325,381` (all `#ifdef PORT`) | D176 sky/fog env-match + sky-vert trace (Path B, PR #18) | **live** (D176(a)/(b) OPEN) |
| `GE_D178` | `src/game/front.c:6586,6594` (`#ifdef PORT`) | briefing-data u16 decode trace (blank-objectives, D143/D178) | dead (D178 closed) |
| `GE_D56` | `src/game/model.c:227,830` | watch `Model` raw-offset reads | dead (D56 closed) |
| `GE_D60` | `port/src/libultra.c:521,735` | gfx frame msgQ delivery trace | dead (D60 closed) |
| `GE_D61` | `port/src/libultra.c:795` (`s_d61opened`) | opens a one-shot log file | dead (D61 closed) |
| `GE_D62` | `port/src/libultra.c:477` | osRecvMesg trace | dead (D62 closed) |
| `GE_D63` | remaining: `src/game/bg.c:2525,2908,2953`, `front.c:1410,1416,7814,7889,8083,8091,8099,8366`, `image.c:2453`, `language.c:431`, `model.c:4340`, `title.c:607` | gun-barrel sub-DL clobber hunt (D63/D64/D65/D66 — the labels drift within the blocks). Stripped M-30 (`d0789358`, `gfx_pc.cpp` trail). Stripped M-32 (`49ce620a`): `memp.c`, `bg.c:2880` (incl. the bare non-PORT `d63bgprimarycount` static + entry log), `blood_animation.c`, `dyn.c`, `rsp.c`. **Remaining blocks are all `#ifdef PORT` + getenv-guarded and inert — strip candidates for a later pass.** | dead (D63 closed) — partially removed |
| `GE_D69` | `src/game/ob.c:165,227` | stage-load (D69) object trace | dead (D69 closed) |
| `GE_D69BB` | `src/game/bg.c:2460,2479,3020` | D69 bg-binary layout trace | dead |
| `GE_D69STAN` | `src/game/stan.c:266,276` | D69 stan-tile trace | dead |
| `GE_D85DUMP` | `src/game/bg.c:2510` | D85 `bgWidenRoomGdl` 8→16B dump | dead (D85 closed) |
| `GE_D85TEX` | `src/game/image.c:2479`, `src/game/tex.c:859,1050` | D85 texpool-full events (Depot analysis) | dead (D85 closed) — was useful for §3 |
| `GE_D86` | `src/game/model.c:6295`, `src/game/objecthandler_2.c:143` | D86 model rwdata trace | dead (D86 closed) |
| `GE_D87` | `src/game/ramromreplay.c:296,363,381` | D87 `ramromfilestructure` endianness | dead (D87 closed) |
| `GE_D88` | `src/game/prop.c:1367`, `src/game/stan.c:3090` | D88 `Usetup*Z` propDef stream | dead (D88 family closed; D88.4 resolved) |
| `GE_D90` | remaining: `src/game/bondview2.c:2146`, `src/game/prop.c:1391` (bondview_r.c blocks stripped M-32, `49ce620a`) | D90 bondview / prop NULL trace | dead (D90 closed) — partially removed |
| `GE_D96` | `src/game/chrprop.c:436,507` (cached `probe`) | D96 chrprop trace | dead (D96 closed) |
| `GE_D104` | `src/game/bg.c:654` (rate-limited, `d104c`) | D104 depth-clear trace | dead (D104 closed) |
| `GE_D71LOG` | `port/fast3d/gfx_pc.cpp:645` | D71 fast3d trace | dead (D71 closed) |
| `GE_D75` | `src/game/title.c` `sub_GAME_7F007F30` (capped 8) | D75 Bug 2 gun-barrel model probe: logs `chrModelInstance`/`gunModelInstance` ptr+obj+numMatrices, `render_pos`, `renderData.mtxlist`, `g_GfxMemPos`, `osVirtualToPhysical(render_pos)` | **LIVE** (D75 Bug 2 OPEN — see §F "D75 Bug 2 — RUNTIME PROBE") |

## Notes

- The bare non-`#ifdef PORT` `GE_D63` block in `bgRenderRoomPrimary`
  (`d63bgprimarycount` static + entry log, was ~`bg.c:2876/2880`) was
  removed M-32 (`49ce620a`). Every remaining `GE_D63` block is
  `#ifdef PORT` / `#if defined(PORT)` wrapped and inert.
- `[Debug]` ini keys (`Debug.FrameDump`, `Debug.InputLog`) mirror
  `GE_PCDUMP` / `GE_INPUTLOG` as a fallback (env var wins) — M-26,
  `port/src/config.c`.
- Not GE_-prefixed but same category: `GE_DETERM` is **implemented but
  experimental — not yet achieving its determinism goal** (fixed-tick
  mode, §F D117, M-52 four-pass writeup); `GE_DETERM_TRACE` (M-52 4th
  pass) adds instrumented clock-event logging when used alongside it, for
  diffing two runs to localize remaining divergence. Both env-gated,
  default off, zero cost when unset.
