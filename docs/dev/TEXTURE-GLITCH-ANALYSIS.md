# Texture Glitch Analysis — PC Port

**Original diagnosis:** 2026-08-29 (read-only) · **Status table refreshed:** 2026-08-31 (M-31)
Companion artifacts: `<local-path>` (outside repo).

---

## 0. Current status (M-31) — READ THIS FIRST

> **M-33 overlay (D168):** every `GE_PCDUMP` / F12 screenshot this doc's
> analysis relied on was **vertically flipped** — the PPM writer never reversed
> `glReadPixels` rows. This is what the D114/D116 "HUD/text X-mirror" finding
> actually was (now closed, not a bug). It also contaminates the **RC1** row
> below: the wallet-Bond photo's "180°-rotated / mis-transformed" description
> was made from an upside-down capture, so at least the vertical component is
> the capture bug, not the model transform. Re-judge RC1 from a fresh
> right-way-up capture before doing any more work on it. Writer fixed
> (`port/fast3d/gfx_opengl.cpp`).

The 2026-08-29 body below is the original diagnosis. Several root causes have
since been fixed or retracted. Authoritative current state:

| RC / bug | 2026-08-29 verdict | Current status (M-31) | Where |
|---|---|---|---|
| **RC2** — mip-chain contamination (LOD rows uploaded as image rows) | open | **FIXED** — `import_texture()` clips a full-width LOD block to the SETTILESIZE base height; GL builds correct mips. Knob `Video.FixMipTextures` (**default on**; `=0` = old byte-identical-to-golden behaviour). | `port/fast3d/gfx_pc.cpp`; §F RC2 |
| **RC4** — palette "off-by-one" in `palette_to_rgba32` | proposed 1-line fix | **RETRACTED** — the current decode is correct **RGBA5551**; the "spec" line in §2 was ARGB1555. Applying the "fix" rotates every channel + moves alpha. **Do not touch.** | `gfx_pc.cpp:~835`; §2 RC4, §F |
| **D159** ("interlaced" / venetian-blind textures, esp. front-end photos) | not yet identified | **FIXED** — `texSwapAltRowBytes` (N64 RDP odd-line TMEM XOR compensation, which fast3d does not emulate) is a `#ifdef PORT` no-op. This was the user's "interlaced textures" report. | `src/game/image.c`; §F D159 |
| **D161** — Depot (`-level_30`) ceiling blue speckle + radial rays (was §3 "blue ground" / B2) | attributed to RC2+RC3+RC4+fog | **FIXED** — a 16×16 **CI8 tile drawn with `G_TT_NONE`** (TLUT disabled) was palette-looked-up against a stale `rdp.palette`. fast3d now routes CI+`G_TT_NONE` → I (intensity). Not RC2/RC3/RC4/filtering. | `port/fast3d/gfx_pc.cpp import_texture()`; §F D161, porting-notes.md §D |
| **RC1** — wallet-Bond photo (mode-select / folder screen) garble | "implement G_SETTEX" | **PARTIALLY OPEN.** The *interlace comb* is gone (D159). The photo now renders as a recognisable grayscale portrait but is still **180°-rotated / mis-transformed** → **D75 front-end-model family** (model-GDL transform, not a fast3d opcode gap — `texLoadFromGdl` already expands G_SETTEX on PC, see §6b). Parked, cosmetic, below crash work. | §6b; GRAPHICS-BACKLOG D149; §F D75/D80/D82/D83 |
| **RC3** — wrap-period mismatch (non-PoT dims "repeat oddly") | open | **IN PROGRESS.** The dead D74 pre-wrap block is now opt-in as `Video.WrapFix` (M-29, **not default** — the naive one-liner activates never-run code and boot-crashes `-level_09`; needs the hoist-out-of-vertex-loop rework + per-level visual check). | `gfx_pc.cpp`; GRAPHICS-BACKLOG B1; §F D74 |
| RC1 (original) — G_SETTEX (0xc0) is `G_NOOP` in fast3d | "implement the command" | **NOT the blocker.** `src/game/tex.c texLoadFromGdl` already expands every G_SETTEX into a standard load sequence under `#ifdef PORT`; fast3d normally never sees raw 0xc0. See §6b. | §6b |

Everything from `## 1.` down is the original 2026-08-29 text, left intact for
the reasoning trail. Cross-check any "open / pending / proposed" wording there
against this table.

## 1. Reported symptoms

1. **In general:** textures repeat oddly with weird artifacting.
2. **Depot (`-level_30`):** loads "completely wrong" textures — the ground is drawn a
   glitched **blue** that is not right (visible in attract mode).
3. **Character models:** obvious texture artifacts during the credits roll.

## 2. Root causes (confirmed by code + ROM evidence)

> **M-31 status overlay** (see §0): RC2 **FIXED** (`Video.FixMipTextures`), RC4 **RETRACTED**,
> RC1 superseded — the "interlaced" symptom was **D159** (`texSwapAltRowBytes`), now **FIXED**;
> the Depot "blue ground" was **D161** (CI8 + `G_TT_NONE`), now **FIXED**. RC3 in progress
> (`Video.WrapFix`, opt-in). RC1 residual = 180°-rotated front-end photo = D75 family, open.

| # | Cause | Location | Explains | Fix class |
|---|-------|----------|----------|-----------|
| RC1 | `G_SETTEX` (0xc0) is a **no-op** in fast3d | `port/fast3d/gfx_pc.cpp:2443` (`case G_NOOP`) | #3 character/model artifacts | ~~implement the command~~ — **not the blocker**: `texLoadFromGdl` already expands it on PC (§6b); residual = D75 model transform |
| RC2 | **Mip-chain contamination**: base + all LODs uploaded as one GL image | `src/game/tex.c:330` (`texGetDepthAndSize` sums all LODs) → `gfx_dp_load_block`/`import_texture_*` | #1, #2 noise bands, wrong scale, minification garbage | **FIXED** — clip LOD block to base-tile height; `Video.FixMipTextures` default on |
| RC3 | **Wrong wrap period**: N64 wraps at `2^ceil(log2(dim))`, GL wraps at uploaded image size; D74 pre-wrap block is dead (`G_TX_WRAP == 0`, `include/PR/gbi.h:391`) | `port/fast3d/gfx_pc.cpp` UV math in `gfx_sp_tri1` + D74 guard | #1 "repeats oddly", squash/stretch on non-PoT dims | **DONE (D167), behind `Video.WrapFix` default OFF** — mask-based wrap period in the hoisted pre-wrap block; needs a human eyeball on Depot before default-on. See §6d |
| RC4 | ~~**Palette off-by-one** in `palette_to_rgba32`~~ — **RETRACTED (M-30)**: current decode is correct RGBA5551 | `port/fast3d/gfx_pc.cpp:835` | nothing — the "spec" line was ARGB1555 | **do not change** |

### RC1 — G_SETTEX no-op (character artifacts)

- GE's `gbi_extension.h` defines opcode 0xc0 as `G_SETTEX` (`gsSPUseTexture`):
  `w0[3:0]=type, [9:6]=shiftt, [13:10]=shifts, [17:16]=tile, [19:18]=cmt, [21:20]=cms`;
  `w1 = minlevel<<24 | detail_id<<12 | texture_id`.
- **Full ROM scan: 508 of 512 model files contain G_SETTEX — 43,314 commands total**
  (top emitters: `PwalletbondZ` 2830, `CbaronsamediZ` 976, `GautoshotZ` 975).
- Worked example `CheadbrosnanZ`: 47 G_SETTEX commands; texture_ids (1624–1626 = 32×64,
  1607 = 64×32, 1608 = 1×1 null) exactly match the model's `Textures[]` table; the GDL
  interleaves them per mesh part. All use tile=2 (`G_TX_RENDERTILE`).
- On N64, gmain.s dispatches 0xc0 via the jump table at offset 0xbc (top-2-bits bucket 3);
  it rebinds the render tile to the texture identified by `texture_id`.
- Our fast3d hits `case G_NOOP: break;` — **every mesh part renders with stale tile state**
  → wrong/shifted textures on characters. This is why the credits roll (many models, many
  texture switches) shows obvious artifacts.
- The in-code comment at `gfx_pc.cpp:2443` ("the game never emits it") is **wrong for GE** —
  presumably inherited from PD assumptions. The PD port also no-ops 0xc0, so there is no
  reference implementation to copy; implement from gmain.s + gbi_extension.h.

### RC2 — Mip-chain contamination (floor/room noise)

- `texGetDepthAndSize()` (`src/game/tex.c:330`) sums **all** LOD levels, so one
  `gDPLoadBlock` carries base + full mip chain. fast3d uploads that as a single GL image:
  a 65×65 floor texture becomes ~65×134, rows below the base being raw mip bytes.
- GL then wraps at the *uploaded* size and `glGenerateMipmap` + the never-disabled
  `GL_LINEAR_MIPMAP_LINEAR` min-filter (`gfx_opengl.cpp:59`) sample **mips of a
  garbage-stacked image**. Distant surfaces (attract-mode camera!) minify into noise.
- ~75% of Depot's textures carry full mip chains (lod 6–7), so this is maximal there.
- N64 per-LOD tile selection is already emulated by the D107 rule (always base tile,
  offset 0) — GL mips are pure poison in this port; disable them.

### RC3 — Wrap period mismatch ("repeats oddly")

- N64 wraps at the tile mask period `2^ceil(log2(dim))` (`texDimensionToMask`,
  `src/game/tex.c:361`). GL `GL_REPEAT` wraps at actual image size.
- Depot's large room surfaces are **non-power-of-two**: 65×65, 56×56, 96×48, 128×32
  (I4/IA4, non-zlib). E.g. 65 wide: N64 period 128 vs GL period 65 → pattern squashed 2×
  horizontally; with RC2 the vertical period is ~2× too long and half of it is noise.
- The D74 pre-wrap UV block never runs: guard tests `cms & G_TX_WRAP` but
  `G_TX_WRAP == 0` (`include/PR/gbi.h:391`) — dead code (already parked in
  `docs/dev/GRAPHICS-BACKLOG.md`).

### RC4 — Palette off-by-one — **RETRACTED (M-30): the current code is correct.**

The "N64 RGBA16 spec" line below is **ARGB1555**, which is *not* what the N64 uses.
N64 `G_IM_FMT_RGBA` / `G_IM_SIZ_16b` is **RGBA5551**: `RRRRR GGGGG BBBBB A`
(R = bits 15-11, G = 10-6, B = 5-1, A = bit 0).

```c
// fast3d (current, CORRECT): r = v>>11; g = (v>>6)&0x1f; b = (v>>1)&0x1f; a = v&1
// the "spec" line here was wrong: r = (v>>10)&0x1f; g = (v>>5)&0x1f; b = v&0x1f; a = v>>15
```

Both `palette_to_rgba32` (`gfx_pc.cpp:~835`) and `import_texture_rgba16`
(`~651`) use the same RGBA5551 decode and agree with each other. The palette is
`PD_BE16`-swapped at load (`gfx_dp_load_tlut`), so `palentry` is already
correctly ordered. **Applying the "fix" would rotate every channel and move
alpha to the wrong bit — do not do it.** If a hue problem is ever confirmed
in-game, it is upstream (TLUT byte order at the converter, or a wrong
`rdp.palette_fmt`), not this function.

## 3. Depot-specific analysis ("why blue ground, only there?")

**Depot is not special — it is the worst case of RC2+RC3+RC4.**

- **Ruled out with data:**
  - *Texpool exhaustion / D85 placeholder path:* a real Depot load (env-gated log) shows
    342 unique texture loads, ~326 KB compressed into the 450 KB stage pool, **zero**
    `D85TEX pool-full` events. The floor is not sampling a garbage pointer.
  - *Broken decompression:* `texInflateZlib`/`texInflateNonZlib` are faithful decompilations;
    the format was independently re-derived offline (§4) and decodes byte-exact.
- **What Depot's floor actually is** (decoded from ROM): non-zlib **I4/IA4 grayscale with
  full 7-level mip chains and non-PoT dimensions** (texnums incl. 85/86, 508, 618, 631,
  779–782, 836, 909–911, 989–996). Exactly the class RC2+RC3 mangle hardest.
- **The blue:** nothing in Depot's texture set is actually blue (decoded color stats are all
  industrial grays/browns, e.g. avg rgb 107/107/108, 147/117/112). The perceived blue =
  garbage I4 values sampled under minification (attract camera is far → mip-filtered noise)
  + Depot's fog table (`src/game/bgfog.c`, LEVELID_DEPOT entry) tinting distant surfaces
  cold. RC4 then shifts whatever hue survives.
- BUNKER1 "renders recognisably" because its surfaces hit the same bugs less severely
  (PoT dims / fewer visible garbage rows at typical camera distances).

## 4. Texture pipeline reference (learned during this investigation)

Useful ground truth for whoever implements the fixes:

- **ROM layout:** `g_Textures[N]` = segment offset; texture N occupies
  `[g_Textures[N], g_Textures[N+1])` in the images segment (`_imagesSegmentRomStart`,
  ROM file offset 0x8F7DF0 in the NTSC dump). `assets/images.def` cumulative sizes match the
  logged runtime offsets **exactly (0/342 mismatch)** — verified against the D66 log.
- `texLoad` (`src/game/image.c:~2450`) copies from `(thisoffset & ~15)` and re-points
  `compptr += (thisoffset & 7)`; empirically every blob decodes at the listed offset (+0).
- **Blob header byte:** `u(1) z(1) lod(6)`. `z=1` → zlib path, else per-image
  format(4)/w(8)/h(8)/comp(4) + Huffman/RLE/lookup payloads.
- **Zlib path** (`texInflateZlib`, `image.c:169`): MSB-first bitstream —
  format(8), numcolours(8)+1, palette nc×16; per image: width(8), height(8), then a
  **rarezip stream = 2-byte header `0x11 0x72` + raw deflate** (GE's `src/game/zlib.c` is
  stock gzip-1.2.4 inflate). Output rows are stored **tight**; `texAlignIndices`
  (`image.c:340`) repacks to 8-byte-aligned rows (CI8: 1 B/px; CI4: 1 B/2 px, high nibble
  first).
- **`texSwapAltRowBytes`** (`image.c:2191`) is applied to **every level of every texture on
  every code path** (calls at 260, 300, 312, 316): odd rows get adjacent u32 groups pairwise
  swapped. The post-swap RAM buffer is what gets uploaded — any offline tool or dump must
  account for this.
- Palette is appended to the buffer big-endian after all images; `G_LOADTLUT` uploads it.
- `u=0` + lod≥2: mips are generated at load by `texShrinkPaletted` (2×2 palette average).

## 5. Open questions / hypotheses

- **H-A (high confidence):** Depot blue ground = RC2+RC3+RC4+fog, per §3. Confirm in-game
  after fixes; the before/after capture is the test.
- **H-B (high confidence):** credits-roll model artifacts = RC1. Confirm by implementing
  G_SETTEX and re-capturing the roll.
- **H-C (unresolved, non-blocking):** the offline contact sheet "still looks off" to the
  reviewer despite stage-by-stage verification of the decoder (§4; exact byte-count matches,
  verbatim function copies, clean odd/even row-boundary statistics). Possible explanations:
  32×32 CI8 textures are inherently noisy at native res; small-cell presentation; or a
  residual misunderstanding of the final layout. Fallback if it ever matters: a standalone C
  harness running GE's actual `texInflateZlib` on the ROM, byte-diffed against the Python
  decoder. **Not on the critical path** — the in-game fixes don't depend on it.

## 6. Fix plan — ORIGINAL (2026-08-29); see §0 for what actually shipped

> **M-31 outcome:** item 2 (mip) shipped as `Video.FixMipTextures`; item 4 (palette)
> retracted; the "interlace" was `texSwapAltRowBytes` not on this list (D159, fixed);
> the Depot roof was CI8+`G_TT_NONE` not this list (D161, fixed); item 5 (dump hook)
> shipped as `GE_DTEX` + `GE_TEXDUMP`. Items 1 (G_SETTEX) and 3 (wrap) not done as
> written — see §0 / §6b.

1. **G_SETTEX** (`gfx_pc.cpp` dispatch): rebind render tile to the texpool entry for
   `texture_id` per gmain.s semantics. Biggest single visual win (characters).
2. **Mip contamination:** upload base level only in `gfx_dp_load_block`/`import_texture_*`;
   drop `glGenerateMipmap` and the `MIPMAP_LINEAR` min-filter default. Fixes Depot floor noise.
3. **Wrap period:** wrap at the N64 mask period (`texDimensionToMask`) — fix the dead D74
   guard (`cms == G_TX_WRAP`), extend to the T axis and non-PoT dims.
4. **Palette off-by-one** (`gfx_pc.cpp:835`): one-line-class fix per channel.
5. **Verification hook:** env-gated dump of the actual uploaded GL image + tile setup for
   the first N texLoads (~10 lines in fast3d) → one Depot run gives true ground truth and a
   before/after reference (also settles H-C if it ever resurfaces).

Each item gets its own Dxx entry in `docs/internals.md` §F/§H at commit time
(proposed: next free labels after D126 — check the §F index first; a stray "D160" appears
once in the doc and may need reconciling).

## 6b. M-30 follow-up — implementation notes (before you touch RC2)

Re-checked against the current tree while chasing the user's "interlaced
textures" screenshot (`screenshots/interlaced texture example.jpg` — main menu,
wallet-Bond photo + right panel show alternating-row tearing).

- **RC1 (G_SETTEX) is not a simple `case` add.** `src/game/tex.c texLoadFromGdl`
  already *expands* every G_SETTEX (opcode 0xc0 == `G_NOOP` slot) in a model GDL
  into a full standard load sequence (`texLoadFromTextureNum` + `texFindInPool` +
  `texWriteTextureCmd`) written into a replacement GDL, and
  `src/game/objecthandler_2.c` (`~line 82`) drives that per display list at model
  load + `modelNodeReplaceGdl`s the node. It **is** `#ifdef PORT`-ported. So on PC
  the RSP/fast3d should normally *not* see raw G_SETTEX. The user's crash log
  showed fast3d walking into `0xfafbfbfc…` garbage (opcode 0x00, **not** 0xc0) —
  i.e. the model GDL pointer / expansion is broken for the front-end wallet
  models, not "an opcode is missing." This is the **D75 front-end-model family**
  (D80/D82/D83 model-GDL relocation, `objecthandler_2.c`), not a fast3d opcode
  gap. Implementing G_SETTEX in fast3d would be a fallback that only helps models
  that reach it with an otherwise-intact DL.
- **RC2 (mip contamination) — the naive height clamp was already tried and
  reverted.** `gfx_pc.cpp import_texture()` ~line 917 has a comment: a prior
  branch that set height = `line * tile.height` was removed because it "dropped
  mip chains" and "truncated sub-tiled textures" (Rare-logo `D_02005FF0` 20×3
  tile uploaded as 32×3). So the fix must distinguish "extra rows are mip levels"
  (drop) from "tile is a window into a larger image" (keep). The signal is
  `rdp.tex_lod` (passed to every `import_texture_*` as `gen_mipmaps`): when LOD is
  on and `size_bytes/row > tile_height`, the excess is mip data → clamp height to
  `((lrt-ult)>>2)+1` from `rdp.texture_tile[tile]` **and** pass `gen_mipmaps=false`
  (don't let GL build mips from the stacked image). When `rdp.tex_lod` is off,
  keep today's behaviour. Verify against the Rare logo (sub-tile case) + Depot
  floor (mip case) + BUNKER1 (regression).
- **The alternating-row look specifically** may also be `texSwapAltRowBytes`
  (`image.c:2191`, §4: applied to *every* level of *every* texture on *every*
  path — odd rows get adjacent u32 groups pairwise swapped). The game swaps the
  RAM buffer *before* upload, so fast3d should read the swapped bytes — confirm
  fast3d isn't reading a pre-swap copy, or double-swapping. This is the
  RenderDoc / §6-item-5 dump task; do that before editing.

**Bottom line (M-30):** #3 is not a grind-fix. Highest-leverage next step is
§6-item-5 (env-gated dump of the actual uploaded GL image + tile setup for the
first ~10 texLoads on the main menu and on Depot) to see whether the base image
is wrong (decode/swap) or just over-tall (RC2 height). Then RC2 per the
`rdp.tex_lod`-gated plan above. G_SETTEX / D75 front-end models are a separate,
larger track.

## 6c. B2 Depot ceiling — ROOT-CAUSED + FIXED (D161, M-31)

The blue-speckle + radial-ray roof was **not** RC2/RC3/RC4/filtering. `GE_TEXDUMP`
(PPM dump of every uploaded texture + a `fmt/siz/palfmt/palidx/pal[0..3]` log
line, both env-gated, in `gfx_pc.cpp`/`gfx_opengl.cpp`) identified the surface as
**one 16×16 CI8 texture uploaded with `rdp.palette_fmt == G_TT_NONE`** — every
other CI texture that frame had `palfmt == 0x8000` (RGBA16). GE draws the Depot
roof with `gsDPSetTextureLUT(G_TT_NONE)`: a CI-siz tile with the TLUT disabled is
a legit N64 idiom meaning "use the raw 8-bit texel as intensity" (≈ I8).
`import_texture()` ignored `palette_fmt` for `fmt==CI` and did a palette lookup
against a **stale `rdp.palette`** → 16×16 of blue/magenta noise; the "rays" were
that noise aliasing on the receding ceiling plane (hence filtering never helped).

**Fix:** `gfx_pc.cpp import_texture()` — `fmt_eff = (fmt==CI && palette_fmt==
G_TT_NONE) ? I : fmt`, dispatch on `fmt_eff` (CI4→I4, CI8→I8). Narrow (only
touches currently-garbage surfaces). Depot corridor + control-room ceilings now
render as clean dark/grey industrial roofs; `-level_09/-30/-34` sweep PASS.
Full write-up: §F D161.

## 6d. RC3 non-PoT wrap period — IMPLEMENTED (D167, M-31), default OFF

`gfx_dp_set_tile` now stores `masks`/`maskt` on the tile (was dropped). The
hoisted per-texunit pre-wrap block in `gfx_sp_tri1` (the reworked D74 block —
already lifted out of the vertex loop at M-30, indexed by texunit `t`) gained a
branch: when the render tile is WRAP (no CLAMP/MIRROR) and `1<<mask != tex_width`,
fold the UV at the N64 period `1<<mask`, then clamp the `[dim, 1<<mask)` overflow
band (no real texels — a TMEM smear on console) to the last texel. All behind the
existing `Video.WrapFix` knob (`GE_WRAPFIX` env overrides it for testing).

**Captures** (`-level_09/-30/-34/-20`, WrapFix OFF vs ON, `GE_PCDUMP` 6-frame
windows): no crashes, 6/6 frames each; Silo ~pixel-identical (phash 0–11),
Facility 180–260 pixel-identical, Depot shows small localized texel changes on
ceiling/wall cells (dmean 5–9, no structural break). Large per-run frame deltas
are all D117 intro-camera-pan nondeterminism.

**Default kept OFF** — no regression, but a headless structural diff can't confirm
the Depot ceiling looks *better*. To finish RC3: run Depot with `Video.WrapFix=1`
and eyeball the corrugated roof / repeating wall panels vs default; flip the
default in `port/src/video.c` (`cfgWrapFix = 1`) if clean. The overflow-band clamp
is an approximation of console TMEM-smear behaviour, not an exact emulation.

## 7. Artifacts (in `<local-path>`, outside repo)

| File | What it is |
|------|-----------|
| `depot_d85.log` | Full Depot run with `GE_D63=1 GE_D85TEX=1` — 342 texLoad records (texnum/thisoff/nextoff/size), zero pool-full events |
| `decode_depot_tex.py` | Offline rarezip/bitstream decoder (verified against §4) |
| `depot_texsheet.png` | Contact sheet, 342 tiles, labeled by texnum (zlib-decoded only; non-zlib I4/IA4 cells blank) |
| `depot_texstats.txt` | Per-texture format/size/lod/color stats for all 342 |
| `tiles_big.png`, `tile_*.png` | Individual full-size decodes (760, 313, 1283, 233, 1082, 21, 451, 7) |
