# D419

<!-- from the old section-F/H table row -->
| D419 | **Split-screen input shared between players / P1 lost solo controls: poll thread used g_CurrentPlayer for per-slot aim/stance/style/use** — full `## D419` entry at file tail | RESOLVED (per-slot `slotPlayer()`, port-only; no current-player swaps from the poll thread; live-tested OK). |

## D419 — Split-screen input shared between players / P1 lost solo controls (issue #99)

**Symptom (user, live):** in a 2P match one player's input drove both views, and P1 keyboard/mouse or pad did not behave like solo (no mouse look, no SOLITARE layout, no crouch/use/reload).

**Root cause:** the slot plumbing (`contSnapshotFromKeyboard` polls 0..3, player N reads controller N) was already per-slot, but `inputComputePad` ran on the scheduler thread and read/wrote `g_CurrentPlayer` — whichever player the game thread last selected — for aim, stance, style and use/reload. The D416 stopgap disabled those paths in a match, which removed them from P1 too.

**Fix (port only, `port/src/input.c`):** `slotPlayer()` resolves the player that owns the slot being polled (`s_slot`, set by a thin `inputComputePad` wrapper); all player-struct access goes through it. In a 2+ player stage the poll thread NEVER calls a game entry point that acts on `g_CurrentPlayer`, and never re-points the current-player globals: control style is written straight onto the slot's player (mirror of `cur_player_set_control_type`), the crouch weapon check reads `p->hands[GUNRIGHT].weaponnum`, use/reload go out as the native B button, gadget cycling is solo-only. Crouch state is per slot; slots 1+ get pad A/X = B, stick click = crouch; slots 1+ use the N64 aim integrator (centred aim state is single-slot); `portMouseAimPdGetTurn` applies to player 0 only; unused slots return neutral.

**Lesson (first attempt hung the game):** the first version wrapped those calls in a save/swap/restore of `g_CurrentPlayer`/`player_num`/`g_playerPerm`. The poll thread is a real thread, not the game thread, and the style check ran the swap every poll. When the game thread did its own `set_cur_player` inside the window, the restore left it on the wrong player and `bondinvCycleForward` (bondinv.c:523) looped forever (circular list walked against another player's list head; kernel heartbeat dump showed mainThread spinning there after a respawn). Never swap the current-player globals from the poll thread. See porting-notes.

**Status:** RESOLVED (user live-tested M&K P1 + pad P2 and the kill/respawn sequence, 2026-09-29). Open: P2+ centred aim; gadget cycling in matches; MP score screen shows an odd big number (not investigated).
