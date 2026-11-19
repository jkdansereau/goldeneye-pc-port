# D372

<!-- from the old section-F/H table row -->
| D372 | **Low-end perf preset (v0.4.0 modern options wave M5, 2026-09-27).** full `## D372` entry at file tail | FIXED (headless-verified; awaiting user live accept): one-row `Video.LowEndMode` in F10 VIDEO; ON writes FpsCap 30 + MSAA x1 (restart), OFF restores compiled defaults (60/2). Registered key persists through orderly exit. |

## D372 — Low-end performance preset row (v0.4.0 modern options wave, M5) (2026-09-27)

**Spec:** `docs/dev/notes/MODERN-OPTIONS-PLAN.md` M5. One row in the
functional VIDEO section (no MODERN bucket — same D356 Turok-standard
decision as D371).

**Implementation (port-only; `port/src/video.c`, `port/src/optionsoverlay.c`):**
- `Video.LowEndMode` (0/1, default 0) registered in `videoConfigInit`. The
  key only records which preset the user last selected; the effect flows
  through the two real knobs.
- F10 row "Low-end mode (restart)" (VIDEO section, after Frame rate cap):
  ON → `rowSet(Video.FpsCap, 30)` + `rowSet(Video.MSAA, 1)`; OFF → 60 / 2
  (the compiled defaults). We do NOT remember per-user pre-preset values —
  the defaults are the N64-original setting, and a single well-known restore
  point is simpler and predictable (deviation from plan §M5's
  "pre-toggle values" wording; plan's parenthetical already notes the
  defaults are the expected pre-toggle state).
- FpsCap applies live (`videoRequestLiveConfig` via the row's own commit);
  MSAA is a restart row (applied at next GL context rebuild), hence the
  "(restart)" label.
- `kResetDefaults`: `Video.LowEndMode = 0`.

**Verification:** NTSC build clean. F10 VIDEO page (`GE_OPTIONSOVERLAY=6`)
boots clean — row resolves, no "not registered" warning. `LowEndMode=1`
survives an orderly `GE_QUITFRAME` exit. **Owed:** live F10 preset
toggle (ON→FpsCap/MSAA written, FPS readout drops to 30), restart-for-MSAA
check, PAL/JP.
