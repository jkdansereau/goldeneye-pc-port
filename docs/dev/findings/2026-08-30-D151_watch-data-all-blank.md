# D151

<!-- from the old section-F/H table row -->
| D151 | **Watch data all blank — objectives / mission background / M / Q / Moneypenny (M-28, user bug report).** — full `## D151` entry at file tail | FIXED (`src/bondtypes.h`, `#ifdef PORT` —… |

## D151 — Watch data all blank — objectives / mission background / M / Q / Moneypenny (M-28, user bug report).

**Watch data all blank — objectives / mission background / M / Q / Moneypenny (M-28, user bug report).** Not a crash (D150 fixed that); every line renders empty. The watch text lives in `propDefs` record types **35** (`WatchMenuObjectiveText`) and **23** (`ObjectiveStart`). In the setup stream each record's 3rd word is a **plain 32-bit language slot id** (see `assets/obseg/setup/Usetup*Z.c` `propDefs[]`: `_mkword(0,_mkshort(0,35)), menu, 11281, 0`). The decomp structs `struct watchMenuObjectiveText` / `struct objective_entry` decode that word as `u16 reserved; u16 text;` and read `text` at offset `0xA` — which only works on the N64 because the id sits in the **low 16 bits of a big-endian word**. On LE, `tools_pc/d88_propdefs.py` `_bswap32`'s the whole word (correct for a 32-bit field), moving the id to the low bytes, so `text` (u16 @0xA) reads `0x0000` → `langGet(0)` → NULL → `get_ptr_text_for_watch_breifing_page` / `get_text_for_objective` return NULL/blank. This is the "briefing/objective text renders blank" side-effect left open under D143 / D150. The per-level lang bank itself IS loaded in-level (`langLoadToAddr(langGetLangBankIndexFromStagenum(stageId))`, `prop.c:1274`).

**Status (from §F table):** FIXED (`src/bondtypes.h`, `#ifdef PORT` — `text` is a full `u32` covering the whole word in both structs; N64 `u16 reserved; u16 text;` kept under `#else`). No converter change, no sidecar regen. `nextentry`/`difficulty`/`unkD` unchanged. porting-notes.md §C. Build compiles clean; interactive re-verify pending (open the watch in any level).
