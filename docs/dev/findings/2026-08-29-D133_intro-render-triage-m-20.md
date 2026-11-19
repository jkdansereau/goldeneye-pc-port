# D133

<!-- from the old section-F/H table row -->
| D133 | **intro render triage (M-20):** — full `## D133` entry at file tail | RESOLVED — not a regression, parked (D75/D76, `GRAPHICS-BACKLOG.md`) |

## D133 — intro render triage (M-20):

intro render triage (M-20): the "intro renders mostly black" item in the M-18/M-19 handoff is **NOT a regression** — it is the D75/D76 parked-cosmetic steady state. M-17 (`9ec6121e`), whose handoff claimed "the entire intro renders — logos → gun barrel → cast", was built in a scratch worktree and captured with the same `GE_PCDUMP="20-900:20"` window: coverage is **pixel-identical** to HEAD `0b5f5d1a` (legal screen 6677 non-clear px / 2.17% on both; centred-logo frames ~7% on both; near-black between). 2D/text/texture layers draw; animated character-model layers (Nintendo-logo transform, gun-barrel Bond, cast models) never appear = D75 exactly. No cheap regression to fix; parked. Secondary: Facility `-level_34` + Jungle `-level_37` re-verify FAILED this pass (boot crash / frame-2 hang) — flagged in LEVEL-STATUS for a clean-machine re-check, not investigated (scope).

**Status (from §F table):** RESOLVED — not a regression, parked (D75/D76, `GRAPHICS-BACKLOG.md`)
