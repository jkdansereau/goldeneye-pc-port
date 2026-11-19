# D552

<!-- from the old section-F/H table row -->
| D552 | **Dam ending cutscene: the first camera shot no longer swivels up onto Bond — the D243 M-190 port-only look-at filter re-seed snapped it onto him** — full `## D552` entry at file tail | FIXED 2026-10-06 (port-only addition removed; code path back to the decomp; maintainer-verified live) |

## D552: Dam ending cutscene — first camera shot no longer swivelled up onto Bond (D243 M-190 re-seed removed) — FIXED 2026-10-06

**Report (maintainer playtest 2026-10-06, vs 1964/GEPD):** in the Dam ending
(the run to the dam edge / bungee), the first camera shot should swivel up
onto Bond, ending pointed at his front right; on the port it started
already aimed.

**Cause.** A port-only addition from D243 (M-190, 2026-09-18) in the
player-chr tick (`src/game/bondview2.c`, `#ifdef PORT`): on every
shot-change teleport during `CAMERAMODE_POSEND` it re-seeded the look-at
filter (`field_3B8` accumulator and `field_3C4/3C8/3CC`) to Bond's new
position, so the camera snapped onto him. The decomp's own leaky integrator
(`bondviewUpdatePlayerCollisionPositionFields`) instead converges over about
20 ticks from the stale pre-teleport position — that convergence is the
swivel (D243 M-143 logged the same sweep, pos2.y -22832 -> +691, and read it
as shake). D243's actual shake cause was the `sizeof(Model)` buffer overlap
(M-189), fixed independently, so neither M-170's freeze nor M-190's re-seed
was still needed.

**Fix.** Removed the re-seed block, its teleport-epoch locals, the epoch
counter (`d243NotifyTeleport` / `d243GetTeleportEpoch`) and the notify call in
`src/game/chrai.c` (`AI_TRYTeleportingChrToPad`) — all `#ifdef PORT`
additions. The path is now the decomp, unmodified (the unconditional
`field_488.pos` write is original code). No Rule-2 sign-off needed: this
removes a port deviation. **Verified:** build rc 0; maintainer replayed the
Dam ending live — swivel present, no shake.
