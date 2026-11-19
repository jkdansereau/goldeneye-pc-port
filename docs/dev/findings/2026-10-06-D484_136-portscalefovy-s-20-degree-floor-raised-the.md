# D484

<!-- from the old section-F/H table row -->
| D484 | **#136: `portScaleFovY`'s 20-degree floor raised the game's own zoom FOVs, capping sniper/camera magnification at 20 degrees instead of the N64's 7 (about a third of the zoom)** — full `## D484` entry at file tail | FIXED 2026-10-02 (local, `fix/d483-sniper`, maintainer-approved 2026-10-02); full zoom renders at 7 degrees, normal views pixel-identical |

## D484 — #136: the port's FOV floor capped sniper/camera zoom at 20 degrees

**Report (#136, outside):** "zoom rate is too low when using sniper rifle".
**Original code:** the sniper and camera zoom is a FOV in degrees, `sniper_zoom` 60 -> 7 (`gun.c` `camera_sniper_zoom_in/out`), fed to the projection through `g_ViBackData->fovy`.
**Port divergence:** `portScaleFovY` (`port/src/video.c`), applied to every in-level projection at `fr.c:735` since the widescreen-FOV work (`643785c2`), clamped the result to [20, 160] degrees even at default settings. The full sniper zoom therefore rendered at 20 degrees instead of 7: about a third of the N64's magnification. The same floor affected any game FOV below 20 (camera gadget, watch zoom).
**Fix (port layer):** the floor is now min(20, the game's own fovy): it still guards the port's widening (FovScale / widescreen boost) but never narrows the game's view less than the game asked for. The 160-degree ceiling is unchanged.
**Verified 2026-10-02:** scripted aim + C-up with the sniper on Dam: the full-zoom frame now matches a probe build with no floor (dmean 7.8, aim-timing variance) instead of the 20-degree frame (dmean 18.2); the gun model drops out of view at full zoom as on the N64; held mission-select menu frame pixel-identical (dmean 0.000), since normal FOVs never reach the floor. Mouse-aim sensitivity while zoomed now scales with the real zoom (input.c uses the same function).
**Related (fixed separately as D485):** the zoom *speed*. `camera_sniper_zoom_in/out` step once per game frame with no tick scaling; measured at `clk=1` (one tick per frame) on the port, so the full 60 -> 7 range takes ~22 frames = ~0.4 s at 60 fps against ~0.75-1.1 s on an N64 running 20-30 fps. Only the sniper and camera use this per-frame stepping; every other zoom (weapon aim zoom, watch zoom) advances by elapsed ticks (`zoomintime += speedgraphframes`, `bondview2.c` ~3151) and is already frame-rate independent. Same class as D13/D329.
