# D269

## D269 — M-128: **the wall is CI8 32×32, not IA at all** — band-gated all-format probe identifies two draw classes per room stream (OPA + XLU), both BILERP/LOD/2-cycle/**FOG-on**; the cards are soft gradient blobs, and the stripe period ≈ one card per visible room

`GE_D266W` v4 (`gfx_sp_tri1`, env-gated): tracks **all** draw classes whose projected vertices fall in the top 3–9% of the viewport (the measured stripe band), logging fmt/line-width/oml/omh/zmode/cyc/blender/imgseq + first-vertex screen position. Results per room stream (imgseq cycling ~174→179→264→269→282…, shared ROM addresses 0x70173840 / 0x70173de8):

- **Class A (OPA):** CI8 32px, oml=0xc4112078, omh=0x0099ac20, first vertex screen(1.30, 0.07) — off-screen right
- **Class B (XLU):** CI8 32px, oml=0xc41049d8 (m1=CLR_IN·A_IN / m2=CLR_MEM·1MA = standard straight-alpha), omh=0x0099ac60, zmode=XLU, first vertex screen(0.61, 0.08)

**omh decode (GE gbi.h field layout: TEXTFILT@12, TEXTLUT@14, TEXTLOD@16, TEXTPERSP@19, CYCLETYPE@20):** both classes are **BILERP + TEXTLOD-on + perspective-corrected + 2-cycle**, and **bit 13 = FOG enabled**. The band is ~19 repeats across a 1957px frame (period ≈101px) ≈ one card per visible room in the row.

Texture content (`GE_TEXDUMP`/`GE_TEXRAW` + offline decode of `texdump/r*.bin`): the CI8 cards are **soft vertical gradients / sparse blobs** (index histograms concentrated at low indices, e.g. idx 6–14; ASCII renders show gradients and small clumps, not tree silhouettes). NCC correlation of every dumped card against the frame-730 capture at scales 1/2/4: nothing above 0.35 — the cards are not visible as rigid copies (consistent with bilerp magnification + fog wash).

**Reframed hypotheses, ranked:**
1. **Fog.** Both classes have FOG on. On N64 these distant cards are presumably washed toward the sky colour into a smooth pale band; if the PC's fog range (`rsp.fog_mul`/`fog_offset`; per-vertex fog computed at `gfx_pc.cpp:1591`, `fog_z = z*winv*rsp.fog_mul + rsp.fog_offset`) or its shader application differs, the raw dark gradient cards show through as hard stripes. D218 family ("fog/far-clip tint"). **Cheapest test:** extend the band probe to log per-vertex fog for class A/B tris (if vFog≈1 but screen shows dark → fog not applied in shader; if vFog small → range mismatch), or env-gate a fog disable and compare.
2. **TEXTLOD / mipmap sampling.** N64 samples mip levels when minifying; check whether the port generates GL mipmaps for these tiles (tex_lod path) and which min/mag filter is used — and first measure the *projected quad extent* (the probe only logs one vertex; if cards are minified rather than magnified, mips matter directly).
3. **Draw density / portal culling.** PC may draw cards from rooms N64 culls → denser wall. Compare total band-tri count vs expected visible-room count.
4. **Palette (D217 family).** GE_TEXRAW now also writes `.pal` (512 B = 256 RGB555) for CI formats; the aborted run never produced them. Decode class A/B cards as RGB and check colours against the N64 ref (`docs/img/bugs/d227-surface-emulator-ref.png`: smooth bright band + organic clusters centred x≈0.35–0.75).

**Owed next (concrete):** (1) re-run `rm -rf texdump && mkdir texdump && GE_TEXDUMP=1 GE_TEXRAW=1 GE_D267=1 timeout 15 ./build-pc/ge007.x86_64.exe -level_36` and confirm `.pal` files land; (2) decode `texdump/r173..r282_f2_*` CI8 cards + palettes offline to RGB PNGs for the user's eyeball (this model cannot view images — programmatic analysis only; a vision-capable session could do the N64-ref comparison directly); (3) add projected quad extent (min/max screen x over the 3 vertices) + per-vertex fog to the `GE_D266W` band probe — one more run decides hypothesis 1 vs 2.
