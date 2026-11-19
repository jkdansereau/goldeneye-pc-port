# D394

<!-- from the old section-F/H table row -->
| D394 | **Xbox 1.1 Jinx controller parity** — user supplied the Xbox 1.1–1.4 control table; B is gadgets, stick clicks crouch, LB aim, RB HD toggle (not weapon cycling). | **PARTIAL (2026-09-28).** Xbox 1.1 / GEPD presets shipped and validated in the campaign playtest; presets 1.2-1.4 -> v0.5.0 wave. |

## D394 — Xbox 1.1 Jinx controller parity (2026-09-27).

Reconstructed stub — the original full entry was lost when `findings.md` was truncated to its section table (recovery, 2026-09-28); this entry is rebuilt from the section-table row and the `findings-index.csv` one-liner. Original root-cause detail is not recoverable.

**Status: PARTIAL (port 1.1 gameplay button roles user-tested; exact gadget-category parity and other selectable presets outstanding; RB HD toggle unavailable)**: user supplied the Xbox 1.1–1.4 control table; B is gadgets, stick clicks crouch, LB aim, RB HD toggle (not weapon cycling).

**Reference table restored (maintainer, 2026-10-03, from StrategyWiki's GoldenEye 007 controls page, "Xbox One/Xbox Series X|S" section).** The four Xbox styles share every button; only the sticks and D-pad differ.

| Xbox control | All four styles |
|---|---|
| RT | Fire |
| LT, LB | Crosshair aim |
| A | Action |
| X | Reload |
| B | Cycle gadgets |
| Y | Cycle weapons |
| L / R stick click | Crouch |
| RB | Toggle updated graphics (no port equivalent) |
| Start | Menu / pause |
| Back (View) | View scores (the port uses Back for the F10 overlay) |

Stick cells on the source read "while aiming / up-down axis / left-right axis". The D-pad copies the left stick in every style.

| Style | Left stick (aiming / up-down / left-right) | Right stick (aiming / up-down / left-right) |
|---|---|---|
| 1.1 Jinx | crosshair / move / strafe | strafe / look / turn |
| 1.2 Christmas | crosshair / move / turn | strafe / look / strafe |
| 1.3 Frost | crosshair / look / strafe | strafe / move / turn |
| 1.4 Elektra | crosshair / look / turn | strafe / move / strafe |

**Mapping onto the port (input.c stick block, 2026-10-03):** `naturalPitchMode` on (SOLITARE) = left stick to C-buttons (move/strafe), right stick to the N64 stick (look/turn) = **1.1 Jinx** (today's default). `naturalPitchMode` off (HONEY) = left stick to the N64 stick (move/turn), right stick to C-buttons (look/strafe) = **1.2 Christmas**. **1.3 Frost** = HONEY with the sticks swapped; **1.4 Elektra** = SOLITARE with the sticks swapped (neither exists yet). Divergence in all styles: on Xbox the LEFT stick moves the crosshair while aiming and the right stick strafes; the port aims with whichever stick feeds the N64 stick (the right stick under Jinx). The D-pad currently sends N64 D-pad bits, not a left-stick copy.

**Original N64 control styles (historic reference; same StrategyWiki page, "Nintendo 64" section, maintainer 2026-10-03).** Stick cells read "while aiming / up-down axis"; the stick's left-right axis always turns. Enum: `CONTROLLER_CONFIG_*` (`src/bondconstants.h`); names `front.c` ~732; watch control-screen labels `options.c` ~149.

Single controller:

| N64 control | 1.1 Honey | 1.2 Solitaire | 1.3 Kissy | 1.4 Goodnight |
|---|---|---|---|---|
| Control stick | crosshair / move (left-right turns) | crosshair / look (left-right turns) | crosshair / move | crosshair / look |
| Z | Fire | Fire | Aim | Aim |
| B | Reload / action | Reload / action | Reload / action | Reload / action |
| A | Change weapon | Change weapon | Fire | Fire |
| L or R | Aim | Aim | Change weapon | Change weapon |
| C left/right | Strafe | Strafe | Strafe | Strafe |
| C up/down | Look up/down | Move forward/back | Look up/down | Move forward/back |
| D-pad | Same as C | Same as C | Same as C | Same as C |
| Start | Pause | Pause | Pause | Pause |

Two controllers (one player holds both):

| N64 control | 2.1 Plenty | 2.2 Galore | 2.3 Domino | 2.4 Goodhead |
|---|---|---|---|---|
| Controller 1 stick | crosshair / move | crosshair / look | crosshair / move | crosshair / look |
| Controller 1 Z | Fire | Fire | Aim | Aim |
| Controller 2 stick | strafe / look | strafe / move | strafe / look | strafe / move |
| Controller 2 Z | Aim | Aim | Fire | Fire |
| B | Reload / action | Reload / action | Reload / action | Reload / action |
| A | Change weapon | Change weapon | Change weapon | Change weapon |
| Start | Pause | Pause | Pause | Pause |

How the Xbox styles relate: 1.1 Jinx is Solitaire's movement split onto two sticks (left = C-buttons move/strafe, right = N64 stick look/turn), which is why the port drives Jinx through `CONTROLLER_CONFIG_SOLITARE` (D194/D238); 1.2 Christmas is Honey split the same way (left = N64 stick move/turn, right = C look/strafe). 1.3/1.4 swap the sticks of 1.2/1.1. Xbox drops the Kissy/Goodnight button layouts and the 2.x two-pad styles.
