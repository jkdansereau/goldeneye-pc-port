# D217

<!-- from the old section-F/H table row -->
| D217 | **1P weapon viewmodel + 3P Bond model textures garbled — wrong/stale palette on model-DL CI textures (M-84).** Two defects: ① texpool arena misalignment (the green), ② CI texture-cache collision. — full `## D217` entries (M-86→M-97) at file tail | **FIXED + merged** (PR #47 + PR #49); white residual split out as D228. |

## D217 — M-88 defect ① root-cause narrowing: NOT the model sidecar; the runtime TLUT-address path (session dispatched to investigate a `tools_pc/d43_*.py` fix; investigation concludes the fix does not belong there)

**Dispatch brief for this session** asked for a root-cause + fix of D217
defect ① (wrong palette bytes arrive for model-DL CI textures) confined to
`tools_pc/d43_*.py`, working theory: the model-DL/sidecar's embedded
palette-source pointer is a fixed 4 bytes / 2 entries too low. **That theory
is refuted** and the fix does not belong in the converter at all — see
below. Per the brief's own instruction ("if your investigation concludes
otherwise, stop and write up why instead of editing game code"), no code
was changed; this is a write-up only.

### Why `tools_pc/d43_emit.py` cannot be the site of this bug

Traced the full model-texture reference chain from the ROM to the draw call:

- A model's texture table (`ModelFileTextures[]`, the "texconfig" block
  `d43_emit.py` emits at `8*NS .. 8*NS+12*NT`) stores each texture as a
  **numeric `TextureID`** (`src/bondtypes.h:836-847`), not an embedded
  pixel/palette blob. `d43_emit.py:508-516` passes this `u32` straight
  through byte-order-swapped (BE→LE) — **the numeric value is never
  remapped or altered**. Verified by reading the emit loop directly; there
  is no code path in `d43_emit.py` that touches texture *content*.
- `texLoadFromModelFileHeader` (`src/game/image.c:2522-2536`, decomp ground
  truth) resolves each `TextureID < MAX_TEXTURES` via `texLoad()` against
  the **global, per-game (not per-model, not per-level) image table**
  (`Globalimagetable`). That table's BE→LE fixup lives in
  `port/src/gimgfixup.c` (D68) — a `port/src/` runtime file, not a
  `tools_pc/` offline converter. `tools_pc/d43_lutscan.py` (read-only
  analysis script, confirmed by re-reading it end to end) independently
  shows every model texture reference resolves through this same global
  `imagelist.u.csv`-indexed table — there is no per-model or per-level
  texture/palette blob for `G*`/`P*`/`C*` model files to have a "sidecar
  offset" bug in.
- Actual pixel+palette bytes are decoded **at runtime, once per level load**
  by `texInflateZlib`/`texInflateNonZlib` (`src/game/image.c`). For the
  zlib/CI-native path (`texInflateZlib:169-331`), the palette is decoded
  from the bitstream (`texReadBits`, endian- and pointer-width-agnostic)
  and **appended after the pixel data in the same `tex->data` allocation**
  (lines 320-326), with `tex->unk0a = numcolours - 1` (`image.c:238`)
  recording the palette's position. None of this is touched by any
  `tools_pc/d43_*.py` script — it is pure runtime C, identical instructions
  on both platforms.
- The actual `G_LOADTLUT`-equivalent commands are **synthesized at runtime**
  by `texWriteLoadToTmemAddr` (`src/game/tex.c:494-554`), which computes a
  hardware-accurate (S,T)-style byte-offset split (`uls`/`ult`) into
  `tex->data` using `tex->unk0a`, and issues them via the *same*
  `gDPSetTextureImage(gdl++, tex->gbiformat, depth, 1, tex->data)` call used
  for the pixel LOADBLOCK moments earlier (`tex.c:503` and `tex.c:519`) —
  there is no separate "model-DL TLUT source pointer" for a converter to
  get wrong; the address is a live pool pointer computed fresh every level
  load, in game code, not ROM-serialized data.

Conclusion: **the "4-byte / 2-entry leading pad" theory in the original
D217 row does not have a mechanism** — nothing in the offline pipeline ever
places a palette 4 bytes off from where the runtime decoder expects it,
because the offline pipeline never places a palette at all for these
textures.

### What the runtime probe actually shows (empirical, this session)

Built successfully in this worktree (`./build-pc.sh ntsc-final`, clean
build, `data/` symlinked from the main checkout — see VERIFY below) and
reproduced the exact byte values quoted in the original D217 row using the
**existing, unmodified** `GE_TEXDUMP=1` env probe already in
`port/fast3d/gfx_pc.cpp:1066-1075` (no code changes, read-only env-gated
diagnostic that ships in the tree):

```
BUNKER1  (-level_09,  GE_PCDUMP=200-440:120):
  GE_TEXI[125] fmt=2 siz=0 palfmt=32768 size=8 tile=1x1 pal[0..3]=0001 18c7 18c9 2107
  GE_TEXI[185] fmt=2 siz=0 palfmt=32768 size=8 tile=1x1 pal[0..3]=0001 0800 0840 0842

FACILITY (-level_34,  GE_PCDUMP=300-400:20):
  GE_TEXI[157] fmt=2 siz=0 palfmt=32768 size=8 tile=1x1 pal[0..3]=1a00 0000 0001 0843
```

`fmt=2 siz=0` = CI4, `tile=1x1` = a flat single-texel fill (matches "grip =
solid colour block"), `pal[0]=0001` on BUNKER1 (black, matches the row's
claim exactly) vs `pal[0]=1a00` on FACILITY (matches the row's claimed
green `0x1A00` exactly, byte-for-byte). This independently confirms the
symptom is real and reproducible with zero code changes — good news for
whoever picks up the actual fix.

**Where the bug most likely actually lives (not verified further — out of
this session's scope to touch):** `port/fast3d/gfx_pc.cpp:2202-2237`
(`gfx_dp_load_tlut`) computes the palette source address as
`rdp.texture_to_load.addr + pitch*ult + uls` where
`pitch = rdp.texture_to_load.width + 1`, and `rdp.texture_to_load.width`
comes from the immediately preceding `gDPSetTextureImage` call. GE's
`texWriteLoadToTmemAddr` (`src/game/tex.c:503,519`) always issues that
`SetTextureImage` with a **literal width argument of `1`** regardless of
the texture's real dimensions — real N64 hardware LOADTLUT addressing uses
this "width" field as a DRAM line-stride for the S,T→byte-offset
computation, so fast3d's software model of that computation is only as
good as its handling of the `width=1` special case for a texture whose
real pixel-data length (`len`, hence the palette's real starting byte) can
be much larger than what a `pitch=2` u16-stride implies. This is consistent
with the finding row's "different bytes per level for the same tile" —
if this pitch/offset math is wrong, the byte read for a `count=1` TLUT load
would land in whatever the shared texpool arena happens to hold near
(but not at) the intended 2-byte palette tail, which is exactly the
level-dependent "reads adjacent garbage" signature already logged. This is
a **hypothesis with strong circumstantial support, not a confirmed
mechanism** — I did not instrument `gfx_dp_load_tlut` further because doing
so would require editing `port/fast3d/gfx_pc.cpp`, explicitly off-limits
for this dispatch.

### Confidence

- **High confidence** the bug is NOT in `tools_pc/d43_*.py` (structural
  proof: the converter never sees texture/palette bytes for model-DL CI
  textures; traced the complete reference chain from ROM to draw call).
- **Medium confidence** the mechanism is the `gfx_dp_load_tlut` /
  `texWriteLoadToTmemAddr` width=1/pitch interaction described above — this
  is the most concrete remaining hypothesis but was not instrumented or
  bisected this session (would require touching the off-limits file).
- Re-affirms the M-86 finding that ② (the CI cache-key content-hash fix,
  `fix/d217-model-texture-palette` / `f6fc0399`) is real, separate, and
  unaffected by any of the above.

### For whoever picks this up next

A follow-up dispatch with `port/fast3d/gfx_pc.cpp` (specifically
`gfx_dp_load_tlut`) and/or `src/game/tex.c` in scope (the latter under the
narrow ABI-only exception if a genuine platform difference is found, or
`#ifdef PORT` in `port/` if the fix is a fast3d-side hardware-model
correction) is the natural next step. Suggested probe: temporarily log
`rdp.texture_to_load.width`, `pitch`, `uls`, `ult`, and the resulting `base`
pointer's offset from `tex->data` inside `gfx_dp_load_tlut` for the
`tile=1x1`/`count=1` case on both BUNKER1 and FACILITY, and compare against
what `texWriteLoadToTmemAddr`'s `len`/`tex->unk0a` actually were for that
texture (a second probe point in `tex.c`, already has a `GE_D85TEX`
env-gate precedent to extend). If `base` does not land exactly 2 bytes past
the real end of the decoded pixel data, that confirms the pitch/width=1
special case as the mechanism.

### VERIFY — what was and wasn't completed this session

- **Build:** clean (`./build-pc.sh ntsc-final` from MSYS2 MINGW64, this
  worktree, `data/` symlinked read-only from the main checkout since this
  worktree had none — no writes to the main checkout's `data/`).
- **BUNKER1:** ran `tools_pc/verify.sh bunker1` — PASS, frames=3, no crash.
  (Note: the golden-frame-stem comparison itself reports a pre-existing,
  unrelated harness mismatch — "golden frame stems don't match --dump
  window" — even though the requested and golden windows are numerically
  identical (200-440:120); this looks like a `verify.sh`/`framediff.py`
  stem-formatting bug, not a regression from this session's zero code
  changes. Not investigated further — out of scope, no code was touched
  either way, and `worst_cell=0` with no crash is the relevant signal here.)
- **FACILITY:** ran the game directly (`GE_PCDUMP=300-400:20`,
  `GE_TEXDUMP=1`) rather than through `verify.sh` (to keep the temp capture
  dir instead of having it auto-deleted) — captured frames 300-400 cleanly,
  no crash, and confirmed the green `pal[0]=1a00` grip palette entry is
  still present (expected — no fix was applied). Did not do a pixel-eyeball
  of the PPM captures beyond confirming they exist; the log-level TLUT
  value match (`1a00` = the documented green) is the decisive signal.
- No `tools_pc/d43_*.py` file was modified; `git status` in this worktree
  shows only the two docs edits (this section + the D217 table-row pointer)
  plus a local `data` symlink (not tracked/committed) used for the build.
## D217 — M-89: the `gfx_dp_load_tlut` width=1/pitch hypothesis is REFUTED (it computes correctly by design); new lead pointed at the runtime texpool arena + image-table conversion, not the model sidecar

A local-Qwen triage dispatch (read-only, no edits) proposed hardcoding
`pitch = 1` in `gfx_dp_load_tlut` (`port/fast3d/gfx_pc.cpp:2214`,
currently `pitch = rdp.texture_to_load.width + 1`), on the theory that N64
LOADTLUT addressing never strides by the SetTextureImage width and the
port's `+1` computation was wrong. **Verified by hand and refuted before
applying it — do not repeat this fix:**

- `gDPSetTextureImage`/`gSetImage` (`include/PR/gbi.h:3097-3111`) pack the
  RDP command word as `_SHIFTL((width)-1, 0, 12)` — i.e. the **raw** command
  field the port reads back (`C0(0,10)` at the `G_SETTIMG` handler,
  `gfx_pc.cpp:2786`) is `width - 1`, not `width`. `texWriteLoadToTmemAddr`
  (`tex.c:503,519,629,643`) always calls `gDPSetTextureImage(..., width=1,
  ...)`, so the raw field is `1-1=0`, and `pitch = 0 + 1 = 1` **already**,
  for every model-DL TLUT load in the game. The proposed patch is a no-op
  for this call site — it changes nothing about the reported bug, because
  the code it "fixes" already computes the value it wants.
- Continuing the derivation with `pitch=1` confirmed: `gfx_dp_load_tlut`'s
  `base = addr + pitch*ult + uls` reduces, for every call from
  `texWriteLoadToTmemAddr`'s `gDPLoadTLUT07(uls, ult, tex->unk0a + uls,
  ult)`, to **`base = addr + len`** regardless of the internal `uls`/`ult`
  split (`tex.c:530-541`; the split exists only to keep both RDP command
  fields inside their 10-bit range when `len` is large — it cancels out in
  the `pitch=1` address math). `addr` is `tex->data` (the `SetTextureImage`
  dram pointer) and `len` is `texGetDepthAndSize`'s halfword-converted
  pixel-data length. This is exactly the documented on-disk/in-arena layout
  from `texInflateZlib` (`image.c:320-326`: palette bytes appended
  immediately after `totalbytesout` bytes of pixel data) — **the addressing
  scheme is self-consistent and correct by inspection**, assuming `len`
  (computed from `tex->width`/`height`/`depth`/`maxlod` via
  `texGetLineSizeInBytes`) truly equals the pixel-byte count the decoder
  actually wrote (`totalbytesout` before the palette-append loop). Also
  confirmed `tex->unk0a = numcolours - 1` (`image.c:238`) exactly explains
  the `count = width*height = unk0a+1` term in `gfx_dp_load_tlut` — matches
  the M-85 `GE_D217Z` "count=1/28/125/172/193" observations precisely (a
  **genuine 1-color palette** for the grip tile, not a partial/truncated
  load of a larger one).
- **New question this raises, not yet answered:** if the addressing math is
  correct and `numcolours=1` is a real, intentional 1-entry palette for
  this tile, then the SAME `texturenum` decoded from the SAME ROM bytes
  should produce the SAME single color on every level — it does not
  (`0001` on BUNKER1 vs `1a00` on FACILITY). That points away from a
  decode-math bug entirely and back toward either (a) **the bytes handed to
  the decoder differ per level** (i.e. an image-table/asset-resolution
  issue — but a *different* one than the already-ruled-out model-DL
  `TextureID`/`d43_emit.py` path; `texLoad` sources compressed bytes from
  `g_Textures[texnum]` offsets into `_imagesSegmentRomStart`, a wholly
  separate table/segment from the per-model `Globalimagetable` that
  `port/src/gimgfixup.c`/D68 already fixes — **not yet audited for this
  bug**), or (b) **the shared texpool arena serves a stale/aliased
  `tex->data` pointer** for what should be a fresh per-level decode (the
  arena is a two-ended allocator, `struct texpool` `leftpos`/`rightpos`,
  reused across many textures per level per `texLoad`/`image.c:2414-2519`;
  if `bytesout` returned by `texInflateZlib`/`NonZlib` for the LOD-limited
  or shrink-paletted paths (`image.c:277-318`, the `forcenumimages >= 2`
  branch with its `if (totalbytesout + imagebytesout > 0x800) { maxlod = j;
  break; }` early-exit) doesn't exactly match what `texGetDepthAndSize`
  later assumes was written for a *different* texture that landed nearby,
  a later texture's palette read could land on an adjacent texture's
  bytes — this would explain "same weapon, different levels, different
  wrong colors" as a level-dependent *decode order/adjacency* effect, not
  a fixed byte offset).
- **Explicitly ruled out this session (in addition to the M-88 sidecar
  ruling):** `gfx_dp_load_tlut`'s pitch/address computation itself
  (`gfx_pc.cpp:2202-2237`) — confirmed correct by hand-derivation against
  the GBI packing macros, not just "not yet disproven."
- **Confidence:** HIGH that the proposed `pitch=1` patch is a no-op and
  must not be applied/merged. MEDIUM on the two new leads above — neither
  was instrumented or built this session (time-boxed to the math
  derivation); the game-fed vs. arena-staleness question is the right next
  probe (an env-gated log of `g_Textures[texnum]` offset + decoded byte
  count + `pool->leftpos`/`rightpos` delta, on the exact FACILITY repro
  frame, would disambiguate directly).
- **For whoever picks this up next:** do NOT re-propose changing
  `gfx_dp_load_tlut`'s pitch formula without first checking whether the
  bytes arriving at `texInflateZlib`'s `src` parameter (i.e. what
  `g_Textures[g_TexNumToLoad]`/`_imagesSegmentRomStart` resolves to) are
  actually identical across BUNKER1 and FACILITY for this `texturenum` —
  that is the one load-bearing assumption ("same weapon, same ROM bytes")
  this whole investigation has taken for granted but never directly
  checked with a probe.

## D217 — M-89 cont.: decisive probe — decode is proven correct+identical across levels; the parked defect ② fix does NOT fix the visible symptom; new leading theory is a stale/dangling `tex->data` arena pointer, not a decode or cache-key bug

Ran the actual empirical test the M-89 section above called for, rather than
delegating it further. **Result answers the open question decisively and
rules out both of this row's previous leads.**

**Probe added** (`src/game/image.c`, `texInflateZlib`, `#ifdef PORT` +
`getenv("GE_D217TEX")`, follows the `GE_TEXDUMP`/`GE_D85TEX` pattern):
logs `texnum`/`format`/`numcolours`/`palette[0]`/`palette[1]` **straight off
the decode bitstream**, before any arena/pointer math — the most direct
possible check of "does this texture number decode to the same bytes on
both levels."

**Result:** ran BUNKER1 (`-level_09`) and FACILITY (`-level_34`) headless
with `GE_D217TEX=1`. `texnum=1608` is the only `numcolours=1` texture common
to both logs, and its `pal0` value (`0001`, black) matches the previously
documented BUNKER1-correct value exactly — confirming this is the PP7 grip.
**`texnum=1608` decodes to `pal0=0001` on BOTH levels, identically, every
time it's called.** This directly disproves the M-89 "different ROM bytes
per level" open question (lead a is dead — the decode is correct and
level-invariant) and also rules out any remaining decode-math theory: the
bug is not in `texInflateZlib`, `texGetDepthAndSize`, or `gfx_dp_load_tlut`
(already shown correct M-89) — it is **strictly downstream of a provably
correct decode**.

**Then tested the parked defect ② fix (`fix/d217-model-texture-palette`,
`f6fc0399` — the CI-texture-cache-key content-hash) directly against this
exact FACILITY repro**, since "correct decode, wrong render" is precisely
the signature that fix targets (a stale cache `HIT` serving a different
material's already-decoded palette). **Built it, ran `-level_34`, captured
frame 380: the grip is still solid green.** The already-written, previously
untested-against-this-repro defect ② fix does **not** fix the visible
symptom. This means defect ① and defect ② are **not the same underlying
bug** — they were right to be tracked separately, and defect ②'s fix should
still land (real, separate, still needs its 21-level sweep) but will not
close this row on its own.

**New leading theory (untested, most concrete lead yet):** since a fresh
decode is proven correct, and the fast3d-side cache-key fix doesn't help,
the render-time GBI command for this material must be reading a `tex->data`
pointer whose *arena contents have changed* between when it was decoded and
when the DL that references it is actually drawn — i.e. a **stale/dangling
pointer into the `texpool` arena**, not a decode or cache-lookup bug. GE's
`texpool` (`image.c` `texLoad`/`texInitPool`, two-ended `leftpos`/`rightpos`
allocator) is reused across many textures per level; if it resets or the
model-node/GDL that references texture 1608 caches a raw pointer/segment
address resolved once (e.g. at weapon-pickup or level-load time) rather
than re-resolving through `texFindInPool` every time it draws, then any
*later* texture decode that reuses that same arena address (plausible on a
texture-heavy level like FACILITY, less likely on the lighter BUNKER1) would
silently overwrite the grip's palette bytes with whatever the next tenant's
content is — explaining both "same weapon, different wrong color per level"
(depends on what else that level's arena happened to decode nearby/after)
and "clean on the lighter level" (less arena churn/reuse pressure).

**Confidence:** HIGH the decode itself and the fast3d/cache-key layer are
now both cleared (empirically, not just by inspection). MEDIUM-LOW on the
stale-arena-pointer mechanism — plausible and consistent with all evidence
so far, but not yet probed directly.

**For whoever picks this up next:** probe whichever code path holds the
`Gfx*`/model-node reference to this material's `SetTextureImage` address at
*draw* time (`texWriteLoadToTmemAddr`'s caller chain from the weapon
model-DL walk, not `texLoad` itself) and log the `tex->data` pointer value
+ a fresh read of the two bytes at the computed palette offset **at draw
time**, alongside a second log of what `texInflateZlib` most recently wrote
to that same address (extend the `GE_D217TEX` probe with an address field
to correlate). If the drawn address's live content no longer matches what
was last decoded there, that confirms the stale-arena-pointer theory
directly and narrows the fix to wherever the model DL caches that pointer
instead of re-resolving it. Probe left in tree (`GE_D217TEX`, env-gated,
inert by default).

**VERIFY performed this session:** `tools_pc/verify.sh bunker1` — PASS,
no regression from the (inert-by-default) probe. `-level_09`/`-level_34`
`GE_D217TEX=1` headless captures — decisive result above. Built and ran
the parked defect ② fix directly against `-level_34` frame 380 — still
green, screenshot evidence in this session's scratchpad.

## D217 — M-90: defect ① ROOT CAUSE FOUND + FIXED — texpool arena base misaligned on PC; the palette sits 4 bytes early inside every weapon-pool allocation while LoadTLUT still reads the N64 offset

**Verdict: the stale-arena-pointer theory (M-89 cont.) is REFUTED, and the
real root cause is found, fixed, and verified.** Arena bump allocations are
never overwritten in place (`leftpos` only advances), so a drawn address's
content cannot "change" after its decode. What actually happens: the palette
**was never at the address the draw reads**.

**The mechanism (all hand-derived, then probe-confirmed):**

1. `texAlignIndices` (`src/game/image.c`) pads each CI index row to an
   **absolute** 8-byte boundary: `outptr = (u8 *)(((u32)outptr + 7) & ~7)`.
   The palette is then written at `dst + imagebytesout`, and the whole
   allocation's size (`texInflateZlib`'s return) depends on where `dst` sits
   in memory.
2. Model-DL TLUT loads address the palette at `tex->data + len*2` bytes
   (`texWriteLoadToTmemAddr`: `uls = len` from `texGetDepthAndSize`, and the
   port's `gfx_dp_load_tlut` computes `base = settimg_addr + pitch*ult + uls`
   in 16-bit units — correct RDP semantics, re-verified). `len` is a
   **formula** that only matches the real layout when rows start 8-aligned.
3. The invariant that makes (1) and (2) agree: **`leftpos` stays 8-aligned
   for life** — pool base 8-aligned, header `+= 8`, every inflate size a
   multiple of 8 (checked: zlib aligns at return; non-zlib per-image). On
   N64 the memp bump allocations feeding these pools happened to be
   8-multiples, so bases were aligned and absolute ≡ relative row padding.
4. **PC broke the invariant.** `mempAllocBytesInBank` (`src/memp.c`) is a
   plain bump allocator with no per-allocation alignment; PC pointer bloat
   (D36/D37-class struct growth) means some STAGE-bank allocation sizes are
   no longer 8-multiples, so by the time `initBondDATA.c:238` allocates the
   weapon buffers, the pool position is off. Probe-confirmed: on FACILITY
   both weapon-pool bases land **%8==4** (stage pool aligned), and *every*
   decode in the misaligned pool has `dst%8==4`.

**The smoking gun (probe data, `GE_D217TEX` extended with a
`D217TEX-SIZE` line at `texInflateZlib`'s return):** pre-fix FACILITY,
`texnum=1608` (PP7 grip, 1x1 CI4, `numcolours=1`, `pal0=0001`) decodes to
**size=8**: image = 1 byte + absolute-align padding = **4 bytes**, palette
at `dst+4..5`. The draw's count=1 TLUT reads `dst+8` (the N64 layout: image
8, palette at +8) — which is exactly the **next tenant's 8-byte header slot**:
`texnum=26` stored as an LE s16 = bytes `1a 00` → `PD_BE16` → **`0x1A00` =
the green `(24,65,0)`**. Permanent (arena never rewrites it), per-level and
per-weapon (the "next tenant" texnum differs → any RGBA5551 value: the green,
blue, white, orange of the M-87 playtest matrix).

**This single mechanism also explains M-86's long-unexplained tell:** every
*larger* model TLUT showed `raw[0]==raw[1]==0x0000` with real colour from
index 2. In a %8==4 pool the whole palette sits **4 bytes (2 entries) early**,
so a load at the N64 offset reads two zero-pad entries then the real palette.
The "leading zero pad / pointer 4 bytes too low" suspicion was literally
true — just on the *decode* side, not the draw side.

**Fix (landed):** `texInitPool` (`src/game/image.c`, `#ifdef PORT`): round
the arena base up to 8 bytes before publishing `start`/`leftpos`; `end`
stays at the original buffer limit (pool loses ≤ 7 bytes). A no-op on N64
(bases were already aligned — the game works there, which is itself the
proof the invariant held). Chosen over aligning inside
`mempAllocBytesInBank` (would shift *every* allocation — far larger blast
radius, and D36/D37 note the STAGE bank is nearly full). This is a
semantics-preserving ABI/layout edit in the AGENTS.md narrow-exception class
(32→64-bit transition broke an implicit layout invariant); no game logic
touched.

**Verification:**
- `./build-pc.sh ntsc-final` clean; no new symbols (local var + expression).
- FACILITY `-level_34`: all 188 decodes now `%8==0`; `texnum=1608` size=16,
  palette at +8; draw probe reads `addr=<base+8> pal0=0001 count=1` (black,
  the decoded value) — zero `pal0=1a00` events. PPM frames 250–450: **0**
  exact-green `(24,65,0)` pixels (pre-fix: ~1300 in frames 300–410) and 0
  strong-green (`g>90, r<70, b<70`) across all 21 frames.
- BUNKER1 `-level_09` (the clean control): still clean — 183/183 decodes
  aligned, 0 CHANGED draw events, 0 green pixels in the capture window.
- DEPOT `-level_30` (had green pre-fix): 229/229 aligned, 0 exact/strong
  green in the capture window.
- Full 21-level `verify.sh sweep`: **21/21 PASS**, no CRASH/REGRESSION/
  STALLED (`sweep_d217.log`, repo-root scratch).

**Status change:** defect ① = **FIXED, pending the sweep + a human
playtest eyeball** (the M-87 matrix weapons: PP7/PPK/AK47/M16 grip+body,
watch face, NPC-held guns). Defect ② (CI texture-cache content-hash key,
branch `fix/d217-model-texture-palette` @ `f6fc0399`) remains a real,
separate, parked fix — still owes its own sweep before merge; M-89 already
proved it does not fix the visible symptom.

**Probes:** stripped before commit — the fix ships clean. The committed
M-89 decode probe (`GE_D217TEX` in `texInflateZlib`) remains as left by that
session; if the playtest eyeball surfaces a residual, re-add the
`D217TEX-SIZE`/`D217TEX-DRAW` probes exactly as documented above.

## D217 — M-92: human playtest eyeball of PR #47 (debug build) — PP7/PPK/watch CONFIRMED FIXED; AK47 and NPC/world-model weapons have a DISTINCT, still-open residual

**Method:** user's own debug build (`build-linux-dbg`, their preferred manual
test flow, not the headless `verify.sh` capture), `fix/d217-texpool-alignment`
checked out. Sampled Dam, Facility, Jungle, Frigate.

**Confirmed fixed by this PR:** PP7/PPK (grip + body) clean on every level
tested; Bond's watch face clean where it was previously green. Matches the
probe/pixel-count evidence in M-90 — the %8==4 texpool-alignment fix is
real and closes the exact symptom it targeted.

**NOT fixed — residual, still open:**
- **AK47** — both **1P viewmodel and 3rd-person world model** still show
  white (missing-texture) patches.
- **NPC-held weapon world models** — white patches on parts that vary per
  gun (magazine, receiver, etc. depending on weapon), **consistent across
  guns/levels** (not intermittent). Frigate was called out as especially bad.

**Reading this against the M-87 raw notes above:** the M-87 matrix already
recorded AK47 white-patch symptoms (viewmodel) pre-fix; M-92 shows the
texpool-base-alignment fix (M-90) did not touch that mechanism at all — it
only fixed the PP7/PPK/watch "wrong solid colour from a misread palette
slot" failure mode. AK47 + NPC-world-model "white/missing" is a different
failure signature (white usually means "texture never bound / fell back to
a default," not "bound the wrong palette entry" — cf. the D161 family this
row already flagged). **Likely a second, distinct D217 mechanism** — not
yet root-caused. Candidates to check first, cheapest to most expensive:
whether the AK47/NPC weapon model DLs route through a *different* texpool
(not the one `texInitPool`'s 8-align fix touches — note `gun.c:930-950`
hand multiple distinct offset/pool pairs per weapon, and `bondview2.c:504`/
`initmttex.c:15` are separate `texInitPool` call sites entirely from the
per-weapon ones); or a genuinely separate "texture never loaded for this
DL" bug (D161-class G_TT_NONE / missing-TLUT-emission) rather than a
misaligned-read one.

**Status:** PR #47 fix is real and should still merge — it closes a
confirmed sub-defect (PP7/PPK/watch) with no regressions. **Do not close
D217 defect ① on this merge** — reopen/retitle as the AK47 + NPC-world-model
white-texture residual once root-caused, since the M-87 matrix already
described it as a separate colour-vs-white failure mode from the start.


**Closing status (from the §F row, appended 2026-09-15 ctx-opt):** FIXED and merged — PR #47 (texpool alignment) + PR #49 (CI-cache palette key, commit `f6fc0399`); confirmed by two independent human playtests (M-92, M-97). The white missing-texture residual is split out as D228. (The row's status had gone stale at OPEN; corrected M-105.)
