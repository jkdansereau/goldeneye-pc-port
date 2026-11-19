# D555

<!-- from the old section-F/H table row -->
| D555 | **Crosshair mouse pointer over the F10 / PC Options overlay** — the game's own crosshair sprite is drawn at the mouse, last in the overlay display list (OS cursor hidden; reworked 2026-10-06 from an SDL colour cursor); opt-out `Input.CrosshairCursor` — full `## D555` entry at file tail | DONE 2026-10-06 (port/src/input.c, optionsoverlay.c, tools_pc/make_icon.py; maintainer by-eye check owed) |

## D555: crosshair mouse pointer over the F10 / PC Options overlay (maintainer ask)

**Ask.** The red in-game crosshair should be on top of the F10 / PC Options box
on the main menu, i.e. the mouse pointer there should be the crosshair. Until
now (D519) the game's own crosshair tracked the mouse UNDER the overlay and the
OS arrow was shown on top (D544).

**Fix (port layer only; no src/game change).** One mechanism: while the overlay
is open and the OS cursor is shown, the OS cursor image is a colour cursor
shaped like the project crosshair, hotspot at the centre. The OS composites
the cursor above the game frame, so it is always over the overlay box.
- Art: `tools_pc/make_icon.py` `_cursor()` / `make_cursor_header()` emit
  `port/fast3d/ge007_cursor.h` (32x32 and 48x48 RGBA, transparent background,
  same proportions as `_crosshair`, red #c62828 with a 1px dark outline;
  row-braced arrays like `ge007_icon.h`; deterministic, run twice = same bytes).
- `port/src/input.c`: `crosshairCursorBuild()` packs the pixels against explicit
  masks, shifting each channel into its lane before masking (the D550 bug), picks
  48 px when the drawable is >= 1.5x the window size (high DPI), else 32.
  `crosshairCursorApply()` does SDL_SetCursor(crosshair) / SDL_SetCursor(
  SDL_GetDefaultCursor()). Both run from `inputApplyMouseRequests()`, the
  host/event thread (videoPumpEvents), next to the SDL_ShowCursor application
  (D287) -- never the game thread. Condition: `Input.CrosshairCursor` on, the
  applied cursor state is "shown", and `optionsOverlayIsOpen()`; otherwise the
  default arrow is restored (overlay closed, focus lost). The cursor object is
  built lazily once and kept for the process lifetime.
- The game's under-overlay crosshair (D519, `overlayFrontEndCrosshair`) is
  unchanged. It tracks the same window position, so outside the box it sits
  directly beneath the pointer and the pointer covers it; whether it visibly
  doubles (slight lag halo) needs a by-eye check. If so, the follow-up is to
  skip `overlayFrontEndCrosshair()` while the custom cursor is active.
- Setting: `Input.CrosshairCursor` (default 1), F10 Mouse page row "Crosshair
  pointer" (toggle), tip "Shows the red crosshair as the mouse pointer in this
  menu", in the reset-defaults table.

**Verification.** Build only (ntsc-final rc=0); visual check owed to the
maintainer.

**Reworked 2026-10-06 (maintainer feedback: the front-end crosshair showed
underneath as a duplicate, and the SDL cursor was neither the game's graphic nor
its size).** The custom SDL colour cursor is REMOVED (`crosshairCursorBuild/Apply`,
`port/fast3d/ge007_cursor.h`, `make_icon.py` `_cursor`/`make_cursor_header`; the
other make_icon outputs are unchanged). New mechanism, port layer only:
- `optionsoverlay.c` `drawCrosshairPointer()` runs last in `overlayEmitLocked()`
  (after the card, outside the centred-aspect block) and draws the game's own
  sprite `crosshairimage` (image_bank.c; 32x32, loaded from the user's ROM
  at runtime -- nothing baked into the tree) with the same
  `texSelect` + `display_image_at_position` as `frontDrawCursor` (white, alpha
  220), at the OS mouse position mapped through `gfx_get_ui_screen_rect`.
- Position/size, FRONT END: the first cut parked `cursor_h_pos/cursor_v_pos` at
  -1000, but the game's own front-end tick clamps the cursor back into bounds
  before `frontDrawCursor`, so a copy appeared at the clamp corner (found in a
  headless capture). Park removed. Now D519's `overlayFrontEndCrosshair()` keeps
  the game cursor tracking the mouse (option on or off) and
  `drawCrosshairPointer()` draws the pointer exactly over it: `input.c`
  `inputFrontEndCursorUiFrac()` is the inverse of D519's window->canvas mapping
  (x = (cursor_h_pos - screenleft)/screenwidth, then the D335 pillarbox
  x*vis + (1-vis)/2 when native widescreen; y likewise), giving fractions of the
  UI screen rect; canvas px = frac * (W,H); half-extent = (image->width/2)/viGetX()
  * W * vis and (image->height/2)/viGetY() * H, i.e. frontDrawCursor's size. The
  sprite lands on the one underneath, so one crosshair is visible.
- IN LEVEL (no front-end cursor): position from `SDL_GetMouseState` through
  `gfx_get_ui_screen_rect`; half-extent `16/ovScale()` x `(4/3)/portNativeAspect()`
  (gunDrawSight's 16.0 and widescreen x fix). It does NOT follow the Video.Crosshair
  colour/size/opacity options (UI pointer: plain sprite, front-end opacity).
- OS cursor: `inputApplyMouseRequests()` (host thread) hides it while the option is
  on and the overlay is open; restores on close / option off.
- Option off: previous behaviour (OS arrow over the overlay, D519 crosshair under it).
- Also: `Game.NoHitFlash` tip is now "Hides the white flash when you take damage".
Headless captures (1280x720, front end, overlay open; frame 1100): AspectMode=1, AspectMode=1 + NativeWidescreen=1 (the front end stays 4:3 pillarboxed), AspectMode=0 (stretched, the pointer stretches with the game, as its own cursor does): exactly one crosshair each. Live mouse feel owed to the maintainer.
