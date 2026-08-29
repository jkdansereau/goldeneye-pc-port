# AUDIT-M6 — `struct player` / `struct hand` raw-offset pass

Survey only (Track 3 of `docs/PLAN-M6-playable.md`). No source changed, no
build. Feeds a follow-up fix session. Companion to §F D98/D100/D101/D102.

## Method

Grep of `src/` for: `handoffset`, `(u8 *)g_CurrentPlayer`, `g_CurrentPlayer + 0x`,
`+ 0x[3-hex]`, `sizeof(struct hand|player)`, `* sizeof(struct`, hardcoded
`0x2A80`/`0x2A70`, `->hands[i] +`, punned `->m[` / inline-Model accesses,
`bondheadmatrices`, `copy_of_body_obj_header`, `field_59C..field_6CC`,
`field_654`, `gaitRwData`. Cross-checked every hit against the live
`struct player` / `struct hand` definitions in `src/game/bondview.h`
(player @ line 314, hand @ line 119).

## PC layout facts (from bondview.h + bondtypes.h)

- `struct hand` (bondview.h:119): N64 `sizeof` ≈ **0x3B8** (base 0x870→0xC28
  inside `player`). PC adds: `audioHandle` +4, `rocket` (`AttachedObj*`) +4,
  `mtxlist` (`Mtxf*`) +4, and the D102 `#ifdef PORT` tail
  `struct Model weaponModel` (~0x2A8) + `u32 weaponRwPool[192]` (0x300).
  **PC `sizeof(struct hand)` ≈ 0x968** — roughly **+0x5B0** vs N64, and *not*
  a clean multiple, so `handnum * sizeof(struct hand)` de-syncs for `handnum==1`.
- `struct player` pointer growth **below N64 offset 0x594** (contradicts the
  D100 note that “anything below 0x594 is unaffected”): `cameratile` (0x34),
  `prop` (0xA8), `bodyModel` (0xD4), `autoaim_target_y` (0x130),
  `autoaim_target_x` (0x140) → **+0x14** by offset ~0x144. `field_10C4`..
  and the `bloodImg*` / `InvItem*` / `textoverride*` blocks add much more
  higher up.
- `model` (N64 0x598): D100 made it an **inline `struct Model` (~0x2A8)**
  replacing an 8-byte pointer → **+0x2A0** for everything after it, before
  `field_59C..field_6CC` (which D100 grep-verified dead except `field_654`).
- `ptr_hand_weapon_buffer[2]` +0x8; `copy_of_body_obj_header[2]` — inline
  `ModelFileHeader` grew 0x20→0x30 (4 pointers ×+4) → **+0x20**;
  `item_related[2]` `struct texpool` pointer growth; `bondheadmatrices` /
  `viewports` unchanged.
- Net delta from `player` base to `hands[]`: **≈ +0x2E0** (0x14 + 0x2A0 +
  0x08 + 0x20 + texpool). So N64 `hands[0]` @ 0x870 is at PC **≈ 0xB50**;
  N64 `hands[0].throw_item_pos_related` @ 0x870+0x268 = **0xAD8** is at PC
  **≈ 0xDB8**.

## Findings table

| # | file:line | accessor | targets (N64 field) | N64 off | PC off (real) | PORT-adj? | BUNKER1 FP reachable? | severity | recommended fix |
|---|-----------|----------|---------------------|---------|---------------|-----------|----------------------|----------|-----------------|
| 1 | `src/game/gunfire.c:4960` `THROWMTX` (macro) used @ 5043,5047,5070,5126,5360,5364,5387,5443 | `(Mtxf*)((u8*)g_CurrentPlayer + handoffset + 0xAD8)` (`0xAD0` EU) | `hands[handnum].throw_item_pos_related` | 0xAD8 (h0) / 0xAD8+0x3B8 (h1) | ≈0xDB8 (h0) / ≈0xDB8+0x968 (h1) | **NO** | **YES** — `sub_GAME_7F068508` runs on every shot with a cartridge (PP7/KF7/…), solo only (`getPlayerCount()>=2` early-out) | **HIGH — memory corruption.** Raw 0xAD8 on PC lands inside the inline `model` / `field_59C` region of `struct player`; `matrix_4x4_copy(THROWMTX,…)` writes 64 bytes there. `handnum==1` (dual wield) additionally uses the wrong stride. Corrupts the gait `Model` / bondhead matrices → wrong Bond-body render or a delayed fault in model code. | Replace macro body with `(&g_CurrentPlayer->hands[handnum].throw_item_pos_related)`. Drop `THROWMTX_OFFSET`, `handoffset`. |
| 2 | `src/game/gunfire.c:4961` `THROWPOS(k)` used @ 5112-5114, 5164-5166, 5429-5431, 5481-5483 | `((f32*)((u8*)g_CurrentPlayer + handoffset + 0xB08))[k]` (`0xB00` EU) | `hands[handnum].throw_item_pos_related.m[3][k]` (translation row) | 0xB08 | ≈0xDE8 | **NO** | **YES** (same path as #1) | **HIGH — silent-wrong + reads corrupted memory.** Feeds `casing->vel` from the frame-to-frame delta of a garbage location → casings fly wrong / NaN, and the read is OOB-ish relative to intent. | `g_CurrentPlayer->hands[handnum].throw_item_pos_related.m[3][k]` |
| 3 | `src/game/gunfire.c:4962` `THROWPREV(k)` used @ 5112-5114, 5164-5166, 5429-5431, 5481-5483 | `((f32*)((u8*)g_CurrentPlayer + handoffset + 0xB48))[k]` (`0xB40` EU) | `hands[handnum].throw_item_pos_related_prev.m[3][k]` | 0xB48 | ≈0xE28 | **NO** | **YES** (same path) | **HIGH** (same as #2) | `g_CurrentPlayer->hands[handnum].throw_item_pos_related_prev.m[3][k]` |
| 4 | `src/game/gunfire.c:5031` and `:5348` | `handoffset = handnum * sizeof(struct hand)` | array stride for `hands[handnum]` | 0x3B8 | 0x968 (and non-uniform) | **NO** (the raw math is only consumed by #1–#3) | YES | **HIGH** — root of #1–#3’s `handnum==1` error; harmless once #1–#3 use `hands[handnum].field`. | delete; fold into the field accessors above. |
| 5 | `src/game/gunfire.c:600-661` weapon-model setup (`copy_of_body_obj_header[handnum]` → `modelInit(HAND_WEAPON_MODEL, mdlhdr, HAND_WEAPON_RWPOOL)`; `hand->mtxlist = rwmtx`; `render_pos = (RenderPosView*)rwmtx`) | field-name access + D102 macros | `hands[h].weaponModel` / `weaponRwPool` / `mtxlist`; `copy_of_body_obj_header[h]` (inline `ModelFileHeader`) | — | — | **YES (D102)** for the Model/pool pun; `copy_of_body_obj_header` is a plain inline struct (layout-consistent) | YES — this *is* the 1P weapon draw path | **MED — prime suspect for “weapon model doesn’t draw.”** `rwmtx` is `dynAllocate`’d (transient per-frame arena) but `weaponModel.render_pos` is set to it once at setup and consumed at render (`gunRenderFirstPersonGunModels:1605`); on N64 `render_pos` aliased the *persistent* `mtxlist` field, here it can point at reused arena memory. Also `flashvisptr = HAND_WEAPON_RWPOOL + node->…RwDataIndex` (gunfire.c:640) assumes N64 word indices into a pool whose PC records are ~2×. | Verify `dynAllocate` lifetime vs. render; consider persisting the matrix list. Re-check `weaponRwPool[192]` sizing against `modelCalculateRwDataLen(mdlhdr)` on PC. Confirm `subdraw` matrix path (shared with Track 2). |
| 6 | `src/game/bondview2.c:3155-3162` (PORT branch of `sub_GAME_7F07E7CC`) | `(Model*)&g_CurrentPlayer->something_with_watch_object_instance`; `static u8 watchRwPool[0xC8]`; `g_CurrentPlayer->step_in_view_watch_animation = 0` | watch-menu item preview Model punned at N64 player+0x230, pool at +0x2EC | 0x230 / 0x2EC | 0x244 / 0x300-ish | **YES (D56)** but suspect | YES — open watch menu / cycle to an item page | **MED — under-reservation.** D56 hosts the pool in a `0xC8` static (N64 capacity) but PC RW records are wider; and `animInit` writes a full inline `struct Model` (~0x2A8) starting at `&something_with_watch_object_instance`, overrunning `field_234..watch_scale_destination..` (N64 those were the tail of the 0xBC Model hole; on PC they are live watch fields). | Give the watch Model real inline storage in `struct player` under `#ifdef PORT` (D100 pattern) + size `watchRwPool` from `modelCalculateRwDataLen`. |
| 7 | `src/game/gunfire.c:4951-4957` | `#define THROWMTX_OFFSET 0xAD8` etc. | (the constants behind #1–#3) | — | — | NO (EU/non-EU split only, both N64) | — | INFO — remove with #1–#3. | delete block. |
| 8 | `src/game/player.c:136` | `mempAllocBytesInBank((sizeof(struct player)+0xF)&~0xF, …)` | player allocation | — | — | **YES (D98)** | — | OK — listed for completeness; correct. | none |
| 9 | `src/game/initBondDATAdefaults.c:103` | `animInit(&g_CurrentPlayer->model, &player_gait_object_header, g_CurrentPlayer->gaitRwData)` | inline gait Model + `gaitRwData[256]` | — | — | **YES (D100)** | YES | OK. | none |
| 10 | `src/game/bondhead.c:217-480`, `bondview2.c:922-9102` | `&g_CurrentPlayer->model`, `bondheadmatrices[0].m[..]` (field-name) | inline gait Model + head matrices | — | — | **YES** (D100 makes field access correct; single TU layout) | YES | OK — no raw offsets; safe as long as D100 inline-Model stays. | none |

### Not player/hand but same class (noted, out of scope for the fix list)

- `src/game/gunfire.c:5513` `render_data.mtxlist = (Mtxf*)model_matrices` — local, fine.
- `initexplosioncasing.c`, `initobjects.c`, `propobj.c`, `glass2.c`,
  `initunk_005520.c` — all `N * sizeof(struct X)` allocations, already
  `sizeof`-correct (D57 covered the rwdata capacities).
- `ramromreplay.c` `+0x110/+0x1F8/+0x21E` — `ramromfilestructure`, handled by D66/D87, unrelated.

## Total

**10 distinct offset sites; 3 live HIGH-severity bugs (findings #1–#3, one
underlying cause #4), 1 dead constant block (#7), 2 MED items to verify
(#5, #6), 4 already-correct (#8–#10).**

All 3 HIGH bugs are the same construct in `gunfire.c` and are reachable in
BUNKER1 first-person the instant the player fires a weapon that ejects a
casing (every ballistic weapon). They are NOT reachable before the first
shot, so they do not explain a weapon model that is missing from frame 1 —
but once firing starts they corrupt the inline gait `Model` region and will
produce cascading render/anim breakage or a fault.

## Where the “1P weapon model doesn’t draw” bug most likely lives

Not in a surviving raw offset — D102 already gave the weapon `Model` and its
RW pool dedicated storage, and `gunRenderFirstPersonGunModels` /
`gunTickHandState` use field names. Best candidates, in order:

1. **Finding #5 — the D102 wiring in `gunfire.c:600-661`.** `weaponModel.render_pos`
   is pointed at a `dynAllocate`’d transient (`rwmtx`) that on N64 aliased the
   *persistent* `hand->mtxlist` field; if the arena is recycled between the
   hand-tick setup and `gunRenderFirstPersonGunModels`, the model draws with
   garbage / off-screen matrices (looks like “not drawn”). Check `dynAllocate`
   lifetime and whether `weaponModel` should own its matrix list on PC.
2. **The `subdraw` / `subcalcmatrices` / `process_02_position` matrix
   composition** — shared with Track 2 (inverted characters). A handedness /
   lookat-sign error there hides the weapon model just as it inverts guards.
3. **`field_87F` gating** (`gunfire.c:520`, `:587-598`) — if the setup branch
   that sets `field_87F = 1` and calls `modelInit` isn’t taken (e.g.
   `get_ptr_weapon_model_header_line(item)` returns NULL because
   `ptr_hand_weapon_buffer` / weapon-model load failed), the render loop
   `continue`s and nothing is emitted. Cheap to instrument.

## Recommended fix order

1. **#1–#4 & #7 (gunfire.c THROW macros)** — one mechanical change, no layout
   work: rewrite `THROWMTX`/`THROWPOS`/`THROWPREV` as
   `g_CurrentPlayer->hands[handnum].throw_item_pos_related[_prev]` field
   accesses (`.m[3][k]` for the POS/PREV variants), delete `handoffset` and
   the `THROW*_OFFSET` defines. Wrap in `#ifdef PORT` keeping the N64 macro
   for the N64 build (or, since the field access is identical semantics on
   both, use it unconditionally — verify byte-match). Log as §F “D115”.
2. **#5** — investigate the weapon-model `render_pos` / `mtxlist` lifetime and
   `weaponRwPool` sizing; this is the likely playability blocker. May fold
   into the Track-2 matrix work.
3. **#6** — give the watch preview Model inline PORT storage + sized pool
   (only matters once the player opens the watch in-level).
4. Add a debug assert in `gunfire.c` weapon-model setup logging
   `mdlhdr->numRecords` vs `weaponRwPool` capacity and `field_87F` state, to
   settle candidate 3 above.

## §F pointer

Add to `docs/PCPortResearch.md` §F after D112: *“**D115 (audit)** —
`struct player`/`struct hand` raw-offset pass: see
`docs/AUDIT-M6-player-offsets.md`. Live HIGH bugs = `gunfire.c` THROWMTX/POS/PREV
macros (`+handoffset+0xAD8/0xB08/0xB48`), not PORT-adjusted; fix = field
accessors. MED = D102 weapon `render_pos` transient-arena aliasing (likely
the ‘weapon model doesn’t draw’ cause) and the D56 watch-Model pool
under-reservation.”*
