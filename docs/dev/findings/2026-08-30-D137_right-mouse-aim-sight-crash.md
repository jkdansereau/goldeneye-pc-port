# D137

<!-- from the old section-F/H table row -->
| D137 | **right-mouse (aim-sight) crash:** — full `## D137` entry at file tail | FIXED (`src/game/gunfire.c` `#ifdef PORT` — `sp54` is `Gfx *`) |

## D137 — right-mouse (aim-sight) crash:

right-mouse (aim-sight) crash: `gunDrawSight` (`gunfire.c:6235`) `s32 sp54` holds a `Gfx*` the whole function (`sp54 = *gdl`; `texSelect(&sp54,…)`/`display_image_at_position(&sp54,…)` take `Gfx**`). As N64 `s32` it is 4 B, so `*(Gfx**)&sp54` reads 4 B of the adjacent stack float as the pointer high word → `_g = 0x03e4fca0_70081220` → wild DL write in `texSetRenderMode` (`othermodemicrocode.c:177`) the moment the crosshair raises. §A.

**Status (from §F table):** FIXED (`src/game/gunfire.c` `#ifdef PORT` — `sp54` is `Gfx *`)
