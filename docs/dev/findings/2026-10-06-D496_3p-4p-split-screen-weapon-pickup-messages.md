# D496

<!-- from the old section-F/H table row -->
| D496 | **3P/4P split-screen: weapon-pickup messages garbled and oversized — `generate_language_specific_text_for_weapon` strcat'd onto an uninitialised stack buffer with 3+ players; `finalstring[0] = '\0'` under `AVOID_UB` (porting-notes D24 class)** — full `## D496` entry at file tail | FIXED 2026-10-03 (condensed 3+P text is faithful, maintainer-confirmed) |

## D496 — 3P/4P split-screen: weapon-pickup messages garbled and oversized (2026-10-03)

**Report:** maintainer, 3-player team game on Basement, natural weapon pickups.
**Cause:** `generate_language_specific_text_for_weapon` (`src/game/propobj.c`, US/EU path) writes `finalstring` only when fewer than 3 players, then strcat's onto it; the caller's buffer (`display_text_for_weapon_in_lower_left_corner`, `char acStack100[100]`) is an uninitialised stack array, so stack leftovers were drawn. Ammo pickups clear their buffer first and were fine. The N64 build has the same code; its stack contents happened to hide it.
**Fix (commit ba8c3942):** `finalstring[0] = '\0'` at the top of the function under `#ifdef AVOID_UB` (D490 / porting-notes D24 pattern), no other change. The same commit adds an optional third field to the GE_STARTMP harness (`src/game/lv.c`): "<players>,<mpStage>,<scenario>", e.g. 3,5,7 = Basement 2v1.
**Class:** porting-notes D24 (UB the N64 stack layout absorbed).
**Verified:** build only.
**Owed:** none.
**Status:** FIXED 2026-10-03 (uninitialised buffer); the condensed 3+P text itself is faithful.
**Maintainer play check 2026-10-03:** the condensed pickup text with 3+ players is the game's own: `prepare_ammo_type_collection_text` and this function drop "Picked up" when `getPlayerCount() >= 3`, so short 3P text is faithful (maintainer: fine). The uninitialised-buffer fix stands on its own.
