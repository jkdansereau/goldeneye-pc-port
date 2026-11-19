# D594

<!-- from the old section-F/H table row -->
| D594 | **arm64 macOS — partial input: mouse-look (relative motion) + F10 work, but the mouse *buttons* (fire / aim) do not, and the cursor "exits" the window. `GE_KEYLOG` **disproved** the cross-thread-poll prime suspect (WASD + LMB reach both the main-thread pump and the scheduler-thread poll); the real gap was the `actHeld` mouse gate on `current_menu == RUN_STAGE` (a forced `-level_NN` start leaves `current_menu == INVALID (-1)`, so the mouse was never read) + the macOS `reconcileGrab` `windowFocused` gate. Fixed by matching PD's input model (in-stage = RUN_STAGE *or* INVALID; macOS grab = PD's MLOCK_ON, no focus gate).** — full `## D594` entry at file tail | **FIXED 2026-10-08** (macos branch; `actHeld` menu gate + macOS `reconcileGrab` focus gate → PD MLOCK_ON; LMB fire verified on the Mac; unblocked D595) |

## D594: arm64 macOS — partial input: mouse-look (relative motion) and F10 work, but mouse *buttons* (fire / aim) do not; the cursor "exits" the window — FIXED 2026-10-08 (macos branch; observed during the D592 live-look; root-caused + fixed via the `GE_KEYLOG` probe; unblocked D595)

### Symptom
While doing the D592 live Mac look (a **native** macOS Terminal.app launch of
`ge007.aarch64 -level_33`), the Dam scene renders correctly, but the game is
**look-only**:
- **Mouse-look (relative motion) WORKS** — the camera turns with mouse
  movement.
- **F10 WORKS** — the F10 menu (pause/options) responds.
- **WASD / arrow-key movement does NOT work** — the player cannot move.
- **Mouse buttons (left/right click) do NOT work** — no fire / no aim-activate.
- The cursor appears to **"exit" the window** (a relative-mouse-mode capture
  issue).

### Known (2026-10-08)
- **Not an ssh-launch artifact:** it reproduces from a *native* macOS
  Terminal.app launch (a proper GUI session), so it is not the
  "ssh-launched window has no key focus" case.
- The input code (`port/src/input.c`) is **standard SDL2** (keyboard + mouse +
  gamecontroller via `SDL_INIT_GAMECONTROLLER`); the only `PLATFORM_MACOS`
  code in `port/src/{input,system,video}.c` is image-base / exe-dir / time —
  **nothing input-specific.**
- Likely suspects (root-cause owed): (a) the SDL **relative-mouse-mode**
  interaction on macOS — `input.c:1589-1590` explicitly sets
  `SDL_HINT_MOUSE_RELATIVE_MODE_WARP=0` + `RELATIVE_SYSTEM_SCALE=0`, so the
  cursor is not warped back to centre and drifts out ("exits"); (b) whether
  SDL **keyboard** events are delivered at all (F10 works but WASD doesn't is
  odd for a plain KEYDOWN stream — suggests either a per-key binding/mapping
  gap or a focus subtlety); (c) mouse-*button* events being suppressed in
  relative mode.

### Root-cause candidate (structural, 2026-10-08)
The game's keyboard/mouse **poll** (`SDL_GetKeyboardState` / `SDL_GetMouseState`
in `inputComputePadSlot`, `input.c`) runs on the **scheduler thread**
(`osContStartReadData → inputComputePad`, `input.c:767`). On macOS the
render-thread SDL pump is **disabled** (`gfx_sdl2.cpp`: AppKit requires polling
on the main thread, so `gfx_sdl_handle_events` returns immediately) and all
SDL event delivery is **main-thread-only** — the main-thread pump
(`videoPumpEvents`, `video.c`) handles the *events* (F10, ESC, window, mouse-
button for click-to-lock) but **not** the held-key poll. So the scheduler-
thread `SDL_GetKeyboardState`/`SDL_GetMouseState` may read stale/zero state
that the main-thread pump is the only thing updating — which would explain why
**F10 (an event on the main thread) works but WASD / mouse buttons (a poll on
the scheduler thread) do not**, while relative mouse-look (a different, shared
mechanism) still works. Prime suspect; `GE_KEYLOG` (below) confirms.

### Root-cause (CONFIRMED 2026-10-08) — the cross-thread poll suspect is DISPROVEN; it was a menu-state gate

The `GE_KEYLOG` probe (two Mac runs, `GE_KEYLOG=1 … -level_33`) **disproved** the
structural cross-thread-poll suspect: WASD `SDL_KEYDOWN` **and** LMB
`MOUSEBUTTONDOWN` both reach **both** the main-thread pump (`E main …`) and the
scheduler-thread poll (`P sched A/W/S/D/LMB …`). So the poll *does* see the
state. The "WASD does not work" line of the D592 live-look **did not
reproduce** — the WASD poll reads fine on macOS.

The reproducible defect is the **mouse *button*** actions (fire, aim). Root
cause: `actHeld()` (`port/src/input.c`) gates the mouse-button read on
`current_menu == GE_MENU_RUN_STAGE` (**11**). But a forced-level start
(`-level_NN`, the standard capture/test path) leaves `current_menu` at
`MENU_INVALID` (**-1**) and never transitions it to `MENU_RUN_STAGE` — so the
gate was false and `SDL_GetMouseState` was never consulted, silently disabling
every mouse-driven action. PD has **no such gate** (it reads the fire/aim
buttons directly), which is why PD fires fine on the same trackpad. A second,
secondary macOS gap: `reconcileGrab()` gated the cursor capture on
`windowFocused`, but `FOCUS_GAINED`/`FOCUS_LOST` are unreliable on macOS
(AppKit key-window; absent over ssh), so the click-to-lock could stay
un-armed and the relative-look/grab would release (the "cursor exits" symptom).

### Fix (2026-10-08, macos branch) — match PD's input model
1. **`actHeld()`**: the in-stage test now matches the file-wide `menuMode`
   semantics — `current_menu == RUN_STAGE || current_menu == INVALID` (not a
   strict `== RUN_STAGE`). A real front-end menu is a specific `MENU_` id
   (`menuMode` 1) and still stays a UI control; only the in-stage states
   (RUN_STAGE or the forced-start INVALID) treat the mouse as a gameplay
   button. This is exactly the PD model.
2. **macOS grab = PD's MLOCK_ON**: on `PLATFORM_MACOS`, `captureArmed` defaults
   to 1 (armed by default) and `reconcileGrab()` drops the `windowFocused`
   gate (wants the grab whenever in-stage and no menu/aim-hold). The mouse is
   locked in-stage like PD's `MLOCK_ON`; menus and aim-holds still free the
   cursor. Non-macOS platforms keep the original click-to-lock unchanged.

**Consequence:** once fire worked, it immediately exposed a *separate* arm64-only
crash in the weapon-fire → AI hit-reaction animation path — filed as **D595**.

### Probe (`GE_KEYLOG`) — IMPLEMENTED 2026-10-08 (port layer, two sites)
Two env-gated, default-off loggers write to `./ge_keylog.txt` (validated on
Windows): `geKeyLogEvent` in `video.c` logs each `SDL_KEYDOWN`/`KEYUP`/
`SDL_MOUSEBUTTONDOWN`/`UP` on the **main thread** (`E main …`); `geKeyLogPoll`
in `input.c` logs the scheduler-thread **poll**'s A/W/S/D + LMB/RMB on change
(`P sched …`). Run it natively on the Mac (`GE_KEYLOG=1 ./build-mac/ge007.aarch64
-level_33`), press WASD + click, then read the log:
- `E main KEYDOWN` for W/A/S/D **present** but `P sched A/W/S/D` stays 0 → the
cross-thread poll is the bug (scheduler thread never sees the main-thread
  keyboard/mouse state). Fix: read the poll on the main thread (or pass the
  state across), port-layer only.
- No `E main KEYDOWN` for W/A/S/D (but F10's arrives) → a per-key
delivery/focus issue (the window isn't the AppKit key window for regular keys).
- `P sched` shows the keys but nothing moves → the keys→stick mapping is the gap.
- `GE_KEYLOG` is registered in [`GE-PROBE-CATALOG.md`](GE-PROBE-CATALOG.md) as
  **STRIP before main** (a one-off D594 diagnostic, no shipping value).
- Cross-tag D592 (the D592 live look was mouse-look-only *because of* D594;
  D592 is still closed on its own byte-level evidence).

### Status
**FIXED 2026-10-08 (macos branch).** Root-caused via `GE_KEYLOG` (cross-thread
poll disproven; the real gap was the `actHeld` `current_menu` menu gate + the
macOS `reconcileGrab` focus gate) and fixed by matching PD's input model
(see Fix above). Verified on the Mac: LMB fire now reaches the input layer
(`GE_CONT_G` set), relative-look + F10 unchanged, no crash; and the
maintainer's **2026-10-08 live pass (Launch E)** confirmed mouse, keyboard
and pad all work as expected on the Mac, matching Win/Linux. Unblocked **D595**
(the arm64 AI-anim crash that firing then exposed — also fixed). Not blocking
D592 (closed) or the D591 byte-order fix (verified).
