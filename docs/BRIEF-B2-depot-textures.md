# BRIEF — B2: Depot (-level_30) wrong-colour / garbage textures

M-29 diagnostic pass. **Not fixed.** Evidence captured, leading hypothesis
recorded. Companion: `docs/BACKLOG.md` B1/B2, `docs/SMALL-FIXES.md` C-section.

## Symptom (captured)

`-level_30`, opening loading-bay area, `GE_PCDUMP` frames ~700-1150
(camera panning up toward the roof):

- The **ceiling / roof surface** renders as bright blue with a dense green
  speckle and radial "light-ray" streaks emanating from a point. On N64 this
  is a dark, low-contrast corrugated roof.
- Walls (green wall panels right, tan crates left) look approximately
  correct — mild green tint at most.
- HUD ammo digits mirrored = D116, unrelated.

Frames: `ppm/frame_0007..1150` this session (regenerate with
`GE_PCDUMP="700-1600:150" ./build-pc/ge007.x86_64.exe -level_30`).

## Leading hypothesis (revised M-29): this is mostly B1, not a decode bug

The radial-ray + sparkle pattern is the **classic grazing-angle aliasing**
signature of a high-frequency texture sampled with point/bilinear filtering
and no working LOD/mipmap chain. The ceiling is at a very shallow angle to
the camera in these frames — exactly where N64 3-point + LOD filtering
suppresses shimmer and our fast3d path does not (see BACKLOG B1: "whole game
has an interlaced/shimmering look"; `port/fast3d/gfx_pc.cpp` mip handling is
stubbed — `gfx_lod_tile_offset` / "for now just ignore smaller mips" at
~1540, and D107 notes fast3d fabricates a crop when detail textures are off).

**Decode path was audited and looks correct:**
- `gfx_dp_load_tlut` byteswaps entries (`PD_BE16`), `palette_to_rgba32`
  decodes RGBA16 5551 and IA16 correctly, `rdp.palette_fmt` is set from
  `other_mode_h & (3<<G_MDSFT_TEXTLUT)` (line ~2389).
- `import_texture_rgba16` / `ci4` / `ci8` decode textbook-correct, BE reads.
- D72 already disabled the PD normal-derived texgen for GE, so the streaks
  are not env-mapping.

Nothing in the decode path obviously produces "blue + green speckle".

## Next diagnostic step (for a focused session)

1. **Probe the ceiling draw:** in `gfx_pc.cpp` `import_texture` / the tri
   setup, `GE_DTEX=1`-gate a log of `{orig_addr, fmt, siz, palette_index,
   line_size_bytes, width, height, cms/cmt, other_mode_h & TEXTFILT, tex_lod}`
   for the first ~200 unique textures on `-level_30`. Identify which entry is
   the ceiling (large, tiled, grazing).
2. If its `fmt/siz` and data look sane → confirm B1: the fix is proper
   trilinear + a real mip chain (or an N64 3-point-filter emulation shader),
   per BACKLOG B1 "MVP: one good default filtering mode". Do B1 first, then
   re-shoot this frame; B2 likely collapses into it.
3. If the data is garbage (wrong TMEM offset, wrong decompressed RZ section)
   → it's a real decode/addressing bug: check `line_size_bytes` vs
   `full_image_line_size_bytes` (the `SUPPORT_CHECK` at
   `import_texture_rgba16:657` is *commented out* with a TODO about garbage
   `full_image_line_size_bytes`), and the RZ section offset from
   `rzipGetSomething()` in `src/game/image.c`.

## Risk / scope

B1 (filtering + mips) is a medium fast3d task with a clear reference (PD port,
Nightdive 3-point emu). It is the highest-leverage single graphics fix —
addresses the game-wide shimmer complaint AND (hypothesis) most of B2. Do it
as its own focused effort, not inline.
