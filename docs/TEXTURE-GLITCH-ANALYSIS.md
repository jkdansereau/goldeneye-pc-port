# Texture Glitch Analysis — PC Port (read-only investigation)

**Date:** 2026-08-29 · **Status:** diagnosis complete, fixes pending (session was read-only)
**Supersedes nothing** — feeds the Dxx entries proposed in §7. Companion artifacts: `CAPTURES_DIR` (outside repo).

---

## 1. Reported symptoms

1. **In general:** textures repeat oddly with weird artifacting.
2. **Depot (`-level_30`):** loads "completely wrong" textures — the ground is drawn a
   glitched **blue** that is not right (visible in attract mode).
3. **Character models:** obvious texture artifacts during the credits roll.

## 2. Root causes (confirmed by code + ROM evidence)

| # | Cause | Location | Explains | Fix class |
|---|-------|----------|----------|-----------|
| RC1 | `G_SETTEX` (0xc0) is a **no-op** in fast3d | `port/fast3d/gfx_pc.cpp:2443` (`case G_NOOP`) | #3 character/model artifacts | implement the command |
| RC2 | **Mip-chain contamination**: base + all LODs uploaded as one GL image | `src/game/tex.c:330` (`texGetDepthAndSize` sums all LODs) → `gfx_dp_load_block`/`import_texture_*` | #1, #2 noise bands, wrong scale, minification garbage | upload base level only; disable GL mips |
| RC3 | **Wrong wrap period**: N64 wraps at `2^ceil(log2(dim))`, GL wraps at uploaded image size; D74 pre-wrap block is dead (`G_TX_WRAP == 0`, `include/PR/gbi.h:391`) | `port/fast3d/gfx_pc.cpp` UV math in `gfx_sp_tri1` + D74 guard | #1 "repeats oddly", squash/stretch on non-PoT dims | mask-based wrap period (fix D74 guard, extend to T axis) |
| RC4 | **Palette off-by-one** in `palette_to_rgba32` | `port/fast3d/gfx_pc.cpp:835` | subtle hue shift on all CI textures (grays survive; red→orange-ish, blue→cyan-ish) | 1-line-class fix per channel |

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
  `docs/GRAPHICS-BACKLOG.md`).

### RC4 — Palette off-by-one (subtle global hue shift)

`port/fast3d/gfx_pc.cpp:835`:

```c
// fast3d (current):   r = v>>11;        g = (v>>6)&0x1f;  b = (v>>1)&0x1f;  a = v&1
// N64 RGBA16 spec:    r = (v>>10)&0x1f; g = (v>>5)&0x1f;  b = v&0x1f;       a = v>>15
```

Channels are read one bit off (alpha treated as LSB). Grays (r≈g≈b) survive nearly intact —
which is why BUNKER1 looks "mostly right" (it's gray/brown) — but saturated hues shift.
Format mapping itself was verified correct: `TEXFORMAT_RGBA16_CI8/CI4` → `G_TT_RGBA16`,
`IA16_CI8/CI4` → `G_TT_IA16` (`src/game/image.c:71–104`).

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

## 6. Fix plan (when read-only lifts), in priority order

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

Each item gets its own Dxx entry in `docs/PCPortResearch.md` §F/§H at commit time
(proposed: next free labels after D126 — check the §F index first; a stray "D160" appears
once in the doc and may need reconciling).

## 7. Artifacts (in `CAPTURES_DIR`, outside repo)

| File | What it is |
|------|-----------|
| `depot_d85.log` | Full Depot run with `GE_D63=1 GE_D85TEX=1` — 342 texLoad records (texnum/thisoff/nextoff/size), zero pool-full events |
| `decode_depot_tex.py` | Offline rarezip/bitstream decoder (verified against §4) |
| `depot_texsheet.png` | Contact sheet, 342 tiles, labeled by texnum (zlib-decoded only; non-zlib I4/IA4 cells blank) |
| `depot_texstats.txt` | Per-texture format/size/lod/color stats for all 342 |
| `tiles_big.png`, `tile_*.png` | Individual full-size decodes (760, 313, 1283, 233, 1082, 21, 451, 7) |
