# M-35 QoL playtest checklist

Everything on this branch is config-gated to prior behaviour, so the
**first** thing to confirm is that nothing changed with a default
`ge007.ini`. Then flip the new keys and check the feel.

Edit `data/ge007.ini` while the game is closed.

---

## 0. Regression (default config) — do first

- [ ] Delete `data/ge007.ini`, launch, quit. It rewrites with new keys:
      `[Input] MouseCaptureMode = 0`, `[Game] ScreenShakeIntensity = 1`.
      Existing keys keep their values.
- [ ] Play a level normally (mouse grabbed on focus, as before). Mouse look,
      aim (RMB), menus — all exactly as the last build. **No behaviour change
      expected at defaults.**
- [ ] Alt-tab away and back — cursor frees / re-grabs (unchanged).

## 1. Click-to-lock capture — `Input.MouseCaptureMode = 1`

- [ ] Launch. At the main menu the **OS cursor is visible and free**; moving
      the mouse moves the game crosshair 1:1 (see item 3).
- [ ] Click in the window during a level → cursor hides, mouse-look engages.
- [ ] Press **ESC** during a level → cursor reappears, mouse-look stops,
      you can move the pointer off the window. (ESC does *not* quit.)
- [ ] Click back in → re-locks. First click only re-locks — it must **not**
      fire the weapon.
- [ ] Open the pause/watch (Start), then a front-end menu → cursor frees
      automatically. Return to the stage → re-locks without a click.
- [ ] Alt-tab out mid-level → cursor frees; alt-tab back → stays free until
      you click (Quake behaviour).
- **Correct feel:** exactly like Quake/Nightdive remasters — click to play,
  ESC to get your cursor back.
- **Tune:** none — it's a mode toggle.

## 2. Aim sensitivity — B3

- [ ] With `Input.MouseCaptureMode` either value, hold RMB (aim mode) and
      make small aiming corrections. Default is now `MouseAimSpeed = 16`
      (was 25).
- **Correct feel:** you can settle the sight on a distant guard's head
  without overshooting; a fast flick still crosses the screen.
- **Tune:** `Input.MouseAimSpeed` (1–100+). Up = faster. If hipfire turning
  now feels faster than aiming, raise `MouseAimSpeed` or lower
  `Input.MouseTurnSpeed` (default 100). `Input.AimBand` (5–40, default 20)
  widens the usable stick range past the aim gate — raise it if aim feels
  capped/notchy at the extremes.
- [ ] Check the D166 hipfire pitch (mouse up/down without RMB): still
      proportional taps, consistent with the yaw. Tune
      `Input.HipfirePitchSpeed` (default 100).

## 3. Menu pointer 1:1 — needs `Input.MouseCaptureMode = 1`

- [ ] On the mission-select grid, move the OS cursor to each of the four
      **corner** level tiles — the game crosshair must reach all of them
      (this is the D169 case; absolute mapping should make it exact now).
- [ ] File-select, mode-select, difficulty-select, watch menu — crosshair
      tracks the OS cursor with no drift, no lag, no acceleration.
- [ ] Keyboard nav (arrows / stick) still works on every screen.
- **Correct feel:** the crosshair sits exactly under the OS pointer, like a
  normal PC menu.
- **Tune:** `Input.MenuPointerSpeed` (default 100) still scales the legacy
  relative path; with capture mode the absolute path ignores it. If tracking
  feels off, check the window isn't a weird aspect ratio (the map assumes the
  render fills the window).
- **Note:** in legacy mode (`MouseCaptureMode = 0`) the menu still uses the
  D165/D169 relative estimator — 1:1 menu tracking requires capture mode.

## 4. Screen shake — `Game.ScreenShakeIntensity`

- [ ] Default `1` — explosions shake the screen exactly as before.
- [ ] Set `0` — no shake on explosions / big hits.
- [ ] Set `2`–`3` — exaggerated.
- **Correct:** `1` is indistinguishable from the current build.

---

## Single most important thing to test first

**Item 0** — confirm a default `ge007.ini` produces a build that plays
identically to `def0e0ac`. If that holds, everything else is opt-in polish.

## Known not-done (parked, see OPTIONS-MENU-PLAN.md)

- No in-game options **menu** yet — only the config file + the expanded
  `config.c` API and the `Game.ScreenShakeIntensity` hook. The menu is a
  port-layer overlay design, documented, not built.
