# D608

<!-- from the old section-F/H table row -->
| D608 | **D34 `ANIM_DATA_x` re-basing guard in `assets/animationtable_data.h` was `__x86_64__`-only: the first arm64 macOS link failed with hundreds of undefined `_ANIM_DATA_*`** — full `## D608` entry at file tail | FIXED 2026-10-07 (macos branch; mechanical guard widening, arm64 link clean) |

## D608: D34 `ANIM_DATA_x` re-basing guard was `__x86_64__`-only — arm64 macOS link failed with hundreds of undefined `_ANIM_DATA_*` — FIXED 2026-10-07 (macos branch)

*Labelled D580 on the macos branch; renumbered at the v0.5.1 merge (the interpolation work had used D579-D585).*

`assets/animationtable_data.h` redefines the `ANIM_DATA_x` symbols as lvalues
into `g_pc_animdata_base` (D34: the N64 `animationtable_data.c` segment links
at VMA 0, so `&ANIM_DATA_x` is a segment offset and game code re-bases it;
the PC form points at the loaded image base instead, and the N64 array
definitions are not compiled on PC). The guard was `#if defined(__x86_64__)`,
so the first Apple-silicon link fell through to the N64 `extern s32
_ANIM_DATA_*` declarations and failed with hundreds of undefined symbols.
Widened to `__x86_64__ || (__aarch64__ && __APPLE__)`: the arm64 image is
loaded at the 16 TiB `PORT_ADDR_BASE` window, which is the same re-basing
class, and `port/src/romdata.c` already sets `g_pc_animdata_base`
unconditionally. Mechanical guard widening — no logic change, no N64-side
change (rule 2's ABI/layout class).
