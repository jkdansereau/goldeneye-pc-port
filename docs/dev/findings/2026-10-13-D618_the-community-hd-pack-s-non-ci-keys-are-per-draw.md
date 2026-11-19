# D618

<!-- from the old section-F/H table row -->
| D618 | **The community HD pack's non-CI keys are per-draw rects partly unreachable from N64 tile state (`440x1` strips), and a rect key cannot land in the whole-texture pack format — the rice-pack route stops here** — ROADMAP #100 rice-pack stage; full `## D618` entry at file tail | STOPPED 2026-10-06 — tdb `;WxH` are per-draw rects (440x1 ×301 over tmem's 128-byte pitch = recompile-side); ~100 byte-domain variants score 0 hits; content-join does not discriminate; rect-keyed replacement would be a format extension. HD/custom texture support ships on the texnum path |

## D618: The community HD pack's non-CI keys are per-draw rects that are partly unreachable from N64 tile state (440x1 strips), and a rect key cannot land in the texnum-keyed pack format — the rice-pack route stops here (ROADMAP #100, rice-pack stage)

**Status: STOPPED (design decision, 2026-10-06) — the display-list walk in
`docs/dev/TEXPACK-RICE-DRAW-PLAN.md` is not worth running.** Measured, not
assumed; all numbers below are from `scratch/rice_domain_probe*.py`,
`scratch/content_join_probe.py`, `scratch/ci_faithfulness_probe.py` (local
analysis, not committed).

**Setup.** Re-tested D617's diagnosis on the GoldenEye-007-HD pack
(`ge007.tdb`, 35517 lines) before spending the plan's work.

**Findings.**

1. **The tdb's `;WxH` field is a per-draw rect, and some rects cannot be
   N64 tile state at all.** Non-CI key dims: `64x64` ×623, `16x16` ×429,
   `440x1` ×301, `64x32` ×187, `95x32` ×148, `65x65` ×84, `54x54` ×81,
   `32x32` ×79, `128x47` ×56, `8x8` ×49, `32x64` ×48, `96x80` ×40. None of
   440/95/128x47/96x80 exist in `imagelist.u.csv`. A `440x1` I8 rect is
   440 bytes/row, over tmem's 128-byte pitch — so those 301 keys are
   **recompile-side (RT64) textures, not loadable tile state**; no display-
   list walk over the vanilla ROM can produce them. ~1/8 of the non-CI key
   set is unreachable by construction, and the rest is per-draw tiling.
2. **The byte domain is not recoverable by search.** ~100 domain variants
   (pool / pool+N64 alt-row swap / decoded RGBA32 / decoded RGBA16, BE and
   LE word reads, aligned / packed / `w*4` strides, RGBA/ABGR/ARGB/BGRA
   orders, alpha = source/opaque/zero) over the 604 dim-matching candidates:
   **0 hits**. The 9 whole-texture hits from D617 are coincidences (texnums
   2085–2100, 2 distinct CRCs).
3. **Content-join is not a workaround.** Downsampling pack PNGs to vanilla
   dims and scoring mean-abs-diff over RGBA gives best scores 8–20 with
   margins <3 against 200+ same-(fmt,siz) candidates — the pack's HD art is
   a rework, not a faithful upscale, so it does not discriminate. (The
   near-zero diffs seen earlier were a channel-count artifact of a raw-
   length probe, not a match.)
4. **The 256 entries we do emit are semantically sound.** `rt64.json`:
   `defaultOperation: "stream"`, `operationFilters: []`, two `shiftFilters`
   (`UI\Checkmark\*`, `UI\Crosshair\*` → `half`) — entries are straight
   replacements, not masks. Of the 256 emitted: 199 color+opaque, 35
   color+alpha, 22 grayscale (legitimate I/IA sources). Pack PNG dims are
   exact integer multiples of the tdb rect (64x/32x/16x/8x).

**The architectural blocker (why the walk would not finish the feature).**
Even with correct per-draw keys, a rect key names a **sub-rectangle of a
texture, drawn tiled per draw**. Our pack format (`data/texpacks/<name>/<texnum>.png`,
D615) replaces a **whole texture**. Dropping a `95x32` tile's HD art at the
parent texnum changes every draw that uses that texture, not just the one
tile. Consuming this pack needs a format extension (rect-keyed entries:
texnum + `ul_s,ul_t,w,h`, per-draw selection) — a second feature, not a
missing step.

**Decision / what ships instead.** HD + custom texture support is the
verified texnum path: dumper → authoring (upscale or user art) →
`data/texpacks/<name>/NNNN.png` → D615 A/B. Community HTS packs stay
unsupported for non-CI content; the CI half (251) already imports. Optional
later nicety, not content: the PD dabs-mod runtime rice index
(`texpackBuildRiceIndex`/`texpackRiceLookup`) so whole-texture rice packs
load with no offline step — it scores the same 251+9 on this pack, so it is
architecture, not coverage.
