# D565

<!-- from the old section-F/H table row -->
| D565 | **AI awareness followed two player render settings: Fog distance below 100 (shortened clip) and FOV (gameplay fog cull scaled by the rendered FOV)** — full `## D565` entry at file tail | FIXED 2026-10-06 (maintainer decision: Fog distance floor 100 in `video.c` + overlay; `propobj.c portSub7F054C58Gameplay` rescaled to the game's own FOV, PORT-only D466 helper; identity at defaults; build-verified) |

## D565: AI awareness followed two player render settings (Fog distance < 100, FOV) — FIXED 2026-10-06 (maintainer decision)

**Found by** the v0.4.0..v0.5.0 AI regression review (interpreter, movement/timing,
visibility/combat; three read-only reviewers, lead re-checked every flag in code).
No regression at the shipped settings (Draw 2.0x, Fog 1.0x, FovScale 100, native
widescreen). Two leaks of render-only settings into AI awareness (the D466/D468
class) at non-default settings:

1. **`Video.FogDistance` below 100.** `bgfog.c fogLoadCurrentEnvironment` sets the
   far clip to `FarFog x min(Fog, Draw)` (D540), so Fog < 1.0 shortens it below the
   authored distance. AI on-screen checks require the render `PROPFLAG_ONSCREEN`
   (`port/src/drawdistgameplay.c portPropGameplayOnScreen`), so guards beyond the
   shortened clip stopped counting as on screen; at Draw 1.0x `portD466Active()` is
   off and the sight range (`fogGetScaledFarFogIntensitySquared`) shrank as well.
   Guards would notice Bond, and on-screen triggers fire, later than on the N64.
   **Fix (maintainer: block it):** the setting's floor is now 100
   (`port/src/video.c` registration, so an ini value of 50/75 clamps to 100 on
   load; `port/src/optionsoverlay.c` slider `uiMin` 100). 100-800 keeps the D540
   extension, which D466 already keeps off gameplay.
2. **FOV other than the game's (`Video.FovScale` != 100, or the stretch-era
   `WidescreenAuto` boost with native widescreen off).** The D466 gameplay fog cull
   `src/game/propobj.c portSub7F054C58Gameplay` scales by `c_lodscalez`, which is
   tan(fovy/2)-proportional and built from the rendered fovy (D222 `frCullFovY`), so
   a wider FOV dropped far fogged positions out of the AI on-screen verdict sooner
   (narrower: later). **Fix:** inside that `#ifdef PORT` D466 helper, rescale
   `scalez` by tan(own/2)/tan(eff/2) with `own = c_perspfovy / portFovYScaleFactor()`
   (the D468 recovery of the game's own fovy; the game's zoom FOVs are kept).
   Identity when the factor is 1 (the defaults). Port-feature leak fix in an
   existing PORT-only helper (D466 sign-off class); maintainer asked for the fix
   2026-10-06.

Checked and left (no AI harm): the D466 verdict reuses the extended pass's room
screen bbox (`posIsOnScreen`), but at Fog 1.0x the clip is the authored far, so the
passes match; D491 (portal bbox seeds) and D431 (light-fixture texnum in ray
scoring) restore N64 data reads; D417, D468 and the M-183/M-185 clamp removal are
signed off and play-covered. Also corrected: a watchdog `pin signature detected ...
confirmation window` line that clears on its own is the normal post-D329 pattern
(the 2026-09-23 D329 sign-off log had three); only `anim pin CONFIRMED` / `pin still
held` indicates a regression. The maintainer's 2026-10-06 Deck Facility run logged
four self-clearing detections and no confirmation.

**Verify:** build; defaults are bit-identical (both changes are no-ops at Fog 100 /
factor 1), so the golden gate is unaffected. By eye (optional): FovScale 150 on a
fogged level (Surface 1, Dam), guards at fog range react as at FovScale 100.
