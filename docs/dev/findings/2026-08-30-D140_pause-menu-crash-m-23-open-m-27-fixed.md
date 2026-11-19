# D140

<!-- from the old section-F/H table row -->
| D140 | **Pause-menu crash (M-23 open → M-27 fixed).** — full `## D140` entry at file tail | FIXED (`src/game/bondview.h`, `src/game/bondview2.c`; `#ifdef PORT`). Exposed the next watch-render crash → **D141**. `-level_09`/`-level_20` unregressed (3/3). porting-notes.md §A. |

## D140 — Pause-menu crash (M-23 open → M-27 fixed).

**Pause-menu crash (M-23 open → M-27 fixed).** `bondviewRenderWatch` (`bondview2.c:8604`) → `bondviewTransformManyPosToViewMatrix(g_CurrentPlayer->field_23C, objheader->numMatrices)` with `field_23C == NULL` → `matrix_4x4_copy(src=0x0)` AV. Root cause (NOT "cache never allocated"): `something_with_watch_object_instance` (N64 player +0x230) is a **`struct Model` (0xBC) + its RW-data pool (0xC8) punned into a 0x184-byte hole** (`field_234..field_3B0`); three of those "fields" are Model members the game reads by name — `watch_scale_destination`==`.scale`, `pause_watch_related_adjust`==`.animframe1`, **`field_23C`==`.render_pos`**. `render_pos` IS populated (by `instcalcmatrices` via `subcalcmatrices` at `:8524`), but on x86-64 `sizeof(struct Model)` grows (4→8B ptrs) so `field_23C` no longer overlaps `.render_pos` → reads Model padding (~0) → NULL. Same class as D100 (gait Model) / D102 (weapon Model) / D56 (this very watch Model's pool). Fix: `#ifdef PORT` makes `something_with_watch_object_instance` a real inline `struct Model` + `u32 watchRwPool[192]` in `struct player` (`bondview.h`); `bondview2.c` redirects the 3 named reads to the member via `GE_WATCH_{ANIMFRAME,SCALE,RENDERPOS}` macros (N64 `#else` = the verbatim field). The D56 branch of `sub_GAME_7F07E7CC` drops its `static u8 watchRwPool[0xC8]` for the inline pool.

**Status (from §F table):** FIXED (`src/game/bondview.h`, `src/game/bondview2.c`; `#ifdef PORT`). Exposed the next watch-render crash → **D141**. `-level_09`/`-level_20` unregressed (3/3). porting-notes.md §A.
