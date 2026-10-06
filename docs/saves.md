---
title: Saves
description: How the GoldenEye 007 PC port backs the N64's EEPROM save with a plain file you can back up.
date: '2026-10-05'
modified_time: '2026-10-05'
---

## Saves

GoldenEye's N64 save is **EEPROM-based** — the controller's EEPROM
(`osEeprom*`), not a Memory Pak. The port backs it with a **plain file**,
`data/ge007.eep` next to the executable, so your progress is a file you can
copy to back it up (or move to another machine).

A few details:

- The **PFS / Memory-Pak and motor (Rumble Pak) code** is only for accessory
  detection and is stubbed to report "no accessory," so the game proceeds
  without a Memory Pak.
- **Emulator saves can be imported** — copy a `GOLDENEYE-usa.eep` in as
  `data/ge007.eep` (see the README's *Using an emulator save*).
- The save's **checksum is part of the file format**. The PRNG/CRC the save
  depends on is kept byte-stable, because changing it would silently
  invalidate existing saves — a porting-notes finding (D9).
- `-fresh` (a launch option) wipes the save and settings for a clean-slate run.

*More: [internals](internals.md) §7; the checksum finding is
[porting notes](porting-notes.md) D9.*
