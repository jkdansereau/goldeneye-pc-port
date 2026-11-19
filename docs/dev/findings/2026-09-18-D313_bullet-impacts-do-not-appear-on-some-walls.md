# D313

<!-- from the old section-F/H table row -->
| D313 | **Bullet impacts do not appear on some walls/surfaces — the projectile appears to pass through them; impacts DO work on some surfaces such as doors/windows (user report, v0.3.0 pre-release playtest, 2026-09-18).** — full `## D313` entry at file tail | FIXED — same root cause as D312, merged diagnosis; same one-line fix. Doors/windows use the separate working `bgTestHitOnObj` path, hence the "surface-dependent" appearance. |

## D313 — Bullet impacts do not appear on some walls/surfaces — the projectile appears to pass through them; impacts DO work on some surfaces such as doors/windows (user report, v0.3.0 pre-release playtest, 2026-09-18) — FIXED: same root cause as D312 (`bgBuildRoomVtxBounds` G_VTX count misread), merged (diagnosed + fixed, 2026-09-18).

**Reported:** user's last-minute v0.3.0 playtest: firing at some walls/surfaces produces no bullet-impact effect — visually it looks like the projectile goes through them — while other surfaces (doors, windows named specifically) show impacts correctly. So the impact system works; its trigger is surface-dependent.

**Diagnosis:** merged into D312 — see that entry for the full trace. The bullet path (`chrprop.c` fire → `bgTestBulletHitBackground`) shares the same broken per-batch bounds table: batches whose G_VTX block holds exactly 16 vertices get single-point bounds, fail the AABB pre-test, and their wall triangles are never ray-tested — so no hit is registered and no impact effect spawns. Doors/windows use the separate `bgTestHitOnObj` path (unaffected), which is why impacts worked there. The "surface-dependent" appearance was actually batch-size-dependent.

**Fix:** same one-line change as D312 (`src/game/bg.c` `bgBuildRoomVtxBounds`, `#ifdef PORT`). Verified: pistol shots at the previously-failing Runaway wall now register hits on the wall plane and spawn impacts.

**Status:** FIXED (v0.3.1). Cross-refs: D312 (root cause), D172 (impact-effect path, fixed earlier).

## D313 — Bullet impacts do not appear on some walls/surfaces — the projectile appears to pass through them; impacts DO work on some surfaces such as doors/windows (user report, v0.3.0 pre-release playtest, 2026-09-18).

**Reported:** user's last-minute v0.3.0 playtest: firing at some walls/surfaces produces no bullet-impact effect — visually it looks like the projectile goes through them — while other surfaces (doors, windows named specifically) show impacts correctly. So the impact system works; its trigger is surface-dependent.

**Not investigated.** Starting pointers only, nothing below is evidence:
- The surface-dependence is the key clue: impacts likely gate on a hit-surface property (surface type / imagetable entry / material class). The first step is finding where `src/game` decides "this hit spawns an impact" and what surface field it reads — then checking whether that field is being misread on PC (§A1 ABI catalogue: a 32-bit-era offset or width in the surface/imagetable record would produce exactly this "works on some surfaces, not others" shape).
- D172 (impact-particle color, FIXED) is the nearest prior art for the impact-effect path itself; D266/D271 show this codebase has a history of texture-format-dependent misreads — worth checking whether the no-impact surfaces correlate with a particular authored surface/texture class.
- Cross-check D312: if throwables clip through the SAME walls that don't show impacts, one shared wall/collider class is the likely root and the two findings should be merged during investigation.

**Status:** OPEN — reported only. Explicitly not for v0.3.0; queued for v0.3.1. Cross-refs: D312 (possible common wall class), D172 (impact-effect path, fixed).

**Current status (2026-09-29 bookkeeping):** the Status line above is historical; the authoritative verdict is the §F row — FIXED — same root cause as D312, merged diagnosis; same one-line fix. Doors/windows use the separate working `bgTestHitOnObj` path, hence the "surface-dependent" appearance.
