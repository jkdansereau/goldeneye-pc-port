# D373

<!-- from the old section-F/H table row -->
| D373 | **In-game crosshair on/off + colour (v0.4.0 modern options wave M2, 2026-09-27).** full `## D373` entry at file tail | FIXED (user live-tested; rule-2 sign-off recorded 2026-09-27): `Video.CrosshairHide` (0 = on, N64) + `Video.CrosshairColor` (7 tints, 0 = white) in F10 GRAPHICS; #ifdef PORT hook in gunfire.c gunDrawSight (the wave's only rule-2 item). |

## D373 — In-game crosshair on/off + colour (v0.4.0 modern options wave, M2) (2026-09-27)

**Spec:** `docs/dev/notes/MODERN-OPTIONS-PLAN.md` M2 — the wave's ONLY
rule-2 item (the port intentionally differs from N64 rendering when the
option is on; sign-off recorded 2026-09-27 via user direction to
implement + live test).

**Implementation:**
- `port/src/gunfire.c` `gunDrawSight`: `#ifdef PORT` guard only.
  `Video.CrosshairHide` → early return (no texSelect, no draw);
  otherwise `portCrosshairTint()` supplies the RGB args that the N64
  hardcodes to 0xFF,0xFF,0xFF (alpha 0x6E unchanged). Non-PORT builds
  keep the original literals byte-identical (local `extern`s inside the
  function, the bondview2 `portNoHitFlash` pattern; single consumer, no
  header declaration).
- `port/src/video.c`: `s32 portCrosshairHide` (non-static, D232
  pattern) + `portCrosshairTint()` — 7 named tints (index 0 = white =
  the N64 identity), `Game`-section registration `Video.CrosshairHide`
  (0/1) + `Video.CrosshairColor` (0..6).
- `port/src/optionsoverlay.c`: GRAPHICS rows "Crosshair" (kOnOffRev,
  0 = On) + "Crosshair colour" (kCrosshairColor) + kResetDefaults.

**Verification:** NTSC build clean. `GE_OPTIONSOVERLAY=4` (GRAPHICS
page) boots clean — both rows resolve. Headless level captures (dam,
`-level_33`) with hide/red-tint configs: no crash; the crosshair is
suppressed in cutscene frames (gunDrawSight only draws in live
gameplay), so the visual A/B was done by the user: **crosshair shows in
normal gameplay (user-confirmed 2026-09-27, "crosshair showed up")**.
Defaults (hide=0, colour=0) leave the render byte-identical to the
pre-wave tree. `betacrosshairimage` confirmed dead (plan §M2) — one
hook covers in-level rendering.
