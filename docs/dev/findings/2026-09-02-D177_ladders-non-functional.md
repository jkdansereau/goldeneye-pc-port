# D177

<!-- from the old section-F/H table row -->
| D177 | **Ladders non-functional — progression blocker (user QA) — FIXED (M-36): `count`/`rooms` land in the high half of a widened pointer.** — full `## D177` entry at file tail | **FIXED (M-36)** — high confidence; interactive climb test owed Owed climb test considered cleared by the 2026-09-28 v0.4.0 campaign sign-off (ROADMAP §3 bookkeeping, 2026-10-01); reopen if seen. |

## D177 — Ladders non-functional: `count`/`rooms` land in the high half of a widened pointer (M-36)

**FIXED (M-36).** Climbing was completely dead — `MoveBond`
(`src/game/bondview2.c`) probes for a ladder tile every frame via
`stanTileDistanceRelated(&curLocus)` and gates the whole ladder-collision
path on `stanGetLocusCount(&curLocus)`, which was always 0 on PC.

Two compounding pointer-width ABI bugs (same class as D79/D90), all in the
stan navigation layer — **no game logic touched**:

1. **The ladder signal was written into the wrong half of a pointer.**
   `stanCheckLinkedSpecialTile` receives the caller's
   `struct StandTileLocusCallbackRecord` typed as `s32 *outFlags` and does
   raw `outFlags[0] = 1` (FORCECROUCH) / `outFlags[1] = 1` (LADDER). The
   record's first member is `s32 *rooms` — 4 bytes on N64, **8 on PC** — so
   on x86-64 `outFlags[1]` is the *upper* 4 bytes of `rooms`, and `count`
   (which moved from +4 to +8) never gets written. `stanGetLocusCount()`
   reads `record->count` → still 0 → ladder path never runs. FORCECROUCH
   survived by luck (LE low half of `rooms` is at +0, and
   `stanGetLocusField0` truncates it back). Fix: `#ifdef PORT` cast to the
   real struct and write `->rooms` / `->count` by name.
2. **`curLocus` was too small to hold the record.** It was declared
   `struct move_bond_temp_struct` — a 2-word "placeholder while matching"
   (`bondview.h`). 8 bytes on N64 = exactly the record; on PC the record is
   larger, so `stanTileDistanceRelated`'s field writes (the D90 zero-fill)
   and the fixed `count` store overflowed the local. Fix: `#ifdef PORT`
   declare it as the real `struct StandTileLocusCallbackRecord`.
3. **`stanGetMoveBondCollisionTiles` would have AV'd on the first climb.**
   Once the ladder path actually runs it calls
   `stanGetTileOrderedPointWorldPos(…, (coord3d *)((s32)coords + off))` —
   `(s32)coords` truncates the 64-bit `&bondCollision` stack pointer → the
   four quad corners written through a garbage address. Fix: `PORT_PTRADD`
   macro (`uintptr_t` arithmetic), added at the top of `stan.c`.

Touched: `src/game/stan.c` (macro + `stanCheckLinkedSpecialTile` +
`stanGetMoveBondCollisionTiles`), `src/game/bondview2.c` (`curLocus` type).
All `#ifdef PORT`, N64 path kept verbatim under `#else`. Candidate root-cause
came from an M-36 subagent; parts 1/3 were its work, part 2 (the local size)
found on review. **Verified:** build links clean; `-level_09` framediff 3/3;
`-level_36` (Surface, has mandatory ladders) boots crash-free.
**Interactive climb test — DONE (M-37): user confirmed climbing works
in-game.** Merged to `main` via PR #5 (`955349db`).

porting-notes.md §C (pointer-width in ROM/record structs).

---
