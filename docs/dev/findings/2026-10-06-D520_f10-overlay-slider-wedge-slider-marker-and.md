# D520

<!-- from the old section-F/H table row -->
| D520 | **F10 overlay slider wedge, slider marker and dropdown arrow antialiased (they stepped in whole canvas pixels that grow with the window)** — Port-only; D504 follow-up, P4k investigation; full `## D520` entry at file tail | FIXED (headless before/after capture; by-eye owed) |

## D520: F10 overlay slider wedge, marker and dropdown arrow antialiased (port-only; D504 follow-up)

**Problem (maintainer by-eye pass 2026-10-04).** The overlay's sliders and dropdown arrows looked more aliased than the PD port's menus. **Cause (P4k investigation, local note `OVERLAY-AA-INVESTIGATION.md`):** the overlay lays out on a 320-wide canvas scaled to the window (`ovScale`), and `drawWedge` / the dropdown tick built their diagonals from integer-pixel fill rects (2-px columns, whole-pixel heights), so every stair step is `ovScale` screen pixels (4 px at 1280, 8 px at 2560). PD draws the same shapes as triangles (`menugfx.c:861-906`) that MSAA smooths. Text was not the issue (bilinear-magnified glyphs: soft, not jagged).

**Fix (`port/src/optionsoverlay.c`).** `drawWedge`: the solid body as runs of equal whole height plus one coverage-alpha top pixel per 1-px column; the marker and the dropdown arrow via `drawTriDownAA` (per-pixel area coverage from 4 sub-rows). Render state is set once per shape (`aaBegin`) and each piece is 2 commands (`aaPiece`, pieces under alpha 6 skipped), so the display-list size stays about where it was (D516 overflow history; `OV_BUF_CMDS` 16384). Applies in every Display mode (maintainer: the overlay is port UI, not part of the N64 picture).

**Verified.** Build rc=0, no warnings; `GE_OPTIONSOVERLAY=1 GE_OPTIONSOVERLAY_SECTION=__HdrAudio GE_PCDUMP=300-300` before/after: the ramps go from ~7 visible steps to a smooth ramp, markers/arrows softened; no DL overflow logged. **Owed:** maintainer by-eye at their window size.
