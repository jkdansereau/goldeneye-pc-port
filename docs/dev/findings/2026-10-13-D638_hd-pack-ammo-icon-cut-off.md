# D638

<!-- from the old section-F/H table row -->
| D638 | **HD-pack ammo icon cut off: the draw normalises texture coordinates by the padded TMEM line (a 5-texel RGBA32 icon is sampled as 8 wide), but the pack image holds only the visible texels, so it was stretched and cropped** - `port/fast3d/gfx_pc.cpp` (import_texture pack upload); full `## D638` entry at file tail | FIXED 2026-10-10 (`chore/v070-agentic-batch`): the pack image is placed on a canvas padded in the same proportion (edge texels replicated); headless Dam run: 5x12 -> 8x12, 5x28 -> 8x28, 54x54 -> 56x54, HUD cartridge renders whole |

## D638: HD ammo icons cut off - pack images vs the padded TMEM line

**Status:** FIXED 2026-10-10 on `chore/v070-agentic-batch` (off
`release/v0.7.0`); port-only. Maintainer report: "the ammo icon doesn't line
up quite right, gets cut off" (the AK's).

**Cause.** The vertex path normalises S/T by `tex_width`/`tex_height`, the
render tile's TMEM line in texels (`line_size_bytes` per `siz`), which pads
each row to the 8-byte line: the RGBA32 HUD ammo icons are 5-14 texels wide
and are sampled as 8 or 16. A vanilla upload is that padded width, so the
visible texels sit where the coordinates expect them. A pack image is drawn
at the *visible* size (e.g. 160x384 = 32 x 5x12), so stretching it over the
padded width showed 5/8 of the art, squeezed. Power-of-two surfaces (floors,
walls) are not padded, which is why only small odd-width art showed it.

**Fix.** In the pack upload path, after the GL-max downscale: if the tile's
line exceeds its visible size (`(lrs-uls)/4+1`, `(lrt-ult)/4+1`) and the
pack image scales the visible size uniformly (|sx - sy| <= 1 %), the image is
copied onto a canvas of `line x scale` with the last column/row replicated
into the padding, so the sampled region matches the original exactly. A
tile that windows a larger image fails the uniform-scale test and is left
alone; the canvas must fit the GL max and the upload buffer.

**Verified** (headless `-level_33`, GE_PCDUMP, GE_QUITFRAME): `D638: texpack
image 160x384 padded to 256x384 (visible 5x12 of a 8x12 TMEM tile)`, 80x448
-> 128x448 (5x28 of 8x28), 1728x1728 -> 1792x1728 (54x54 of 56x54); the HUD
cartridge renders whole and in proportion. Clean exit (render parked, GL
teardown).
