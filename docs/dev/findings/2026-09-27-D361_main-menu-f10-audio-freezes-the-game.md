# D361

<!-- from the old section-F/H table row -->
| D361 | **Main-menu F10 Audio freezes the game: EEPROM save from joyPoll deadlocks waiting for its own poll ack (2026-09-27).** — full `## D361` entry at file tail | FIXED (port-only, `port/src/watchsettings.c` + F10 probe in `optionsoverlay.c`): queue front F10 save edits to game thread; actual poll-thread Music+FX commits tested with existing and fresh EEPROM, orderly quit, user's ini/save restored. |

## D361 — F10 Audio on the main menu locks up: self-deadlock in EEPROM poll gate (2026-09-27)

**User repro:** fresh v0.4.0 playtest, change an AUDIO slider from F10
on the main menu → game locks (not merely an inaudible volume change).

**Exact chain:** F10 input is processed inside `inputComputePad()` called
by `osContStartReadData()` in `joyPoll()` (`src/joy.c:500`). D354 routed
front-context watch edits through `watchSettingsSet()` → `saveFrontField()`
on that same poll thread. Both `fileGamePakProbe()` and `fileWriteSave()` call
`joyGamePakProbe/LongWrite()` → `joyDisablePoll()` → blocking receive on
`g_ContDisablePollReceiveMessageQueue`. Only `joyPoll()` can send the
acknowledgement (`src/joy.c:434`), but it is itself blocked inside the
F10 edit: deterministic self-deadlock. D354's claim that one settings
surface meant this thread's writes were safe was incorrect; serialization
between editors does not make the controller-poll handshake re-entrant.

**Fix (port-only):** front F10 writes now coalesce in a small locked
folder/field queue. `watchSettingsGameTick()` drains commits on the game
thread before its non-stage early return and runs `saveFrontField()` there,
with no lock held across EEPROM I/O. The on-screen value is staged before
enqueue for immediate feedback, then cleared only after the matching commit.
The separate stage queue and the direct front options screen path retain
their prior behavior. `GE_WSPROBE_F10FRONT=1` is an opt-in regression probe
called from F10's actual controller-poll input path at file select (once,
after a valid save is available), committing Music+FX to 4096.

**Verified:** NTSC incremental build/link succeeded. Native Windows test
with `GE_OPTIONSOVERLAY=1`, `Game.SkipIntro=1` and the poll-thread probe
logged the queue plus both Music/FX field persist events and exited via
`GE_QUITFRAME=700` (with existing EEPROM **and** absent EEPROM on boot).
The pre-existing game-thread `GE_WSPROBE_FRONT=120` front-path regression
also persisted Music 255→32 and quit normally. The user's ini and EEPROM
were copied aside and restored byte-identically after each run. Manual
mouse slider/section-reset and stage transition checks remain owed.
