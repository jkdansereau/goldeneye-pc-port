# In-game PC options menu — design + resume point

Status: **config-system foundation landed (M-35); menu surface not built.**
Pattern follows `docs/dev/AUDIO-PLAN.md` — a plan doc that a later session
executes.

Findings context: D180 (input), D181 (first route-(b) hook). Config API:
`configRegister{Int,UInt,Float,String}` with clamps (`port/include/config.h`).

---

## 1. What PD does, and why it doesn't port directly

PD's `pd_port/port/src/optionsmenu.c` (~2000 lines) hooks port-side handlers
into **PD's own decomp menu tables** (`menudata`/`menuitem` arrays with
`MENUITEMTYPE_SLIDER` etc.). It works because PD's front end is a generic
data-driven list menu.

GE is different. GE's settings UI is the **in-game watch** (`src/game/options.c`):
`draw_watch_game_options_page` / `draw_watch_control_options_page` +
`watch_screenN_navigation` — hand-drawn pages with bespoke nav functions,
not a table. The front-end menus (`front.c`, `initmenus.c`, `mpmenu.c`) are
also bespoke draw/nav pairs, not a reusable list widget.

So there is **no table to inject rows into**. A PC options surface is either
(A) a new hand-built watch page, (B) a new hand-built front-end screen, or
(C) a port-layer overlay drawn outside the game's menu system.

## 2. Recommended approach: (C) port-layer overlay

A self-contained immediate-mode overlay in `port/`, toggled by a hotkey
(e.g. `F10`), drawn as fast3d 2D quads + the game's own `textRender` over
the top of whatever is on screen, with keyboard/mouse nav handled entirely
in the port layer. Reasons:

- **Zero `src/` menu-code edits.** No new bespoke nav function to get wrong
  (the watch-nav code is exactly the D118d / over-scroll family).
- Works in-level *and* in the front end (it's above the game).
- The values it edits are already port-owned `config.c` variables — the
  overlay is just a view over the registered option list.
- Precedent: the port already draws port-owned UI (FPS in the window title,
  F12 screenshot). This is the render-side equivalent.

Cost: an immediate-mode widget layer (label, slider, toggle, dropdown) in
fast3d 2D. ~300–500 lines. The `config.c` registry already gives us
key/min/max/value + a type tag (after M-35) — enough to auto-generate rows.

### Sketch

```
port/src/optionsoverlay.c
  optionsOverlayToggle()        // F10 in video.c event pump
  optionsOverlayHandleInput()   // called from inputComputePad idx 0 when open:
                                //   swallow kbd/mouse from the game, drive the cursor
  optionsOverlayRender(Gfx**)   // called from a fast3d 2D hook after the game DL
```

Registry additions needed in `config.c`:
- `configForEachOption(cb)` — iterate {key, type, ptr, min, max}.
- optional per-option metadata: display label, step, enum-value names,
  "apply live" vs "needs restart". Add a `configRegisterIntEx(...)` variant
  or a side table keyed by dotted key.

Live-apply: most keys already re-read their variable every frame
(`mouseAimSpeed`, `portScreenShakeScale`, `cfgVSync` via `videoSetVSync`).
Ones that need a hook (MSAA, fullscreen) get an `configOnChange` callback
or are flagged "restart".

## 3. Minimum option set for v1

| Row | Key | Type | Notes |
|---|---|---|---|
| VSync | `Video.VSync` | toggle | live (`SDL_GL_SetSwapInterval`) |
| Frame cap | `Video.FpsCap` | int slider 0–360 | live |
| MSAA | `Video.MSAA` | dropdown 1/2/4/8 | needs FBO rebuild — flag "restart" for v1 |
| Texture filter | `Video.TextureFilter` | dropdown nearest/bilinear/3-point | live-ish |
| Mouse aim speed | `Input.MouseAimSpeed` | slider 1–100 | live |
| Mouse turn speed | `Input.MouseTurnSpeed` | slider 1–100 | live |
| Mouse invert Y | `Input.MouseInvertY` | toggle | live |
| Capture mode | `Input.MouseCaptureMode` | toggle | live |
| Screen shake | `Game.ScreenShakeIntensity` | slider 0–3 (×) | live (D181) |
| Screenshot key | — | (F12, documented; rebind = later) | — |

`configSave()` on overlay close.

## 4. Alternative if an in-fiction surface is required later

Route (B): a new front-end screen `constructor_menuXX_pcoptions` +
`menuXX_pcoptions_navigation` in `front.c` under `#ifdef PORT`, reached from
a new "PC OPTIONS" row on the main options menu. Each row calls a
`configGet/Set` shim. This is a genuine route-(b) `src/` edit (menu content
change) → its own Dxx, N64 verbatim under `#else`, opt-in. More faithful,
more surface area, more nav bugs. Defer unless the owner wants it in-fiction.

## 5. Resume checklist

1. Add `configForEachOption` + a label/step/enum side-table to `config.c`.
2. Build the fast3d 2D immediate-mode widget helpers (reuse `gDPFillRectangle`
   + `textRender`; see how `options.c draw_watch_game_options_page` emits
   text for the font/`Gfx**` conventions).
3. `port/src/optionsoverlay.c` state machine + F10 toggle in `video.c`.
4. Route input: when open, `inputComputePad` idx 0 returns a neutral pad and
   forwards nav to the overlay (same "swallow" pattern WI-1 uses for the
   free-cursor click).
5. `configOnChange` hooks for MSAA/fullscreen; everything else is live.
6. Verify: overlay open/close in-level and in the front end; golden dumps
   unaffected with the overlay closed (it draws nothing).
