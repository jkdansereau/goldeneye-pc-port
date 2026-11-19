# D993

## D993: Blood on hit and Co-op enemy health in the PC menu; co-op enabled on PC -- BUILT + RUN 2026-11-18 (Windows cross-build: ini keys written, co-op split screen; maintainer request)

**Change (port layer only, no game code).**
- **Blood on hit** (`nxgame.c`): the blood part of the file is built everywhere (the aim options below it stay `PLATFORM_SWITCH`); PC default 0 = the game's own puff, Switch default 1; `nxGameApply` runs at boot and on change on every platform. PC menu row on the Game page.
- **Co-op enemy health** + **co-op itself**: `coop.c` activated only under `PLATFORM_SWITCH` (`coopStageNumPlayers`), so the row would have done nothing. Both guards are gone: with `GE_COOP=1` the Surface level starts as a two-player split screen on PC. Entering it from the mode menu needs two connected pads and was not exercised.

**Not done.** The rest of the Switch gamepad work stays Switch-only: Centred-aim defaults and weapon sway, Aim zoom (handheld), Aim assist, Aim entry, Stop on aim, Look ahead, Look precision, Turn acceleration, Aim sensitivity (`nxgame.c`, `input.c`, `video.c`). The Switch branch in `input.c` replaces the original per-axis stick curve, so it changes the feel of existing PC pad users and cannot be checked without a pad. The 2-player split-screen FOV (`video.c` `splitFovY`) is also Switch-only and has no menu row.

**Verified.** Windows cross-build: `Game.BloodOnHit` is written to the ini; with `GE_COOP=1` and two connected pads the Surface level starts as a two-player split screen (log `coop: co-op stage 36, 2 players`). Linux and Switch build. The menu rows were not opened (no scripted key input).
