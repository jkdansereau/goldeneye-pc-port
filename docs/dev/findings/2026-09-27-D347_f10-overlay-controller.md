# D347

<!-- from the old section-F/H table row -->
| D347 | **F10 overlay controller: left/right did nothing (only A/Y adjusted values) and no input had hold-to-repeat (2026-09-27).** — full `## D347` entry at file tail | FIXED (port-only, `optionsoverlay.c`): D-pad left/right + stick-X wired into value adjust (A/X/B/Y kept); 18/4-frame hold-to-repeat on adjust and up/down nav, matching the options screen's D345(e)/(f) cadence. User validated physical-controller pass. |

## D347 — F10 overlay controller value-adjust + hold-to-repeat (2026-09-27)

User report (controller, Steam Deck class): in the F10 options overlay,
left/right did nothing — only A (increment) / Y (decrement) adjusted values,
"feels bad". Root cause: `optionsOverlayHandleInput()` wired only DPAD_UP/DOWN
+ stick-Y to navigation and A/X/B/Y to adjust; **DPAD_LEFT/RIGHT and stick-X
were never read**, and every action was edge-triggered — no hold-to-repeat
anywhere (holding a key or the stick stepped exactly once).

Fix (port-only, `optionsoverlay.c`):
- D-pad left/right + left-stick X (±12000, matching the existing Y-axis style)
  OR-ed into the lf/rt adjust inputs. A/X/B/Y keep working as before.
- Hold-to-repeat on value adjust: first press fires immediately, then an
  18-frame delay and a 4-frame repeat — the same cadence as the options
  screen's D345(e)/(f) blocks, so both settings UIs now feel identical.
- Same repeat added to up/down navigation (previously one row per press even
  on keyboard — traversing the list held-down was impossible). `overlayStepSel`
  clamps at the ends, so holding at an end is a harmless no-op.
- Repeat state (`navDir/navTimer/adjDir/adjTimer`) resets when the overlay
  closes, alongside the existing prev*/dragRow reset.

Toggles flip on either adjust direction (existing `rowAdjust` behaviour);
sliders step by their registered step; MSAA/enum wrap as before.

**Verification:** `build-pc.sh ntsc-final` clean; headless boot smoke clean.
Subsequent user physical-controller pass validated the F10 overlay, options
screen and bottom-bar label; all good. No new automated controller test was run.
