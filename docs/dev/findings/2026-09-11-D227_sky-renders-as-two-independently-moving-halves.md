# D227

<!-- from the old section-F/H table row -->
| D227 | **Sky renders as two independently-moving halves — "two sky processes" (M-93 QA; root cause M-100b: unit error, `unk20`/`unk24` are S10.5 not texels).** — full `## D227` entries (M-94→M-100b) at file tail | **FIXED (M-100b, PR #43)** — also fixed "scrolls too fast" (M-96) + over-tiling; human play-test confirmed. Full history in the sections. |

## D227 — M-94: visual confirmation + a concrete mechanistic hypothesis for the "two sky processes" seam — per-primitive `wScale` quantization in D176(a)'s fix, not yet confirmed or fixed

**Method.** Headless capture, `-level_22` (Statue), `GE_PCDUMP` at frames
300/330/345/350/355/360, `land/d176a-sky-perspective-scroll` built and run
directly (not through `verify.sh`, to keep the PPM frames instead of having
them auto-deleted). Converted to PNG and cropped to the sky band with PIL for
close inspection.

**What the capture shows.** Frame 345's sky band has a clearly visible
crease: the cloud streak pattern on the right portion of the frame breaks
continuity from the pattern on the left, with a faint diagonal artifact line
right at the boundary — this is a single static frame, so it rules out "just
normal camera pan across a busy texture" as the explanation for at least this
instance. Screenshot: `docs/img/bugs/d227-sky-seam-statue.png`.

**Hypothesis.** D176(a)'s fix (`skyPortRenderPoly`, `src/game/sky.c`)
computes a per-call `wScale = maxAbsW / 30000` from the vertices passed to
*that one call*. But `skyRenderTri`/`skyRenderFull` issue **up to 5 separate
`skyPortRenderPoly` calls per frame**, one per triangle in a 5-vertex sky fan
(`sky.c:900-920` and `:1372-1393`) — so each triangle in the fan can pick a
different `wScale` depending on its own 3 vertices' camera-space `w`
(`unk0c`).

The position math itself is exact for any `wScale > 0` (re-derived
algebraically: `clip.x = ob[0]·wScale = (ndcX·w/wScale)·wScale = ndcX·w`,
independent of `wScale` — this was already verified when the D176a fix was
first reviewed this session). So this is not a bug in the fix's core
logic. The likely mechanism is **quantization**: `Vtx.ob[]` is `s16`, so
`ndcX·wv` (where `wv = w/wScale`) rounds to a 16-bit integer *at a
granularity that depends on `wScale`*. Two triangles sharing an edge each
call `skyPortRenderPoly` independently, each picking its own `wScale` from
its own 3 vertices — so a vertex that is logically shared between two
triangles can round to two slightly different quantized `ob[]` values
depending on which triangle's call computed it, cracking the shared edge by
a small but visible amount. That reads exactly like "two regions, different
pattern" without requiring two draw calls, two cloud layers, or any
screen-half-specific logic — a single quantization granularity mismatch
between neighboring primitives is sufficient.

**Not yet confirmed.** This explains the static crease. It does **not** yet
confirm the user's "each half moves at a different rate" observation — that
would require isolating camera motion from sky-texture scroll (a
static-camera, sim-time-only comparison), which this session's captures
didn't attempt. The user's original alternate theory (a widescreen/aspect-
ratio rendering artifact, unrelated to game logic) is also not ruled out —
it could coexist with or be independent of the quantization crease.

**Fix direction, if this hypothesis holds:** compute one `wScale` shared
across the *entire* sky fan for the frame (max `unk0c` over every vertex in
every poly about to be drawn, not just the 3 vertices of one
`skyPortRenderPoly` call), then pass it into each call instead of
recomputing locally. Every triangle's vertices would then quantize against
the same scale, and shared edges should match exactly. Not attempted this
session — flagging the direction for whoever picks this up, along with the
static-camera test needed to confirm before spending a build cycle on it.

**Housekeeping:** capture files (`ppm/`, a `ppm2/` scratch dir, and the run
logs) were cleaned up after pulling the one evidence screenshot; nothing
else from this session's captures is left in the tree.

**M-94 addendum — static-camera confirmation, same session.** `-level_22`'s
fixed level-title cutscene ("Statue Park, St. Petersburg") holds a genuinely
static camera (verified: the foreground statue/pedestal region differs by
only mean ~3.0/255 between frames 30 and 90 — lighting/AA noise, no
detectable pan/zoom) while the sky region above it differs by mean ~15.9/255
over the same two frames. **This confirms the sky animates over time,
independent of camera motion** — it is not merely a static geometric crease
that only *looks* like motion under a moving camera. That said, the diff
magnitude was non-monotonic across the sampled frames (30→45 measured larger
than 30→90), which doesn't cleanly fit a simple constant-rate linear scroll
either — worth another look with a denser, longer static-camera sample
before concluding *how* it's animating. Does not distinguish between "one
sky animating uniformly past a static quantization seam" (this session's
leading hypothesis) and "two separately-animating regions" (the user's
original read) — either is still consistent with a real per-frame diff in
the sky region. Capture files cleaned up after taking these measurements;
nothing added to `docs/img/bugs/` beyond the one screenshot already
committed.

## D227 — M-100b: FIXED. The real cause is a unit error (`unk20`/`unk24` are S10.5, not texels); the s16 overflow was its symptom, and so are the over-tiling and the "scrolls too fast" complaint

**This supersedes the fix design in M-100 below.** M-100's measurement and its
overflow diagnosis were right; its *explanation* of why the numbers were so
large was not. Neither tessellation nor the tile shift turned out to be needed.

**The unit bug.** M-82 ("D176(a) Defect 1") read `unk20`/`unk24` as texel
counts and therefore baked `tc` as `(S - fold) * 32` to convert texels into
`tc`'s S10.5 units. They are not texel counts. `skyRender` sets them as
`worldpos * 0.1f` (the `sp4b4[].unk0c`/`unk10` assignments) and the N64 path
hands them to the RDP's coefficient block unscaled (`skyRenderTri`:
`S * unk34 * sp368`), so they are **already in the RDP's native S10.5
1/32-texel units**. The stray `* 32` made every sky texture coordinate 32x too
large, which produced three symptoms tracked until now as separate bugs:

1. **D227 itself.** Spans of ~30,000 *texels* against an `s16` `tc` holding
   +/-1024 -> 7 of 8 coordinates overflowed and wrapped -> starburst / "two
   sky processes".
2. **Over-tiling** — ~466 repeats of the 64-texel cloud across one quad
   instead of ~15.
3. **"The N64 sky is nearly static, ours races"** (M-96), measured at
   ~7 px/tick in M-98. `g_SkyCloudOffset` advances 1 unit/tick, which is
   1/32 texel — not 1 texel. **The M-96 scroll complaint and D227 are the same
   bug**, which is why M-98's re-derivation of the tick path kept coming back
   clean: the accumulator was never wrong, its unit was.

**The fix:** `tc = (S - fold)`, 1:1, with the fold rebasing to a whole tile
period (`SKY_TC_WRAP` = 64 texels x 32 = 2048 units). The ~30,000-unit span
then fits an `s16` unaided — which is also why the original N64 code needs no
tessellation here: it never had a range problem to solve.

**How the intermediate wrong turn read on screen, for the record.** Building
the tile-shift version first (correct overflow handling, still-wrong units)
removed the starburst and the diagonal split completely but left the sky a
dense fine moire — the *intended* mapping at 32x the intended scale. That is
what identified the unit error; the user independently flagged the same thing
on sight ("background looks like it tiles too small... sky i mean").

**Verified** (`-level_41` Cradle frame 350, the M-98 starburst repro):
`docs/img/bugs/d227-cradle-fixed-m100.png` — soft cloud bands, correct scale,
correct perspective compression toward the horizon, no starburst, no diagonal
split, no moire; matches the character of the emulator reference. `GE_D227V`
confirms `kS = kT = 0` (no shift needed) and every `tc` in range, max 31,513
of 32,767. `-level_22` Statue frame 345: clean horizon cloud band, no seam.
`tools_pc/verify.sh bunker1`: **PASS**, `worst_cell = 10.32` (the known
stale-golden-baseline number, unaffected by sky work). Surface1 frame 345 has
almost no sky in frame at that camera, so it is not evidence either way — the
user's own play-test screenshot remains the Surface evidence.

**What was kept, and why.** M-95's shared `wScale` and M-98's shared fold both
stay — they were real bugs, and per-fan consistency is still what keeps a
fan's shared vertices bit-identical between its primitives. The tile-shift
machinery (`skyPortPickShift` / `skyPortCaptureTile` / `skyPortEmitTileShift`)
also stays as a **safety valve**: the observed span is 31,513 against a 32,767
limit, only ~4% of headroom, so a camera that exceeds it is not far-fetched,
and the mechanism degrades gracefully instead of overflowing. It is not
untested code — the intermediate build above ran it at `k = 5` and it worked
correctly (coherent mapping; the wrong scale there was the unrelated unit
bug). It uses the RDP's own tile `shifts`/`shiftt`, which `port/fast3d`
already implements (`gfx_pc.cpp` ~1783), so it needs no fast3d change.
`skyPortCaptureTile` replays the exact `G_SETTILE` that `texSelect` emitted
with only the shift fields patched, rather than rebuilding it, so it cannot
drift from `texSelect`'s format/line/mask derivation.

**Still owed:** a human play-test. Every previous D227 "fix" (M-95, M-98)
passed a static headless check and then failed live, so this does not count as
closed until it is played — though unlike those two, this one has a mechanism
that explains every reported symptom, and the user has eyeballed the fixed
frame. Also worth re-measuring the scroll rate against M-98's ~7 px/tick
baseline: it should now be ~0.2 px/tick.

**Human play-test (from the §F row, appended 2026-09-15 ctx-opt; post-M-100b):** confirmed by the user on
the previously-failing levels — closes the "still owed" above.

## D227 — M-100: ROOT CAUSE FOUND AND PROVEN — `s16` texture-coordinate overflow. Not a crack, not view-dependence, not a quantization mismatch. Fix not yet implemented (design decision open).

**Status: root cause proven by direct measurement + a confirming diagnostic
build. D227, D176(a) "Defect 2", and the M-98 "Cradle tower starburst" are
all the same defect.** M-95 (shared `wScale`) and M-98 (shared S/T fold) each
fixed a real but *second-order* bug; neither could have fixed this one.

**The unblocking evidence was a human screenshot pair** (user supplied, same
level / weapon / ammo count / near-identical framing, Surface):
`docs/img/bugs/d227-surface-emulator-ref.png` (1964 emulator — soft, blobby,
near-isotropic clouds) vs `docs/img/bugs/d227-surface-starburst-ours.png`
(our build — clouds smeared into long streaks radiating from a point, with
sharp *radial* boundaries). A radial starburst is not a crack between two
mis-quantized vertices, which is what M-94→M-98 had been chasing.

**M-99's "blocked, needs a human co-session" conclusion was wrong, and cost a
session.** The defect reproduces headlessly and *statically* — M-98 had
already captured it at Cradle frame 350
(`docs/img/bugs/d176a-cradle-tower-starburst.png`) and even confirmed frames
350/380 pixel-identical, but filed it as "pre-existing Defect 2, not D227."
That misclassification is what made D227 look view-dependent-and-unreachable.
Note the Cradle capture matches the *original* D227 report almost word for
word: left half starburst, right half normal horizontal streaks, hard
boundary between them — "each half of the screen shows a different sky
pattern."

**Measurement (new `GE_D227V` probe, `skyPortRenderPoly`, env-gated in the
same style as `GE_D176`).** `-level_41`, the frame-350 camera. The sky is one
`nverts=4` quad, corners TL/TR/BL/BR:

| i | screen x,y | w (`unk0c`) | S (`unk20`) | T (`unk24`) |
|---|---|---|---|---|
| 0 | 0.0, 10.0 | 6457.0 | 236.7 | 489.3 |
| 1 | 319.8, 10.0 | 6457.0 | -658.1 | -122.8 |
| 2 | 0.0, 212.9 | 215228.8 | 1422.2 | 29806.0 |
| 3 | 319.8, 212.9 | 215277.7 | -28408.5 | 9406.1 |

All four w are **positive** — no vertex behind the camera, no sign change, so
no projective pole (a hypothesis worth ruling out; it is ruled out).

The span across this single primitive is **29,830 texels in S and 29,929 in
T**. `Vtx.tc` is S10.5 (1/32-texel units) in an `s16`, so one primitive can
represent at most **±1024 texels**. The fold (`floor(min/64)*64`) rebases the
minimum vertex but cannot compress a span 29x larger than the entire
representable range. Computing `(S - foldS) * 32` for these four vertices
(`foldS = -28416`, `foldT = -128`):

| i | tc S exact | → `s16` | tc T exact | → `s16` |
|---|---|---|---|---|
| 0 | 916,886 | **-618** | 19,754 | 19,753 ok |
| 1 | 888,253 | **-29,252** | 166 | 166 ok |
| 2 | 954,822 | **-28,218** | 957,888 | **-25,152** |
| 3 | 240 ok | 240 | 305,091 | **-22,589** |

**Seven of the eight texture coordinates overflow and wrap to essentially
arbitrary values.** The rasteriser then interpolates between wrapped
garbage — which is exactly a starburst.

**Why it reads as "two halves that differ, split by a line."** The quad is
emitted as `gSP2Triangles(0, 1, 3, 0, 3, 2)` — split on the **0-3 diagonal**.
Vertices 0 and 3 are precisely the two that each keep *one* coordinate in
range, so the two triangles rasterise different mixes of wrapped values,
divided by that diagonal. This also explains the reported view-dependence
without any view-dependent mechanism: S/T are functions of view, so the
wrap point moves as you look around, and the garbage pattern changes with it.

**Confirming diagnostic (decisive).** Rebuilt with `tc` baked at 1-texel
instead of 1/32-texel granularity (32x the range, deliberately *wrong*
texture scale, nothing else changed), same `-level_41` frame 350:
`docs/img/bugs/d227-cradle-tcscale-diagnostic.png`. The starburst and the
two-halves split are **completely gone**; the sky is smooth and coherent and
structurally matches the emulator reference. The texture is over-zoomed, as
expected for a deliberately wrong scale. **s16 tc overflow is the whole
mechanism** — there is no residual second defect hiding behind it.

**Consequences for the existing record:**
- D176(a) "Defect 2" (adaptive tessellation of wide-span sky triangles) is
  not a separate cosmetic nicety — it *is* D227, and it is the dominant sky
  defect on every affected level.
- The `skyPortRenderPoly` code comment predicted this exactly ("Triangles
  whose S/T span exceeds ~1000 texels ... still need Defect-2's finer
  tessellation") but the severity was underestimated: it is not a
  horizon-only artifact, it corrupts the entire sky quad.
- M-95's and M-98's shared-`wScale`/shared-fold fixes remain correct and
  should stay: once spans are small enough for tc to be in range, per-fan
  consistency is exactly what keeps sub-primitive boundaries seamless.
- The M-96 "sky scrolls extremely fast" complaint and M-98's measured
  ~7 px/tick are plausibly the *same* bug (wrapped tc makes the apparent
  pattern move at a rate unrelated to the true 1 texel/tick scroll), but
  that is not yet confirmed — recheck after the fix.

**Fix not implemented — one design decision is genuinely open.** The fix is
to subdivide until each sub-primitive's S/T span is under ~960 texels, with
per-sub-primitive folds (folds differing by multiples of 64 texels are
seamless under `GL_REPEAT` at tile width 64, which is what makes subdivision
safe). Two things are already worked out:

- **Exact subdivision math.** Interpolate `(1/w, S/w, T/w, colour/w)`
  bilinearly in the quad's `(u,v)` and divide through — exact for any quad,
  because affine-in-screen functions commute with affine combinations whose
  weights sum to 1 (true of bilinear weights). A triangle is handled as a
  degenerate quad with the `v=1` edge collapsed, also exact.
- **Where to place the cuts.** Along an edge, `1/w` is affine in screen, so
  `S = (a + b·(1/w))/(1/w) = b + a·w` — **S is linear in w**. Subdividing
  *uniformly in w* therefore gives uniform S/T steps, and the cut count is
  just `ceil(span / threshold)`. Uniform-in-*screen* subdivision is badly
  wrong here: on this quad's right edge, half the screen distance carries
  807 texels and the other half carries 26,943.

**The open decision is the vertex budget.** `dynAllocateVertices` is an
unchecked bump allocator over a per-frame pool of `-mvtx50` = 50 KB (3,200
`Vtx`), shared with the whole scene; overrunning it corrupts memory silently.
A pure-tessellation fix needs ~32 divisions per axis at this camera, i.e.
~1,200-1,300 verts (~20 KB, ~40% of the pool) even with vertex sharing and
2-row `gSPVertex` batches (fast3d's `MAX_VERTICES` is 128, so batching is not
the constraint). Options:
- **A. Pure tessellation.** No port/fast3d change; costs ~40% of the frame's
  vertex pool on sky alone. Self-limiting via `dynGetFreeVtx()` with
  graceful degradation (slightly-over-threshold cells in extreme views).
- **B. Widen the texcoord transport in `port/fast3d/`** so tc carries more
  range, then only ~4x4 subdivision is needed (~100 verts). Cheap at
  runtime; touches the RSP emulation, which is port-layer and therefore
  permitted, but is a wider blast radius.
- **C. Hybrid** — a modest fast3d widening plus modest tessellation.
- **D. RDP tile shift (found last, and it dominates A/B/C — recommended).**
  The RDP already has a native mechanism for trading texcoord precision
  against range: the tile descriptor's `shifts`/`shiftt`. **`port/fast3d/`
  implements it fully, including the left-shift case** (`gfx_pc.cpp` ~1783:
  `shifts <= 10` divides, `shifts >= 11` multiplies by `1 << (16 - shifts)`).
  So emit tc at `32 / 2^k` units instead of 32 and set `shifts = 16 - k` to
  multiply it back — **2^k more range, no tessellation, no fast3d change, no
  vertex-budget cost, and an authentic RDP idiom rather than a port-only
  hack.** At `k = 5` tc is 1 texel per unit with a range of ±32,767 texels,
  which covers the measured 29,830-texel span outright — that is exactly the
  M-100 diagnostic build that already demonstrably removes the artifact, with
  the scale corrected by the tile shift instead of left wrong. Precision cost
  is a ≤0.5-texel rounding *at the vertices only* (≤0.8% of the 64-texel
  tile); interpolation between them is float in the GPU. Implementation care
  needed in two places: emitting the shift on the sky's tile without
  disturbing other draws (an extra `gDPSetTile` after the existing
  `texSelect`), and choosing `k` per draw from the measured span (`k = 0`
  when the span is already small). A light 2x2 subdivision can still be added
  on top for spans beyond ±32,767 texels if any camera produces them.


**Human playtest (same session, after M-98's fold-sharing fix was pushed):**
the seam is still visible on Statue, and — new data point, sharper than
anything in M-93/M-96 — **it visibly changes as the mouse/player view
moves.** Also reported: "still pretty bad" on Cradle, and Surface has the
same issue. M-98's fix was real (verified via a same-frame diff) but is
evidently not the dominant mechanism live play is hitting — the same
pattern as M-95's wScale fix before it.

**Ruled out this pass:** the cross-quad water/cloud texture-scale mismatch
theory (block1's below-horizon quad uses raw `unk0c`, block2's cloud quad
uses `unk0c*0.1f`, and each computes its own independent `wScale`/fold via
separate `skyPortBeginFan` calls — a plausible source of a horizon-tracking
seam). Checked via the existing `GE_D176` env probe: **both Statue and
Cradle report `IsWater=0`.** For `IsWater==0`, block1 is a flat
`gDPFillRectangle` (`sky.c` ~874-897) — no texture, no scroll, can't be the
source of a texture-pattern seam. This theory is **not ruled out in
general** — only on the two `IsWater==0` levels actually tested. It's
untested on any `IsWater==1` level (Frigate/Runway/Depot/Surface1/2 are
candidates — check `GE_D176=1` first, cheap).

**Blocked on: headless camera-pan repro doesn't work.** Tried reproducing
"changes with view" via `GE_INPUTSCRIPT` — both sustained analog-stick
(`SRIGHT`, held 90 ticks) and repeated C-button pulses (`CRIGHT`/`CUP`)
produced **zero visible camera rotation** across captured frames (identical
look direction/HUD framing throughout). The real in-game mouselook almost
certainly injects yaw/pitch through a path `GE_INPUTSCRIPT` doesn't reach —
it only emulates the N64 controller (buttons + one fake stick), and
turning during solo FPS play is evidently driven by something else
entirely (real SDL mouse deltas, likely, given D194's mouselook work).
**Any view-angle-dependent visual bug needs a human playtest to chase until
this gets a debug hook** (e.g. an env-gated `GE_FORCEVIEW=yaw,pitch` set
once per frame, or finding the actual mouse-delta injection point in
`port/src/input.c` and driving it directly instead of going through the
N64-controller emulation layer). Don't re-attempt `GE_INPUTSCRIPT`
stick/button tokens for camera turn next time — confirmed dead end.

**Ranked candidates for next session, in order of promise:**
1. Cross-block independent-fan state on an `IsWater==1` level (see above,
   not yet ruled out) — cheapest to check, `GE_D176=1` first.
2. Frame-to-frame popping at the S/T fold boundary (not a same-frame
   spatial crack) — `skyPortRenderPoly`'s fold recomputes every frame from
   the scrolling min-S, jumping by 64 texels whenever it crosses that
   boundary; never independently verified that fast3d's real texture wrap
   makes that jump invisible for this combiner. **Doesn't need view
   rotation to test** — a static-camera `step:1` capture bracketing a
   known fold-crossing tick (from M-98's ~7px/tick rate) would show it
   directly. Best headlessly-testable option if no human is available.
3. The clip-shape switch (`skyRender`'s 16-case corner-mask switch) handing
   block2 a differently-*shaped* fan frame to frame as screen corners
   cross the horizon, not just moved vertices — could read as "the pattern
   changes as you look around" without a same-frame crack. Not
   investigated.

**Concretely blocked on:** a screenshot or precise repro detail (level,
look angle/pitch, screen location) from a human. Full narrative + the
environment gotchas hit this pass: HANDOFF.md "M-99" (local, this
session — condense into this row if HANDOFF gets rotated before someone
picks this up).

## D227 — M-98: seam residual FIXED (fold-sharing, same class as M-95's wScale-sharing) + scroll-rate quantitatively measured

**Picking up exactly where M-96/M-97 left off:** re-derive the intended N64
sky scroll rate and check the port's actual per-frame tc delta, rather than
re-attempting the seam in isolation.

**Ruled out first: the tick rate.** `skyTick()` (`src/game/sky.c`, verbatim
N64 logic, no `#ifdef PORT`) does `g_SkyCloudOffset += g_ClockTimer` once per
logic tick, wrapping at 4096. `g_ClockTimer` is set from `speedgraphframes`
(`frametiming.c`'s `updateFrameCounters`), which D193/D204 already
established runs at ~1/tick under normal load with a 6-tick catch-up clamp.
If the global tick rate were wrong, movement/AI/animation would all be sped
up too, not just the sky — that's not the report. Also checked
`sub_GAME_7F097388`'s call sites (`sky.c:862,1351`) for a second,
compounding time-based scale — both pass constant `65535.0f` args, not
anything frame-dependent. So the accumulator side is clean; per AGENTS.md
rule 2 (game logic is ground truth), any real bug has to be in the
`#ifdef PORT` D176(a) Path B substitution, not the decompiled game code.

**Found: M-95 only shared half of what needed sharing.** `skyPortBeginFan()`
(added by M-95) computes one `wScale` across the whole fan so shared-edge
vertices quantize their *position* consistently. But `skyPortRenderPoly`'s
S/T fold (`foldS`/`foldT`) — the `floorf(minS/64)*64` that keeps texel
coordinates in `s16` range — was still being computed **per call**, from
only that triangle's own 2-4 vertices, exactly the bug class M-95 fixed one
field over and never touched here. Two triangles in one fan can each
independently floor a shared vertex's S/T to a different multiple of 64
texels; the *visual* result is supposed to be identical (`GL_REPEAT` has
period 64), but each triangle bakes `(S - fold) * 32` into its own `s16`
`Vtx` buffer, and two folds 64 texels apart produce tc values exactly 2048
apart (`64 * 32`) — an s16-precision jump easily large enough to crack at
the shared edge. This is why M-96's human playtest still saw the seam after
M-95: M-95's fix was real (closed *a* crease, per its frame-345 diff) but
not the dominant one live play was hitting.

**Fix:** extended `skyPortBeginFan()`/`skyPortEndFan()` to also compute one
shared fold across the whole fan (`s_skyFanFoldS`/`s_skyFanFoldT`,
`s_skyFanFoldSet`), threaded into `skyPortRenderPoly` the same way as
`wScale` (falls back to the old per-call computation if unset — same
defensive shape as the wScale fallback). `src/game/sky.c`, `#ifdef PORT`
only, no game-logic touch.

**Verified:**
- Build clean (`./build-pc.sh ntsc-final`), no new/duplicate symbols.
- `-level_22` frame 345 (same frame M-95's before/after screenshots used) —
  fresh capture shows continuous diagonal cloud streaks with no visible
  crease at the ~x=1000/1280 location that's clearly cracked in the
  pre-M-95 reference screenshot (`docs/img/bugs/d227-sky-seam-statue.png`).
  No new screenshot committed this pass (didn't diff against an M-96-era
  "still broken" capture, since none was saved) — human eyeball still owed
  per the usual rule for visual fixes landed without display access.
- `verify.sh bunker1`: PASS, `worst_cell=10.3` — unchanged from before this
  fix, matching M-95's note that this number is the already-known stale
  golden-baseline issue (owed a re-baseline, unrelated to sky.c), not a
  regression.

**Scroll-rate measurement (the actual M-96/M-97 ask):** captured 11
consecutive frames (`GE_PCDUMP=345-355:1`, static camera, Statue spawn) and
cross-correlated a sky-band row (y=60) frame-to-frame. Result: a highly
consistent **~7 px/tick horizontal shift at 640px capture width** (346→347
through 354→355 all landed at 6-7px, one outlier at the 345→346 transition
likely a cutscene/fade boundary). At 60 ticks/sec that's a full 640px
screen-width sweep roughly every 90 ticks (~1.5s) — hard quantitative
confirmation of "extremely fast," not just a subjective read, and a
concrete number for whoever chases the magnitude next.

**Not resolved this pass: *why* that magnitude is what it is.** Since the
accumulator (1 texel/tick) and the vertex geometry that turns a texel delta
into screen pixels (`sub_GAME_7F097388`) are both unmodified N64 logic, a
genuine wrong-magnitude bug (if the rate really is wrong relative to N64,
which still isn't confirmed absent a hardware/emulator reference) would
have to live in how the D176(a) Path B substitution maps a given per-vertex
S/T value to on-screen texels — e.g. camera-to-cloud-plane distance, FOV,
or a scale factor implicit in the `skyPortRenderPoly` perspective matrix —
not in the scroll-rate accumulator itself. Didn't chase this further this
pass; next session should compare the recovered per-pixel S gradient
(`tc[0]` delta between adjacent screen columns within one frame, not
frame-to-frame) against what the `130.0f`/`arg4`-driven N64 edge-setup math
in the `#else` branch of `skyRenderTri` computes for the same triangle, to
find whether the *shape* of the perspective correction (M-90) also carries
the right *magnitude*, independent of the wrap-rate question addressed here.

**Still open / human-gated:** the seam-close and the scroll-speed data
point both need a live eyeball before this can come off draft — same as
every other rendering fix landed without display access this session.
Cradle side of PR #43 still never checked (Statue only, again, this pass).

## D227 — M-96: human playtest — seam/crack STILL PRESENT after the M-95 fix; the "static-crease" theory alone does not explain the reported symptom

**Method:** user's own build, this branch (`land/d176a-sky-perspective-scroll`
at `b1733eda`, which includes the M-95 `wScale`-sharing fix). Launched
directly into Statue (`-level_22`), looked at the sky.

**Result: sky renders (the original D176a black-sky bug is fixed — sky is
visibly present, not black), but the seam/crack is still there.** This
directly contradicts the M-95 commit's "closes the visible seam" claim.

**New scroll-speed data point (user, same session): the sky should be
*almost static* on the original N64 game, and on this build it currently
scrolls *extremely fast* — a stronger complaint than M-93's "measurably
better but still reads too fast."** This bears directly on D176(a) itself
(the perspective-correct tc-interpolation fix this whole PR is about), not
just the D227 seam residual. Two live symptoms reported together on the
same Statue session: (a) scroll rate way too fast, (b) the seam. Worth
treating as possibly related — if the effective per-frame tc delta is
wrong by a large factor, that same wrong-magnitude value could plausibly
also drive the per-vertex/per-primitive divergence read as a "seam" (the
D227 M-94 hypothesis assumed *quantization* was the cause of the seam, but
never ruled out a *scroll-rate* mismatch between primitives as an
alternative explanation for "two things moving at different rates"). **Not
yet investigated — next session should re-derive the intended N64 scroll
rate from `sky.c`/the ROM env data and check what the port's actual
per-frame tc delta computes to, rather than assuming D176(a)'s fix already
has the right magnitude just because it's no longer solid black.**

**What this means for the M-94/M-95 diagnosis.** M-95's write-up was explicit
that it verified only the *static per-primitive `wScale` quantization* half
of M-94's hypothesis (via a same-frame before/after screenshot diff), and
flagged the *"each half moves at a different rate"* half of the original
user report as **not re-verified** — that measurement (M-94's static-camera
diff) was never repeated against the M-95 build. This playtest is that
missing verification, and it says the crease is still visible in live play.
Two non-exclusive readings:

1. The screenshot-diff comparison (single static frame) wasn't sensitive to
   whatever actually causes the live-play seam — i.e. the M-95 fix may have
   closed *a* crease (real, per the frame 345 before/after) without being
   *the* crease the user is seeing, or without being the dominant one.
2. There is a second, independent seam-causing mechanism — most likely the
   still-unconfirmed "two halves animate at different rates over time" theory
   from the original M-93/M-94 report, which is a *temporal* divergence
   (rates, not one-frame geometry) that a single-frame diff cannot detect at
   all.

**Status: D227 NOT resolved. PR #43 stays in draft — do not merge on the
strength of the M-95 fix alone.** Next investigation should target the
temporal/rate theory specifically: capture the same fixed camera position
across a range of frames (not just one) on both sky "halves" and measure
whether their texture-coordinate scroll rate actually diverges over time,
per M-94's original (but never re-run post-M-95) methodology.

## D227 — M-95: FIXED (the static-crease part) — one shared `wScale` across the whole sky fan instead of one per triangle

**The fix.** `src/game/sky.c`, `#ifdef PORT` throughout, three additions:

- `skyPortBeginFan(SkyRelated38 *v, s32 n)` — scans `n` verts' `unk0c` for the
  max absolute camera-space `w` and stores `wScale` in a new file-static
  `s_skyFanWScale`. Forward-declared near the top of the file (its first use
  is ~600 lines before its definition).
- `skyPortEndFan(void)` — resets `s_skyFanWScale` to `0.0f` (the "unset"
  sentinel).
- `skyPortRenderPoly` now checks `s_skyFanWScale > 0.0f` first; if set, uses
  it directly instead of recomputing `maxAbsW`/`wScale` from just its own
  2-4 verts. Falls back to the original per-call computation if unset —
  `skyRenderTri`/`skyRenderFull` have no callers outside this file (checked
  by grep), but this keeps a safe default instead of silently reusing a
  stale value if that ever changes.
- Both fan-drawing blocks (the `sp274[5]`/`sp94[5]` local arrays,
  `src/game/sky.c`) call `skyPortBeginFan(array, s1)` once right after
  populating the array — before dispatching to their 1-3
  `skyRenderTri`/`skyRenderFull` calls — and `skyPortEndFan()` once after
  the last one. For the `sp94[]` block specifically: computed from `unk0c`,
  which the position-override code for the full-quad special case (`s1==4`,
  the `sp94[0].unk28 = ...` assignments) does not touch, so the order is
  safe regardless of whether that override path runs.

**Why this was the right diagnosis.** `skyRenderTri`/`skyRenderFull` draw up
to 5 triangles per frame from a *shared* pool of up to 5 vertices (built
once, then fan-triangulated across 1-3 calls). Each call used to compute its
own `wScale` from only its own 2-4 verts. The position math is exact for any
`wScale > 0` (re-derived algebraically in M-94), so this was never a logic
bug — but `Vtx.ob[]` is `s16`, and two triangles sharing a vertex could each
independently pick a different `wScale`, quantizing that logically-identical
shared vertex to two different 16-bit values depending on which triangle's
call computed it. That's a crack at the shared edge — exactly the visible
"seam" from M-94's screenshot.

**Verification.**
- `./build-pc.sh ntsc-final`: clean build, no new/duplicate symbols.
- Full `verify.sh sweep`: **21/21 PASS**, no crash/regression, both with and
  without this change (isolated by `git stash`/`stash pop` and re-running).
- `bunker1`'s non-zero `worst_cell` framediff is unaffected by this change —
  reproduced identically (~64) on a pre-fix build. Confirmed pre-existing:
  the stale golden-baseline issue already noted in the M-83/M-84 memory
  (owed a re-baseline), unrelated to `sky.c`.
- **Direct same-frame before/after comparison, `-level_22` frame 345:**
  `docs/img/bugs/d227-sky-seam-statue.png` (pre-fix — visible kink in the
  cloud streaks around x=1000/1280 in the cropped+2x-upscaled screenshot)
  vs. `docs/img/bugs/d227-sky-seam-statue-fixed.png` (post-fix, same exact
  frame — streaks flow continuously through that region, kink gone).
- **Cradle checked too** (`-level_41`, the other level named in the M-93
  report): the large black rectangular gap visible there is confirmed
  **pre-existing and unrelated** — reproduced identically on a pre-fix
  build capture at the same frame. That's the already-documented
  FOV-coupled black-sky-past-frustum-edge symptom (D218/D222 family, "not
  attempted" per the D176(a) PR body) — a different defect, not touched by
  this fix, not chased further here.

**Not re-verified post-fix:** the "each half moves at a different rate"
half of the original user report (M-94's static-camera diff measurement
was not repeated against this build) and the aspect-ratio-artifact
alternate theory (never independently confirmed or ruled out either way).
The crease visibly closing at a fixed resolution/aspect is reasonable
evidence the crease itself was code-side, not a display-scaling artifact,
but this is not a substitute for a human eyeball pass — owed before merge,
consistent with every other rendering fix landed this session without
interactive display access.

**Generalizable quirk for `porting-notes.md`:** when a decompiled routine
fans a single shared vertex set out across multiple separate perspective-
correction / projection-matrix-building calls (one call per triangle
instead of one call for the whole primitive group), and the fix stores a
derived per-call scale factor into a low-precision (`s16`) vertex field,
each call's independently-chosen scale can quantize a *logically shared*
vertex differently — producing a visible crack at the shared edge even
though the underlying position math is exact for any scale value. Fix:
compute the scale once for the whole shared-vertex group, not per call.
