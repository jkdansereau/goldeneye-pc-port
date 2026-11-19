# D236

<!-- from the old section-F/H table row -->
| D236 | **Surface 1: 2D billboard trees render as a wall/sheet of tree texture instead of discrete camera-facing tree sprites (M-106, user QA report).** — full `## D236` entry at file tail | **CLOSED (M-198, pass 26, `425237f9`; probes stripped `0503d335`): root cause = `TEXTURETYPE_DETAIL` tile binding — fast3d drew the detail tile (opaque IA8 noise) instead of the base image; `gfx_lod_tile_offset` now samples tile 1 when the pair differs (`Video.DetailBaseTile`, default on). Live-verified Surface 1/2 + Depot; awaiting user play-test sign-off.** Historical trail — pass 24 (2026-09-21, static) DECODED THE TREE TEXTURE** (new `tools_pc/texdecode.py` ports image.c's LOOKUP/HUFFLOOKUP/RLELOOKUP codecs). **`tex1198` LOD 0 = 64×17 RGBA5551, 44.2% opaque, and its 1-bit alpha renders as an unmistakable treeline silhouette** — pass 23's identification confirmed by decode, not inference. **Rare authored a six-level LOD pyramid that PRESERVES the silhouette and its alpha coverage (44.2% → 43.1% → 38.8%), each level a separately-authored image with its own compmethod.** Since `XLU_SURF` has no alpha test (pass 22), averaging that 0/1 alpha into fractional mip alpha would smear the minified treeline into exactly the reported band — so 'authored LOD chain replaced by driver-generated mips' is now the leading mechanism, with hard ground truth behind it. Corrects pass 23: only `tex1198` is a treeline (`tex1199` 89.4% opaque denser foliage; `tex1200`/`tex1201` 100% opaque detail textures sharing the class). Two decoder bugs found while porting (missing per-image byte-align, `image.c:1096`; a GUESSED rather than ported RLE fudge) — both produced confident wrong pictures. **Pass 25 (live) answered all three questions: (1) YES 64×17 RGBA16 intact, pool ≡ offline decode; (2) YES alpha survives (44.2% opaque); (3) NO — no code path uploads authored chains as GL mips.** New: `tex1198`–`1201` are **never drawn from an idle spawn** (zero RGBA16 imports in 4800 frames) — the spawn wall is painted by other classes (cf D276/D280); the drawn tree-class card is **`tex149` IA8 32×64 hard cutout, byte-exact import, no authored chain**. Mip mechanism located: `texGetDepthAndSize` sizes LOADBLOCKs from the header `lods` field even for single-image textures (live A/B: 2808 B vs 2048 B real data), and the port's RC2 clip + `glGenerateMipmap`/trilinear (observed live) box-filters hard 0/F alpha into fractional mips → XLU smear. Pass 26 fix directions in entry (cheapest: no mip gen for no-chain textures). |

## D236 — Surface 1: 2D billboard trees render as a wall/sheet of tree texture instead of discrete camera-facing tree sprites (M-106, user QA report).

**Surface 1: 2D billboard trees render as a wall/sheet of tree texture instead of discrete camera-facing tree sprites (M-106, user QA report).** Eight passes so far (D265, D266–D270, D271, D272, D273, D274, D275–D276). Ruled out: IA4/IA8 decode (as originally scoped), the blend pipeline, fog combine (as originally scoped), portal-culling over-draw (D271, real bug, fixed, not the cause), converter/UV-conversion bugs (D272, code-read clean), the CI palette bit-layout (D273/D274, tested twice against real content), "mips absent" (D274, confirmed present/enabled), "identical UV window" *for the CI8 classes* (D272). **Current leading picture (D276, M-134): the actual visible weave at confirmed weave pixels is painted by a different, previously-dismissed class — an IA8 blocky-noise texture (`oml=0xc81049d8`/`fmt=3`) sampled with a fixed, byte-identical 2×-wrapped UV window and low fog, reproducing D265's original "same window" observation exactly (D269's CI8-classes framing had reframed away from that lead prematurely).** A third, unattributed CI8 class (`0x0c184b50`) may be the reference's actual discrete-tree layer and shows an erratic z/w suggesting its own occlusion/billboard bug — D236 may split into two root causes. **Ninth pass (D280, M-135): fog math verified N64-faithful (±2 truncation over 2505 verts), the IA8 noise class visually confirmed as the weave's painter (HF 14.8→2.9 when collapsed), `0xc8102078` ruled out (terrain, 1.4M tris), and the depth-race read inverted — `0x0c184b50` tree cards are IN FRONT of the noise quad (z/w smaller=closer) yet still don't visibly win; under deterministic `-level_36` boot they barely instantiate at all (~4 tris vs 720 in a free run). No quick win: remaining work is CPU-side instantiation/room-streaming, not a port fix.** **Tenth pass (M-155): that CPU-side-instantiation framing itself is REFUTED — three independent deterministic re-runs of the same `-level_36` boot (one with sustained camera rotation) proved the class's per-frame count is byte-identical, not run-to-run dependent; the earlier "~4 vs 720" comparison was an outdoor-vs-indoor room mismatch (the boot's scripted intro walks the camera out of the tree-bearing room at a fixed frame), not nondeterminism. No fix; the original wall-vs-discrete-sprites symptom is unresolved and untouched by this pass.** Distinct from D176(b)'s painted tree-line backdrop. Logged in `GRAPHICS-BACKLOG.md`.

**Status (from §F table):** OPEN — **pass 24 (2026-09-21, static) DECODED THE TREE TEXTURE** (new `tools_pc/texdecode.py` ports image.c's LOOKUP/HUFFLOOKUP/RLELOOKUP codecs). **`tex1198` LOD 0 = 64×17 RGBA5551, 44.2% opaque, and its 1-bit alpha renders as an unmistakable treeline silhouette** — pass 23's identification confirmed by decode, not inference. **Rare authored a six-level LOD pyramid that PRESERVES the silhouette and its alpha coverage (44.2% → 43.1% → 38.8%), each level a separately-authored image with its own compmethod.** Since `XLU_SURF` has no alpha test (pass 22), averaging that 0/1 alpha into fractional mip alpha would smear the minified treeline into exactly the reported band — so 'authored LOD chain replaced by driver-generated mips' is now the leading mechanism, with hard ground truth behind it. Corrects pass 23: only `tex1198` is a treeline (`tex1199` 89.4% opaque denser foliage; `tex1200`/`tex1201` 100% opaque detail textures sharing the class). Two decoder bugs found while porting (missing per-image byte-align, `image.c:1096`; a GUESSED rather than ported RLE fudge) — both produced confident wrong pictures. Next: one `-level_36` run filtered on **`fmt=0 siz=2`**, diffing the live import against this offline reference on (1) 64×17 RGBA5551 dims/format, (2) ~44% alpha transparency, (3) whether all six authored LODs reach GL or only LOD 0.

**Next pass framing (post rule-2-clarification):**

```
TASK: D236 pass 10 — determine why the 0x0c184b50 discrete tree-card class
instantiates inconsistently (~4 tris on deterministic -level_36 boot vs
~720 in a free run) despite being geometrically in front of the confirmed
N64-faithful noise-quad "wall" (D280).
READ FIRST: docs/porting-notes.md §A1 (pattern + tells, now including D255);
docs/dev-process.md's "Diagnosis is unrestricted" section and §7 sign-off
procedure; docs/dev/findings.md D236 full entry (D265, D271, D272,
D273/D274, D276, D280) plus D209/D210/D255 as worked A1 examples.
FILES YOU MAY TOUCH: none pre-committed — this pass is diagnosis-first.
Touching src/game requires first clearing buckets (a) and (b) below, with
evidence, per CONSTRAINTS.
KNOWN-GOOD / RULED OUT (do not re-investigate): IA4/IA8 decode, blend
pipeline, fog combine (verified N64-faithful, ±2 truncation over 2505
verts), portal-culling over-draw (separately fixed, not the cause),
converter/UV-conversion, CI palette bit-layout, mip presence, "identical UV
window" for the CI8 classes.
PRE-FLIGHT ATTACHED: D280's z/w depth-race data (0x0c184b50 confirmed in
front of the noise quad); the instantiation tri-count delta (4 vs 720)
between deterministic and free boot.
BUDGET: <=6 build->run cycles / ~90 min. On expiry: revert probes, write up
with confidence, no forced fix.
CONSTRAINTS: Trace the instantiation-count/gating decision into src/game
(prop-spawn, room-streaming, or distance-cull logic — likely bg.c) as far
as the evidence leads; name the exact struct/field/counter responsible
before concluding anything. Specifically, before writing this up as bucket
(c) (a genuine timing/streaming behavior difference worth a rule-2 sign-off
request):
  - Check bucket (a) first: is the count/visibility-gating field read at a
    struct offset that shifted under 32->64-bit pointer widening? (§A1
    tells: deterministic-but-wrong output; a raw/literal offset or narrow
    field read into a struct/union with any pointer member anywhere in its
    layout.)
  - Check bucket (b) next: is a port-layer timing/order-of-operations
    difference (frame pacing, load order, an uninitialized or
    differently-timed port-owned counter) feeding src/game a different
    input than N64 would have produced upstream of the gating logic?
  - Only once both are actively ruled out with cited evidence (not
    assumption) does this qualify for a rule-2 sign-off write-up under
    docs/dev-process.md §7. Do not request sign-off preemptively or as a
    fallback for "didn't find an ABI/port explanation in the time budget."
VERIFY: the deterministic -level_36 boot repro + tri-count probe already
used in D280; attach before/after instantiation counts for any change.
REPORT: (a) the exact struct/field/counter gating instantiation, file:line,
        and which bucket (a/b/c) it falls in, with the evidence for that
        classification  (b) fix diff if (a) or (b), or a full rule-2
        sign-off request per §7 if (c), never a partial/ambiguous one
        (c) probes left in tree  (d) confidence.
        Append any generalisable quirk to docs/porting-notes.md — extend
        §A1 if this turns out to be another instance of the pattern.
```

**Pass 10 result (M-155, 2026-09-16): D280's "instantiation is run-to-run
dependent" premise is REFUTED — it's ordinary room-based occlusion, not a
CPU-side gating bug. No fix; this closes the specific mystery the pass-10
brief posed, D236 itself stays OPEN on the original "wall not discrete
sprites" symptom.**

Added a small env-gated diagnostic (`GE_D236RM`, `port/fast3d/gfx_pc.cpp`
`gfx_sp_tri1`, see `GE-ENV-PROBES.md`) that counts triangles per frame
(`num_dls`) whose `other_mode_l == 0x0c184b50` (the discrete-tree-card
render-mode signature from D280), heartbeat-logging every 300 frames even
at zero so a class going silent is visible, not just absent.

Ran the deterministic `-level_36` boot **three times independently**
(two static, one with `GE_INPUTSCRIPT="1:SRIGHT"` sustained rotation) —
**all three produced byte-identical per-frame counts**: 4 tris/frame for
frames 1-536, 2 tris/frame for frames 537-747, then **zero for the rest
of a 55-60s run** (verified to frame 3299, well past D280's tick-1500
sample point). This alone falsifies "not pinned by the boot flag" as
stated — the boot IS fully deterministic frame-by-frame; D280's
comparison must have been against session state, not process determinism.

Cross-referencing with the already-in-tree `GE_D104` room-census probe
(`bg.c:654`, unrelated finding, still live) in the same run pinpoints the
mechanism exactly: **the class's count drops to zero at the same tick
(~9601 controller-reads ≈ frame ~740-748) that `curRoom` transitions from
12 to 18, and `roomsDrawn` collapses from 17-20 rooms to 1-5.** Room 12's
neighbour set (17-20 rooms, several with small portal-window bboxes —
consistent with a big outdoor vista) is where the tree-card class draws;
room 18 (near-fullscreen bbox, 1-5 rooms visible — consistent with an
enclosed indoor space) is where it permanently stops, and stays at zero
through the rest of the capture regardless of continuous `SRIGHT`
rotation. This is the deterministic `-level_36` boot's own scripted
intro camera walking from an outdoor establishing shot into the player's
actual indoor start point — the class legitimately has nothing to draw
once indoors, the same way any outdoor-only geometry would correctly stop
rendering behind walls on real N64. D280's "~4 tris on deterministic
boot vs ~720 in a free run" was comparing this boot's post-intro *indoor*
sample against a live session's *outdoor* one — an apples-to-oranges
comparison, not evidence of nondeterministic instantiation.

**Bucket classification: none of (a)/(b)/(c) — the room-transition
explanation accounts for the entire observed variance without invoking
an ABI misread, a port-layer timing bug, or a genuine N64 divergence.**
No `src/game` or `port/` edit made or warranted by this pass.

**What this does NOT resolve:** whether, from *inside* room 12 looking
directly at the tree line, the discrete tree cards draw enough
individual, correctly-placed triangles to look like trees rather than
merging into D276's IA8 noise "wall" — this pass never got the camera
into that outdoor room's tree-facing view (the scripted intro's own path
through room 12 stays at low counts, 4→2, never approaching D280's 720
figure). **Next pass, concrete:** find (or script) a camera path that
stays in room 12 and faces the tree line directly — e.g. `GE_D104` to
locate room 12's screen-space bbox windows facing the trees, then a
`GE_INPUTSCRIPT` sequence that moves (not just rotates) the player
toward one — and re-run `GE_D236RM` there; a healthy count scaling up
toward the ~720 range as the camera closes on the tree line would confirm
the CPU-side instantiation is fine and the remaining D236 mystery is
purely a rendering/blend-order question (D276's territory); a count that
stays anomalously low even facing the trees head-on from inside room 12
would be the first real evidence of a genuine instantiation bug and
should restart the bucket-(a)/(b)/(c) check from there.

Probe left in tree (`GE_D236RM`, env-gated, zero cost unset, cataloged in
`GE-ENV-PROBES.md`). Confidence: HIGH on the room-transition mechanism
(three independent deterministic reproductions, direct room-index
correlation); LOW-to-none on the original "wall vs discrete sprites"
symptom, which this pass didn't reach.

**Pass 11 (same day): partial nav progress via `GE_INPUTSCRIPT`, still
short of D280's ~720 baseline — no bucket resolved, wrote up as
directed.** Attempted to keep the camera in the outdoor room and facing
the tree line using `GE_INPUTSCRIPT` stick holds from frame 0:

- `GE_INPUTSCRIPT="0:SDOWN"` (sustained backward): `curRoom=7` at the
  first sample (vs pass 10's baseline `curRoom=12`), meaning held stick
  input during the pre-transition window *does* change which room the
  player/camera occupies — this is not a fully input-inert cutscene.
  Tree-card count stayed flat (4 for 535 samples, then 2, then 0) —
  never exceeded pass 10's baseline.
- `GE_INPUTSCRIPT="0:SUP"` (sustained forward): `curRoom=13` at the
  first sample — a third distinct starting room, confirming input
  changes the pre-transition room but not in an obviously useful
  direction. Count stayed flat at 2-3, again never scaling up.
- `GE_INPUTSCRIPT="0:START;60:START;900:SUP"` (skip the intro faster
  with two `START` presses, then try to walk back out): `curRoom=12`
  again (pass 10's exact baseline room) and the room-18 transition
  happened noticeably *earlier* (D104 tick ~1801 vs ~6601 in the other
  two runs) — `START` does shorten the pre-transition window rather than
  extending it, the opposite of what's wanted. **But this run's very
  first `GE_D236RM` samples hit `count=10` then `count=6`** (dl=1, dl=2)
  before immediately collapsing to 2 by dl=95 and 0 by dl=599 — the
  highest counts seen in any of the four runs to date (pass 10's three +
  this one), all occurring in the first couple of rendered frames.

**Interpretation:** all three attempts confirm player stick input *does*
influence the pre-transition room (curRoom varies 7/12/13 depending on
direction), refuting a fully-scripted-camera-only model — but none found
a way to *hold* the camera in the outdoor room past the transition, and
the transition itself appears to be timer/event-driven (not
distance-to-a-trigger), since `START`-skipping made it fire sooner, not
later. The one genuinely new data point — a brief `count=10` at the very
first sampled frame of the `START`-skip run — shows the discrete
tree-card class CAN render more than pass 10's flat 4, but still nowhere
near D280's ~720 figure, and only for 1-2 frames before dropping back
down. **This is the strongest evidence yet that D280's ~720-triangle
sample was taken from a materially different vantage (later in the level,
in a genuine free-roam session, likely much closer to the tree line or
facing a wider arc of it) than anything reachable from this deterministic
`-level_36` boot's opening seconds** — not evidence of a CPU-side
instantiation bug at this vantage. No src/game or port/ file touched;
`GE_D236RM` unmodified from pass 10. Confidence: MEDIUM that the
transition is timer-driven rather than position-driven (3 consistent
data points); LOW-to-none on why D280's original ~720 figure was so much
higher — that needs either a `GE_INPUTSCRIPT` script that survives well
past the mission-start hand-off into free movement (not attempted this
pass — budget spent), or a live human session with `GE_D236RM` set,
walking toward the tree line and comparing against this deterministic
boot's numbers directly.

**Next pass, concrete:** don't fight the scripted intro further — instead
run `-level_36` with `GE_D236RM=1` interactively (human input, no
`GE_INPUTSCRIPT`) *or* extend the script well past the mission hand-off
(needs `GE_INPUTLOG` calibration of the frame-vs-real-time ratio first,
not done this pass) and walk toward the tree line from inside the level
proper, not during the opening establishing shot. Correlate against
D280's own capture conditions (what level state/position produced 720)
if that's recorded anywhere in the D280 write-up above.

**Pass 12 (2026-09-18, this session): a genuinely new mechanism found and confirmed real, but the exact visually-bugged view still has too little tree-class geometry to test it against — not a fix, narrows the next pass.**

**New mechanism, not considered by passes 1-11 (all of which reasoned from z/w comparisons):** decoded both classes' full `other_mode_l` words offline — `0xc81049d8` (noise) and `0x0c184b50` (tree cards) are **both `Z_CMP=1`/`Z_UPD=0`/`ZMODE=DEC`** (decal mode, depth-tested but not depth-written). Two non-depth-writing decals never actually depth-test against each other — each tests only against the static background's z-buffer — so on-screen precedence between them is decided by **draw order alone** (whichever is submitted later in the frame wins the color buffer), not by which is geometrically closer. This means the z/w comparisons all 11 prior passes made were measuring something that doesn't determine the visual winner between these two specific classes.

Built `GE_D236ORDER` (`port/fast3d/gfx_pc.cpp`, `gfx_sp_tri1`, env-gated, zero cost unset) to measure this directly: a per-frame submission-order counter, logging the min/max order index for each class whenever `num_dls` (frame) changes. Also built `GE_D236ZFIX` (same file), a diagnostic-only experiment forcing `depth_update=true` for the tree-card class alone, to test whether giving it real z-write lets it correctly block a later-drawn noise quad — **not proposed as a fix** (decal-mode classes aren't supposed to write depth; this would need its own rule-2 justification if it ever became a real fix candidate).

**Headless repro finally landed on the actual bug, no live session needed this pass.** `GE_PCDUMP=1300-1700:100` on `-level_36` (a snowy corridor level, not literally "Surface 1/2" by debug number but visually and mechanically the same wall-of-static symptom) captured the exact reported look: a solid black-and-white static-noise "wall" on both sides of a snow corridor, `docs/dev/GRAPHICS-BACKLOG.md`-consistent with the user's screenshots. **`GE_D236ORDER` at this exact captured frame range shows `tree_n=0` throughout** — the discrete tree-card class has **zero triangles present at all** in the view that shows the bug, not merely "in the wrong order." A few hundred frames earlier in the same deterministic run (dl≈609-634), 2 tree triangles ARE present and (in that window) drawn **after** the noise class — the theoretically-correct order for them to win — but 2 triangles is nowhere near enough geometry to visually register against a whole wall of noise texture, and a screenshot at that exact frame (`scratch/ppm_620/frame_000620.png`) confirms it looks identical to the bugged view (no visible trees).

**Conclusion: the draw-order/decal mechanism is real and worth keeping as a documented candidate, but it isn't provably the active cause of the specific reported visual** — the deterministic boot simply never has enough tree-class geometry present at the bugged camera position to test the theory against, the same wall every pass since D280 has hit. This is unchanged from pass 9-11's own conclusion despite the new mechanism: **the real blocker is still CPU-side tree-prop instantiation/room-streaming at this exact camera position**, not (as far as this pass can tell) a port-side rendering bug. Confidence: HIGH that the decal-mode draw-order mechanism is real and correctly understood; LOW-to-none that it's the dominant cause of the reported symptom, since the necessary competing geometry isn't present to observe it losing or winning.

**Next pass, concrete (supersedes the "correlate against D280" step above, still valid but now sharper):** a live human session, `GE_D236ORDER=1` set, walking directly toward the tree line from inside the level (not the deterministic boot's opening corridor) is now the single most informative next capture — it would show, in one run: (a) does the tree-card count reach anything near D280's ~720 figure at this camera position, and if so (b) is `GE_D236ORDER`'s per-frame order relationship consistently noise-after-tree (bad) or tree-after-noise (fine) once real geometry is present. That single data point would finally either confirm or rule out the decal draw-order mechanism as the actual fix target, something no headless capture has been able to test in three sessions running. **Not v0.3.0-blocking, per standing user priority** — recommend parking again rather than rushing an unverified fix under release-timeline pressure. `GE_D236ORDER`/`GE_D236ZFIX` left in tree, env-gated, zero behavior change when unset.

**Pass 13 (2026-09-18, same session): the requested live capture landed, and it demotes the draw-order mechanism rather than confirming it — the more promising remaining lead is the tree class's own fog, not draw order.**

User played live with `GE_D236ORDER=1` set, walked toward Surface1's tree line, and confirmed the wall bug still reproduces (`scratch/d236_live_capture.log`, 2024 `D236ORDER` lines). Real data at last: tree-card count reached up to **121 triangles** (still short of D280's old ~720, but far beyond the deterministic boot's 2-4 — confirms the boot genuinely undersamples this class, not that it's absent from the level). More importantly: **the draw-order relationship is distance-dependent** — at low tree counts (6-12 tris, farther away) the noise class is consistently drawn *after* the tree class (bad order, matches pass 12's theory); at high tree counts (100+, closer) this **flips** — the tree class is consistently drawn *after* the noise class (good order, trees should win the color buffer).

**The decisive fact: the user still saw the wall bug while walking through the zone where tree count was high AND draw order favored the trees.** If pass 12's decal-ordering mechanism were the dominant cause, the bug should have resolved (or visibly improved) exactly in that zone — it didn't. **Conclusion: draw order is real (confirmed twice now, in two different measurement contexts) but is NOT the dominant cause of the reported symptom** — something else is preventing the tree cards from reading as discrete trees even when they have substantial triangle count and favorable draw order.

**Redirects the investigation to a lead D280 flagged but never closed: the tree class's own fog computation, not the noise class's.** D280's fog-math verification (`## D280` above, step 1) proved the *noise* class (`0xc81049d8`)'s fog is N64-faithful — it never checked the *tree* class (`0x0c184b50`)'s own fog, which D280's own census separately found to be a strange bimodal 25/255 split per vertex (`gfog=0`, CPU-baked, not RSP-computed). A tree card drawn correctly on top of the noise (as the live data now shows happens at close range) but rendered with fog=255 (fully white-washed) would look exactly like a blank/hazy patch of "wall," not a tree — consistent with the reported symptom persisting despite correct geometry and order.

**Not attempted this pass — genuinely the next step, not a rehash:** trace `0x0c184b50`'s own per-vertex fog computation (where it's set, CPU-side vs RSP, and against what reference distance) and compare it to what N64 would compute for the same vertex positions. This is a different code path than D280's already-verified noise-class fog check and hasn't been looked at directly. Given the depth of this investigation (13 passes) and the release timeline, **recommend keeping this parked for v0.3.0** — the mechanism is narrowed further than ever, but not yet a safe, confident fix. `GE_D236ORDER`/`GE_D236ZFIX` remain in tree, env-gated, zero cost when unset; the live capture log is preserved at `scratch/d236_live_capture.log` for whoever picks this up next.

**Pass 14 (2026-09-18, later session, no timeline pressure — user asked to keep going): sharpened pass 13's fog lead into a falsifiable mechanism and built the probe to test it; not yet live-captured.**

**Two things checked by source review before touching code, both clean:** (1) `port/fast3d/gfx_pc.cpp`'s `gfx_sp_vertex` fog fallback (`if (rsp.geometry_mode & G_FOG) {...} else { d->fog = rdp.fog_color.a; } d->color.a = vcn->a;`) is **byte-identical to the local Perfect Dark PC port reference** (`pd_port/port/fast3d/gfx_pc.cpp:1177-1194`) — this exact branch is not a GE-specific bug, it's shared, more-battle-tested fast3d ground truth. (2) `rsp/graphics/gmain.s` (GE's own RSP ucode, the AGENTS.md ground truth for GBI semantics) has **zero fog-related code** — GE's ucode doesn't customize fog generation at all, so there's nothing GE-specific to cross-check there either. Both rule out "our RSP fog emulation is wrong" as the mechanism.

**Decoded the tree class's actual render mode/blend equation** (`0x0c184b50`, matched against `include/PR/gbi.h`'s `RM_AA_ZB_XLU_DECAL(clk)` macro: `AA_EN|Z_CMP|IM_RD|CVG_DST_WRAP|CLR_ON_CVG|FORCE_BL|ZMODE_DEC`, blend `GBL_c(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA)` — exactly matches D280/D272's Z_CMP=1/Z_UPD=0/ZMODE=DEC finding). This is an ordinary **alpha-blend decal**: `out = src_color*src_alpha + dst_color*(1-src_alpha)`. Critically, the blend equation does **not** reference `G_BL_CLR_FOG`/`G_BL_A_FOG` at all — whatever ends up as this triangle's alpha (SHADE alpha, i.e. `vcn->a`, the same raw per-vertex value D280's census found bimodal at 25/255) directly controls how *transparent* the tree card is over whatever was drawn underneath it (the noise class, confirmed drawn first in the close/high-count zone by pass 13's own data).

**New hypothesis, not proposed by passes 1-13:** if a meaningful fraction of a close tree card's vertices carry the low end of that bimodal split (`~25`, i.e. ~10% opacity) rather than the high end (`255`, opaque), the card would blend almost invisibly over the noise texture even when it has correct geometry, correct triangle count, and correct (tree-after-noise) draw order — reproducing exactly the "still looks like a wall" result pass 13 reported in its own favorable zone. This reframes the open question from "is the *fog value* right" (D280's framing, ambiguous — this render mode doesn't consume `d->fog`/RSP fog at all) to the narrower, directly testable one: **is vertex alpha low specifically for the close/high-count trees the live capture walked through, or does it correlate sanely with distance (opaque near, faded far, as a billboard fade LOD would legitimately do)?** Neither this pass nor any prior one has the data to answer that yet — D280's "bimodal 25/255" finding was a class-wide census, not correlated against per-instance camera distance at the specific bugged vantage.

**Built, not yet run live:** `GE_D236ALPHA` (`port/fast3d/gfx_pc.cpp`, `gfx_sp_tri1`, env-gated, zero cost unset, same pattern as `GE_D236ORDER`/`GE_D236RM`). For every triangle of the `0x0c184b50` class (capped at 4000 hits to bound log size): logs the combine_mode word once whenever it changes (to confirm alpha really is sourced from SHADE and not, say, ENVIRONMENT or a constant, before trusting the hypothesis further), then each triangle's three vertex alphas (`v1/v2/v3->color.a`) alongside `1/w` for each vertex (a cheap camera-distance proxy, smaller w = closer) so alpha can be correlated against distance the same way pass 13's live capture correlated tree count/draw-order against distance. Build verified clean (`./build-pc.sh ntsc-final`, 243/243, only pre-existing unrelated warnings); `tools_pc/verify.sh dam` PASS, crash-free with the probe compiled in and unset.

**Next step, concrete: a live capture identical in method to pass 13's** — `GE_D236ALPHA=1`, walk toward Surface1's tree line from far to close, same as before. Read the resulting log for: (a) does `v->color.a` trend toward 255 as `1/w` grows (closer)? If yes, the port is faithfully reproducing an N64-authored near-opaque/far-faded billboard fade and this hypothesis is refuted, redirecting back to an as-yet-unfound mechanism. If alpha stays low (~25) even at high `1/w` (close range, matching the zone where pass 13's draw order was already favorable), that's a concrete, falsifiable bug signature — the next question after that would be *where* that low value is coming from (baked ROM vertex data being read wrong vs. a legitimate baked-low value that N64's own combiner/blend path compensates for in some other way this port doesn't reproduce). **Not v0.3.0-blocking per standing user priority, but the user asked to keep working it regardless of release timing this session.** `GE_D236ORDER`/`GE_D236ZFIX`/`GE_D236ALPHA` all remain in tree, env-gated, zero cost when unset.

**Pass 15 (2026-09-18, same later session — the requested live capture landed, decisively quantifying the mechanism, but still stops short of a fix): confirmed the tree class's vertex alpha genuinely never reaches anywhere near opaque, and confirmed exactly how it drives the final transparency — but couldn't establish whether the low values are faithful ROM data or a decode bug.**

**Launched the game directly** (`GE_D236ALPHA=1 ./build-pc/ge007.x86_64.exe -level_36`, MSYS2 PATH for the SDL2 runtime dep) so the user could walk the exact same far→close tree-line approach as pass 13 without a separate build step; 1500 `D236ALPHA` lines captured (500 triangles × 3 vertices = 4500 alpha samples), well under the probe's 4000-triangle cap.

**Result 1 — alpha never gets close to opaque, anywhere in the walk.** Across the *entire* session, `v->color.a` ranged **0–36** (out of 255, i.e. ≤14% opacity) — not the 25/255 bimodal split D280's older class-wide census described. Bucketing all 4500 samples into deciles by `1/w` (camera-distance proxy, larger = closer) shows a real, monotonic trend — mean alpha rises from 6.2 in the farthest decile to 27.1 in the closest — but the ceiling never lifts: even the single closest samples captured topped out at 36. Two readings are consistent with this data and neither is ruled out yet: (a) the player never got quite close enough in this session to reach whatever distance a card would need to hit 255 (the closest `1/w` bucket, 0.0011–0.0018, has no calibration reference in this codebase to say whether that's "close" in engine units), or (b) the alpha value is capped/scaled wrong regardless of true distance.

**Result 2 — decoded the actual combine equation, resolving pass 14's open "which alpha slot" question.** The single `combine_mode` word logged this session (`0x00f39e4f1f39e4f1`, unchanged for the whole class) decodes via this file's own shader-synthesis extraction (`gfx_pc.cpp` ~L359-366) to alpha cycle `(a=1,b=7,c=4,d=7)` = `(TEXEL0 − 0) × SHADE + 0` in both cycles — i.e. **final alpha = texture alpha × vertex(SHADE) alpha**, the standard `G_CC_MODULATEIA` idiom. This confirms pass 14's hypothesis outright: whatever the CI8 tree texture's own per-texel alpha is (presumably near-255 for opaque tree pixels, going by the format), the vertex alpha multiplies it back down — so a vertex alpha capped at 36/255 caps *every* tree pixel drawn through that vertex at ≤14% opacity, full stop, regardless of the texture's own content. This is now a settled fact about the render pipeline, not a hypothesis.

**Traced the vertex-color path as far as source review allows this pass, found no swap/decode bug in it, but couldn't identify what actually computes/bakes this specific class's alpha.** `bgLoadRoomVtxData`/`bgSwapRoomVtx` (`bg.c:2353-2369`, room-level static vertex table load) explicitly documents and correctly implements "swap the 6 leading u16 fields (position/flag/uv), **leave the 4 colour bytes alone**" — if this room's tree-card geometry comes through this path, the raw ROM-authored alpha byte passes through completely unmodified, byte-for-byte, and this port has no bug in that specific step. Went looking for a CPU-side per-frame alpha *computation* next (the mechanism that would explain the real, non-flat 1/w correlation the data shows) and found one real candidate, `fogGetPropDistColor`/`chrobjFogVisRangeRelated` (`bgfog.c:695`, `propobj.c:7420+`) — a genuine decomp-verbatim distance-fog alpha blend used for `PropRecord`/`ObjectRecord` instances (doors, tinted glass, damage-decal props) — but confirmed by grep that `bg.c`/`bgapply.c` (the actual room-background-geometry load path this tree class's render-mode signature points to, per D280's census) **never reference** `fogGetPropDistColor` or its `far_fog_dist_scaled`/`near_fog_dist_scaled` fields at all, so that specific function is not this class's mechanism — a dead end, not a fit, ruled out this pass. **Did not find the actual code that determines whether this tree class is static ROM-baked vertex data (in which case N64 would show identical values, and any bug is downstream of the raw bytes, not in reading them) or is computed by some other per-frame CPU path not yet located** (the smooth 1/w correlation in the live data argues against "flat static bytes coincidentally increasing with distance across unrelated tree instances," but isn't proof).

**Numeric aside, unconfirmed, flagged for whoever picks this up:** 36 ≈ 255/7 (36.43). If a genuinely different underlying quantity here is an unsigned 3-bit "fog/light level" (0-7) later expanded to a byte via `level * (255/7)` — a common N64-era bit-depth-expansion idiom — the observed 0-36 range would mean this session's walk only ever reached **level 0 or 1 of a possible 0-7**, i.e. the mechanism computing the level may be stuck near its floor rather than the byte itself being wrong. Not chased down this pass (no `* 7`/`/ 7`/level-table hits found in a quick `bg*.c`/`bgfog.c` grep) — worth a targeted look at whatever function ultimately writes this specific room's tree-card `Vtx.cn[3]` if the next pass wants to keep pulling this thread, but flagged as a numerical coincidence, not a confirmed mechanism.

**Not v0.3.0-blocking, user is continuing anyway.** Next step, concrete: find the actual write site for this room-geometry class's baked/computed alpha (search the ROM room-decompression path more specifically for this level's data, or instrument `bgLoadRoomVtxData` itself to dump the raw pre-swap bytes for this room and compare against what the running probe reports post-load — if they match exactly, the value is confirmed ROM-faithful and the investigation should pivot to "is this actually correct N64 behavior, i.e. is the real bug elsewhere (wrong texture selected, wrong room's data loaded, etc.)" rather than continuing to chase the alpha value itself). `GE_D236ALPHA` log preserved at `scratch/d236_pass14_live_capture.log` (filename predates this being pass 15's data) for reference.

**Pass 16 (2026-09-18, same later session, user said "go for it" — a decisive negative result plus a still-open "where does this data actually come from" question): ruled out room-background static vertex data as the source of the low alpha, and ruled out the two most obvious dynamic-quad-builder candidates in `model.c`, but did not find the actual write site.**

**Built `GE_D236RAW`** (`src/game/bg.c`, `bgLoadRoomVtxData`, right after `bgSwapRoomVtx`, env-gated): dumps a histogram of every vertex's raw `cn[3]` alpha byte for each room's *entire* static vertex table the moment it's decompressed and swapped — independent of camera position or which triangles happen to be drawn, unlike `GE_D236ALPHA`. Ran headless 25s on `-level_36` (no live session needed for this step): **64 room-load events across ~40 distinct rooms, and every single one reported `amin=255 amax=255`** — literally every vertex in every room's raw table is fully opaque. **This conclusively rules out "static ROM-authored per-vertex alpha" as the source of the ≤36/255 values pass 15 measured at render time** — `bgSwapRoomVtx` was already confirmed not to touch colour bytes (pass 15), and now the pre-render data itself is confirmed uniformly opaque across the whole level. Whatever produces the low alpha happens **after** room load, and is not baked into this table at all.

**Extended `GE_D236ALPHA`** (`port/fast3d/gfx_pc.cpp`) to also report which segment the tree class's vertices were loaded from, by recording the top byte of the raw `G_VTX` command's segmented address (`g_d236_last_vtx_seg`, set in the `G_VTX` case of the main DL dispatch, read back in the existing per-triangle probe). Live capture in the same session as pass 15's walk was not repeated; a headless run instead confirmed the class reports **`vtxseg=0x70`** consistently. Per `seg_addr()`'s own documented convention (`gfx_pc.cpp:3260-3294`) and `port/src/dram.c`'s `DRAM_V1_BASE=0x70000000` comment ("ALL game RAM symbols live" in this view starting at that address), `0x70` is **not** one of the game's named symbolic segments (`SPSEGMENT_MODEL_VTX=4`, `SPSEGMENT_BG_VTX=14`, etc., `bondconstants.h:2897-2909`) — it means this particular `G_VTX` command references a **raw, unsegmented host pointer** into the general RAM arena, the same pattern used by every dynamically-built quad in `model.c` that calls `osVirtualToPhysical(freshly_allocated_buffer)` directly instead of going through `gSPSegment`. This is consistent with (though doesn't newly prove beyond) pass 15's "not static data" framing, and additionally rules out the two *specific* addressing schemes (segment 4, segment 14) a static per-model or per-room table would use.

**Went looking for the actual CPU-side quad-builder and came up empty.** `model.c` has exactly four functions that dynamically allocate and write `Vtx`/`Vertex` arrays via `vtxallocator()` for `ModelNode` rendering: `dogfnegx` (`MODELNODE_OPCODE_GUNFIRE` — PD name `modelRenderNodeChrGunfire`, muzzle flash), `doshadow` (`MODELNODE_OPCODE_SHADOW`, character/prop drop shadows), `dorottex` (`MODELNODE_OPCODE_DLPRIMARY`, "star" muzzle-flash flare arms per its own header comment), and `dotube` (`MODELNODE_OPCODE_OP07`, a two-vertex-per-iteration rotating-ring interpolator, gun-barrel/rope-like geometry). Read all four in full: none match "outdoor decorative camera-facing billboard sprite" by content, and the two that use segment-tagged addressing (`dorottex` explicitly calls `gSPSegment(..., SPSEGMENT_MODEL_VTX, ...)`) would report `vtxseg=0x04`, not `0x70`, ruling them out on the addressing evidence too. The rest of `model.c`'s `MODELNODE_OPCODE_OP*` dispatch table (`OP03/OP05/OP06/OP11/OP14/OP16/OP17/OP20`) resolves to literal `return;` stubs in this decomp — dead opcodes, not candidates. Checked `propobj.c` again for any other `vtxallocator`-style builder beyond the already-examined "screen" (in-game TV monitor image) quad system — found none.

**Status: genuinely stuck on locating the write site, not just deferring.** The mechanism is now well-characterized (raw-pointer-addressed, dynamically-built quad batch, confirmed *not* room-static or model-static data, confirmed *not* any of the four known `model.c` dynamic-quad builders) but the actual function remains unidentified after a real search, not a skipped one. **Next step, concrete and different from anything tried in 16 passes:** rather than continuing to guess-and-grep by content/name, instrument at the point of certainty — add a probe to the `G_VTX` case itself (where `g_d236_last_vtx_seg` is now set) that also captures and prints a short run of bytes *around* the raw pointer's neighborhood, or (better, if the harness supports it) print a host stack backtrace via `execinfo.h`/`backtrace()` the first time `rdp.other_mode_l == 0x0c184b50u` is seen after a `G_VTX` load — pinpointing the actual C call site directly instead of inferring it from render-mode/segment signatures. That would settle in one capture what six passes (11 through 16) of signature-matching have not.

**Not v0.3.0-blocking, user is continuing anyway.** `GE_D236RAW` and the `vtxseg` extension to `GE_D236ALPHA` are both in tree, env-gated, zero cost unset. Raw headless dump preserved at `scratch/d236_raw_headless.log`.

**Pass 17 (2026-09-21, fresh session) — WRITER IDENTIFIED: `doshadow`. Plus a false alarm resolved.**

1. **Vertex struct is 16 bytes, not 32**: GE's RSP uses a custom vertex format `{s16 ob[3]; u16 flag; s16 tc[2]; cn[4]}` (alpha at byte 0xF) — no screen-space sx/sy/st/ct. `vtxallocator(n)` = `dynAllocate(n<<4)` = n×16B; the observed +0x40 arena increments are doshadow/dogfnegx's 4-vertex (64B) allocations, not 2×32B Vtx.
2. **Decisive probe**: env-gated backtrace at allocation time (`GE_D236BT`, `get_ptr_allocated_block_for_vertices` in `chr.c`, calling a new C-linkage `port_d236bt_dump()` extracted from the RSP-side unwind logic in `gfx_pc.cpp`). All allocator hits resolve to **`doshadow`** (`model.c:5397`) via `drawjointlist → chrRenderProp → chrpropRender → chrpropsRenderPass → lvlRender`. Zero dotube activity. First allocation's physical address exactly matches the RSP-side rawvtx capture. Pass 16's exclusion of doshadow was based on a wrong claim ("dotube/doshadow use segment-tagged addressing") — only `dorottex` tags G_VTX; both use raw pointers.
3. **doshadow alpha mechanism**: `cn[3] = (envcolour.a * D_800363F0)/255` for CHR/SMOKE props, else `= D_800363F0` — per-frame global shadow intensity, explains the varying low alphas (observed 1→2→12→13→14; probe saw 13 and 80 alternating). All 4 vertices are copies of a constant template (`D_800363F8`). Textured quad via `shadow->image` → `texSelect`.
4. **FALSE ALARM (pass 17c) — the d43_emit.py op-13 "ShadowRecord bug" does not exist.** A ROM scan of all 42 op-13 records (all in C*Z character files, none in P*Z props) initially suggested the converter writes HeaderNode to r+24 / Scale to r+32 / omits BaseAddr — misaligned vs the C struct's *commented* offsets (image@0x10, HeaderNode@0x14, Scale@0x18, BaseAddr@0x1C). But a runtime dump of the live record (`GE_D236BT` probe in `doshadow`) shows **HeaderNode@0x14 = 0 and the valid pointer at 0x18** — because on PC (64-bit) `image` widens to 8 bytes, shifting HeaderNode→0x18, Scale→0x20, BaseAddr→0x28. The converter's r+16/r+24/r+32 layout **matches the PC struct exactly**; the C comments describe the N64 32-bit layout (the D52/A1 pointer-widening class). Live values all sane: image→valid tconfig (32×32, fmt=0), HeaderNode valid, height≈110, size≈(389,413), BaseAddr set by the loader's promotion pass (`modelPromoteNodeOffsetsToPointers` SHADOW case overwrites it with fileramaddr anyway). **Do not "fix" d43_emit.py op-13.**
5. **Conclusion so far**: the faint textured sheets ARE `doshadow`'s drop-shadow quads — large (≈390×413 world units) flat ground quads sampling a tiny 32×32 texture at low per-frame alpha. N64 draws the identical quads (same code, same data). **The user-visible bug is therefore almost certainly that the trees' actual sprite geometry is missing/broken on PC**, leaving only the shadow sheets visible — not a shadow bug.

**Next steps (fresh session):** (a) look at the dumped frames (`ppm/frame_0003xx.ppm`, converted PNGs in /tmp) to characterize what's actually on screen; (b) identify which prop/character model owns the shadow node (node 0x7020ef44, file image base 0x7020eda0 — all 42 op-13 records are in C*Z character files, so check which characters are present on Surface 1 and whether *their* main geometry renders); (c) if the tree sprites are a separate prop class, verify that file's GDL/vertex emission through `d43_emit.py` (D75 blob-bug class is the standing suspect for missing geometry). **TEMP probes in tree (env-gated, zero cost unset):** `GE_D236BT` backtrace probe in `chr.c` + live-record dump in `model.c:doshadow` (writes `d236sh.bin` to CWD on first hit — delete both when D236 concludes), `port_d236bt_dump()` helper in `gfx_pc.cpp`. Untracked scan script: `tools_pc/scan_op13.py`.

**Pass 18 (2026-09-21, static ROM analysis session) — the tree-class RM word is a runtime LUT rewrite; exact source pair identified.**

1. **All tree/plant prop files carry zero renderable geometry — confirmed with the real node parser, not just an opcode histogram.** `PpalmtreeZ`, `PpalmZ`, `Pjungle3_treeZ`, `Pjungle5_treeZ`, `Pplant1`–`Pplant11Z`, `Pgas_plant_*Z`: node set is only {Group(2), BBox(10), DLCollision(24)} — **no op-4 DisplayListRecord anywhere**. Same on N64. The visible tree sprites are therefore drawn by the room background (`bg_sevx_all_p.seg`, ROM 0x66F700, 116176 B; Surface1=level_36, Surface2=level_43), not by prop models.
2. **The measured tree-class RM word `0x0c184b50` exists in no ROM data.** Scanned all 25 non-empty bg files (decompressed RZ GDLs) and all 512 model GDLs: zero hits. The only `0x0c18xxxx` word in ROM is `0x0c182048` (menu logo bg). It is generated at runtime.
3. **Exact source found — `DL_LUT_CLOUD`.** Compiled GE's LUT pairs (`scratch/d236/lutdump.c`, output in `scratch/d236/lutvals.txt`): CLOUD = from `G_RM_PASS|G_RM_AA_ZB_XLU_SURF2` = **`0x0c1849d8`** → to `G_RM_PASS|G_RM_ZB_CLD_SURF2` = **`0x0c184b50`** — exact match with the measured tree-class word. The "from" word is one of the six bg RM words present in ROM (×24 in sevx). `G_RM_PASS = 0x0c080000` explains the high bits: `0x0c08 | 0x0010 (ZB_CLD_SURF2 hi) = 0x0c18`. So: room GDLs contain `SETOTHERMODE_L 0x0c1849d8`, and a runtime `bgApplyDynamicCCRMLUT` pass rewrites them to `0x0c184b50` before the RSP sees them. Note `DL_LUT_BILLBOARD` shares the same "from" word (`0x0c1849d8`) but maps to `0x0c193078` (TEX_EDGE2) — which LUT wins depends on the applied index.
4. **Pass 17's doshadow finding stands but is a different sheet.** doshadow emits `G_RM_ZB_CLD_SURF|G_RM_ZB_CLD_SURF2 = 0x00504b50` (no PASS bits) — a *different* word from the measured tree class. The faint character drop-shadow quads (doshadow, 32×32 shadow texture) and the tree-texture billboards (bg CLOUD-rewritten quads) are two separate draw sources; the user-visible "wall of tree texture" is the latter.
5. **N64 cutout mechanism (hypothesis to verify):** `0x0c184b50` = ZMODE_XLU + FORCE_BL + CVG_WRAP/FULL + IM_RD, alphacompare bits 0–2 = 000 (NONE), no CVG_X_ALPHA. No alpha *test* — the silhouette must come from plain alpha **blending** of the IA texture (`out = src*A + dst*(1-A)`; transparent texels have A=0). The port's `use_alpha` gate in `gfx_pc.cpp` (~line 2386: requires `(oml & (3<<20)) == G_BL_CLR_MEM<<20 && (oml & (3<<16)) == G_BL_1MA<<16`) must be checked against this word's blender bits (bits 16–17 = 00, bits 20–21 = 01) — if the gate rejects it, fast3d draws the quad opaque → exactly the reported "wall/sheet" symptom.

**Next steps (fresh session):** (a) read `bgApplyDynamicCCRMLUT`'s implementation + the `enum CCRMLUT` in `src/game/bg.h`; map the literal indices 1/5/6/7 used at `bg.c:2797–2819` and `CCRMLUT_WALLETBOND` (`front.c:2177`) to tables, and find which index applies `DL_LUT_CLOUD` plus **all** call sites of that index (the room-load path uses 1/5/6/7 only — the CLOUD application may live in a second call path); (b) verify the port's `use_alpha`/blend handling in `port/fast3d/gfx_pc.cpp` for RM `0x0c184b50` (it is already special-cased at ~lines 2166/2217/2259/2279/2356 — D236 probes + zfix; review what they do); (c) if the gate rejects this RM, fix in `port/fast3d` (port layer — rule 2 allows it); (d) when concluded, remove TEMP probes listed above. Scratch tools: `scratch/d236/{lutdump.c,lutbuild.bat,lutvals.txt,scan_bg_rm.py,rmvals.txt}`, `tools_pc/scan_op13.py`.

**Pass 19 (2026-09-22, static session) — both pass-18 leads REFUTED; complete runtime-emitter map for `0x0c184b50` established; the discrete-sprite mechanism identified as `dogfnegx` (op-12 GUNFIRE node).**

1. **`CCRMLUT_CLOUD` (index 4) has ZERO call sites — pass 18's "runtime LUT rewrite" mechanism is dead.** Complete call-site map for `bgApplyDynamicCCRMLUT` (`src/game/bgapply.c:77`): `bg.c:2797/2804/2812/2819` (room load; literal indices 1=`CCRMLUT_PRIMARY_ADDFOG`, 5=`CCRMLUT_SECONDARY_ADDFOG` when `g_FogSkyIsEnabled`, else 6=`CCRMLUT_PRIMARY`, 7=`CCRMLUT_SECONDARY`) and `front.c:2177` (`CCRMLUT_WALLETBOND`). `CCRMLUT_BILLBOARD` (2), `CCRMLUT_WATER` (3) and `CCRMLUT_CLOUD` (4) are defined (`bondconstants.h:2931-2942`) but never applied anywhere in the compiled set — the `DL_LUT_CLOUD` table itself exists (`bg.c:2174`, comment: "Transparent to Cloud (Saves AA - Stops Jaggies from appearing behind BillBoard)") mapping exactly `0x0c1849d8 → 0x0c184b50`, but its enum index is never passed to `bgApplyDynamicCCRMLUT`. Corroborating: the ADDFOG LUTs that ARE applied map the same "from" word `0x0c1849d8` → `G_RM_FOG_SHADE_A|G_RM_AA_ZB_XLU_SURF2` = **`0xc81049d8`** (`bg.c:2039-2040`, `2061-2062`) — exactly D276's confirmed noise-weave class. So the sevx room-bg quads that carry `0x0c1849d8` in ROM end up as `0xc81049d8` at runtime (fog enabled), NOT as tree cards.
2. **The only code that emits `0x0c184b50` is `texSetRenderMode` mode 4 ("cloud"), 2-cycle, zbuffer-on** (`othermodemicrocode.c:300`: `gDPSetRenderMode(G_RM_PASS, G_RM_ZB_CLD_SURF2)` = `0x0c184b50` exactly), reached from its callers via `sub_GAME_7F073038` (`model.c:4940-4942`) → `texSelect(..., arg2=4, ...)` → `texSetRenderMode`. Mode-4 `texSelect` call sites: **`model.c:5369` `dogfnegx`** (op-12 `MODELNODE_OPCODE_GUNFIRE`), **`model.c:5545` `doshadow`** (op-13 `MODELNODE_OPCODE_SHADOW`), `glass2.c:732` bullet sparks, `gunfire.c:1512/1518/1524` flares, `explosion.c:1890` smoke (zbuf=1), `front.c` menu/crosshair (zbuf=0 → emits `0x0c184340` instead). Note the 1-cycle variant (emitted when `tconfig==NULL` or `tconfig->level==0`, `othermodemicrocode.c:415`) emits `G_RM_ZB_CLD_SURF|G_RM_ZB_CLD_SURF2` = `0x00504b50` with zbuffer on (`0x00504340` without) — so pass 18's "doshadow emits `0x00504b50`" holds only for the 1-cycle case; with a mipmapped texture + zbuffer on, **both doshadow and dogfnegx emit exactly `0x0c184b50`** (2-cycle combine: `G_CC_TRILERP`/`G_CC_MODULATEIA2` for RGBA/IA → A = texel.a × shade.a). Pass 17's backtrace attributed the tree class's vertex allocations to `doshadow`, but `dogfnegx` is a co-emitter through the identical `drawjointlist → chrRenderProp` path and the same `vtxallocator(4)` + raw-pointer pattern — the attribution may have resolved only the more frequent of the two.
3. **`dogfnegx` IS the discrete camera-facing sprite mechanism** (`model.c:5225-5380`): it computes the view direction from `rodata->Offset` through the node's model matrix, builds a rotation aligning the quad to face the camera, applies per-frame random scale (0.75–1.25) and random UV rotation around the texture centre, and draws a `rodata->Size`-sized textured quad with mode-4 cloud RM. Gated on `(renderdata->flags & 2) && rwdata->Gunfire.visible`. `ModelRoData_GunfireRecord` (`bondtypes.h:1151`): Offset@0x0, Size@0xC, Image@0x18, Scale@0x1C, RwDataIndex@0x20. This is almost certainly what N64 shows as the "discrete tree sprites".
4. **The port's `use_alpha` gate ACCEPTS `0x0c184b50` — pass 18's prime suspect is refuted by static decode.** Against this repo's gbi.h blender layout (`include/PR/gbi.h:720-723`: `GBL_c2` puts c2-modifier at bits 16-17, c2-secondary-colour at bits 20-21 of other_mode_l): the gate (`gfx_pc.cpp` ~line 2386) requires bits 16-17 == `G_BL_1MA`(0) and bits 20-21 == `G_BL_CLR_MEM`(1); the word has byte2=`0x18` → bits 16-17=0 ✓, bits 20-21=1 ✓ → `use_alpha=true`. Full decode: ALPHACOMPARE=NONE, no CVG_X_ALPHA/ALPHA_CVG_SEL, FORCE_BL set, c1=(IN, A=0), c2=(IN, A_IN over MEM, 1MA) = standard src-over alpha blend with A from the combiner (`texSelect` mode-4 2-cycle sets `G_CC_TRILERP`/`G_CC_MODULATEIA2` for IA → A = texel.a × shade.a). No port-side opaque-draw gate explains the wall.
5. **Implication / reframed picture:** with the tree class alpha-blended correctly in the port, the consistent story across D276 (visible weave painted by the `0xc81049d8` room-bg noise class), D280 (tree cards in front yet not visibly winning) and pass 15 (tree-class vertex alpha capped ≤36/255 ≈ 14%, bimodal 25/255) is that the tree cards blend at ~10% opacity over the opaque noise wall — nearly invisible, exactly "the wall shows through". The remaining open questions are (a) whether that low alpha is ROM-faithful (doshadow's `cn[3] = envcolour.a × D_800363F0/255`, `D_800363F0` set per-chr from shade alpha at `chr.c:3319`, init 0x50; dogfnegx uses template `D_800363E0`) or a port-fed wrong input, and (b) whether the discrete billboards (`dogfnegx`) actually instantiate on PC at all — its gate inputs (`renderdata->flags & 2`, `rwdata->Gunfire.visible`) are unverified.

**Next steps (fresh session):** (a) ROM-holding session: run new `tools_pc/scan_op12.py` (added this pass; counts op-12/op-13 nodes per model file, dumps GunfireRecord Offset/Size/Image) to identify which models own Surface 1's tree billboards/shadows and cross-ref against pass 17's D236SH live-record data (32×32 fmt=0 shadow texture); (b) runtime: `GE_D236ALPHA` + `GE_D236RM` on `-level_36`, correlate the tree class's per-vertex alpha with `D_800363F0`/envcolour.a, and compare against N64 reference footage (faint vs solid sprites decides faithful-faint vs port bug); (c) verify `dogfnegx`'s gate inputs on PC (`renderdata->flags & 2`, `rwdata->Gunfire.visible`) — if billboards never fire, the discrete sprites are missing entirely (rule-2 bucket (a)/(b) question); (d) on conclusion remove TEMP probes: `GE_D236RM/ORDER/ALPHA/ZFIX/BT` in `gfx_pc.cpp`, `GE_D236BT` + D236SH dump in `model.c:doshadow`, `port_d236bt_dump()`, `tools_pc/scan_op1{2,3}.py`. (Probes stripped `0503d335`; the two scan tools removed 2026-09-30, strip pass.) Scratch tools: `scratch/d236/{lutdump.c,lutbuild.bat,lutvals.txt,scan_bg_rm.py,rmvals.txt,rmdump.c,rmbuild.bat,rmnames.txt}`.

**Pass 20 (2026-09-22, ROM-holding session) — pass-19 step (a) executed: full 512-file `scan_op12.py` run; the `dogfnegx`=trees identification is REFUTED by the asset data.**

1. **Verified scan result** (re-ran `tools_pc/scan_op12.py ntsc-final` this pass). *Note: the script's final line "total files with op12: 62" is a reporting artifact — that counter accumulates every file with EITHER opcode, so it equals the op13 file count; the real per-opcode totals are below.* Across all 512 converted model files:
   - **op-12 (`MODELNODE_OPCODE_GUNFIRE` / `dogfnegx`) — 21 nodes total, ALL in weapon/gun/tank models.** 16 one-node weapon files (`PchrautoshotZ`, `Pchrfnp90Z`, `PchrgoldenZ`, `PchrgrenadelaunchZ`, `PchrkalashZ`, `Pchrm16Z`, `Pchrmp5kZ`, `Pchrmp5ksilZ`, `PchrrugerZ`, `PchrshotgunZ`, `PchrskorpionZ`, `PchrspectreZ`, `Pchrtt33Z`, `PchruziZ`, `PchrwppkZ`, `PchrwppksilZ`) + `PgroundgunZ`(2), `Pgun_runway1Z`(1), `ProofgunZ`(1), `PtankZ`(1). The dumped records confirm muzzle-flash semantics: barrel-relative offsets (e.g. shotgun `(-375,0,16)`), modest sizes, small embedded-image indices (`img` 0x18–0x6c, `vma?False`). `PtankZ`'s single node is a large smoke/flash quad (`(0,5,0)`, size 2143×1026).
   - **op-13 (`MODELNODE_OPCODE_SHADOW` / `doshadow`) — 42 nodes total, ALL in C*Z character files** (one per character; matches pass 17's "42 op-13 records, all in C*Z").
   - **No tree/plant model carries either node.** The 16 foliage models present (`Pjungle3_treeZ`, `Pjungle5_treeZ`, `PpalmZ`, `PpalmtreeZ`, `Pplant1Z/2Z/2bZ/3Z/11Z`, `Pgas_plant_*Z`, `Pgasplant_clear_doorZ`) contain **only** `{GROUP(2), BBOX(10), DLCOLLISION(24)}` nodes — static geometry + collision, zero billboard/shadow nodes.

2. **Refutation of pass 19's central identification.** Pass 19 concluded "`dogfnegx` (op-12 GUNFIRE) is the discrete camera-facing billboard [that draws the trees]" and that the `0x0c184b50` class is emitted by dogfnegx/doshadow. But `dogfnegx` has **zero tree instances** in the entire asset set — every one of its 21 nodes is a weapon muzzle flash or tank effect. So the `0x0c184b50` class that passes 17/19 chased (pass 17's backtrace attributed its vertex allocations to `doshadow`) is, on this level set, most plausibly **character drop-shadows + weapon muzzle flashes**, *not* the trees. Passes 17–19 appear to have been instrumenting the wrong draw class for the reported symptom.

3. **Redirection: the visible trees are static geometry, not dynamic billboards.** Given (1), the Surface 1 trees must be drawn from static data — either (a) the foliage PROP models (`PpalmZ`/`Pjungle3_treeZ`/etc., which DO load and contain GROUP/BBOX/DLCOLLISION nodes) or (b) the level background file `bg_sevx_all_p.bin`. Both are static; neither uses the dogfnegx/doshadow path. **Honest caveat — not yet disambiguated:** my offline decode of `bg_sevx_all_p`'s room secondary-DL streams found billboard-shaped cards (G_VTX+G_TRI4 with a 0xC0/G_SETTEX texture-select), but their OML words (`0x0c1849d8`/`0x0c184dd8`) do *not* match the runtime `0x0c184b50`, and pass 19 established those exact bg quads become the **noise wall** (`0xc81049d8`) at runtime (fog-enabled ADDFOG LUT). So it is NOT yet confirmed whether the visible tree sprites come from the foliage prop models or the bg file — that needs a runtime attribution.

4. **Next step, concrete (supersedes pass 19's b/c):** a single runtime probe that attributes the on-screen tree-region triangles to their draw source — tag `gfx_sp_tri*` triangles by whether their vertices came from a **model-node GDL** (prop/model path) vs a **bg room DL** (`g_BgRoomInfo[].ptr_*_expanded_mapping_info`, SPSEGMENT_BG_VTX=14 / the bg-widened arena), and for the visible tree pixels log the active RM + texture/tile. That one capture decides (a)-vs-(b) and ends the "which class is actually the trees" question 19 passes have been circling. If it's the foliage prop models, the standing D75 blob-conversion suspect (missing/mis-converted mesh or texture in `d43_emit.py`) becomes the prime fix target; if it's the bg file, the D85/D276 bg-DL + noise-wall thread resumes.

**TEMP probes still in tree (env-gated, zero cost unset) — remove when D236 concludes:** `GE_D236RM/ORDER/ALPHA/ZFIX/BT` (`gfx_pc.cpp`), `GE_D236BT` backtrace + D236SH dump (`model.c:doshadow`, writes `d236sh.bin`), `port_d236bt_dump()` helper, `tools_pc/scan_op1{2,3}.py`.


**Pass 21 (2026-09-21, static session) — pass-20's step (a)-vs-(b) ANSWERED STATICALLY, no runtime probe needed: the Surface 1 trees cannot be prop models, because the level places none. Prop/`d43_emit.py` conversion is retired as a D236 fix target; the bg-file branch is the only one left.**

Pass 20 closed by proposing a runtime draw-source attribution probe (tag `gfx_sp_tri*` by model-node GDL vs bg room DL) to decide whether the visible trees are (a) foliage prop models or (b) static `bg_sevx_all_p` geometry. That probe is **not necessary** — the setup data answers it, and two independent static lines of evidence agree.

1. **No Severnaya-type tree model exists in the asset set at all.** All 16 foliage models in `data/pcmodels-ntsc-final/manifest.csv` are jungle-themed (`Pjungle3_treeZ`, `Pjungle5_treeZ`, `PpalmZ`, `PpalmtreeZ`, `Pplant1Z/2Z/2bZ/3Z/11Z`, `Pgas_plant_*Z`), and the `PROP` enum in `src/bondconstants.h` describes each one as "Jungle …" (`PROP_JUNGLE3_TREE` = "Jungle Large Tree", `PROP_PALM` = "Jungle Palm Tree", `PROP_PLANT2B` = "Jungle Plant, Low Shrub", …). There is no pine/fir/snow-tree prop anywhere in the 512-model set.

2. **Surface 1's setup places zero foliage props — and neither does any other setup in the repo.** New tool `tools_pc/scan_setup_props.py` parses the `PROP` enum out of `src/bondconstants.h` and the object records out of `assets/obseg/setup/Usetup*Z.c`, then reports which props each setup instantiates. For `UsetupsevxZ.c` (Surface 1 — `bg.c:201` maps `LEVELID_SURFACE` → `bg/bg_sevx_all_p.seg` + this setup) it matches **219 object records**, of which **all 83 `StandardProp` records are individually accounted for** (`PROP_WOOD_SM_CRATE4`×31, `PROP_BOOK1`×16, `PROP_OIL_DRUM7`×12, `PROP_WOODEN_TABLE1`×4, `PROP_BOOKSHELF1`×4, `PROP_PADLOCK`×4, `PROP_DESK_LAMP2`×2, `PROP_METAL_CHAIR1`×2, `PROP_JERRY_CAN1`×2, plus one each of `PROP_MOTORBIKE`, `PROP_CARBMW`, `PROP_STOOL1`, `PROP_HATPEAKED`, `PROP_KEYBOARD1`, `PROP_CHRMAP`) — **not one foliage prop**. Same result for all 14 setups present in the repo (`arch ark azt cave control cryp dam depo pete run sevbunker sevb sevxb sevx`), including Dam/Runway/Streets, which also have visible outdoor greenery. GE's outdoor trees are never props.

   *Positive control (the zero is meaningful).* A "0 foliage" count is only evidence if the detector can fire. The tool now self-tests and prints it: `PROP enum: 341 entries; foliage props recognised (9): PROP_JUNGLE3_TREE, PROP_JUNGLE5_TREE, PROP_PALM, PROP_PALMTREE, PROP_PLANT1, PROP_PLANT11, PROP_PLANT2, PROP_PLANT2B, PROP_PLANT3`. The matcher resolves every foliage name in the enum and still reports none placed. The record parser is independently sanity-checked by the distinctive props it *does* find in Surface 1 — motorbike, safe + safe door, the map objective item, the Yale and bolt keys, `PROP_HATCHSEVX`, `PROP_SEVDISH` — all of which really are in that level.

   *Tooling bug caught during this pass, worth recording:* the first version of `load_prop_enum()` located the enum with `l.strip().startswith("typedef enum PROP")`, which matches `typedef enum PROPFLAG` (`bondconstants.h:294`) hundreds of lines earlier. That yields an enum containing **no `PROP_*` members at all**, so every `obj` resolved to `?` and every foliage count was a meaningless 0. The run *looked* exactly like a clean negative result. Fixed to an exact `l.strip() == "typedef enum PROP"` match, and the self-test above exists specifically so this failure mode cannot masquerade as a finding again. This is the D219 probe lesson in a different costume (**a diagnostic must be shown capable of producing the positive before its negative is trusted**).

3. **What this settles.** Pass 20's alternative (a) is dead: **the visible Surface 1 trees are static background geometry in `bg_sevx_all_p.seg`**, reached through `bgLoadRoomPrimaryGdl` / `bgLoadRoomSecondaryGdl`. The standing D75 blob-conversion suspect (`tools_pc/d43_emit.py` mis-converting a prop mesh or texture) is therefore **not** a D236 fix target and should not be pursued for this bug. This independently corroborates D280's ninth-pass reframing ("the remaining work is CPU-side instantiation/room-streaming, not a port fix") and explains why 19 passes of model-node instrumentation (op-12/op-13, `dogfnegx`/`doshadow`) found nothing tree-shaped: **there was never a tree model to find.**

4. **Render-mode path re-checked while here (no bug found; recorded so pass 22 does not re-derive it).** The bg-authored foliage/billboard cards carry `G_RM_AA_ZB_XLU_SURF` (`0x…49d8`), and `DL_LUT_BILLBOARD` (`bg.c:2151`, `CCRMLUT_BILLBOARD` = index 2) exists precisely to rewrite `XLU_SURF` → `G_RM_AA_ZB_TEX_EDGE` ("Transparent 1Cycle to BillBoard 1Cycle") — the alpha-cutout mode that would give discrete silhouettes instead of a blended sheet. **But index 2 is never applied**, on N64 or PC: the only call sites are `bg.c:2797/2804/2812/2819` (room load, literals 1/5/6/7 = `PRIMARY_ADDFOG`/`SECONDARY_ADDFOG`/`PRIMARY`/`SECONDARY` by `g_FogSkyIsEnabled`) and `front.c:2177` (`WALLETBOND`). So `BILLBOARD`, `WATER` and `CLOUD` are all dead LUTs in the original game too — this is **not** the PC/N64 divergence, and "apply the BILLBOARD LUT" is not a legitimate fix (it would be a game-logic change, rule 2).

   Also checked and **cleared**: `bgApplyDynamicCCRMLUT`'s scan bound. `Gfx` is 16 bytes on PC (`include/PR/gbi.h`: `Gwords { uintptr_t w0, w1; }`), and `end` is computed as `ptr + usize_primary_DL_binary` in *bytes* — but `usize_*` is assigned from `expanded_size` **after** `bgWidenRoomGdl()` has already rewritten it to the widened PC byte count (`bg.c:2526`/`2559`), so the scan covers the whole room DL, not half of it. No 32→64-bit truncation here. The LUT tables themselves are layout-safe too: their scalar initializers (`DL_LUT_WATER`'s raw `0xB900031D, 0x00552078, …`) brace-elide into `Gfx.words.w0/w1` and zero-extend correctly at either width.

5. **Next step for pass 22 (bg branch only).** The question is now narrow: *within `bg_sevx_all_p`'s room display lists, which draw class carries the tree cards, and why does it not reach the screen?* D280 established that the `0x0c184b50` class sits in front of the noise quad, has CPU-baked bimodal fog (25/255) and "barely draws at all" under a deterministic `-level_36` boot — but pass 20 proved that word is only ever emitted at runtime by `texSetRenderMode` mode 4 (shadows/muzzle flashes/sparks), so it is **not** the bg tree class either. The remaining candidates are the bg-authored classes D269/D272 catalogued (`oml=0xc4112078` OPA and `0xc41049d8` XLU, both CI8) versus D276's `0xc81049d8` IA8 noise painter. Concretely: dump the Surface 1 room GDLs with the existing `GE_D85DUMP` probe (no new code) and census the distinct `oml`/texture pairs per room, then match against a room the user can stand in front of trees in. That is a static-plus-one-run step, not a new instrumentation build.

**Tooling added this pass:** `tools_pc/scan_setup_props.py` (read-only, no ROM needed, runs against the repo's own sources). Unlike the `GE_D236*` env probes it costs nothing at runtime and is generally useful — keep it rather than removing it when D236 concludes.


**Pass 22 (2026-09-21, static session) — pass-21's owed step done OFFLINE, no run: complete render-mode census of Surface 1's bg room DLs, straight out of the ROM. D276's wall painter `0xc81049d8` is now tied to its authored source (bg *secondary* DL, `G_RM_AA_ZB_XLU_SURF`, 24 of 42 rooms). No bg file in any level authors the TEX_EDGE "billboard cut-out" class, so the tree cards are plain alpha-blended quads and the remaining suspicion falls on their texture's alpha, not on geometry, culling, fog or billboard orientation.**

Pass 21 proposed dumping the Surface 1 room GDLs with `GE_D85DUMP` and censusing the `oml`/texture classes — a build plus one run. That turned out to be unnecessary too: the bg `.seg` files are stored **uncompressed** in the ROM (they have to be — `obLoadBGFileBytesAtOffset` random-accesses them by byte offset), so the whole structure is readable offline.

**Two new tools, both read-only, no ROM-dependent build, no C compiler:**

* `tools_pc/bg_gdl_census.py` — decodes `bg_<level>_all_p` out of the ROM, walks every room's primary and secondary GDL, and censuses the distinct render modes per room. It reproduces `bg.c`'s own structure exactly: file word[1] → the 24-byte `bg_room_data` array (`bg.c:865`); a room's compressed blob size = distance to the next non-zero mapping-bin offset with primary and secondary treated as one interleaved stream (`bg.c:944-990` + `getPri/SecMappingBinCount`); each blob raw-deflate behind its own 2-byte header (`bgDecompress` → `decompressdata`, which is just "skip 2 bytes, inflate").
  *Validation:* **every** blob in every level tested inflates cleanly with zero failures. Since the sizes are derived, not stored, a single off-by-one in that derivation would corrupt the stream and fail inflate — so a clean sweep is a real check on the derivation, not a vacuous one.
* `tools_pc/rendermode.py` — evaluates the `G_RM_*`/`RM_*(clk)`/`GBL_c1`/`GBL_c2` macros out of `include/PR/gbi.h` and the `DL_LUT_*` tables out of `src/game/bg.c` in pure Python (93 `G_RM_*` composites resolve). It decodes any `oml` word into flags + blender equation, and produces the (from → to) rewrite maps for the LUTs `bg.c` actually applies. *Cross-check:* it independently reproduces pass 19's `0x0c1849d8 → 0xc81049d8` ADDFOG mapping, which pass 19 derived by hand.

1. **Surface 1 (`bg_sevx_all_p`, 42 rooms) authors exactly six render-mode classes** — the complete set, with their fog-on LUT images:

   | authored | fog-on (LUT 1 pri / 5 sec) | setups | rooms | mode |
   |---|---|---|---|---|
   | `0x0c182078` | `0xc8102078` | 39 | 39 | `AA_ZB_OPA_SURF` (primary DLs) |
   | `0x0c1849d8` | `0xc81049d8` | 24 | 24 | `AA_ZB_XLU_SURF` (secondary DLs) |
   | `0x0c184dd8` | `0xc8104dd8` | 15 | 15 | `AA_ZB_XLU_DECAL` |
   | `0x0c192d58` | `0xc8112d58` | 7 | 7 | `AA_ZB_OPA_DECAL` |
   | `0x00502078` | (unchanged) | 2 | 2 | OPA_SURF, no-fog blender |
   | `0x005049d8` | (unchanged) | 1 | 1 | XLU_SURF, no-fog blender |

2. **D276's wall painter is now attributed to authored data.** `0xc81049d8` — the class D276 identified as the thing actually painting the visible weave — is **exactly** the LUT image of the bg *secondary* DL class `0x0c1849d8` (`G_RM_AA_ZB_XLU_SURF`), present in 24 of Surface 1's 42 rooms. Nineteen passes have logged that word live; this is the first time it has been traced back to a specific authored source in a specific file. It is ordinary bg transparent-surface geometry, and there is a lot of it.

3. **`LEVELID_SURFACE` == 36**, so the `-level_36` deterministic boot used by D269/D272/D276/D280 really was Surface 1 (`LEVELID_STATUE = 22` + 14 sequential entries; `bg.c:201` then maps it to `bg/bg_sevx_all_p.seg`). Worth stating explicitly — several passes' conclusions rest on it and none of them checked.

4. **No bg file in ANY level authors the TEX_EDGE "billboard cut-out" class `0x0c193078`.** Censused `sevx sevb sev dam jun run depo azt` — the union of their authored render modes is `{0c182078, 0c182048, 0c1849d8, 0c184dd8, 0c184e50, 0c192d58, 00502078, 005049d8, 00504dd8, 00552d58}`, and `0x0c193078` appears in none of them, **including Jungle and Depot**. That matters because `DL_LUT_PRIMARY_ADDFOG` carries a dedicated rewrite pair for it with Rare's own comment: *"Billboard Cut-out to FOG Billboard Cut-out - eg, Mario Tree or Depot lamp"*. So the engine's TEX_EDGE billboard path is real but is **never fed from bg geometry** — it belongs to the prop/model path (which is where the jungle foliage props live, per pass 21). Combined with pass 21, Surface 1 has neither tree props nor TEX_EDGE bg cards.

5. **Consequence — the mechanism is narrowed to texture alpha.** Surface 1's tree cards can only be plain `XLU_SURF` textured quads in the secondary DLs. `XLU_SURF` is `GBL_c2(CLR_IN, A_IN, CLR_MEM, 1MA)` — a straight src-over blend with **no alpha test and no coverage-from-alpha**. Under that mode a card is discrete only if its texture's alpha is transparent outside the silhouette. So "sheet instead of discrete trees" is, mechanically, "the alpha of this class's texture is opaque where it should be transparent" — which is precisely what D276 found and measured (*"fully opaque, alpha nibble mostly 0xf"*, 1023/1024 texels at A=F per D267). **This retires, for this class, the geometry/culling/fog/billboard-orientation branches** that D269→D280 cycled through: none of them can turn a correct-alpha XLU_SURF card into a sheet, and none of them are needed to explain it.

   The open sub-question is no longer *"which draw class"* but *"is this texture's format/alpha being decoded correctly at all"*. D265 tried IA4 `I3:A1`→`I2:A2` (reverted), D267 dumped the IA8 set and dismissed it on a single-instance visual judgement, D273 ruled out the CI palette layout, D276 re-identified the IA8 class but never questioned whether `fmt=3 siz=1` is the *right* interpretation of those bytes. A 32×32 image that decodes as "high-frequency blocky gray static with opaque alpha" is exactly what a tree texture looks like when read at the **wrong format or wrong bit-split** — that specific possibility has never been tested.

6. **Anomaly to resolve before trusting the older logs.** D269 (M-128) recorded the two live Surface 1 classes as `oml=0xc4112078` (OPA) and `0xc41049d8` (XLU). The LUT images derived here are `0xc8112078` and `0xc81049d8`. The difference is exactly one field — cycle-1's fog alpha source, bits 26-27: `G_BL_A_SHADE` (2, what the LUT writes via `G_RM_FOG_SHADE_A`) vs `G_BL_A_FOG` (1, what D269 logged). D276 and D280 logged the `c8` (A_SHADE) form, matching the LUT. So either something rewrites the fog alpha source after `bgApplyDynamicCCRMLUT`, or D269's words were mis-transcribed and three passes of "two draw classes per room stream" rest on them. Decide this by decoding a live log with `python tools_pc/rendermode.py <word>` rather than by eye — it is a one-bit-field difference that is very easy to read past, and it changes the fog blend if real.

7. **Also recorded (fog-off path).** `DL_LUT_PRIMARY` (6) and `DL_LUT_SECONDARY` (7) contain **zero** render-mode rewrite pairs — their bodies are entirely `gsDPSetCombineMode`/`gsDPSetCombineLERP`. So when `g_FogSkyIsEnabled` is false, room render modes are left exactly as authored and only combiners are patched. (This is a correct reading of the tables, not a parser gap: the tool finds 8 pairs in LUT 1 and 3 in LUT 5 from the same code path.)

**Next step for pass 23 (one run, or possibly none):** re-dump the Surface 1 textures for the `0xc81049d8` class with the existing `GE_TEXDUMP`/`GE_TEXRAW`/`GE_D270KEEPOML=1049d8` probe combination (the current `texdump/` in-tree is from a D75 logo capture, not a Surface run), then decode the dumped bytes offline under *every* plausible format hypothesis — IA8 as currently assumed, IA4 both bit-splits, I8, I4, CI4/CI8 with the room's TLUT, RGBA5551 — and look for the one that yields a tree silhouette with a sensible alpha channel. Cheap, offline, and it directly tests the one mechanism left standing. Resolve item 6's `c4`/`c8` discrepancy from the same run's log.

**Tooling added this pass (keep — read-only, no runtime cost):** `tools_pc/bg_gdl_census.py`, `tools_pc/rendermode.py`.


**Pass 23 (2026-09-21, static session) — THE TREE CARDS ARE IDENTIFIED. Surface 1's `0x0c1849d8`/`0xc81049d8` class binds 13 distinct textures, and four of them — `tex1198`–`tex1201`, `RGBA16` (RGBA5551, ONE bit of alpha), 64×17, `HUFFLOOKUP`, six EXPLICIT LODs — are each bound in 21 of the level's 42 rooms, 3.5× the next most widespread. They are the only explicit-LOD textures in the entire Surface 1 bg. They would log as `fmt=0 siz=2`, which is why D265's IA hunt, D269/D272/D273/D274's CI8 hunt and D276's IA8 hunt all missed them: every probe filter in this investigation has been looking at minority members of the right render-mode class.**

Pass 22 left the census with `tex=0` — Surface 1's room DLs contain no `G_SETTIMG` at all. That turned out to be the key, not a dead end.

1. **bg room DLs reference textures by NUMBER, not address.** `texLoadFromGdl` (`tex.c:779`) has `case G_NOOP: texnum = in->words.w1 & 0xfff;` — opcode `0xC0`, 329 occurrences in Surface 1's room DLs — and resolves each into real texture commands at room load. So the texture identity of every bg draw is readable offline, by tracking the active render mode across the command stream and recording the `G_NOOP` texnums bound under it. `bg_gdl_census.py` now does this.

2. **Reading the texture headers offline.** A texture's ROM blob begins with a control byte (`image.c:2568`): bit 7 `hasExplicitLods`, bit 6 `iszlib`, bits 0-5 `lod` (image count); the bitstream starts at **byte 1**. `iszlib=0` → `texInflateNonZlib`'s 24-bit `ffff wwwwwwww hhhhhhhh cccc` header; `iszlib=1` → `texInflateZlib`'s `format u8, numcolours-1 u8, palette[], w/h per image`. New tool `tools_pc/texheader.py`; offsets/sizes come from the existing `imagelist.u.csv`.
   *Sanity check, and a near-miss worth recording:* the first attempt forgot the control byte. The resulting parse **still looked plausible** — it produced four "formats" and confident dimensions — but 1020 of 2699 entries were implausible and the format nibble only ever took the values 0/4/8/12, the tell that the field was reading across a boundary. With the byte-1 offset, **1** of 2699 is implausible and the dimension histogram is dominated by 64×32, 64×64, 32×64, 32×32. The tool prints that count every run so the offset can't silently drift again ([[porting-notes §E positive-control rule]] — same class as pass 21's enum bug).

3. **The full texture list for the wall class** (`oml=0x0c1849d8` → live `0xc81049d8`), by room coverage:

   | texture | rooms | ROM format | dims | compmethod | explicit LODs | logs live as |
   |---|---|---|---|---|---|---|
   | `tex1201` | **21** | `RGBA16` | 64×17 | HUFFLOOKUP | **yes (6)** | `fmt=0 siz=2` |
   | `tex1200` | **21** | `RGBA16` | 64×17 | HUFFLOOKUP | **yes (6)** | `fmt=0 siz=2` |
   | `tex1199` | **21** | `RGBA16` | 64×17 | HUFFLOOKUP | **yes (6)** | `fmt=0 siz=2` |
   | `tex1198` | **21** | `RGBA16` | 64×17 | HUFFLOOKUP | **yes (6)** | `fmt=0 siz=2` |
   | `tex149` | 6 | `IA8` | 32×64 | RLELOOKUP | no | `fmt=3 siz=1` |
   | `tex806` | 5 | `IA8` | 64×32 | HUFFLOOKUP | no | `fmt=3 siz=1` |
   | `tex844` | 4 | `RGBA16_CI8` | (zlib, 180 col) | — | no | `fmt=2 siz=1` |
   | `tex1373` | 3 | `IA8` | 64×32 | RLE | no | `fmt=3 siz=1` |
   | `tex239` | 2 | `IA8` | 64×32 | HUFFBLUR | no | `fmt=3 siz=1` |
   | `tex241`/`tex151` | 1 | `RGBA16_CI8` | (zlib) | — | no | `fmt=2 siz=1` |
   | `tex619` | 1 | `IA8` | 32×64 | RLELOOKUP | no | `fmt=3 siz=1` |
   | `tex238` | 1 | `IA8` | 64×32 | RLE | no | `fmt=3 siz=1` |

4. **This resolves the D269-vs-D276 contradiction.** Three passes argued about whether "the wall" is CI8 (D269/D272: `fmt=2 siz=1`) or IA8 (D276: `fmt=3 siz=1`). **Both are members of this one render-mode class, and both are minority members.** D269 sampled a CI8 card and generalised; D276 sampled an IA8 card and generalised and called D269's reframing premature. Neither was wrong about what it sampled; both were wrong to treat their sample as *the* class. The dominant members — four textures covering half the level — are `fmt=0 siz=2` **RGBA16**, a format nobody has filtered for or dumped in 22 passes.

5. **Why these four are the tree cards** (inference, stated as such — see the honesty note below). Four consecutive texture numbers; near-identical compressed sizes (1569/2035/1902/1964 bytes); identical geometry (64×17) and codec; each bound in exactly 21 of 42 rooms — i.e. every outdoor room, four variants apiece; drawn in the *secondary* (transparent) pass. That is precisely the signature of a small set of foliage/treeline billboard variants scattered across an outdoor level. Nothing else in the level looks like this.

6. **Mechanism hypothesis, now concrete for the first time.** `RGBA16` is RGBA**5551** — exactly **one bit** of alpha. A 64×17 strip is a wide, short card: a treeline band whose alpha bit is 0 for sky above the silhouette and 1 for tree. It is drawn under `G_RM_AA_ZB_XLU_SURF`, which pass 22 established is a straight src-over blend with **no alpha test and no coverage-from-alpha**. So the silhouette exists *only* in that one alpha bit, and the card is minified hard at distance — which is why Rare authored **six explicit LOD images** for it, the only explicit-LOD textures in this entire bg. Two ways that becomes a sheet on PC, both untested:
   * **the 1-bit alpha is decoded as always-opaque** → a solid rectangle of tree-coloured texture tiled across the horizon; or
   * **the authored LOD chain is dropped and GL generates mips by averaging** → the 0/1 alpha becomes fractional, and with no alpha test a minified treeline smears into a translucent band instead of crisp cutouts.

   The second is the concrete, asset-grounded version of the "TEXTLOD/mip sampling" suspicion D269 raised, D272 reframed to, and D274 parked for lack of a mechanism. It now has one.

7. **Honesty note — what is NOT established.** The pixels have not been decoded: `HUFFLOOKUP` is one of Rare's huffman/RLE/lookup codecs and none of them are implemented offline, so "these are trees" is a strong inference from format, geometry, count and room coverage, **not** a decode. It should be confirmed before any fix is built on it. Equally, no claim is made here that either mechanism in (6) is the actual bug — both are hypotheses that now have a specific, cheap test.

**Next step for pass 24 — one run, tightly specified:** boot `-level_36` and dump the textures for this class with the probe filter set to **`fmt=0 siz=2`** (RGBA16) rather than the IA/CI filters every previous pass used — `GE_TEXDUMP=1 GE_TEXRAW=1 GE_D270KEEPOML=1049d8`, then match `GE_TEXI[N] addr=` against the dump by address as D275 did. Three things to read off that one capture: (a) do `tex1198`–`tex1201` arrive as 64×17 RGBA5551, or has the width/height/format been mangled on the way in (D265 already found one transposed-dimensions bug in this area); (b) is the alpha bit non-trivial, or opaque everywhere; (c) do all six explicit LOD images arrive, or only LOD 0. Also resolve pass 22's item 6 (`c4` vs `c8` fog alpha) from the same log using `python tools_pc/rendermode.py <word>`.

**Tooling added/extended this pass (keep — read-only, no runtime cost):** `tools_pc/texheader.py` (new); `tools_pc/bg_gdl_census.py` now tracks `G_NOOP` texnums and prints the decoded ROM header for every texture each render-mode class binds.


**Pass 24 (2026-09-21, static session) — THE TREE TEXTURE IS DECODED. `tex1198` LOD 0 is a 64×17 RGBA5551 treeline strip, 44.2 % opaque, and its alpha channel renders as an unmistakable row of trees. Rare authored a six-level LOD pyramid for it that PRESERVES the silhouette and its alpha coverage all the way down (44.2 % → 43.1 % → 38.8 %). Pass 23's mechanism (b) — authored LOD chain replaced by averaged mips — now has hard ground truth behind it, and the reference image to diff a live capture against exists offline. Still no build, still no run.**

Pass 23 identified the candidate cards but could not decode them (`HUFFLOOKUP` unimplemented). That is now done: `tools_pc/texdecode.py` ports `texReadBits` (`image_bank.c:99`), `texInflateNonZlib`'s image loop, `texBuildLookup`, `texInflateHuffman`, `texInflateRle` and `texInflateLookupFromBuffer` from `src/game/image.c`, covering the `LOOKUP` / `HUFFLOOKUP` / `RLELOOKUP` compmethods.

1. **`tex1198` LOD 0 — 64×17 RGBA16 (RGBA5551), HUFFLOOKUP, 481/1088 texels opaque (44.2 %).** Its 1-bit alpha channel:

   ```
   ###....###############...############...########....########.###
   ###.....##############...############....#.#####.....######..###
   ###.....#############....###########......######.....######...##
   ##.....#############.....######.####.......#####....######.#..##
   ###.....#############....###########.......###......#######...##
   ###.....####.########....###########......######.....######...##
   #.......############.....###########.......#####.....#####.....#
   ##.......###########.....#######..##.......####......#####.....#
   ##......#######.####......#..###..#........####......#####.....#
   #.......########.###......#..###..#........####......####......#
   ##.......###########........####...........#.#.......#####.....#
   ##.......###########.........###...........####......#####.....#
   ##.......##.###..###.........###...........####.......####.....#
   ##........#.###..##..........###............#.#.......###......#
   .#...........#...##..........##.............##........###.......
   .................##...........#.............##..........#.......
   .............................................#..................
   ```

   Vertical trunks tapering to points, transparent sky between them, thinning to isolated tips at the bottom edge. **These are the trees.** Pass 23's identification is confirmed by decode, not inference. 522 of the 1088 texels are literally `0x0000` (black, alpha 0).

2. **The authored LOD pyramid preserves the silhouette** — this is the load-bearing new fact:

   | LOD | dims | compmethod | alpha opaque |
   |---|---|---|---|
   | 0 | 64×17 | HUFFLOOKUP | 481/1088 = **44.2 %** |
   | 1 | 32×9 | RLELOOKUP | 124/288 = **43.1 %** |
   | 2 | 16×5 | LOOKUP | 31/80 = **38.8 %** |
   | 3 | 8×3 | RLE | (channel codec, not ported) |

   LOD 1 and LOD 2 still read as recognisable tree silhouettes, and the opaque fraction holds near 40 % instead of collapsing. Each level uses a *different* compression method, i.e. these are six separately authored images, not a generated chain. **That is exactly what you do to keep a 1-bit-alpha cutout crisp under minification, and exactly what naive box-filter mip generation destroys** — averaging a 0/1 alpha yields fractional alpha, and under `XLU_SURF` (no alpha test, no coverage-from-alpha — pass 22) fractional alpha does not cut out, it blends. A minified treeline then smears into a translucent band across the horizon: the reported "wall/sheet of tree texture".

3. **Correction to pass 23.** Pass 23 called `tex1198`–`tex1201` "the tree cards". The decode refines that: only **`tex1198`** is unambiguously a treeline strip (44.2 % alpha). **`tex1199`** is the same geometry and codec but 89.4 % opaque with thin vertical gaps — a denser foliage/branch mass or a treed cliff face. **`tex1200` and `tex1201` are 100 % opaque** dense detail textures (138–162 distinct colours, no alpha structure at all) that merely share the render-mode class. The "four textures in 21 of 42 rooms each" observation stands; "all four are trees" does not.

4. **Two decoder bugs found and fixed while porting — both would have produced confident, wrong pictures.**
   * **Per-image byte alignment.** `image.c:1096` ends each LOD image with `if (img_bitcount == 0) img_curpos++; else img_bitcount = 0;` — discard the partial byte, and skip a whole byte when already aligned. Omitting it desynchronised every LOD after the first. It surfaced as LOD 1 parsing as `fmt=0 1x32 cm=0`; a scan of neighbouring bit offsets showed a constant **+8-bit** drift, which pointed straight at the align.
   * **`texInflateRle`'s fudge was guessed, not ported.** The first draft invented `fudge = ((1<<bt) + (1<<rl) + 1 + blocksize) // (blocksize+1)`. The real derivation (`image.c:1605`) is a loop: `cost = bt + rl + blocksize + 1; while (cost > 0) { cost -= blocksize + 1; fudge++; }`. The guess still decoded *something* — it gave `tex1198` LOD 1 as 66.7 % opaque and `tex149` as 48.3 % — and only the corrected port gives the self-consistent 43.1 % and 34.4 %. **Every RLELOOKUP figure in this entry is from the corrected port; any earlier session notes quoting 48.3 % for `tex149` are wrong.** (Same lesson as passes 21 and 23, third time in four passes: in this codebase a half-right bitstream parse produces plausible output, never an obvious failure. Port the derivation, don't reconstruct it.)

5. **What this gives pass 25.** There is now an offline, N64-correct reference for the exact texture at the centre of D236: format, dimensions, per-LOD alpha coverage, and the silhouette itself, reproducible with `python tools_pc/texdecode.py 1198`. A live capture can be diffed against it field by field instead of being interpreted on its own.

**Next step (pass 25) — one run, three questions, all now answerable by direct comparison:**
   1. Does `tex1198` arrive as **64×17 RGBA5551**, or are the dimensions/format mangled? (D265 already found one transposed-dimensions bug in this area.)
   2. Is the imported alpha still **~44 % transparent**, or opaque everywhere? If opaque, the bug is in the 5551 alpha-bit decode and mechanism (a) is confirmed.
   3. Do **all six authored LOD images** reach the GL texture, or only LOD 0 with driver-generated mips? If the latter, mechanism (b) is confirmed and the fix is to upload the authored chain (`glTexImage2D` per level with `GL_TEXTURE_MAX_LEVEL`) rather than letting the driver filter a 1-bit alpha.
   Capture with `GE_TEXDUMP=1 GE_TEXRAW=1 GE_D270KEEPOML=1049d8` on `-level_36`, filtering **`fmt=0 siz=2`**. Note `texSwapAltRowBytes` (`image.c:1088`) runs on explicit-LOD textures and swaps bytes on alternate rows — expect that in the raw dump; `texdecode.py` decodes logically and does not apply it.

**Tooling added this pass (keep — read-only, no runtime cost):** `tools_pc/texdecode.py` (`<texnum>`, `--lod N`, `--ppm out.ppm`; PPM renders transparent texels magenta).

**Pass 25 (2026-09-21, live session) — all three pass-24 questions answered by direct comparison; the mip mechanism is now located to a line. Q1 YES, Q2 YES, Q3 NO — and the "wall" turns out not to be painted by `tex1198` at all from an idle spawn.**

**Setup:** live runs of `-level_36` (idle boot, 4800 frames) with `GE_TEXDUMP=1 GE_TEXRAW=1 GE_D219RAW=1`, plus one A/B run flipping the in-tree config key `Video.FixMipTextures` (default 1 → 0) to observe unclipped LOADBLOCK sizes. `texdecode.py` was first verified function-by-function against `image.c`/`image_bank.c` (all pass-24 numbers reproduced: 44.2/43.1/38.8 %, 89.4 %, 100 %, tex149 34.375 %). One latent decoder bug fixed on the way: a `max(1, …)` clamp on `(ncol-1).bit_length()` in the plain-LOOKUP path that C's `texGetBitSize(1)=0` does not have (unreachable for GE assets; no output change).

**Q1 — YES, dimensions/format intact.** D219RAW shows all six `tex1198` LODs pool-decoded at level load with the exact offline dims/compmethods (64×17 HUFFLOOKUP, 32×9 RLELOOKUP, 16×5 LOOKUP, 8×3/4×2/2×2 RLE), format=1 (RGBA16) throughout. The live C decoder's first row of LOD 0 is **pixel-identical** to the offline decode after the documented `texSwapAltRowBytes` byte-swap — and that swap is a no-op on PORT (D159 early-return), so raw dumps are plain linear pixels.

**Q2 — YES, alpha survives intact.** Pool bytes ≡ ROM-decoded bytes ⇒ 44.2 % opaque at the pool stage. `import_texture_rgba16` (`gfx_pc.cpp:791`) maps bit0→alpha 255/0; `import_texture_ia8` maps upper nibble=intensity, lower=alpha — both preserve alpha by construction. Data check on the one drawn member of the class: `GE_TEXI[101]` (IA8 32×64, `tex149`) is **byte-for-byte identical** to the offline ROM decode (2048/2048).

**Q3 — NO, structurally.** `gfx_opengl_upload_texture` (`gfx_opengl.cpp:713`) does `glTexImage2D(level 0)` + optional `glGenerateMipmap`; there is no code path that uploads an authored chain as GL mip levels. Each LOADBLOCK becomes one GL texture; the driver box-filters level 0.

**New finding N1 — `tex1198`–`tex1201` are never drawn from an idle spawn.** Across all 4800 frames there is not a single `fmt=0 siz=2` (RGBA16) import while CI8/IA8 background draws continue every frame. The tree-like cover visible at Bond's spawn point is therefore painted by the *other* classes — consistent with D276/D280 (the IA8 noise class paints the weave). Pass 24's "leading mechanism" applies to `tex1198`'s cards, which only draw when the player reaches their rooms; the spawn-visible wall is a different painter.

**New finding N2 — the drawn tree-class texture is `tex149`: IA8 32×64 hard cutout, 34.4 % opaque** (alpha nibble F for opaque pixels, 0 for holes — no ramp). Its ROM blob is **329 bytes = exactly one image**; there is no authored mip chain.

**New finding N3 — the mip mechanism, located precisely.** `texGetDepthAndSize` (`tex.c:330`) sizes the LOADBLOCK from `maxlod`, and `texInflateNonZlib` sets `maxlod = forcenumimages` = the header's `lods` field (**7** for `tex149`) **even when `explicit_lods=0` and only one image was decoded**. So GE's LOADBLOCK is deliberately larger than the real pool data: live A/B with `Video.FixMipTextures=0` shows the same 32×64 cards at **size=2808** (base 2048 + 760 B "mip region") vs 2048 clipped. On N64 those extra bytes are whatever adjacent stage-pool memory follows the allocation — deterministic, likely neighbouring level textures. The port's RC2 clip (`g_fix_mip_textures`, `gfx_pc.cpp:1089/1142`, default ON) truncates to base height and `glGenerateMipmap` + trilinear minification replaces that region with driver box-filtered mips — **observed live on the 32×64 upload: `gen_mip=1 filter=LINEAR mipmap_filter_mode=MIPMAP_LINEAR`; 380/400 sampled uploads generate mips.** For a hard 0/F alpha cutout, box filtering turns every mip level into fractional alpha, and the XLU blend then smears minified silhouettes — the reported wall/sheet. (For `tex1198`-class cards with real six-level chains, the same clip+regenerate path replaces the authored pyramid — pass 24's mechanism (b), now confirmed at the line level.)

**Honesty note.** The N64 side of N3 is established for the *load* (block size, pool contents) but not for the *sample*: what the RDP actually read out of that 760 B mip region for minified cards (adjacent-pool bytes vs effectively level-0) cannot be resolved without hardware or an RDP sampler model. Both candidate port fixes below are therefore approximations to be judged by playtest A/B, not by derivation.

**Fix directions for pass 26 (port-only; no game logic):**
1. **No-chain textures (`explicit_lods=0`, single-image blob — `tex149` class):** do not `glGenerateMipmap`; sample level 0 only (`GL_LINEAR`, non-mip min filter). Matches "RDP with no real mips" and kills the fractional-alpha smear at zero cost. This is the cheapest test of the whole hypothesis: if the spawn wall sharpens into discrete trees, mechanism confirmed end-to-end.
2. **Chained textures (`tex1198` class):** upload the authored levels as real GL mip levels (needs per-LOD dims from the pool header; GE's authored dims are *not* exact halvings — 17→9→5→3→2 vs GL's required 17→8→4→2 — so levels must be padded/cropped to legal sizes, or uploaded into a custom layout). Bigger job; only after (1) is judged.

**Probe added this pass (TEMPORARY — remove once the mip question is closed):** `GE_D236P25` env-gated block in `gfx_opengl_upload_texture` logging width/height/gen_mipmaps/filter per upload (capped 400 lines). **Process note:** Git-Bash `kill` does not terminate the game here — use `taskkill //F //IM ge007.x86_64.exe` (two idle runs were left alive this session).

**Pass 26 (2026-09-21, live session) — D236 ROOT-CAUSED AND FIXED. The tree cards were never a decode, alpha or mip problem: the port was binding the wrong texture. Surface 1 now renders discrete snow-covered pines; verified live.**

**The pass-25 fix direction was tested first and REFUTED.** Implemented cut-out
mip suppression (skip `glGenerateMipmap` + `GL_TEXTURE_MAX_LEVEL 0` for
textures whose alpha is hard 0/255 with real holes), config-gated, and A/B'd it
live on `-level_36` at the idle spawn. **The wall did not change.** Two things
came out of that run and both mattered:

- The per-upload alpha census showed the spawn view is dominated by **248
  uploads of a fully-opaque 32×32 texture** (`clear=0 solid=1024 mid=0`) — the
  "wall" is opaque, so no alpha/blend mechanism could have produced it. That
  retires the whole mip / 1-bit-alpha / XLU-smear branch (passes 22–25).
- Correction to pass 25's **N3**: the 760 B "mip region" beyond a no-chain
  texture's base image is **not adjacent pool garbage**. `texInflateNonZlib`
  (`image.c:1123-1153`) and `texInflateZlib` (`image.c:324-360`) *generate* the
  chain themselves via `texShrinkNonPaletted`/`texShrinkPaletted` when
  `explicit_lods == 0 && forcenumimages >= 2`. Those shrinkers also re-quantise
  alpha to the source's bit depth — for RGBA5551, `a = (a0+a1+a2+a3 + 2) >> 2`
  keeps every level a hard 0/1 cut-out, unlike a GL box filter. Worth knowing
  for any future mip work; irrelevant to this bug.

**Root cause (`gfx_lod_tile_offset`, `port/fast3d/gfx_pc.cpp`).** GE's
`texLoadFromGdl` picks a handler per binding from `in->words.w0 & 7`:

- `TEXTURETYPE_LOD`(0) / `TEXTURETYPE_MIPMAP`(2) — `texHandleType0`/`Type2`:
  ONE load block at TMEM 0, tiles 0..n are LOD levels of that one image.
- `TEXTURETYPE_DETAIL`(1) — `texHandleType1` (`tex.c`): **TWO** load blocks —
  the *detail* texture at TMEM 0 (`texWriteLoadToTmemZero(tex2)`) and the
  **base image** at TMEM `texGetSizeInBytes(tex2,0)` on tiles 1+
  (`texWriteLoadToTmemAddr(tex1,size)` + `texWriteTileLods(tex1,…,1,size)`).

D107 folded every `G_TL_LOD` draw to tile 0 on the premise "GE loads the whole
mip chain at TMEM 0". True for types 0/2, **false for type 1** — there tile 0 is
the detail texture. So the port drew the detail texture instead of the base
image, and never imported the base image at all.

**Confirmed against the data, offline and live:**

1. `GE_D85TEX` on `-level_36`: `tex1198`–`tex1201` each resolve 28× with a
   non-NULL pool pointer and **`type=1`** — they were being bound all along.
   Pass 25's N1 ("zero `fmt=0 siz=2` imports in 4800 frames") was the symptom,
   not evidence the cards weren't referenced.
2. Offline scan of every `G_NOOP` in `bg_sevx_all_p`: the only `type=1`
   bindings in Surface 1 are exactly the four tree cards, **21 rooms each, all
   sharing `texnum2 = 2465`**. Everything else in the level is `type=2` with
   `detail=0`.
3. `tex2465` = **IA8 32×32 HUFFLOOKUP, `lods=0`, fully opaque** — precisely the
   248-upload opaque 32×32 class above, and the same texture D267/D276 measured
   at "1023/1024 texels at A=F" and called the noise painter. D276's painter was
   never a mystery texture; it is the tree cards' detail tile.

**Fix (port-layer only, `gfx_lod_tile_offset`).** When `rdp.tex_lod` is set and
tile `fi+1` sits at a different TMEM address *and* declares a different
`fmt`/`siz` than tile `fi`, return tile offset 1 (the base image) instead of 0.
Deliberately asymmetric: LOD levels of one image always share a format, so a
mip chain can never trip it, and a detail pair that happened to share a format
would simply keep today's behaviour. Config `Video.DetailBaseTile`, default on.

**Two weaker discriminators were tried live and both failed — recorded so they
are not retried:** (a) "tile fi+1 is at a different TMEM address" alone — mip
levels are at different TMEM offsets too, so every ordinary surface started
sampling its first mip (trees correct, all ground/snow textures wrecked; the
user caught this in the same build); (b) additionally requiring
`rdp.loaded_texture[tmem].addr` to be set — same failure, because
`rdp.loaded_texture[]` is persistent state never cleared between binds, so a
stale entry from an earlier type-1 draw satisfies it.

**Verification.**
- Live `-level_36` idle spawn, frame 560: opaque tiled noise wall →
  **discrete snow-covered pines lining the valley**, ground/snow unchanged
  against the pre-fix capture. Surface 2 (`-level_43`) likewise shows a discrete
  night treeline; Depot (`-level_30`, the only other level with `type=1`
  bindings) renders clean.
- `bunker1` golden framediff is **bit-identical with the fix on and off**
  (`worst_cell=189.741875` both ways — that delta is pre-existing, from the
  other uncommitted work in the tree, not from this change).
- Offline blast-radius scan of every level's bg: `type=1` bindings exist **only**
  in `sevx` (84), `depo` (39), `sevb` (4), `sev` (4). Every other level has zero,
  so the change is provably inert on them.

**Generalised to `docs/porting-notes.md` §D12.**

**Housekeeping:** with D236 closed, every D236 diagnostic is out of the tree 
-- the pass-25 `GE_D236P25` probe and the pass-26 cut-out experiment, plus 
`GE_D236RM`/`ORDER`/`ALPHA`/`ZFIX` (`gfx_pc.cpp` `gfx_sp_tri1`), `GE_D236RAW` 
(`bg.c`), `GE_D236BT` (`chr.c` + `model.c`), the `port_d236bt_dump()` host 
backtrace dumper and the `windows.h` include it needed, and the 
`g_d236_last_vtx_seg`/`_raw` G_VTX trackers. Their rows are gone from 
`GE-ENV-PROBES.md`; `gen_env_probes.py --check` is in sync. Build clean and 
the Surface 1 spawn capture is unchanged across the strip.
