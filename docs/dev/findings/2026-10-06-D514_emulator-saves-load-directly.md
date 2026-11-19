# D514

<!-- from the old section-F/H table row -->
| D514 | **Emulator saves load directly: `ge007.eep` in 1964/Project64 format is converted on first launch** — Port-only; D492/D297 follow-up; full `## D514` entry at file tail | DONE headless (by-eye mission select owed) |

## D514: emulator saves load directly — `ge007.eep` in 1964/Project64 format is converted on first launch (ROADMAP §5a, S8)

**Change (port-only; new `port/src/eepimport.c`, one call in `port/src/libultra.c` `geEepromLoad`, README):** copying an emulator `.eep` in as `data/ge007.eep` now just works. Before the game's first EEPROM read, `geEepImportEmulatorSave()` checks each region (block 0 + the five slots) and converts in place only regions that validate in emulator format AND not in port format; a region invalid in both is left alone for the game's own wipe path; port-valid regions are untouched. If anything converts: the original bytes go to `ge007.eep.emulator.bak` first (never overwriting an existing backup — `.bak2`, `.bak3`, …), then the converted file is written, and one line is logged: `eep: imported emulator save (N regions converted, backup <path>)`. Nothing converted -> no write, no log.

**Rules mirror `tools_pc/eep_convert.py` exactly** (D492): per slot, byte-swap `options` (u16 @12; it lies INSIDE the CRC range [8..96), so a plain word swap would still fail) then re-stamp `chksum1/2` over [8..96) stored little-endian — via the game's own `fileGenerateCRC`, not a C replica; block 0 (no multi-byte field in its CRC range) is a plain byte-swap of its checksum pair. Validation reads the stored words big- or little-endian and compares against `fileGenerateCRC` of the raw bytes (the CRC value is endian-independent). No CMakeLists change (`SRC_PORT` is `GLOB_RECURSE`, as for D297's `legacycrc.c`). Export to an emulator stays with `eep_convert.py`; README "Using an emulator save" now says "copy it in".

**Verified (headless, `GE_QUITFRAME=2500` boots, `data/ge007.eep` swapped per case):**
1. `./build-pc.sh ntsc-final` rc=0, no new warnings in touched files; `geEepImportEmulatorSave` defined exactly once (nm on the exe), statics folded away.
2. `scratch/eepinv/1964_orig.eep` (md5 `c9d7da5a…`) -> after boot, `ge007.eep` md5 `3ca7cf84…` = byte-identical to the in-game-proven `1964_converted.eep`; `ge007.eep.emulator.bak` byte-identical to `1964_orig.eep`; log line "6 regions converted" present, before `eeprom: loaded`.
3. `1964_converted.eep` (a port save) -> byte-unchanged (`3ca7cf84…`), no backup created, no log line.
4. `1964_orig.eep` with one byte flipped inside slot 3's CRC region (offset 370) -> "5 regions converted"; `GE_SAVELOG=1` shows slots 0-2/4 `-> OK` and slot 3 `stored_crc=51aea8db,50d2558b calc_crc=840fcfe9,c77fd429 -> BAD -> RESET (progress wiped)` — i.e. the importer left it as-is and the game's own wipe path took over (the wiped slot re-stamps to `d4ca8847,a793c0eb`, exactly the converted original's value, so the final file again equals `1964_converted.eep`); second validation pass all five OK. Backup byte-identical to the corrupted input.
5. Second boot after case 2 -> no change (`3ca7cf84…`), no log line, no `.bak2`.
6. `python tools_pc/eep_convert.py verify --as pc` on case 2's result: all six regions OK.

**Owed (maintainer):** by-eye mission select after importing a real 1964 save (all levels shown as in the emulator).

**ROADMAP row text (not edited here — applies in the main checkout):** "Emulator saves load directly in the game (no tool for import) | verify (D514, by-eye mission select owed) | D492; `port/src/eepimport.c` called from `geEepromLoad`; D297 `legacycrc.c` same pattern | Copying a 1964/Project64 `.eep` in as `data/ge007.eep` works: on first launch each region (block 0 + 5 slots) that validates in emulator format and not port format is converted in place (same rules as `eep_convert.py`, CRCs from the game's own `fileGenerateCRC`); regions invalid in both are left to the game's wipe; port saves untouched. Original backed up to `ge007.eep.emulator.bak` (never overwritten). On-disk format stays port format. Export stays with `eep_convert.py`."
