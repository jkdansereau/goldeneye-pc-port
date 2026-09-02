# M-35 — Native-PC input + options-menu QoL review

Branch: `qol/native-pc-input-menu` (cut from `def0e0ac`). Port-layer only
except one documented route-(b) `#ifdef PORT` line in `src/fr.c` (D181).
Every new behaviour is config-gated with the default reproducing prior
behaviour; `tools_pc/golden/` headless captures are unaffected at defaults
(no mouse in a headless run; `Game.ScreenShakeIntensity` default 1.0 is a
no-op multiply).

Findings: **D180** (input pass), **D181** (screen-shake hook). See
`docs/dev/findings.md` §F.

---

## WI-1 — In-game mouse: Quake-style click-to-lock

**Status: landed, feel-check owed.**

### What changed

| File:area | Change |
|---|---|
| `port/src/input.c` — new statics `mouseCaptureMode` / `captureArmed` / `windowFocused` | Capture-mode state. `mouseCaptureMode` registered as `Input.MouseCaptureMode` (0/1, **default 0**). |
| `port/src/input.c` `inputInit` | In capture mode, start **un**grabbed (`SDL_SetRelativeMouseMode(FALSE)`), wait for a click. Legacy mode unchanged (grab on start). |
| `port/src/input.c` `applyGrab()` / `reconcileGrab(menuMode)` | `applyGrab` is the old `inputSetMouseGrab` body (SDL relative-mode toggle + delta drain). `reconcileGrab` computes the wanted grab state: legacy → `windowFocused`; capture → `captureArmed && windowFocused && !menuMode`. Called once per controller-0 poll and from every notify hook. |
| `port/src/input.c` `inputSetMouseGrab(on)` | Now just records `windowFocused` and calls `reconcileGrab` — so focus loss/gain works in both modes without special-casing. |
| `port/src/input.c` `inputNotifyClick()` | Capture mode + focused → `captureArmed = 1` + regrab. No-op otherwise (game sees the click). |
| `port/src/input.c` `inputReleaseCapture()` | Capture mode + grabbed → `captureArmed = 0`, release, return 1 (caller swallows the key). Returns 0 otherwise (ESC falls through to the N64 B button, D145). |
| `port/src/input.c` `inputComputePad` idx 0 | Calls `reconcileGrab(menuMode)`; when capture-mode + free + in a stage, zeroes the local `mb` so mouse buttons never reach the game (no phantom fire from the re-lock click). |
| `port/src/video.c` event pump | `SDL_MOUSEBUTTONDOWN` → `inputNotifyClick()`. `SDLK_ESCAPE` keydown → `inputReleaseCapture()` (before the existing fall-through). |
| `port/include/input.h` | Prototypes for the three new hooks. |

### Config keys

| Key | Range | Default | Effect |
|---|---|---|---|
| `Input.MouseCaptureMode` | 0–1 | **0** | 0 = legacy (cursor grabbed whenever the window is focused). 1 = click-to-lock: free cursor until you click the window; ESC / focus-loss / opening a menu frees it; entering a stage while "armed" re-locks. |
| `Input.MouseAimSpeed` | 1–500 | **16** (was 25) | Aim-mode (RMB / LShift) sensitivity %. B3: still overshot at 25. |

Controller input is completely independent of all of the above — the pad
branch in `inputComputePad` is untouched, and `reconcileGrab` only ever
toggles `SDL_SetRelativeMouseMode`.

### D166 / hipfire-pitch interaction

The D166 hipfire pitch pulse is unchanged. It only runs when the cursor is
grabbed (mouse deltas are zero while free), so click-to-lock does not
perturb it. The M-24 aim-band (`61 + gain` into the `60 + aimBand` ceiling)
is unchanged; only the `mouseAimSpeed` scalar feeding `gx`/`gy` moved.

### Owed feel-checks (playtest checklist items 1–4)

Headless runs cannot move the mouse, so the capture ergonomics, the new
`MouseAimSpeed = 16` default, and the D166 pulse consistency under the new
model are all inferred, not observed.

---

## WI-2 — Menu pointer: absolute cursor tracking

**Status: landed for capture mode, feel-check owed. Route (a), port-only.**

### What changed

`port/src/input.c`, the `menuMode` pointer branch (the D165/D169
P-controller). Added an **absolute-position** target source: when
`mouseCaptureMode && !mouseGrabbed` (i.e. a real free OS cursor over a
front-end screen), read `SDL_GetMouseState` + `SDL_GetWindowSize` of the
mouse-focus window, normalise to `[0,1]²`, and map straight onto the live
virtual front-end rect (`loH..hiH × loV..hiV`, already derived from
`getPlayer_c_screen*` for D169). That becomes `menuTgt{H,V}` directly
(no accumulation, no drift). The P-controller/estimator below is unchanged
and simply chases the target, so the game's `cursor_h_pos/v_pos` lands
exactly where the OS pointer is.

The relative-delta path (legacy `MenuPointerMode`, or capture mode while
grabbed) is **unchanged** — `haveAbs` gates only the new assignment.

### Why this is enough for 1:1

The D169 fix already made the clamp rect track the real 440×330 field. The
only thing missing for true 1:1 was a non-drifting target: a free OS cursor
*is* an absolute device, so mapping its window fraction onto the rect is
exact. No route-(b) hook needed.

### Keyboard nav parity

Untouched — the menu branch only ever adds to `sx/sy`; D-pad / stick keys
still feed the same axes. The D118d `front.c` latent sibling was **not**
touched (no evidence the absolute path exposes it; the stick values emitted
are small P-controller corrections, never a slammed ±80).

### Caveat

Absolute tracking is only wired for `MouseCaptureMode = 1`. In legacy mode
(default) the menu still uses the D165/D169 relative estimator. If
click-to-lock becomes the default later, WI-2 is 1:1 for free; until then
menu 1:1 requires `Input.MouseCaptureMode = 1`.

---

## WI-3 — In-game video/options menu

**Status: config-system foundation landed; menu surface = design doc only.**
See `docs/dev/OPTIONS-MENU-PLAN.md`.

### Landed

- `configRegisterFloat` / `configRegisterUInt` + clamps (`port/src/config.c`,
  `port/include/config.h`). `configSave` rewritten to emit all four option
  kinds section-grouped in one pass.
- `Game.ScreenShakeIntensity` (0.0–10.0, default 1.0) — the first
  route-(b) hook, D181, proving the pattern the menu will lean on.

### Not landed (budget) — precise resume point in the design doc

- The menu tables / handlers themselves. GE's menu system (`front.c` /
  `initmenus.c` / `mainmenu.c`) differs enough from PD's `optionsmenu.c`
  that it is an adaptation, not a copy. Design doc covers the entry point,
  the handler shape, and the minimum option set.

---

## Verification evidence

| Check | Result |
|---|---|
| `./build-pc.sh ntsc-final` | green, 241/241 |
| `-level_09` framediff (200/320/440) | 3/3 within threshold; nonclear Δ 0.00–0.01pp (91.6%); phash 0/1/5 = documented D117 intro-pan noise |
| `GE_STARTMENU=7` boot | crash-free, no `ge007.crash.log` |
| golden dumps at defaults | unaffected (no headless mouse; shake scale 1.0 = no-op) |
| linkcheck | see PR body |
