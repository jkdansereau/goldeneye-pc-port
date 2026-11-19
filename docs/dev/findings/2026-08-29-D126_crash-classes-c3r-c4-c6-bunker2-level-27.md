# D126

<!-- from the old section-F/H table row -->
| D126 | **crash classes C3r/C4/C6 (Bunker2 `-level_27` `door7F054FB4` propobj.c:13523, Depot `-level_30` prop.c:902, Surface2 `-level_43` loadobjectmodel.c:393):** — full `## D126` entry at file tail | resolved — `d88_propdefs.py` PROPDEF_PC_BYTES[30/32/33/35]=24 + typed handler; `loadobjectmodel.c` sizepropdef PORT returns 6. Bunker2/Depot/Surface2 now PASS (13→16/21) |

## D126 — crash classes C3r/C4/C6 (Bunker2 `-level_27` `door7F054FB4` propobj.c:13523, Depot `-level_30` prop.c:902, Surface2 `-level_43` loadobjectmodel.c:393):

crash classes C3r/C4/C6 (Bunker2 `-level_27` `door7F054FB4` propobj.c:13523, Depot `-level_30` prop.c:902, Surface2 `-level_43` loadobjectmodel.c:393): the objective sub-records `criteria_picture` (30), `criteria_roomentered` (32), `criteria_deposit` (33), `setup_objective_text` (35) each end in a `T *next` list pointer that the setup walk (`set_parent_cur_obj_*` / `setup_briefing_text_entry_parent`) writes unconditionally. On PC that pointer widens 4→8B and lands 8-aligned at offset 16 → struct is 24B/6w (N64 16/20). `d88_propdefs.py` emitted them at N64 size via the generic arm → the runtime 8-byte `->next` write clobbered the *next* record's header → propdef walk desynced, command indices drifted ~100, `linkedDoorOffset + arg2` resolved to the wrong record → door `linkedDoor` chain walked into garbage.

**Status (from §F table):** resolved — `d88_propdefs.py` PROPDEF_PC_BYTES[30/32/33/35]=24 + typed handler; `loadobjectmodel.c` sizepropdef PORT returns 6. Bunker2/Depot/Surface2 now PASS (13→16/21)
