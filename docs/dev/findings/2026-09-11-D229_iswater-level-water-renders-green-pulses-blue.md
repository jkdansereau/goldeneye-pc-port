# D229

<!-- from the old section-F/H table row -->
| D229 | **IsWater-level water renders green, pulses blue<->green (M-101; Frigate `-level_26`).** — full `## D229` entry at file tail | **FIXED (port-layer)** — ZLIB+CI8 water image now imports via the CI8 palette path (`gfx_pc.cpp`); Frigate verified live ("looks better"); sweep + Surface 2 by-eye still owed. Full trail in the section. |

## D229 — M-101: Frigate/IsWater-level water renders green and pulses blue<->green. Pre-existing (reproduces on main), mechanism partly identified, NOT root-caused

**Reported (user playtest, M-101):** on Frigate the water "turns green
randomly and goes between looking right and glitching green", later refined to
**"it changes from blue to green on an interval like pulsating"** and, at one
point under a bad intermediate build, "only green". Also reported in the same
message: green weapon textures on Frigate and Surface 2 — **that half was a
stale-branch artifact, not a bug** (see below).

**The gun half: not a bug.** `land/d176a-sky-perspective-scroll` was 2 commits
behind `main`, and those 2 commits were exactly the D217 green-texture fixes
(`7278baad` = #47 texpool 8-byte alignment, `f15a12b9` = #49 CI texture cache
keyed on palette content). The user was play-testing a branch build that
predated both. Merged main in (`f8822805`); user confirms **"gun is good on
frigate"**. No new defect. Worth remembering as a process point: a D217-family
symptom report against a feature branch should check branch-vs-main first.

**Pre-existing, and not caused by the D227 work — proven by A/B.** Built plain
`main` (none of the D227 changes) and captured `-level_26` frame 340: the
water is the **same green**. The only difference is tiling density — main
shows it 32x over-tiled (the D227/M-100b unit bug), the branch shows it
smooth. Green in both. So D229 is independent of D227.

**Mechanism, as far as it is established.** The water quad is `sky.c`'s
`IsWater` block (`skyRender` block1; Frigate confirmed `IsWater=1` via
`GE_D176`, which also answers M-99's ranked item 1 — that path IS textured and
does go through `skyPortRenderPoly`). Its draw state comes from
`sub_GAME_7F09343C` (`src/game/unk_092E50.c:278`):

- tile 0 and tile 1 are both `G_IM_FMT_RGBA`/`G_IM_SIZ_16b`, `line=4`,
  **both at TMEM 0** — i.e. the same texture — with `masks = maskt = 5`
  (32-texel wrap).
- tile 1 is offset: `gDPSetTileSize(1, uls=90, ult=150, lrs=0, lrt=0)`, so it
  samples the same image shifted by 22.5 / 37.5 texels.
- 2-cycle combine `gDPSetCombineLERP(TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0,
  ...)` with cycle 2 `= COMBINED * SHADE`, and the LOD fraction is
  **`gDPSetPrimColor(..., sinf(flt_CODE_bss_80079E88) * 127.0f + 128.0f, ...)`
  — an animated sine.**

**That sine is the "pulsating".** The effect cross-fades TEXEL0 and TEXEL1 over
time; the user seeing blue<->green on an interval means **one texunit resolves
correct (blue) and the other resolves green**, with the sine fading between
them. This is the single most useful clue in the report and it came from the
user, not from a capture — a static frame cannot see it (frame 340 samples
green on *all three* builds tested, because the sine phase happens to land
there, which is why single-frame diffs were useless here).

**Ruled out:** the shade path. Water vertex colour is
`skyChooseWaterVtxColour` = `env->RGB + env->WaterRGB * (1 - c/255) * (1 - a)`;
measured on Frigate `env RGB = 16,48,96` and `WaterRGB = 255,255,150`, so the
shade ranges from dark blue to pale yellow and **can never be green**. Measured
water pixels are ~`(5, 70..175, 20..37)` — R and B crushed, so the green comes
from the texel side, not the shade.

**Not yet established:** why one texunit decodes green. Both tiles point at the
same TMEM with the same format, so a plain "wrong texture" explanation does not
fit; the difference between them is only the uls/ult offset and whatever
fast3d does with tile 1's inverted window. One concrete oddity found and *not*
yet shown to matter: `uls/ult/lrs/lrt` are `uint16_t` and
`gfx_dp_set_tile_size` computes `width = ((lrs - uls) >> 2) + 1`, so tile 1
gets `((0 - 90) >> 2) + 1 = -22` stored into a `uint16_t` = **65514** (height
likewise 65500). `tex_width2` goes negative by the same arithmetic, which as it
happens makes the `g_wrap_fix` sub-tile-window branch skip (`tex_width2 > 0`
fails) and fall through to the mask-based 32-texel wrap — which is the correct
period — so this may be harmless. It has not been proven either way.

**FOV context (M-101):** the user's playtest ran with **FOV turned up**
(`Video.FovScale`, PR #36's slider, range 50-150). Every headless capture in
this entry ran at the ini's `FovScale = 100`, i.e. **default**, and still
reproduces the green — so raised FOV is *not required* to trigger D229. Whether
it worsens it is untested (deliberately: the config file was open in the user's
editor and not worth racing). Worth checking when this is picked up, because
FOV-coupled sky/frustum coverage is a known separate symptom family
(D218/D222), and the black bands visible at the edges of the Frigate captures
are likely that rather than D229.

**Next steps:** dump what fast3d actually decodes for each texunit on this draw
(the existing `GE_D172` multitex probe does not fire here — it filters on
`tile1.tmem != tile0.tmem`, and these are equal, so it needs widening);
capture a *sequence* across the sine period rather than one frame, since the
defect is temporal; and check `skywaterimages[WaterImageId=2]`'s real format
against the `RGBA/16b` that `sub_GAME_7F09343C` re-declares.

**D227 deliberately decoupled from this.** The M-100b safety valve now takes an
`allowShift` argument and the IsWater quad passes `FALSE`, leaving that path
byte-identical to its pre-D227 behaviour. Reason: the water quad is a
two-texunit draw sharing one set of vertex tc, so a tc rescale must be matched
by a tile shift on *both* tiles — and shifting both makes them sample
identically, which flattens the very cross-fade the effect is built on. That
was observed live: an intermediate build that shifted both tiles turned the
water from pulsating into **constantly green**. Neither variant is right, so
the water quad keeps plain 1:1 tc and tolerates the overflow on its horizon
vertex exactly as before. D227's fix stands on its own for the non-water
levels; D229 is a separate defect needing its own fix.

**ROOT CAUSE FOUND AND FIXED (port-layer, `port/fast3d/gfx_pc.cpp`).** The water
image (`skywaterimages[2]` = texnum 1509, `IMAGE_WATER_BLUE`, 32×32, level 6) is a
ZLIB+CI8 image: the texture pool holds raw 8-bit palette indices (verified:
the dumped pool block is 1400 bytes, every byte ≤ 23 — exactly the 24-colour
blue palette's index range; the full mipmap chain 32²+16²+8²+4²+2² plus row
padding). `texSelect` (`othermodemicrocode.c`, "Perfect Dark texSelect") loads it
with `gDPSetTextureImage(CI, 16b)` + `gDPLoadBlock` — the 16b "size" is only the
fast3d 4KB-per-block convention (lrs counts 16-bit units: 700 × 2 = 1400 bytes of
8-bit indices). On N64, GE's custom RSP ucode expands the indices through the
TLUT into real 16-bit pixels in TMEM at load time — which is why
`sub_GAME_7F09343C` can then re-declare TMEM slot 0 as RGBA/16b and sample blue.
fast3d has no such expansion: `import_texture_rgba16` read pairs of index bytes
as 16-bit texels, where g = 2·idx mod 32 dominates and r/b ≤ 2 → the green
mottle. The "pulsing" is the pre-existing `sinf()`-driven `PRIM_LOD_FRAC`
cross-fade between the two tiles (both mis-decoded, at different UV offsets),
not a second bug.

**Fix:** track the source format of each TMEM load (`texture_to_load.fmt` was
previously dropped; now recorded into `LoadedTexture.src_fmt` by
gfx_dp_load_block/gfx_dp_load_tile) and, in `import_texture`, when a tile is
declared RGBA/16b but the slot's last load came from a CI source, route the
import through `import_texture_ci8` (palette expansion via `rdp.palette`) — the
port-layer equivalent of the ucode's expand-at-load. Fires only on the genuine
format disagreement; real RGBA16 textures are untouched. Verified live: Frigate
water renders blue and the user confirms "looks better". Closure still wants the
standard verification ritual (sweep + by-eye on Surface 2, the other IsWater
level).
