# D428

<!-- from the old section-F/H table row -->
| D428 | **No gun turn-sway with direct mouse look (issue #117).** — full `## D428` entry at file tail | FIXED in tree (2nd pass: exact inverse of the game's yaw integration, clamp fov/60); user-confirmed feels right 2026-09-29. |

## D428 — No gun turn-sway with direct mouse look (issue #117)

Gun turn lag = `weapon_theta_displacement = -1.75*speedtheta` (`gun.c` `gunSetBondWeaponSway`). Direct-look paths (D238 hipDirectCompute, centred, PD aim) write the camera and leave `speedtheta`~0. Writing `speedtheta` would also move the camera; needs a display-only route or design decision. **Fix (rule-2, user go-ahead; `#ifdef PORT`):** `hipDirectCompute` records the yaw degrees it applied; hipfire feeds them to an atomic accumulator (`swayFeedTheta`, `port/src/input.c`); `bondview2.c` adds `portGunSwayTheta(g_GlobalTimerDelta)` to the `speedtheta` value passed to `gunSetBondWeaponSway` (player 0 only). The speed uses the game's own convention for deriving speed from an angle change (`bondview2.c` speedverta: delta / (2*dt), clamp +/-0.7). Camera untouched; vertical sway already worked (the game derives `speedverta` from the real `vv_verta` change). Aim mode not fed (sway-free while aiming). Pad centred-direct path not fed. Verified the hook fires (`GE_INPUTLOG` `gunsway` lines, ~0.13 speed at 8 px/poll); amount is by-feel, tune the 2*dt divisor if needed.
**Status:** FIXED; user-confirmed feels right 2026-09-29 (second pass, see below; the formula in the Fix paragraph above is the superseded first pass). **Split-screen note:** the accumulator is global and drained only by player 0; revisit when #99 (mouse on a non-zero slot) merges.

## D428 second pass (2026-09-29)

**Split-screen update (2026-09-30):** the sway accumulator is now one per player (`s_swayThetaMdeg[4]`, `ac92e0ab`). The game-side drain in `bondview2.c` is still gated to player 0 (`get_cur_playernum() == 0`), which is correct today: `swayFeedTheta` is only called from the slot-0 mouse block, so slots > 0 are never fed. Widening the gate is only needed if a non-zero slot ever gets a mouse (a `src/game` edit inside the D428 Rule-2 block; ask for sign-off then).

Probe (`GE_D428`) showed the hook firing with the input clamped at 0.7 on most fast turns. The game's natural stick turn sets `speedtheta = (stick/70)^2 * fov/60` with NO ramp (max ~1.0) and integrates `vv_theta += speedtheta*dt*3.5`, so the exact inverse for a mouse turn is `deg / (3.5*dt)` clamped to `fov/60`. The first pass used the vertical axis' `delta/(2*dt)` clamped to 0.7: too weak at full speed, too strong at slow speed. Switched; user reports a slight difference turning fast, which this addresses.
