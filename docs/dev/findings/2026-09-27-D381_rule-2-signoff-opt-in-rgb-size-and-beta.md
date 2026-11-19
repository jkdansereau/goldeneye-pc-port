# D381

<!-- from the old section-F/H table row -->
| D381 | **RULE-2-SIGNOFF: opt-in RGB, size and Beta crosshair (2026-09-27).** full `## D381` entry at file tail | **CLOSED (2026-09-28).** Shipped in v0.4.0; accepted in the user's final review / campaign playtest. |

## D381 — RULE-2-SIGNOFF: opt-in RGB, size and Beta crosshair (2026-09-27)

**Status: IMPLEMENTED; live visual acceptance and golden sweep owed.** The
user asked for an RGB slider in the menus, then crosshair size and alternate
in-game art. After the concrete design was explained (Original/default,
Custom RGB with a swatch, 50–200% size, Original/Beta using the existing
`IMAGE_CROSSHAIR2`), the user explicitly replied **“OK proceed with all of
these.”** This is an opt-in customization, not a claim that the N64 drawing is
wrong: with the default Original/100%/Original, the same game code draws the
same source image at the same dimensions with the same N64 combiner. There
is therefore no alleged default N64 divergence to prove. D379 already
established the actual red source image/combiner contract (not a port bug in
the default path). The user-approved opt-in behaviour deliberately differs
from the fixed N64 appearance; it is not an ABI/layout fix.

**Narrow src/game scope:** `src/game/gunfire.c:gunDrawSight`, inside the
existing `#ifdef PORT` crosshair hook (D373/D379) only. The exact
`texSelect`/`display_image_at_position` calls determine which of the two
32×32 asset-table entries is uploaded and the half-size of the sprite. These
cannot be changed safely from `port/` alone without intercepting and
rewriting another game's texture/draw display list after the fact. Only the
opt-in image pointer and half-size are selected; the aim coordinates, alpha,
other sprites, and unguarded N64 `#else` code are unchanged. PD's analogous
port uses configurable sight colour/size in `../pd_port/src/game/sight.c`
and sliders in `../pd_port/port/src/optionsmenu.c`; its image formats are
not assumed interchangeable with GE's. The previously unused
`betacrosshairimage` is loaded in `src/game/image_bank.c` and backed by
`IMAGE_CROSSHAIR2` in `assets/oddtextures.c`; it is **not** automatically
selected by 007 mode.

**Implementation:** `port/src/video.c` registers `Video.CrosshairRed/Green/
Blue` (0–255, defaults 255), `Video.CrosshairSize` (50–200, default 100),
and `Video.CrosshairStyle` (0 Original, 1 Beta). Existing colour values
0–7 preserve their meanings; 8 = Custom. Colour 0 keeps the sprite RGB
and original combine mode; all other colours (including Custom) use the
D379 alpha-silhouette combiner. The three RGB sliders appear only while
Custom is selected; changing presets does not discard their saved values.
`port/src/optionsoverlay.c` adds a functional CROSSHAIR page because the
GRAPHICS front page has a fixed 14-row cap; F10 and front options share its
rows, live values, and section reset (including hidden RGB rows). Both UIs
show a representative colour swatch; Original shows red, not the white
identity multiplier. The game menu cursor and every other image remain
unaffected. Ini saved under `$S/ge007.ini` as before; user ini was untouched
for testing.

**Verification:** MSYS2 MINGW64 `./build-pc.sh ntsc-final` compiled/linked.
In an isolated temporary data directory, `-level_09` with scripted aim
(`GE_INPUTSCRIPT='30:START;60:RHOLD'`, `GE_PCDUMP=580-580`) rendered and
quit cleanly twice. Beta/Custom (10,200,240)/150% had 159 cyan-dominant and
0 red-dominant pixels in the 100×80 reticle region; Original/100% had 384
red-dominant and 0 cyan-dominant pixels there. With `GE_TEXDUMP`/`GE_TEXRAW`,
the Beta texture was actually loaded as 32×32 RGBA32 (4096 bytes): 112
nonzero-alpha pixels in a thin plus-shaped sight, confirming that the
D379 colour-mask approach applies. Section row census: CROSSHAIR has at
most 8 content rows, GRAPHICS 10, both under the front menu's 14-row cap.
`git diff --check` passes. These scripted checks do **not** replace a human
size/style/colour swatch inspection or the post-landing 21-level golden gate.
