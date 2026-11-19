# D517

<!-- from the old section-F/H table row -->
| D517 | **Holding left/right on a Bond settings row dropped the fps: every hold-repeat step committed (D489 staged sliders only)** — Port-only; D489 scope gap, found in the D516 live pass; full `## D517` entry at file tail | FIXED (live pass 2026-10-04) |

## D517: Holding left/right on a Bond settings row dropped the fps -- every hold-repeat step committed (D489 staged sliders only)

**Problem (maintainer live pass 2026-10-04, during the D516 test).** In a mission, adjusting "Controller style" (or any other Bond row: the Look/AutoAim/AimControl/Sight/LookAhead/Ammo toggles) with left/right on the pad dropped the frame rate for as long as the direction was held.

**Root cause.** D489 made *sliders* stage their steps and commit once on release (`s_adjWatchField` + `optionsAdjustCommitPending()`), because "each commit is three joyDisablePoll/joyEnablePoll handshakes plus an EEPROM file rewrite on the game thread". The other value kinds -- ROW_TOGGLE, ROW_ENUM (incl. the new `Bond.Control`), ROW_MSAA, ROW_FPSCAP -- still called `rowSet()` (commit = 1) on EVERY step. Each commit queues through to the game thread's `watchSettingsPersistField()` -> `fileWriteSave()`. Hold-repeat fires every 4 frames, so a held direction kept the game thread persisting: reproduced headlessly with a pad physically held -- `GE_PERFSTAT` showed 60 fps baseline vs 40-50 fps while cycling, and the log shows the style walking 1->0->7->...->3 with `watchsettings: persisted field 8 in Bond file 1` 57 times over a 900-frame run. (The overlay's own render cost is not the cause: `dl=` stays ~0.3 ms with the heaviest page open.)

**Fix (port-only, `optionsoverlay.c`).** New `rowSetStepWatch()`: a stepped row that has a watch field now stages exactly like a slider (commit = 0; switching the pending field commits the previous one first), and the existing `optionsAdjustCommitPending()` -- which already runs every frame while left/right is not held in BOTH UIs (F10 `overlayHandleInputLocked`, frontoptions release path) and on F10 close -- saves once on release. Non-watch rows fall through to a plain commit, so ini rows are byte-for-byte unchanged. The dropdown popup confirm (`ddConfirm`) still commits immediately: one deliberate pick, not a hold.

**Verified.** Build rc=0, no warnings in touched files. Headless: `GE_OPTIONTREEPROBE` PASS + `GE_PRESETPROBE` PASS (front), `GE_WSPROBE_RESET` stage 6/6 sections + saved bytes OK; `GE_PERFSTAT` baseline still 60 fps with the overlay closed. **Live pass (maintainer, 2026-10-04):** confirmed fixed -- "those things are fixed".
