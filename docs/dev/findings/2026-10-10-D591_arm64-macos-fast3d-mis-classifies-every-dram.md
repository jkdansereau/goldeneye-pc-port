# D591

<!-- from the old section-F/H table row -->
| D591 | **arm64 macOS fast3d mis-classifies every DRAM/cart texture source as a “C-compiled array” and byte-swaps it — the rev32 behind the D589 confetti. `gfx_tex_source_is_c_array` (`port/fast3d/gfx_pc.cpp`) stripped `PORT_ADDR_BASE` before comparing to host-space `PORT_DRAM_V1/K0_BASE` bounds; at `PORT_ADDR_BASE==0` the two spaces coincide (accidentally correct) but at 16 TiB (arm64 macOS) every in-window source falls through to the C-array default and `gfx_tex_normalize_source` bswaps it. Decode-time pool is 134/134 byte-identical Mac/Win; the reversal is import-time only. Fix = compare the host pointer directly (drop the subtraction), port-layer only, no-op at base 0. Win verified byte-identical (400/400 r-bins + PPM unchanged); **Mac A/B now verified** (carray=0, raw8 matches Win, rev32 gone).** — full `## D591` entry at file tail | FIXED + Mac A/B VERIFIED 2026-10-08 (macos branch; `9c59cbe7`) — byte order confirmed gone on the Mac; the remaining D589 scatter is the second cause D592 |

## D591: arm64 macOS fast3d mis-classifies every DRAM/cart texture source as a "C-compiled array" and byte-swaps it — the rev32 that makes the D589 pool render as static — FIXED + Mac A/B VERIFIED 2026-10-08 (macos branch; fix `9c59cbe7`; byte order confirmed gone on the Mac, but D589 stays open on the second cause, D592)

### Symptom

The D589 symptom (Surface 1/2 billboard trees + Dam rock faces render as
colourful confetti/static on the macOS arm64 build, while sky/ground/CI
textures and the *entire Windows* image are correct) was, by the time of
this finding, **localized to a single line but the mechanism was still
labelled "the pool holds the rev32" (unproven)**. A full-sequence A/B
settled the mechanism:

- **Decode-time pool is byte-identical.** `GE_TEXDUMP`+`GE_TEXRAW` on both
  platforms: 400 import-time `r*.bin` dumps each. The 134 `d_nz_*.bin`
  decode-time pool dumps are **134/134 byte-identical** Mac vs Win — the
  writer/`rle_expand`/pool-store side is clean (the D589 "pool holds the
  rev32" theory is wrong: the pool is correct).
- **Import-time sources are a clean rev32 on the Mac only.** The
  import-time `r*.bin` bins differ 347/350 on the common names, and the
  wide-pixel classes are an *exact* 32-bit byte reversal: r000 (f3 64x64)
  and r003 (f2 1x1) are **100% rev32** (Mac bin == `byteswap32` of the Win
  bin, byte 0 onward); the 8-bit CI classes show the ~50% coincidence rate
  you'd expect from single-byte texels. The GE_TEXN discriminator probe
  (added in the same work, `port/fast3d/gfx_pc.cpp`, `GE_TEXDUMP`-gated)
  shows the whole story on one line each side:

  ```
  Win: GE_TEXN[0] carray=0 raw8= 76 98 87 98 98 98 a9 bb ...  norm8==raw8
  Mac: GE_TEXN[0] carray=1 raw8= 76 98 87 98 98 98 a9 bb ...  norm8=rev32(raw8)
  ```

  Same `raw8` (the pool bytes are identical), **different `carray` verdict**
  → on the Mac `gfx_tex_normalize_source` bswaps every pool source; on
  Windows it passes the raw pointer through.

### Root cause

`gfx_tex_source_is_c_array` (`port/fast3d/gfx_pc.cpp:834`) mixes two
address spaces in one test. It stripped the window base first, putting
`a` in **N64 raw space** (a pool pointer `0x1000701994f8` → `a =
0x701994f8`), but the range bounds it compared against are
**host-space** — `PORT_DRAM_V1_BASE`/`PORT_DRAM_K0_BASE` are defined as
`PORT_ADDR_BASE + 0x70000000`/`+0x80000000` (`port/include/portaddr.h:86,89`).
At `PORT_ADDR_BASE == 0` (x86_64/Windows/Linux) the two spaces coincide so
the test is accidentally correct. At `PORT_ADDR_BASE == 0x100000000000`
(arm64 macOS) they diverge by 16 TiB: `a = 0x701994f8` is **above** the
cart window in raw space (`0x701994f8 > 0x20000000`, so the cart check
fails) and far **below** the host-space `PORT_DRAM_V1_BASE =
0x100070000000` (so both DRAM checks fail), so all three range checks miss
and the function falls through to `return true` ("C-compiled array") for
every DRAM and cart texture source. `gfx_tex_normalize_source` then
builds a stable `PD_BE32`-swapped copy of each such source, and fast3d
consumes the swapped copy — a clean rev32 of the whole wide-pixel texture
pool. That is the D589 confetti (and the D219/M-114 "ammo HUD pink"
symptom class).

The K0→V1 mirror normalisation at `gfx_pc.cpp:1285` does it right (compares
the host pointer against the host-space `PORT_DRAM_K0_BASE` directly, no
subtraction); the C-array test was the one site that stripped the base.

### Fix

`port/fast3d/gfx_pc.cpp` `gfx_tex_source_is_c_array`: compare the **host
pointer** directly against the host-space bounds (drop the
`− PORT_ADDR_BASE`), and express the cart range as
`PORT_ADDR_BASE + 0x10000000 … PORT_ADDR_BASE + 0x20000000`. Port-layer
only (no `src/game` change); a plain no-op on the N64 build and on
`PORT_ADDR_BASE==0`. Commit `9c59cbe7`.

### Verification state (at commit time)

- **Windows: no-op confirmed.** Rebuilt `build-pc` (x86_64, `ntsc-final`),
  ran the seeded Dam with `GE_TEXDUMP=1 GE_TEXRAW=1 GE_PCDUMP=150
  GE_RSEED=0x0123456789abcdef GE_QUITFRAME=1500 GE_INPUTSCRIPT=20:START`:
  `GE_TEXN[0] carray=0` (unchanged), and all **400/400** import-time
  `r*.bin` are **byte-identical** to the pre-fix capture (and PPM
  `frame_000150` regenerated at the same size). Windows behaviour is
  unchanged by the fix, as the `PORT_ADDR_BASE==0` no-op requires.
- **Mac A/B — VERIFIED 2026-10-08 (Mac back online).** Clean arm64 build
  at HEAD `25f92e09`, seeded Dam run (`GE_TEXDUMP=1 GE_TEXRAW=1
  GE_PCDUMP=150 GE_RSEED=0x0123456789abcdef GE_QUITFRAME=1500
  GE_INPUTSCRIPT=20:START`): **`GE_TEXN carray=0` on all 6 probe lines**
  (was `carray=1` pre-fix) and the Mac `raw8` bytes now equal the Win
  baseline byte-for-byte. The rev32 is gone. **But the D589 symptom is not
  fully closed** — a *second, distinct* cause surfaced (a GL-layer divergence — GL texture-object / mipmap / sampling, downstream of byte-identical fast3d texels; **D592**; the initial "import-order" framing was a camera-dependent red herring, later DISPROVEN): the frame still diverges at
  the pixel level (the rock striping; the exact `glTexImage2D` input is byte-identical Mac/Win; no pink/magenta on
  either side in the HUD region — the D219/M-114 "ammo HUD pink" class is
  *not* present; the HUD is clean). So **D591 (byte order) is
  verified-fixed; D589 stays open on D592.** Maintainer visual-sanity
  package: `scratch/d589-dam/ab3-mac/{d591_sbs_win_mac.png,
  d591_diffmap.png, D591-AB-SUMMARY.txt}`. (The earlier "400/400 bins
  byte-identical" was a *filename-based* match on the rNNN index; see D592
  for why that comparison is unreliable once the import order diverges.)

### Cross-tag

D589 (the symptom this root-causes; its "pool holds the rev32" line is
superseded by this entry), D219/M-114 (the "ammo HUD pink" symptom class
— same rev32 mechanism), D441 (the host-address model / above-4-GiB tier
whose D441 note sits on the very range this function tests), D457 / D131
(the address-space model). §A1 cross-tag added to
`docs/porting-notes.md` (diagnostic tell: a platform where
`PORT_ADDR_BASE != 0` and a range test strips the base *before* comparing
to host-space `PORT_DRAM_*` bounds ⇒ every in-window source falls through
to the C-array default and gets bswapped).
