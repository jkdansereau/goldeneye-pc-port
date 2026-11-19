# D589

<!-- from the old section-F/H table row -->
| D589 | **macOS arm64: wide-pixel (32-bit RGBA / 16-bit) textures render as colourful static / confetti (the Surface 1/2 billboard trees) while x86_64 renders them correctly. The "pool holds the rev32" line is SUPERSEDED: the decode-time pool is 134/134 byte-identical Mac/Win (the writer side is clean); the rev32 happens at the *import* — fast3d's `gfx_tex_source_is_c_array` mis-classifies every DRAM/cart source as a C-compiled array on arm64 macOS (host/raw address-space mixup) and `gfx_tex_normalize_source` bswaps it. See D591 for the root-cause line + fix (`9c59cbe7`), now **Mac A/B verified** (carray=0, decoded texels no longer reversed). The frame still diverged (the "rock striping") from a SECOND cause, **D592** (initially framed as a GL-layer / NPOT `glGenerateMipmap` divergence, later CLOSED as a **non-issue** — a VHF-metric false-positive on natural rock grain; the sampler state + NPOT mip levels are byte-identical Win/Mac and the maintainer's live Mac look is clean). D589 is RESOLVED (both causes closed: D591 fixed, D592 non-issue).** — full `## D589` entry at file tail | RESOLVED 2026-10-08 (macos branch; byte order fixed+verified = D591; the SECOND CAUSE D592 is CLOSED as a non-issue — VHF-metric false-positive, sampler+mips byte-identical Win/Mac, maintainer live-look clean; NPOT guard + 82dbea9c reverted b80dcd08) |

## D589: macOS arm64 wide-pixel textures render as colourful static (Surface 1/2 billboard trees) — PARTIAL 2026-10-08 (macos branch; the rev32 / byte-order half is root-caused + FIXED + Mac-verified as D591; a SECOND cause — a GL-layer divergence (GL texture-object / mipmap / sampling, downstream of byte-identical fast3d texels; D592) — keeps D589 open, the rock striping remaining; the decode/normalize/D75-swap path and the glTexImage2D input are proven byte-identical Mac/Win)

### Symptom

Live user report on the macOS (arm64) build: the **Surface 1 / Surface 2
billboard trees** (and, on the Dam, the rock-face textures) render as
**colourful static / confetti** (green/purple/pink dithered noise, no tree
structure) instead of the correct snow pines. Sky, ground, weapon and
single-byte-format (CI4/CI8/I4/I8/IA8) textures render correctly on the Mac;
only the **wide-pixel** (32-bit RGBA / 16-bit) textures garble. The x86_64
(Windows) build renders the same scenes correctly.

This is a *content* (byte-order) defect in the texels, **not** a geometry /
billboard defect — the quads sit at the right place and size on both platforms
(cf. D236, the closed billboard *wall/sheet* issue, which was a tiling /
geometry problem, a different bug). It is also *Mac-only*: x86_64 is fine.

### Evidence (all same-seed, same-level A/B; the Mac is at `c675ee99`, the
Windows at `8bfc5a1b` — D588 is Mac-neutral so the texture code is identical)

1. **Captures.** `mac-surface1-f900` = confetti trees; `win-surface1-f900`
   = structured snow pines, same frame / camera / seed.
2. **The TLUT is NOT the bug.** Aligning the two `GE_TEXI` import logs by
   normalised N64 address (Mac addr = 16 TiB window base + off; Windows
   base-0), every import with a real palette (`palfmt != 0`) carries the
   **identical** `pal[0..3]` on both platforms. The palettes load correctly on
   the Mac, so the garble is in the texel bytes, not the palette.
3. **The texel bytes are byte-reversed on the Mac — proved on the first
   import (r000), a clean per-32-bit-word reversal, not noise.** A fresh
   single-frame Dam A/B (`-level_33`, `GE_RSEED=0x0123456789abcdef`,
   `GE_INPUTSCRIPT=20:START`, `GE_TEXDUMP=1 GE_TEXRAW=1`) aligned by **import
   sequence** (same seed + level ⇒ the common prefix of the two import lists
   is 1:1). The very first 32-bit RGBA import — `r000_f3_s1_64x64`, N64 addr
   `0x701994f8`, imported identically (same address, same format, same first
   import) in both runs — is a **perfect 4096-byte per-32-bit-word reversal**
   of x86_64's `texdump/r000`: mac `98 87 98 76 | bb a9 98 98 | dd cc bb ba
   | ee dd cc bb | ed ff ff fe | …` vs win `76 98 87 98 | 98 98 a9 bb | ba
   bb cc dd | cc cb dc ee | fe ff ff ed | …` (every one of the 1024 words
   matches `mac[k:k+4] == reverse(win[k:k+4])`). These are *structured*
   ramp/gradient texels, so this is real content, exactly byte-reversed —
   not noise. Nine more aligned imports are clean `rev32` the same way (all
   32-bit and 16-bit families).
   **Caveat on the "other" bucket:** of the 99-tuple common prefix, **68 are
   re-imports** (the same `(addr,fmt,siz)` appears >1× in the log) and are
   imported at slightly different frames in the two runs, so their content
   legitimately differs — they match under *no* transform and are **not**
   evidence for or against a byte-order difference. The 10 clean-`rev32`
   samples (incl. r000) are the single-import, same-moment subset and are the
   meaningful signal.

### Why this garbles (the D219 contract)

`src/game/image.c` (D219 / M-113 / M-115): the runtime-decoded wide-pixel
families have opposite byte-order contracts, keyed on GBI size:

- **16-bit** (`import_texture_rgba16` / `import_texture_ia16`): read the pool
  as *manually big-endian* bytes → the store must `bswap16` (`PORT_PIXEL16`).
- **32-bit** (`import_texture_rgba32`, `port/fast3d/gfx_pc.cpp:896`): does
  `PD_BE32()` on a **native** u32 load — it expects the pool to hold **native
  host-order words** (`PORT_PIXEL32` = identity).

On x86_64 (little-endian) the pool holds native-LE words, `import_texture_rgba32`
applies `PD_BE32`, and the GPU gets the correct texels. The A/B shows the
**arm64 pool holds the reversed (big-endian) order**, so after `PD_BE32` the
arm64 GPU samples byte-swapped texels (R↔A, G↔B) → the colourful static.

### Root cause (two candidates; the writing line is not yet pinned)

`import_texture_rgba32` and `texReadUncompressed` (the `TEXFORMAT_RGBA32` /
`RGB24` `dst32[x] = PORT_PIXEL32(px)` store) are *shared* code and run
identically on both platforms, so the reversed pool must originate upstream:

1. **(a) a big-endian write into the 32-bit pool on arm64.** Something in the
   sidecar / asset pipeline lays the 32-bit texels down byte-reversed on the
   16 TiB base. The sidecar load (`port/src/pccg.c:119 pccgLoadSidecars`) is a
   byte-exact `fsRead`, so if it is the source it must be a mis-addressed read
   (candidate 2).
2. **(b) the source region is mis-addressed on the 16 TiB image-rel** (D131
   class, the same family as D588): either the pccg sidecar is read from the
   wrong in-window address, or the runtime bitstream (`texReadBits`,
   `img_curpos`) points at a shifted region, so the decoder reconstructs
   byte-reversed texels.

### Next (concrete, owed)

- Instrument the write: dump the *source* region bytes (before `import_*`)
  for one 32-bit RGBA texture on **both** platforms — `GE_TEXDUMP_ADDR=<host
  addr>` (host addr differs per platform: 16 TiB window on arm64, base-0 on
  x86_64; take it from each run's own `GE_TEXI` log) — and compare. If the
  source is already `rev32` → mechanism (b) (mis-addressed / byte-reversed
  source); if the source is native-LE on both but the pool is `rev32` on the
  Mac → mechanism (a) (a Mac-specific store) and the defect is the `dst32` /
  sidecar write.

- **BLOCKED (2026-10-08) — UNBLOCKED by D590 (2026-10-08):** the D589 A/B needs a working Windows `build-pc/` to
  produce the Win-side rNNN pool dumps, but the Windows build now hits a
  **new deterministic `load_bg_file` SIGSEGV** (`src/game/bg.c:897`,
  `exe+0x729b`, bg-load worker thread, 4/4 incl. a plain run with no probe env
  vars — so it is NOT the `GE_ZLIBDUMP`/`GE_D589POOL` probes and NOT a
  `bg.c` change; the tree is clean at `eed6b774`). Two open hypotheses: a
  build/reconfigure delta from the full 251-object rebuild (CMakeCache
  regenerated) vs. the new `image_rel=1` path in `load_bg_file` (D607/D613
  unification). Full state + investigation order:
  `docs/dev/notes/HANDOFF-WIN-LOADBG-CRASH.md`. The Mac is also unreachable
  (ssh to the Mac timed out ×3).
- Note: the arm64 **single-frame** run (`GE_PCDUMP=900:900 GE_QUITFRAME=901`)
  crashes with a null-PC (`PC=0x0` SIGSEGV) on Surface 1/Dam at the
  early-quit path — a *separate* defect to file (the full-sweep / full-boot
  runs are unaffected); do not let it mask the texture diagnosis.

**Offline L1/L2 leads (2026-10-08, `scratch/d589-dam/` dumps; the Mac is
unreachable — no new A/B):**

- **L1 (find the ASCII writer) — the ASCII tell is a false positive.** The
  "ASCII runs" in the Mac r000 bin are the normal shape of 4-bit CI packed
  pixel data: every byte holds two 4-bit palette indices, so most bytes are
  printable ASCII, and adjacent pixels frequently share an index (repeating
  byte pairs like `76 98 98 87 98 98 …`). The **Windows** r000 bin shows the
  same character exactly. r000 is *not* being clobbered by non-texture
  data; there is no second writer at this bin. The ASCII-run reasoning in
  the D589 review is retired.
- **Pairing check (before trusting any Mac-vs-Win bin comparison): the
  import sequences have diverged.** Of 800 dump names per side, **464 do not
  match** (the `WxH` suffix disagrees at the same rNNN index — e.g. r036 is
  `48x32` on Mac vs `32x32` on Win), and of the 336 names that *do* match,
  334 differ in content. Pairing by tdc index is invalid until the import
  lists are aligned; any byte-level "rev32" comparison made on mis-paired
  bins proves nothing.
- **L2 (pool-cursor state) — 2026-10-08, both legs run: the pool state
  MATCHES THROUGH LOAD 8; the allocator half is NOT yet retired.** A `GE_TEXP`-gated probe in `texLoad`
  (image.c, after the pool write) dumps the pool cursors + the just-
  written tex header for the first 8 loads, plus a `GE_SIZEOF` startup
  line (`port/src/main.c`: struct tex/texpool + native scalar widths).
  One-pass Mac script `scratch/mac_d589_onecmd.sh` (sync to `65d2eb5a`,
  clean build, seeded Dam run `GE_TEXP=1 GE_SIZEOF=1 GE_RSEED=0x0123456789abcdef
  GE_QUITFRAME=1500 GE_INPUTSCRIPT=20:START`, 640x480 ini; game exit 0,
  no crash log — the D590 fix landed in the sync, so the Mac did NOT hit
  the bg-load crash) vs the identical Windows run. **Every field of all
  8 loads matches**: same texnum order (2106, 2084, 2085, 2088, 2089,
  2098, 2099, 2100), same `bytesout` (3136/448), same `leftpos-start`
  deltas (+3144…+17088, i.e. `bytesout+8` per load), same `end-rightpos`
  deltas (−24…−192, 24 = PC `sizeof(struct tex)` per load, N64 16),
  same fmt/maxlod/w/h per tex; `start/end` sit at identical offsets
  (0x112050/0x1ae450) in each platform's DRAM view. **`sizeof` also
  matches where it matters**: `struct tex`=24, `struct texpool`=32 on
  both (the D457 widening); the only ABI-width difference is `long`
  (8 arm64 / 4 x86_64), which neither struct contains. Artifacts:
  `scratch/d589-dam/{mac-texp,win-texp1500}/`.
  **Consequence: the pool/allocator state MATCHES THROUGH LOAD 8 — the
  divergence is later in the sequence / at import-time reads. Not
  retired, only not-yet-reached: the earlier 224/400 texdump-bin
  `size_bytes` divergence happens later, so a full-sequence cursor sweep
  (the raised `GE_TEXP` cap, self-determinism first — the loads run on
  the bg-load worker thread, so a platform that differs from its *own*
  second run makes every cross-platform diff meaningless) is owed
  before L3 or any texdump A/B. If the first diverging load is found,
  also check its decode for uninitialised-byte consumption (the 1×1
  texnum=1608 zlib scratch showed stale stack garbage in bytes 1–31 —
  a classic cross-platform difference source), before L3 (K0/V1).

### D591 A/B result (2026-10-08) — the byte order is fixed; a SECOND cause (D592) keeps D589 open

The D591 fix (`9c59cbe7`) was A/B'd on the arm64 Mac (back online at
HEAD `25f92e09`). **The rev32 / byte-order defect is gone:** `GE_TEXN
carray=0` on every probe line (was `1`), the Mac `raw8` bytes equal the
Win baseline, and the decoded wide-pixel texels are no longer byte-reversed.
So the *mechanism this entry was chasing* ("the pool holds the rev32",
root-caused as the `gfx_tex_source_is_c_array` host/raw mixup in D591) is
closed.

**But D589 as a whole is NOT closed.** After the fix the Dam frame still
diverges from Windows at **~31.8% of pixels** (was ~96% pre-fix), and the
divergence is spread across the **whole 3D scene** (per-80px-cell 0–73%,
worst in the sky-adjacent top-right), **not** localized to one texture or
the HUD — and there are **no pink/magenta pixels on either side** in the
HUD region, so the D219/M-114 "ammo HUD pink" class is not present. That
broad-scatter signature (structurally-aligned geometry + HUD, wrong
*content* scattered everywhere) points at a **second, distinct cause,
filed as D592**: the two platforms import the same *set* of textures
(decoded-shape multiset matches 1166/1167; the Mac has 10 fewer 16-bit/f2
textures) but in a **different order** — the import streams are identical
through r000–r098, then scramble. The rNNN texdump index is a per-run
*positional* counter, so once the order diverges the rNNN-suffixed bins are
not cross-platform-alignable and (if texture→object binding keys on import
position) individually-correct textures land on different regions.
Maintainer visual-sanity package: `scratch/d589-dam/ab3-mac/`.

Cross-tag D219 (the wide-pixel byte-order contract), D159 (the sibling
odd-row pre-swap class, `#ifdef PORT` no-op in `image.c`), D131 (image-rel
range), D441 (the address model), D236 (the closed billboard *wall*, a
different geometry bug), **D591 (the byte-order root-cause, now
verified-fixed), D592 (the second cause — a GL-layer divergence, downstream of byte-identical fast3d texels; the initial import-order theory was a camera-dependent red herring, DISPROVEN — D589 stays open on this)**. The D589 "pairing check" above
(464/800 name mismatch) is the same camera-dependent import-order effect, now retired as a red herring (see D592);
