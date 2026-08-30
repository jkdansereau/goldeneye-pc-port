# M-24 — input rework + playtest QoL (review sheet)

Session M-24, 2026-08-30. Three commits on `master`:

| Commit | Title |
|---|---|
| `f3ec5170` | mouse-look rework (D118b/c) + real `ge007.ini` parser |
| `65ed0315` | F12 screenshot + mouse frees on focus loss |
| `33506aee` | gamepad hotplug, wheel weapon-cycle, title FPS |

**Scope guarantee:** every change is in `port/` (shim layer). **No `src/`
file was touched** — no game-logic / ABI / asset-format change. Nothing
here is `#ifdef PORT` in game code. The N64 build is untouched.

Files changed: `port/src/input.c`, `port/src/video.c`, `port/src/config.c`,
`port/include/input.h`, plus docs (`HANDOFF.md`, `LEVEL-PLAYTEST.md`,
`PORT-LEARNINGS.md`, `PCPortResearch.md` §F D118).

---

## 1. Mouse-look rework (`port/src/input.c`) — fixes D118b, D118c

### The bug

The old bridge always converted mouse-Y to digital C-up/C-down presses.
That was wrong two ways, because GE's aim model is **mode-dependent**
(read from `src/game/bondview2.c` `bondviewProcessInput` / `MoveData`):

| Mode | Yaw source | Pitch source | What C-up/C-down do |
|---|---|---|---|
| Hipfire (`!insightaimmode`) | analog stick-X ("natural turn") | **digital C-up/C-down only** (stick-Y is move fwd/back, no pitch) | look up / down |
| Aim mode (R / RMB held) | analog stick past ±60 → `(stick-60)/10` | analog stick past ±60 | **crouch / lean / zoom** (`bondview2.c:5340,5351`) — *not* aim |

- **D118c** (aim + mouse-down → crouch): emitting C-down for "look down"
  while aiming triggered `moveData.crouchDown`.
- **D118b** (inverted Y): GE's native pitch is inverted —
  `U_CBUTTONS → speedVertaDown` (`bondview2.c:5272-5279`), i.e. C-up looks
  *down*. Mapping mouse-down → C-down gave mouse-down = look up.

### The fix

`inputComputePad()` now branches on whether the aim button is held (our own
RMB/LShift state — the exact signal for the default hold-to-aim scheme):

- **Aim mode:** push `stick_x` / `stick_y` into the 61..80 band,
  proportional to the per-poll mouse delta (`× MouseAimSpeed/100 × 4`,
  clamped). **Emit no C-buttons.** → crouch bug gone; yaw and pitch now
  behave identically (both analog).
- **Hipfire:** yaw on `stick_x += dx × MouseTurnSpeed/100 × 6`; pitch =
  digital C-up/C-down on `|dy| ≥ 1.5` px/poll.
- "Mouse-down looks down" is the default (accounts for GE's inversion);
  `MouseInvertY` flips it.
- Per-poll deltas, **no accumulator** (mouse = displacement device, GE aim
  = rate device; consume the whole delta each poll, stop moving → stick
  releases immediately).

### Known residual — D118a (lower priority, not a regression)

In **hipfire only**, yaw (analog) and pitch (digital C-button) still feel
different. Aim mode — where GE's precise vertical aiming actually happens —
is now fully analog and consistent. A fully-analog hipfire pitch would need
an `#ifdef PORT` hook in `bondview.c` (would also be needed for
toggle-aim control schemes, where "RMB held" ≠ `insightaimmode`). Deferred.

### Review risk

Low. Pure output-shaping of the N64 pad struct. Worst case is bad *feel*,
tunable at runtime (below). Cannot crash or desync — same `OSContPad`
contract as before.

---

## 2. `ge007.ini` parser (`port/src/config.c`) — was a stub

`configLoad()` / `configSave()` only logged `TODO` before. The registered
options (`configRegisterInt`) therefore could not be changed without a
recompile.

Now: parses `$S/ge007.ini` — `[Section]` blocks, `Key = value`, `#` / `;`
comments; clamps ints to their registered `min`/`max`; writes a defaults
file on first run; rewrites on clean exit (`main.c` already calls
`configLoad()` at startup and `configSave()` on the unreachable-in-practice
shutdown path). Dotted keys (`Input.MouseAimSpeed`) group into
`[Input]` on disk.

**Live-tunable `[Input]` knobs** (edit with the game closed):

| Key | Default | Meaning |
|---|---|---|
| `MouseEnabled` | 1 | mouse-look on/off |
| `MouseAimSpeed` | 50 | aim-mode (RMB) sensitivity, percent |
| `MouseTurnSpeed` | 100 | hipfire yaw sensitivity, percent (new; split from AimSpeed) |
| `MouseInvertY` | 0 | 1 = mouse-down looks up |

Other sections already register options (`[Audio] BufferSize`,
`QueueLimit`) and now round-trip too.

**Implementation notes for review:** no `<ctype.h>` / `atoi` — the project's
`src/str.c` defines `isspace`, which collided with libmsvcrt at link time
once `config.c` referenced the libc one, so `config.c` carries tiny local
`isws` / `lc` / `parseInt` helpers. Options registered *after* `configLoad()`
(none today — all are `PD_CONSTRUCTOR`) would miss the first-run save.

### Review risk

Low-medium. New file I/O in `port/`. Failure modes are contained: missing
file → write defaults; unparseable line → skipped; unknown key → logged and
ignored. No effect on any code path that does not read a registered option.

---

## 3. Playtest QoL (`port/src/video.c` + `port/src/input.c`)

All wired through the existing host-thread SDL event pump
(`videoPumpEvents`).

| Feature | How | Notes |
|---|---|---|
| **F12 screenshot** | keydown sets a flag; consumed in `videoEndFrame` (render thread, GL context current) via the existing `gfx_opengl_dump_bound_fbo` | → `ppm/shot_NNN.ppm`; view with `tools_pc/ppm2bmp.py`. Same mechanism as `GE_PCDUMP`. |
| **Alt-tab frees the mouse** | `SDL_WINDOWEVENT_FOCUS_LOST/GAINED` → `inputSetMouseGrab(0/1)` | New `inputSetMouseGrab()` in `input.c`: toggles SDL relative-mouse mode (respecting `MouseEnabled`), zeroes the aim delta, and gates `inputUpdate()` mouse reads on a `mouseGrabbed` flag so a released cursor can't drift the aim. |
| **Mouse wheel = weapon cycle** | `SDL_MOUSEWHEEL` → `inputPostWheel()` queues a 2-poll `GE_CONT_A` press | GE's default scheme cycles weapon forward on a bare A edge (`invButtons = A_BUTTON`, `bondview2.c:5162` → `weaponForwardOffset`, `:5326`). Both wheel directions cycle **forward** — no clean backward input without the A+fire combo. Same overload the keyboard already has (Space/Z/E = A). |
| **Gamepad hotplug** | `SDL_CONTROLLERDEVICEADDED/REMOVED` → `inputRescanPads()` (close all + re-open) | Caveat: the game latches `inputConnectedMask()` at `osContInit` (boot). A pad added *after* boot still works for play (it merges into controller 0 — `inputComputePad(0)` always checks `pads[0]`) but won't appear as a separate controller channel. Plug it before launch for genuine multi-pad. |
| **Window title shows FPS** | `videoPumpEvents` calls `wmAPI->set_window_title("GoldenEye 007  -  N fps")` ~1 Hz from `vidAvgFPS` | Cosmetic. |

Alt-Enter fullscreen toggle was **already present** (`gfx_sdl2.cpp:299`) —
not added here, just documented.

### Review risk

Low. Screenshot reuses a proven path. Focus/grab and hotplug are SDL window
ops on the thread that created the window. Wheel→A is opt-in physical input
identical in effect to tapping the existing keyboard A key.

---

## Verification done

- Build green (`./build-pc.sh ntsc-final`), no new warnings in touched files.
- `-level_09` (BUNKER1): crash-free to frame 900 (30–35 s window), **91.6%
  pixel coverage — unregressed** vs the committed baseline, checked after
  each of the 3 commits (3/3, 2/2, 2/2 runs; the one early NO-FRAME was the
  known D117/D134 machine-load flakiness, cleared on retry).
- `-level_20` (Silo): crash-free.
- `ge007.ini`: written on first run, re-read with **no unknown-key
  warnings**, values round-trip.
- Input is a headless-agent blind spot — **live feel of the mouse rework,
  the wheel cycle, and a real gamepad are UNVERIFIED**; that's the WS6
  playtest's job. See `docs/LEVEL-PLAYTEST.md`.

## What a reviewer should check by playing

1. Mouse aim in **aim mode (hold RMB)** feels smooth and 1:1-ish; no crouch
   when you pull the mouse down to look down (D118c).
2. Mouse-Y not inverted at `MouseInvertY = 0` (D118b); flipping the ini
   value inverts it.
3. Editing `MouseAimSpeed` / `MouseTurnSpeed` in `data/ge007.ini` (game
   closed) actually changes sensitivity next launch.
4. Mouse wheel swaps weapons; F12 drops a `ppm/shot_*.ppm`; alt-tab frees
   the cursor and returns cleanly; a gamepad plugged in before launch works.

## Still TODO (not started)

Key rebinding, in-render on-screen FPS overlay, in-game options menu,
backward weapon cycle, the `bondview.c` analog-aim hook (D118a + toggle-aim).
