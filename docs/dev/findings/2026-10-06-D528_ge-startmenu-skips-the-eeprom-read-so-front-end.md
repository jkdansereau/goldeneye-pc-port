# D528

<!-- from the old section-F/H table row -->
| D528 | **`GE_STARTMENU` skips the EEPROM read, so front-end-only sessions never import the emulator save** — harness trap; D514 follow-up; full `## D528` entry at file tail | OPEN (harness documentation; the D514 imported-save test must run from a normal boot; a log-or-import fix is owed in the port layer) |

## D528: `GE_STARTMENU` skips the EEPROM read, so front-end-only sessions never import the emulator save (harness trap)

**OPEN (harness documentation).** A `GE_STARTMENU` session (front end up with no level load) never reaches the EEPROM read — no `eeprom:` log line; the front end reports `watchsettings: no valid Bond save in any folder` even with `data/ge007.eep` present. Consequence: the D514 imported-save test **cannot** be driven from a `GE_STARTMENU` boot; it must run from a **normal boot**. There it reproduces exactly: `eep: imported emulator save (6 regions converted, backup data\\ge007.eep.emulator.bak)` and `data/ge007.eep` md5 `3ca7cf84…` = the in-game-proven converted save (2026-10-05 session).

Decision owed: document it (a NOTE log line when `GE_STARTMENU` is active: "EEPROM import skipped") or make the front end perform the import — port-layer only; the N64 front end has no EEPROM at all, so this is pure port scaffolding.

**Never do:** drive the D514 imported-save test from a `GE_STARTMENU` boot — it must run from a normal boot, or the EEPROM import is skipped and the test cannot see the converted save.

---
