# D559

<!-- from the old section-F/H table row -->
| D559 | **Look ahead off for untouched existing folders (maintainer-approved, port layer only)** — full `## D559` entry at file tail | FIXED 2026-10-06 (`watchsettings.c` `migrateUntouchedLookAhead`) |

## D559: Look ahead off for untouched existing folders — FIXED 2026-10-06 (maintainer-approved, port layer only)

D557 only changes folders initialised from now on. `port/src/watchsettings.c`
`migrateUntouchedLookAhead()` handles existing ones with no further `src/game` edit:
when a stage is starting (first game-thread tick with `stageActive()`, before the stage
load's `fileLoadSettingsForFolder` re-reads the save; also applied live if the load already
ran) and `selected_folder_num` is a valid save with options EXACTLY the old factory default
`AUTOAIM|SIGHTONSCREEN|LOOKAHEAD|DISPLAYAMMO` (0x3a, control-type bits 0), `flag_007 == 0`,
no unlocked cheats and every `times[]` byte zero (no stage completed at any difficulty),
it clears `OPTION_LOOKAHEAD` and persists with `fileWriteSave` (same path as the F10
per-profile rows), logging one INFO line. Folders with any progress or any other option bit
are never touched. Once per stage activation. **Verified** with a temporary forced-state
hook (removed): untouched folder fires and logs; with progress it does not.
