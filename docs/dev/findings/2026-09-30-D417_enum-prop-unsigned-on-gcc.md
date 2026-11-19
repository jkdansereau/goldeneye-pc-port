# D417

<!-- from the old section-F/H table row -->
| D417 | **`enum PROP` unsigned on GCC: `prop >= 0` passed for `getPropForHeldItem()`'s -1 (fist) -> `modelLoad(-1)` null deref in any 2P+ stage (player.c `sub_GAME_7F09B398`)** — full `## D417` entry at file tail | RESOLVED (src/bondconstants.h `PROP__PORT_SIGNED = -1`, D3/D142 precedent; Rule-2 review done 2026-10-06: compiler-ABI class, no behaviour change). |

## D417 — `enum PROP` unsigned: fist held-prop lookup (-1) passes `>= 0` and crashes 2P+ (issue #99)

**Crash (gdb, 2026-09-29):** `modelLoad(modelid=-1)` <- `something_with_generating_object(propid=-1, itemid=ITEM_FIST)` <- `sub_GAME_7F09B398` (player.c) <- `gunTickHandState` <- `bondviewProcessInput`. Fault: `PitemZ_entries[-1].header->RootNode`, header NULL.

**Root cause:** `getPropForHeldItem()` returns -1 for items with no held model (fist, taser). `sub_GAME_7F09B398` guards with `if (prop >= 0)`; `PROP` is an all-non-negative enum, so GCC gives it an unsigned type and -1 passes. Solo never runs this path (only other players' visible weapons need it). Same class as porting-notes D3 / D142.

**Fix:** `PROP__PORT_SIGNED = -1` sentinel under `#ifdef PORT` at the head of `enum PROP` (`src/bondconstants.h`), following the `SP_LEVEL__PORT_SIGNED` precedent. `PROP_ALARM1` stays 0, sizeof unchanged, no stored value changes. Also corrects `drop_inventory` (`propid >= 0`). **This is a `src/game`-header edit: Rule-2 review owed.**

**Rule-2 review (2026-10-06, 3f behaviour audit):** not a behavioural change. IDO gives `enum PROP` a signed int type, so the decomp's `prop >= 0` test rejected -1 on the N64; GCC's unsigned choice for an all-non-negative enum is compiler-ABI drift (porting-notes §D3, D142, `SP_LEVEL__PORT_SIGNED` precedent). The unused sentinel restores IDO's signedness: no value, size or logic change, N64 semantics exact. Closed as ABI class; no sign-off needed.

**Status:** RESOLVED.
