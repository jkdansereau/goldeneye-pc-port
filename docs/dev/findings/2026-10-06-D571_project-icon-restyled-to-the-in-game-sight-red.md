# D571

<!-- from the old section-F/H table row -->
| D571 | **Project icon restyled to the in-game sight red as pixel art (exe, window, favicon)** — full `## D571` entry at file tail | CHANGED 2026-10-06 (maintainer request; make_icon.py; social preview unchanged) |

## D571: Project icon restyled: in-game sight red, pixel-art grid — CHANGED 2026-10-06 (maintainer request)

The D550 mark (gold, smooth vector ring) is redrawn by `tools_pc/make_icon.py`
as pixel art in the in-game sight's red (core RGB 200,40,48; a lighter 225,120,130
tip texel on each bar like the sprite's anti-aliasing) on #050607: hand-tuned
16/24/32 grids (1-px ring, 2-px bars, open centre), larger sizes scaled with
NEAREST (48 = 24 grid x2, 64..512 = 32 grid). Original procedural art (no ROM or
capture data). Outputs regenerated: exe `.ico`, `docs/favicon.ico` (byte copy),
`docs/img/icon/ge007-icon.png`, `port/fast3d/ge007_icon.h` (now written with LF).
`--check` passes; two runs byte-identical. The social preview card is unchanged
(maintainer decision).
