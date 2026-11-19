# D646

<!-- from the old section-F/H table row -->
| D646 | **F10 dropdown popup drew over the hint/tooltip panel (popup bounded by the screen edge, not the settings box)** — full `## D646` entry at file tail | FIXED 2026-10-12 (`ddGeom` bounded to the box; maintainer-verified) |

## D646: F10 dropdown popup drew over the hint/tooltip panel

**Symptom.** Maintainer 2026-10-12: the Video page "Frame rate cap" dropdown opened downward over the tooltip panel below the settings box (both translucent, text overlapped).

**Cause.** `ddGeom` (`port/src/optionsoverlay.c`) only flipped the popup upward when it would pass the screen edge (`ovH() - 4`), not the settings box bottom (`o.bottom`); the hint panel sits between the two.

**Fix.** The popup is bounded to the settings box (`o.titleB + 1` .. `o.bottom - 1`): it opens upward when there is no room below, and the D568 fallback now pins it flush to the box bottom instead of the screen.

**Status.** FIXED, maintainer-verified 2026-10-12.
