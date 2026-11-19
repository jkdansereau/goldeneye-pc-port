# D139

<!-- from the old section-F/H table row -->
| D139 | **Stage-unload crash root cause + fix (M-23).** — full `## D139` entry at file tail | FIXED (`src/game/cleanup_objects.c`, `#ifdef PORT` —… |

## D139 — Stage-unload crash root cause + fix (M-23).

**Stage-unload crash root cause + fix (M-23).** `cleanupObjects` (`cleanup_objects.c`) walks `g_CurrentSetup.propDefs` with `(u8)obj[0]` as the record type — correct only because the header word `[u16 extrascale][u8 state][u8 type]` is **big-endian** on N64, so `type` is the low byte. On little-endian PC the low byte is `extrascale`, so `(u8)obj[0]` is never `PROPDEF_END` → the `while` runs off the end of the blob, and the `switch` dispatches on garbage → `objFreePermanently()` on records that aren't `ObjectRecord`s → `obj->prop`/`obj->model` read adjacent float data (`0x3e567400405c30c0`) → AV in `objFreeEmbedmentOrProjectile` (`propobj.c:888`). `sizepropdef()` and every other propDef consumer already use `pdef->type` (struct member, offset 3) — only this one spot used the raw byte cast. Not a `d88_propdefs.py` layout issue at all.

**Status (from §F table):** FIXED (`src/game/cleanup_objects.c`, `#ifdef PORT` — `CLEANUP_PDTYPE(o)` = `((PropDefHeaderRecord*)o)->type`). **UNVERIFIED** — the fast teardown test (pause→abort) hit D140 first; `-level_09` unregressed. porting-notes.md §C.
