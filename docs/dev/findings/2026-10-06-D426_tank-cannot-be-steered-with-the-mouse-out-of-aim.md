# D426

<!-- from the old section-F/H table row -->
| D426 | **Tank cannot be steered with the mouse out of aim mode (issue #116).** — full `## D426` entry at file tail | RESOLVED (user 2026-09-29: tank controls now match GEPD/1964; see D429). |

## D426 — Tank cannot be steered with the mouse out of aim mode (issue #116)

Tank hull turns from stick X (`analogTurn`, `bondview2.c` ~5166); port hipfire mouse used `hipDirectCompute` (direct `vv_theta` write, ignored/overwritten in tank). **Fix (`port/src/input.c`):** in tank, hipfire mouse X emits stick turn (x `Input.TankAimScale`), mouse Y dropped (stick Y is drive). Pad CENTRED direct path in tank not audited. **Status:** FIXED; user-verified 2026-09-29 (non-aim tank mouse turning good; aim behaviour matches the original, D429).
