# D399

<!-- from the old section-F/H table row -->
| D399 | **File-select bottom row (SELECT FILE text + Copy/Erase + PC Options dot/label) shifted 15px left as a block (user sign-off 2026-09-27).** Full `## D399` entry at file tail. | **CLOSED (2026-09-28 bookkeeping: landed with user sign-off, shipped in v0.4.0).** Earlier: LANDED (port-only, D343 class): `selectpos.f[0]` 110→95 (new `#ifdef PORT` in front.c), Copy 200/222→185/207, Erase 282/304→267/289 (D343 PORT block), `LABEL_X` 358→343 (`frontoptions.c`); hit bounds recompute from the shifted positions. Clean build, PCDUMP frame 195 no overlaps; by-eye check **done 2026-09-28 (user: good) -- RESOLVED**. |

## D399 — File-select bottom row shifted 15px left as a block (user sign-off 2026-09-27)

User: the D398 layout "is okay but a bit cramped" — asked whether the whole
row, **including the "SELECT FILE" text**, could move left. Answer: yes —
every x on that bar is a plain float constant, so it's the same mechanical
D343 class of port-guarded position tweak (no logic, no new assets).

Shift applied (all −15 logical px, NTSC; JP/PAL share the same PORT
constants):
- `selectpos.f[0]` 110 → 95 — the `IMAGE_SELECTFILE` 122×18 title, which is
  NOT a 3D/asset element: it's `texSelect` + `display_image_at_position`
  at `selectpos` in `constructor_menu05_fileselect` (src/game/front.c, ~2850).
  Wrapped in `#ifdef PORT` (new; N64 keeps 110).
- Copy icon 200 → 185, Copy text 222 → 207 (D343 PORT block, values updated).
- Erase icon 282 → 267, Erase text 304 → 289 (same block).
- `LABEL_X` 358 → 343 in `port/src/frontoptions.c`; the dot's dynamic
  `x = folder_option_ERASE_bound.right + LABEL_GAP` tracks the new Erase
  position automatically (323 + 20 = 343 = LABEL_X, so the clamp is a no-op
  on NTSC).

Hit bounds (`folder_option_COPY/ERASE_bound`) recompute from the shifted
positions, so the clickable regions move with the art. Nothing else on the
screen references these coordinates.

**Verification:** clean build; PCDUMP frame 195 column profile: SELECT FILE
text now 34..156, Copy icon 169..201, Erase text ends ~323, dot ~341..357
(no overlap, 18px clearance), label ends before the right edge. **Owed:
by-eye check via `scratch\playtest_v040.bat`.**
