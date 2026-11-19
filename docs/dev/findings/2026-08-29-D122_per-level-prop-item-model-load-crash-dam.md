# D122

<!-- from the old section-F/H table row -->
| D122 | per-level prop/item model-load crash (Dam/Facility/Runway `modelLoad`/`modelInitRwData`): `d88_propdefs.py` had no handler for 6 ObjectRecord-derived propDef types (47/39/40/45/13/20) → generic arm half-swapped the `[s16 obj][s16 pad]` word → OOB `PitemZ_entries[]` | converter fixed; residual chr/fast3d crashes on those levels are separate |
