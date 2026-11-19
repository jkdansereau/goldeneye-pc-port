# D123

<!-- from the old section-F/H table row -->
| D123 | **crash class C1: `chrIsNotDeadOrShot` NULL deref on 6 levels (Dam/Runway/Frigate/Statue/Streets/Cradle).** — full `## D123` entry at file tail | converter fixed (`OBJ_ID_WORDS`); C1 cleared on all 6, residual crashes are fast3d (C2) |

## D123 — crash class C1: `chrIsNotDeadOrShot` NULL deref on 6 levels (Dam/Runway/Frigate/Statue/Streets/Cradle).

crash class C1: `chrIsNotDeadOrShot` NULL deref on 6 levels (Dam/Runway/Frigate/Statue/Streets/Cradle). D122's `OBJ_TAIL_DESC` zeroed the widened `VehichleRecord/AircraftRecord.ailist` slot (w32), but `prop.c:1764/1786` reads a pre-populated int AI-list id there → `ailistFindById(0)` → `GAILIST_AIM_AT_BOND` → `ai()` runs a CHR aim list with `ChrEntityp==NULL`

**Status (from §F table):** converter fixed (`OBJ_ID_WORDS`); C1 cleared on all 6, residual crashes are fast3d (C2)
