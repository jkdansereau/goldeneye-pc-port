# D183

<!-- from the old section-F/H table row -->
| D183 | **M-36 Family A ("texture line/pitch shear") is DISPROVEN for the Surface repro (`-level_36`).** The importers' pitch assumption is real but never fires there; the Surface cliff "grey static" is not a shear. Full evidence + what the walls actually are: §F "D183" below. A defensive, provably-no-op de-stride landed in `import_texture()` anyway (covers the `gfx_dp_load_tile` case the `SUPPORT_CHECK`s assert against), plus `GE_DTEX` STRIDED marker + `GE_TEXRAW` raw-source dump. | CLOSED (2026-09-26, user-approved: disproven hypothesis; the Surface sky issue it served was fixed under D176). Earlier: Hypothesis DISPROVEN + diagnostics shipped; D176(b)/D182(2) still OPEN |

## D183 — M-36 "Family A" texture line/pitch shear: DISPROVEN on `-level_36` (M-36)

**Brief:** fix the "diagonal grey static / comb interlacing" attributed to
`import_texture_*` assuming `full_image_line_size_bytes == line_size_bytes`
(`docs/dev/M-36-TRIAGE.md` Family A; bugs D176(b) Surface walls, D182(2)
file-select spiral). Repro: `-level_36` Surface, headless `GE_PCDUMP`.

### 1. The pitch hypothesis does not fire on Surface — measured, not argued

`full_image_line_size_bytes != line_size_bytes` can only be produced by
**`gfx_dp_load_tile`** (`gfx_pc.cpp:2166`, a windowed sub-rect load).
`gfx_dp_load_block` (`:2139`) sets `line = full = size_bytes` unconditionally,
so the two are equal by construction.

GE loads *every* texture through `gDPLoadBlock`: `texWriteLoadToTmemZero` /
`texWriteLoadToTmemAddr` (`src/game/tex.c:492,617`) emit
`gDPSetTextureImage` + `gDPSetTile(…, line = 0, …)` + `gDPLoadBlock` and never
a `gDPLoadTile`. Confirmed at runtime: `GE_DTEX` (now prints `line=`/`full=`
and a `<-- STRIDED` marker) over a Surface run — **0 of 64** logged imports
strided; the full `GE_TEXDUMP` set — **0 of 166**. So no importer on this level
ever does the flat-read-of-a-strided-source that the hypothesis needs.

### 2. The importers' width/height/pitch math is *verified correct*

New probe `GE_TEXRAW=1` (with `GE_TEXDUMP=1`) writes the raw source bytes
handed to each importer to `texdump/rNNN_f<fmt>_s<siz>_<w>x<h>.bin`. Scanning
each dump's vertical neighbour-difference over every candidate row pitch
4…128 finds the true pitch of the source image:

| texture | fmt/siz | tile | pitch fast3d uses | best-scoring pitch | vdiff |
|---|---|---|---|---|---|
| `r030` | IA8 | 54×54 | 56 | **56** | 0.22 (smooth) |
| `r021` | CI8 | 32×32 | 32 | **32** | 0.72 (smooth) |
| `r031` (the wall) | IA8 | 32×32 | 32 | none — flat ~3.7 at *every* pitch | — |

The importers pick exactly the pitch the data is stored at. (The 54→56 case is
`texAlignIndices`/`texChannelsToPixels` 8-byte row alignment,
`src/game/image.c:340,1740` — handled correctly today.)

Also re-tested and re-confirmed: applying `texSwapAltRowBytes`' odd-row u32-pair
swap to these dumps makes vdiff **worse** (0.22 → 1.57 on `r030`). **D159's
`#ifdef PORT` no-op is correct; do not re-enable it.**

### 3. What the Surface walls actually are

The cliff/perimeter wall texture is `GE_TEXI` fmt=3 siz=1 = **IA8, 32×32,
block size exactly 1024 B (no LOD chain)**. Its source bytes are a
high-frequency grey noise field: intensity nibble mean 6.5, full 0–15 range,
horizontal neighbour diff 2.98 / vertical 4.06 (uniform random ≈ 5.3); alpha
nibble is a constant 15. There is no pitch, no row swap and no bit-depth
reinterpretation that turns it into a coherent image — the *data itself* is
noise-like. It is decoded and uploaded faithfully; the on-screen "static" is
that texture tiled at ≈1 texel per pixel on the wall faces.

Two filtering theories were tested and **both failed** (temporary
`GE_MINFIX` probe in `gfx_opengl.cpp`, since reverted):

- forcing mip minification when the game point-samples
  (`gfx_opengl.cpp:752` `linear_filter ? … : GL_NEAREST`) — **no pixel change**;
  the walls are already `linear_filter == true`.
- forcing `mipmaps = true` on every sampler apply — **no change to the walls**,
  and it *regressed* the 2D HUD ammo digits to white blocks.

So the walls already get `GL_LINEAR_MIPMAP_LINEAR`. Not a filter bug.

### 4. Where D176(b) actually goes next (unresolved)

Remaining candidates, in order:

1. **The wrong texture is bound to those faces.** The next decisive step is ROM
   ground truth: decode Surface's texnums offline (the
   `TEXTURE-GLITCH-ANALYSIS.md` §7 toolchain) and check whether *any* Surface
   texture is this 32×32 IA8 noise field, or whether the rock wall should be a
   different, larger, structured texture. If it exists in ROM as-is, D176(b) may
   be **not a bug at all** (GE's Surface cliffs are a mottled grey rock) and the
   user report is really about *tiling density*.
2. **UV / tiling scale** — the texture repeats far too densely across each wall
   face. That is RC3/`Video.WrapFix` territory (D167) plus the `shifts`/`shiftt`
   the game sets per LOD tile in `texWriteTileLods` (`tex.c:596` passes
   `shifts = shiftt = lod`), which fast3d stores but the LOD-tile selection
   (D107 "always base tile") may be mismatching.
3. **D182(2)** (file-select spiral after re-entry) is *not* covered by this
   finding — it was never reproduced here, and it is the one symptom that could
   still be a real `gfx_dp_load_tile` strided case (front-end `texLoadFromGdl`
   paths do use windowed loads). The defensive fix in §5 would cover it if so.

**M-37 UPDATE (inconclusive, ~8-min timebox):** the §7 offline decoder + logs
live *outside* this checkout — a full offline ROM decode was not achievable.
Static findings: Surface's bg is `bg/bg_sevx_all_p.seg` (`bg.c:201`); the wall
texture is a `u16` image ID *embedded in the bg GDL stream*, not a static
per-level texnum list, so it cannot be grepped — needs a runtime `GE_DTEX`/
`GE_TEXRAW` correlation. `assets/images.def` has no rock/cliff names (only
`STATIC_NOISE 0x389`); Surface-named entries are mission-select thumbnails.
Leaning **"real texture / tiling-density" (candidate 2), low-med confidence** —
D183's own measurement (constant full alpha 0x0F + full-range noise intensity,
zero residual structure at any pitch) is the signature of an intentional grey
rock-detail tile, not a mis-decoded structured image. Decisive next steps:
(i) grep the *converted* `bg_sevx_all_p` GDL for opcode `0xC0` (`G_SETTEX`) —
settles the one remaining "wrong bind" scenario (stale tile state, TEXTURE-
GLITCH-ANALYSIS §2 RC1) offline; (ii) headless `-level_36` `GE_DTEX/TEXRAW=1`
logging the image ID bound to the cliff tris, look it up in `g_Textures[]`,
read + decode that ROM range per §4. Scratch note: M-37 session scratchpad
`D176b-rom-groundtruth.md`.

### 5. What shipped (`port/fast3d/gfx_pc.cpp` only)

- **De-stride in `import_texture()`**, before the format dispatch: when
  `full_image_line_size_bytes > line_size_bytes`, compact the strided rows into
  a contiguous scratch buffer and hand every importer a row-packed image with
  `line == full`. This is the correct behaviour the seven `SUPPORT_CHECK
  (full_image_line_size_bytes == line_size_bytes)` asserts (`gfx_pc.cpp:689,
  710, 739, 765, 791, 818, 866`) merely assert against — and those asserts
  compile out in the release build, so today the wrong read is silent. Only the
  CI8 importer strides correctly on its own (`:893`); it becomes a no-op stride
  after this change. **Strictly a no-op whenever `full == line`, which is
  166/166 loads on Surface and every `gDPLoadBlock` in the game** — hence
  golden-safe. Unverified as a *fix* (nothing in the current repro exercises it);
  kept because it is provably-correct and removes a latent silent-corruption path.
- **`GE_DTEX`** now prints `line=`/`full=` and flags `<-- STRIDED (pitch shear?)`.
- **`GE_TEXRAW=1`** (alongside `GE_TEXDUMP=1`) dumps raw importer-input bytes.

### 6. Verification

- `-level_09` golden framediff: **3/3 within threshold** (200/320/440;
  worst dmean 10.4, phash 21/1/4) — unchanged.
- `-level_36` before/after: **visually identical** (as expected — the de-stride
  never fires there). The wall static is unchanged and remains open.

*Confidence:* "Family A pitch shear is not the Surface bug" — **high**
(runtime-counted, 0/166). "The wall texture is decoded correctly" — **high**
(independent offline pitch scan of the raw importer input). "The shipped
de-stride is golden-safe" — **high**. "What D176(b) really is" — **low**;
needs ROM ground truth for that texnum.

---
