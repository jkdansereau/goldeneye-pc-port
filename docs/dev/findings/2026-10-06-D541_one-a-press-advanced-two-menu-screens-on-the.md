# D541

<!-- from the old section-F/H table row -->
| D541 | **One A press advanced two menu screens on the Steam Deck (Desktop Mode) — cause unconfirmed (Steam desktop layout A=Enter or a pad reseat)** — Deck playtest; full `## D541` entry at file tail | PARTIAL 2026-10-05 (mitigation landed, port-only: 70 ms menu release hold-off in `input.c` + overlay accept; one back per 150 ms) — mitigation, cause unconfirmed; Deck re-test owed |

## D541: one A press advanced two menu screens on the Steam Deck (Desktop Mode) — two OR'd input sources for one press (Deck playtest, 2026-10-05)

**Report:** v0.5.0-rc on the Deck, launched in Desktop Mode — A "clicks forward
too fast" on file select, level select and the F10 / PC Options overlay
(maintainer: a double trigger from single quick presses).

**Cause: UNCONFIRMED.** The maintainer had switched Steam to controller
(gamepad) mode in Desktop Mode, which should disable the desktop key mapping
below, so that mapping is only one candidate; the other is a Steam virtual-pad
re-enumeration (`inputOpenPads` reseat) mid-press (not read). Tracer
(per-source A edges + pad add/remove) deferred by the maintainer. Candidate 1: outside a
Steam-launched game, Steam applies its Desktop Configuration to the Deck's
controls (A types Enter, B types Escape) while SDL also reads the raw pad. The
menu accept is an OR of pad A/X, Enter and the left mouse button
(`input.c` front-end mapping; overlay `accept`), so the two sources of one
press, a few ms out of step, gave a 1→0→1 level = two edges. In the overlay
Escape (key event → `s_backPending`) and the pad-B edge are separate back
paths, so one B could back out two pages. Ruled out (Sonnet triage, high
confidence): the `joyPoll` (scheduler thread) / `joyConsumeSamples` (game
thread) ring in `src/joy.c` — a clean edge lands in exactly one consume
window; the full-ring overwrite path can only drop a press, not repeat one.

**Fix (port-only, menus only):** a ~70 ms release hold-off on menu A/B/Start
in `inputComputePad` (applied before the GE_INPUTSCRIPT harness) and on the
overlay's `accept`; one back action per 150 ms across Esc and pad B. Press
edges are untouched, gameplay is unaffected. Covers candidate 1 and any
multi-source press; may not cover a reseat. **Owed:** Deck re-test in Desktop
Mode (and Game Mode) — one press = one screen; B backs out one page.
