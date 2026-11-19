# D213

<!-- from the old section-F/H table row -->
| D213 | **On-screen FPS readout — `Video.DisplayFPS` (M-83 QoL wave, port-only, PD parity).** — full `## D213` entry at file tail | FIXED (2026-09-26 bookkeeping: shipped; user reports it feels good in v0.4.0 play). Earlier: **LANDED, port-only.** Build clean; `bunker1` no-op confirmed. **Owed:** eyeball that the counter reads a sane ~30 and doesn't overlap the HUD. |

## D213 — On-screen FPS readout — `Video.DisplayFPS` (M-83 QoL wave, port-only, PD parity).

**On-screen FPS readout — `Video.DisplayFPS` (M-83 QoL wave, port-only, PD parity).** `port/src/optionsoverlay.c` samples frame time once per emitted frame (`fpsTick()` via `sysGetMicroseconds()`, ~0.5 s window) and, when enabled, emits a small top-right green "N FPS" DL even while the F10 panel is closed. Config-only knob (kept off the 13-row panel, which is at its documented layout limit — matches PD, whose `DisplayFPS` is also file-only). **Default 0 ⇒ `optionsOverlayEmit()` still returns NULL when the panel is closed ⇒ golden dumps byte-identical.**

**Status (from §F table):** **LANDED, port-only.** Build clean; `bunker1` no-op confirmed. **Owed:** eyeball that the counter reads a sane ~30 and doesn't overlap the HUD.
