# D375

<!-- from the old section-F/H table row -->
| D375 | **Crouch wired to the wrong pad bit: B is not the crouch input (v0.4.0 M3 playtest, 2026-09-27).** full `## D375` entry at file tail | **CLOSED (2026-09-28).** Shipped in v0.4.0; accepted in the user's final review / campaign playtest. |

## D375 -- Crouch wired to the wrong pad bit (v0.4.0 M3 playtest, 2026-09-27)

**User report:** "crouch still does not work" after D374. The ini was checked
first: clean (`Crouch =` empty, preset applies) -- **not the cause**.

**Diagnosis:** the engine never reads B for crouch. bondview2.c:5318/5579:
`moveData.crouchDown = insightaimmode && (buttons & (D_JPAD | D_CBUTTONS))`
(keyboard/stick variant: `insightaimmode && stickY < -30`). N64 crouch was
**Z (aim) + C-down**. M3's `IA_CROUCH -> GE_CONT_B` (and the toggle latch)
was inert -- the bit the game reads was never set.

**Fix (port-layer only, `port/src/input.c`):** IA_CROUCH hold + the
`CrouchMode=1` latch now emit `GE_CONT_D` (0x0004, C-down). Side effect,
N64-faithful: S (IA_BACK, already C-down for "back") also crouches while
aiming, exactly as C-down did on N64. Crouch therefore only works **while
in aim (insight) mode** -- holding RMB/Left Shift + Left Ctrl. On release
(or leaving aim) the player stands (bondview2's `crouchUp` branch).

**Verification:** build clean; clean boot + exit. **Owed:** user live
re-test (aim + Ctrl, both hold and F10 Toggle mode). Cross-tag: the D373
entry's "emits B" wording was superseded by D375 (reload still emits B;
crouch no longer does).
