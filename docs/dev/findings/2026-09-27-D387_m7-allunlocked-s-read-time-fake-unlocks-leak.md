# D387

<!-- from the old section-F/H table row -->
| D387 | **M7 AllUnlocked's read-time fake unlocks leak into saved EEPROM (2026-09-27).** full `## D387` entry (fix section `### D387 Fix` at file tail) | FIXED 2026-09-30 (port-only per-slot raw/seen merge on EEPROM write, `libultra.c` `geD387MergeSlotWrite`; isolated ON-vs-OFF byte-identity matrix passed). Not repaired: fakes already persisted by older builds. Limitation: cheats earned while ON are not persisted. Live accept owed. |

## D387 — M7 AllUnlocked read-time patches leak into persistent saves (2026-09-27)

**Superseded by D442 (2026-09-30): the save patch and this merge machinery were removed entirely; AllUnlocked is now a pure RAM/query-time override.**
**Status: FIXED 2026-09-30 for saves made from now on (see "D387 Fix" at the end of this file); fakes persisted by earlier builds are not repaired. Original diagnosis follows unchanged.**
User offered `data/` for investigation. Original `data/ge007.ini` and
`data/ge007.eep` were **read only**, checksums unchanged after all runs.
Private isolated test folders live under
`AppData/Local/Temp/ge-m7-20260927/`: current ini + ROM/sidecars,
with the older `data/ge007 copy.eep` as the reproducible EEPROM seed.
The original seed has two genuinely completed times in folder 0, no
0x3FF fake completions, and only its earned cheat bits (`04 00 00`).
No edits to game logic, port code, or user saves in this investigation.

**Mechanism:** `port/src/libultra.c:geEepromRW` patches a *copy* of all five
save slots on the block-4 read when `portAllUnlocked` is on. The raw EEPROM
on disk stays untouched until a later save. `fileValidateSaves` accepts the
patched copies (CRC recomputed by `geEepromPatchAllCheats`) into the live
`src/game/file2.c:saves[]` array. An unrelated call to `fileWriteSave`
serializes **all 96 bytes of the live slot** back to EEPROM, including its
synthetic unlocked-cheat bits and 0x3FF time fields. D257's original finding
explicitly warned these unlocks can persist after a save; the M7 plan's
"read-time patch should guarantee untoggle preservation" premise is false.
The two genuine times in the test slot were retained, but fake completions
and cheats now survive an OFF restart. `main.c` also seeds the RAM debug
unlock flags only when the process starts: switching the setting OFF during
the same process does not clear those flags. A valid OFF test needs restart.

**Isolated evidence:** with `GE_SAVELOG=1 GE_QUITFRAME=105`, both ON and OFF
booted from the same older seed; no-gameplay-write EEPROM hashes were equal
(legacy CRC migration occurred in both). Starting from the migrated raw
EEPROM, `Game.AllUnlocked=1 GE_WSPROBE_FRONT=600 GE_QUITFRAME=780` called the
real front-end `watchSettingsSet(Music, 4096, commit=1)` on folder 0.
`SAVELOG` recorded `fileWriteSave slot=4 block=52`; only that EEPROM slot
changed. Cheat bits went `04 00 00` → `ff ff 0f`; 58 empty completion
times became 0x3FF, and the 2 earned times remained. Starting from that
result, `AllUnlocked=0` followed by a clean restart/quit validated the
slot's stored CRC and left the 58 fakes intact. Logs and private snapshots
in `on-save/` and `after-off/`. This reproduces on a **settings save alone**,
without a mission completion. Current user `data/ge007.eep` already has two
nonfree slots with 60/60 0x3FF times and all cheat bits set, consistent with
past persistence; this test neither created nor changed those bytes. Do not
silently overwrite it with an older backup (that would lose later progress).

**Scope before fixing:** decide whether OFF must restore *earned* progress
while stripping only artificial unlocks (recommended). A port-only write
adapter needs to retain unpatched per-slot state and merge legitimate
changes while ON, including new real completion times and cheat awards;
simply masking every 0x3FF on writes cannot distinguish a genuinely earned
capped time or a previously persisted fake. There is no safe automatic
repair for already-persisted fake bits without prior raw-state provenance.
Test fresh/real/mixed saves in isolated directories, ON and OFF restarts,
settings writes and actual mission completion, CRC validity and all folders;
only then mark M7 done. The 21-level visual gate is independent and open.
