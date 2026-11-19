# D341

<!-- from the old section-F/H table row -->
| D341 | **Intro gun-barrel blood drip draws as static blocks/lines instead of dripping (user, v0.4.0, 2026-09-26; death blood fine).** — full `## D341` entry at file tail | FIXED (M-201, user-verified 2026-09-27): two fast3d defects — the 1-cycle blood texrect sampled a stale tile-1 declaration via the D236 LOD rule, and the in-place-regenerated dyn-pool texture hit a stale address-keyed cache entry. |

## D341 — Intro gun-barrel blood drip draws as static blocks/lines instead of dripping (M-201, 2026-09-26)

**Report:** the 2D red blood "drip" at the end of the gun-barrel intro glitches and doesn't draw right; the death blood looks normal.

**Measured:** `die_blood_image_routine` builds a correct 96x80 I4 drip image each step (dumped and rendered: a growing top band with drips); `gunbarrelBloodOverlayDL` draws it as a full-screen 1-cycle texture rectangle at the title's 440x330 VI (dsdx 223, dtdy 248, tile 0 = I4 96x80, `GE_D116` confirmed). The screen showed a constant pattern of red blocks and horizontal lines that never changed.

**Cause 1 (the wrong image):** texture-unit 0 imported from **tile 1** (`unit=0 tile=1 cyc=1CYCLE lod=1`): the title leaves `G_TL_LOD` set from earlier draws, and `gfx_lod_tile_offset`'s D236 detail-base rule (tile 1 in different TMEM with a different format → sample tile 1) redirected the rectangle to a stale CI8 tile-1 declaration sharing TMEM 0 — 128 bytes of the blood buffer decoded as CI8 and stretched. On the RDP, LOD tile selection only exists in 2-cycle mode; a 1-cycle texture rectangle samples exactly its command's tile. Fix: `gfx_lod_tile_offset` returns `i` while a texture rectangle is drawing outside 2-cycle mode (`s_in_texrect`, set around `gfx_draw_rectangle` in the texrect + wide-texrect paths). 3D triangle paths (D107/D236) are unchanged.

**Cause 2 (frozen animation):** the image is regenerated in place in the per-frame dynamic pool, at the same address every step, and reached via `OS_K0_TO_PHYSICAL` → the KSEG0 mirror (`0x800a8f20`). The GL texture cache keys non-CI textures on address only, so after the first import every later step was a stale hit. Fix: textures in the dyn pool (`[g_VtxBuffers[0], g_VtxBuffers[2])`, with the 0x80000000 mirror normalised to V1) get an FNV-1a content hash in the key. A/B with `GE_DYNTEXHASH_OFF=1`: blood coverage stays at 1% (frozen); with it, the drip grows.

**Verified:** headless boot capture — the blood now runs down from the top over the gun barrel as on N64 (`scratch/blood_after.png`). Golden gate 3/3. User visual check owed.

**Status: FIXED (M-201, user-verified 2026-09-27).**
