# D187

<!-- from the old section-F/H table row -->
| D187 | **HUD ammo-type/weapon icon vanished at `-O2` (M-46 root-cause, M-47 fix + verify).** — full `## D187` entry at file tail | FIXED (`src/game/gunfire.c`, `#ifdef AVOID_UB`); PR #19 |

## D187 — HUD ammo-type/weapon icon vanished at `-O2` (M-46 root-cause, M-47 fix + verify).

**HUD ammo-type/weapon icon vanished at `-O2` (M-46 root-cause, M-47 fix + verify).** `set_rgba_redirect_generate_microcode()` (`src/game/gunfire.c:5929`, sole emitter of the bottom-right magazine icon; call sites 6057/6123/6217/6219, all `gdl = f(...)`) is `Gfx *` with **no `return`** — the decomp banner above it flags exactly this. Harmless under `-Og` (GCC left the tail-call result in `$v0`); commit `34885535` (Sep 2) switched release to `-O2` → undefined return reg → callers resume the DL from the icon's start offset and overwrite its `texSelect`/`display_image` commands with the following `gunDrawHudInteger` output. Ammo *numbers* use a separate correctly-returning path — hence the exact symptom split. Fix = one line, `return` under `#ifdef AVOID_UB` (already `-DAVOID_UB=1`, `CMakeLists.txt:207`); N64 byte-match path unchanged under `#else`. **Verified M-47 (interactive console):** `-O2` build, `-level_09` `GE_PCDUMP` 200–440 — orange clip glyph absent pre-fix, present post-fix; `framediff.py` frame 320 worst-cell dmean **21.8 → 4.0**, frame 440 **21.7 → 4.6** (converges to the pre-regression golden); other levels unaffected (change is one HUD function). Do **not** revert `34885535`.

**Status (from §F table):** FIXED (`src/game/gunfire.c`, `#ifdef AVOID_UB`); PR #19
