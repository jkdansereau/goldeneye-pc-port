# D615

## D615: HD texture packs (first stage) — `Video.TexturePack` selects a `<texnum>.png` directory; the renderer substitutes pack images at texture import, keyed on the texture number the game knows (ROADMAP #100)

**Scope:** the first stage of ROADMAP #100 (HD texture packs, post-1.0
batch; this landed early in the `feature/texpack` worktree off
`release/v0.4.1`). A texture pack is a sub-directory of `data/texpacks/`
holding PNGs named `<texture-number>.png` (the 12-bit `texturenum` the
game assigns each texture, zero-padded decimal). The new ini option
`Video.TexturePack` (sub-directory name; empty = off) selects one; the
port substitutes the pack's image for the ROM's decompressed texture
wherever that number is imported.

**Why the hook is in `texLoad` (the only game-code touch):** the N64
pipeline never makes the texture number visible to the RSP — GBI carries
pool addresses only. The game side knows the number (`tex->texturenum`,
written at `src/game/image.c` texLoad) and the renderer sees the address
(`import_texture`'s `orig_addr`), so one line bridges the two:
`#ifdef PORT texpackRegister(tex->data, (s32)tex->texturenum)` after the
pool allocation. **AGENTS rule 2:** this is a behavioral addition
(opt-in visual replacement, not an ABI/layout edit), so it was
implemented under the requested sign-off, behind `#ifdef PORT`, with the
default (empty option) fully inert — the un-patched build path is
untouched and byte-identical.

**Mechanism (all in `port/`):**
- `port/src/texpack.c` — address→texnum registry (open-addressing,
  insert/update only: the texture pool is a ring, so recycled addresses
  get re-registered; the D75 content hash still protects the GL cache
  independently). K0/V1 normalised direction-independently
  (`GE_HIGHARENA_BASE` inverts the numeric order). One mutex: the game
  thread registers, the render thread resolves; PNG decodes under the
  lock (each file decodes once; FIFO cache, 128 entries / 512 MB cap).
- `port/src/pngread.c` — self-contained PNG→RGBA32 decoder (zlib, which
  is already linked; no new dependency), adapted from the Perfect Dark
  PC port's (MIT; NOTICE). Handles the pack-relevant subset: 8/16-bit
  RGB(A)/palette/gray, plain + Adam7.
- `port/fast3d/gfx_pc.cpp` `import_texture` — when the pack has a file
  for the resolved texnum, the decoded RGBA32 takes over the upload
  (shared `tex_upload_buffer` → `upload_texture`, mip generation per
  `rdp.tex_lod`), bypassing the N64 pixel-format dispatch; the cache key
  is tagged `0x5EA7xxxx | texnum` so a pack upload never aliases a
  vanilla key. Images larger than the upload buffer / GL max fall back
  to the vanilla import (logged once).
- `port/src/video.c` — `Video.TexturePack` string option +
  `videoTexPackInit()` called from `main.c` right after `configLoad()`
  (same ordering as the Steam Deck preset).

**Verification:** `-level_09` A/B with a 512-file test pack
(`tools_pc/texpack_make_test_pack.py`, solid-colour PNGs covering
texnums 0–511): baseline vs packed frames — the level's 10 active
texnums (7, 257, 259, 295, 296, 315, 468, …) visibly substitute
(walls/floor/ceiling turn solid red), every un-replaced element
(doors, arrows, lights, the CCTV window) renders unchanged; decode log
clean, no errors, 32-frame runs stable. Default (empty option) run:
`texpack: no pack selected`, frame-identical scene.

**Known limitations (documented for the stage):** pack files replace
the *base image* — the N64 wrap period (the RC3/D167 mask period) is
unchanged, so a pack image larger than the tile's authored period will
repeat at the authored period; in-exe-image (C-array) assets are not
registered (only the ROM-serialised `texLoad` path), so title/HUD 2D
art is out of scope this stage; decode is synchronous on the render
thread (first-use stall per file, ~1–3 ms); no F10 options row yet
(string options have no options-overlay row — follow-up); rice-pack
(rice→texnum) support is the next stage, per the dabs reference.

---

### Session 2 (2026-10-06): pack-authoring dumper, alpha-true dumps, and the "line artifact" fix

**Texnum dumper (ROADMAP #100's dumper half).** The registry is now
always-on (`texpackRegister`/`texpackTexNumFor` no longer gated on a
selected pack — one hash probe + mutex per texLoad; only the pixel
swap in `texpackReplacement` stays gated), and two things were added:

- `GE_TEXNUMDUMP=1` appends every `(address, texnum)` pair to
  `texdump/texnumd.log` at registration — the offline map that turns a
  `GE_TEXDUMP` run into a pack-authoring job list.
- `texpackBeginImport(addr)` / `texpackCurrentTexNum()` (import context
  on the render thread, strictly synchronous, zero locking): 
  `import_texture` records the address it is about to import, and the
  GL upload layer (`gfx_opengl.cpp`) names its `GE_TEXDUMP` PPMs
  `texdump/NNNN.ppm` (texnum) for registered pool textures instead of
  the `tNNN_WxH_mipN` sequence name; unregistered/C-array textures and
  the fire-shape branch keep the old names.

**Alpha-true dump sidecar.** The PPM writer has no alpha channel, and
the pack-authoring tool initially flattened every pixel to A=255 —
which silently destroyed vanilla 32-bit cutout alpha (arrow HUD quads,
cactus leaves). The texnum-named dump now also writes a raw RGBA32
sidecar `texdump/NNNN.rgba` (`GE1\nW H\n` + w*h*4 bytes); the
authoring tool prefers it and falls back to the PPM (A=255
interleaved) only when the sidecar is absent.

**The "line artifacts" (4x pack looked wrong) — root cause + fix.**
Three separate bugs, in order of discovery:

1. *UV scale bug (introduced, then reverted):* a per-tile float UV
   multiplier (`tpack_su/sv = pack_dim / orig_dim`, applied to the
   vertex-pass UVs) assumed the GL image size had to be folded into
   the UV range. It did not — the vertex pass normalises by the N64
   texel count (`tex_width` from the tile line-size), and a wrapping
   tile's GL repeat period grows with the uploaded image size,
   cancelling the texel growth exactly. The multiplier therefore
   made every replaced surface Nx denser (the "line artifacts" of
   the first 4x test). Reverted at all four sites; the pack branch
   comment records why. (The 1x A/B: with UV scaling off and an
   alpha-true pack, 0 texnums differ at the dump level and the
   frame diff drops to a sub-2-level cactus-region remnant.)
2. *Nearest-neighbour upscale grid:* N64 textures are 16–64 px and
   most surfaces are **magnified** on screen. GL_LINEAR interpolates
   the small originals smoothly; a 4x NN pack turns each texel into a
   hard 4-px block, so the wall (a 32x32 original) showed a ~9-px
   square grid at mid-range. Fix: the authoring tool's default
   upscale is now **Lanczos-3** (separable 2x steps, pure stdlib;
   `--nn` keeps the blocky look).
3. *Minification aliasing:* in-level `tex_lod` is 0, so the vanilla
   sampler is base-level-only GL_LINEAR; a 4x pack image minifies
   4x harder and aliases into regular banding even with smooth
   content. Fix (port-only, pack-only): pack replacements always
   upload with a generated mip chain (`upload_texture(..., true)`) and
   the cache node's new `sampler_mips` flag forces the MIPMAP min
   filter (compared in the vertex pass so a pack/vanilla bind swap
   re-sets it). Vanilla nodes and the vanilla upload path are
   untouched (vanilla `sampler_mips` = `rdp.tex_lod` as before).

**Result:** 4x Lanczos pack on `-level_09` frame 100 vs vanilla:
mean abs diff 1.31/channel (the banded 4x NN build was ~11); no
visible artifacts at the cactus/wall/panel regions. A
procedurally-authored 1024x1024 demo pack (`testhd`, walls 0295/0259
with seams/rivets/grain/scratches) shows the authored-detail
"HD pack" look in-engine — the port needed nothing further for it.

**Pack-authoring tools (`tools_pc/`, stdlib-only):**
`texpack_from_dumps.py` (texdump dir -> pack dir; `.rgba` sidecars
preferred, PPM fallback; Lanczos-3 default, `--nn` option; scale must
be a power of two) and `texpack_demo_hd.py` (same pipeline, but the
listed texnums get a procedurally *authored* 1024x1024 texture —
seams/rivets/grain/AO/scratches composited over the smooth upscale —
as a stand-in for hand-drawn HD art).


**Addendum 2026-10-10 (alpha preservation + per-level overrides; port layer
only, `port/src/texpack.c`, `port/fast3d/gfx_pc.cpp`):**

*Fix 1 -- keep the original's transparency.* Glass (texture 654, an I-format
texture whose N64 alpha is its intensity) turned solid because the pack PNG
is fully opaque. "Fully opaque" is computed once per PNG at decode and
cached with it. On a texture-cache miss, when the replacement is opaque:
I (and CI drawn with the TLUT off, which the importer already treats as I)
and IA get alpha = Rec.601 luminance of each replacement pixel (exact N64
semantics for I; for IA a flat RGB image carries no alpha, so luminance is
the best available). CI with any transparent entry in the used palette bank,
RGBA16 with any alpha bit clear, or RGBA32 with any alpha < 255 cannot be
rebuilt faithfully: the texnum falls back to the vanilla texture (logged
once, first 32), remembered in a per-texnum mark cleared on level change.
Caveat: the CI check looks at the palette at first use; a texnum reused with
a different palette keeps its first decision until the next level. A PNG
that already has alpha is used as-is.

*Fix 2 -- per-level overrides.* `data/texpacks/<pack>/<SS>/<texnum>.png`
(SS = stage number, 1-3 digits, e.g. `33` = Dam) overrides the flat
`<texnum>.png` while that stage is loaded. The key is
`lvlGetCurrentStageToLoad()` (what the D416 log prints as `stage=`), read
port-side only when the pack ships at least one override directory.
Directories are scanned at pack-select time; the decode cache and the GL
texture-cache key include the source (stage+1 xor-folded into the 0x5EA7xxxx
tag), so a level change never reuses another level's override. With no pack
selected nothing changes (texpackReplacement returns NULL on the first test).
