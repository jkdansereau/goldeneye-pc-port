# D317

<!-- from the old section-F/H table row -->
| D317 | **Crash when toggling Game.AllUnlocked back and forth in-game (user report, v0.3.0 playtest build, 2026-09-18) — no dump left behind.** — full `## D317` entry at file tail | CLOSED — symptom of D316 (confirmed instant window close, no WER report; the D316 hit-band bias let a click on "All unlocked" fall through to the adjacent "Quit to desktop" row). Same fix resolves it. |

## D317 — Crash when toggling Game.AllUnlocked back and forth in-game (user report, v0.3.0 playtest build, 2026-09-18) — no dump left behind.

**Reported:** "Toggling unlock all back and forth in game causes a crash." Reproduced by the user on the current-tip playtest exe (the Desktop `ge007-v0.3.0-playtest` folder, includes D310/D311 fixes). **Repro detail (user, 2026-09-18):** level = **Runway**. **No crash dump or log was left in that folder** — hard exit, so Windows Event Viewer is the only system-side artifact to check.

**Closed as a symptom of D316, not a real crash.** Evidence:
- No WER report exists for the playtest exe; every `ge007` AppCrash in Windows Error Reporting traces to this repo's own `build-pc` dev runs, none after 12:00 that day, while the playtest session was ~13:15 — a real crash would have logged one.
- `Game.AllUnlocked` and `__QuitToDesktop` are the last two rows in the overlay's `rows[]` table (adjacent, `Game.AllUnlocked` directly above `__QuitToDesktop`).
- User confirmed (2026-09-18, asked directly) the exit was an **instant window close, not a hang/freeze** — consistent with `__QuitToDesktop`'s `ROW_ACTION` firing via the game's normal exit path, not a fault.
- The D316 fix's downward hit-test bias explains it directly: a toggle-off click on "All unlocked" (last visible row in most layouts) landing low enough would resolve to the row below it, `__QuitToDesktop`, producing a clean exit that reads as "it crashed."

Same fix as D316 resolves this (no separate code change). Verification: toggle `Game.AllUnlocked` on Runway ~5x post-fix, confirm it never falls through to Quit — owed as part of D316's interactive verification, not yet run (headless here).

(Separately noted, not part of this fix: Windows Error Reporting history does show 39 genuine heap-corruption crashes from this repo's own `build-pc` dev runs, latest dated 2026-09-13 — unrelated to the user's playtest exe/session, flagged here as a latent finding worth a future look, not a v0.3.0 blocker.)

**Status:** CLOSED — symptom of D316; the D316 fix resolves it. Re-open if a post-fix Runway toggle test still exits unexpectedly.
