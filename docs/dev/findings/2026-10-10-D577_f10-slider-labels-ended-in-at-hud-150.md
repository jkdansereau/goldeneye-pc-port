# D577

<!-- from the old section-F/H table row -->
| D577 | **F10 slider labels ended in ".." at HUD 150: the slider bar and value column now shrink when the page's widest slider label does not fit** — full `## D577` entry at file tail | FIXED 2026-10-07 (captures HUD 100 unchanged / HUD 150 whole) |

## D577: F10 slider labels ellipsised at HUD 150 — FIXED 2026-10-07

`overlayLayout()` (`port/src/optionsoverlay.c`) placed the slider bar at fixed
canvas units (value column 54, bar 66), but the canvas shrinks with
`Game.HudScale` (D512: `ovW = 320*100/hud`). At HUD 150 a slider label had 53
units (160 at HUD 100), so "Anisotropic filtering", "Draw distance", "Fog
distance" showed as "Anisotrop..", "Draw dist..", "Fog dista..". (Open item
from D554.) Now, on a settings page, the widest slider label over the whole
page (so the bar never moves while scrolling) is measured; if it does not fit,
the bar shrinks first (66 -> 32), then the value column (54 -> 40); anything
still too long keeps the existing ".." fallback. A page that fits is laid out
exactly as before. Drag hit-testing and the label limits read the same layout.

**Verified:** build rc 0; 1280x720 captures of the Video page with
`GE_OPTIONSOVERLAY_SECTION=__HdrVideo`: HUD 150 labels whole (before/after
pair); HUD 100 bar, value and label positions match the unfixed build.
**Owed (maintainer):** by-eye pass of the other slider pages at HUD 150.
