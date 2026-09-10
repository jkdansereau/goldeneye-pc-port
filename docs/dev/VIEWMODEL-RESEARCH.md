# 1P weapon viewmodel — static research (read-only)

> **RESOLVED 2026-09-09 (M-83, D215).** None of the three ranked suspects below
> was the cause — all were ruled out by the `GE_DVM=1` probe (added this
> session). The bug was `renderdata.flags == 0` in `gunRenderFirstPersonGunModels`:
> the N64 `*(ModelRenderData*)&D_80035CC0` reinterpret-across-adjacent-globals
> breaks under x86-64 pointer widening + no guaranteed global layout. Fixed
> `#ifdef PORT` in `src/game/gunfire.c`. See findings.md D215. This doc is kept
> for the ruled-out analysis only.

Companion to `GRAPHICS-BACKLOG.md` ("1P weapon viewmodel") and findings D115
item #5 / AUDIT-M6 #5. Goal: narrow the five audit suspects to a ranked,
probe-ready list. No code changed by this doc.

## Path map (verified in source)

- **Setup (per frame):** `bondview2.c:9040` → `gunUpdateAndFireBothHands()`
  (`gunfire.c:1018`) → `gunUpdateAndFire(handnum)` (`gunfire.c:327`). Inside,
  the weapon-model block at `gunfire.c:599-662`: computes `field_87F` (the
  show/hide gate), then if set: `dynAllocate(numMatrices × sizeof Mtxf)` →
  identity-fill → `modelCalculateRwDataLen` → `modelInit(HAND_WEAPON_MODEL,
  &copy_of_body_obj_header[handnum], HAND_WEAPON_RWPOOL)` →
  `hand->mtxlist = rwmtx` and (PORT/D102)
  `weaponModel.render_pos = (RenderPosView*)rwmtx`.
- **Render (same frame, later):** `gunRenderFirstPersonGunModels` loop at
  `gunfire.c:1532-1540`: **`if (handptr->field_87F == 0) continue;`** then
  `subdraw(&renderdata, HAND_WEAPON_MODEL(handptr))` (`model.c:5326`) which
  does `gSPSegment(gdl, 3, osVirtualToPhysical(mdl->render_pos))` and walks
  `mdl->obj->RootNode` — **a NULL RootNode returns silently, drawing
  nothing**.

## Statically ruled out (with evidence)

1. **Transient-arena lifetime** (the audit's prime suspect). `dynAllocate`
   is a bump allocator on the vtx pool (`src/game/dyn.c:123`), reset only by
   `dynSwapBuffers()` at frame end. Setup and render both happen inside one
   frame, so `render_pos`/`mtxlist` are valid when consumed — in solo play.
   (Split-screen mid-frame swaps would be a separate question; out of scope.)
2. **Word-index / record-size mismatch** (`flashvisptr = RWPOOL +
   RwDataIndex`, `gunfire.c:650`). D52 kept `Model.datas` as `u32*` on PC
   precisely so N64 word math holds, and `RwDataIndex` values are computed
   at runtime by `modelCalculateRwDataIndexes` from the *PC* record sizes —
   self-consistent. (Self-consistent ≠ in-bounds; see suspect 2.)
3. **Segment/render_pos mechanism itself.** Every working model in the game
   (bodies, props) uses the same `gSPSegment(3, render_pos)` +
   `bondviewTransformManyPosToViewMatrix` machinery — and it renders. The
   **watch menu draws the same weapon models** via a different path
   (`set_enviro_fog_for_items_in_solo_watch_menu`, `gunfire.c:1630+`, local
   matrices, no 87F gate, no hand rw-pool) and they appear (albeit faint —
   D75/D141 note). So the model assets and headers are very likely fine; the
   defect is in-level-path-specific.

## Ranked suspects + probe spec

**#1 — `field_87F` gating (`gunfire.c:599-610`).** The clear-condition
disjunction contains `(Gun_hand_without_item(handnum) == 0)`. That function
(`gun.c:812-816`) returns `hand_invisible[h] > 0 || (hand_item[h] == 0 &&
field_2A44[h] < 0)` — so the term is TRUE (→ gate cleared → gun hidden)
whenever the hand *has* an item and isn't invisible. Read naively, that
hides the gun exactly when you're holding one; since the N64 shows the gun,
the runtime values of `hand_item`/`field_2A44`/`hand_invisible` must differ
from their names suggest — but this is the single most likely place for a
PC-only divergence (e.g. a field the port initializes differently). If 87F
is 0, the render loop `continue`s and the symptom is exactly "no gun, no
crash".

**#2 — rw-pool capacity.** PC records are wider than N64's (pointer fields);
`weaponRwPool[192]` (768 B) was D102's scaled-up budget, but nothing bounds-
checks it. The `#ifdef DEBUG` print at `gunfire.c:620` still tests the *N64*
32-word threshold — stale on PC. If `numRecords` words exceed 192,
`modelInitRwData` overruns the pool into whatever follows it in `struct
hand` → corruption that could kill the draw (or worse, silently).

**#3 — stale `copy_of_body_obj_header`.** Populated only at item load
(`gun.c:924` shallow copy / `gun.c:931-951` `load_object_fill_header`). If a
PC asset-load quirk zeroed it for the in-level path, `RootNode == NULL` and
`subdraw` no-ops silently. Lower priority given the watch menu renders the
same models — but cheap to confirm in the same probe.

**Probe (one session, PORT-only, env-gated `GE_DVM=1`, zero behavior
change):** in the `field_87F != 0` block at `gunfire.c:611+`, print once per
frame while a pistol is held in BUNKER1: `handnum`, `item`, each clear-term
value + final `field_87F`, `&copy_of_body_obj_header[h]`, `numMatrices`,
`numRecords` (words) vs 192, `RootNode`, `rwmtx`. One run triages all three
suspects: 87F==0 → suspect #1 (log which term); numRecords>192 → #2;
RootNode NULL/garbage → #3.

## Fix-shape notes (for when the probe lands)

- If #1: the fix is a `#ifdef PORT` initialization/correction of whichever
  hand field diverges — **not** an edit to the gate logic itself (rule #2);
  document as Dxxx with the N64-vs-PC value table.
- If #2: enlarge `weaponRwPool` to the measured max + a PORT bounds guard
  that prints (mirrors the existing DEBUG print, correct threshold).
- If #3: trace the `load_object_fill_header` call for the in-level weapon
  load; likely an asset-offset issue in the port's loader path.
