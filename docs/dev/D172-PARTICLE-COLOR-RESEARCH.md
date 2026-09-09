# D172 — particle/flares wrong color (magenta/cyan blood): static research + root-cause candidate

Symptom (user-reported, GRAPHICS-BACKLOG line 31): bullet impacts / flares / blood spatter
render magenta/cyan instead of dark red. Static audit done; one strong root-cause candidate
with a cheap in-game verification. **No code changed.**

## 1. How particles get their color state (mechanism)

Particle triangles (`explosion.c`, `gSPVertex` + `G_TRI_FILL`/`G_TRI_SHADE_TXTR`) carry
**no per-primitive material command**. Their CC/tile state comes from replaying ROM-resident
state records via `gSPDisplayList(gdl++, g_ExplosionDisplayLists[var_s2])`
(`explosion.c:894`, 15-record loop, one record + its particles per iteration).

- Records: `globalDL_0x078 … globalDL_0x9a8`, a 17-entry table at image-bank offset
  0x28 (`g_pc_gimg_off_globalDL_*`), bank base cart 0x1029D160 (see `image_bank.c:322`).
- Each record is also scanned at load by `texLoadFromDisplayList` (`src/game/image.c:475`)
  for its `G_SETTIMG` + ABCD marker word — that part works (textures upload fine).

## 2. Record layout (decoded from ROM)

```
+0x00  E7 C3 FF FE        G_RDPPIPESYNC
+0x08  B9 00 03 1D        ???  <-- UNHANDLED by fast3d (see §4)
+0x10  <varies>           second word of the ??? command
+0x18  FC ...             G_SETCOMBINE w0   (rgb c0 | alpha c0 <<16)
+0x20  FF ...             G_SETCOMBINE w1   (rgb c1 | alpha c1 <<16)
+0x28  FD ...             G_SETTIMG w0      (timg pointer)
+0x30  .. 50 50 50 50     G_SETTIMG w1      (ABCD marker)
```

Dominant particle combine mode (14 of 17 records), decoded with the standard libultra
bit layout:

- cycle 0 RGB: `TEXEL0, 0, TEXEL1, COMB_A`
- cycle 0 ALP: `TEXEL0A, COMB_A, SHADE, COMB_A`
- cycle 1 RGB: `COMBINED, 0, SHADE, COMB_A`   ← **2-cycle** modulate
- cycle 1 ALP: same as cycle 0

## 3. fast3d audit (what is right)

- `G_SETCOMBINE` bit extraction (`gfx_pc.cpp:2786`) matches the standard libultra layout
  exactly (verified field-by-field against the ROM words). Not the bug.
- Store/decode of `rdp.combine_mode` (`gfx_dp_set_combine_mode` vs `gfx_generate_cc`
  key shifts) are consistent. Not the bug.
- The generated GLSL combiner implements the RDP formula with the usual special cases;
  `SHADER_COMBINED` correctly resolves to the previous cycle's running value.

## 4. The 0xB9 root-cause candidate is WITHDRAWN (M-82, 2026-09-08)

The earlier audit claimed fast3d has "no case for opcode 0xB9" and that the D146
`default:` guard therefore aborts every particle sub-DL. **That is wrong.**

This build does **not** define `F3DEX_GBI_2` (verified: no `-DF3DEX_GBI_2` in
`CMakeLists.txt`, no `#define` in `include/PR/gbi.h` or anywhere in tree). In the
non-F3DEX2 branch of `gbi.h`, `G_SETOTHERMODE_L = G_IMMFIRST-6 = -71`, and
`(uint8_t)(-71) == 0xB9`. So `gfx_pc.cpp:2731` `case (uint8_t)G_SETOTHERMODE_L:`
**is** `case 0xB9:`.

Decode of the record word `w0 = 0xB900031D`, `w1 = <varies>`:
`gfx_sp_set_other_mode(C0(8,8)=3, C0(0,8)=29, w1)` → sets `other_mode_l` bits
3..31. `sft=3 == G_MDSFT_RENDERMODE`, `len=29` — this is exactly
`gsDPSetRenderMode(w1)`. `w1` values (`0x0C184B50`, `0x00504B50`, …) are
render-mode / blender words. fast3d applies it correctly.

So the particle sub-DL runs to completion on PC: `E7` RDPPIPESYNC (handled),
`B9` SetRenderMode (handled), `FC` SETCOMBINE (handled), `FD` SETTIMG (handled).
**No D146 abort. No stale-state fallthrough.** The wrong-colour bug is elsewhere.

## 5. Cycle-type is ALSO ruled out — the real record, and where the bug is (M-82)

The actual particle records are in **`assets/oddtextures.c`**
(`globalDL_0x078` … , 17 of them). One record verbatim:

```c
gsDPPipeSync(),
gsDPSetCycleType(G_CYC_2CYCLE),            // <-- record DOES set 2-cycle
gsDPSetRenderMode(G_RM_PASS, G_RM_ZB_CLD_SURF2),
gsDPSetTextureLOD(G_TL_TILE),
gsDPSetCombineMode(G_CC_INTERFERENCE, G_CC_MODULATEIA2),
gsSPTexture(..., G_TX_RENDERTILE, G_ON),
  // tile 0: IMAGE_SMOKE_0  IA16 image -> loaded/rendered as IA8, 56x56
  // tile 1: IMAGE_FIRE_0   RGBA16, 16x14, loaded to tmem 0x188
gsDPSetTextureLUT(G_TT_NONE),
gsDPPipeSync(),
gsSPEndDisplayList(),
```

So the record **explicitly sets `G_CYC_2CYCLE`** (opcode `0xBA` =
`G_SETOTHERMODE_H` in this build), and fast3d handles `0xBA` correctly:
`gfx_pc.cpp:2734` → `gfx_sp_set_other_mode(52, 2, w1<<32)` sets `other_mode_h`
cycletype = 2CYC. **Confirmed by probe (M-82):** the `GE_D172=1` SETCOMBINE
probe logged the particle combine `w0=fc26a004` (= `G_CC_INTERFERENCE`/
`G_CC_MODULATEIA2`) with **`cycletype=1 (2CYC)`** every time. The cycle-type
hypothesis is dead too.

**The bug is the `G_CC_INTERFERENCE` two-tile combine / TEXEL1 path.**
`G_CC_INTERFERENCE` cycle-0 = `TEXEL0 * TEXEL1`; the record binds **two
different tiles of two different formats** (tile 0 IA8 smoke, tile 1 RGBA16
fire at tmem 0x188). Magenta/cyan = a TEXEL1 sampling/decode fault: wrong tmem
offset for tile 1, RGBA16 (5551) channel/endian misread, or fast3d treating
tile 1 as an LOD mip of tile 0 under `G_TL_TILE`. This needs a visual
render-iterate pass (headless can't see particle colour) — see §6.

### Probes shipped (M-82, `#ifdef PORT`, `GE_D172=1`, inert when unset)

- `gfx_pc.cpp` G_SETCOMBINE case — logs each distinct combine + active
  cycletype (dedup, 64 max).
- `gfx_pc.cpp` `gfx_sp_tri1` — flags tris with a non-trivial cyc2 combine
  drawn while cycletype != 2CYC (over-fires on trivial passthrough cyc2 —
  read `combine_mode` values, don't trust the count).

## 6. Fix path (next session — needs a display)

1. Get a `-level_XX` capture with visible particle spray (Silo guard kill at
   close range per the original report, or any wall-impact spark). Note the
   colour.
2. Env-gated probe in `gfx_pc.cpp` `import_texture` / the TEXEL1 bind path:
   for tile 1 when combine == the particle key, log format/size/tmem-addr/
   line_size and the first few decoded texels. Compare against the RGBA16
   `IMAGE_FIRE_0` bytes in the extracted asset.
3. Likely fixes, in order of suspicion: (a) tile-1 tmem address 0x188 not
   honoured → TEXEL1 reads tile-0 bytes; (b) RGBA16→RGBA32 decode for the
   second texunit; (c) `G_TL_TILE` LOD path picking the wrong tile for TEXEL1.
4. Re-decode `G_CC_INTERFERENCE` / `G_CC_MODULATEIA2` as fast3d generates them
   (`gfx_cc.cpp`) and confirm the GLSL matches `TEXEL0*TEXEL1` then
   `COMBINED*SHADE`.

## Files

- `src/game/explosion.c` — record table (165), replay loop (894)
- `src/game/image_bank.c:322`, `src/game/image.c:475` — record offsets / load-time scan
- `port/fast3d/gfx_pc.cpp` — SP walker cases (~2600–2930), D146 default (2920),
  SETCOMBINE decode (2786), combine store (2270), cc generation (312+), 2CYCLE flag (1523)
- `rsp/graphics/gmain.s` — N64 ucode ground truth for 0xB9 (dispatch table at dmem 0xbc;
  ginit.s missing from repo)

Note (M-82): 0xB9 is `G_SETOTHERMODE_L` in this non-F3DEX2 build and is already
handled by fast3d — `gmain.s` / `ginit.s` are NOT needed to resolve D172.
