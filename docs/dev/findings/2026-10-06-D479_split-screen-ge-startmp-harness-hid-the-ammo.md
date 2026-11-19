# D479

<!-- from the old section-F/H table row -->
| D479 | **Split-screen `GE_STARTMP` harness hid the ammo counter: it skipped the file-select screen's `selected_folder_num = -1`, so the stage load applied a blank folder-1 record (options 0: ammo, sight, auto-aim off); harness now sets -1 like a real MP launch** — full `## D479` entry at file tail | FIXED 2026-10-02 (local); harness-only, 2P frame shows the counter in both halves |

## D479 — `GE_STARTMP` harness: ammo counter missing (skipped file-select side effect)

**Symptom:** in headless split-screen runs launched with `GE_STARTMP` the ammo counter never showed (seen 2026-10-01 with both players firing AK47s); the maintainer sees it in every menu-launched match.
**Probe** (`GE_AMMOPROBE`, TEMP, branch `probe/ammo-startmp`, never merge): in the harness match both players had `gunammooff = 0x1` (`GUNAMMOREASON_OPTION`) and `cur_player_get_ammo_onscreen_setting() == 0`. The stage load's `fileLoadSettingsForFolder(selected_folder_num)` (`options.c` -> `file.c` -> `file2.c`) ran with folder 0 and found a record with `options = 0x0000`; the solo `-level_26` shortcut, by contrast, found the real file 1 (`options = 0x013a`, ammo bit set) in a different `saves[]` slot.
**Cause:** porting-notes D10 class. A real MP launch passes the file-select screen, whose setup sets `selected_folder_num = -1` (`front.c`), so `fileGetSaveForFoldernum(-1)` returns NULL and no Bond file's options are applied in MP (the defaults stay: ammo on). The harness skipped that screen and kept the boot default `FOLDER1`. The record it then matched is very likely an all-zero `saves[]` entry (the MP harness also skips the legal screen's `fileValidateSaves()`, the only EEPROM -> `saves[]` load, see D299 and porting-notes D10); not separately verified.
**Fix (`src/game/lv.c`, inside the `#ifdef PORT` `GE_STARTMP` harness block only):** set `selected_folder_num = -1` before `init_mp_options_for_scenario`. No game-logic or menu-path change.
**Verified:** `GE_STARTMP=2 GE_MPVIRT=1 GE_STARTWEAPON=8`, pinned 1280x720 ini, frame 600: both halves draw "30 | 370" with the ammo icon; rc=0; ini and eep unchanged.
**Note:** other harness-only differences may remain because `saves[]` is unvalidated in `GE_STARTMP` runs (anything that reads save data during an MP match, e.g. unlock checks). Not observed so far.
