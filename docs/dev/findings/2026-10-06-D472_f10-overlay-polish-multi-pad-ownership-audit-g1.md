# D472

<!-- from the old section-F/H table row -->
| D472 | **F10 overlay polish: multi-pad ownership (audit G1-G3), widescreen pillarbox (D335b), red EXPERIMENTAL rows, unknown-ini-key warning.** — full `## D472` entry at file tail | FIXED 2026-10-01 (merged; port-only, no src/game). Headless-verified pillarbox/red rows/warning at 1280x720; multi-pad ownership by code reading only, live check owed. |

## D472 -- F10 overlay polish: pad ownership, pillarbox, experimental colour, unknown-key warning

**Ownership (PAD-MENU-AUDIT G1-G3).** `input.c`: `padSelectPrev` is now per pad; `s_ovOwner` records the pad whose Back opened F10 (reset to -1 whenever the overlay is closed); `inputOverlayOwnerPad()` returns the opener while attached, else the lowest attached pad (keyboard-opened, or the owner detached), else 0. Only the owner's Back closes it from the poll path (non-owner Back is edge-tracked and ignored while open). The overlay tick still runs from slot 0's poll, but every `inputPadButton(0, ..)`/`inputPadAxis(0, ..)` in `optionsoverlay.c` (nav, A/X, B, Start, Y-clear, D469 capture fallbacks) now reads the owner. A non-zero owner's own slot is returned neutral while open (as pad 0 always was). `inputPadHelpFmt` uses the owner's family while the overlay is open (front-end page still seat 0). Solo single pad: owner is always pad 0 = unchanged. D343 respected (`frontOptionsBlocksOverlay`). No `g_CurrentPlayer` access (D419).

**Pillarbox (D335b).** The dim stays full-window; card + text are wrapped in `PORT_HUD_ASPECT(CENTER)` ... `NONE` (hudaspect.h, no emission at <= 4:3, so 4:3 is byte-identical). Mouse: `optionsoverlay.c` maps the window fraction through the same centred region (`vis = (4/3)/portNativeAspect()`, as the D335 front-end pointer) before the logical x, so hover/click/slider-drag/close-box hit-test the drawn layout.

**Red experimental rows.** Rows whose label contains "EXPERIMENTAL" draw label (and ON value) in red ink (selected brighter). F10 only; the front-end PC Options page has its own draw and was left alone.

**Unknown ini keys.** `config.c` counts keys that `applyKV` rejects during `configLoad` (`configUnknownKeyCount/First`); the F10 root page shows an amber "N UNKNOWN INI KEY(S) - SEE LOG" in the title line. Non-blocking, no behaviour change. Legacy migrated keys (e.g. old `Input.AimStyle`) also count, once, until the ini self-cleans on exit.

**Verified (Windows, 1280x720, MouseEnabled=0, `GE_OPTIONSOVERLAY=1`, GE_PCDUMP frame 250, rc=0, no crash log):** root page pillarboxed with the warning (`scratch/f472/root.png`); Gameplay page with red Skip intro / All unlocked rows (`scratch/f472/gp250.png`). **Not verifiable headless:** multi-pad ownership. Maintainer live checks: with 2 pads, P2 Back opens F10, P2 navigates/adjusts/B-backs/Start-closes while P1 Back/Start does nothing; detach the owner mid-menu, the lowest remaining pad takes over; keyboard F10 + pad 1 still drives; solo single-pad unchanged; pillarboxed mouse hover/click lands on the drawn row at 16:9 and 21:9; PS pad in seat 2 shows PS names in the help line when it owns the overlay.
