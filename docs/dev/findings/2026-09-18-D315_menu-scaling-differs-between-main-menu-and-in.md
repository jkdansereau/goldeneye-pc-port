# D315

<!-- from the old section-F/H table row -->
| D315 | **Menu scaling differs between main menu and in-game — main menu scales OK, in-game menu "seems kinda big sometimes" (user report, v0.3.0 playtest build, 2026-09-18).** — full `## D315` entry at file tail | CLOSED (2026-09-26, user: menu scaling looks fine; likely resolved by the v0.4.0 widescreen/HUD-scale work D334/D335/D226). Earlier: OPEN — disclosed as a known issue, deferred to v0.3.1 (bigger design question, not a mechanical fix). |

## D315 — Menu scaling differs between main menu and in-game — main menu scales OK, in-game menu "seems kinda big sometimes" (user report, v0.3.0 playtest build, 2026-09-18).

**Reported:** "Menu scaling can change between main menu / game — main menu scales OK, game seems kinda big sometimes." The "sometimes" is the key word: it tracks some state (video mode? window size? level?) rather than being constant.

**Not investigated.** Pointers only:
- Check how the in-game overlay derives its scale factor vs the main-menu UI — if one uses a fixed design resolution and the other tracks the current render/window size, any video-mode change between contexts (resolution switch at level load) would produce exactly "fine on main menu, big in game".
- Widescreen FOV phase 4 landed recently and touches the in-game letterbox/FOV path — check whether it changed any shared scale constant the overlay reads. D246/D247 (overscan crop) is the adjacent video-mode work.
- Repro ask: which resolution/video mode, and does resizing the window change the in-game menu size live?

**Triage update (2026-09-18):** confirmed distinct from D316 (D316 was a click-accuracy bug in the overlay's *input* mapping; D315 is about the overlay's *rendered* size differing between contexts) — not a shared root cause. This is a bigger design question (whether/how the safe-area crop and the front-end/in-game logical-canvas size difference — 320x240 in-game vs 440x330 on front-end screens, `viSetXY` — should both feed into overlay scale) than a mechanical fix. Disclosing as a known issue and deferring to v0.3.1 rather than fixing in this pass.

**Status:** OPEN — disclosed as a known issue, deferred to v0.3.1. Lower urgency than D314/D316/D317.

**Current status (2026-09-29 bookkeeping):** the Status line above is historical; the authoritative verdict is the §F row — CLOSED (2026-09-26, user: menu scaling looks fine; likely resolved by the v0.4.0 widescreen/HUD-scale work D334/D335/D226). Earlier: OPEN — disclosed as a known issue, deferred to v0.3.1 (bigger design question, not a mechanical fix).
