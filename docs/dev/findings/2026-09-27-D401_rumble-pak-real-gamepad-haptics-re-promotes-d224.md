# D401

<!-- from the old section-F/H table row -->
| D401 | **Rumble Pak → real gamepad haptics (re-promotes D224/M-87): `CONT_CARD_ON` gate + `osPfs*`/`osMotor*` shims rewired to `SDL_GameControllerRumble`, single global `Rumble strength` slider on `Input.RumbleScale` (2026-09-27/28).** Full `## D401` entry at file tail. | **CLOSED (2026-09-28).** Rumble shipped in v0.4.0 (D401); Steam Deck real-pad validation: works, user confirmed for release. |

## D401 — Rumble Pak → real gamepad haptics: `CONT_CARD_ON` gate + `osPfs*`/`osMotor*` shims rewired to `SDL_GameControllerRumble`, global `Input.RumbleScale` slider (2026-09-27/28).

Implements **D224 (M-87)** per `docs/dev/RUMBLE-PLAN.md` — the N64 Rumble
Pak state machine (`src/joy.c` `joyRumblePakInit`/`joyRumblePakTick`/Start/Stop,
call sites `gunfire.c:3284/3288`, `bondview2.c:9722/9726`, `lv.c:1688`,
`sched.c:272`) now drives real SDL gamepad haptics. **Port layer only: zero
`src/` edits**, so rule 2 / the ABI exception are not implicated.

**The two M-87 addendum blockers** (both `port/src/libultra.c`):

1. `contSnapshotFromKeyboard` left `g_contStatus[i].status = 0`, so
   `joyRumblePakInit` (`src/joy.c:178` — requires `status & CONT_CARD_ON`) never
   ran and `joyRumblePak` was never populated. Now
   `status = (connected && inputRumbleSupported(i)) ? CONT_CARD_ON : 0` —
   gated on real capability so non-rumble pads never enter the rumble path
   (cleaner than PD's unconditional bit; `joyRumblePakInit` is the real gate).
   `type` stays `CONT_TYPE_NORMAL` (includes `CONT_JOYPORT`; PD's
   `CONT_ABSOLUTE` would have broken the init gate).
2. `osPfsInit` stubbed `PFS_ERR_NOPACK`, so `osMotorInit` was never
   attempted (`src/joy.c:186-190` proceeds only on `PFS_ERR_ID_FATAL` or
   `PFS_ERR_DEVICE`). Now returns `PFS_ERR_DEVICE` ("Rumble Pak present, not a
   Memory Pak") for rumble-capable channels, `PFS_ERR_NOPACK` otherwise.

**`port/src/input.c`** (flat statics, GE style — not PD's `padsCfg[]`):
`padRumbleOn[MAX_PADS]` + a single global `gRumbleScale` (default 0.5) —
the per-pad `padRumbleScale[]` array was superseded 2026-09-28 by the
global slider (see "Menu row" below). Detection in
`inputOpenPads`: `SDL_GameControllerHasRumble` on SDL ≥ 2.0.18, else
`SDL_JoystickIsHaptic` with the known-type Windows no-haptic fallback; zeroed
on close in `inputDestroy`/`inputRescanPads`. PS4/PS5 HIDAPI rumble hints in
`inputInit`. New API: `inputRumbleSupported(idx)` (bounds-checked against
`MAX_PADS` — JPN `_HW_VERSION_1` has `MAXCONTROLLERS == 6`, so out-of-range
channels report unsupported and stay silent), `inputRumble(idx, strength,
time)` (no-op on `gRumbleScale <= 0`; scales strength, `*65535` / `*1000`
units, `SDL_GameControllerRumble`; `(0,0)` stops), global
`inputRumbleGetScale(void)`/`inputRumbleSetScale(f32)`. Config: a single
`Input.RumbleScale` (range 0..1, default 0.5) registered in
`inputConfigInit`; the four per-player `Input.Player%d.RumbleScale` keys
were dropped in the same-day menu-shape decision (stale keys in an
existing ini log four `unknown key` `LOG_NOTE` lines and self-clean on the
next `configSave`). `keyboard`/mouse-only play stays silent: no pad open
⇒ `pads[idx]` NULL ⇒ `inputRumble` no-ops.

**`port/src/libultra.c`** shims: `osPfsInit` as above; `osPfsIsPlug` sets its
pattern bits for the capable channels over `MAXCONTROLLERS` (completeness
only — **no GE game call sites**); `osMotorInit` stores `pfs->queue`,
`pfs->channel`, `pfs->activebank = 0xff` (PD `osMotorProbe` shape) and returns
0 for capable channels, `PFS_ERR_NOPACK` otherwise (no `PFS_MOTOR_INITIALIZED`
store — not defined in GE headers and `joy.c` never reads `pfs->status`);
`osMotorStart` → `inputRumble(channel, 1.0f, 5.0f)` (one generous window;
the game's `joyRumblePakTimer60` normally stops it, PD-style), `osMotorStop` →
`inputRumble(channel, 0.0f, 0.0f)`; both return 0 for non-NULL `pfs`.

**Verification.** `./build-pc.sh ntsc-final` clean (no new undefined/duplicate
symbols; `inputRumble*` defined once in input.c, declared in input.h).
**Real-pad haptics confirmed working by the user (2026-09-28)** — the gun-
impact rumble path is live on a real controller. The `RumbleScale=0`
silence check remains unverified (it is the trivial `gRumbleScale <= 0`
early-out in `inputRumble`). Same-session environment fix: the
recurring `Cannot create temporary file in C:\Windows\: Permission denied`
link failure is now self-healed by `build-pc.sh` (native-TMP probe →
cmake+ninja re-run under PowerShell with `TMP`/`TEMP` set natively; see the
AGENTS.md "Build" note), and `cannot open output file ... Permission denied`
is documented as the running-`.exe`-lock case (close the game before
rebuilding).

**Menu row (plan item 3, 2026-09-28).** The deferred options-menu slider
landed the day after the user confirmed the haptics, as a **single global
row** (user decision — "making the rumble menu just 1 player"):
`port/src/optionsoverlay.c` gains one `ROW_SLIDER` row — `Rumble strength`
on `Input.RumbleScale` (0.05 detents; the generic `CONFIG_OPT_FLOAT`
value/display path, shown `%.2f`) — placed in the INPUT section after the
other controller rows, with a `kResetDefaults` entry (0.5, the
`gRumbleScale` C initializer) so INPUT's "Reset to defaults" covers it.
The originally-coded four per-player rows (`Rumble (Player 1..4)` on
`Input.Player%d.RumbleScale`) were replaced before first compile; the
four ini keys are dropped rather than kept as hidden overrides (stale ones
self-clean on the next save, and a per-pad override would be a no-op since
`configSave` rewrites all registered keys — see `RUMBLE-PLAN.md` item 3).
Both the F10 overlay and the front options screen walk the shared `rows[]`
(`frontoptions.c` via the `optionsRow*` accessors), so the row appears in
both. A "row disabled when no pad is rumble-capable" state is NOT
implemented — the overlay has no generic row-disabled mechanism; the row is
inert (no-op) on unsupported/absent pads, and the disabled state is deferred
with the per-pad tuning row (`QOL-INVENTORY.md`), which will need the same
mechanism for deadzone/swap rows.

**Steam Deck confirmation (2026-09-27/28):** rumble works on the Deck's Steam Controller — `SDL_GameControllerRumble` fires on SteamOS/Steam Input with `Input.RumbleScale > 0` (user: "rumble works").
