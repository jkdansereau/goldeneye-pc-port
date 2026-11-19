# D395

<!-- from the old section-F/H table row -->
| D395 | **Controller trapped in keyboard/mouse-only binding capture** — pad could enter the binding modal but could not finish or cancel without keyboard. | RESOLVED (v0.4.0 campaign sign-off 2026-09-28, user: pad capture-block + B-cancel confirmed on the physical pad). Both options UIs block pad-initiated capture; pad B can cancel an active modal; **Steam Deck retest 2026-09-27: a pad enters bindings and is not stuck — the "trapped" symptom is not reproduced on the Deck's Steam Controller**. Distinct from the still-open controller-rebinding *feature* — PR #109 / D383.) |

## D395 — Controller trapped in keyboard/mouse-only binding capture (2026-09-27).

Reconstructed stub — the original full entry was lost when `findings.md` was truncated to its section table (recovery, 2026-09-28); this entry is rebuilt from the section-table row and the `findings-index.csv` one-liner. Original root-cause detail is not recoverable.

**Status: PARTIAL (both options UIs block pad-initiated capture; pad B can cancel an active modal; NTSC build linked, controller-only retest owed)**: pad could enter the binding modal but could not finish or cancel without keyboard.

**Steam Deck retest (2026-09-27/28, Steam Controller):** a pad can go into the binding UI and is not left stuck — the original "trapped in capture" symptom is not reproduced. (This is distinct from the broader controller-rebinding *feature*, which still doesn't work — PR #109 / D383.)
