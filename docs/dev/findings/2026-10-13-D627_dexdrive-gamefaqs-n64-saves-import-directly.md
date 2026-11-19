# D627

<!-- from the old section-F/H table row -->
| D627 | **DexDrive / GameFAQs `.n64` saves import directly: the GE note (game code `3BADD1E5`) is pulled from the Controller Pak dump, zero-padded to 2048 B and handed to the D514 emulator-save importer** | FIXED 2026-10-10 (`eepimport.c` + `eep_convert.py dexdrive-to-pc`); byte-identical to the reference extraction, malformed inputs rejected untouched; by-eye mission select owed |

## D627: DexDrive / GameFAQs `.n64` Controller Pak dumps load directly — `ge007.eep` may be the dump itself; the GoldenEye note is extracted and handed to the D514 importer (ROADMAP §5a)

**Status:** verify 2026-10-10 (`port/src/eepimport.c`, `tools_pc/eep_convert.py`). Port-only; by-eye mission select with a real DexDrive save owed.

**Format (confirmed from the 36,928-byte GameFAQs sample `goldeneye-007.1080.n64`).** 0x1040-byte DexDrive header (`123-456-STD`, comment text at 0x40), then the 32 KB Controller Pak (128 x 256 B pages). Pak page 1 = inode table (128 x u16 BE, low byte = next page, 1 = end of chain); pages 3-4 = note table (16 x 32 B; game code @0, publisher @4, start page u16 BE @6). The GE note is code `3BADD1E5`, start page 5, chain 5 -> 6 -> end = 2 pages = the 512-byte cartridge EEPROM. Those 512 B zero-padded to 2048 are byte-identical to the previously hand-extracted `gamefaqs_dexdrive_2001.eep`, and all 6 regions convert under D514.

**Implementation.** `geEepImportDexDriveSave(path)` runs first inside `geEepImportEmulatorSave` (no libultra.c change): file must start with `123-456-STD` (else untouched, old path), be at least 0x1040+0x8000 bytes (tail ignored, 1 MB read cap), contain a note with code `3BADD1E5` and start page 5..127, whose inode chain is exactly 2 pages, in range, ending at 1 (loop/out-of-range chains rejected). Original is backed up to `ge007.eep.dexdrive.bak` (.bak2... never overwriting), then `ge007.eep` is rewritten as 2048 B and the ordinary D514 conversion runs (second backup `.emulator.bak` of the extracted bytes). Any failure: one `eep:` LOG_ERROR line, file untouched, no backup; the game then treats it like any wrong-size/invalid save (its first 2048 bytes are header junk, every region fails CRC, slots wiped on first write) — same as before this change. `eep_convert.py dexdrive-to-pc IN.n64 OUT.eep` does the same offline.

**Verified (no game window).** Build clean (no new warnings in eepimport.c; `abi_ratchet` OK). gcc harness around the real `eepimport.c` with a C `fileGenerateCRC`: the sample .n64 -> extraction identical to the reference, final file byte-identical to `eep_convert.py dexdrive-to-pc` output; truncated, header-only, magic-only, no-note, looping chain, out-of-range chain, bad start page: all rejected, file untouched, no backup. Plain emulator save path unchanged (identical output).

**Owed:** drop a real DexDrive dump in as `data/ge007.eep` and check mission select / unlocks by eye. Other Controller Pak dump formats (raw `.mpk`, no header) are not handled.
