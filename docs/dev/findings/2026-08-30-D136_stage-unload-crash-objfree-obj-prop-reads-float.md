# D136

<!-- from the old section-F/H table row -->
| D136 | **Stage-unload crash — `objFree` `obj->prop` reads float garbage (M-22, WS6 Facility playthrough).** — full `## D136` entry at file tail | ROOT-CAUSED → **D139** (it was NOT a layout bug — `cleanupObjects` read the type from the wrong byte on LE and walked off the propDef blob). |

## D136 — Stage-unload crash — `objFree` `obj->prop` reads float garbage (M-22, WS6 Facility playthrough).

**Stage-unload crash — `objFree` `obj->prop` reads float garbage (M-22, WS6 Facility playthrough).** After D135 cleared the firefight crashes the level is completable; then on teardown `lvlUnloadStageTextData` → `cleanupObjects` (`cleanup_objects.c:42`) → `objFreePermanently` → `objFree` (`propobj.c:994`) → `objFreeEmbedmentOrProjectile(obj->prop)` faults at `:888` (`prop->obj` deref). Real gdb `bt`: `obj` = `0x701bd730` (valid) but `obj->prop` = `0x3e567400405c30c0` — two packed `f32` (~0.21, ~3.44), i.e. a `coord3d` read as a pointer → `ObjectRecord.prop` (N64 `/*0x10*/`, after `inherits PropDefHeaderRecord` + `s16 obj; s16 pad; u32 flags; u32 flags2;`) lands at the wrong PC offset for this object's propDef type, OR the record is undersized and setup's `->prop` store never happened / was clobbered. Same family as D122/D132 (`tools_pc/d88_propdefs.py` per-type record sizing vs the compiled `ObjectRecord`/subtype layout). Type not yet identified — needs a gdb run with a breakpoint on `objFree` to read `obj->type` (`cleanupObjects` frees WEAPON/AMMO/MAGAZINE/COLLECTABLE/MONITOR/RACK/AUTOGUN/HAT/ARMOUR/GAS_RELEASING/VEHICHLE/AIRCRAFT/GLASS/SAFE/TANK/TINTED_GLASS via this path). Mission completes; crash is before the next briefing loads.

**Status (from §F table):** ROOT-CAUSED → **D139** (it was NOT a layout bug — `cleanupObjects` read the type from the wrong byte on LE and walked off the propDef blob).
