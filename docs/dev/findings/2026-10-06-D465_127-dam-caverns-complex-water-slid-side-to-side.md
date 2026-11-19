# D465

<!-- from the old section-F/H table row -->
| D465 | **#127 Dam/Caverns/Complex water slid side to side: the water animation controller patched its PRIM LOD fraction into a static DL by raw u32 index 8 (N64 `Gfx[4].w0`), which on the 16-byte PC `Gfx` is `Gfx[2].w0`, tile 0's SetTileSize T offset (class B)** — full `## D465` entry at file tail | FIXED 2026-10-01 (`fix/d465-water-primlod`, local); rate measured = N64; maintainer by-eye on Dam: still 'slightly faster' than 1964 (see entry). |

## D465 — #127: Dam/Caverns water slides side to side (16-byte Gfx raw-index write, class B)

**Symptom (#127, outside report):** in Dam and Caverns the water texture moved side to side very fast.

**Root cause (code, [H]):** `sub_GAME_7F092E50` (`unk_092E50.c`, the water animation controller, once per frame from `lv.c:1357`) scrolls tiles 0/1 of two static display lists through the LE shim's `.loadtile.sl/.tl` (correct on PC), then writes the shimmer value `sin(t)*127+128` into the PRIM LOD fraction with `((u32 *)MipMap2C_Something_Setup)[8]` (and the same for `..Something2..`). u32 index 8 is N64 `Gfx[4].w0` (the `gsDPSetPrimColor`; its low byte is the LOD fraction). On PC, where `Gfx` is 16 bytes, index 8 is `Gfx[2].w0`: tile 0's `SetTileSize`, whose low byte is part of the T origin. So every tick the T scroll was overwritten by the shimmer sine (period 157 ticks ≈ 2.6 s, amplitude up to 255 quarter-texels ≈ 64 texels): the water swung back and forth, and the cross-fade never animated. The lists are used by BG water textures (`tex.c`: texnum 1508 Caverns → `MipMap2C_Something_Setup`; texnum 1511 Dam/Complex → `MipMap2C_Something2_Setup`). Sky water (Frigate sea, `sky.c` → `sub_GAME_7F09343C(gdl, 0)`) emits its own commands and was never affected.

**Fix:** `#ifdef PORT`, write the same masked value into `MipMap2C_*[4].words.w0` (the N64 word sits in the low dword of `w0`; the second list still reads the first list's word, as the original does). The `#else` arm is unchanged. Porting-notes class B (16-byte PC `Gfx`); the layout/ABI exception, no Rule-2.

**Rate check (TEMP probe, removed):** after the fix, a headless Dam run measured the sim at 59.84 ticks/s and the S scroll at 14.97 quarter-texels/s, against an N64 expectation of 0.25 × 59.94 = 14.99. That's one controller call per frame, and the port clock is exactly 60 ticks/s (`osGetCount` 46.5525/µs ÷ 775875). fast3d applies the tile origin as `S/32 >> shift − uls/4`, which is the RDP's formula. So on the PC side the water speed matches N64 to within about 0.1%.

**Maintainer by-eye (Dam, vs 1964/GEPD, fixed build):** "it does still seem SLIGHTLY faster on PC vs 1964". Not explained by code: candidates are 1964 running below full speed, frame-rate smoothness (60 vs ~20 fps rendering), and filtering. Objective check: time ten shimmer brightness cycles (expect 26.2 s, i.e. 157 ticks each) on both.
