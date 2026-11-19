# D141

<!-- from the old section-F/H table row -->
| D141 | **Watch-menu item-model crash (M-27), exposed by D140.** — full `## D141` entry at file tail | FIXED (`src/game/gunfire.c`, `#ifdef PORT`). Watch page now renders (weapon page verified — mirrored text D114/D116, dark weapon model D75, both parked cosmetic). `-level_09`/`-level_20` unregressed. |

## D141 — Watch-menu item-model crash (M-27), exposed by D140.

**Watch-menu item-model crash (M-27), exposed by D140.** Pressing Start → watch page renders → `set_enviro_fog_for_items_in_solo_watch_menu` (`gunfire.c:1620`) → `modelGetNodeRwData(&model, root)` with `root` garbage → AV reading `root->Opcode` (`model.c:478`). `gunfire.c:1720-1743` walks `bodymodel->Switches[]` (a `ModelNode*` array) with **raw byte offsets** `((u8*)Switches) + j + 0x48` / `+ 0x5c` and `j += 4` — assumes 4-byte N64 pointers. `0x48/4=18`, `0x5c/4=23`, `k=j>>2=0..4` → intent is `Switches[18+k]` and `Switches[23+k]`. PC's 8-byte stride makes the raw math read misaligned garbage → bogus non-NULL `ModelNode*`. porting-notes.md §B (raw hardcoded stride into a pointer array, cf. D128). Fix: `#ifdef PORT` uses `bodymodel->Switches[18 + (j>>2)]` / `[23 + (j>>2)]`.

**Status (from §F table):** FIXED (`src/game/gunfire.c`, `#ifdef PORT`). Watch page now renders (weapon page verified — mirrored text D114/D116, dark weapon model D75, both parked cosmetic). `-level_09`/`-level_20` unregressed.
