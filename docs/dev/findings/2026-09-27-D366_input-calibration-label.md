# D366

<!-- from the old section-F/H table row -->
| D366 | **Input calibration label: call 50 the default, not original (2026-09-27).** — full `## D366` entry at file tail | **CLOSED (2026-09-28).** Shipped in v0.4.0; accepted in the user's final review / campaign playtest. |

## D366 — Input slider calibration: “default,” not “original” (2026-09-27)

**Request:** user prefers the GEPD bundle as the standard for input and
wants the `(50 = original)` note to read `(50 = default)`.

**Fix:** changed both settings UIs' Input-header text to `(50 = default)`.
`calibratedDefault()` still maps the port's current *real* defaults
(`MouseSensitivity=100`, `PadDeadzone=7000`, `PadTriggerPct=23`) to 50/100,
and `kResetDefaults` agrees with `input.c`. No live config or save edited.
**Caveat:** GEPD's emulator bundle is an external reference; this checkout
has a stub in `reference/mouse-injector/` and its `GEPD-INPUT-PLAN.md` records
its camera aim formulas, but neither has an authoritative bundle controller
config for all three numbers. The port's 38/50 per-mode mouse speeds were
user-calibrated for its own FOV. Don't claim numeric parity with the GEPD
bundle or re-tune default input feel without the user's bundle configuration.

**Verification:** NTSC compile and staged link succeed. The active user's
playtest held `build-pc/ge007.x86_64.exe`, so the new binary was linked to
`scratch/d366-test/` and the local v040 launcher installs it only after the
running game exits. No stage smoke test while the user's save is active. The user later exited;
D367 includes the subsequent native stage smoke test of both changes.
