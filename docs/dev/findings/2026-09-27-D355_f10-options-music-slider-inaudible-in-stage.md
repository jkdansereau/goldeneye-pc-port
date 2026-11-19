# D355

<!-- from the old section-F/H table row -->
| D355 | **F10/options Music slider inaudible in-stage: the slider drives only the X-track (track 2) via `set_mTrack2Vol`, but in-stage BGM is track 1, which the game pins to `VOLUME_MAX` at level start (lv.c:364) and never rescales from `mTrack2Vol` in solo -- so neither the N64 watch nor the F10 could change the music the player was hearing; maintainer-reported (2026-09-27).** — full `## D355` entry at file tail | FIXED (port-only, `port/src/watchsettings.c`): the Music commit now also calls `musicTrack1ApplySeqpVol(v)` (the exact resync the MP path already does, mpmenu.c:354) and a once-per-activation resync runs on stage entry; by-ear confirmed by the maintainer. N64 watch path untouched. |

## D355 — F10/options Music slider inaudible on in-stage BGM (2026-09-27)

Maintainer report (after D354): F10 sound-FX changes apply in real time, but the Music slider does not; opening the in-game watch menu seemed to be what "applied" it. Diagnosis (port-only):

- **Root cause: the slider and the audible music are on different tracks.** The Music row drives `mTrack2Vol` → `set_mTrack2Vol` → the **X-track (track 2)** CSP player (cutscene/stinger music). In-stage BGM, however, is **track 1**: solo level start plays `lvlPlayMusicTrack1(getmusictrack_or_randomtrack(...))` (lv.c:304) and the level-start reset pins **all three** players to `VOLUME_MAX` (lv.c:364–366) — it never rescales track 1 from `mTrack2Vol` in solo. The MP path is the exception: `mpmenu.c:354` explicitly feeds `get_mTrack2Vol()` into `musicTrack1ApplySeqpVol` at level entry, and `mp_music.c` mission-state transitions do the same before each `musicTrack1Play`.
- **Why the watch "fixed" it:** the in-game watch re-applies `set_mTrack2Vol` every frame while its volume screen is drawn (`draw_music_volume_slider`), so the watch-screen/stinger music (X track) audibly tracked the slider — the in-stage BGM never did. A one-shot F10 commit has the same effect as the N64 watch: correct value stored, inaudible on BGM.
- **Fix (`port/src/watchsettings.c`, port-side only):**
  - `applyValue(WATCH_SETTING_MUSIC)` now calls `musicTrack1ApplySeqpVol(value)` after `set_mTrack2Vol(value)` — the *same* resync the game's own MP path performs (mpmenu.c:354), so F10/options Music edits are audible on the BGM player in real time (mirroring how FX always was via the global SFX scaler).
  - **Once-per-activation resync** in `watchSettingsGameTick`: level start pins track 1 to `VOLUME_MAX` before `init_watch_at_start_of_stage` (lv.c:474, level load) re-applies the saved music value to the X track; the hook resyncs track 1 to `get_mTrack2Vol()` on the first active tick of each activation (`wsStageWasActive` reset whenever the stage is inactive), so the player's setting survives level starts, not just live edits. The `!=` guard keeps it a no-op when the game already set an equal value.
  - Track 3 (MP BGM) is intentionally untouched — the game drives it from its own volume, and the PC port is solo.
  - The N64 watch-menu path is **unchanged** (ground truth: the watch scales the X track only); this is a PC-settings-surface extension, in the same category as D350's existing `set_mTrack2Vol` call from the port.
- GE_WSPROBE logs extended with the track-1 volume (`t1=`) so the resync is visible headless.

Verification: NTSC build clean; **by-ear confirmed by the maintainer** — F10 Music now changes in-stage music in real time. The front-end commit path (input thread) now also calls `musicTrack1ApplySeqpVol`, which wraps `alCSPSetVol` on the track-1 player — same thread pattern as the pre-existing track-2 call, so the D350 threading gate is unchanged in effect.
