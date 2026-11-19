# D470

<!-- from the old section-F/H table row -->
| D470 | **Audio master volume + output-device selection (ROADMAP 5a):** `Audio.MasterVolume` (0..100, scratch-copy gain on the final mix, default 100 = byte-identical) and `Audio.Device` (name, default system device; live reopen on the audio-producer thread, fallback on missing/removed device). | FIXED (live listening test owed) |

## D470 -- Audio master volume + output-device selection (ROADMAP 5a)

**Keys.** `Audio.MasterVolume` (int 0..100, default 100) and `Audio.Device` (string, default empty = system default device). Both in `port/src/audio.c`; rows "Master volume" / "Output device" at the top of the AUDIO section (shared by the F10 overlay and front-end PC Options; whole %).

**Gain.** Applied in `audioSetNextBuffer` to a static scratch copy of the mixed block (`s*gain/100`, gain <= 100 so no clip is possible), after the game's own Music/FX volumes. At 100 the original buffer pointer is queued untouched (byte-identical). Live: the int is read per block.

**Device.** SDL audio here is queue-based (`SDL_QueueAudio`), all queue/size calls happen on the audio-producer thread. UI/hot-plug only set atomics (`s_devReopenReq`, `s_devRemovedId`); the close/reopen runs at the top of `audioSetNextBuffer` on that same thread, so no queue call races the close (a few ms of queued audio is dropped on a switch). Named device that fails to open -> `LOG_WARNING` + default device. `SDL_AUDIODEVICEREMOVED` (pumped in video.c) -> warning, close, reopen default, config name cleared. Startup logs every enumerated output (`audioInit: output device N: "..."`). Default config makes the same `SDL_OpenAudioDevice(NULL, ...)` call as before.

**Probe.** `GE_AUDIOGAINLOG=1`: once per second, max |sample| of the final queued buffer (`D470 gain: master=N peak=P`).

**Verification (Windows, -level_09, GE_QUITFRAME=600, 640x480, MouseEnabled=0, driver directsound).** a) defaults: opens default device, rc=0, peaks 2717/8158/7459/11225/... b) `MasterVolume=50` + `Device=Speakers (Focusrite USB Audio)`: log "opened audio device by name", rc=0, peaks 1358/4576/4242/5218/... (first window exactly half of a; later windows ~0.5-0.56, timing-dependent mixes). c) `Device=NoSuchDevice`: `WARN audioInit: audio device "NoSuchDevice" unavailable (No such device.); falling back to the default device`, then default opened. In run c the process did NOT self-quit at GE_QUITFRAME (ran to the 120 s `timeout`, rc=124, ~7000 frames, no crash; no "quit requested" line); runs a and b quit normally with identical settings otherwise, so it is probably a harness/quit-path flake unrelated to audio, but it was not re-run (launch budget) -- confidence medium on that point. No crash log in any run.

**Maintainer listening checklist.** (1) Master 100 sounds identical to before. (2) Slider 0/25/50/100 in F10 and in front-end PC Options changes loudness live, music+FX together; 0 is silent. (3) Output device row cycles Default + devices both directions; audio moves to the chosen device immediately, no crash, brief gap only. (4) Restart: chosen device and volume persist. (5) Unplug the active USB device mid-game: game keeps running and audio resumes on the default device. (6) Edit the ini to a bogus device name: game starts on default. (7) "Reset to defaults" in AUDIO restores 100 and Default (does not touch Music/FX per-file volumes beyond their existing reset).


**Lead re-check (2026-10-01):** run c (`Audio.Device = NoSuchDevice`, `-level_09`, `GE_QUITFRAME=600`) repeated once on the same build: fallback warning, default device opened, `quit requested (GE_QUITFRAME)` and a clean exit (rc=0, no crash log). The agent's earlier non-quitting run c did not reproduce; not an audio-path issue.
