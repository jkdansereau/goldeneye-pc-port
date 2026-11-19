# D368

<!-- from the old section-F/H table row -->
| D368 | **Simplify F10 click targets: setting name selects, the rest changes (2026-09-27).** — full `## D368` entry at file tail | **CLOSED (2026-09-28 bookkeeping: superseded by D369 (input rule kept)).** Earlier: INPUT RULE KEPT; tinted strips user-rejected, removed in D369. |

## D368 — F10: name selects, right-of-name changes (2026-09-27)

**User decision:** stop trying to frame each differently sized value; all
of the row except the setting name should change it. D367's boxed controls
were still misleading in a live test.

**Fix (port-only):** `overlayControlSpan` now starts after the measured
Bank Gothic setting name (capped at its label-column clipping edge) and
extends to the panel's right edge. It is used by mouse hover, left/right
click and a single subtly tinted strip with a thin left divider; the
per-value borders are gone. Clicking a name still focuses without changing.
A slider click on its track positions/drags; clicking elsewhere to the right
of its name changes it by one step instead of snapping to the minimum or
maximum. Reset is an action, so its whole row activates the existing
confirmation. The footer now says `RIGHT: CHANGE`. No config/save or game
logic changes. NTSC build/link passes; live mouse check still owed.
