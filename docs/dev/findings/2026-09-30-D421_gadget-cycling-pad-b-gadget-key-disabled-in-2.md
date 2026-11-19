# D421

<!-- from the old section-F/H table row -->
| D421 | **Gadget cycling (pad B / gadget key) disabled in 2+ player matches** — full `## D421` entry at file tail | CLOSED 2026-10-01: moot, no MP inventory holds a gadget (mp_weapon.c); hook reviewed + reverted. |

## D421 — Gadget cycling disabled in 2+ player matches

**CLOSED 2026-10-01: moot, no fix needed.** The pad-B cycle only selects items `ITEM_BOMBCASE..ITEM_WATCHMAGNETATTRACT` (mission gadgets). No MP weapon set (`src/game/mp_weapon.c`; every `INLINE_S_MP_WEAPON_SET` entry tallied) contains one: MP 'devices' are the timed/proximity/remote mines, which sit below `ITEM_BOMBCASE` and already cycle natively. So a match inventory never holds a gadget, and B would select nothing even with a working hook. A design review (Sonnet subagent, read-only, delegated by the maintainer) had approved a game-thread drain hook (`inputGadgetDrain()` inside `if (canCycleWeapons)` in `MoveBond`'s input step, bondview2.c ~6325, per-seat `SDL_atomic_t` one-shot, gated on alive/watch/lock/stop/game-over). It was implemented and built, then reverted unmerged once the empty gadget range was found. The early return in `inputCycleGadget()` stays. If a mode ever puts gadgets into an MP inventory, that design is the one to use. Side note from the review: solo's `inputCycleGadget()` also runs on the poll thread and mutates `hands[]` while the game thread may read it. No hang is possible in solo (`g_CurrentPlayer` never changes), but it is a latent data race; low priority.

`inputCycleGadget()` (port/src/input.c) acts on `g_CurrentPlayer` via game entry points with no player argument; calling it from the poll thread raced the game tick (D419 hang), so it now returns immediately in a match. Solo unchanged. Restoring it needs a game-thread hook (src/game edit, Rule-2 sign-off) or a native-button equivalent. Native N64 weapon cycling (C-buttons / pad Y) still works.
