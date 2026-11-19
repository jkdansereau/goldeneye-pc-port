# D545

<!-- from the old section-F/H table row -->
| D545 | **Player rockets can pass through the ground when fired near Bond's feet** — `gunInitProjectileFromPlayer` passes a single `s32` as a 20-entry room out-array (N64 overflow absorbed by `pad_a`; D490 / porting-notes §D24 class); full `## D545` entry at file tail | FIXED 2026-10-06 (AVOID_UB array, gun.c; maintainer Archives preset A/B: 1 drop in ~60 at wide view, residual = room-pool hypothesis, hardening backlogged) |

## D545: player rockets can pass through the ground when fired near Bond's feet — FIXED 2026-10-05 (maintainer report; 3e triage)

**Report:** with the (dual) rocket launcher on Aztec and most maps, aiming down
near Bond's feet while running sometimes sends the rocket through the ground
or a solid object.

**Cause (static, high confidence it is a real defect):**
`gunInitProjectileFromPlayer` (`src/game/gun.c`) declares `s32 sp54;` and
passes `&sp54` to `bgFindRoomsAlongSegment(..., &sp54, &sp50, 0x14)`, which
writes up to 0x14 traversed room numbers there. On the N64 the 19 extra slots
landed in the decomp's `u8 pad_a[0x4c]` (exactly 19 more `s32`); GCC lays the
frame out differently, so on PC the writes can hit live locals (`pos`, `tile`,
`usedfallback`) -- a spawn position/tile corrupted to just under the floor
gives a rocket that never meets the ground. Only the player launch path does
this; NPC rockets and the per-tick flight (`handles_projectile_motion`, full
`roomNums[]` buffers) are clean. Same class as D490 (porting-notes §D24).

**Fix:** `s32 sp54[0x14]` under `#ifdef AVOID_UB` (array passed directly);
the `#else` arm keeps the original. No reads change. Build-verified.
**Verify (maintainer):** Aztec, dual rocket launchers, aim at Bond's feet
while running; if it still happens, the same shot on 1964/GEPD decides
faithful vs port.

**Residual suspects from the triage (not proven to fire):** (a) the
`bgBuildRoomVtxBounds` safety-net `continue` (bg.c) leaves a skipped batch
with impossible bounds = a silent collision hole (worth a counter if the
report persists); (b) projectile room lists only grow via portal crossings,
so a stale `unkCC` skips world tests (faithful logic). Object/chr collision
(`projectileTestObjectCollisionRecursive`) was not traced.

**Maintainer re-test (2026-10-06):** near Bond mostly fixed; some rockets
(akimbo) still pass through **walls and floor** (not doors), e.g. Archives.
Second static triage: no ABI/UB defect in the object/door path (door-only
suspect `sub_GAME_7F041400` `bestedge > 0` set aside, since it isn't doors). Leading
hypothesis, port-caused: the room pool starves (D294 documents mema starving
on PC when Draw/LOD/FOV widen the resident room set; D294's pool bump only
partly covers 2.0x), and `bgBuildRoomVtxBounds`'s `memaAlloc` failing leaves
`vtx_batch_bounds` NULL, so `bgTestBulletHitBackground` returns no hit for
EVERY wall/floor of a room that still renders. Test (no diagnostics, by
maintainer choice): same Archives shots under Modern vs the Original N64
preset (level restarted). If confirmed, the proposed fix allocates the bounds table
from port memory instead of the room pool (a few KB per room, freed with the
room; no game-logic change). Status stays PARTIAL until that test.

**Preset A/B result (maintainer, 2026-10-06, Archives, akimbo rockets):** fine
under Modern, Original N64 and custom high FOV; one rocket dropped in the whole
session (roughly 1 in 60), most likely under Modern or high FOV. D545 fixed the
main case. The single residual is consistent with the wide-view room-pool
hypothesis but not proven; the port-memory bounds table stays a proposed
hardening (backlog), not a release item.
