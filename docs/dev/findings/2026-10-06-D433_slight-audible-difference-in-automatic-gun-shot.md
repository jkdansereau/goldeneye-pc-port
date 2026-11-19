# D433

<!-- from the old section-F/H table row -->
| D433 | **Slight audible difference in automatic-gun shot sound vs GEPD/1964 after cadence was matched (user by-ear, Caverns).** — full `## D433` entry at file tail | OPEN (not a timing issue; audio layer suspected). |

## D433 — Automatic-gun shot sound still audibly differs slightly from GEPD/1964 (user by-ear, Caverns, 2026-09-29)

**Measured (headless, `-level_09`, held fire, temporary shot + `GE_D207S` probes, since removed):** after D427/D432 the shot and sound-start cadence match the console's rules exactly -- AK47 shots 6 ticks / sounds 6 ticks, offset 0 on every shot; Klobb shots 6 / sounds 12 (SoundTriggerRate 11, same gate as N64); weapon 9 shots 4 / sounds 6; zero SKIPs. So the residual difference is NOT fire/sound cadence.
**Suspects (unverified):** (1) the port's RSP-audio HLE (`port/src/mixer.c`): resampler interpolation, envelope/`aEnvMixer` handling, reverb; (2) how a superseded shot sound is stopped (`sndDeactivate` -> release envelope) versus the emulator's plugin; (3) the reference itself -- 1964's audio plugin is also HLE and is not a ground-truth for timbre. **Next:** ask for a description (pitch / tail cut / loudness / reverb / overlap, which gun) or a capture of both; then compare mixer resampling and release handling to libaudio's N64 behaviour.
**Status:** OPEN.

### D433 -- code audit 2026-10-01 (branch `audit/d433-fire-sound`, headless, `GE_D433LOG` TEMP probe in `gunfire.c`)
**Result: the game-side shot-sound path is faithful; the residual is in the audio layer (by-ear/capture item), not in `src/game` logic.**
- AK47 held fire, `-level_09`, 60 fps: SHOT and FIRESND land on the same tick every 6 ticks (`gt` 556, 562, 568, ...; field_890 6/12/18, field_88C 5/11/17), mag 30 -> 0 without skips. Matches the D427/D451 rules.
- Sound plumbing (`gunTickHandState` FIRE block): each sound-trigger first `sndDeactivate`s the previous shot sound (`audioHandle` / `field_A48` pair), then plays sound 109 into whichever handle is NULL. `GE_AUDIOTRACE` shows the expected alternation: `sndDeactivate(4db0)` -> play 109 into slot 7009be30 -> `sndDeactivate(4e80)` -> play into 7009be28, every slot `(was 0)` when written, i.e. the handle-clear contract of the N64 libaudio holds and the D208 `field_A48` layout fix is in effect. Order is FIFO at delay 0. Besides the shot (109) each tick pair also starts one bullet-impact sound (ids 29-40, hit-type table), same as N64.
- Nothing tick-vs-frame-sensitive remains on this path except the already-fixed D432 `|1` offset and the D427/D451 gate; `sndDeactivate` posts an event at delay 0, so how soon the release envelope starts is decided by the audio thread (port mixer), not by game code.
- **Remaining suspects (not settled by code):** (1) mixer release-envelope length / when a deactivate event is consumed relative to the next play event within one audio frame (a superseded 6-tick shot is cut by the release, the N64's cut-off shape is a function of its audio-frame size); (2) reverb/resampler HLE. Only a capture comparison (PCM of both) or a by-ear A/B with a wav dump settles it; this cannot be established from the log.
**Status:** OPEN (audio layer, by-ear). Confidence that game logic is faithful: high; that the delta is mixer-side: medium.
