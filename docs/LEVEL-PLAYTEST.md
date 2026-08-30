# LEVEL-PLAYTEST — WS6 human completion-validation checklist

Run **after** `docs/LEVEL-STATUS.md` is all-21 load+render PASS. This is
the part that needs real input — a human plays each level start to finish
and confirms it matches retail. Findings feed a WS5-style triage round
(log each as a crash-class row or a new Dxx candidate).

## How to run one level

```sh
export PATH="/c/msys64/mingw64/bin:$PATH" && ./build-pc.sh ntsc-final
./build-pc/ge007.x86_64.exe -level_XX          # bare — pools auto-injected (D121)
```

Controls: `port/src/input.c` — keyboard+mouse or SDL gamepad. Mouse-look is
mode-aware since M-24 (hold RMB = analog aim). Tune feel in
`data/ge007.ini` `[Input]` (`MouseAimSpeed` 50, `MouseTurnSpeed` 100,
`MouseInvertY` 0) with the game closed. Known residual (do not re-log):
D118a — hipfire pitch is digital vs analog yaw. **F12** = screenshot to
`ppm/shot_NNN.ppm`. **Mouse wheel** = cycle weapon. **Alt-Enter** =
fullscreen. Alt-tab away frees the cursor (re-grabs on focus). Title bar
shows FPS.
Difficulty for a bare `-level_XX` boot defaults to Agent; use the menu path
(mission select → difficulty) to test 00/007.

Objective status: `src/game/objective_status.c`
(`get_status_of_objective()` `:161`); objective propDefs are
`PROPDEF_OBJECTIVE_*` (`bondconstants.h:4332`).

## Per-level checklist (copy this block per level)

```
### <Level> (-level_XX)   difficulty tested: [ ] Agent  [ ] Secret Agent  [ ] 00 Agent

- [ ] Spawn point + start orientation match retail
- [ ] Geometry / textures / lighting recognisable, no missing rooms
- [ ] Guards spawn, path, react, take/deal damage, die correctly
- [ ] Doors / lifts / switches / destructibles work
- [ ] Pickups (ammo, armour, key items, gadgets) present and collectable
- [ ] Each difficulty-gated objective reachable AND registers COMPLETE
      (objective_status)
- [ ] Alarms / reinforcements / scripted events fire
- [ ] Level exit trigger fires → MISSION COMPLETE (not a hang / wrong screen)
- [ ] Auto-advance to next briefing works
- [ ] Frame rate + timing acceptable
- Notes / defects:
```

## 21 solo levels (mission order — numbers = `-level_XX`)

| # | Level | -level | Briefing asset |
|---|---|---|---|
| 1 | Dam | 33 | UbriefdamZ |
| 2 | Facility | 34 | UbriefarkZ |
| 3 | Runway | 35 | UbriefrunZ |
| 4 | Surface 1 | 36 | UbriefsevxZ |
| 5 | Bunker 1 | 09 | UbriefsevbunkerZ |
| 6 | Silo | 20 | UbriefsiloZ |
| 7 | Frigate | 26 | UbriefdestZ |
| 8 | Surface 2 | 43 | UbriefsevxbZ |
| 9 | Bunker 2 | 27 | UbriefsevbZ |
| 10 | Statue | 22 | UbriefstatueZ |
| 11 | Archives | 24 | UbriefarchZ |
| 12 | Streets | 29 | UbriefpeteZ |
| 13 | Depot | 30 | UbriefdepoZ |
| 14 | Train | 25 | UbrieftraZ |
| 15 | Jungle | 37 | UbriefjunZ |
| 16 | Control | 23 | UbriefcontrolZ |
| 17 | Caverns | 39 | UbriefcaveZ |
| 18 | Cradle | 41 | UbriefcradZ |
| 19 | Aztec | 28 | UbriefaztZ |
| 20 | Egypt | 32 | UbriefcrypZ |
| 21 | Cuba | 54 | (post-Egypt unlock) |

(Mission table: `front.c:433`. Cuba = `LEVELID_CUBA`, not in the standard
folder list — unlocked after Egypt.)
