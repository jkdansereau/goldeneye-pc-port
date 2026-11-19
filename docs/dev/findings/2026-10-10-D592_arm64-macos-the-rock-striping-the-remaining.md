# D592

<!-- from the old section-F/H table row -->
| D592 | **arm64 macOS — the "rock striping" (the remaining cross-platform frame diff after the D591 byte-order fix) is a VHF-metric FALSE-POSITIVE, not a GL-layer defect. The CPU texel bytes, the GL sampler state, and the NPOT mip level counts are all byte-identical Win/Mac (the `GE_TEXSTATE` probe: the Metal backend builds the full NPOT mip chain, the aniso extension is present on both, and the NPOT rock texture gets min=trilinear/mag=linear/aniso=4 on both sides); a 0-pink scan confirmed D591 held; and the maintainer's live Mac look showed the Dam rendering clean (no visible striping, ammo counter not pink, F10 menu fine). The "most-striped" cell was smooth-shaded rock grain, not a periodic row-stripe. The NPOT `glGenerateMipmap` guard + companion single-level sampler fix (`9bed5904` / `82dbea9c`) were reverted as dead weight (`b80dcd08`, which also strips the `GE_TEXSTATE` probe).** — full `## D592` entry at file tail | CLOSED (non-issue) 2026-10-08 (macos branch; VHF-metric false-positive — sampler state + NPOT mip levels byte-identical Win/Mac, maintainer live Mac look clean, 0 pink; NPOT guard `9bed5904` + companion `82dbea9c` reverted `b80dcd08`; `GE_TEXSTATE` stripped; the genuine symptom was D591, fixed. The D592 live look was mouse-look-only due to D594) |

## D592: arm64 macOS — the "rock striping" (the remaining cross-platform frame diff after the D591 byte-order fix) — **CLOSED as a NON-ISSUE (VHF-metric false-positive), 2026-10-08.** The "stripes" were natural rock grain flagged by a crude high-frequency metric, *not* a GL-layer artifact: the CPU texel bytes, the GL sampler state, and the NPOT mip level counts are all **byte-identical Win/Mac**, and the **maintainer's live Mac look shows the Dam rendering clean** (no visible striping; ammo counter not pink; F10 menu fine). The NPOT `glGenerateMipmap` guard + companion single-level sampler fix (`9bed5904` / `82dbea9c`) were **reverted as dead weight** (`b80dcd08`); the genuine cross-platform symptom (pink HUD) was **D591 (fixed)**. 2026-10-08 (macos branch; the "second cause" that kept D589 open is resolved — see Close-out below)

### Symptom

After the D591 byte-order fix is verified on the arm64 Mac (D589 confetti
rev32 gone; `GE_TEXN carray=0`; decoded texels no longer reversed), the
same seeded Dam A/B still diverges from Windows at **~31.8% of pixels
at frame 150** (down from ~96% pre-fix, but far from clean). The
divergence is spread across the **whole 3D scene** (per-80px-cell 0–73%;
worst in the sky-adjacent top-right), **not** localized to one texture or
the HUD, and there are **no pink/magenta pixels on either side** in the
HUD region (the D219/M-114 "ammo HUD pink" class is not present — the HUD
is clean). Geometry + HUD are structurally aligned on both sides; what
differs is the *texture content* scattered across regions.

### Evidence (byte-level, 2026-10-08; keyed by the stable RAW source address, not rNNN)

The rNNN "import-order" line above was a **red herring**: frame 150's
camera pose already differs between the two runs (truck / hut / horizon /
clouds all shifted), so the *visible* texture set — and hence the import
*order* — differs for a benign reason. `tdc` is positional by construction,
so rNNN is not a cross-platform key. The decisive test is the **decoded
bytes, keyed by the stable RAW-space source address** (`orig_addr -
PORT_ADDR_BASE`, the low-32 address the game uses), via two new
`GE_`-gated probes (`GE_TEXN` now logs the `d75` verdict + `dxt0` +
`rawaddr`; `GE_TEXDUMPROW=1` dumps the FINAL post-transform source bytes
to `texdump/row_<rawaddr>_f<fmt>_s<siz>.bin`):

- **The D75 odd-row swap never fires on either platform.** Every one of
  the ~1170 imports is `carray=0` (a DRAM pool source, raw `0x70xxxxxx`)
  and `d75=0`. D591 fixed `gfx_tex_source_is_c_array`, so the `d75_swap`
  gate (which re-uses that classifier, `gfx_pc.cpp:1275`) is now 0
  everywhere. The "alt-row / odd-row swizzle" hypothesis is therefore
  **disproven** — there is no swizzle being applied or skipped.
- **The decoded source is byte-identical cross-platform.** Of 141 unique
  `rawaddr` row-bins per side, 112 share an address and are **100%
  byte-identical (0 differ)**; the 29 Win-only + 29 Mac-only are the
  *same content at different pool addresses* (the pool allocator placed
  the 16-bit/`f2` textures differently) — by content hash the two
  platforms have **exactly the same 128 unique texture blobs, 0 differ**.
  The decode / `gfx_tex_normalize_source` / destride path is clean.
- **The exact `glTexImage2D` input is byte-identical cross-platform.** The
  `GE_TEXDUMP` GL-level PPMs (`gfx_opengl_upload_texture`, `tNNN_WxH_mipM`
  = the post-expansion RGBA32 buffer handed to the driver, 400 per side)
  match as a **multiset of (W,H,content): 400/400, 0 differ**.
- **Conclusion:** fast3d produces byte-identical texels down to the
  driver hand-off, yet the Mac renders the rock striped (maintainer
  eyeball). The divergence is **downstream of the CPU texel data** — in
  the GL texture-object build, `glGenerateMipmap`, or sampling (the
  GL-driver / GPU path) — not in the decode/normalize/swap or
  import-order logic.

### Root cause — PINNED (HIGH confidence): Metal `glGenerateMipmap` on NPOT textures, trilinear-sampled

The CPU-side input to `glTexImage2D` is proven byte-identical (below), so
the striping is introduced by the GL backend. The backend is now identified
and the responsible path narrowed by a 5-run ini A/B (2026-10-08):

- **Backend.** `--debug-gl`: Mac = **Apple M3, GL 4.1 (Metal), vendor
  Apple, GLSL 4.10** (Apple's deprecated GL core, Metal-backed); Win =
  **NVIDIA GeForce RTX 5090, GL 4.6, GLSL 4.60**. The Metal path is the
  divergent layer. (The earlier "ANGLE→Metal or Mesa" guess is corrected:
  the Mac runs Apple's native GL 4.1 / Metal, **not ANGLE**.)
- **Q1 (byte-exact upload, incl. alpha).** A `.rgba32` sibling dump of the
  `glTexImage2D` buffer (added this turn, `GE_TEXDUMP`) shows **116/116
  unique content blobs byte-identical Mac/Win, 0 platform-exclusive —
  *including alpha*** (the RGB-only PPM check was a lossy proxy: 66% of
  the sampled texels are non-opaque, so the alpha is real and it matches).
  The upload is airtight.
- **A/B (ini-only, 5 runs, one clean build).** Per-80px-cell *change* vs
  the R0 baseline (TF=1 AN=4 FM=1 MSAA=2):
  - **R4 MSAA-off (MSAA=1): 6.1%** and **R1 aniso-off (AN=1): 5.1%** →
    MSAA and anisotropy are **exonerated** (near no-ops on the frame).
  - **R2 nearest (TF=0): 9.1%** → the linear-filter path matters.
  - **R3 all-off (TF=0 AN=1 FM=0 MSAA=1): 26.4%** → the **mipmap /
    linear-filter sampling path is the dominant lever**, concentrated in the
    same scene cells that differ from Win. No toggle *converges* the frame
    toward Win (R3 is 34.6% vs Win, *worse* than R0's 28.4%) — expected,
    since Win renders with filtering ON and the frame-150 camera also
    differs (the P0 camera pin is owed for a clean pixel test). The A/B
    localizes the *lever*, not a "fix" setting.
- **NPOT (Q2).** The upload shapes include **6 NPOT sizes, 114/400
  uploads, dominated by 32x48 (×61)** (plus 48x32, 32x33, 16x3, 64x17,
  56x54). `glGenerateMipmap` on an NPOT texture is **undefined behaviour
  in the GL 1.x contract (mips only on power-of-two)**; the Metal backend
  evidently generates (or leaves) a bad chain and trilinear
  (`GL_LINEAR_MIPMAP_LINEAR`) sampling of it reads as the row-shifted /
  interleaved striping. The rock is almost certainly the 32x48 NPOT family.

**Confidence: HIGH** that the striping is the Metal `glGenerateMipmap`-on-
NPOT / trilinear-sampling path (byte-identical upload + the A/B exonerating
MSAA/aniso + the dominant NPOT shape). **MEDIUM** that the *exact*
mechanism is row-shifted generated mips (inferred from the interleaved
visual + NPOT + A/B, not directly dumped — a per-mip `glGetTexImage` would
close this).

**What is not the cause** (ruled out by the byte comparison + A/B): the
decode path, `gfx_tex_source_is_c_array` / `gfx_tex_normalize_source`
(D591, fixed), the D75 odd-row swap (0 on both), destride, import ordering
(benign camera effect), MSAA (6.1%), aniso (5.1%). Keep D589 open on this
until the fix lands.

**Base-space audit (2026-10-08, the "any other classifier?" sweep).**
`grep` of `port/` for a strip-`PORT_ADDR_BASE`-then-compare-host-bounds
pattern and for any other swap-gate: the *only* address-space classifier is
`gfx_tex_source_is_c_array` (line 834) — the one D591 fixed — and the
*only* alt-row/odd-row swap decision is `d75_swap` (line 1275), which
re-uses that same (now-correct) classifier, so it is 0 for every DRAM/cart
source. No other code path classifies a texture source by address space or
gates an odd-row/alt-row swap. The `PORT_ADDR_BASE` uses in
`port/src/{libultra,n64stubs,romdata}.c` are host-space *mappings* (add the
base), not strip-then-compare. So the "alt-row swizzle" theory is dead not
just because `d75=0`, but because there is a single swap gate and it is
off; the striping has no fast3d-side origin — it is the Metal GL layer.

### Next (concrete, owed)

- **Fix (port layer, `gfx_opengl.cpp`) — IMPLEMENTED 2026-10-08 (`9bed5904`):**
  a `GE_NPOT_NOGENMIP` guard skips `glGenerateMipmap` for non-POT textures,
  so the sampler uses a single level (N64-faithful — the RDP only mip-mapped
  POT). `GE_`-gated and **default-OFF**, so x86_64/Win (which tolerates NPOT
  mips) is untouched by default. **Win A/B:** guard ON vs OFF changes 5.2%
  of pixels, localized to the NPOT scene/rock cells (NVIDIA's NPOT mips were
  only the grazing-angle shimmer the guard removes); the unset path is
  code-identical to before.
- **Companion sampler fix (`82dbea9c`):** the skip-only guard left the sampler
  on a mipmap-aware min filter, so on Metal the (now-absent) mip levels read as
  **black** — a Mac R0-off vs R1-on A/B showed a +5.5 % black-pixel regression
  over the NPOT rock region (a cell went to solid 0,0,0). `set_sampler`
  now, under `GE_NPOT_NOGENMIP`, queries the bound texture for a mip level 1
  and forces the single-level (`MIPMAP_DISABLED`) min filter when it is absent.
  POT textures (level 1 present) and the default-off path are untouched.
- **Mac A/B NEGATIVE RESULT (2026-10-08, guard + companion):** rebuilt the
  arm64 binary and A/B'd `GE_NPOT_NOGENMIP` off (R0) vs on (R1) on Dam
  (`-level_33`, PCDUMP=150, pinned seed/input). The black regression is gone
  (R1 black 3.63 % vs R0 3.65 %), **but the "rock-band" vertical high-freq
  energy is UNCHANGED (11.16 → 11.17) — the guard does NOT clear the
  striping.** Worse, the "most-striped" cell (per-cell vertical variance ×
  sign-changes) has a **smooth** per-row-mean profile (40→…→90→back, R0≈R1),
  i.e. a smoothly-shaded rock, *not* a periodic row-stripe. So (a) the
  NPOT-mipmap → trilinear pin is **not supported** (removing the mip chain
  changes nothing), and (b) this Dam frame-150 repro may not even show the
  user's actual symptom (fine-grained noise on walls/snow, the D219/M-114
  pink-HUD class). **The pin stands down; D592 re-pinning is owed via the P0
  camera-pinned pixel test on a symptom-matching repro (snow walls / ammo
  counter), not on Dam.** Visual package: `scratch/d589-dam/ab3-mac/npot_guard/`
- **Confirm.** A camera-pinned frame (P0, owed) for a clean pixel test — now
  the **critical** next step, since the guard A/B came back negative (the
  mip chain is not the cause). If a per-mip `glGetTexImage` / `glGetTexLevel`
  dump is wanted it should target the *actual* symptom repro (snow walls /
  ammo counter), not Dam. A known Metal texture quirk would be reported
  (report, not fix) if the cause is confirmed to be driver-side.
- **Self-determinism** (worker-thread loads): still a good hygiene step,
  but it is no longer on D592's critical path (the byte-level diff is
  already pinned to the GL layer).
- Do **not** fold this into D591 (byte order) — it is a distinct,
  later-in-the-pipeline defect. Keep D589 open on this until the GL-layer
  cause is pinned and the scene matches.

Cross-tag D589 (the symptom this second cause keeps open), D591 (the
byte-order fix that closed the rev32 half — it also closed the D75 gate,
disproving the swizzle theory), D219/M-114 (the HUD-pink class, *not*
present here). The rNNN "import-order" theory is retired as a red herring
(camera-dependent), kept only as the tooling note in the evidence above.

### Close-out (2026-10-08) — CLOSED as a non-issue; NPOT guard + companion reverted

Three new data points (all 2026-10-08) close D592 as a **metric false-
positive, not a GL-layer defect**:

- **`GE_TEXSTATE` GL-state probe (added `de5a0fea`, stripped `b80dcd08`).**
  The effective sampler state the port sets is **byte-identical Win/Mac**:
  the NPOT rock texture gets `min=0x2703` (GL_LINEAR_MIPMAP_LINEAR =
  trilinear), `mag=0x2601` (linear), **aniso=4**, `mmode=MIPMAP_LINEAR`, and
  the aniso extension is present on **both** platforms. The **NPOT mip level
  counts are also identical** (32×48→6, 48×32→6, 32×33→6, 56×54→6, 16×3→5,
  64×17→7): the Apple M3 (Metal) backend **does build the full NPOT mip
  chain** — it does not silently drop the mips. So neither the sampler
  *state* nor the mip *chain* differs between platforms; there is no
  GL-layer divergence to fix.
- **Pink/magenta scan of frame-150 (both sides): 0 pink pixels on Win, 0 on
  Mac** → the D219/M-114 "ammo HUD pink" class is not present; **D591
  (the byte-order fix) held** — the genuine cross-platform symptom is gone.
- **The maintainer's live Mac look (mouse-look across the Dam): "stuff looks
  fine, ammo counter is fine, F10 menu is fine, what I can see is fine."**
  No visible striping. The "most-striped" cell had already shown a *smooth*
  per-row profile (rock grain, not a periodic row-stripe), so the VHF metric
  was counting natural rock grain as "stripes."

**Disposition.** D592 is closed as a **non-issue**: the "rock striping" was a
false positive of the crude vertical-high-frequency metric on naturally-
shaded rock, and every byte-level layer (CPU texels, GL sampler state, NPOT
mip level counts) is byte-identical Win/Mac. **The `GE_NPOT_NOGENMIP` guard +
companion single-level sampler fix (`9bed5904` / `82dbea9c`) are reverted
(`b80dcd08`) as dead weight** — their motivation (a bad NPOT mip chain) is
disproven and the guard A/B showed no effect on the rock region. The
`GE_TEXSTATE` D592 probe is stripped in the same commit. The second cause that
kept **D589** open is thereby resolved (its remaining diff was D592, now
closed).

**Caveat (honest scope):** the maintainer's look was **mouse-look only**
(camera turn, no free walking) because of **D594** (a macOS partial-input bug
— see below), so it is a *limited-vantage* confirmation, not a full-scene
sweep. It agrees, however, with every byte-level test and with the smooth-
per-row cell analysis, so the close stands. A full-scene headless camera
sweep would be a nice-to-have, not owed.

Cross-tag **D594** (the macOS partial-input bug that forced the mouse-look-
only look), D591 (the byte-order fix that removed the real symptom), D589
(closed on this), D219/M-114 (the HUD-pink class, confirmed absent).
