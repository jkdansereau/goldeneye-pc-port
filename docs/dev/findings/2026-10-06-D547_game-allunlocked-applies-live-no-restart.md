# D547

<!-- from the old section-F/H table row -->
| D547 | **Game.AllUnlocked applies live (no restart)** — F10 row seeds/clears the RAM unlock flags it owns; full `## D547` entry at file tail | FIXED 2026-10-05 (port-only, main.c/optionsoverlay.c; build-verified, maintainer mission-select check owed) |

## D547: Game.AllUnlocked applies live — FIXED 2026-10-05 (maintainer ask)

The file2.c query-time hooks (D442) already read `portAllUnlocked` live; only
the two RAM flags `debug_enable_all_levels_flag` / `debug_007_unlock_flag`
were boot-seeded. `portAllUnlockedApply()` (`port/src/main.c`) now runs at
boot and from `rowSetCommit` (F10 change and section reset): On sets each flag
that is clear and remembers it; Off clears only flags it set, so GE_UNLOCK_ALL
/ debug-menu unlocks survive. Row values are plain Off/On ("(restart)"
dropped). LEFTOVERDEBUG builds only, as before. Maintainer check owed: on at
file select -> mission select shows all levels + 007; off -> back to the save's
progress (screens refresh on re-entry).
