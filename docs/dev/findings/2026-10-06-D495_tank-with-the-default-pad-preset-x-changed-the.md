# D495

<!-- from the old section-F/H table row -->
| D495 | **Tank: with the default pad preset X changed the tank weapon and Y left it, because in-tank faces fell back to the fixed menu faces; `PadMapCtx.tank` routes Next/Prev weapon to N64 A and aim/fire to R/Z** — full `## D495` entry at file tail | FIXED 2026-10-03, maintainer-verified in play |

## D495 — Tank: pad Next/Prev weapon changed the tank weapon / left the tank instead of following the bindings (2026-10-03)

**Report:** maintainer: with the default pad preset, X changed the tank weapon and Y left the tank; on foot Y changes weapon.
**Cause:** in the tank `inputCanUseGameplayActions` is false, so `padMapPure` (`port/src/input.c`) used the fixed menu faces (A/X -> N64 A = change weapon, B/Y -> N64 B = exit).
**Fix (port only, commit a69cc144):** `PadMapCtx.tank`; while seated in the tank (slot 0, stage running, not paused, outside the watch) the pad's Next/Prev weapon bindings send N64 A, digital aim/fire bindings send R/Z, and the raw face buttons send nothing else; exit stays on the bound Use action via the existing D407 locked-B path. Preset 0 outside the tank is unchanged (GE_PADMAPTEST contexts leave `tank = 0`).
**Verified:** build only.
**Owed:** none.
**Status:** FIXED, maintainer-verified 2026-10-03.
**Maintainer play check 2026-10-03:** works, "feels good now".
