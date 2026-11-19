# D321

<!-- from the old section-F/H table row -->
| D321 | **D318 trigger chain fully mapped: tanks → combat bit is an authored ~3.5 s gas-cascade delay (chr 254 script); derail lands same-tick; PC behavior confirmed faithful to N64 (probe captures, 2026-09-20).** — full `## D321` entry at file tail | FIXED via D329 (2026-09-26 bookkeeping: tick-granularity root cause; Rule-2 fix in model.c, user-verified 2026-09-23). Earlier: OPEN — re-classified 2026-09-23 by D329 (tick-granularity divergence at 1 tick/frame, not a platform-invariant lock); fix decision pending, watchdog stays shipped. Earlier resolution text kept in the full entry. |

## D321 — D318 trigger chain fully mapped: tanks → combat bit is an authored ~3.5 s gas-cascade delay (chr 254 script); derail lands same-tick; PC behavior confirmed faithful to N64 (probe captures, 2026-09-20).

**2026-09-23 correction (D329):** the pin is a tick-granularity divergence, not a platform-invariant lock. It persists only at 1 sim tick per rendered frame (the port at 60 fps); a single frame of 2 or more ticks (GE's normal N64 regime) breaks it. Lockstep A/B (GE_D318B_CLK) on ai_19 and ai_9: clk=1 pins every time, clk=2 never confirms. Earlier WSL wall-clock pin rates are uncalibrated (mixed clk 1-6). See D329.

**Question.** User's N64 memory: Ourumov shoots Trevelyan the moment the tanks are blown up. PC playtests looked like he waited for the countdown to finish, then paused ~10 s, then shot. D318 covered the pin + watchdog; this entry closes the upstream question — *when and why does the combat bit `0x04` land, and is the PC trigger chain faithful?*

**Probes (all env-gated, port-only, capped, read-only; post-release removal candidates per M-192 item 7):**
- `GE_OBJT` in `chrSetStageFlags`/`chrUnsetStageFlags` (`chraction.c`): logs every low-16-bit write to `objectiveregisters1` with tick, writing chr, its actiontype/aioffset, and its AI-list ID (`chraiGetAIListID`).
- `GE_OBJT` in the `PROPDEF_GAS_RELEASING` destruction handler (`propobj.c`): timestamps each tank destruction.
- Existing `GE_D318T` timeline (`port/src/d318watchdog.c`) for correlation.

**Findings (three playtest captures, Facility `-level_34`):**
1. The level setup script (`UsetuparkZ.c`) never writes the low bits of `objectiveregisters1` — only character-action scripts can, via `chrSetStageFlags`. Writers observed: chr 67 (Trevelyan) sets `0x100`, `0x02`, and `0x20` (monologue); **chr 254** (`ACT_BONDMULTI`, aiid=0x1001, off=15 — a scripted gas-cascade entity) sets **`0x40` (flee) + `0x04` (combat) together, same tick**.
2. With tank-destruction timestamps: all 10 tanks destroyed t=7544–7634; the bits land t=7846 — **Δ = 212 ticks (~3.5 s) after the last tank**. This is a fixed ROM script delay in chr 254's cascade, identical data on the N64 — not a port-side lag.
3. The derail at ai_22 off=75 fires **on the same tick the bit lands** (run A: t=8807→8808; run B: t=7846→7847). While c78 waits in the monologue loop at off=75 the check is re-evaluated continuously (heartbeat-verified), so there is no additional polling latency.
4. Run B took the **natural escape**: Trevelyan died the same tick as the derail, zero `D318W` lines — i.e. "blow up the tanks → shot ~3.5 s later" is exactly what this build does, matching the user's N64 memory. Runs A and the original 09-18 report rolled the **pin branch** (random aim variant, D318 step 4), where the ~10 s felt by the user was the watchdog's 600-tick confirmation window breaking an anim pin the N64 would soft-lock on permanently.

**Conclusion.** No port bug; no `src/game` change warranted or needed. The perceived delay decomposes into (a) the authored ~3.5 s gas cascade + (b), only on the pin branch, the watchdog confirmation window. The PC trigger chain is faithful to the N64 end-to-end.

**Optional tuning (deferred, user's call).** `D318_DEADLOCK_TICKS` 600→300 would halve the worst-case pin-branch pause (10 s → 5 s); the margin over the natural escape (<2 s; run B: 0 ticks) stays ≥2.5×. One-line `#define` in `port/src/d318watchdog.c`; not done pre-ship to avoid re-validating the watchdog on the release branch.

**Status:** CLOSED — trigger chain mapped and faithful *at the script-data level* (the ~3.5 s cascade delay and same-tick derail are identical ROM data on both platforms); D318 watchdog validated in live play (fired + recovered in run A, correctly inert in run B). Caveat per D318's 2026-09-22 correction: "faithful end-to-end" does not establish that the derail window ever opens organically on N64 — that remains the open upstream-divergence-vs-unobserved question. Cross-ref: D318 (pin mechanism + watchdog + N64-manifestation correction), D320 (sweep of the same race in 8 levels), D263 (ai_22 mechanism map), M-192 (release review).
