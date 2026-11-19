# D542

<!-- from the old section-F/H table row -->
| D542 | **Stray polygons left of Bond's gun, frequent on foggy levels (Streets, Surface 2)** — D540-build playtest; full `## D542` entry at file tail | OPEN 2026-10-05 (triage: GE_SHADEPERSP / GE_FOGVERTEX / rc1 A/B, then 1964) |

## D542: stray polygons drawn to the left of Bond's gun, frequent on foggy levels (Streets, Surface 2) — OPEN (Deck/Windows playtest of the D540 build, 2026-10-05)

**Report:** with the D540 fog build, polygons "randomly draw to the left of
Bond's gun", very often, especially on foggy levels (Streets, Surface 2).

**Not yet triaged.** Candidates: (a) a D540 regression in near-camera geometry
(the viewmodel and nearby room triangles: `noperspective` shade, or the
per-pixel fog's `fog * w` path); (b) the pre-existing Streets "1-frame polygon
explosion near the body armour / grenade launcher" from the same playtest (its
1964 check is still owed), now seen more often. **First step (cheap A/B, no
code):** same spot with `GE_SHADEPERSP=1`, then `GE_FOGVERTEX=1`, then the rc1
binary (Deck: `rc1-backup/ge007.x86_64`); whichever makes it vanish names the
layer. A 1964 look at the same spot settles faithful vs port.
