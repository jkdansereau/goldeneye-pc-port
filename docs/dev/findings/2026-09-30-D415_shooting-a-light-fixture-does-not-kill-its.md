# D415

<!-- from the old section-F/H table row -->
| D415 | **Shooting a light fixture does not "kill" its flickering light (Bunker, Caverns): the fixture takes the hit but the flicker keeps going (community report, issue #87 comment by the reporter, 2026-09-28, 00 Agent playthrough of v0.4.0).** — full `## D415` entry at file tail | FIXED (2026-09-29 bookkeeping: root-caused and fixed as D430 + D431, user-verified vs GEPD/1964 on Caverns 2026-09-29; #119). Earlier: ROOT-CAUSED as D430 (see there); fix in tree, live check owed. |

## D415 — Shooting a light fixture does not "kill" its flickering light (Bunker, Caverns) (community report, 2026-09-28)

**Report (issue #87, comment from the issue's reporter, 2026-09-28, after an
00 Agent playthrough of v0.4.0):** "shooting the lights fixtures in bunker and
caverns does not actually 'kill' the flicker light." Reported as one of "a few
little things"; the reporter asked whether it is already known before filing
separate tickets. It was not: no prior finding or backlog row covers it.

**Not yet established:** whether the N64 original (1964/GEPD) kills the
flicker on those specific fixtures, i.e. whether this is a port bug at all;
which fixtures (flickering vs. static lights) and levels are affected; whether
the fixture's texture/vertex darkening happens and only the flicker persists,
or nothing changes.

**Starting points (unverified leads, not a diagnosis):** the break path is
`lightFixtureBreak()` (`src/game/lightfixture.c:407`) →
`darken_triangle_in_room()` / `darken_vertex_in_room()`, which edit room vertex
colours located via `lightFindVertexBaseForTri()` (`:176`, the same cast #107
touches; s32-safe on Windows/Linux because the DRAM arena is pinned at
0x70000000). The flicker itself may be driven by a separate per-frame
room/vertex light animation that rewrites the colours after the darkening; if
so, check whether the fixture table's per-room state (`light_fixture_table`,
`redarken_lights_in_room()`) is honoured on PC. Also rule out fast3d vertex
caching of room geometry, which would hide a vertex-colour edit.

**Next:** confirm the N64 behaviour on 1964 (Bunker 1 and Caverns, shoot a
flickering fixture), then repro on PC.

**Status:** OPEN (logged, not investigated). Now tracked as GitHub issue #119; the reporter's statement that the N64 stops the flicker is accepted as the baseline (a 1964 check is a bonus, not a gate).

**Current status (2026-09-29 bookkeeping):** the Status line above is historical; the authoritative verdict is the §F row — FIXED (2026-09-29 bookkeeping: root-caused and fixed as D430 + D431, user-verified vs GEPD/1964 on Caverns 2026-09-29; #119). Earlier: ROOT-CAUSED as D430 (see there); fix in tree, live check owed.
