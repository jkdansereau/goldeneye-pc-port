# D637

<!-- from the old section-F/H table row -->
| D637 | **Fingerprint fallback in the loader: a texture with no texture number is keyed by RiceCRC32 over its loaded texels and replaced from `<pack>/fp/GOLDENEYE#crc#fmt#siz...png` - brings in the menu/HUD font (188 glyphs, D635)** - `port/src/texpack.c`, `port/fast3d/gfx_pc.cpp`, `tools_pc/texpack/gehd_build.py --fp`; full `## D637` entry at file tail | BUILT 2026-10-10 (`feat/texpack-fp`); C CRC == Python on 80 ROM glyphs; not run-verified (game runs on hold) |

## D637: fingerprint fallback for textures without a texture number (the font)

**Status:** BUILT 2026-10-10 on `feat/texpack-fp`; port-only, inert unless
the selected pack has an `fp/` folder. Not run-verified (game runs on hold,
D632): the first launch should show the HD font in the menus and HUD.

**Loader** (`port/src/texpack.c`). `texpackInit` also scans
`<pack>/fp/` (recursively, a few levels) for files named
`GOLDENEYE#<crc>#<fmt>#<siz>...png`, adds them to the file table under a
stage of their own (never matched by a texnum lookup; same decode cache,
2048 cap and LRU as every pack file) and builds a sorted key index
(crc, G_IM_FMT, G_IM_SIZ; first file wins per key). `texpackRiceCrc` is the
GlideHQ/RT64 RiceCRC32 (big-endian words, word hash carried into the row
end); checked equal to the Python implementation (`font_key_probe.py`,
`rice_hq`) on 80 glyphs read from the ROM font banks.

**Renderer** (`import_texture`). After a texture-cache miss, a texture with
no registered texture number and a non-CI format is fingerprinted: row bytes
= the render tile's line (`(width+7)&~7` bytes for the I8 font, the D635
match condition), height = the tile's, row stride = the full image line for
a LoadTile window, else the row; bytes in N64 order via
`gfx_tex_normalize_source`, clamped to the loaded extent. A hit uploads the
pack image under the vanilla cache key (so it is computed once per cache
entry) through the same path as texnum replacements, including the D615
transparency rules. Textures that have a texture number are untouched.

**Builder.** `gehd_build.py --fp [folders]` copies pack folders under
`fp/` keeping their names (default `UI/Letters`, the font). The authoring
guide documents `fp/`.

**Not covered yet:** paletted (CI) textures without a texture number (their
emulator key also carries a palette CRC).
