# D197

<!-- from the old section-F/H table row -->
| D197 | **Character face/head textures wrap around the head, seen on Silo (M-49, user playtest).** — full `## D197` entry at file tail | CLOSED — user playtest v0.2.0-pre |

## D197 — Character face/head textures wrap around the head, seen on Silo (M-49, user playtest).

**Character face/head textures wrap around the head, seen on Silo (M-49, user playtest).** Reads as `G_TX_CLAMP` not honoured (or a non-power-of-two wrap period) on a head/face texture tile — the texture repeats around the mesh instead of clamping at the UV edge. Not yet isolated to a specific character or texture; no `GE_PCDUMP`/`GE_TEXDUMP` capture taken. Likely fast3d texture-tile setup (clamp/wrap mode bits from the tile descriptor not round-tripped) rather than a data bug, given it's cosmetic and geometry-independent. **Next:** `GE_TEXDUMP` the offending head texture's tile descriptor on Silo, diff clamp/mirror bits against `gmain.s`'s expectations for that material. **v0.2.0-pre playtest (user):** no longer observed on Silo — closed, dropped from release known-issues.

**Status (from §F table):** CLOSED — user playtest v0.2.0-pre
