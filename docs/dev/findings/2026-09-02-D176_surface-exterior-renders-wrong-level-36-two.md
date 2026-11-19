# D176

<!-- from the old section-F/H table row -->
| D176 | **Surface exterior renders wrong (`-level_36`), two independent defects.** (a) black sky = RDPHALF no-op, root-caused M-37; scroll-speed FIXED (perspective-correct tc in `skyPortRenderPoly`). (b) cliff/rock walls = grey diagonal static — NOT a decode bug (D183: 0/166 loads strided); re-scope from ROM ground truth of the wall texnum (M-37 inconclusive, leaning tiling-density). — full `## D176(a)` entries at file tail; see also GRAPHICS-BACKLOG.md D176 | FIXED (2026-09-29 bookkeeping: (a) sky via D227, human play-test confirmed; (b) Surface perimeter tree/rock wall via D236, closed M-198; GRAPHICS-BACKLOG row closed 2026-09-28). Earlier: **(a) scroll-speed FIXED** (fidelity pass; HIGH confidence on root cause, MODERATE on N64 parity); M-93 playtest + M-98 Defect-2 starburst screenshot in the sections. **(b) open.** |

## D176(a) — Surface black sky: env data is correct, the cloud-sky emit renders nothing (M-36, partial)

Surface (`-level_36`) sky is solid **black** where the N64 shows a warm
sunset cloud gradient. Headless `-level_36` + `GE_D176=1` probe
(`src/game/bgfog.c` `fogLoadLevelEnvironment`, `src/game/sky.c` `skyRender`):

- **The environment data is right.** `fog_tables[]` matches `Id=36`,
  `sizeof(EnvironmentRecord)=92`, `Sky.Clouds=1`, `Sky.RGB=96,96,128`,
  `CloudRGB=240,120,30` (the warm sunset), `CloudRepeat(skyheight)=10000`,
  `SkyImageId=0`, `IsWater=0`. So this is **not** a serialized-struct
  byte-order bug like D178 — `fog_tables[]` is a compiled-in C initializer
  and every field reads sane.
- **`skyRender` runs the cloud path** (not the `!Clouds` flat-fill early
  return). Corner probe: `eye=(-467.7, 374.2, -7190.5)`,
  `WaterConcavity=7.0`; the four screen-corner unproject rays come back
  `c0/c1.y ≈ +62` (`skyIsScreenCornerInSky` → TRUE) and `c2/c3.y ≈ -13.5`
  (→ FALSE). So the corner-classification switch value is
  `(1<<3)|(1<<2)|0|0 = 12` — the "top half of the screen is sky" case,
  which is correct for that camera.
- **Nothing draws.** Case 12 → `s1=4`, the sky region is built from the
  edge-vertex path and handed to `sub_GAME_7F097388` (pure-C project +
  perspective divide → screen coords) then `skyRenderTri` / `skyRenderFull`
  with `texSelect(&skywaterimages[SkyImageId=0], …)` and a
  `SHADE,ENV,TEXEL0,ENV` combine. The output is pure black, not even the
  `env->Red/Green/Blue` (96,96,128) fill — so the sky polygons are either
  degenerate after projection, culled, or the `skywaterimages[0]` bind is
  failing in a way that kills the primitive.

**Not yet root-caused.** Next steps (needs ~1–2 h / a dedicated pass):
1. Probe `sub_GAME_7F097388`'s output `unk28/unk2c` (screen x/y ×4 subpixel)
   for the 4 verts — are they on-screen and non-degenerate, or all clamped
   to one edge? That splits "projection math wrong" from "emit path wrong".
2. Check `skywaterimages` (`src/game/image_bank.c:284`,
   `globalbank_rdram_offset + GIMG_OFF(s_skywaterimages)`, PC offset
   `0xFB4`) actually resolves to valid `sImageTableEntry` records on PC, and
   that entry `[0]` (the Surface sky texture) loads — a bad global-image-bank
   offset (D69/pccg family) would give `texSelect` a junk texture.
3. fast3d-side: trace whether the sky tris reach `gfx_sp_tri` at all and
   with what verts/CC. `skyRenderTri` is a 500-line subdivided-tri emitter —
   a PC vtx/DL bug there is plausible (cf. D75 model-transform family).
4. Cross-check another cloud-sky level (Dam `-level_33` exterior, Statue
   `-level_22`) — if they're also black it's the emit path; if only Surface,
   suspect that level's sky image / `skywaterimages[0]`.

`GE_D176=1` probe left in tree (`#ifdef PORT`, env-gated, inert):
`src/game/bgfog.c` + `src/game/sky.c`. Separate from D176(b) (the tree/rock
"grey static", Family A / a different investigation).

### D176(a) — M-37 UPDATE: ROOT-CAUSED (static analysis, high confidence)

The emit path is not "wrong" — **the PC software RSP deliberately discards it.**
`port/fast3d/gfx_pc.cpp:2901`:

```cpp
case (uint8_t)G_RDPHALF_1:
case (uint8_t)G_RDPHALF_2:
case (uint8_t)G_RDPHALF_CONT:
    // on N64 skyRender uses these to render some types of skies and skybox water
    // by issuing low-level ucode commands G_TRI_FILL and G_TRI_SHADE_TXTR
    // the port renders the sky in a different manner
    break;
```

`skyRenderTri` / `skyRenderFull` build their geometry **exclusively** as
`gImmp1(gdl++, G_RDPHALF_1 / G_RDPHALF_CONT / G_RDPHALF_2, …)` pairs — a bespoke
packed triangle-raster stream (command byte in bits 24–31 = `G_TRI_SHADE_TXTR`/
`G_TRI_FILL`, `0x00800000` backface flag, subpixel Y coords, then S15.16 edge X +
dX/dY slopes via `sub_GAME_7F094298`, then per-vertex RGBA shade + S/T/W tex-coord
gradients, `G_RDPHALF_2` terminator) that GE's modified RSP ucode (`gmain.s`)
interprets. There is **no** `gSPVertex`/`gSP*Triangle` fallback and **no**
fill-rect on the textured case-12 branch — only `viSetFillColor(env->RGB)` sets
FILL state that nothing consumes. → dropped → solid black.

Explains every prior observation: `!Clouds` levels use a plain `gDPFillRectangle`
(→ work today); `Clouds` levels (Surface, Statue `-level_22`, Frigate `-level_29`,
Dam exterior) go through the RDPHALF stream (→ black). The projector
`sub_GAME_7F097388` is pure float math off endian-clean matrices — not the bug.

**No PD-port shortcut:** `pd_port/port/fast3d/gfx_pc.cpp:2512` has the identical
no-op + comment and PD's `sky.c` emits the same stream; no port-side sky
replacement exists anywhere in the PD tree. The comment is aspirational in both.

**Fix (Phase 2, ~1 session, genuine new fast3d code):** decode the RDPHALF stream
in `gfx_pc.cpp` — accumulate words, on a `G_RDPHALF_1` carrying a tri opcode start
a primitive, synthesise screen-space `LoadedVertex[3]` (bypass model/proj
transform like the 2D `gSPTextureRectangle` path; screen XY = value/4 − viewport;
shade + ST from integrating the slopes over the vertex delta), rasterise with the
current combiner/tile, close on `G_RDPHALF_2`. Full stream spec + the alternative
`#ifdef PORT` sky.c option in the M-37 scratch note.

Cheap first step: headless `-level_22` / `-level_29` — confirm also black (proves
emit path, not a Surface asset).

### D176(a) — M-42: `-level_22` headless repro CONFIRMED

`-level_22` (Statue Park, night) at frame ~360, **bare boot, no input** — the
spawn/intro camera already faces the horizon: the entire upper half of the
frame is **pure black** `(0,0,0)` where N64 shows a moonlit sky; the park
pillars and ground render fine, HUD present. Confirms the defect is the shared
cloud-sky emit path (RDPHALF stream dropped), not a Surface-specific asset.
`-level_22` frame 360 is the cleanest headless verification target for the
RDPHALF-decoder fix — no `GE_INPUTSCRIPT` needed.

Note: `-level_36` / `-level_43` (Surface) bare-boot intro cameras point **down
at the terrain**, so a no-input capture there does not frame the sky. `-level_29`
(Streets, night) top-of-frame reads dim `(29,24,22)` not pure black — needs a
visual check to distinguish "dark night sky drawn" from "partial".

### D176(a) — M-43 UPDATE: full stream decode + two implementation paths (static analysis)

The M-37 scratch note (`D176a-sky-rootcause.md`) was never committed. This is the
recovered, complete spec, re-derived from `src/game/sky.c` and `include/PR/gbi.h`.
No build, no run — analysis only. **Implementation is still owed and is a full
session; this entry exists so the next pass starts from the format, not a
grep.**

**What the stream actually is.** `skyRenderTri` (`sky.c:1480`, textured tris),
`skyRenderFull` (`sky.c:1976`, textured quads = 2 tris) and the untextured
`G_TRI_FILL` path emit a **verbatim N64 RDP triangle command**, byte-for-byte,
chopped into 32-bit halves and carried as `gImmp1(G_RDPHALF_1 / _CONT / _2, word)`
pairs. GE's modified RSP ucode (`rsp/graphics/gmain.s`) does nothing clever with
them — it copies the words into a DMEM scratch buffer and DMAs the assembled
command straight to the RDP as a `G_TRI_FILL` (0xc8) or `G_TRI_SHADE_TXTR` (0xce)
edge-walked triangle. So "decode the stream" == "own an RDP triangle rasteriser
with edge + shade + S/T/W coefficient planes." fast3d has no such thing (it is an
RSP-level vertex/`gSP*Triangle` interpreter); `pd_port` has the identical no-op.

**Word-by-word layout** (all values are S15.16 or int/frac-split 16.16 unless
noted; `sub_GAME_7F094298(f)` = clamp ±32767.9 then `(s32)(f * 65536)`, i.e. a
float→S15.16 fixed convert):

1. *Edge header* — 2 words:
   - `w0 = (opcode<<24) | (backface ? 0x00800000 : 0) | (s32)YL`
     where opcode is `G_TRI_SHADE_TXTR` or `G_TRI_FILL`, backface = `sp444 < 0`
     (signed double area after the 3-way `unk2c` vertex sort), `YL = sp47c->unk2c`
     (lowest vertex, screen-Y × 4, subpixel).
   - `w1 = ((s32)YM << 16) | (s32)YL_mid` → actually `(s32)sp480->unk2c << 16 |
     (s32)sp484->unk2c` = `YM<<16 | YH` (mid, high). N64 order is YL/YM/YH; here
     the code sorts so `sp484 ≤ sp480 ≤ sp47c` on `unk2c`, then emits
     hi=sp484, mid=sp480, lo=sp47c.
2. *Edge slopes* — 3 word-pairs, `(X.16.16 , dXdy.16.16)` for the L, M, H edges:
   - `(7F094298(sp480->unk28 * 0.25), 7F094298(sp384))` — XL, DxLDy
   - `(7F094298(sp410),               7F094298(sp394))` — XH, DxHDy
   - `(7F094298(sp408),               7F094298(sp38c))` — XM, DxMDy
   `sp384/38c/394` are the edge inverse-slopes (`dx/dy`) clamped to ±1877;
   `sp408/sp410` are the H/M edge X starting values back-stepped to the YH
   scanline (`sp37c` = sub-pixel fraction of `sp484->unk2c * 0.25`).
   `*0.25` because `unk28/unk2c` are stored ×4.
   **`skyRenderTri` returns here if `!textured`** (G_TRI_FILL: header + edges
   only).
3. *Shade coefficients* (textured only) — 8 word-pairs = the RDP shade DMEM
   block: RGBA base, DrDx/DgDx/DbDx/DaDx, DrDe.., DrDy.. each as an int-parts
   word then a frac-parts word (`(v & 0xffff0000) | (next & 0xffff0000) >> 16`
   packs two ints; the matching `<<16 | (next & 0xffff)` packs the fracs). Values:
   `sp210[0..3]` = colour at origin, `sp290[0..3]` = d/dx, `sp2b0[0..3]` = d/dy,
   `sp230[0..3]` = d/de, all run through `7F094298`. Colours are the per-vertex
   `SkyRelated38.rgba` barycentrically solved to a plane via `sp440`
   (= 1/doubled-area) and the `sp3c8..sp3d4` edge deltas.
4. *Texture coefficients* (textured only) — 8 word-pairs, same int/frac packing,
   for S, T, W: `sp210[4..6]`, `sp290[4..6]`, `sp2b0[4..6]`, `sp230[4..6]`, each
   scaled by `sp190` (an LOD/overflow clamp: `1/max(perspective-corrected
   gradient magnitude / 1024, 1)`). The final pair is emitted with
   `G_RDPHALF_2` (not `_CONT`) — **that is the stream terminator.**

Total data words: **2 + 6 + 16 + 16 = 40** for `G_TRI_SHADE_TXTR` (20 RDPHALF
pairs), **2 + 6 = 8** for `G_TRI_FILL`. The 2-word "header" is exactly the raw
N64 RDP triangle command's word0 (`[cmd 8b][backface 1b @23]...[YL 14b]`) +
word1 (`[YM 14b][YH 14b]`) — matches the canonical hardware format; the shade
and texture blocks are the standard 16-word int-halves-then-frac-halves RDP
coefficient layout (cross-checked M-43 against `include/PR/gbi.h` immediate-word
packing conventions — the low-level RDP tri macros themselves are RSP-internal
and not in this tree's `gbi.h`).

`skyRenderFull` is the same but solves the plane over 4 corner verts and emits
the quad as two RDP tris sharing the coefficient blocks; its own header pair also
ends the first tri and a second header starts the second.

**Vertex data available upstream** (before the DL): `sub_GAME_7F097388`
(`sky.c:1398`) fully projects each sky corner and writes `SkyRelated38`:
`unk28` = screen X ×4, `unk2c` = screen Y ×4 (minus `WaterConcavity*4`),
`unk30` = screen Z (0..0x7fff), `unk34` = 1/w, `unk20/unk24` = S/T, `rgba` =
per-vertex colour. **This is a complete screen-space vertex.** The projection is
pure float math off endian-clean matrices — not the bug, and reusable as-is.

---

**Path A — RDP triangle rasteriser in `port/fast3d/gfx_pc.cpp` (port-only, "correct").**
At `case G_RDPHALF_*` (`gfx_pc.cpp:~2901`): accumulate words into a small buffer;
when a `G_RDPHALF_1` carries a `0xc8`/`0xce` opcode start a command, close on
`G_RDPHALF_2`. Then either (a) feed a real edge-walk rasteriser writing into the
current render target with the active combiner/tile, or (b) **invert the plane
equations** — you have YH/YM/YL, the three edge X-at-Y and slopes, so recover the
3 screen (x,y); evaluate the RGBA and STW planes at those 3 points to get 3
`LoadedVertex` (already-projected: set `.x/.y` from screen coords mapped to
NDC-ish like the `gDPTextureRectangle` path, `.z` from a fixed sky depth, `.w`
from `unk34`), then hand to the existing `gfx_sp_tri` / GL path with a forced
"2D, no model matrix" flag. (b) is a few hundred lines and reuses the whole
existing combiner/texture pipeline; (a) means a new software rasteriser. Prefer
(b). Cost: ~1 session, genuinely new code, verifiable headless.

**Path B — swap the emit in `src/game/sky.c` behind `#ifdef PORT` ("cheap").**
`skyRenderTri` / `skyRenderFull` already hold 3–4 fully-projected screen-space
`SkyRelated38` verts. Under `#ifdef PORT`, skip the whole RDPHALF block and emit
a normal `gSPVertex` of screen-space `Vtx` (XYZ from `unk28/2c/30` ÷4, ST from
`unk20/24`, RGBA from the struct) + `gSP1Triangle` / `gSP2Triangles`, with a
`G_TEXTURE`/combiner setup mirroring what the RDP path implied
(`SHADE,ENV,TEXEL0,ENV`, `texSelect(&skywaterimages[SkyImageId], …)` already runs
just before). fast3d already eats screen-space verts (that is the front-end / HUD
path). ~30–60 lines, no new fast3d code. **Cost: a few hours.** Tension with the
"no game-logic edits" rule — but this is a pure N64-RSP-idiom substitution (same
class as the `G_TRI4` and dynamic-lighting `#ifdef PORT`s already in `src/`), it
changes no behaviour on N64 (`#else` keeps the stream verbatim), and it is
exactly the "if a game file seems to need a behavioural change, the fix belongs
in `port/`" *narrow* exception for a hardware idiom that cannot be isolated in
`port/` without reimplementing the RDP. Document as D176(a) in §F if taken.

**Recommendation:** Path B first — it lights up every cloud-sky level
(Surface `-level_36`, Statue `-level_22`, Frigate `-level_29`, Dam exterior) for
a few hours' work and de-risks Path A by giving a visual ground truth. Path A
stays the "right" long-term answer if Path B's screen-space verts show
seams/precision artefacts vs. the RDP's subpixel edge walk. Either way the
verification target is **`-level_22` (Statue, night) frame ~360, no input** — the
M-42 clean headless black-sky repro.

**Cheap first step (unchanged, still not done):** headless `-level_22` /
`-level_29` — confirm also black, proving it is the shared emit path and not a
Surface-only asset.

### D176(a) — M-46: Path B implemented (`src/game/sky.c`, `#ifdef PORT`)

Path B is in. `src/game/sky.c` only; N64 path kept byte-for-byte under `#else`.

**What changed.** A new file-scope `#ifdef PORT` helper `skyPortRenderPoly(gdl,
SkyRelated38 **v, nverts)` and an early `#ifdef PORT` return in both
`skyRenderTri` (3 verts, `arg1..arg3`) and `skyRenderFull` (4 verts,
`arg1..arg4`), placed right after the `sp444/sp488 == 0` degenerate-area guard
and `1.0f / area` so all shared early-outs still run. The helper:

- `dynAllocateVertices(n)` + two `dynAllocateMatrix()` from the vtx pool
  (same lifetime idiom as the bondview2 watch-gauge ortho overlay and
  explosion.c billboards).
- `guOrtho(proj, l, l+w, t+h, t, -32768, 32768, 1)` where `l/t/w/h =
  getPlayer_c_screen{left,top,width,height}()` — maps the already-projected
  pixel coords 1:1 into clip space, Y flipped (screen-down → NDC-up); identity
  modelview. `gSPMatrix` LOAD PROJECTION then MODELVIEW. `bg.c` reloads the
  perspective projection at the top of `bgLevelRender` (lines 638/689/…), so
  leaving ortho loaded after `skyRender` is safe.
- Per vertex: `ob = {unk28, unk2c, unk30} * 0.25` (the `*4` subpixel scale the
  RDP path used); `tc = {unk20, unk24} * 32` (standard N64 S10.5); `cn = rgba`
  from the struct.
- `gSPClearGeometryMode(G_LIGHTING|G_CULL_BOTH|G_FOG)` +
  `gSPSetGeometryMode(G_SHADE|G_SHADING_SMOOTH)`, then `gSPVertex` +
  `gSP1Triangle(0,1,2)` (tri) / `gSP2Triangles(0,1,3, 0,3,2)` (quad, winding
  irrelevant with culling off). Combiner (`SHADE,ENV,TEXEL0,ENV`), env colour
  and `texSelect(&skywaterimages[SkyImageId/WaterImageId], 1,0,2)` are already
  emitted by the caller immediately before — the helper does not touch them.

**Verified.** `src/game/sky.c` compiles clean under the real PC build flags
(`gcc.exe … -DPORT=1 -std=gnu11 -Wall`, exit 0, no new warnings) with the
worktree's `build-pc/` CMake configuration. Full link + run NOT done: the
worktree has no `data/` ROM so assets can't be extracted here. Every symbol the
helper uses (`dynAllocate*`, `guOrtho`, `guMtxIdent`, `gSPVertex`,
`gSP{1,2}Triangle{,s}`, `osVirtualToPhysical`) is already linked by the compiled
set, so link is expected clean.

**Owed to a human (cannot headless-verify):**
1. Eyeball `-level_22` (Statue, night) frame ~360, bare boot no input — the
   M-42 black-sky repro. Expect a rendered moonlit sky in the upper half.
   Also spot-check `-level_36` (Surface), `-level_29` (Streets).
2. **Untested risk — texture scale.** `tc = unk20/unk24 * 32` is the
   conventional guess; `unk20/unk24` come out of `sub_GAME_7F097388` as
   `unk0c * (65535/65536)` with `unk0c ≈ worldX*0.1` for clouds. If the sky
   texture is tiled wrong / smeared, the fix is localised to the two `tc[]`
   lines in `skyPortRenderPoly` (multiplier, or an explicit `gSPTexture`
   scaling override) — no other code moves.
3. **Untested risk — ortho Z / depth.** `ob[2] = unk30*0.25` in `[0,8192]`
   under an `[-32768,32768]` ortho; sky renders first into a cleared buffer so
   depth ordering should not matter, but if the sky Z-fights or is culled,
   drop `ob[2]` to 0.
4. Seam/precision check vs the N64 RDP subpixel edge-walk (the Path A
   motivation) — only relevant once it renders at all.

**Confidence:** MODERATE. The emit-path substitution is sound and matches the
established screen-space-overlay idiom; compile is clean. The two numeric
unknowns (texture scale, and whether fast3d's viewport transform composes with
this ortho exactly as the projection math intends) need one visual check.

### D176(a) — M-82: PR #18 rebased; Defect 1 (texcoord) fixed by static analysis

- **PR #18 rebased onto `main`** (was 5 days stale, `docs/porting-notes.md`
  conflict resolved). Builds clean; `bunker1` verify PASS, `worst_cell`
  unchanged (sky.c change is `#ifdef PORT` and only touches cloud-sky levels).
- **Defect 1 root-caused, no visual iteration needed.** The M-46 "untested
  risk — texture scale" is resolved:
  - Cloud tile = `s_skywaterimages[0]` = `IMAGE_CLOUDS_GRAYSCALE`, **64×64
    IA8, `G_TX_WRAP`** (`assets/oddtextures.c:646`). It *tiles* — S/T are not
    [0,1) fractions.
  - GE's own screen-space textured-quad idiom is `tc = width << 5`
    (`src/game/glass2.c:678`), so the patch's `× 32.0f` **is the right scale**.
  - `unk20`/`unk24` are texel coords reaching ±20 000 near the horizon (M-47),
    so `(s16)(S × 32)` **overflows** → the streak.
  - **Fix (committed):** per-primitive phase-fold — subtract the same
    multiple of the 64-texel tile period from every vertex of one triangle
    before scaling. Preserves inter-vertex deltas (hence the interpolated
    texture); keeps `tc` in s16 range. Exact for any triangle whose S/T span
    is < ~1000 texels — i.e. everything except triangles straddling the
    horizon line, which remain Defect 2.
- **Defect 2 (coverage / horizon-straddling tris) still open** → adaptive
  tessellation, denser toward the horizon.
- **Owed:** one `-level_22` visual check — see `docs/dev/D176a-SKY-NOTES.md`
  "Verification ask".

### D176(a) — fidelity pass: "scrolls too fast / tiles too visibly" ROOT-CAUSED + FIXED (linear vs. perspective-correct tc interpolation)

M-84/M-87 playtest found Path B partially working: where the sky renders it
"scrolls too fast and repeats too visibly" (tiling period reads too short).
Root-caused with a `GE_D176SCROLL` probe (temporarily added to
`skyPortRenderPoly`, removed after use — same throwaway-probe pattern as
`GE_D176`):

- **`skyPortRenderPoly` placed every sky vertex under a plain pixel-space
  `guOrtho` projection**, i.e. every vertex got clip `w = 1`. fast3d's normal
  GPU pipeline then interpolates `tc` (and every other varying) *linearly in
  screen space* across the primitive — correct only when every vertex shares
  the same `w`.
- **They don't.** `unk20`/`unk24` (S/T, texel units) are computed upstream
  (`sub_GAME_7F097388`, `sky.c:1401`) from the *world-space* cloud-plane
  intersection, with **no perspective divide applied** — exactly like the
  real RDP's `G_TRI_SHADE_TXTR` coefficient block, which carries `S·w′`/`T·w′`/`w′`
  triples (`docs/dev/D176a-SKY-NOTES.md` wire-format section) and relies on
  the RDP's own perspective-correct texture unit to divide back out per
  pixel. `SkyRelated38.unk0c` (a field Path B wasn't reading before this fix)
  already carries that raw camera-space `w` (`sky.c:1450`,
  `arg5->unk0c = sp68[3]` — the same `w` sub_GAME_7F097388 itself divides by
  to get screen X/Y, so it's directly reusable).
- **Measured on `-level_22` (Statue), frame ~330:** one sky quad's four
  corners had camera-space `w` = 5469/5469 (top edge, near) vs.
  207300/207257 (bottom edge, at the horizon) — a **38x ratio** — over an S
  span of ~30000 texels. Linear interpolation smears that whole 30000-texel
  delta evenly across every screen pixel of the quad; perspective-correct
  interpolation concentrates it almost entirely into the last few pixels
  next to the horizon-grazing vertices (hand-computed check: naive linear
  interpolation at the screen-space midpoint gives S≈-9090, true
  perspective-correct gives S≈-884 — an order of magnitude apart). The
  linear version reads exactly as reported: a uniformly dense, aliased
  "hatching" over the *entire* visible sky instead of a gentle gradient that
  only gets busy right at the horizon.
- **Fix (`src/game/sky.c`, `skyPortRenderPoly`, `#ifdef PORT`):** replaced
  the `guOrtho` load with a hand-built projection matrix that gives each
  vertex its *real* camera-space `w` (from `unk0c`) instead of `w=1`, while
  keeping every vertex's screen-space `x,y` position bit-identical (the
  matrix is constructed so `clip.xy = ndc.xy * w` and `clip.w = w`, so the
  GPU's perspective divide recovers the same `ndc.xy` regardless of `w` —
  only the *interpolation* across the primitive changes). `Vtx.ob[]` is
  `s16` and can't hold raw `w` (up to ~2×10^5 near the horizon), so each
  vertex pre-divides by a per-primitive `wScale` (`max|w| / 30000` when that
  exceeds 30000, else 1) and the projection matrix multiplies back by
  `wScale` for `clip.x/y/w`; `ob[2]` (unused for depth — the sky draws first
  into a cleared buffer, M-46) carries `w/wScale` through to the matrix's
  w-column. This rides the exact same fast3d vertex/triangle/GLSL pipeline
  every other textured draw in the game already depends on for correct
  perspective (`gl_Position = aVtxPos;`, `port/fast3d/gfx_opengl.cpp:320` —
  default GLSL `varying` interpolation is perspective-correct whenever
  `w != 1`); no `port/fast3d` changes.
- **Verified:**
  - `tools_pc/verify.sh bunker1` — PASS, `frames=3`, no crash (non-sky level,
    confirms no regression).
  - `-level_22` (Statue) and `-level_41` (Cradle) headless `GE_PCDUMP`
    captures, before/after, eyeballed. The intro camera pan isn't frame-exact
    between runs (small wall-clock-driven timing drift in the pre-gameplay
    camera — a separate, out-of-scope-for-this-task timing question, not the
    D193/D209 gameplay-tick class which is already fixed), so an exact
    per-pixel diff isn't reliable, but the *qualitative* pattern change is
    unambiguous and reproduces every run: before, uniform parallel aliased
    streaks over the whole sky; after, a proper perspective-converging fan —
    coarse near the top (near vertices), converging to fine detail only near
    the horizon edge (far vertices) — the textbook look of a textured plane
    correctly extending to a horizon. Cradle (`-level_41`) sky at default FOV
    renders a normal blue sky with cloud streaks, not black.
  - Not verified: an actual side-by-side against real N64 footage (none
    available in this environment), and whether the residual "busy" look at
    the very top of Cradle's sky (still fairly dense) is acceptable or needs
    Defect 2's tessellation on top — see below.
- **Interaction with Defect 2 (coverage / horizon-straddling tris, still
  open):** this fix corrects the interpolation *mode* (linear → perspective)
  but does not add tessellation. A primitive whose w-ratio between vertices
  is this extreme will still show real (now correctly-shaped, but still
  dense) detail concentrated at its horizon edge — finer subdivision near
  the horizon (Defect 2) is what would spread that out further and is still
  the right follow-up for full visual quality. Defect 2 itself (vertical
  coverage / horizon-straddling triangles needing adaptive tessellation,
  and the separate FOV-coupled black-sky-past-frustum-edge symptom on Cradle
  at high FovScale, D218/D222-adjacent) is **unchanged/still open** — not
  attempted this pass.
- **Confidence:** HIGH on root cause (confirmed numerically: measured 38x
  `w` ratio + hand-computed order-of-magnitude interpolation error) and on
  the fix being a real, correct improvement (reproducible qualitative
  before/after pattern change, matches the mathematically expected
  perspective-converging shape). MODERATE on "fully resolves the reported
  symptom to N64 parity" — no N64 reference capture to diff against in this
  environment; a human should eyeball `-level_22` and `-level_41` and say
  whether the remaining density right at the horizon reads as acceptable or
  still warrants Defect 2's tessellation work.
- **Porting-notes quirk added:** `docs/porting-notes.md` §D — screen-space
  "already-projected" GBI overlays (HUD/front-end idiom) are only safe to
  place under an identity/orthographic (`w=1`) transform when every vertex
  in the primitive really does share one depth/scale; a primitive whose
  per-vertex data was computed via a *world-space* perspective projection
  upstream (sky, and potentially other N64 RDP-immediate idioms) needs its
  real `w` preserved through to the GPU, or every non-positional varying
  (texture coords, vertex colour) interpolates wrong across the primitive.

---

## D176(a) — M-93: human playtest of PR #43 (debug build) — Cradle default-FOV black sky CONFIRMED FIXED; Statue scroll speed improved but still off; NEW residual split out as D227 (sky renders as two independently-moving halves)

**Method:** user's own debug build (`build-linux-dbg`), branch
`land/d176a-sky-perspective-scroll` checked out. Sampled Statue
(`-level_22`) and Cradle (`-level_41`), plus a Bunker1 sanity pass.

**Confirmed fixed:** Cradle's sky at default FOV is no longer black — matches
the PR's claim exactly.

**Confirmed improved, not fully fixed:** Statue's cloud scaling reads
noticeably better than before (the PR's perspective-correct-`tc`
interpolation fix is doing real work), but the user's read is it's **still
too fast**. Not yet re-measured numerically post-verdict; could be residual
under-correction in the fix itself, or could be downstream of the D227
split-sky issue below making the whole thing harder to judge cleanly.

**Confirmed still-open, as expected:** at **max FOV**, Cradle's black-skybox
artifact reappears. This matches the PR body's own disclaimer verbatim
("the separate FOV-coupled black-sky-past-frustum-edge symptom (D218/D222
-family) ... unchanged/still open — not attempted here") — not a surprise,
not a regression, just confirmation the known gap is real and reproducible.

**New, not previously documented:** the sky visibly renders as **two
independently-moving halves** on both Statue and Cradle — user's words,
"like there's two different sky processes running at once roughly taking
half the sky each," each half scrolling differently. Split out as its own
row, **D227**, rather than folded into D176(a), since it's a distinct visual
signature (a seam + differential motion between two regions) rather than a
scroll-rate or color/black issue. Not established whether this pre-dates
PR #43 (the fix could have just made it easier to notice once the gross
scroll-speed error calmed down) or whether it's a widescreen/aspect-ratio
rendering artifact rather than a logic bug — user explicitly flagged the
latter as plausible and unconfirmed pending a side-by-side against real
N64 output at the same camera angle.

**Sanity pass:** Bunker1 (non-sky level) — clean, no regression, matches
the PR's own headless `verify.sh bunker1` PASS claim.

**Disposition:** PR #43's fix is real and should still merge — it closes
the confirmed black-sky sub-symptom on Cradle and measurably improves (if
not fully fixes) the Statue scroll-rate symptom, with no regression
elsewhere. D176(a) stays open for the residual scroll-speed-still-too-fast
report and defect 2 (horizon-band tessellation, pre-existing, unchanged).
D227 is a new, separate open item pending investigation.

## D176(a) — M-98: Cradle "tower starburst" — concrete screenshot of the already-known, still-open Defect 2 (horizon-band tessellation)

**Context:** checking the Cradle side of PR #43 (never done in M-97, per its
own "not done this session" note) for D227 regressions from the fold-sharing
fix below. It wasn't regressed, but turned up a much more visually dramatic
pre-existing artifact than anything previously screenshotted for this level.

**What it looks like:** near Cradle's spawn, looking up the suspension
tower/catwalk, roughly the left half of the sky renders as a sharp
"starburst" of radiating streaks converging toward a point near the top of
frame, directly adjacent to normal horizontal cloud streaks on the right
half. Screenshot: `docs/img/bugs/d176a-cradle-tower-starburst.png`
(`-level_41` frame 350).

**Confirmed pre-existing, not caused by this session's work:** rebuilt with
`src/game/sky.c` reverted to its pre-M-98 (and pre-M-95, since the revert
target predates both) state, same camera/frame — pixel-identical to the
post-fix capture. Also confirmed **static**, not the D227 "two halves move
at different rates" symptom: frame 350 and frame 380 (30 ticks apart, same
static camera) are pixel-identical.

**This is Defect 2, not a new bug.** The D176(a) finding row has carried
"Defect 2 (horizon-band coverage/adaptive tessellation) ... unchanged, still
open" since M-90, and `skyPortRenderPoly`'s own code comment already
predicts exactly this shape of failure: "Triangles whose S/T span exceeds
~1000 texels (only those straddling the horizon) still need Defect-2's
finer tessellation." Looking almost straight up the tower is precisely the
camera angle that produces one sky fan triangle spanning from near-camera
to horizon-grazing texel coordinates — the >1000-texel-span case the fold
logic already flags as unhandled. The perspective-correct interpolation
(M-90) gets the *shape* of the gradient right end-to-end, but with only 1-3
triangles per fan and no subdivision, a single triangle covering that much
of the texture's S/T range still shows visible linear-interpolation
artifacts across itself — reading as this radiating streak pattern rather
than smooth cloud, because the fold-and-interpolate math treats the whole
huge span as one linear ramp.

**Not fixed this pass** — tessellation (subdividing wide-span sky triangles
before the fold/perspective step) is a materially bigger change than the
fold-sharing fix below, and out of scope for "did Cradle regress." Logged
here with a concrete repro + screenshot so Defect 2 has hard evidence
instead of the prior "M-37: inconclusive, leaning tiling-density" note.
